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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#define PLUGIN_C
typedef struct session hexchat_context;
#include "../common/hexchat-plugin.h"
#include "../common/plugin.h"
#include "../common/util.h"
#include "../common/outbound.h"
#include "../common/fe.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "gtkutil.h"
#include "maingui.h"

#if !HC_GTK4
/* model for the plugin treeview (GTK3 only) */
enum
{
	NAME_COLUMN,
	VERSION_COLUMN,
	FILE_COLUMN,
	DESC_COLUMN,
	FILEPATH_COLUMN,
	N_COLUMNS
};
#endif

static GtkWidget *plugin_window = NULL;

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold plugin row data */
#define HC_TYPE_PLUGIN_ITEM (hc_plugin_item_get_type())
G_DECLARE_FINAL_TYPE (HcPluginItem, hc_plugin_item, HC, PLUGIN_ITEM, GObject)

struct _HcPluginItem {
	GObject parent;
	char *name;
	char *version;
	char *file;
	char *desc;
	char *filepath;
};

G_DEFINE_TYPE (HcPluginItem, hc_plugin_item, G_TYPE_OBJECT)

static void
hc_plugin_item_finalize (GObject *obj)
{
	HcPluginItem *item = HC_PLUGIN_ITEM (obj);
	g_free (item->name);
	g_free (item->version);
	g_free (item->file);
	g_free (item->desc);
	g_free (item->filepath);
	G_OBJECT_CLASS (hc_plugin_item_parent_class)->finalize (obj);
}

static void
hc_plugin_item_class_init (HcPluginItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_plugin_item_finalize;
}

static void
hc_plugin_item_init (HcPluginItem *item)
{
	item->name = NULL;
	item->version = NULL;
	item->file = NULL;
	item->desc = NULL;
	item->filepath = NULL;
}

static HcPluginItem *
hc_plugin_item_new (const char *name, const char *version,
                    const char *file, const char *desc, const char *filepath)
{
	HcPluginItem *item = g_object_new (HC_TYPE_PLUGIN_ITEM, NULL);
	item->name = g_strdup (name);
	item->version = g_strdup (version);
	item->file = g_strdup (file);
	item->desc = g_strdup (desc);
	item->filepath = g_strdup (filepath);
	return item;
}

/* Factory setup - create a label */
static void
plugingui_setup_label_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.5); /* center align like GTK3 */
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

/* Factory bind callbacks for each column */
static void
plugingui_bind_name_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcPluginItem *plugin = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), plugin->name ? plugin->name : "");
}

static void
plugingui_bind_version_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcPluginItem *plugin = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), plugin->version ? plugin->version : "");
}

static void
plugingui_bind_file_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcPluginItem *plugin = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), plugin->file ? plugin->file : "");
}

static void
plugingui_bind_desc_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcPluginItem *plugin = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), plugin->desc ? plugin->desc : "");
}

/* Get selected plugin item */
static HcPluginItem *
plugingui_get_selected_item (void)
{
	GtkColumnView *view;
	GtkSelectionModel *sel_model;

	if (!plugin_window)
		return NULL;

	view = g_object_get_data (G_OBJECT (plugin_window), "view");
	sel_model = gtk_column_view_get_model (view);

	return hc_selection_model_get_selected_item (sel_model);
}

static GtkWidget *
plugingui_columnview_new (GtkWidget *box)
{
	GListStore *store;
	GtkWidget *view;
	GtkWidget *scrolled;
	GtkColumnViewColumn *col;

	/* Create list store for plugin items */
	store = g_list_store_new (HC_TYPE_PLUGIN_ITEM);
	g_return_val_if_fail (store != NULL, NULL);

	/* Create column view with single selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (store), GTK_SELECTION_SINGLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Add columns */
	col = hc_column_view_add_column (GTK_COLUMN_VIEW (view), _("Name"),
	                                  G_CALLBACK (plugingui_setup_label_cb),
	                                  G_CALLBACK (plugingui_bind_name_cb), NULL);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, FALSE);

	col = hc_column_view_add_column (GTK_COLUMN_VIEW (view), _("Version"),
	                                  G_CALLBACK (plugingui_setup_label_cb),
	                                  G_CALLBACK (plugingui_bind_version_cb), NULL);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, FALSE);

	col = hc_column_view_add_column (GTK_COLUMN_VIEW (view), _("File"),
	                                  G_CALLBACK (plugingui_setup_label_cb),
	                                  G_CALLBACK (plugingui_bind_file_cb), NULL);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, FALSE);

	col = hc_column_view_add_column (GTK_COLUMN_VIEW (view), _("Description"),
	                                  G_CALLBACK (plugingui_setup_label_cb),
	                                  G_CALLBACK (plugingui_bind_desc_cb), NULL);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);

	/* Wrap in scrolled window */
	scrolled = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
	                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	hc_scrolled_window_set_child (scrolled, view);
	hc_box_pack_start (box, scrolled, TRUE, TRUE, 0);

	/* Store references */
	g_object_set_data (G_OBJECT (scrolled), "column-view", view);
	g_object_set_data (G_OBJECT (scrolled), "store", store);

	return scrolled;
}

