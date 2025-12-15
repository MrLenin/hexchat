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
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/modes.h"
#include "../common/outbound.h"
#include "../common/hexchatc.h"
#include "gtkutil.h"
#include "maingui.h"
#include "banlist.h"

/*
 * These supports_* routines set capable, readable, writable bits */
static void supports_bans (banlist_info *, int);
static void supports_exempt (banlist_info *, int);
static void supports_invite (banlist_info *, int);
static void supports_quiet (banlist_info *, int);

static mode_info modes[MODE_CT] = {
	{
		N_("Bans"),
		N_("Ban"),
		'b',
		RPL_BANLIST,
		RPL_ENDOFBANLIST,
		1<<MODE_BAN,
		supports_bans
	}
	,{
		N_("Exempts"),
		N_("Exempt"),
		'e',
		RPL_EXCEPTLIST,
		RPL_ENDOFEXCEPTLIST,
		1<<MODE_EXEMPT,
		supports_exempt
	}
	,{
		N_("Invites"),
		N_("Invite"),
		'I',
		RPL_INVITELIST,
		RPL_ENDOFINVITELIST,
		1<<MODE_INVITE,
		supports_invite
	}
	,{
		N_("Quiets"),
		N_("Quiet"),
		'q',
		RPL_QUIETLIST,
		RPL_ENDOFQUIETLIST,
		1<<MODE_QUIET,
		supports_quiet
	}
};

#if !HC_GTK4
/* model for the banlist tree (GTK3 only) */
enum
{
	TYPE_COLUMN,
	MASK_COLUMN,
	FROM_COLUMN,
	DATE_COLUMN,
	N_COLUMNS
};
#endif

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold ban list row data */
#define HC_TYPE_BAN_ITEM (hc_ban_item_get_type())
G_DECLARE_FINAL_TYPE (HcBanItem, hc_ban_item, HC, BAN_ITEM, GObject)

struct _HcBanItem {
	GObject parent;
	char *type;
	char *mask;
	char *from;
	char *date;
};

G_DEFINE_TYPE (HcBanItem, hc_ban_item, G_TYPE_OBJECT)

static void
hc_ban_item_finalize (GObject *obj)
{
	HcBanItem *item = HC_BAN_ITEM (obj);
	g_free (item->type);
	g_free (item->mask);
	g_free (item->from);
	g_free (item->date);
	G_OBJECT_CLASS (hc_ban_item_parent_class)->finalize (obj);
}

static void
hc_ban_item_class_init (HcBanItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_ban_item_finalize;
}

static void
hc_ban_item_init (HcBanItem *item)
{
	item->type = NULL;
	item->mask = NULL;
	item->from = NULL;
	item->date = NULL;
}

static HcBanItem *
hc_ban_item_new (const char *type, const char *mask, const char *from, const char *date)
{
	HcBanItem *item = g_object_new (HC_TYPE_BAN_ITEM, NULL);
	item->type = g_strdup (type ? type : "");
	item->mask = g_strdup (mask ? mask : "");
	item->from = g_strdup (from ? from : "");
	item->date = g_strdup (date ? date : "");
	return item;
}

static GtkColumnView *
get_view (struct session *sess)
{
	return GTK_COLUMN_VIEW (sess->res->banlist->treeview);
}

static GListStore *
get_store (struct session *sess)
{
	return G_LIST_STORE (g_object_get_data (G_OBJECT (sess->res->banlist->treeview), "store"));
}

#else /* GTK3 */

static GtkTreeView *
get_view (struct session *sess)
{
	return GTK_TREE_VIEW (sess->res->banlist->treeview);
}

static GtkListStore *
get_store (struct session *sess)
{
	return GTK_LIST_STORE (gtk_tree_view_get_model (get_view (sess)));
}

#endif /* HC_GTK4 */

static void
supports_bans (banlist_info *banl, int i)
{
	int bit = 1<<i;

	banl->capable |= bit;
	banl->readable |= bit;
	banl->writeable |= bit;
	return;
}

static void
supports_exempt (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (serv->have_except)
		goto yes;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == 'e')
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->writeable |= bit;
}

static void
supports_invite (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (serv->have_invite)
		goto yes;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == 'I')
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->writeable |= bit;
}

static void
supports_quiet (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == modes[i].letter)
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->readable |= bit;
	banl->writeable |= bit;
}

