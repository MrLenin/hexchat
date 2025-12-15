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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/ignore.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "maingui.h"

#if !HC_GTK4
/* model for the ignore treeview (GTK3 only) */
enum
{
	MASK_COLUMN,
	CHAN_COLUMN,
	PRIV_COLUMN,
	NOTICE_COLUMN,
	CTCP_COLUMN,
	DCC_COLUMN,
	INVITE_COLUMN,
	UNIGNORE_COLUMN,
	N_COLUMNS
};
#endif

static GtkWidget *ignorewin = 0;

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold ignore row data */
#define HC_TYPE_IGNORE_ITEM (hc_ignore_item_get_type())
G_DECLARE_FINAL_TYPE (HcIgnoreItem, hc_ignore_item, HC, IGNORE_ITEM, GObject)

struct _HcIgnoreItem {
	GObject parent;
	char *mask;
	gboolean chan;
	gboolean priv;
	gboolean notice;
	gboolean ctcp;
	gboolean dcc;
	gboolean invite;
	gboolean unignore;
};

G_DEFINE_TYPE (HcIgnoreItem, hc_ignore_item, G_TYPE_OBJECT)

static void
hc_ignore_item_finalize (GObject *obj)
{
	HcIgnoreItem *item = HC_IGNORE_ITEM (obj);
	g_free (item->mask);
	G_OBJECT_CLASS (hc_ignore_item_parent_class)->finalize (obj);
}

static void
hc_ignore_item_class_init (HcIgnoreItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_ignore_item_finalize;
}

static void
hc_ignore_item_init (HcIgnoreItem *item)
{
	item->mask = NULL;
	item->chan = FALSE;
	item->priv = FALSE;
	item->notice = FALSE;
	item->ctcp = FALSE;
	item->dcc = FALSE;
	item->invite = FALSE;
	item->unignore = FALSE;
}

static HcIgnoreItem *
hc_ignore_item_new (const char *mask, gboolean chan, gboolean priv,
                    gboolean notice, gboolean ctcp, gboolean dcc,
                    gboolean invite, gboolean unignore)
{
	HcIgnoreItem *item = g_object_new (HC_TYPE_IGNORE_ITEM, NULL);
	item->mask = g_strdup (mask ? mask : "");
	item->chan = chan;
	item->priv = priv;
	item->notice = notice;
	item->ctcp = ctcp;
	item->dcc = dcc;
	item->invite = invite;
	item->unignore = unignore;
	return item;
}

static GListStore *
get_store_gtk4 (void)
{
	return G_LIST_STORE (g_object_get_data (G_OBJECT (ignorewin), "store"));
}

static int
ignore_get_flags_gtk4 (HcIgnoreItem *item)
{
	int flags = 0;

	if (item->chan)
		flags |= IG_CHAN;
	if (item->priv)
		flags |= IG_PRIV;
	if (item->notice)
		flags |= IG_NOTI;
	if (item->ctcp)
		flags |= IG_CTCP;
	if (item->dcc)
		flags |= IG_DCC;
	if (item->invite)
		flags |= IG_INVI;
	if (item->unignore)
		flags |= IG_UNIG;
	return flags;
}

#endif /* HC_GTK4 */

static GtkWidget *num_ctcp;
static GtkWidget *num_priv;
static GtkWidget *num_chan;
static GtkWidget *num_noti;
static GtkWidget *num_invi;

#if !HC_GTK4
static GtkTreeModel *
get_store (void)
{
	return gtk_tree_view_get_model (g_object_get_data (G_OBJECT (ignorewin), "view"));
}

static int
ignore_get_flags (GtkTreeModel *model, GtkTreeIter *iter)
{
	gboolean chan, priv, noti, ctcp, dcc, invi, unig;
	int flags = 0;

	gtk_tree_model_get (model, iter, 1, &chan, 2, &priv, 3, &noti,
	                    4, &ctcp, 5, &dcc, 6, &invi, 7, &unig, -1);
	if (chan)
		flags |= IG_CHAN;
	if (priv)
		flags |= IG_PRIV;
	if (noti)
		flags |= IG_NOTI;
	if (ctcp)
		flags |= IG_CTCP;
	if (dcc)
		flags |= IG_DCC;
	if (invi)
		flags |= IG_INVI;
	if (unig)
		flags |= IG_UNIG;
	return flags;
}
#endif /* !HC_GTK4 */

