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

#if !HC_GTK4
enum
{
	COL_PIX=0,		/* GdkPixbuf * */
	COL_NICK=1,		/* char * */
	COL_HOST=2,		/* char * */
	COL_USER=3,		/* struct User * */
	COL_GDKCOLOR=4	/* GdkRGBA * - foreground color */
};
#endif

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
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

#endif /* HC_GTK4 */

#if HC_GTK4
/* Forward declaration for GTK4 selection list function */
static char **userlist_selection_list_gtk4 (GtkColumnView *view, int *num_ret);
#endif

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

#if !HC_GTK4
static void
scroll_to_iter (GtkTreeIter *iter, GtkTreeView *treeview, GtkTreeModel *model)
{
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);
	if (path)
	{
		gtk_tree_view_scroll_to_cell (treeview, path, NULL, TRUE, 0.5, 0.5);
		gtk_tree_path_free (path);
	}
}
#endif

/* select a row in the userlist by nick-name */

#if HC_GTK4
void
userlist_select (session *sess, char *name)
{
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
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
#else /* GTK3 */
void
userlist_select (session *sess, char *name)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (sess->server->p_cmp (row_user->nick, name) == 0)
			{
				if (gtk_tree_selection_iter_is_selected (selection, &iter))
					gtk_tree_selection_unselect_iter (selection, &iter);
				else
					gtk_tree_selection_select_iter (selection, &iter);

				/* and make sure it's visible */
				scroll_to_iter (&iter, treeview, model);
				return;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}
#endif

#if HC_GTK4
char **
userlist_selection_list (GtkWidget *widget, int *num_ret)
{
	/* GTK4: Use the column view version */
	return userlist_selection_list_gtk4 (GTK_COLUMN_VIEW (widget), num_ret);
}
#else /* GTK3 */
char **
userlist_selection_list (GtkWidget *widget, int *num_ret)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = (GtkTreeView *) widget;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	struct User *user;
	int i, num_sel;
	char **nicks;

	*num_ret = 0;
	/* first, count the number of selections */
	num_sel = 0;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				num_sel++;
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	if (num_sel < 1)
		return NULL;

	nicks = g_new (char *, num_sel + 1);

	i = 0;
	gtk_tree_model_get_iter_first (model, &iter);
	do
	{
		if (gtk_tree_selection_iter_is_selected (selection, &iter))
		{
			gtk_tree_model_get (model, &iter, COL_USER, &user, -1);
			nicks[i] = g_strdup (user->nick);
			i++;
			nicks[i] = NULL;
		}
	}
	while (gtk_tree_model_iter_next (model, &iter));

	*num_ret = i;
	return nicks;
}
#endif

#if HC_GTK4
void
fe_userlist_set_selected (struct session *sess)
{
	GListStore *store = sess->res->user_model;
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
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
	/* GTK4: GtkColumnView is inside a GtkScrolledWindow */
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
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
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
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
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
		GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
		GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
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

#else /* GTK3 */

void
fe_userlist_set_selected (struct session *sess)
{
	GtkListStore *store = sess->res->user_model;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sess->gui->user_tree));
	GtkTreeIter iter;
	struct User *user;

	/* if it's not front-most tab it doesn't own the GtkTreeView! */
	if (store != (GtkListStore*) gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree)))
		return;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, COL_USER, &user, -1);

			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				user->selected = 1;
			else
				user->selected = 0;

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));
	}
}

static GtkTreeIter *
find_row (GtkTreeView *treeview, GtkTreeModel *model, struct User *user,
			 int *selected)
{
	static GtkTreeIter iter;
	struct User *row_user;

	*selected = FALSE;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (row_user == user)
			{
				if (gtk_tree_view_get_model (treeview) == model)
				{
					if (gtk_tree_selection_iter_is_selected (gtk_tree_view_get_selection (treeview), &iter))
						*selected = TRUE;
				}
				return &iter;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	return NULL;
}

void
userlist_set_value (GtkWidget *treeview, gfloat val)
{
	gtk_adjustment_set_value (
			hc_tree_view_get_vadjustment (treeview), val);
}

gfloat
userlist_get_value (GtkWidget *treeview)
{
	return gtk_adjustment_get_value (hc_tree_view_get_vadjustment (treeview));
}

int
fe_userlist_remove (session *sess, struct User *user)
{
	GtkTreeIter *iter;
/*	GtkAdjustment *adj;
	gfloat val, end;*/
	int sel;

	iter = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
						  GTK_TREE_MODEL(sess->res->user_model), user, &sel);
	if (!iter)
		return 0;

/*	adj = gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (sess->gui->user_tree));
	val = adj->value;*/

	gtk_list_store_remove (sess->res->user_model, iter);

	/* is it the front-most tab? */
/*	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == sess->res->user_model)
	{
		end = adj->upper - adj->lower - adj->page_size;
		if (val > end)
			val = end;
		gtk_adjustment_set_value (adj, val);
	}*/

	return sel;
}