/* fe_add_ban_list() and fe_ban_list_end() return TRUE if consumed, FALSE otherwise */
gboolean
fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode)
{
	banlist_info *banl = sess->res->banlist;
	int i;
#if HC_GTK4
	GListStore *store;
	HcBanItem *item;
#else
	GtkListStore *store;
	GtkTreeIter iter;
#endif

	if (!banl)
		return FALSE;

	for (i = 0; i < MODE_CT; i++)
		if (modes[i].code == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected value in fe_add_ban_list:  %d\n", rplcode); */
		return FALSE;
	}
	if (banl->pending & 1<<i)
	{
		store = get_store (sess);
#if HC_GTK4
		item = hc_ban_item_new (_(modes[i].type), mask, who, when);
		g_list_store_append (store, item);
		g_object_unref (item);
#else
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, TYPE_COLUMN, _(modes[i].type), MASK_COLUMN, mask,
						FROM_COLUMN, who, DATE_COLUMN, when, -1);
#endif

		banl->line_ct++;
		return TRUE;
	}
	else return FALSE;
}

/* Sensitize checkboxes and buttons as appropriate for the moment  */
static void
banlist_sensitize (banlist_info *banl)
{
	int checkable, i;
	gboolean is_op = FALSE;

	if (banl->sess->me == NULL)
		return;

	/* FIXME: More access levels than these can unban */
	if (banl->sess->me->op || banl->sess->me->hop)
		is_op = TRUE;

	/* CHECKBOXES -- */
	checkable = is_op? banl->writeable: banl->readable;
	for (i = 0; i < MODE_CT; i++)
	{
		if (banl->checkboxes[i] == NULL)
			continue;
		if ((checkable & 1<<i) == 0)
		/* Checkbox is not checkable.  Grey it and uncheck it. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], FALSE);
			hc_check_button_set_active (banl->checkboxes[i], FALSE);
		}
		else
		/* Checkbox is checkable.  Be sure it's sensitive. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], TRUE);
		}
	}

	/* BUTTONS --- */
	if (!is_op || banl->line_ct == 0)
	{
		/* If user is not op or list is empty, buttons should be all greyed */
		gtk_widget_set_sensitive (banl->but_clear, FALSE);
		gtk_widget_set_sensitive (banl->but_crop, FALSE);
		gtk_widget_set_sensitive (banl->but_remove, FALSE);
	}
	else
	{
		/* If no lines are selected, only the CLEAR button should be sensitive */
		if (banl->select_ct == 0)
		{
			gtk_widget_set_sensitive (banl->but_clear, TRUE);
			gtk_widget_set_sensitive (banl->but_crop, FALSE);
			gtk_widget_set_sensitive (banl->but_remove, FALSE);
		}
		/* If any lines are selected, only the REMOVE and CROP buttons should be sensitive */
		else
		{
			gtk_widget_set_sensitive (banl->but_clear, FALSE);
			gtk_widget_set_sensitive (banl->but_crop, banl->line_ct == banl->select_ct? FALSE: TRUE);
			gtk_widget_set_sensitive (banl->but_remove, TRUE);
		}
	}

	/* Set "Refresh" sensitvity */
	gtk_widget_set_sensitive (banl->but_refresh, banl->pending? FALSE: banl->checked? TRUE: FALSE);
}
/* fe_ban_list_end() returns TRUE if consumed, FALSE otherwise */
gboolean
fe_ban_list_end (struct session *sess, int rplcode)
{
	banlist_info *banl = sess->res->banlist;
	int i;

	if (!banl)
		return FALSE;

	for (i = 0; i < MODE_CT; i++)
		if (modes[i].endcode == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected rplcode value in fe_ban_list_end:  %d\n", rplcode); */
		return FALSE;
	}
	if (banl->pending & modes[i].bit)
	{
		banl->pending &= ~modes[i].bit;
		if (!banl->pending)
		{
			gtk_widget_set_sensitive (banl->but_refresh, TRUE);
			banlist_sensitize (banl);
		}
		return TRUE;
	}
	else return FALSE;
}

#if HC_GTK4
/* GTK4: Action callbacks for copy menu - store context in user_data */
typedef struct {
	GtkWidget *view;
	gboolean copy_mask_only;
} BanlistCopyContext;

static void
banlist_copy_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	BanlistCopyContext *ctx = user_data;
	GtkSelectionModel *sel_model;
	HcBanItem *item;
	char *str;

	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (ctx->view));
	item = hc_selection_model_get_selected_item (sel_model);
	if (!item)
	{
		g_free (ctx);
		return;
	}

	if (ctx->copy_mask_only)
		str = g_strdup (item->mask);
	else
		str = g_strdup_printf (_("%s on %s by %s"), item->mask, item->date, item->from);

	if (str && str[0] != 0)
		gtkutil_copy_to_clipboard (ctx->view, NULL, str);

	g_free (str);
	g_object_unref (item);
	g_free (ctx);
}
#else /* GTK3 */
static void
banlist_copyentry (GtkWidget *menuitem, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	GValue mask;
	GValue from;
	GValue date;
	char *str;

	memset (&mask, 0, sizeof (mask));
	memset (&from, 0, sizeof (from));
	memset (&date, 0, sizeof (date));

	/* get selection (which should have been set on click)
	 * and temporarily switch to single mode to get selected iter */
	sel = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		gtk_tree_model_get_value (model, &iter, MASK_COLUMN, &mask);
		gtk_tree_model_get_value (model, &iter, FROM_COLUMN, &from);
		gtk_tree_model_get_value (model, &iter, DATE_COLUMN, &date);

		/* poor way to get which is selected but it works */
		if (strcmp (_("Copy mask"), gtk_menu_item_get_label (GTK_MENU_ITEM(menuitem))) == 0)
			str = g_value_dup_string (&mask);
		else
			str = g_strdup_printf (_("%s on %s by %s"), g_value_get_string (&mask),
								g_value_get_string (&date), g_value_get_string (&from));

		if (str[0] != 0)
			gtkutil_copy_to_clipboard (menuitem, NULL, str);

		g_value_unset (&mask);
		g_value_unset (&from);
		g_value_unset (&date);
		g_free (str);
	}
	gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
}
#endif /* HC_GTK4 */