#if HC_GTK4
/*
 * GTK4: Mask editing callback - called when GtkEditableLabel editing is done
 */
static void
ignore_mask_changed_cb (GtkEditableLabel *label, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item = GTK_LIST_ITEM (user_data);
	HcIgnoreItem *item = gtk_list_item_get_item (list_item);
	const char *new_text;
	int flags;

	if (!item)
		return;

	new_text = gtk_editable_get_text (GTK_EDITABLE (label));

	if (!strcmp (item->mask, new_text))	/* no change */
		return;

	if (ignore_exists (new_text))	/* duplicate, ignore */
	{
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
		/* Revert to old value */
		gtk_editable_set_text (GTK_EDITABLE (label), item->mask);
		return;
	}

	/* delete old mask, and add new one with original flags */
	ignore_del (item->mask, NULL);
	flags = ignore_get_flags_gtk4 (item);
	ignore_add (new_text, flags, TRUE);

	/* Update item */
	g_free (item->mask);
	item->mask = g_strdup (new_text);
}

/*
 * GTK4: Toggle callback - called when GtkCheckButton is toggled
 */
static void
ignore_toggle_cb (GtkCheckButton *button, gpointer user_data)
{
	GtkListItem *list_item = GTK_LIST_ITEM (user_data);
	HcIgnoreItem *item = gtk_list_item_get_item (list_item);
	gboolean active;
	int flags;
	int col_id;

	if (!item)
		return;

	active = gtk_check_button_get_active (button);
	col_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "col_id"));

	/* Update item field based on column */
	switch (col_id)
	{
		case 1: item->chan = active; break;
		case 2: item->priv = active; break;
		case 3: item->notice = active; break;
		case 4: item->ctcp = active; break;
		case 5: item->dcc = active; break;
		case 6: item->invite = active; break;
		case 7: item->unignore = active; break;
	}

	/* update ignore list */
	flags = ignore_get_flags_gtk4 (item);
	if (ignore_add (item->mask, flags, TRUE) != 2)
		g_warning ("ignore columnview is out of sync!\n");
}

#else /* GTK3 */

static void
mask_edited (GtkCellRendererText *render, gchar *path, gchar *new, gpointer dat)
{
	GtkListStore *store = GTK_LIST_STORE (get_store ());
	GtkTreeIter iter;
	char *old;
	int flags;

	gtkutil_treemodel_string_to_iter (GTK_TREE_MODEL (store), path, &iter);
	gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, 0, &old, -1);

	if (!strcmp (old, new))	/* no change */
		;
	else if (ignore_exists (new))	/* duplicate, ignore */
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
	else
	{
		/* delete old mask, and add new one with original flags */
		ignore_del (old, NULL);
		flags = ignore_get_flags (GTK_TREE_MODEL (store), &iter);
		ignore_add (new, flags, TRUE);

		/* update tree */
		gtk_list_store_set (store, &iter, MASK_COLUMN, new, -1);
	}
	g_free (old);

}

static void
option_toggled (GtkCellRendererToggle *render, gchar *path, gpointer data)
{
	GtkListStore *store = GTK_LIST_STORE (get_store ());
	GtkTreeIter iter;
	int col_id = GPOINTER_TO_INT (data);
	gboolean active;
	char *mask;
	int flags;

	gtkutil_treemodel_string_to_iter (GTK_TREE_MODEL (store), path, &iter);

	/* update model */
	gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, col_id, &active, -1);
	gtk_list_store_set (store, &iter, col_id, !active, -1);

	/* update ignore list */
	gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, 0, &mask, -1);
	flags = ignore_get_flags (GTK_TREE_MODEL (store), &iter);
	if (ignore_add (mask, flags, TRUE) != 2)
		g_warning ("ignore treeview is out of sync!\n");

	g_free (mask);
}

