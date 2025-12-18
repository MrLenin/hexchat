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
#include <sys/types.h>
#include <sys/stat.h>

#include <gdk/gdkkeysyms.h>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "menu.h"
#include "gtkutil.h"
#include "maingui.h"
#include "editlist.h"


static GtkWidget *editlist_win = NULL;
static GSList *editlist_list = NULL;

/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold edit item row data */
#define HC_TYPE_EDIT_ITEM (hc_edit_item_get_type())
G_DECLARE_FINAL_TYPE (HcEditItem, hc_edit_item, HC, EDIT_ITEM, GObject)

struct _HcEditItem {
	GObject parent;
	char *name;
	char *cmd;
};

G_DEFINE_TYPE (HcEditItem, hc_edit_item, G_TYPE_OBJECT)

static void
hc_edit_item_finalize (GObject *obj)
{
	HcEditItem *item = HC_EDIT_ITEM (obj);
	g_free (item->name);
	g_free (item->cmd);
	G_OBJECT_CLASS (hc_edit_item_parent_class)->finalize (obj);
}

static void
hc_edit_item_class_init (HcEditItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_edit_item_finalize;
}

static void
hc_edit_item_init (HcEditItem *item)
{
	item->name = NULL;
	item->cmd = NULL;
}

static HcEditItem *
hc_edit_item_new (const char *name, const char *cmd)
{
	HcEditItem *item = g_object_new (HC_TYPE_EDIT_ITEM, NULL);
	item->name = g_strdup (name ? name : "");
	item->cmd = g_strdup (cmd ? cmd : "");
	return item;
}

static GListStore *
get_store (void)
{
	return g_object_get_data (G_OBJECT (editlist_win), "store");
}

static void
editlist_save (GtkWidget *igad, gchar *file)
{
	GListStore *store = get_store ();
	guint i, n_items;
	char buf[512];
	int fh;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
		for (i = 0; i < n_items; i++)
		{
			HcEditItem *item = g_list_model_get_item (G_LIST_MODEL (store), i);
			if (item)
			{
				g_snprintf (buf, sizeof (buf), "NAME %s\nCMD %s\n\n",
				            item->name ? item->name : "",
				            item->cmd ? item->cmd : "");
				write (fh, buf, strlen (buf));
				g_object_unref (item);
			}
		}

		close (fh);
		hc_window_destroy (editlist_win);
		if (editlist_list == replace_list)
		{
			list_free (&replace_list);
			list_loadconf (file, &replace_list, 0);
		} else if (editlist_list == popup_list)
		{
			list_free (&popup_list);
			list_loadconf (file, &popup_list, 0);
		} else if (editlist_list == button_list)
		{
			GSList *list = sess_list;
			struct session *sess;
			list_free (&button_list);
			list_loadconf (file, &button_list, 0);
			while (list)
			{
				sess = (struct session *) list->data;
				fe_buttons_update (sess);
				list = list->next;
			}
		} else if (editlist_list == dlgbutton_list)
		{
			GSList *list = sess_list;
			struct session *sess;
			list_free (&dlgbutton_list);
			list_loadconf (file, &dlgbutton_list, 0);
			while (list)
			{
				sess = (struct session *) list->data;
				fe_dlgbuttons_update (sess);
				list = list->next;
			}
		} else if (editlist_list == ctcp_list)
		{
			list_free (&ctcp_list);
			list_loadconf (file, &ctcp_list, 0);
		} else if (editlist_list == command_list)
		{
			list_free (&command_list);
			list_loadconf (file, &command_list, 0);
		} else if (editlist_list == usermenu_list)
		{
			list_free (&usermenu_list);
			list_loadconf (file, &usermenu_list, 0);
			usermenu_update ();
		} else
		{
			list_free (&urlhandler_list);
			list_loadconf (file, &urlhandler_list, 0);
		}
	}
}

static void
editlist_load (GListStore *store, GSList *list)
{
	struct popup *pop;

	while (list)
	{
		pop = (struct popup *) list->data;
		HcEditItem *item = hc_edit_item_new (pop->name, pop->cmd);
		g_list_store_append (store, item);
		g_object_unref (item);
		list = list->next;
	}
}