/*
 * Right-click context menu handler for banlist
 * GTK3: Uses GdkEventButton from "button-press-event" signal and gtk_menu_popup
 * GTK4: Uses GtkGestureClick and GtkPopoverMenu
 */
#if HC_GTK4
/* Popover closed callback to clean up action group */
static void
banlist_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (popover));
	if (parent)
		gtk_widget_insert_action_group (parent, "banlist", NULL);
	gtk_widget_unparent (GTK_WIDGET (popover));
}

/* Action callback for copy-mask */
static void
banlist_copy_mask_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GtkWidget *view = GTK_WIDGET (user_data);
	GtkSelectionModel *sel_model;
	HcBanItem *item;

	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));
	item = hc_selection_model_get_selected_item (sel_model);
	if (item)
	{
		if (item->mask && item->mask[0] != 0)
			gtkutil_copy_to_clipboard (view, NULL, item->mask);
		g_object_unref (item);
	}
}

/* Action callback for copy-entry */
static void
banlist_copy_entry_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GtkWidget *view = GTK_WIDGET (user_data);
	GtkSelectionModel *sel_model;
	HcBanItem *item;
	char *str;

	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));
	item = hc_selection_model_get_selected_item (sel_model);
	if (item)
	{
		str = g_strdup_printf (_("%s on %s by %s"), item->mask, item->date, item->from);
		if (str && str[0] != 0)
			gtkutil_copy_to_clipboard (view, NULL, str);
		g_free (str);
		g_object_unref (item);
	}
}

