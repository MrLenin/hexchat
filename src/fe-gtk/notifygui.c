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

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/notify.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/util.h"
#include "../common/userlist.h"
#include "../common/outbound.h"
#include "gtkutil.h"
#include "maingui.h"
#include "palette.h"
#include "notifygui.h"


#if !HC_GTK4
/* model for the notify treeview (GTK3 only) */
enum
{
	USER_COLUMN,
	STATUS_COLUMN,
	SERVER_COLUMN,
	SEEN_COLUMN,
	COLOUR_COLUMN,
	NPS_COLUMN, 	/* struct notify_per_server * */
	N_COLUMNS
};
#endif

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold notify row data */
#define HC_TYPE_NOTIFY_ITEM (hc_notify_item_get_type())
G_DECLARE_FINAL_TYPE (HcNotifyItem, hc_notify_item, HC, NOTIFY_ITEM, GObject)

struct _HcNotifyItem {
	GObject parent;
	char *user;
	char *status;
	char *server;
	char *seen;
	GdkRGBA *colour;
	struct notify_per_server *nps;
};

G_DEFINE_TYPE (HcNotifyItem, hc_notify_item, G_TYPE_OBJECT)

static void
hc_notify_item_finalize (GObject *obj)
{
	HcNotifyItem *item = HC_NOTIFY_ITEM (obj);
	g_free (item->user);
	g_free (item->status);
	g_free (item->server);
	g_free (item->seen);
	/* colour is a pointer to static colors[] array, don't free */
	G_OBJECT_CLASS (hc_notify_item_parent_class)->finalize (obj);
}

static void
hc_notify_item_class_init (HcNotifyItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_notify_item_finalize;
}

static void
hc_notify_item_init (HcNotifyItem *item)
{
	item->user = NULL;
	item->status = NULL;
	item->server = NULL;
	item->seen = NULL;
	item->colour = NULL;
	item->nps = NULL;
}

static HcNotifyItem *
hc_notify_item_new (const char *user, const char *status, const char *server,
                    const char *seen, GdkRGBA *colour, struct notify_per_server *nps)
{
	HcNotifyItem *item = g_object_new (HC_TYPE_NOTIFY_ITEM, NULL);
	item->user = g_strdup (user ? user : "");
	item->status = g_strdup (status ? status : "");
	item->server = g_strdup (server ? server : "");
	item->seen = g_strdup (seen ? seen : "");
	item->colour = colour;
	item->nps = nps;
	return item;
}

static GListStore *notify_store = NULL;
#endif /* HC_GTK4 */


static GtkWidget *notify_window = 0;
static GtkWidget *notify_button_opendialog;
static GtkWidget *notify_button_remove;


static void
notify_closegui (void)
{
	notify_window = 0;
	notify_save ();
}

#if !HC_GTK4
/* Need this to be able to set the foreground colour property of a row
 * from a GdkRGBA * in the model  -Vince
 */
static void
notify_treecell_property_mapper (GtkTreeViewColumn *col, GtkCellRenderer *cell,
                                 GtkTreeModel *model, GtkTreeIter *iter,
                                 gpointer data)
{
	gchar *text;
	GdkRGBA *colour;
	int model_column = GPOINTER_TO_INT (data);

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
	                    COLOUR_COLUMN, &colour,
	                    model_column, &text, -1);
	g_object_set (G_OBJECT (cell), "text", text, NULL);
	g_object_set (G_OBJECT (cell), "foreground-rgba", colour, NULL);
	g_free (text);
}
#endif /* !HC_GTK4 */

#if HC_GTK4
static void
notify_row_cb (GtkSelectionModel *sel_model, guint position, guint n_items, gpointer userdata)
{
	HcNotifyItem *item;

	item = hc_selection_model_get_selected_item (sel_model);
	if (item)
	{
		gtk_widget_set_sensitive (notify_button_opendialog, item->nps ? item->nps->ison : 0);
		gtk_widget_set_sensitive (notify_button_remove, TRUE);
		g_object_unref (item);
		return;
	}

	gtk_widget_set_sensitive (notify_button_opendialog, FALSE);
	gtk_widget_set_sensitive (notify_button_remove, FALSE);
}
#else /* GTK3 */
static void
notify_row_cb (GtkTreeSelection *sel, GtkTreeView *view)
{
	GtkTreeIter iter;
	struct notify_per_server *servnot;

	if (gtkutil_treeview_get_selected (view, &iter, NPS_COLUMN, &servnot, -1))
	{
		gtk_widget_set_sensitive (notify_button_opendialog, servnot ? servnot->ison : 0);
		gtk_widget_set_sensitive (notify_button_remove, TRUE);
		return;
	}

	gtk_widget_set_sensitive (notify_button_opendialog, FALSE);
	gtk_widget_set_sensitive (notify_button_remove, FALSE);
}
#endif /* HC_GTK4 */