static void
editlist_delete (GtkWidget *wid, gpointer userdata)
{
	GtkColumnView *view = g_object_get_data (G_OBJECT (editlist_win), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store ();
	guint position;

	position = hc_selection_model_get_selected_position (sel_model);
	if (position != GTK_INVALID_LIST_POSITION)
	{
		guint n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
		g_list_store_remove (store, position);

		/* Select next item if available */
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
		if (n_items > 0)
		{
			if (position >= n_items)
				position = n_items - 1;
			gtk_selection_model_select_item (sel_model, position, TRUE);
		}
	}
}

static void
editlist_add (GtkWidget *wid, gpointer userdata)
{
	GtkColumnView *view = g_object_get_data (G_OBJECT (editlist_win), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store ();
	guint position;
	HcEditItem *item;

	/* Add new empty item */
	item = hc_edit_item_new ("", "");
	g_list_store_append (store, item);
	g_object_unref (item);

	/* Select the new item */
	position = g_list_model_get_n_items (G_LIST_MODEL (store)) - 1;
	gtk_selection_model_select_item (sel_model, position, TRUE);

	/* Scroll to show new item - GtkColumnView handles this via selection */
}

static void
editlist_close (GtkWidget *wid, gpointer userdata)
{
	hc_window_destroy (editlist_win);
	editlist_win = NULL;
}

/*
 * Key press handler for editlist
 * Uses GtkEventControllerKey with different signature
 */
static gboolean
editlist_keypress (GtkEventControllerKey *controller, guint keyval,
                   guint keycode, GdkModifierType state, gpointer userdata)
{
	GtkColumnView *view = g_object_get_data (G_OBJECT (editlist_win), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store ();
	guint position, n_items;
	gboolean handled = FALSE;
	int delta = 0;

	if (state & GDK_SHIFT_MASK)
	{
		if (keyval == GDK_KEY_Up)
		{
			handled = TRUE;
			delta = -1;
		}
		else if (keyval == GDK_KEY_Down)
		{
			handled = TRUE;
			delta = 1;
		}
	}

	if (handled)
	{
		position = hc_selection_model_get_selected_position (sel_model);
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));

		if (position != GTK_INVALID_LIST_POSITION)
		{
			guint new_pos;
			if (delta == -1 && position > 0)
				new_pos = position - 1;
			else if (delta == 1 && position < n_items - 1)
				new_pos = position + 1;
			else
				return FALSE; /* Can't move */

			/* Swap items in store */
			GObject *item1 = g_list_model_get_item (G_LIST_MODEL (store), position);
			GObject *item2 = g_list_model_get_item (G_LIST_MODEL (store), new_pos);

			/* Remove and re-insert to swap positions */
			g_object_ref (item1);
			g_object_ref (item2);
			g_list_store_remove (store, position > new_pos ? position : new_pos);
			g_list_store_remove (store, position > new_pos ? new_pos : position);

			if (delta == -1) {
				g_list_store_insert (store, position - 1, item1);
				g_list_store_insert (store, position, item2);
			} else {
				g_list_store_insert (store, position, item2);
				g_list_store_insert (store, position + 1, item1);
			}

			g_object_unref (item1);
			g_object_unref (item2);

			/* Select the moved item at its new position */
			gtk_selection_model_select_item (sel_model, new_pos, TRUE);
		}
	}

	return handled;
}

/*
 * GTK4 Implementation using GtkColumnView with GtkEditableLabel for inline editing
 */

/* Callback when editable label editing is done - updates the model */
static void
editlist_name_changed_cb (GtkEditableLabel *label, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item = GTK_LIST_ITEM (user_data);
	HcEditItem *item = gtk_list_item_get_item (list_item);
	const char *new_text;

	if (!item)
		return;

	new_text = gtk_editable_get_text (GTK_EDITABLE (label));
	g_free (item->name);
	item->name = g_strdup (new_text ? new_text : "");
}

static void
editlist_cmd_changed_cb (GtkEditableLabel *label, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item = GTK_LIST_ITEM (user_data);
	HcEditItem *item = gtk_list_item_get_item (list_item);
	const char *new_text;

	if (!item)
		return;

	new_text = gtk_editable_get_text (GTK_EDITABLE (label));
	g_free (item->cmd);
	item->cmd = g_strdup (new_text ? new_text : "");
}

/* Factory setup callback - creates an editable label */
static void
editlist_setup_name_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_editable_label_new ("");
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
editlist_setup_cmd_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_editable_label_new ("");
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

/* Factory bind callbacks */
static void
editlist_bind_name_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcEditItem *edit_item = gtk_list_item_get_item (item);

	gtk_editable_set_text (GTK_EDITABLE (label), edit_item->name ? edit_item->name : "");

	/* Connect signal to update model when editing completes */
	g_signal_connect (label, "notify::text", G_CALLBACK (editlist_name_changed_cb), item);
}