static void
banlist_button_pressed (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkWidget *wid = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkWidget *popover;
	GMenu *gmenu;
	GSimpleActionGroup *action_group;
	GSimpleAction *action;
	int button;

	button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

	/* Check for right click */
	if (button == 3)
	{
		/* Create action group for menu actions */
		action_group = g_simple_action_group_new ();

		action = g_simple_action_new ("copy-mask", NULL);
		g_signal_connect (action, "activate", G_CALLBACK (banlist_copy_mask_action), wid);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new ("copy-entry", NULL);
		g_signal_connect (action, "activate", G_CALLBACK (banlist_copy_entry_action), wid);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);

		gtk_widget_insert_action_group (wid, "banlist", G_ACTION_GROUP (action_group));
		g_object_unref (action_group);

		/* Create menu model */
		gmenu = g_menu_new ();
		g_menu_append (gmenu, _("Copy mask"), "banlist.copy-mask");
		g_menu_append (gmenu, _("Copy entry"), "banlist.copy-entry");

		popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
		gtk_widget_set_parent (popover, wid);
		gtk_popover_set_pointing_to (GTK_POPOVER (popover),
			&(GdkRectangle){ (int)x, (int)y, 1, 1 });

		g_signal_connect (popover, "closed", G_CALLBACK (banlist_popover_closed_cb), NULL);
		gtk_popover_popup (GTK_POPOVER (popover));

		g_object_unref (gmenu);
	}
}
#else /* GTK3 */
static gboolean
banlist_button_pressed (GtkWidget *wid, GdkEventButton *event, gpointer userdata)
{
	GtkTreePath *path;
	GtkWidget *menu, *maskitem, *allitem;

	/* Check for right click */
	if (event->type == GDK_BUTTON_PRESS && event->button == 3)
	{
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (wid), event->x, event->y,
												&path, NULL, NULL, NULL))
		{
			/* Must set the row active for use in callback */
			gtk_tree_view_set_cursor (GTK_TREE_VIEW(wid), path, NULL, FALSE);
			gtk_tree_path_free (path);

			menu = gtk_menu_new ();
			maskitem = gtk_menu_item_new_with_label (_("Copy mask"));
			allitem = gtk_menu_item_new_with_label (_("Copy entry"));
			g_signal_connect (maskitem, "activate", G_CALLBACK(banlist_copyentry), wid);
			g_signal_connect (allitem, "activate", G_CALLBACK(banlist_copyentry), wid);
			gtk_menu_shell_append (GTK_MENU_SHELL(menu), maskitem);
			gtk_menu_shell_append (GTK_MENU_SHELL(menu), allitem);
			gtk_widget_show_all (menu);

			gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
							event->button, gtk_get_current_event_time ());
		}

		return TRUE;
	}

	return FALSE;
}
#endif

#if HC_GTK4
static void
banlist_select_changed (GtkSelectionModel *sel_model, guint position, guint n_items, gpointer userdata)
{
	banlist_info *banl = userdata;
	GtkBitset *selection;

	if (banl->line_ct == 0)
		banl->select_ct = 0;
	else
	{
		selection = gtk_selection_model_get_selection (sel_model);
		banl->select_ct = gtk_bitset_get_size (selection);
		gtk_bitset_unref (selection);
	}
	banlist_sensitize (banl);
}
#else /* GTK3 */
static void
banlist_select_changed (GtkWidget *item, banlist_info *banl)
{
	GList *list;

	if (banl->line_ct == 0)
		banl->select_ct = 0;
	else
	{
		list = gtk_tree_selection_get_selected_rows (GTK_TREE_SELECTION (item), NULL);
		banl->select_ct = g_list_length (list);
		g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (list);
	}
	banlist_sensitize (banl);
}
#endif /* HC_GTK4 */

/**
 *  * Performs the actual refresh operations.
 *  */
static void
banlist_do_refresh (banlist_info *banl)
{
	session *sess = banl->sess;
	char tbuf[256];
	int i;
#if HC_GTK4
	GListStore *store;
#else
	GtkListStore *store;
#endif

	banlist_sensitize (banl);

	if (sess->server->connected)
	{
		g_snprintf (tbuf, sizeof tbuf, "Ban List (%s, %s) - %s",
						sess->channel, sess->server->servername, _(DISPLAY_NAME));
		mg_set_title (banl->window, tbuf);

		store = get_store (sess);
#if HC_GTK4
		g_list_store_remove_all (store);
#else
		gtk_list_store_clear (store);
#endif
		banl->line_ct = 0;
		banl->pending = banl->checked;
		if (banl->pending)
		{
			for (i = 0; i < MODE_CT; i++)
				if (banl->pending & 1<<i)
				{
					g_snprintf (tbuf, sizeof tbuf, "quote mode %s +%c", sess->channel, modes[i].letter);
					handle_command (sess, tbuf, FALSE);
				}
		}
	}
	else
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
	}
}

static void
banlist_refresh (GtkWidget * wid, banlist_info *banl)
{
	/* JG NOTE: Didn't see actual use of wid here, so just forwarding
	   *          * this to chanlist_do_refresh because I use it without any widget
	   *          * param in chanlist_build_gui_list when the user presses enter
	   *          * or apply for the first time if the list has not yet been
	   *          * received.
	   *          */
	banlist_do_refresh (banl);
}

