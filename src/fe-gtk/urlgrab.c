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

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/url.h"
#include "../common/tree.h"
#include "gtkutil.h"
#include "menu.h"
#include "maingui.h"
#include "urlgrab.h"

#if !HC_GTK4
/* model for the URL treeview (GTK3 only) */
enum
{
	URL_COLUMN,
	N_COLUMNS
};
#endif

static GtkWidget *urlgrabberwindow = 0;

#if HC_GTK4
/*
 * GTK4 Implementation using GtkStringList + GtkListView
 */

/* Get the selected URL string from the list view */
static gchar *
url_listview_get_selected_url (GtkWidget *view)
{
	GtkSelectionModel *sel_model;
	GtkStringObject *str_obj;
	const gchar *str;

	sel_model = gtk_list_view_get_model (GTK_LIST_VIEW (view));
	str_obj = hc_selection_model_get_selected_item (sel_model);
	if (!str_obj)
		return NULL;

	str = gtk_string_object_get_string (str_obj);
	gchar *result = g_strdup (str);
	g_object_unref (str_obj);
	return result;
}

/* Factory setup callback - creates a label widget */
static void
url_factory_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); /* left align */
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_list_item_set_child (item, label);
}

/* Factory bind callback - sets the label text from the string item */
static void
url_factory_bind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	GtkStringObject *str_obj = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), gtk_string_object_get_string (str_obj));
}

/* Double-click callback for activating URLs */
static void
url_listview_activate_cb (GtkListView *view, guint position, gpointer user_data)
{
	GtkSelectionModel *sel_model;
	GtkStringObject *str_obj;

	sel_model = gtk_list_view_get_model (view);
	str_obj = g_list_model_get_item (G_LIST_MODEL (gtk_selection_model_get_model (sel_model)), position);
	if (str_obj)
	{
		fe_open_url (gtk_string_object_get_string (str_obj));
		g_object_unref (str_obj);
	}
}

/* Right-click handler for context menu */
static void
url_click_pressed_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	GtkWidget *view = GTK_WIDGET (user_data);
	guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

	if (button == 3) /* right click */
	{
		gchar *url = url_listview_get_selected_url (view);
		if (url)
		{
			menu_urlmenu (view, x, y, url);
			g_free (url);
		}
	}
}

static GtkWidget *
url_listview_new (GtkWidget *box)
{
	GtkStringList *store;
	GtkWidget *view;
	GtkWidget *scrolled;
	GtkGesture *gesture;

	/* Create string list model */
	store = gtk_string_list_new (NULL);
	g_return_val_if_fail (store != NULL, NULL);

	/* Create list view with single selection */
	view = hc_list_view_new_simple (G_LIST_MODEL (store),
	                                GTK_SELECTION_SINGLE,
	                                G_CALLBACK (url_factory_setup_cb),
	                                G_CALLBACK (url_factory_bind_cb),
	                                NULL);

	/* Connect double-click activation */
	g_signal_connect (view, "activate", G_CALLBACK (url_listview_activate_cb), NULL);

	/* Add right-click gesture for context menu */
	gesture = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 3); /* right button */
	g_signal_connect (gesture, "pressed", G_CALLBACK (url_click_pressed_cb), view);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));

	/* Wrap in scrolled window */
	scrolled = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
	                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	hc_scrolled_window_set_child (scrolled, view);
	hc_box_pack_start (box, scrolled, TRUE, TRUE, 0);

	/* Store model reference on window for later access */
	g_object_set_data (G_OBJECT (scrolled), "url-view", view);
	g_object_set_data (G_OBJECT (scrolled), "url-store", store);

	return scrolled;
}

#else /* GTK3 */

static gboolean
url_treeview_url_clicked_cb (GtkWidget *view, GdkEventButton *event,
                             gpointer data)
{
	GtkTreeIter iter;
	gchar *url;
	GtkTreeSelection *sel;
	GtkTreePath *path;
	GtkTreeView *tree = GTK_TREE_VIEW (view);

	if (!event || !gtk_tree_view_get_path_at_pos (tree, event->x, event->y, &path, 0, 0, 0))
		return FALSE;

	/* select what they right-clicked on */
	sel = gtk_tree_view_get_selection (tree);
	gtk_tree_selection_unselect_all (sel);
	gtk_tree_selection_select_path (sel, path);
	gtk_tree_path_free (path);

	if (!gtkutil_treeview_get_selected (GTK_TREE_VIEW (view), &iter,
	                                    URL_COLUMN, &url, -1))
		return FALSE;

	switch (event->button)
	{
		case 1:
			if (event->type == GDK_2BUTTON_PRESS)
				fe_open_url (url);
			break;
		case 3:
			menu_urlmenu (event, url);
			break;
		default:
			break;
	}
	g_free (url);

	return FALSE;
}

static GtkWidget *
url_treeview_new (GtkWidget *box)
{
	GtkListStore *store;
	GtkWidget *view;

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING);
	g_return_val_if_fail (store != NULL, NULL);

	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store), NULL,
	                             URL_COLUMN, _("URL"), -1);
	g_signal_connect (G_OBJECT (view), "button_press_event",
	                  G_CALLBACK (url_treeview_url_clicked_cb), NULL);
	/* don't want column headers */
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
	gtk_widget_show (view);
	return view;
}

#endif /* HC_GTK4 */

static void
url_closegui (GtkWidget *wid, gpointer userdata)
{
	urlgrabberwindow = 0;
}

#if HC_GTK4