#else /* GTK3 */

static GtkWidget *
plugingui_treeview_new (GtkWidget *box)
{
	GtkListStore *store;
	GtkWidget *view;
	GtkTreeViewColumn *col;
	int col_id;

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
	                            G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	g_return_val_if_fail (store != NULL, NULL);
	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store), NULL,
	                             NAME_COLUMN, _("Name"),
	                             VERSION_COLUMN, _("Version"),
	                             FILE_COLUMN, _("File"),
	                             DESC_COLUMN, _("Description"),
	                             FILEPATH_COLUMN, NULL, -1);
	hc_tree_view_set_rules_hint (view, TRUE);
	for (col_id=0; (col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_id));
	     col_id++)
			gtk_tree_view_column_set_alignment (col, 0.5);

	return view;
}

static char *
plugingui_getfilename (GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	GValue file;
	char *str;

	memset (&file, 0, sizeof (file));

	sel = gtk_tree_view_get_selection (view);
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		gtk_tree_model_get_value (model, &iter, FILEPATH_COLUMN, &file);

		str = g_value_dup_string (&file);
		g_value_unset (&file);

		return str;
	}

	return NULL;
}

#endif /* HC_GTK4 */

static void
plugingui_close (GtkWidget * wid, gpointer a)
{
	plugin_window = NULL;
}

extern GSList *plugin_list;

void
fe_pluginlist_update (void)
{
	hexchat_plugin *pl;
	GSList *list;
#if HC_GTK4
	GtkColumnView *view;
	GListStore *store;
#else
	GtkTreeView *view;
	GtkListStore *store;
	GtkTreeIter iter;
#endif

	if (!plugin_window)
		return;

	view = g_object_get_data (G_OBJECT (plugin_window), "view");

#if HC_GTK4
	store = g_object_get_data (G_OBJECT (plugin_window), "store");
	/* Clear the store */
	g_list_store_remove_all (store);

	list = plugin_list;
	while (list)
	{
		pl = list->data;
		if (pl->version[0] != 0)
		{
			HcPluginItem *item = hc_plugin_item_new (pl->name, pl->version,
			                                         file_part (pl->filename),
			                                         pl->desc, pl->filename);
			g_list_store_append (store, item);
			g_object_unref (item);
		}
		list = list->next;
	}
#else /* GTK3 */
	store = GTK_LIST_STORE (gtk_tree_view_get_model (view));
	gtk_list_store_clear (store);

	list = plugin_list;
	while (list)
	{
		pl = list->data;
		if (pl->version[0] != 0)
		{
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter, NAME_COLUMN, pl->name,
			                    VERSION_COLUMN, pl->version,
			                    FILE_COLUMN, file_part (pl->filename),
			                    DESC_COLUMN, pl->desc,
			                    FILEPATH_COLUMN, pl->filename, -1);
		}
		list = list->next;
	}
#endif
}

