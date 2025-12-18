/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fe-gtk.h"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.h"
#include "../common/util.h"
#include "../common/userlist.h"
#include "../common/modes.h"
#include "../common/text.h"
#include "../common/notify.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "palette.h"
#include "maingui.h"
#include "menu.h"
#include "pixmaps.h"
#include "userlistgui.h"
#include "fkeys.h"

/*
 * GTK4 Implementation using GListStore + GtkListView
 *
 * In GTK4, we use a GListStore containing HcUserItem objects instead of
 * GtkListStore. Each session still has its own model (sess->res->user_model).
 */

/* GObject to hold user list row data */
#define HC_TYPE_USER_ITEM (hc_user_item_get_type())
G_DECLARE_FINAL_TYPE (HcUserItem, hc_user_item, HC, USER_ITEM, GObject)

struct _HcUserItem {
	GObject parent;
	char *nick;				/* display nick (may include prefix if icons disabled) */
	char *hostname;			/* user's hostname */
	struct User *user;		/* pointer to backend User struct (not owned) */
	GdkTexture *icon;		/* user status icon (op, voice, etc.) - may be NULL */
	int color_index;		/* color index into colors[] array, 0 = no color */
};

G_DEFINE_TYPE (HcUserItem, hc_user_item, G_TYPE_OBJECT)

static void
hc_user_item_finalize (GObject *obj)
{
	HcUserItem *item = HC_USER_ITEM (obj);
	g_free (item->nick);
	g_free (item->hostname);
	g_clear_object (&item->icon);
	/* Note: item->user is not owned by us, don't free */
	G_OBJECT_CLASS (hc_user_item_parent_class)->finalize (obj);
}

static void
hc_user_item_class_init (HcUserItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_user_item_finalize;
}

static void
hc_user_item_init (HcUserItem *item)
{
	item->nick = NULL;
	item->hostname = NULL;
	item->user = NULL;
	item->icon = NULL;
	item->color_index = 0;
}

static HcUserItem *
hc_user_item_new (const char *nick, const char *hostname, struct User *user,
                  GdkPixbuf *pixbuf, int color_index)
{
	HcUserItem *item = g_object_new (HC_TYPE_USER_ITEM, NULL);
	item->nick = g_strdup (nick ? nick : "");
	item->hostname = g_strdup (hostname ? hostname : "");
	item->user = user;
	item->icon = pixbuf ? hc_pixbuf_to_texture (pixbuf) : NULL;
	item->color_index = color_index;
	return item;
}

/* Forward declaration for GTK4 selection list function */
static char **userlist_selection_list_gtk4 (GtkListView *view, int *num_ret);

GdkPixbuf *
get_user_icon (server *serv, struct User *user)
{
	char *pre;
	int level;

	if (!user)
		return NULL;

	/* these ones are hardcoded */
	switch (user->prefix[0])
	{
		case 0: return NULL;
		case '+': return pix_ulist_voice;
		case '%': return pix_ulist_halfop;
		case '@': return pix_ulist_op;
	}

	/* find out how many levels above Op this user is */
	pre = strchr (serv->nick_prefixes, '@');
	if (pre && pre != serv->nick_prefixes)
	{
		pre--;
		level = 0;
		while (1)
		{
			if (pre[0] == user->prefix[0])
			{
				switch (level)
				{
					case 0: return pix_ulist_owner;		/* 1 level above op */
					case 1: return pix_ulist_founder;	/* 2 levels above op */
					case 2: return pix_ulist_netop;		/* 3 levels above op */
				}
				break;	/* 4+, no icons */
			}
			level++;
			if (pre == serv->nick_prefixes)
				break;
			pre--;
		}
	}

	return NULL;
}

void
fe_userlist_numbers (session *sess)
{
	char tbuf[256];

	if (sess == current_tab || !sess->gui->is_tab)
	{
		if (sess->total)
		{
			g_snprintf (tbuf, sizeof (tbuf), _("%d ops, %d total"), sess->ops, sess->total);
			tbuf[sizeof (tbuf) - 1] = 0;
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), tbuf);
		} else
		{
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), NULL);
		}

		if (sess->type == SESS_CHANNEL && prefs.hex_gui_win_ucount)
			fe_set_title (sess);
	}
}