#if HC_GTK4
/*
 * GTK4 Column View factory callbacks with color support
 */

/* Generic setup - creates a label */
static void
notify_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.5);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

/* Helper to apply color to label */
static void
notify_apply_colour (GtkWidget *label, GdkRGBA *colour)
{
	if (colour)
	{
		char css[128];
		GtkCssProvider *provider;
		g_snprintf (css, sizeof(css), "label { color: rgba(%d,%d,%d,%.2f); }",
		            (int)(colour->red * 255), (int)(colour->green * 255),
		            (int)(colour->blue * 255), colour->alpha);
		provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_string (provider, css);
		gtk_style_context_add_provider (gtk_widget_get_style_context (label),
		                                GTK_STYLE_PROVIDER (provider),
		                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (provider);
	}
}

static void
notify_bind_user_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcNotifyItem *notify = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), notify->user ? notify->user : "");
	notify_apply_colour (label, notify->colour);
}

static void
notify_bind_status_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcNotifyItem *notify = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), notify->status ? notify->status : "");
	notify_apply_colour (label, notify->colour);
}

static void
notify_bind_server_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcNotifyItem *notify = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), notify->server ? notify->server : "");
	notify_apply_colour (label, notify->colour);
}

static void
notify_bind_seen_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcNotifyItem *notify = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), notify->seen ? notify->seen : "");
	notify_apply_colour (label, notify->colour);
}

static GtkWidget *
notify_columnview_new (GtkWidget *box)
{
	GtkWidget *scroll;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;
	GtkSelectionModel *sel_model;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Create list store for notify items */
	notify_store = g_list_store_new (HC_TYPE_NOTIFY_ITEM);
	g_return_val_if_fail (notify_store != NULL, NULL);

	/* Create column view with single selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (notify_store), GTK_SELECTION_SINGLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Connect selection changed signal */
	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));
	g_signal_connect (sel_model, "selection-changed",
	                  G_CALLBACK (notify_row_cb), NULL);

	/* Add Name column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (notify_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (notify_bind_user_cb), NULL);
	col = gtk_column_view_column_new (_("Name"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Status column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (notify_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (notify_bind_status_cb), NULL);
	col = gtk_column_view_column_new (_("Status"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Network column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (notify_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (notify_bind_server_cb), NULL);
	col = gtk_column_view_column_new (_("Network"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Last Seen column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (notify_setup_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (notify_bind_seen_cb), NULL);
	col = gtk_column_view_column_new (_("Last Seen"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (box, scroll, TRUE, TRUE, 0);

	return view;
}

#else /* GTK3 */

static GtkWidget *
notify_treeview_new (GtkWidget *box)
{
	GtkListStore *store;
	GtkWidget *view;
	GtkTreeViewColumn *col;
	int col_id;

	store = gtk_list_store_new (N_COLUMNS,
	                            G_TYPE_STRING,
	                            G_TYPE_STRING,
	                            G_TYPE_STRING,
	                            G_TYPE_STRING,
	                            G_TYPE_POINTER,	/* can't specify colour! */
										 G_TYPE_POINTER
	                           );
	g_return_val_if_fail (store != NULL, NULL);

	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store),
	                             notify_treecell_property_mapper,
	                             USER_COLUMN, _("Name"),
	                             STATUS_COLUMN, _("Status"),
	                             SERVER_COLUMN, _("Network"),
	                             SEEN_COLUMN, _("Last Seen"), -1);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), 0), TRUE);

	for (col_id=0; (col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_id));
	     col_id++)
			gtk_tree_view_column_set_alignment (col, 0.5);

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (view))),
							"changed", G_CALLBACK (notify_row_cb), view);

	gtk_widget_show (view);
	return view;
}
#endif /* HC_GTK4 */