#endif /* HC_GTK4 */

#if HC_GTK4
/*
 * GTK4 Factory callbacks for GtkColumnView
 */

/* Mask column - editable label */
static void
ignore_setup_mask_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_editable_label_new ("");
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
ignore_bind_mask_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcIgnoreItem *ignore = gtk_list_item_get_item (item);

	gtk_editable_set_text (GTK_EDITABLE (label), ignore->mask ? ignore->mask : "");
	g_signal_connect (label, "notify::text", G_CALLBACK (ignore_mask_changed_cb), item);
}

static void
ignore_unbind_mask_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	g_signal_handlers_disconnect_by_func (label, ignore_mask_changed_cb, item);
}

/* Toggle column setup - creates a check button */
static void
ignore_setup_toggle_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *check = gtk_check_button_new ();
	gtk_widget_set_halign (check, GTK_ALIGN_CENTER);
	gtk_list_item_set_child (item, check);
}

/* Toggle bind - sets check state and connects signal */
static void
ignore_bind_toggle_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *check = gtk_list_item_get_child (item);
	HcIgnoreItem *ignore = gtk_list_item_get_item (item);
	int col_id = GPOINTER_TO_INT (user_data);
	gboolean active = FALSE;

	/* Get the appropriate field based on column */
	switch (col_id)
	{
		case 1: active = ignore->chan; break;
		case 2: active = ignore->priv; break;
		case 3: active = ignore->notice; break;
		case 4: active = ignore->ctcp; break;
		case 5: active = ignore->dcc; break;
		case 6: active = ignore->invite; break;
		case 7: active = ignore->unignore; break;
	}

	gtk_check_button_set_active (GTK_CHECK_BUTTON (check), active);

	/* Store column ID on the check button for the callback */
	g_object_set_data (G_OBJECT (check), "col_id", user_data);

	g_signal_connect (check, "toggled", G_CALLBACK (ignore_toggle_cb), item);
}

static void
ignore_unbind_toggle_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *check = gtk_list_item_get_child (item);
	g_signal_handlers_disconnect_by_func (check, ignore_toggle_cb, item);
}

static GtkWidget *
ignore_columnview_new (GtkWidget *box, GListStore **store_out)
{
	GtkWidget *scroll;
	GListStore *store;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Create list store for ignore items */
	store = g_list_store_new (HC_TYPE_IGNORE_ITEM);
	g_return_val_if_fail (store != NULL, NULL);

	/* Create column view with single selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (store), GTK_SELECTION_SINGLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Add Mask column (editable) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_mask_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_mask_cb), NULL);
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_mask_cb), NULL);
	col = gtk_column_view_column_new (_("Mask"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Channel column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (1));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("Channel"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Private column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (2));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("Private"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Notice column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (3));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("Notice"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add CTCP column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (4));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("CTCP"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add DCC column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (5));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("DCC"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Invite column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (6));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("Invite"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Unignore column (toggle) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (ignore_setup_toggle_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (ignore_bind_toggle_cb), GINT_TO_POINTER (7));
	g_signal_connect (factory, "unbind", G_CALLBACK (ignore_unbind_toggle_cb), NULL);
	col = gtk_column_view_column_new (_("Unignore"), factory);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (box, scroll, TRUE, TRUE, 0);

	*store_out = store;
	return view;
}

#else /* GTK3 */

static GtkWidget *
ignore_treeview_new (GtkWidget *box)
{
	GtkListStore *store;
	GtkWidget *view;
	GtkTreeViewColumn *col;
	GtkCellRenderer *render;
	int col_id;

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING,
	                            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	g_return_val_if_fail (store != NULL, NULL);

	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store),
	                             NULL,
	                             MASK_COLUMN, _("Mask"),
	                             CHAN_COLUMN, _("Channel"),
	                             PRIV_COLUMN, _("Private"),
	                             NOTICE_COLUMN, _("Notice"),
	                             CTCP_COLUMN, _("CTCP"),
	                             DCC_COLUMN, _("DCC"),
	                             INVITE_COLUMN, _("Invite"),
	                             UNIGNORE_COLUMN, _("Unignore"),
	                             -1);

	hc_tree_view_set_rules_hint (view, TRUE);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), 0), TRUE);

	/* attach to signals and customise columns */
	for (col_id=0; (col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_id));
	     col_id++)
	{
		GList *list = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (col));
		GList *tmp;

		for (tmp = list; tmp; tmp = tmp->next)
		{
			render = tmp->data;
			if (col_id > 0)	/* it's a toggle button column */
			{
				g_signal_connect (render, "toggled", G_CALLBACK (option_toggled),
				                  GINT_TO_POINTER (col_id));
			} else	/* mask column */
			{
				g_object_set (G_OBJECT (render), "editable", TRUE, NULL);
				g_signal_connect (render, "edited", G_CALLBACK (mask_edited), NULL);
				/* make this column sortable */
				gtk_tree_view_column_set_sort_column_id (col, col_id);
				gtk_tree_view_column_set_min_width (col, 272);
			}
			/* centre titles */
			gtk_tree_view_column_set_alignment (col, 0.5);
		}

		g_list_free (list);
	}

	gtk_widget_show (view);
	return view;
}