/* select a row in the userlist by nick-name */

void
userlist_select (session *sess, char *name)
{
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	GListModel *model;
	guint n_items, i;
	HcUserItem *item;

	if (!sel_model)
		return;

	model = gtk_selection_model_get_model (sel_model);
	n_items = g_list_model_get_n_items (model);

	for (i = 0; i < n_items; i++)
	{
		item = g_list_model_get_item (model, i);
		if (item && item->user && sess->server->p_cmp (item->user->nick, name) == 0)
		{
			/* Toggle selection */
			if (gtk_selection_model_is_selected (sel_model, i))
				gtk_selection_model_unselect_item (sel_model, i);
			else
				gtk_selection_model_select_item (sel_model, i, FALSE);

			g_object_unref (item);
			return;
		}
		if (item)
			g_object_unref (item);
	}
}

char **
userlist_selection_list (GtkWidget *widget, int *num_ret)
{
	/* GTK4: Use the list view version */
	return userlist_selection_list_gtk4 (GTK_LIST_VIEW (widget), num_ret);
}

void
fe_userlist_set_selected (struct session *sess)
{
	GListStore *store = sess->res->user_model;
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	GListModel *list_model;
	guint n_items, i;
	HcUserItem *item;

	if (!sel_model)
		return;

	/* Get the underlying model - need to check if it's the same as our store */
	list_model = gtk_selection_model_get_model (sel_model);

	/* If we're using a sort model, we need to get the underlying store */
	if (GTK_IS_SORT_LIST_MODEL (list_model))
		list_model = gtk_sort_list_model_get_model (GTK_SORT_LIST_MODEL (list_model));

	if (G_LIST_MODEL (store) != list_model)
		return;

	n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
	for (i = 0; i < n_items; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (item && item->user)
		{
			item->user->selected = gtk_selection_model_is_selected (sel_model, i) ? 1 : 0;
		}
		if (item)
			g_object_unref (item);
	}
}

/*
 * GTK4: Find position of user in GListStore and return selection status
 */
static gboolean
find_row_gtk4 (GListStore *store, struct User *user, guint *position, int *selected,
               GtkSelectionModel *sel_model)
{
	guint n_items, i;
	HcUserItem *item;

	*selected = FALSE;
	n_items = g_list_model_get_n_items (G_LIST_MODEL (store));

	for (i = 0; i < n_items; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (item && item->user == user)
		{
			*position = i;
			if (sel_model)
				*selected = gtk_selection_model_is_selected (sel_model, i);
			g_object_unref (item);
			return TRUE;
		}
		if (item)
			g_object_unref (item);
	}

	return FALSE;
}

void
userlist_set_value (GtkWidget *view, gfloat val)
{
	/* GTK4: GtkListView is inside a GtkScrolledWindow */
	GtkWidget *parent = gtk_widget_get_parent (view);
	if (GTK_IS_SCROLLED_WINDOW (parent))
	{
		GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (parent));
		gtk_adjustment_set_value (adj, val);
	}
}

gfloat
userlist_get_value (GtkWidget *view)
{
	GtkWidget *parent = gtk_widget_get_parent (view);
	if (GTK_IS_SCROLLED_WINDOW (parent))
	{
		GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (parent));
		return gtk_adjustment_get_value (adj);
	}
	return 0.0f;
}

int
fe_userlist_remove (session *sess, struct User *user)
{
	GListStore *store = sess->res->user_model;
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	guint position;
	int sel;

	if (!find_row_gtk4 (store, user, &position, &sel, sel_model))
		return 0;

	g_list_store_remove (store, position);

	return sel;
}