void
notify_gui_update (void)
{
	struct notify *notify;
	struct notify_per_server *servnot;
	GSList *list = notify_list;
	GSList *slist;
	gchar *name, *status, *server, *seen;
	int online, servcount, lastseenminutes;
	time_t lastseen;
	char agobuf[128];

#if HC_GTK4
	GtkColumnView *view;
	GtkSelectionModel *sel_model;
	HcNotifyItem *item;

	if (!notify_window)
		return;

	view = g_object_get_data (G_OBJECT (notify_window), "view");

	/* Clear store and rebuild */
	g_list_store_remove_all (notify_store);

	while (list)
	{
		notify = (struct notify *) list->data;
		name = notify->name;
		status = _("Offline");
		server = "";

		online = FALSE;
		lastseen = 0;
		/* First see if they're online on any servers */
		slist = notify->server_list;
		while (slist)
		{
			servnot = (struct notify_per_server *) slist->data;
			if (servnot->ison)
				online = TRUE;
			if (servnot->lastseen > lastseen)
				lastseen = servnot->lastseen;
			slist = slist->next;
		}

		if (!online)				  /* Offline on all servers */
		{
			if (!lastseen)
				seen = _("Never");
			else
			{
				lastseenminutes = (int)(time (0) - lastseen) / 60;
				if (lastseenminutes < 60)
					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), lastseenminutes);
				else if (lastseenminutes < 120)
					g_snprintf (agobuf, sizeof (agobuf), _("An hour ago"));
				else
					g_snprintf (agobuf, sizeof (agobuf), _("%d hours ago"), lastseenminutes / 60);
				seen = agobuf;
			}
			item = hc_notify_item_new (name, status, server, seen, &colors[4], NULL);
			g_list_store_append (notify_store, item);
			g_object_unref (item);

		} else
		{
			/* Online - add one line per server */
			servcount = 0;
			slist = notify->server_list;
			status = _("Online");
			while (slist)
			{
				servnot = (struct notify_per_server *) slist->data;
				if (servnot->ison)
				{
					if (servcount > 0)
						name = "";
					server = server_get_network (servnot->server, TRUE);

					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), (int)(time (0) - lastseen) / 60);
					seen = agobuf;

					item = hc_notify_item_new (name, status, server, seen, &colors[3], servnot);
					g_list_store_append (notify_store, item);
					g_object_unref (item);

					servcount++;
				}
				slist = slist->next;
			}
		}

		list = list->next;
	}

	sel_model = gtk_column_view_get_model (view);
	notify_row_cb (sel_model, 0, 0, NULL);

#else /* GTK3 */
	GtkListStore *store;
	GtkTreeView *view;
	GtkTreeIter iter;
	gboolean valid;	/* true if we don't need to append a new tree row */

	if (!notify_window)
		return;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	store = GTK_LIST_STORE (gtk_tree_view_get_model (view));
	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);

	while (list)
	{
		notify = (struct notify *) list->data;
		name = notify->name;
		status = _("Offline");
		server = "";

		online = FALSE;
		lastseen = 0;
		/* First see if they're online on any servers */
		slist = notify->server_list;
		while (slist)
		{
			servnot = (struct notify_per_server *) slist->data;
			if (servnot->ison)
				online = TRUE;
			if (servnot->lastseen > lastseen)
				lastseen = servnot->lastseen;
			slist = slist->next;
		}

		if (!online)				  /* Offline on all servers */
		{
			if (!lastseen)
				seen = _("Never");
			else
			{
				lastseenminutes = (int)(time (0) - lastseen) / 60;
				if (lastseenminutes < 60)
					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), lastseenminutes);
				else if (lastseenminutes < 120)
					g_snprintf (agobuf, sizeof (agobuf), _("An hour ago"));
				else
					g_snprintf (agobuf, sizeof (agobuf), _("%d hours ago"), lastseenminutes / 60);
				seen = agobuf;
			}
			if (!valid)	/* create new tree row if required */
				gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter, 0, name, 1, status,
			                    2, server, 3, seen, 4, &colors[4], 5, NULL, -1);
			if (valid)
				valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);

		} else
		{
			/* Online - add one line per server */
			servcount = 0;
			slist = notify->server_list;
			status = _("Online");
			while (slist)
			{
				servnot = (struct notify_per_server *) slist->data;
				if (servnot->ison)
				{
					if (servcount > 0)
						name = "";
					server = server_get_network (servnot->server, TRUE);

					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), (int)(time (0) - lastseen) / 60);
					seen = agobuf;

					if (!valid)	/* create new tree row if required */
						gtk_list_store_append (store, &iter);
					gtk_list_store_set (store, &iter, 0, name, 1, status,
					                    2, server, 3, seen, 4, &colors[3], 5, servnot, -1);
					if (valid)
						valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);

					servcount++;
				}
				slist = slist->next;
			}
		}

		list = list->next;
	}

	while (valid)
	{
		GtkTreeIter old = iter;
		/* get next iter now because removing invalidates old one */
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store),
                                      &iter);
		gtk_list_store_remove (store, &old);
	}

	notify_row_cb (gtk_tree_view_get_selection (view), view);