void
fe_userlist_rehash (session *sess, struct User *user)
{
	GtkTreeIter *iter;
	int sel;
	int nick_color = 0;

	iter = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
						  GTK_TREE_MODEL(sess->res->user_model), user, &sel);
	if (!iter)
		return;

	if (prefs.hex_away_track && user->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(user->nick);

	gtk_list_store_set (GTK_LIST_STORE (sess->res->user_model), iter,
							  COL_HOST, user->hostname,
							  COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
							  -1);
}

void
fe_userlist_insert (session *sess, struct User *newuser, gboolean sel)
{
	GtkTreeModel *model = GTK_TREE_MODEL(sess->res->user_model);
	GdkPixbuf *pix = get_user_icon (sess->server, newuser);
	GtkTreeIter iter;
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

	gtk_list_store_insert_with_values (GTK_LIST_STORE (model), &iter, 0,
									COL_PIX, pix,
									COL_NICK, nick,
									COL_HOST, newuser->hostname,
									COL_USER, newuser,
									COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
								  -1);

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

	/* is it the front-most tab? */
	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == model)
	{
		if (sel)
			gtk_tree_selection_select_iter (gtk_tree_view_get_selection
										(GTK_TREE_VIEW (sess->gui->user_tree)), &iter);
	}
}

void
fe_userlist_clear (session *sess)
{
	gtk_list_store_clear (sess->res->user_model);
}

#endif /* HC_GTK4 else GTK3 */

#if HC_GTK4
/*
 * GTK4: File drop handler for userlist - drops file on the selected user
 *
 * Note: GtkColumnView doesn't have get_path_at_pos like GtkTreeView, so we
 * use the currently selected item instead of determining the row under cursor.
 * This is a simpler UX: user selects target, then drops file.
 */
static gboolean
userlist_file_drop_cb (GtkDropTarget *target, const GValue *value,
                       double x, double y, gpointer user_data)
{
	GtkWidget *view = user_data;
	GtkColumnView *column_view = GTK_COLUMN_VIEW (view);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (column_view);
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
	/* Nothing needed for GtkColumnView - selection is handled differently */
}

#else /* GTK3 */

static void
userlist_dnd_drop (GtkTreeView *widget, GdkDragContext *context,
						 gint x, gint y, GtkSelectionData *selection_data,
						 guint info, guint ttime, gpointer userdata)
{
	struct User *user;
	gchar *data;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
		return;

	model = gtk_tree_view_get_model (widget);
	if (!gtk_tree_model_get_iter (model, &iter, path))
		return;
	gtk_tree_model_get (model, &iter, COL_USER, &user, -1);

	data = (char *)gtk_selection_data_get_data (selection_data);

	if (data)
		mg_dnd_drop_file (current_sess, user->nick, data);
}

static gboolean
userlist_dnd_motion (GtkTreeView *widget, GdkDragContext *context, gint x,
							gint y, guint ttime, gpointer tree)
{
	GtkTreePath *path;
	GtkTreeSelection *sel;

	if (!tree)
		return FALSE;

	if (gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
	{
		sel = gtk_tree_view_get_selection (widget);
		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
	}

	return FALSE;
}

static gboolean
userlist_dnd_leave (GtkTreeView *widget, GdkDragContext *context, guint ttime)
{
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (widget));
	return TRUE;
}

#endif /* HC_GTK4 */

#if HC_GTK4
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

#else /* GTK3 */

static int
userlist_alpha_cmp (GtkTreeModel *model, GtkTreeIter *iter_a, GtkTreeIter *iter_b, gpointer userdata)
{
	struct User *user_a, *user_b;

	gtk_tree_model_get (model, iter_a, COL_USER, &user_a, -1);
	gtk_tree_model_get (model, iter_b, COL_USER, &user_b, -1);

	return nick_cmp_alpha (user_a, user_b, ((session*)userdata)->server);
}

static int
userlist_ops_cmp (GtkTreeModel *model, GtkTreeIter *iter_a, GtkTreeIter *iter_b, gpointer userdata)
{
	struct User *user_a, *user_b;

	gtk_tree_model_get (model, iter_a, COL_USER, &user_a, -1);
	gtk_tree_model_get (model, iter_b, COL_USER, &user_b, -1);

	return nick_cmp_az_ops (((session*)userdata)->server, user_a, user_b);
}