#endif /* HC_GTK4 */

static void
ignore_delete_entry_clicked (GtkWidget * wid, struct session *sess)
{
#if HC_GTK4
	GtkColumnView *view = g_object_get_data (G_OBJECT (ignorewin), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store_gtk4 ();
	HcIgnoreItem *item;
	guint position, n_items;

	item = hc_selection_model_get_selected_item (sel_model);
	if (item)
	{
		position = hc_selection_model_get_selected_position (sel_model);

		/* delete from ignore list */
		ignore_del (item->mask, NULL);
		g_object_unref (item);

		/* delete this row */
		g_list_store_remove (store, position);

		/* select next item if available */
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
		if (n_items > 0)
		{
			if (position >= n_items)
				position = n_items - 1;
			gtk_selection_model_select_item (sel_model, position, TRUE);
		}
	}
#else /* GTK3 */
	GtkTreeView *view = g_object_get_data (G_OBJECT (ignorewin), "view");
	GtkListStore *store = GTK_LIST_STORE (gtk_tree_view_get_model (view));
	GtkTreeIter iter;
	GtkTreePath *path;
	char *mask = NULL;

	if (gtkutil_treeview_get_selected (view, &iter, 0, &mask, -1))
	{
		/* delete this row, select next one */
		if (gtk_list_store_remove (store, &iter))
		{
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
			gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 1.0, 0.0);
			gtk_tree_view_set_cursor (view, path, NULL, FALSE);
			gtk_tree_path_free (path);
		}

		ignore_del (mask, NULL);
		g_free (mask);
	}
#endif /* HC_GTK4 */
}

static void
ignore_store_new (int cancel, char *mask, gpointer data)
{
	int flags = IG_CHAN | IG_PRIV | IG_NOTI | IG_CTCP | IG_DCC | IG_INVI;
#if HC_GTK4
	GtkColumnView *view = g_object_get_data (G_OBJECT (ignorewin), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store_gtk4 ();
	HcIgnoreItem *item;
	guint position;
#else /* GTK3 */
	GtkTreeView *view = g_object_get_data (G_OBJECT (ignorewin), "view");
	GtkListStore *store = GTK_LIST_STORE (get_store ());
	GtkTreeIter iter;
	GtkTreePath *path;
#endif

	if (cancel)
		return;
	/* check if it already exists */
	if (ignore_exists (mask))
	{
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
		return;
	}

	ignore_add (mask, flags, TRUE);

#if HC_GTK4
	/* ignore everything by default (except unignore) */
	item = hc_ignore_item_new (mask, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE);
	g_list_store_append (store, item);
	g_object_unref (item);

	/* make sure the new row is visible and selected */
	position = g_list_model_get_n_items (G_LIST_MODEL (store)) - 1;
	gtk_selection_model_select_item (sel_model, position, TRUE);
#else /* GTK3 */
	gtk_list_store_append (store, &iter);
	/* ignore everything by default */
	gtk_list_store_set (store, &iter, 0, mask, 1, TRUE, 2, TRUE, 3, TRUE,
	                    4, TRUE, 5, TRUE, 6, TRUE, 7, FALSE, -1);
	/* make sure the new row is visible and selected */
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
	gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 1.0, 0.0);
	gtk_tree_view_set_cursor (view, path, NULL, FALSE);
	gtk_tree_path_free (path);
#endif
}