#endif /* HC_GTK4 */
}

static void
notify_opendialog_clicked (GtkWidget * igad)
{
#if HC_GTK4
	GtkColumnView *view;
	GtkSelectionModel *sel_model;
	HcNotifyItem *item;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	sel_model = gtk_column_view_get_model (view);
	item = hc_selection_model_get_selected_item (sel_model);
	if (item)
	{
		if (item->nps)
			open_query (item->nps->server, item->nps->notify->name, TRUE);
		g_object_unref (item);
	}
#else /* GTK3 */
	GtkTreeView *view;
	GtkTreeIter iter;
	struct notify_per_server *servnot;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	if (gtkutil_treeview_get_selected (view, &iter, NPS_COLUMN, &servnot, -1))
	{
		if (servnot)
			open_query (servnot->server, servnot->notify->name, TRUE);
	}
#endif /* HC_GTK4 */
}

static void
notify_remove_clicked (GtkWidget * igad)
{
#if HC_GTK4
	GtkColumnView *view;
	GtkSelectionModel *sel_model;
	HcNotifyItem *item;
	guint pos;
	char *name = NULL;
	gboolean found = FALSE;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	sel_model = gtk_column_view_get_model (view);
	pos = hc_selection_model_get_selected_position (sel_model);
	if (pos == GTK_INVALID_LIST_POSITION)
		return;

	item = g_list_model_get_item (G_LIST_MODEL (notify_store), pos);
	if (item)
	{
		/* Check if this has the real name, or if we need to search backwards */
		if (item->user && item->user[0] != 0)
		{
			name = g_strdup (item->user);
			found = TRUE;
		}
		g_object_unref (item);

		/* Search backwards for the real nick */
		while (!found && pos > 0)
		{
			pos--;
			item = g_list_model_get_item (G_LIST_MODEL (notify_store), pos);
			if (item)
			{
				if (item->user && item->user[0] != 0)
				{
					name = g_strdup (item->user);
					found = TRUE;
				}
				g_object_unref (item);
			}
		}

		if (found && name)
		{
			notify_deluser (name);
			g_free (name);
		}
	}
#else /* GTK3 */
	GtkTreeView *view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path = NULL;
	gboolean found = FALSE;
	char *name;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	if (gtkutil_treeview_get_selected (view, &iter, USER_COLUMN, &name, -1))
	{
		model = gtk_tree_view_get_model (view);
		found = (*name != 0);
		while (!found)	/* the real nick is some previous node */
		{
			g_free (name); /* it's useless to us */
			if (!path)
				path = gtk_tree_model_get_path (model, &iter);
			if (!gtk_tree_path_prev (path))	/* arrgh! no previous node! */
			{
				g_warning ("notify list state is invalid\n");
				break;
			}
			if (!gtk_tree_model_get_iter (model, &iter, path))
				break;
			gtk_tree_model_get (model, &iter, USER_COLUMN, &name, -1);
			found = (*name != 0);
		}
		if (path)
			gtk_tree_path_free (path);
		if (!found)
			return;

		/* ok, now we can remove it */
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
		notify_deluser (name);
		g_free (name);
	}
#endif /* HC_GTK4 */
}