void
fe_userlist_rehash (session *sess, struct User *user)
{
	GListStore *store = sess->res->user_model;
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	guint position;
	int sel;
	int nick_color = 0;
	HcUserItem *item;

	if (!find_row_gtk4 (store, user, &position, &sel, sel_model))
		return;

	if (prefs.hex_away_track && user->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(user->nick);

	/* Get the item and update its fields */
	item = g_list_model_get_item (G_LIST_MODEL (store), position);
	if (item)
	{
		g_free (item->hostname);
		item->hostname = g_strdup (user->hostname);
		item->color_index = nick_color;
		g_object_unref (item);

		/* Notify the model that the item changed so the view updates */
		g_list_store_remove (store, position);
		item = hc_user_item_new (user->nick, user->hostname, user,
		                         get_user_icon (sess->server, user), nick_color);
		g_list_store_insert (store, position, item);
		g_object_unref (item);
	}
}

void
fe_userlist_insert (session *sess, struct User *newuser, gboolean sel)
{
	GListStore *store = sess->res->user_model;
	GdkPixbuf *pix = get_user_icon (sess->server, newuser);
	HcUserItem *item;
	char *nick;
	int nick_color = 0;

	if (prefs.hex_away_track && newuser->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(newuser->nick);

	nick = newuser->nick;
	if (!prefs.hex_gui_ulist_icons)
	{
		nick = g_malloc (strlen (newuser->nick) + 2);
		nick[0] = newuser->prefix[0];
		if (nick[0] == '\0' || nick[0] == ' ')
			strcpy (nick, newuser->nick);
		else
			strcpy (nick + 1, newuser->nick);
		pix = NULL;
	}

	item = hc_user_item_new (nick, newuser->hostname, newuser, pix, nick_color);
	g_list_store_append (store, item);
	g_object_unref (item);

	if (!prefs.hex_gui_ulist_icons)
	{
		g_free (nick);
	}

	/* is it me? */
	if (newuser->me && sess->gui->nick_box)
	{
		if (!sess->gui->is_tab || sess == current_tab)
			mg_set_access_icon (sess->gui, pix, sess->server->is_away);
	}

	/* Select the new item if requested */
	if (sel)
	{
		GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
		GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
		if (sel_model)
		{
			guint n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
			if (n_items > 0)
				gtk_selection_model_select_item (sel_model, n_items - 1, FALSE);
		}
	}
}

void
fe_userlist_clear (session *sess)
{
	g_list_store_remove_all (sess->res->user_model);
}

/*
 * GTK4: File drop handler for userlist - drops file on the selected user
 *
 * Note: GtkListView doesn't have get_path_at_pos like GtkTreeView, so we
 * use the currently selected item instead of determining the row under cursor.
 * This is a simpler UX: user selects target, then drops file.
 */
static gboolean
userlist_file_drop_cb (GtkDropTarget *target, const GValue *value,
                       double x, double y, gpointer user_data)
{
	GtkWidget *view = user_data;
	GtkListView *list_view = GTK_LIST_VIEW (view);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (list_view);
	GFile *file;
	char *uri;
	HcUserItem *item;

	(void)target;
	(void)x;
	(void)y;

	if (!G_VALUE_HOLDS (value, G_TYPE_FILE))
		return FALSE;

	file = g_value_get_object (value);
	if (!file)
		return FALSE;

	/* Get currently selected user */
	item = hc_selection_model_get_selected_item (sel_model);
	if (!item || !item->user)
	{
		if (item)
			g_object_unref (item);
		return FALSE;
	}

	uri = g_file_get_uri (file);
	if (uri)
	{
		mg_dnd_drop_file (current_sess, item->user->nick, uri);
		g_free (uri);
	}

	g_object_unref (item);
	return TRUE;
}

/* GTK4: Signal that we accept drops */
static GdkDragAction
userlist_drop_motion_cb (GtkDropTarget *target, double x, double y, gpointer user_data)
{
	(void)target;
	(void)x;
	(void)y;
	(void)user_data;

	/* Simply indicate we can accept the drop */
	return GDK_ACTION_COPY;
}

/* GTK4: Clear selection when drag leaves */
static void
userlist_drop_leave_cb (GtkDropTarget *target, gpointer user_data)
{
	(void)target;
	(void)user_data;
	/* Nothing needed for GtkListView - selection is handled differently */
}

/*
 * GTK4 sorting comparison functions for GtkCustomSorter
 */
static int
userlist_alpha_cmp_gtk4 (gconstpointer a, gconstpointer b, gpointer userdata)
{
	HcUserItem *item_a = HC_USER_ITEM ((gpointer)a);
	HcUserItem *item_b = HC_USER_ITEM ((gpointer)b);

	return nick_cmp_alpha (item_a->user, item_b->user, ((session*)userdata)->server);
}

static int
userlist_ops_cmp_gtk4 (gconstpointer a, gconstpointer b, gpointer userdata)
{
	HcUserItem *item_a = HC_USER_ITEM ((gpointer)a);
	HcUserItem *item_b = HC_USER_ITEM ((gpointer)b);

	return nick_cmp_az_ops (((session*)userdata)->server, item_a->user, item_b->user);
}

GListStore *
userlist_create_model (session *sess)
{
	GListStore *store;

	store = g_list_store_new (HC_TYPE_USER_ITEM);

	/* Sorting is handled by GtkSortListModel wrapped around this store
	 * when the view is created. The store itself is unsorted. */

	return store;
}

/*
 * GTK4 Factory callbacks for GtkListView
 */

/*
 * Helper to update nick label color based on selection state
 * When selected, clears any nick color so CSS selection color applies.
 * When not selected, applies the user's nick color if set.
 */
static void
userlist_update_nick_color (GtkListItem *item)
{
	GtkWidget *hbox, *child, *nick_label = NULL;
	HcUserItem *user_item;
	gboolean selected;

	hbox = gtk_list_item_get_child (item);
	user_item = gtk_list_item_get_item (item);
	selected = gtk_list_item_get_selected (item);

	if (!hbox || !user_item)
		return;

	/* Find nick label */
	for (child = gtk_widget_get_first_child (hbox); child; child = gtk_widget_get_next_sibling (child))
	{
		if (g_strcmp0 (gtk_widget_get_name (child), "userlist-nick") == 0)
		{
			nick_label = child;
			break;
		}
	}

	if (!nick_label)
		return;

	/* When selected, clear attributes so CSS selection color applies.
	 * When not selected, apply nick color if set. */
	if (selected)
	{
		gtk_label_set_attributes (GTK_LABEL (nick_label), NULL);
	}
	else if (user_item->color_index > 0)
	{
		GdkRGBA *color = &colors[user_item->color_index];
		PangoAttrList *attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_foreground_new (
			(guint16)(color->red * 65535),
			(guint16)(color->green * 65535),
			(guint16)(color->blue * 65535)));
		gtk_label_set_attributes (GTK_LABEL (nick_label), attrs);
		pango_attr_list_unref (attrs);
	}
	else
	{
		gtk_label_set_attributes (GTK_LABEL (nick_label), NULL);
	}
}

/*
 * Signal handler for selection state changes on userlist rows
 */
static void
userlist_selection_changed_cb (GtkListItem *item, GParamSpec *pspec, gpointer user_data)
{
	userlist_update_nick_color (item);
}

/*
 * GtkListView row setup - creates a horizontal box containing icon, nick, and host.
 */
static void
userlist_setup_row_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *hbox, *picture, *nick_label, *host_label;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

	/* Icon (only if enabled) */
	if (prefs.hex_gui_ulist_icons)
	{
		picture = gtk_picture_new ();
		gtk_picture_set_content_fit (GTK_PICTURE (picture), GTK_CONTENT_FIT_SCALE_DOWN);
		gtk_widget_set_size_request (picture, 16, -1);
		if (prefs.hex_gui_compact)
			gtk_widget_set_margin_top (picture, 0);
		gtk_widget_set_name (picture, "userlist-icon");
		gtk_box_append (GTK_BOX (hbox), picture);
	}

	/* Nick label */
	nick_label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (nick_label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (nick_label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (nick_label, TRUE);
	if (prefs.hex_gui_compact)
		gtk_widget_set_margin_top (nick_label, 0);
	gtk_widget_set_name (nick_label, "userlist-nick");
	gtk_box_append (GTK_BOX (hbox), nick_label);

	/* Host label (only if enabled) */
	if (prefs.hex_gui_ulist_show_hosts)
	{
		host_label = gtk_label_new (NULL);
		gtk_label_set_xalign (GTK_LABEL (host_label), 0.0);
		gtk_label_set_ellipsize (GTK_LABEL (host_label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand (host_label, TRUE);
		if (prefs.hex_gui_compact)
			gtk_widget_set_margin_top (host_label, 0);
		gtk_widget_set_name (host_label, "userlist-host");
		gtk_box_append (GTK_BOX (hbox), host_label);
	}

	gtk_list_item_set_child (item, hbox);

	/* Connect to selection changes to update nick color */
	g_signal_connect (item, "notify::selected", G_CALLBACK (userlist_selection_changed_cb), NULL);
}

/*
 * GtkListView row bind - populates icon, nick, and host from HcUserItem
 */
static void
userlist_bind_row_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *hbox = gtk_list_item_get_child (item);
	HcUserItem *user_item = gtk_list_item_get_item (item);
	GtkWidget *child;
	GtkWidget *picture = NULL;
	GtkWidget *nick_label = NULL;
	GtkWidget *host_label = NULL;

	if (!user_item)
		return;

	/* Store reference to item on the hbox for position lookup during click handling */
	g_object_set_data (G_OBJECT (hbox), "hc-user-item", user_item);

	/* Find the child widgets by name */
	for (child = gtk_widget_get_first_child (hbox); child; child = gtk_widget_get_next_sibling (child))
	{
		const char *name = gtk_widget_get_name (child);
		if (g_strcmp0 (name, "userlist-icon") == 0)
			picture = child;
		else if (g_strcmp0 (name, "userlist-nick") == 0)
			nick_label = child;
		else if (g_strcmp0 (name, "userlist-host") == 0)
			host_label = child;
	}

	/* Set icon */
	if (picture)
	{
		if (user_item->icon)
			gtk_picture_set_paintable (GTK_PICTURE (picture), GDK_PAINTABLE (user_item->icon));
		else
			gtk_picture_set_paintable (GTK_PICTURE (picture), NULL);
	}

	/* Set nick text */
	if (nick_label)
	{
		gtk_label_set_text (GTK_LABEL (nick_label), user_item->nick ? user_item->nick : "");
	}

	/* Set nick color (respects selection state) */
	userlist_update_nick_color (item);

	/* Set host */
	if (host_label)
	{
		gtk_label_set_text (GTK_LABEL (host_label), user_item->hostname ? user_item->hostname : "");
	}
}

/*
 * GTK4 version of userlist_selection_list
 * Gets selected nicks from GtkListView's selection model
 */
static char **
userlist_selection_list_gtk4 (GtkListView *view, int *num_ret)
{
	GtkSelectionModel *sel_model;
	GtkBitset *selection;
	GtkBitsetIter iter;
	guint position;
	gboolean valid;
	int num_sel, i;
	char **nicks;
	GListModel *model;
	HcUserItem *item;

	*num_ret = 0;
	sel_model = gtk_list_view_get_model (view);
	if (!sel_model)
		return NULL;

	selection = gtk_selection_model_get_selection (sel_model);
	num_sel = gtk_bitset_get_size (selection);

	if (num_sel < 1)
	{
		gtk_bitset_unref (selection);
		return NULL;
	}

	nicks = g_new (char *, num_sel + 1);

	/* Get the underlying base model (not the sort model) */
	model = gtk_selection_model_get_model (sel_model);

	i = 0;
	valid = gtk_bitset_iter_init_first (&iter, selection, &position);
	while (valid && i < num_sel)
	{
		item = g_list_model_get_item (model, position);
		if (item && item->user)
		{
			nicks[i] = g_strdup (item->user->nick);
			i++;
		}
		if (item)
			g_object_unref (item);
		valid = gtk_bitset_iter_next (&iter, &position);
	}
	nicks[i] = NULL;
	gtk_bitset_unref (selection);

	*num_ret = i;
	return nicks;
}

/*
 * Helper to find position at coordinates in GtkListView
 * Returns the position or GTK_INVALID_LIST_POSITION if not found.
 * Uses gtk_widget_pick to find the widget at coordinates.
 */
static guint
userlist_get_position_at_coords (GtkListView *view, double x, double y)
{
	GtkWidget *child;
	GtkWidget *widget;
	GtkSelectionModel *sel_model;
	GListModel *model;
	guint n_items;

	/* Pick the widget at the given coordinates */
	child = gtk_widget_pick (GTK_WIDGET (view), x, y, GTK_PICK_DEFAULT);
	if (!child)
		return GTK_INVALID_LIST_POSITION;

	/* Walk up from picked widget to find the GtkListItem's row container.
	 * Each row in GtkListView has a child managed by the factory.
	 * We need to find which position this corresponds to. */
	widget = child;
	while (widget != NULL && widget != GTK_WIDGET (view))
	{
		/* Check if this widget's parent is directly the view's internal container.
		 * The GtkListItem widgets are direct children of the listview's internal layout. */
		GtkWidget *parent = gtk_widget_get_parent (widget);
		if (parent != NULL)
		{
			GtkWidget *grandparent = gtk_widget_get_parent (parent);
			if (grandparent == GTK_WIDGET (view) || grandparent == NULL)
			{
				/* 'widget' or 'parent' is a row - try to find its index by position comparison */
				break;
			}
		}
		widget = parent;
	}

	/* Fallback: Use the allocation/position approach.
	 * Get the model and iterate to find which row contains the y coordinate. */
	sel_model = gtk_list_view_get_model (view);
	if (!sel_model)
		return GTK_INVALID_LIST_POSITION;

	model = gtk_selection_model_get_model (sel_model);
	n_items = g_list_model_get_n_items (model);

	/* For a simple list view, we can estimate position based on row height.
	 * However, this is inexact. A more reliable approach is to use the
	 * scrolled window adjustment and row height estimation.
	 *
	 * For now, use a heuristic: walk through visible items and check bounds.
	 * Since this is called in response to a click, the clicked item should be visible.
	 */

	/* Use gtk_widget_pick more aggressively - the child we picked should be
	 * part of a GtkListItem. Walk up to find data we can use. */
	child = gtk_widget_pick (GTK_WIDGET (view), x, y, GTK_PICK_DEFAULT);
	widget = child;

	/* Look for the GtkListItem by checking CSS name */
	while (widget != NULL && widget != GTK_WIDGET (view))
	{
		const char *name = gtk_widget_get_name (widget);
		/* GtkListView uses an internal row structure. We need to find the position
		 * by checking the model's items against what's rendered at this position.
		 * Since GtkListView doesn't expose position directly, use the approach of
		 * extracting data from the widget and matching it. */

		/* Try getting the user data from the widget's first child (our row box) */
		GtkWidget *check = widget;
		while (check && !GTK_IS_BOX (check))
			check = gtk_widget_get_first_child (check);

		if (GTK_IS_BOX (check))
		{
			/* Found our row box - the user data is attached via factory bind */
			HcUserItem *item = g_object_get_data (G_OBJECT (check), "hc-user-item");
			if (item && item->user)
			{
				/* Found the item - now find its position in the model */
				for (guint i = 0; i < n_items; i++)
				{
					HcUserItem *model_item = g_list_model_get_item (model, i);
					if (model_item == item)
					{
						g_object_unref (model_item);
						return i;
					}
					if (model_item)
						g_object_unref (model_item);
				}
			}
		}
		widget = gtk_widget_get_parent (widget);
	}

	return GTK_INVALID_LIST_POSITION;
}

/*
 * Left-click handler for userlist (released signal).
 * Handles double-click command execution.
 */
static void
userlist_left_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkListView *view = GTK_LIST_VIEW (widget);
	GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
	char **nicks;
	int i;

	if (!(state & GDK_CONTROL_MASK) &&
		n_press == 2 && prefs.hex_gui_ulist_doubleclick[0])
	{
		nicks = userlist_selection_list_gtk4 (view, &i);
		if (nicks)
		{
			nick_command_parse (current_sess, prefs.hex_gui_ulist_doubleclick, nicks[0],
									  nicks[0]);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
		}
	}
}

/*
 * Right-click handler for userlist.
 * Selects the clicked item (if clicking on one), then shows context menu.
 */
static void
userlist_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkListView *view = GTK_LIST_VIEW (widget);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
	char **nicks;
	int i;
	guint clicked_pos;

	/* Check for multi-selection (Ctrl held) */
	if (state & GDK_CONTROL_MASK)
	{
		/* With Ctrl held, add to selection rather than replacing */
		nicks = userlist_selection_list_gtk4 (view, &i);
		if (nicks && i > 0)
		{
			menu_nickmenu (current_sess, widget, x, y, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
		}
		return;
	}

	/* Find which row was clicked */
	clicked_pos = userlist_get_position_at_coords (view, x, y);

	if (clicked_pos != GTK_INVALID_LIST_POSITION)
	{
		/* Select the clicked item (standard behavior: right-click selects) */
		gtk_selection_model_select_item (sel_model, clicked_pos, TRUE);
	}

	/* Now get the selection and show menu */
	nicks = userlist_selection_list_gtk4 (view, &i);
	if (nicks && i > 0)
	{
		menu_nickmenu (current_sess, widget, x, y, nicks[0], i);
		while (i)
		{
			i--;
			g_free (nicks[i]);
		}
		g_free (nicks);
	}
	else if (clicked_pos == GTK_INVALID_LIST_POSITION)
	{
		/* Clicked on empty area - clear selection */
		gtk_selection_model_unselect_all (sel_model);
	}
}

/*
 * Key handler for userlist - forwards printable keys to input box
 */
static gboolean
userlist_key_cb (GtkEventControllerKey *controller, guint keyval,
                 guint keycode, GdkModifierType state, gpointer userdata)
{
	if (keyval >= GDK_KEY_asterisk && keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		/* GTK4: Cannot forward events directly, insert the character instead */
		if (keyval >= GDK_KEY_space && keyval <= GDK_KEY_asciitilde)
		{
			char buf[2] = { (char)keyval, 0 };
			int pos = -1;
			gtk_editable_insert_text (GTK_EDITABLE (current_sess->gui->input_box), buf, 1, &pos);
		}
		return TRUE;
	}

	return FALSE;
}

/*
 * GTK4 version: Create GtkListView (no headers, unlike GtkColumnView)
 */
GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *view;
	GtkListItemFactory *factory;
	GtkDropTarget *drop_target;

	sw = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
											  prefs.hex_gui_ulist_show_hosts ?
												GTK_POLICY_AUTOMATIC :
												GTK_POLICY_NEVER,
											  GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (sw, TRUE);
	hc_box_pack_start (box, sw, TRUE, TRUE, 0);
	hc_widget_show (sw);

	/* Create list view with single-row factory - model will be set later in userlist_show() */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (userlist_setup_row_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (userlist_bind_row_cb), NULL);

	view = gtk_list_view_new (NULL, factory);
	gtk_widget_set_name (view, "hexchat-userlist");
	gtk_widget_set_can_focus (view, FALSE);

	/* DND: File drops for DCC (drop file on user to send) */
	drop_target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (drop_target, "drop", G_CALLBACK (userlist_file_drop_cb), view);
	g_signal_connect (drop_target, "motion", G_CALLBACK (userlist_drop_motion_cb), view);
	g_signal_connect (drop_target, "leave", G_CALLBACK (userlist_drop_leave_cb), view);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (drop_target));

	/* Layout swapping drag source (drag userlist to reposition) */
	mg_setup_userlist_drag_source (view);

	/* Event controllers for click and key events */
	/* Left-click gesture for double-click handling (use "released" for reliable detection) */
	{
		GtkGesture *gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1); /* Left-click only */
		g_signal_connect (gesture, "released", G_CALLBACK (userlist_left_click_cb), NULL);
		gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));
	}
	/* Right-click gesture for context menu (use "pressed" for immediate response) */
	{
		GtkGesture *gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 3); /* Right-click only */
		g_signal_connect (gesture, "pressed", G_CALLBACK (userlist_right_click_cb), NULL);
		gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));
	}
	hc_add_key_controller (view, G_CALLBACK (userlist_key_cb), NULL, NULL);

	hc_scrolled_window_set_child (sw, view);
	hc_widget_show (view);

	return view;
}