static void
ignore_clear_cb (GtkDialog *dialog, gint response)
{
#if HC_GTK4
	GListStore *store = get_store_gtk4 ();
	guint i, n_items;
	HcIgnoreItem *item;

	hc_window_destroy (GTK_WIDGET (dialog));

	if (response == GTK_RESPONSE_OK)
	{
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));

		/* remove from ignore_list */
		for (i = 0; i < n_items; i++)
		{
			item = g_list_model_get_item (G_LIST_MODEL (store), i);
			if (item)
			{
				ignore_del (item->mask, NULL);
				g_object_unref (item);
			}
		}

		/* remove from GUI */
		g_list_store_remove_all (store);
	}
#else /* GTK3 */
	GtkListStore *store = GTK_LIST_STORE (get_store ());
	GtkTreeIter iter;
	char *mask;

	hc_window_destroy (GTK_WIDGET (dialog));

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter) && response == GTK_RESPONSE_OK)
	{
		/* remove from ignore_list */
		do
		{
			mask = NULL;
			gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, MASK_COLUMN, &mask, -1);
			ignore_del (mask, NULL);
			g_free (mask);
		}
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));

		/* remove from GUI */
		gtk_list_store_clear (store);
	}
#endif /* HC_GTK4 */
}

static void
ignore_clear_entry_clicked (GtkWidget * wid)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (NULL, 0,
								GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					_("Are you sure you want to remove all ignores?"));
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (ignore_clear_cb), NULL);
	hc_window_set_position (dialog, GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);
}

static void
ignore_new_entry_clicked (GtkWidget * wid, struct session *sess)
{
	fe_get_str (_("Enter mask to ignore:"), "nick!userid@host.com",
	            ignore_store_new, NULL);

}

static void
close_ignore_gui_callback (void)
{
	ignore_save ();
	ignorewin = 0;
}

static GtkWidget *
ignore_stats_entry (GtkWidget * box, char *label, int value)
{
	GtkWidget *wid;
	char buf[16];

	sprintf (buf, "%d", value);
	gtkutil_label_new (label, box);
	wid = gtkutil_entry_new (16, box, 0, 0);
	gtk_widget_set_size_request (wid, 30, -1);
	gtk_editable_set_editable (GTK_EDITABLE (wid), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (wid), FALSE);
	hc_entry_set_text (wid, buf);

	return wid;
}