static int
banlist_unban_inner (gpointer none, banlist_info *banl, int mode_num)
{
	session *sess = banl->sess;
	char tbuf[2048];
	char **masks;
	int num_sel, i;
#if HC_GTK4
	GListStore *store;
	GtkSelectionModel *sel_model;
	GtkBitset *selection;
	GtkBitsetIter biter;
	guint pos;
	guint n_items;
	HcBanItem *item;

	store = get_store (sess);
	sel_model = gtk_column_view_get_model (get_view (sess));

	n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
	if (n_items == 0)
		return 0;

	masks = g_new (char *, banl->line_ct);
	num_sel = 0;

	selection = gtk_selection_model_get_selection (sel_model);
	if (gtk_bitset_iter_init_first (&biter, selection, &pos))
	{
		do
		{
			item = g_list_model_get_item (G_LIST_MODEL (store), pos);
			if (item)
			{
				/* If it's the wrong type of mask, just continue */
				if (strcmp (_(modes[mode_num].type), item->type) == 0)
				{
					/* Otherwise add it to our array of mask pointers */
					masks[num_sel++] = g_strdup (item->mask);
				}
				g_object_unref (item);
			}
		}
		while (gtk_bitset_iter_next (&biter, &pos));
	}
	gtk_bitset_unref (selection);

#else /* GTK3 */
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	char *mask, *type;

	/* grab the list of selected items */
	model = GTK_TREE_MODEL (get_store (sess));
	sel = gtk_tree_view_get_selection (get_view (sess));

	if (!gtk_tree_model_get_iter_first (model, &iter))
		return 0;

	masks = g_new (char *, banl->line_ct);
	num_sel = 0;
	do
	{
		if (gtk_tree_selection_iter_is_selected (sel, &iter))
		{
			/* Get the mask part of this selected line */
			gtk_tree_model_get (model, &iter, TYPE_COLUMN, &type, MASK_COLUMN, &mask, -1);

			/* If it's the wrong type of mask, just continue */
			if (strcmp (_(modes[mode_num].type), type) != 0)
				continue;

			/* Otherwise add it to our array of mask pointers */
			masks[num_sel++] = g_strdup (mask);
			g_free (mask);
			g_free (type);
		}
	}
	while (gtk_tree_model_iter_next (model, &iter));
#endif /* HC_GTK4 */

	/* and send to server */
	if (num_sel)
		send_channel_modes (sess, tbuf, masks, 0, num_sel, '-', modes[mode_num].letter, 0);

	/* now free everything */
	for (i=0; i < num_sel; i++)
		g_free (masks[i]);
	g_free (masks);

	return num_sel;
}

static void
banlist_unban (GtkWidget * wid, banlist_info *banl)
{
	int i, num = 0;

	for (i = 0; i < MODE_CT; i++)
		num += banlist_unban_inner (wid, banl, i);

	/* This really should not occur with the redesign */
	if (num < 1)
	{
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
		return;
	}

	banlist_do_refresh (banl);
}

static void
banlist_clear_cb (GtkDialog *dialog, gint response, gpointer data)
{
	banlist_info *banl = data;
#if HC_GTK4
	GtkSelectionModel *sel_model;
#else
	GtkTreeSelection *sel;
#endif

	hc_window_destroy (GTK_WIDGET (dialog));

	if (response == GTK_RESPONSE_OK)
	{
#if HC_GTK4
		sel_model = gtk_column_view_get_model (get_view (banl->sess));
		gtk_selection_model_select_all (sel_model);
#else
		sel = gtk_tree_view_get_selection (get_view (banl->sess));
		gtk_tree_selection_select_all (sel);
#endif
		banlist_unban (NULL, banl);
	}
}

static void
banlist_clear (GtkWidget * wid, banlist_info *banl)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (NULL, 0,
								GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					_("Are you sure you want to remove all listed items in %s?"), banl->sess->channel);

	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (banlist_clear_cb), banl);
	hc_window_set_position (dialog, GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);
}

static void
banlist_add_selected_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	GSList **lp = data;
	GtkTreeIter *copy;

	if (lp == NULL)
	{
		return;
	}

	copy = g_new (GtkTreeIter, 1);
	*copy = *iter;

	*lp = g_slist_append (*lp, copy);
}