GtkListStore *
userlist_create_model (session *sess)
{
	GtkListStore *store;
	GtkTreeIterCompareFunc cmp_func;
	GtkSortType sort_type;

	store = gtk_list_store_new (5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
										G_TYPE_POINTER, GDK_TYPE_RGBA);

	switch (prefs.hex_gui_ulist_sort)
	{
	case 0:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 1:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 2:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	case 3:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	default:
		/* No sorting */
		gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE(store), NULL, NULL, NULL);
		return store;
	}

	gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE(store), cmp_func, sess, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store),
						GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, sort_type);

	return store;
}

#endif /* HC_GTK4 */

#if HC_GTK4
/*
 * GTK4 Factory callbacks for GtkColumnView
 */

/* Icon column setup - creates a GtkPicture */
static void
userlist_setup_icon_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *picture = gtk_picture_new ();
	gtk_picture_set_content_fit (GTK_PICTURE (picture), GTK_CONTENT_FIT_SCALE_DOWN);
	gtk_widget_set_size_request (picture, 16, -1);
	if (prefs.hex_gui_compact)
		gtk_widget_set_margin_top (picture, 0);
	gtk_list_item_set_child (item, picture);
}

/* Icon column bind - sets texture from HcUserItem */
static void
userlist_bind_icon_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *picture = gtk_list_item_get_child (item);
	HcUserItem *user_item = gtk_list_item_get_item (item);

	if (!user_item)
		return;

	if (user_item->icon)
		gtk_picture_set_paintable (GTK_PICTURE (picture), GDK_PAINTABLE (user_item->icon));
	else
		gtk_picture_set_paintable (GTK_PICTURE (picture), NULL);
}

/* Nick column setup - creates a GtkLabel */
static void
userlist_setup_nick_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	if (prefs.hex_gui_compact)
		gtk_widget_set_margin_top (label, 0);
	gtk_list_item_set_child (item, label);
}

/* Nick column bind - sets text and color from HcUserItem */
static void
userlist_bind_nick_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcUserItem *user_item = gtk_list_item_get_item (item);
	PangoAttrList *attrs;

	if (!user_item)
		return;

	gtk_label_set_text (GTK_LABEL (label), user_item->nick ? user_item->nick : "");

	/* Apply color if set */
	if (user_item->color_index > 0)
	{
		GdkRGBA *color = &colors[user_item->color_index];
		attrs = pango_attr_list_new ();
		pango_attr_list_insert (attrs, pango_attr_foreground_new (
			(guint16)(color->red * 65535),
			(guint16)(color->green * 65535),
			(guint16)(color->blue * 65535)));
		gtk_label_set_attributes (GTK_LABEL (label), attrs);
		pango_attr_list_unref (attrs);
	}
	else
	{
		gtk_label_set_attributes (GTK_LABEL (label), NULL);
	}
}

/* Host column setup - creates a GtkLabel */
static void
userlist_setup_host_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	if (prefs.hex_gui_compact)
		gtk_widget_set_margin_top (label, 0);
	gtk_list_item_set_child (item, label);
}

/* Host column bind - sets text from HcUserItem */
static void
userlist_bind_host_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcUserItem *user_item = gtk_list_item_get_item (item);

	if (!user_item)
		return;

	gtk_label_set_text (GTK_LABEL (label), user_item->hostname ? user_item->hostname : "");
}

#else /* GTK3 */

static void
userlist_add_columns (GtkTreeView * treeview)
{
	GtkCellRenderer *renderer;

	/* icon column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
																"pixbuf", COL_PIX, NULL);

	/* nick column */
	renderer = gtk_cell_renderer_text_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
																"text", COL_NICK,
																"foreground-rgba", COL_GDKCOLOR, NULL);

	if (prefs.hex_gui_ulist_show_hosts)
	{
		/* hostname column */
		renderer = gtk_cell_renderer_text_new ();
		if (prefs.hex_gui_compact)
			g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
		gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
		gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																	-1, NULL, renderer,
																	"text", COL_HOST, NULL);
	}
}

#endif /* HC_GTK4 */

/*
 * Click handler for userlist
 * GTK3: Uses GdkEventButton from "button_press_event" signal
 * GTK4: Uses GtkGestureClick with different signature
 */
#if HC_GTK4
/*
 * GTK4 version of userlist_selection_list
 * Gets selected nicks from GtkColumnView's selection model
 */
static char **
userlist_selection_list_gtk4 (GtkColumnView *view, int *num_ret)
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
	sel_model = gtk_column_view_get_model (view);
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