void
ignore_gui_open ()
{
	GtkWidget *vbox, *box, *stat_box, *frame;
	GtkWidget *view;
	GSList *temp = ignore_list;
	char *mask;
	gboolean private, chan, notice, ctcp, dcc, invite, unignore;
	char buf[128];
#if HC_GTK4
	GListStore *store;
	HcIgnoreItem *item;
#else
	GtkListStore *store;
	GtkTreeIter iter;
#endif

	if (ignorewin)
	{
		mg_bring_tofront (ignorewin);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Ignore list - %s"), _(DISPLAY_NAME));
	ignorewin =
			  mg_create_generic_tab ("IgnoreList", buf, FALSE, TRUE,
											close_ignore_gui_callback,
											NULL, 700, 300, &vbox, 0);
	gtkutil_destroy_on_esc (ignorewin);

#if HC_GTK4
	view = ignore_columnview_new (vbox, &store);
	g_object_set_data (G_OBJECT (ignorewin), "view", view);
	g_object_set_data (G_OBJECT (ignorewin), "store", store);
#else
	view = ignore_treeview_new (vbox);
	g_object_set_data (G_OBJECT (ignorewin), "view", view);
#endif

	frame = gtk_frame_new (_("Ignore Stats:"));
	gtk_widget_show (frame);

	stat_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	hc_container_set_border_width (stat_box, 6);
	hc_frame_set_child (frame, stat_box);
	gtk_widget_show (stat_box);

	num_chan = ignore_stats_entry (stat_box, _("Channel:"), ignored_chan);
	num_priv = ignore_stats_entry (stat_box, _("Private:"), ignored_priv);
	num_noti = ignore_stats_entry (stat_box, _("Notice:"), ignored_noti);
	num_ctcp = ignore_stats_entry (stat_box, _("CTCP:"), ignored_ctcp);
	num_invi = ignore_stats_entry (stat_box, _("Invite:"), ignored_invi);

	hc_box_pack_start (vbox, frame, 0, 0, 5);

	box = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (box, GTK_BUTTONBOX_SPREAD);
	hc_box_pack_start (vbox, box, FALSE, FALSE, 2);
	hc_container_set_border_width (box, 5);
	gtk_widget_show (box);

	gtkutil_button (box, "document-new", 0, ignore_new_entry_clicked, 0,
						 _("Add..."));
	gtkutil_button (box, "edit-delete", 0, ignore_delete_entry_clicked,
						 0, _("Delete"));
	gtkutil_button (box, "edit-clear", 0, ignore_clear_entry_clicked,
						 0, _("Clear"));

#if HC_GTK4
	while (temp)
	{
		struct ignore *ign = temp->data;

		mask = ign->mask;
		chan = (ign->type & IG_CHAN) != 0;
		private = (ign->type & IG_PRIV) != 0;
		notice = (ign->type & IG_NOTI) != 0;
		ctcp = (ign->type & IG_CTCP) != 0;
		dcc = (ign->type & IG_DCC) != 0;
		invite = (ign->type & IG_INVI) != 0;
		unignore = (ign->type & IG_UNIG) != 0;

		item = hc_ignore_item_new (mask, chan, private, notice, ctcp, dcc, invite, unignore);
		g_list_store_append (store, item);
		g_object_unref (item);

		temp = temp->next;
	}
#else /* GTK3 */
	store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (view)));

	while (temp)
	{
		struct ignore *ignore = temp->data;

		mask = ignore->mask;
		chan = (ignore->type & IG_CHAN);
		private = (ignore->type & IG_PRIV);
		notice = (ignore->type & IG_NOTI);
		ctcp = (ignore->type & IG_CTCP);
		dcc = (ignore->type & IG_DCC);
		invite = (ignore->type & IG_INVI);
		unignore = (ignore->type & IG_UNIG);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
		                    MASK_COLUMN, mask,
		                    CHAN_COLUMN, chan,
		                    PRIV_COLUMN, private,
		                    NOTICE_COLUMN, notice,
		                    CTCP_COLUMN, ctcp,
		                    DCC_COLUMN, dcc,
		                    INVITE_COLUMN, invite,
		                    UNIGNORE_COLUMN, unignore,
		                    -1);

		temp = temp->next;
	}
#endif /* HC_GTK4 */
	gtk_widget_show (ignorewin);
}

void
fe_ignore_update (int level)
{
	/* some ignores have changed via /ignore, we should update
	   the gui now */
	/* level 1 = the list only. */
	/* level 2 = the numbers only. */
	/* for now, ignore level 1, since the ignore GUI isn't realtime,
	   only saved when you click OK */
	char buf[16];

	if (level == 2 && ignorewin)
	{
		sprintf (buf, "%d", ignored_ctcp);
		hc_entry_set_text (num_ctcp, buf);

		sprintf (buf, "%d", ignored_noti);
		hc_entry_set_text (num_noti, buf);

		sprintf (buf, "%d", ignored_chan);
		hc_entry_set_text (num_chan, buf);

		sprintf (buf, "%d", ignored_invi);
		hc_entry_set_text (num_invi, buf);

		sprintf (buf, "%d", ignored_priv);
		hc_entry_set_text (num_priv, buf);
	}
}