static void
banlist_crop (GtkWidget * wid, banlist_info *banl)
{
	session *sess = banl->sess;
#if HC_GTK4
	GtkSelectionModel *sel_model;
	GtkBitset *current_selection;
	GtkBitset *inverted;
	guint n_items;

	sel_model = gtk_column_view_get_model (get_view (sess));
	n_items = g_list_model_get_n_items (
		gtk_selection_model_get_model (sel_model));

	/* Get current selection and create inverted selection */
	current_selection = gtk_selection_model_get_selection (sel_model);

	if (gtk_bitset_get_size (current_selection) > 0)
	{
		/* Create a bitset with all items selected, then remove currently selected */
		inverted = gtk_bitset_new_range (0, n_items);
		gtk_bitset_subtract (inverted, current_selection);

		/* Select the inverted set (everything that wasn't selected) */
		gtk_selection_model_set_selection (sel_model, inverted,
			gtk_bitset_new_range (0, n_items));

		gtk_bitset_unref (inverted);
		gtk_bitset_unref (current_selection);

		banlist_unban (NULL, banl);
	}
	else
	{
		gtk_bitset_unref (current_selection);
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
	}
#else /* GTK3 */
	GtkTreeSelection *select;
	GSList *list = NULL, *node;
	int num_sel;

	/* remember which bans are selected */
	select = gtk_tree_view_get_selection (get_view (sess));
	/* gtk_tree_selected_get_selected_rows() isn't present in gtk 2.0.x */
	gtk_tree_selection_selected_foreach (select, banlist_add_selected_cb,
	                                     &list);

	num_sel = g_slist_length (list);
	/* select all, then unselect those that we remembered */
	if (num_sel)
	{
		gtk_tree_selection_select_all (select);

		for (node = list; node; node = node->next)
			gtk_tree_selection_unselect_iter (select, node->data);

		g_slist_foreach (list, (GFunc)g_free, NULL);
		g_slist_free (list);

		banlist_unban (NULL, banl);
	} else
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
#endif /* HC_GTK4 */
}

static void
banlist_toggle (GtkWidget *item, gpointer data)
{
	banlist_info *banl = data;
	int i, bit = 0;

	for (i = 0; i < MODE_CT; i++)
		if (banl->checkboxes[i] == item)
		{
			bit = 1<<i;
			break;
		}

	if (bit)		/* Should be gassert() */
	{
		banl->checked &= ~bit;
		banl->checked |= (hc_check_button_get_active (item))? bit: 0;
		banlist_do_refresh (banl);
	}
}

/* NOTICE:  The official strptime() is not available on all platforms so
 * I've implemented a special version here.  The official version is
 * vastly more general than this:  it uses locales for weekday and month
 * names and its second arg is a format character-string.  This special
 * version depends on the format returned by ctime(3) whose manpage
 * says it returns:
 *     "a null-terminated string of the form "Wed Jun 30 21:49:08 1993\n"
 *
 * If the real strpftime() comes available, use this format string:
 *		#define DATE_FORMAT "%a %b %d %T %Y"
 */
static void
banlist_strptime (char *ti, struct tm *tm)
{
	/* Expect something like "Sat Mar 16 21:24:27 2013" */
	static char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
								  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };
	int M = -1, d = -1, h = -1, m = -1, s = -1, y = -1;

	if (*ti == 0)
	{
		memset (tm, 0, sizeof *tm);
		return;
	}
	/* No need to supply tm->tm_wday; mktime() doesn't read it */
	ti += 4;
	while ((mon[++M]))
		if (strncmp (ti, mon[M], 3) == 0)
			break;
	ti += 4;

	d = strtol (ti, &ti, 10);
	h = strtol (++ti, &ti, 10);
	m = strtol (++ti, &ti, 10);
	s = strtol (++ti, &ti, 10);
	y = strtol (++ti, NULL, 10) - 1900;

	tm->tm_sec = s;
	tm->tm_min = m;
	tm->tm_hour = h;
	tm->tm_mday = d;
	tm->tm_mon = M;
	tm->tm_year = y;
}

#if HC_GTK4
/* GTK4 sorting comparison function for date column */
static int
banlist_date_sort_gtk4 (gconstpointer a, gconstpointer b, gpointer user_data)
{
	HcBanItem *item_a = (HcBanItem *)a;
	HcBanItem *item_b = (HcBanItem *)b;
	struct tm tm1, tm2;
	time_t t1, t2;

	banlist_strptime (item_a->date, &tm1);
	banlist_strptime (item_b->date, &tm2);
	t1 = mktime (&tm1);
	t2 = mktime (&tm2);

	if (t1 < t2) return 1;
	if (t1 == t2) return 0;
	return -1;
}

/* Generic string sort for other columns */
static int
banlist_string_sort (gconstpointer a, gconstpointer b, gpointer user_data)
{
	HcBanItem *item_a = (HcBanItem *)a;
	HcBanItem *item_b = (HcBanItem *)b;
	int col = GPOINTER_TO_INT (user_data);
	const char *str_a = NULL, *str_b = NULL;

	switch (col)
	{
		case 0: str_a = item_a->type; str_b = item_b->type; break;
		case 1: str_a = item_a->mask; str_b = item_b->mask; break;
		case 2: str_a = item_a->from; str_b = item_b->from; break;
		default: return 0;
	}

	return g_strcmp0 (str_a, str_b);
}