static void
plugingui_load_cb (session *sess, char *file)
{
	if (file)
	{
		char *buf;

		if (strchr (file, ' '))
			buf = g_strdup_printf ("LOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("LOAD %s", file);
		handle_command (sess, buf, FALSE);
		g_free (buf);
	}
}

void
plugingui_load (void)
{
	char *sub_dir = g_build_filename (get_xdir(), "addons", NULL);

	gtkutil_file_req (NULL, _("Select a Plugin or Script to load"), plugingui_load_cb, current_sess,
							sub_dir, "*."PLUGIN_SUFFIX";*.lua;*.pl;*.py;*.tcl;*.js", FRF_FILTERISINITIAL|FRF_EXTENSIONS);

	g_free (sub_dir);
}

static void
plugingui_loadbutton_cb (GtkWidget * wid, gpointer unused)
{
	plugingui_load ();
}

static void
plugingui_unload (GtkWidget * wid, gpointer unused)
{
#if HC_GTK4
	HcPluginItem *item;
	char *modname, *file;

	item = plugingui_get_selected_item ();
	if (!item)
		return;

	modname = g_strdup (item->name);
	file = g_strdup (item->filepath);
	g_object_unref (item);

	if (g_str_has_suffix (file, "."PLUGIN_SUFFIX))
	{
		if (plugin_kill (modname, FALSE) == 2)
			fe_message (_("That plugin is refusing to unload.\n"), FE_MSG_ERROR);
	}
	else
	{
		char *buf;
		/* let python.so or perl.so handle it */
		if (strchr (file, ' '))
			buf = g_strdup_printf ("UNLOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("UNLOAD %s", file);
		handle_command (current_sess, buf, FALSE);
		g_free (buf);
	}

	g_free (modname);
	g_free (file);
#else /* GTK3 */
	char *modname, *file;
	GtkTreeView *view;
	GtkTreeIter iter;

	view = g_object_get_data (G_OBJECT (plugin_window), "view");
	if (!gtkutil_treeview_get_selected (view, &iter, NAME_COLUMN, &modname,
	                                    FILEPATH_COLUMN, &file, -1))
		return;

	if (g_str_has_suffix (file, "."PLUGIN_SUFFIX))
	{
		if (plugin_kill (modname, FALSE) == 2)
			fe_message (_("That plugin is refusing to unload.\n"), FE_MSG_ERROR);
	}
	else
	{
		char *buf;
		/* let python.so or perl.so handle it */
		if (strchr (file, ' '))
			buf = g_strdup_printf ("UNLOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("UNLOAD %s", file);
		handle_command (current_sess, buf, FALSE);
		g_free (buf);
	}

	g_free (modname);
	g_free (file);
#endif
}

static void
plugingui_reloadbutton_cb (GtkWidget *wid, gpointer user_data)
{
#if HC_GTK4
	HcPluginItem *item;
	char *file;

	item = plugingui_get_selected_item ();
	if (!item)
		return;

	file = g_strdup (item->filepath);
	g_object_unref (item);
#else
	GtkTreeView *view = GTK_TREE_VIEW (user_data);
	char *file = plugingui_getfilename (view);
#endif

	if (file)
	{
		char *buf;

		if (strchr (file, ' '))
			buf = g_strdup_printf ("RELOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("RELOAD %s", file);
		handle_command (current_sess, buf, FALSE);
		g_free (buf);
		g_free (file);
	}
}

void
plugingui_open (void)
{
	GtkWidget *vbox, *hbox;
	char buf[128];
#if HC_GTK4
	GtkWidget *scrolled;
	GtkWidget *view;
	GListStore *store;
#else
	GtkWidget *view;
#endif

	if (plugin_window)
	{
		mg_bring_tofront (plugin_window);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Plugins and Scripts - %s"), _(DISPLAY_NAME));
	plugin_window = mg_create_generic_tab ("Addons", buf, FALSE, TRUE, plugingui_close, NULL,
														 700, 300, &vbox, 0);
	gtkutil_destroy_on_esc (plugin_window);

#if HC_GTK4
	scrolled = plugingui_columnview_new (vbox);
	view = g_object_get_data (G_OBJECT (scrolled), "column-view");
	store = g_object_get_data (G_OBJECT (scrolled), "store");
	g_object_set_data (G_OBJECT (plugin_window), "view", view);
	g_object_set_data (G_OBJECT (plugin_window), "store", store);
#else
	view = plugingui_treeview_new (vbox);
	g_object_set_data (G_OBJECT (plugin_window), "view", view);
#endif

	hbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (hbox, GTK_BUTTONBOX_SPREAD);
	hc_container_set_border_width (hbox, 5);
	hc_box_pack_end (vbox, hbox, 0, 0, 0);

	gtkutil_button (hbox, "document-revert", NULL,
	                plugingui_loadbutton_cb, NULL, _("_Load..."));

	gtkutil_button (hbox, "edit-delete", NULL,
	                plugingui_unload, NULL, _("_Unload"));

	gtkutil_button (hbox, "view-refresh", NULL,
	                plugingui_reloadbutton_cb, view, _("_Reload"));

	fe_pluginlist_update ();

	hc_widget_show_all (plugin_window);
}