static void
userlist_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkColumnView *view = GTK_COLUMN_VIEW (widget);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	char **nicks;
	int i;
	int button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
	GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

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
		return;
	}

	if (button == 3)
	{
		/* do we have a multi-selection? */
		nicks = userlist_selection_list_gtk4 (view, &i);
		if (nicks && i > 1)
		{
			menu_nickmenu (current_sess, widget, x, y, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
			return;
		}
		if (nicks)
		{
			g_free (nicks[0]);
			g_free (nicks);
		}

		/* For right-click on unselected item, we'd need to figure out which row
		 * was clicked. GtkColumnView doesn't have get_path_at_pos like GtkTreeView.
		 * For now, just show menu for current selection or do nothing. */
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
		else
		{
			/* Clear selection if clicking on empty area */
			gtk_selection_model_unselect_all (sel_model);
		}
	}
}
#else /* GTK3 */
static gint
userlist_click_cb (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
	char **nicks;
	int i;
	GtkTreeSelection *sel;
	GtkTreePath *path;

	if (!event)
		return FALSE;

	if (!(event->state & STATE_CTRL) &&
		event->type == GDK_2BUTTON_PRESS && prefs.hex_gui_ulist_doubleclick[0])
	{
		nicks = userlist_selection_list (widget, &i);
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
		return TRUE;
	}

	if (event->button == 3)
	{
		/* do we have a multi-selection? */
		nicks = userlist_selection_list (widget, &i);
		if (nicks && i > 1)
		{
			menu_nickmenu (current_sess, event, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
			return TRUE;
		}
		if (nicks)
		{
			g_free (nicks[0]);
			g_free (nicks);
		}

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
			 event->x, event->y, &path, 0, 0, 0))
		{
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
			gtk_tree_path_free (path);
			nicks = userlist_selection_list (widget, &i);
			if (nicks)
			{
				menu_nickmenu (current_sess, event, nicks[0], i);
				while (i)
				{
					i--;
					g_free (nicks[i]);
				}
				g_free (nicks);
			}
		} else
		{
			gtk_tree_selection_unselect_all (sel);
		}

		return TRUE;
	}

	return FALSE;
}
#endif

/*
 * Key handler for userlist - forwards printable keys to input box
 * GTK3: Uses GdkEventKey from "key_press_event" signal
 * GTK4: Uses GtkEventControllerKey with different signature
 */
#if HC_GTK4
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
#else /* GTK3 */
static gboolean
userlist_key_cb (GtkWidget *wid, GdkEventKey *evt, gpointer userdata)
{
	if (evt->keyval >= GDK_KEY_asterisk && evt->keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		gtk_widget_event (current_sess->gui->input_box, (GdkEvent *)evt);
		return TRUE;
	}

	return FALSE;
}
#endif

#if HC_GTK4
/*
 * GTK4 version: Create GtkColumnView with sorting
 */
GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *view;
	GtkListItemFactory *factory;
	GtkColumnViewColumn *col;
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

	/* Create column view - model will be set later in userlist_show() */
	view = gtk_column_view_new (NULL);
	gtk_widget_set_name (view, "hexchat-userlist");
	gtk_widget_set_can_focus (view, FALSE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), FALSE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), FALSE);

	/* Icon column (only if icons enabled) */
	if (prefs.hex_gui_ulist_icons)
	{
		factory = gtk_signal_list_item_factory_new ();
		g_signal_connect (factory, "setup", G_CALLBACK (userlist_setup_icon_cb), NULL);
		g_signal_connect (factory, "bind", G_CALLBACK (userlist_bind_icon_cb), NULL);
		col = gtk_column_view_column_new (NULL, factory);
		gtk_column_view_column_set_fixed_width (col, 20);
		gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
		g_object_unref (col);
	}

	/* Nick column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (userlist_setup_nick_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (userlist_bind_nick_cb), NULL);
	col = gtk_column_view_column_new (NULL, factory);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Host column (only if enabled) */
	if (prefs.hex_gui_ulist_show_hosts)
	{
		factory = gtk_signal_list_item_factory_new ();
		g_signal_connect (factory, "setup", G_CALLBACK (userlist_setup_host_cb), NULL);
		g_signal_connect (factory, "bind", G_CALLBACK (userlist_bind_host_cb), NULL);
		col = gtk_column_view_column_new (NULL, factory);
		gtk_column_view_column_set_expand (col, TRUE);
		gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
		g_object_unref (col);
	}

	/* DND: File drops for DCC (drop file on user to send) */
	drop_target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (drop_target, "drop", G_CALLBACK (userlist_file_drop_cb), view);
	g_signal_connect (drop_target, "motion", G_CALLBACK (userlist_drop_motion_cb), view);
	g_signal_connect (drop_target, "leave", G_CALLBACK (userlist_drop_leave_cb), view);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (drop_target));

	/* Layout swapping drag source (drag userlist to reposition) */
	mg_setup_userlist_drag_source (view);

	/* Event controllers for click and key events */
	hc_add_click_gesture (view, G_CALLBACK (userlist_click_cb), NULL, NULL);
	hc_add_key_controller (view, G_CALLBACK (userlist_key_cb), NULL, NULL);

	hc_scrolled_window_set_child (sw, view);
	hc_widget_show (view);

	return view;
}