/* Factory setup callback - creates label */
static void
banlist_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.5);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

/* Factory bind callbacks for each column */
static void
banlist_bind_type_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcBanItem *ban = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), ban->type ? ban->type : "");
}

static void
banlist_bind_mask_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcBanItem *ban = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), ban->mask ? ban->mask : "");
}

static void
banlist_bind_from_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcBanItem *ban = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), ban->from ? ban->from : "");
}

static void
banlist_bind_date_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcBanItem *ban = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), ban->date ? ban->date : "");
}

static GtkWidget *
banlist_columnview_new (GtkWidget *box, banlist_info *banl)
{
	GtkWidget *scroll;
	GListStore *store;
	GtkSortListModel *sort_model;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;
	GtkSorter *sorter;
	GtkSelectionModel *sel_model;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Create list store for ban items */
	store = g_list_store_new (HC_TYPE_BAN_ITEM);
	g_return_val_if_fail (store != NULL, NULL);

	/* Wrap with sort model - initially sort by date */
	sorter = GTK_SORTER (gtk_custom_sorter_new (banlist_date_sort_gtk4, NULL, NULL));
	sort_model = gtk_sort_list_model_new (G_LIST_MODEL (store), sorter);

	/* Create column view with multi-selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (sort_model), GTK_SELECTION_MULTIPLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Connect selection changed signal */
	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));
	g_signal_connect (sel_model, "selection-changed",
	                  G_CALLBACK (banlist_select_changed), banl);

	/* Add Type column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (banlist_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (banlist_bind_type_cb), NULL);
	col = gtk_column_view_column_new (_("Type"), factory);
	sorter = GTK_SORTER (gtk_custom_sorter_new (banlist_string_sort, GINT_TO_POINTER(0), NULL));
	gtk_column_view_column_set_sorter (col, sorter);
	g_object_unref (sorter);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Mask column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (banlist_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (banlist_bind_mask_cb), NULL);
	col = gtk_column_view_column_new (_("Mask"), factory);
	sorter = GTK_SORTER (gtk_custom_sorter_new (banlist_string_sort, GINT_TO_POINTER(1), NULL));
	gtk_column_view_column_set_sorter (col, sorter);
	g_object_unref (sorter);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add From column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (banlist_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (banlist_bind_from_cb), NULL);
	col = gtk_column_view_column_new (_("From"), factory);
	sorter = GTK_SORTER (gtk_custom_sorter_new (banlist_string_sort, GINT_TO_POINTER(2), NULL));
	gtk_column_view_column_set_sorter (col, sorter);
	g_object_unref (sorter);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Date column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (banlist_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (banlist_bind_date_cb), NULL);
	col = gtk_column_view_column_new (_("Date"), factory);
	sorter = GTK_SORTER (gtk_custom_sorter_new (banlist_date_sort_gtk4, NULL, NULL));
	gtk_column_view_column_set_sorter (col, sorter);
	g_object_unref (sorter);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add right-click gesture for context menu */
	hc_add_click_gesture (view, G_CALLBACK (banlist_button_pressed), NULL, NULL);

	/* Store references */
	g_object_set_data (G_OBJECT (view), "store", store);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (box, scroll, TRUE, TRUE, 0);

	return view;
}

#else /* GTK3 */

gint
banlist_date_sort (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	struct tm tm1, tm2;
	time_t t1, t2;
	char *time1, *time2;

	gtk_tree_model_get(model, a, DATE_COLUMN, &time1, -1);
	gtk_tree_model_get(model, b, DATE_COLUMN, &time2, -1);
	banlist_strptime (time1, &tm1);
	banlist_strptime (time2, &tm2);
	t1 = mktime (&tm1);
	t2 = mktime (&tm2);

	if (t1 < t2) return 1;
	if (t1 == t2) return 0;
	return -1;
}