static void
notifygui_add_cb (GtkDialog *dialog, gint response, gpointer entry)
{
	char *networks;
	char *text;

	text = (char *)hc_entry_get_text (entry);
	if (text[0] && response == GTK_RESPONSE_ACCEPT)
	{
		networks = (char*)hc_entry_get_text (g_object_get_data (G_OBJECT (entry), "net"));
		if (g_ascii_strcasecmp (networks, "ALL") == 0 || networks[0] == 0)
			notify_adduser (text, NULL);
		else
			notify_adduser (text, networks);
	}

	hc_window_destroy (GTK_WIDGET (dialog));
}

static void
notifygui_add_enter (GtkWidget *entry, GtkWidget *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
}

void
fe_notify_ask (char *nick, char *networks)
{
	GtkWidget *dialog;
	GtkWidget *entry;
	GtkWidget *label;
	GtkWidget *wid;
	GtkWidget *table;
	char *msg = _("Enter nickname to add:");
	char buf[256];

	dialog = gtk_dialog_new_with_buttons (msg, NULL, 0,
										"_Cancel", GTK_RESPONSE_REJECT,
										"_OK", GTK_RESPONSE_ACCEPT,
										NULL);
	if (parent_window)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	hc_window_set_position (dialog, GTK_WIN_POS_MOUSE);

	table = gtk_grid_new ();
	hc_container_set_border_width (table, 12);
	gtk_grid_set_row_spacing (GTK_GRID (table), 3);
	gtk_grid_set_column_spacing (GTK_GRID (table), 8);
	hc_box_add (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), table);

	label = gtk_label_new (msg);
	gtk_grid_attach (GTK_GRID (table), label, 0, 0, 1, 1);

	entry = gtk_entry_new ();
	hc_entry_set_text (entry, nick);
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (notifygui_add_enter), dialog);
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_grid_attach (GTK_GRID (table), entry, 1, 0, 1, 1);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (notifygui_add_cb), entry);

	label = gtk_label_new (_("Notify on these networks:"));
	gtk_grid_attach (GTK_GRID (table), label, 0, 2, 1, 1);

	wid = gtk_entry_new ();
	g_object_set_data (G_OBJECT (entry), "net", wid);
	g_signal_connect (G_OBJECT (wid), "activate",
						 	G_CALLBACK (notifygui_add_enter), dialog);
	hc_entry_set_text (wid, networks ? networks : "ALL");
	gtk_widget_set_hexpand (wid, TRUE);
	gtk_grid_attach (GTK_GRID (table), wid, 1, 2, 1, 1);

	label = gtk_label_new (NULL);
	g_snprintf (buf, sizeof (buf), "<i><span size=\"smaller\">%s</span></i>", _("Comma separated list of networks is accepted."));
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_grid_attach (GTK_GRID (table), label, 1, 3, 1, 1);

	hc_widget_show_all (dialog);
}

static void
notify_add_clicked (GtkWidget * igad)
{
	fe_notify_ask ("", NULL);
}

void
notify_opengui (void)
{
	GtkWidget *vbox, *bbox;
	GtkWidget *view;
	char buf[128];

	if (notify_window)
	{
		mg_bring_tofront (notify_window);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Friends List - %s"), _(DISPLAY_NAME));
	notify_window =
		mg_create_generic_tab ("Notify", buf, FALSE, TRUE, notify_closegui, NULL, 400,
								250, &vbox, 0);
	gtkutil_destroy_on_esc (notify_window);

#if HC_GTK4
	view = notify_columnview_new (vbox);
#else
	view = notify_treeview_new (vbox);
#endif
	g_object_set_data (G_OBJECT (notify_window), "view", view);
  
	bbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (bbox, GTK_BUTTONBOX_SPREAD);
	hc_container_set_border_width (bbox, 5);
	hc_box_pack_end (vbox, bbox, 0, 0, 0);
	gtk_widget_show (bbox);

	gtkutil_button (bbox, "document-new", 0, notify_add_clicked, 0,
	                _("Add..."));

	notify_button_remove =
	gtkutil_button (bbox, "edit-delete", 0, notify_remove_clicked, 0,
	                _("Remove"));

	notify_button_opendialog =
	gtkutil_button (bbox, NULL, 0, notify_opendialog_clicked, 0,
	                _("Open Dialog"));

	gtk_widget_set_sensitive (notify_button_opendialog, FALSE);
	gtk_widget_set_sensitive (notify_button_remove, FALSE);

	notify_gui_update ();

	gtk_widget_show (notify_window);
}