static void
editlist_bind_cmd_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcEditItem *edit_item = gtk_list_item_get_item (item);

	gtk_editable_set_text (GTK_EDITABLE (label), edit_item->cmd ? edit_item->cmd : "");

	/* Connect signal to update model when editing completes */
	g_signal_connect (label, "notify::text", G_CALLBACK (editlist_cmd_changed_cb), item);
}

/* Factory unbind callbacks - disconnect signals to avoid issues on rebind */
static void
editlist_unbind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	g_signal_handlers_disconnect_by_func (label, editlist_name_changed_cb, item);
	g_signal_handlers_disconnect_by_func (label, editlist_cmd_changed_cb, item);
}

static GtkWidget *
editlist_columnview_new (GtkWidget *box, char *title1, char *title2, GListStore **store_out)
{
	GtkWidget *scroll;
	GListStore *store;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Create list store for edit items */
	store = g_list_store_new (HC_TYPE_EDIT_ITEM);
	g_return_val_if_fail (store != NULL, NULL);

	/* Create column view with single selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (store), GTK_SELECTION_SINGLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_reorderable (GTK_COLUMN_VIEW (view), TRUE);

	/* Add Name column with editable label */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (editlist_setup_name_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (editlist_bind_name_cb), NULL);
	g_signal_connect (factory, "unbind", G_CALLBACK (editlist_unbind_cb), NULL);
	col = gtk_column_view_column_new (title1, factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, FALSE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Command column with editable label */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (editlist_setup_cmd_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (editlist_bind_cmd_cb), NULL);
	g_signal_connect (factory, "unbind", G_CALLBACK (editlist_unbind_cb), NULL);
	col = gtk_column_view_column_new (title2, factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add key controller for Shift+Up/Down row movement */
	hc_add_key_controller (view, G_CALLBACK (editlist_keypress), NULL, NULL);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (box, scroll, TRUE, TRUE, 0);

	*store_out = store;
	return view;
}


void
editlist_gui_open (char *title1, char *title2, GSList *list, char *title, char *wmclass,
					char *file, char *help)
{
	GtkWidget *vbox, *box;
	GtkWidget *view;
	GListStore *store;

	if (editlist_win)
	{
		mg_bring_tofront (editlist_win);
		return;
	}

	editlist_win = mg_create_generic_tab (wmclass, title, TRUE, FALSE,
												editlist_close, NULL, 450, 250, &vbox, 0);

	editlist_list = list;

	view = editlist_columnview_new (vbox, title1, title2, &store);
	g_object_set_data (G_OBJECT (editlist_win), "view", view);
	g_object_set_data (G_OBJECT (editlist_win), "store", store);

	if (help)
		gtk_widget_set_tooltip_text (view, help);

	box = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (box, GTK_BUTTONBOX_SPREAD);
	hc_box_pack_start (vbox, box, FALSE, FALSE, 2);
	hc_container_set_border_width (box, 5);
	gtk_widget_show (box);

	gtkutil_button (box, "document-new", 0, editlist_add,
					NULL, _("Add"));
	gtkutil_button (box, "edit-delete", 0, editlist_delete,
					NULL, _("Delete"));
	gtkutil_button (box, "process-stop", 0, editlist_close,
					NULL, _("Cancel"));
	gtkutil_button (box, "document-save", 0, editlist_save,
					file, _("Save"));

	editlist_load (store, list);

	gtk_widget_show (editlist_win);
}