static GtkWidget *
banlist_treeview_new (GtkWidget *box, banlist_info *banl)
{
	GtkListStore *store;
	GtkWidget *view;
	GtkTreeSelection *select;
	GtkTreeViewColumn *col;
	GtkTreeSortable *sortable;

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_STRING);
	g_return_val_if_fail (store != NULL, NULL);

	sortable = GTK_TREE_SORTABLE (store);
	gtk_tree_sortable_set_sort_func (sortable, DATE_COLUMN, banlist_date_sort, GINT_TO_POINTER (DATE_COLUMN), NULL);

	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store), NULL,
										  TYPE_COLUMN, _("Type"),
										  MASK_COLUMN, _("Mask"),
										  FROM_COLUMN, _("From"),
										  DATE_COLUMN, _("Date"), -1);
	g_signal_connect (G_OBJECT (view), "button-press-event", G_CALLBACK (banlist_button_pressed), NULL);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), MASK_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_min_width (col, 100);
	gtk_tree_view_column_set_sort_column_id (col, MASK_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, TRUE);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), TYPE_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, TYPE_COLUMN);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), FROM_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, FROM_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, TRUE);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), DATE_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, DATE_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, TRUE);

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	g_signal_connect (G_OBJECT (select), "changed", G_CALLBACK (banlist_select_changed), banl);
	gtk_tree_selection_set_mode (select, GTK_SELECTION_MULTIPLE);

	gtk_widget_show (view);
	return view;
}
#endif /* HC_GTK4 */

static void
banlist_closegui (GtkWidget *wid, banlist_info *banl)
{
	session *sess = banl->sess;

	if (sess->res->banlist == banl)
	{
		g_free (banl);
		sess->res->banlist = NULL;
	}
}

void
banlist_opengui (struct session *sess)
{
	banlist_info *banl;
	int i;
	GtkWidget *table, *vbox, *bbox;
	char tbuf[256];

	if (sess->type != SESS_CHANNEL || sess->channel[0] == 0)
	{
		fe_message (_("You can only open the Ban List window while in a channel tab."), FE_MSG_ERROR);
		return;
	}

	if (sess->res->banlist == NULL)
	{
		sess->res->banlist = g_new0 (banlist_info, 1);
	}
	banl = sess->res->banlist;
	if (banl->window)
	{
		mg_bring_tofront (banl->window);
		return;
	}

	/* New banlist for this session -- Initialize it */
	banl->sess = sess;
	/* For each mode set its bit in capable/readable/writeable */
	for (i = 0; i < MODE_CT; i++)
		modes[i].tester (banl, i);
	/* Force on the checkmark in the "Bans" box */
	banl->checked = 1<<MODE_BAN;

	g_snprintf (tbuf, sizeof tbuf, _("Ban List (%s) - %s"),
					sess->server->servername, _(DISPLAY_NAME));

	banl->window = mg_create_generic_tab ("BanList", tbuf, FALSE,
					TRUE, banlist_closegui, banl, 700, 300, &vbox, sess->server);
	gtkutil_destroy_on_esc (banl->window);

#if !HC_GTK4
	hc_container_set_border_width (banl->window, 3);
#endif
	gtk_box_set_spacing (GTK_BOX (vbox), 3);

	/* create banlist view */
#if HC_GTK4
	banl->treeview = banlist_columnview_new (vbox, banl);
#else
	banl->treeview = banlist_treeview_new (vbox, banl);
#endif

	table = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (table), 16);
	hc_box_pack_start (vbox, table, FALSE, FALSE, 0);

	for (i = 0; i < MODE_CT; i++)
	{
		if (!(banl->capable & 1<<i))
			continue;
		banl->checkboxes[i] = gtk_check_button_new_with_label (_(modes[i].name));
		hc_check_button_set_active (banl->checkboxes[i], (banl->checked & 1<<i? TRUE: FALSE));
		g_signal_connect (G_OBJECT (banl->checkboxes[i]), "toggled",
								G_CALLBACK (banlist_toggle), banl);
		gtk_grid_attach (GTK_GRID (table), banl->checkboxes[i], i+1, 0, 1, 1);
	}

	bbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (bbox, GTK_BUTTONBOX_SPREAD);
#if !HC_GTK4
	hc_container_set_border_width (bbox, 5);
#endif
	hc_box_pack_end (vbox, bbox, FALSE, FALSE, 0);
	hc_widget_show (bbox);

	banl->but_remove = gtkutil_button (bbox, "list-remove", 0, banlist_unban, banl,
	                _("Remove"));
	banl->but_crop = gtkutil_button (bbox, "list-remove", 0, banlist_crop, banl,
	                _("Crop"));
	banl->but_clear = gtkutil_button (bbox, "edit-clear", 0, banlist_clear, banl,
	                _("Clear"));

	banl->but_refresh = gtkutil_button (bbox, "view-refresh", 0, banlist_refresh, banl, _("Refresh"));

	banlist_do_refresh (banl);

	hc_widget_show_all (banl->window);
}