#else /* GTK3 */

GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *treeview;
	static const GtkTargetEntry dnd_dest_targets[] =
	{
		{"text/uri-list", 0, 1},
		{"HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 }
	};
	static const GtkTargetEntry dnd_src_target[] =
	{
		{"HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 }
	};

	sw = hc_scrolled_window_new ();
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
													 GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
											  prefs.hex_gui_ulist_show_hosts ?
												GTK_POLICY_AUTOMATIC :
												GTK_POLICY_NEVER,
											  GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (sw, TRUE);
	hc_box_pack_start (box, sw, TRUE, TRUE, 0);
	hc_widget_show (sw);

	treeview = gtk_tree_view_new ();
	gtk_widget_set_name (treeview, "hexchat-userlist");
	gtk_widget_set_can_focus (treeview, FALSE);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection
										  (GTK_TREE_VIEW (treeview)),
										  GTK_SELECTION_MULTIPLE);

	/* GTK3: set up drops */
	gtk_drag_dest_set (treeview, GTK_DEST_DEFAULT_ALL, dnd_dest_targets, 2,
							 GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);
	gtk_drag_source_set (treeview, GDK_BUTTON1_MASK, dnd_src_target, 1, GDK_ACTION_MOVE);

	/* file DND (for DCC) */
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (userlist_dnd_motion), treeview);
	g_signal_connect (G_OBJECT (treeview), "drag_leave",
							G_CALLBACK (userlist_dnd_leave), 0);
	g_signal_connect (G_OBJECT (treeview), "drag_data_received",
							G_CALLBACK (userlist_dnd_drop), treeview);

	g_signal_connect (G_OBJECT (treeview), "button_press_event",
							G_CALLBACK (userlist_click_cb), 0);
	g_signal_connect (G_OBJECT (treeview), "key_press_event",
							G_CALLBACK (userlist_key_cb), 0);

	/* tree/chanview DND */
	g_signal_connect (G_OBJECT (treeview), "drag_begin",
							G_CALLBACK (mg_drag_begin_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_drop",
							G_CALLBACK (mg_drag_drop_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (mg_drag_motion_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_end",
							G_CALLBACK (mg_drag_end_cb), NULL);

	userlist_add_columns (GTK_TREE_VIEW (treeview));

	hc_scrolled_window_set_child (sw, treeview);
	hc_widget_show (treeview);

	return treeview;
}

#endif /* HC_GTK4 */

#if HC_GTK4
/*
 * GTK4: Connect the session's model to the GtkColumnView with sorting
 */
void
userlist_show (session *sess)
{
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
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

	/* Create multi-selection model for the column view */
	sel_model = gtk_multi_selection_new (G_LIST_MODEL (sort_model));

	/* Set the model on the column view */
	gtk_column_view_set_model (view, GTK_SELECTION_MODEL (sel_model));

	/* We don't unref sel_model/sort_model - the column view takes ownership */
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	GtkColumnView *view = GTK_COLUMN_VIEW (sess->gui->user_tree);
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListModel *model;
	guint n_items, i;
	HcUserItem *item;
	int thisname;
	char *name;

	(void)scroll_to; /* TODO: Implement scroll_to for GtkColumnView */

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

#else /* GTK3 */

void
userlist_show (session *sess)
{
	gtk_tree_view_set_model (GTK_TREE_VIEW (sess->gui->user_tree),
									 GTK_TREE_MODEL(sess->res->user_model));
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	int thisname;
	char *name;
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		if (do_clear)
			gtk_tree_selection_unselect_all (selection);

		do
		{
			if (*word[0])
			{
				gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
				thisname = 0;
				while ( *(name = word[thisname++]) )
				{
					if (sess->server->p_cmp (row_user->nick, name) == 0)
					{
						gtk_tree_selection_select_iter (selection, &iter);
						if (scroll_to)
							scroll_to_iter (&iter, treeview, model);
						break;
					}
				}
			}

		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}

#endif /* HC_GTK4 */