static void
url_button_clear (void)
{
	GtkStringList *store;

	url_clear ();
	store = GTK_STRING_LIST (g_object_get_data (G_OBJECT (urlgrabberwindow), "model"));
	/* GtkStringList doesn't have a clear method, so we remove all items */
	while (g_list_model_get_n_items (G_LIST_MODEL (store)) > 0)
		gtk_string_list_remove (store, 0);
}

static void
url_button_copy (GtkWidget *widget, gpointer data)
{
	GtkWidget *view = GTK_WIDGET (data);
	gchar *url;

	url = url_listview_get_selected_url (view);
	if (url)
	{
		gtkutil_copy_to_clipboard (view, FALSE, url);
		g_free (url);
	}
}

#else /* GTK3 */

static void
url_button_clear (void)
{
	GtkListStore *store;

	url_clear ();
	store = GTK_LIST_STORE (g_object_get_data (G_OBJECT (urlgrabberwindow),
	                                           "model"));
	gtk_list_store_clear (store);
}

static void
url_button_copy (GtkWidget *widget, gpointer data)
{
	GtkTreeView *view = GTK_TREE_VIEW (data);
	GtkTreeIter iter;
	gchar *url = NULL;

	if (gtkutil_treeview_get_selected (view, &iter, URL_COLUMN, &url, -1))
	{
		gtkutil_copy_to_clipboard (GTK_WIDGET (view), NULL, url);
		g_free (url);
	}
}

#endif /* HC_GTK4 */

static void
url_save_callback (void *arg1, char *file)
{
	if (file)
	{
		url_save_tree (file, "w", TRUE);
	}
}

static void
url_button_save (void)
{
	gtkutil_file_req (NULL, _("Select an output filename"),
							url_save_callback, NULL, NULL, NULL, FRF_WRITE);
}

void
fe_url_add (const char *urltext)
{
#if HC_GTK4
	GtkStringList *store;
	guint n_items;

	if (urlgrabberwindow)
	{
		store = GTK_STRING_LIST (g_object_get_data (G_OBJECT (urlgrabberwindow), "model"));

		/* GtkStringList doesn't have prepend, so we splice at position 0 */
		const char *strings[] = { urltext, NULL };
		gtk_string_list_splice (store, 0, 0, strings);

		/* remove any overflow */
		if (prefs.hex_url_grabber_limit > 0)
		{
			n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
			while (n_items > (guint)prefs.hex_url_grabber_limit)
			{
				gtk_string_list_remove (store, n_items - 1);
				n_items--;
			}
		}
	}
#else /* GTK3 */
	GtkListStore *store;
	GtkTreeIter iter;
	gboolean valid;

	if (urlgrabberwindow)
	{
		store = GTK_LIST_STORE (g_object_get_data (G_OBJECT (urlgrabberwindow),
		                                           "model"));
		gtk_list_store_prepend (store, &iter);
		gtk_list_store_set (store, &iter,
		                    URL_COLUMN, urltext,
		                    -1);

		/* remove any overflow */
		if (prefs.hex_url_grabber_limit > 0)
		{
			valid = gtk_tree_model_iter_nth_child (
				GTK_TREE_MODEL (store), &iter, NULL, prefs.hex_url_grabber_limit);
			while (valid)
				valid = gtk_list_store_remove (store, &iter);
		}
	}
#endif
}

static int
populate_cb (char *urltext, gpointer userdata)
{
	fe_url_add (urltext);
	return TRUE;
}

void
url_opengui ()
{
	GtkWidget *vbox, *hbox, *view;
	char buf[128];
#if HC_GTK4
	GtkWidget *scrolled;
	GtkStringList *store;
#endif

	if (urlgrabberwindow)
	{
		mg_bring_tofront (urlgrabberwindow);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("URL Grabber - %s"), _(DISPLAY_NAME));
	urlgrabberwindow =
		mg_create_generic_tab ("UrlGrabber", buf, FALSE, TRUE, url_closegui, NULL,
							 400, 256, &vbox, 0);
	gtkutil_destroy_on_esc (urlgrabberwindow);

#if HC_GTK4
	/* GTK4: url_listview_new returns the scrolled window container */
	scrolled = url_listview_new (vbox);
	view = g_object_get_data (G_OBJECT (scrolled), "url-view");
	store = g_object_get_data (G_OBJECT (scrolled), "url-store");
	g_object_set_data (G_OBJECT (urlgrabberwindow), "model", store);
	g_object_set_data (G_OBJECT (urlgrabberwindow), "view", view);
#else
	view = url_treeview_new (vbox);
	g_object_set_data (G_OBJECT (urlgrabberwindow), "model",
	                   gtk_tree_view_get_model (GTK_TREE_VIEW (view)));
#endif

	hbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (hbox, GTK_BUTTONBOX_SPREAD);
	hc_container_set_border_width (hbox, 5);
	hc_box_pack_end (vbox, hbox, 0, 0, 0);
	gtk_widget_show (hbox);

	gtkutil_button (hbox, "edit-clear",
						 _("Clear list"), url_button_clear, 0, _("Clear"));
	gtkutil_button (hbox, "edit-copy",
						 _("Copy selected URL"), url_button_copy, view, _("Copy"));
	gtkutil_button (hbox, "document-save-as",
						 _("Save list to a file"), url_button_save, 0, _("Save As..."));

	gtk_widget_show (urlgrabberwindow);

	if (prefs.hex_url_grabber)
		tree_foreach (url_tree, (tree_traverse_func *)populate_cb, NULL);
	else
	{
#if HC_GTK4
		/* Clear the string list */
		while (g_list_model_get_n_items (G_LIST_MODEL (store)) > 0)
			gtk_string_list_remove (store, 0);
#else
		gtk_list_store_clear (GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (view))));
#endif
		fe_url_add ("URL Grabber is disabled.");
	}
}