/*
 * GTK4: Connect the session's model to the GtkListView with sorting
 */
void
userlist_show (session *sess)
{
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GListStore *store = sess->res->user_model;
	GtkSortListModel *sort_model;
	GtkMultiSelection *sel_model;
	GtkCustomSorter *sorter = NULL;
	GCompareDataFunc cmp_func = NULL;
	gboolean reversed = FALSE;

	/* Determine sort function based on prefs */
	switch (prefs.hex_gui_ulist_sort)
	{
	case 0:
		cmp_func = userlist_ops_cmp_gtk4;
		reversed = FALSE;
		break;
	case 1:
		cmp_func = userlist_alpha_cmp_gtk4;
		reversed = FALSE;
		break;
	case 2:
		cmp_func = userlist_ops_cmp_gtk4;
		reversed = TRUE;
		break;
	case 3:
		cmp_func = userlist_alpha_cmp_gtk4;
		reversed = TRUE;
		break;
	default:
		/* No sorting */
		break;
	}

	/* Create sorter if needed */
	if (cmp_func)
	{
		sorter = gtk_custom_sorter_new (cmp_func, sess, NULL);
		if (reversed)
		{
			/* GTK4 doesn't have a direct "reversed" option for custom sorters,
			 * but we can wrap the comparison. For simplicity, we'll just negate
			 * the result in the compare functions if needed. For now, sorting
			 * will be ascending only - proper descending would need wrapper. */
		}
	}

	/* Create sorted model wrapping the store */
	sort_model = gtk_sort_list_model_new (G_LIST_MODEL (g_object_ref (store)),
	                                       sorter ? GTK_SORTER (sorter) : NULL);

	/* Create multi-selection model for the list view */
	sel_model = gtk_multi_selection_new (G_LIST_MODEL (sort_model));

	/* Set the model on the list view */
	gtk_list_view_set_model (view, GTK_SELECTION_MODEL (sel_model));

	/* We don't unref sel_model/sort_model - the list view takes ownership */
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	GtkListView *view = GTK_LIST_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_list_view_get_model (view);
	GListModel *model;
	guint n_items, i;
	HcUserItem *item;
	int thisname;
	char *name;

	(void)scroll_to; /* TODO: Implement scroll_to for GtkListView */

	if (!sel_model)
		return;

	model = gtk_selection_model_get_model (sel_model);
	n_items = g_list_model_get_n_items (model);

	if (do_clear)
		gtk_selection_model_unselect_all (sel_model);

	for (i = 0; i < n_items; i++)
	{
		if (*word[0])
		{
			item = g_list_model_get_item (model, i);
			if (item && item->user)
			{
				thisname = 0;
				while (*(name = word[thisname++]))
				{
					if (sess->server->p_cmp (item->user->nick, name) == 0)
					{
						gtk_selection_model_select_item (sel_model, i, FALSE);
						break;
					}
				}
			}
			if (item)
				g_object_unref (item);
		}
	}
}
