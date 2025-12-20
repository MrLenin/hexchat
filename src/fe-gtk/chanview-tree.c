/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

/* file included in chanview.c */

/*
 * =============================================================================
 * GTK4: GtkListView with GtkTreeListModel for hierarchical display
 * =============================================================================
 *
 * In GTK4, we use GtkTreeListModel which wraps a flat GListModel and adds
 * hierarchy via a callback that returns child models. This is displayed
 * in a GtkListView with GtkTreeExpander widgets for expand/collapse UI.
 *
 * The backing data is still managed by chanview.c using GtkTreeStore,
 * so we create a "virtual" model that reads from the GtkTreeStore.
 */

typedef struct
{
	GtkListView *view;
	GtkWidget *scrollw;		/* scrolledWindow */
	GtkTreeListModel *tree_model;	/* hierarchical model wrapper */
	GListStore *root_store;		/* root level items */
} treeview;

/*
 * HcChanItem: GObject wrapper for chan* to use in GListStore
 */
#define HC_TYPE_CHAN_ITEM (hc_chan_item_get_type())
G_DECLARE_FINAL_TYPE (HcChanItem, hc_chan_item, HC, CHAN_ITEM, GObject)

struct _HcChanItem {
	GObject parent;
	chan *ch;			/* pointer to channel (not owned) */
	GListStore *children;		/* child channels (for servers) */
	gboolean is_server;		/* TRUE if this is a server (can have children) */
};

G_DEFINE_TYPE (HcChanItem, hc_chan_item, G_TYPE_OBJECT)

static void
hc_chan_item_finalize (GObject *obj)
{
	HcChanItem *item = HC_CHAN_ITEM (obj);
	g_clear_object (&item->children);
	/* Note: item->ch is not owned by us, don't free */
	G_OBJECT_CLASS (hc_chan_item_parent_class)->finalize (obj);
}

static void
hc_chan_item_class_init (HcChanItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_chan_item_finalize;
}

static void
hc_chan_item_init (HcChanItem *item)
{
	item->ch = NULL;
	item->children = NULL;
	item->is_server = FALSE;
}

static HcChanItem *
hc_chan_item_new (chan *ch, gboolean is_server)
{
	HcChanItem *item = g_object_new (HC_TYPE_CHAN_ITEM, NULL);
	item->ch = ch;
	item->is_server = is_server;
	if (is_server)
		item->children = g_list_store_new (HC_TYPE_CHAN_ITEM);
	return item;
}

/* Forward declarations for GTK4 functions */
static void cv_tree_rebuild_model (chanview *cv);
static HcChanItem *cv_tree_find_item (chanview *cv, chan *ch);
static HcChanItem *cv_tree_find_server_item (chanview *cv, void *family);
static chan *cv_tree_get_parent (chan *ch);

#include <gdk/gdk.h>

/*
 * GTK4: Row activated callback for GtkListView
 * Toggle expansion state of tree rows on double-click
 */
static void
cv_tree_activated_cb (GtkListView *view, guint position, gpointer data)
{
	chanview *cv = data;
	GtkTreeListRow *row;
	GtkSelectionModel *sel_model;

	sel_model = gtk_list_view_get_model (view);
	row = g_list_model_get_item (G_LIST_MODEL (sel_model), position);

	if (row && gtk_tree_list_row_is_expandable (row))
	{
		gboolean expanded = gtk_tree_list_row_get_expanded (row);
		gtk_tree_list_row_set_expanded (row, !expanded);
	}

	if (row)
		g_object_unref (row);
}

/*
 * GTK4: Selection changed callback for GtkListView
 */
static void
cv_tree_sel_cb (GtkSelectionModel *sel_model, guint position, guint n_items, chanview *cv)
{
	GtkBitset *selection;
	guint selected_pos;
	GtkTreeListRow *row;
	HcChanItem *item;

	selection = gtk_selection_model_get_selection (sel_model);
	if (gtk_bitset_is_empty (selection))
	{
		gtk_bitset_unref (selection);
		return;
	}

	selected_pos = gtk_bitset_get_nth (selection, 0);
	gtk_bitset_unref (selection);

	row = g_list_model_get_item (G_LIST_MODEL (sel_model), selected_pos);
	if (!row)
		return;

	item = gtk_tree_list_row_get_item (row);
	g_object_unref (row);

	if (item && item->ch)
	{
		cv->focused = item->ch;
		cv->cb_focus (cv, item->ch, item->ch->tag, item->ch->userdata);
	}

	if (item)
		g_object_unref (item);
}

/*
 * Tree view click handler (for context menus)
 * Uses GtkGestureClick with different signature
 */

/*
 * Helper to find position at coordinates in GtkListView
 * Returns the position or GTK_INVALID_LIST_POSITION if not found.
 *
 * This uses gtk_widget_pick to find the widget at coordinates, then
 * retrieves the position from the GtkTreeExpander's list row.
 */
static guint
cv_tree_get_position_at_coords (GtkListView *view, double x, double y)
{
	GtkWidget *child;
	GtkWidget *widget;
	GtkTreeListRow *row;
	GtkSelectionModel *sel_model;
	GListModel *model;
	guint n_items, i;

	/* Pick the widget at the given coordinates */
	child = gtk_widget_pick (GTK_WIDGET (view), x, y, GTK_PICK_DEFAULT);
	if (!child)
		return GTK_INVALID_LIST_POSITION;

	/* Walk up to find the GtkTreeExpander which has our data */
	widget = child;
	while (widget != NULL && widget != GTK_WIDGET (view))
	{
		if (GTK_IS_TREE_EXPANDER (widget))
		{
			row = gtk_tree_expander_get_list_row (GTK_TREE_EXPANDER (widget));
			if (row)
			{
				/* Find this row's position in the selection model */
				sel_model = gtk_list_view_get_model (view);
				model = G_LIST_MODEL (sel_model);
				n_items = g_list_model_get_n_items (model);

				for (i = 0; i < n_items; i++)
				{
					GtkTreeListRow *model_row = g_list_model_get_item (model, i);
					if (model_row == row)
					{
						g_object_unref (model_row);
						return i;
					}
					if (model_row)
						g_object_unref (model_row);
				}
			}
		}
		widget = gtk_widget_get_parent (widget);
	}

	return GTK_INVALID_LIST_POSITION;
}

/*
 * Left-click handler - explicitly select the clicked item.
 * GtkListView with GtkTreeExpander doesn't always auto-select properly.
 * We use "released" signal to set selection after GtkListView's internal
 * handling has completed.
 */
static void
cv_tree_left_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, chanview *cv)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkListView *view = GTK_LIST_VIEW (widget);
	GtkSelectionModel *sel_model;
	guint position;

	(void)gesture;
	(void)n_press;

	sel_model = gtk_list_view_get_model (view);

	/* Find which row was clicked */
	position = cv_tree_get_position_at_coords (view, x, y);

	if (position != GTK_INVALID_LIST_POSITION)
	{
		/* Explicitly select this item */
		gtk_selection_model_select_item (sel_model, position, TRUE);
	}

	/* Return focus to input box */
	if (current_sess && current_sess->gui)
		gtk_widget_grab_focus (current_sess->gui->input_box);
}

/*
 * Right-click handler - select clicked item, then show context menu
 */
static void
cv_tree_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, chanview *cv)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkListView *view = GTK_LIST_VIEW (widget);
	GtkSelectionModel *sel_model;
	guint clicked_pos;
	GtkTreeListRow *row;
	HcChanItem *item;

	sel_model = gtk_list_view_get_model (view);

	/* Find which row was clicked */
	clicked_pos = cv_tree_get_position_at_coords (view, x, y);
	if (clicked_pos == GTK_INVALID_LIST_POSITION)
		return;

	/* Select the clicked item (standard behavior: right-click selects) */
	gtk_selection_model_select_item (sel_model, clicked_pos, TRUE);

	row = g_list_model_get_item (G_LIST_MODEL (sel_model), clicked_pos);
	if (!row)
		return;

	item = gtk_tree_list_row_get_item (row);
	g_object_unref (row);

	if (item && item->ch)
	{
		cv->cb_contextmenu (cv, item->ch, item->ch->tag, item->ch->userdata, widget, x, y);
	}

	if (item)
		g_object_unref (item);
}

/*
 * Scroll event handler for tree view
 * Uses GtkEventControllerScroll with different signature
 */
static gboolean
cv_tree_scroll_event_cb (GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data)
{
	if (prefs.hex_gui_tab_scrollchans)
	{
		if (dy > 0)
			mg_switch_page (1, 1);
		else if (dy < 0)
			mg_switch_page (1, -1);

		return TRUE;
	}

	return FALSE;
}

/*
 * GTK4: Factory callbacks for GtkListView with GtkTreeExpander
 */

/* Setup callback - create the row widget structure */
static void
cv_tree_factory_setup_cb (GtkListItemFactory *factory, GtkListItem *item, chanview *cv)
{
	GtkWidget *expander, *content_box, *icon, *label;

	/* Tree expander for expand/collapse */
	expander = gtk_tree_expander_new ();
	gtk_widget_set_hexpand (expander, TRUE);

	/* Create horizontal box to hold icon + label inside expander */
	content_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

	/* Icon (only if icons are enabled) */
	if (cv->use_icons)
	{
		icon = gtk_picture_new ();
		gtk_picture_set_content_fit (GTK_PICTURE (icon), GTK_CONTENT_FIT_SCALE_DOWN);
		gtk_widget_set_size_request (icon, 16, -1);
		gtk_box_append (GTK_BOX (content_box), icon);
		g_object_set_data (G_OBJECT (item), "icon", icon);
	}

	/* Label for channel/server name */
	label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_box_append (GTK_BOX (content_box), label);

	/* Put content box (icon + label) inside expander */
	gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), content_box);

	gtk_list_item_set_child (item, expander);

	/* Store references for bind callback */
	g_object_set_data (G_OBJECT (item), "expander", expander);
	g_object_set_data (G_OBJECT (item), "label", label);
}

/* Bind callback - populate row with data */
static void
cv_tree_factory_bind_cb (GtkListItemFactory *factory, GtkListItem *item, chanview *cv)
{
	GtkWidget *expander, *icon, *label;
	GtkTreeListRow *row;
	HcChanItem *chan_item;
	char *name = NULL;
	GdkPixbuf *pixbuf = NULL;
	PangoAttrList *attr = NULL;
	GtkTreeIter iter;

	expander = g_object_get_data (G_OBJECT (item), "expander");
	icon = g_object_get_data (G_OBJECT (item), "icon");
	label = g_object_get_data (G_OBJECT (item), "label");

	/* Safety checks for stored widget references */
	if (!expander || !label)
		return;

	row = gtk_list_item_get_item (item);
	if (!row)
		return;

	/* Set the tree list row on the expander for expand/collapse functionality */
	gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);

	chan_item = gtk_tree_list_row_get_item (row);
	if (!chan_item || !chan_item->ch)
	{
		if (chan_item)
			g_object_unref (chan_item);
		return;
	}

	/* Get data from the GtkTreeStore using the channel's iter */
	if (gtk_tree_store_iter_is_valid (cv->store, &chan_item->ch->iter))
	{
		gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &chan_item->ch->iter,
		                    COL_NAME, &name,
		                    COL_ATTR, &attr,
		                    COL_PIXBUF, &pixbuf,
		                    -1);
	}

	/* Set label text and attributes */
	gtk_label_set_text (GTK_LABEL (label), name ? name : "");
	gtk_label_set_attributes (GTK_LABEL (label), attr);

	/* Set icon if we have one */
	if (icon && pixbuf)
	{
		GdkTexture *texture = hc_pixbuf_to_texture (pixbuf);
		gtk_picture_set_paintable (GTK_PICTURE (icon), GDK_PAINTABLE (texture));
		if (texture)
			g_object_unref (texture);
	}
	else if (icon)
	{
		gtk_picture_set_paintable (GTK_PICTURE (icon), NULL);
	}

	g_free (name);
	if (attr)
		pango_attr_list_unref (attr);
	g_object_unref (chan_item);
}

/* Unbind callback - cleanup */
static void
cv_tree_factory_unbind_cb (GtkListItemFactory *factory, GtkListItem *item, chanview *cv)
{
	GtkWidget *expander;

	if (!item)
		return;

	expander = g_object_get_data (G_OBJECT (item), "expander");
	if (expander)
		gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), NULL);
}

/*
 * GtkTreeListModel child model callback
 * Returns a GListModel for children of the given item, or NULL if no children.
 */
static GListModel *
cv_tree_create_child_model (gpointer item, gpointer user_data)
{
	HcChanItem *chan_item = HC_CHAN_ITEM (item);

	if (chan_item->is_server && chan_item->children)
	{
		/* Return a reference to the children store */
		return G_LIST_MODEL (g_object_ref (chan_item->children));
	}

	return NULL;
}

static void
cv_tree_init (chanview *cv)
{
	GtkWidget *view, *win;
	GtkListItemFactory *factory;
	GtkTreeListModel *tree_model;
	GtkSingleSelection *sel_model;
	treeview *tv = (treeview *)cv;

	win = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (win),
	                                GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (win, TRUE);
	gtk_widget_set_vexpand (win, TRUE);
	hc_box_pack_start (cv->box, win, TRUE, TRUE, 0);
	hc_widget_show (win);

	/* Create root store for server items */
	tv->root_store = g_list_store_new (HC_TYPE_CHAN_ITEM);

	/* Create tree list model that wraps our root store */
	tree_model = gtk_tree_list_model_new (
		G_LIST_MODEL (g_object_ref (tv->root_store)),
		FALSE,	/* passthrough - FALSE means we get GtkTreeListRow items */
		TRUE,	/* autoexpand */
		cv_tree_create_child_model,
		cv,
		NULL);	/* destroy notify */
	tv->tree_model = tree_model;

	/* Create selection model */
	sel_model = gtk_single_selection_new (G_LIST_MODEL (tree_model));
	gtk_single_selection_set_autoselect (sel_model, FALSE);

	/* Create factory for list items */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (cv_tree_factory_setup_cb), cv);
	g_signal_connect (factory, "bind", G_CALLBACK (cv_tree_factory_bind_cb), cv);
	g_signal_connect (factory, "unbind", G_CALLBACK (cv_tree_factory_unbind_cb), cv);

	/* Create list view */
	view = gtk_list_view_new (GTK_SELECTION_MODEL (sel_model), factory);
	gtk_widget_set_name (view, "hexchat-tree");
	gtk_widget_set_can_focus (view, FALSE);

	/* Connect signals */
	g_signal_connect (sel_model, "selection-changed",
	                  G_CALLBACK (cv_tree_sel_cb), cv);
	g_signal_connect (view, "activate",
	                  G_CALLBACK (cv_tree_activated_cb), cv);

	/* Event controllers */
	/* Left-click gesture for explicit selection (GtkListView doesn't auto-select reliably with GtkTreeExpander)
	 * Use "released" signal so we set selection after GtkListView's internal handling completes */
	{
		GtkGesture *gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1); /* Left-click only */
		g_signal_connect (gesture, "released", G_CALLBACK (cv_tree_left_click_cb), cv);
		gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));
	}
	/* Right-click gesture for context menu */
	{
		GtkGesture *gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 3); /* Right-click only */
		g_signal_connect (gesture, "pressed", G_CALLBACK (cv_tree_right_click_cb), cv);
		gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));
	}
	hc_add_scroll_controller (view, G_CALLBACK (cv_tree_scroll_event_cb), NULL);

	/* DND - drag source for layout swapping */
	mg_setup_chanview_drag_source (view);

	hc_scrolled_window_set_child (win, view);
	tv->view = GTK_LIST_VIEW (view);
	tv->scrollw = win;
	hc_widget_show (view);
}

static void
cv_tree_postinit (chanview *cv)
{
	/* In GTK4, tree is autoexpanded via GtkTreeListModel setting */
	/* Rebuild the model from the GtkTreeStore */
	cv_tree_rebuild_model (cv);
}

static void *
cv_tree_add (chanview *cv, chan *ch, char *name, GtkTreeIter *parent)
{
	treeview *tv = (treeview *)cv;
	HcChanItem *item;
	HcChanItem *parent_item;

	/* Create item for this channel */
	item = hc_chan_item_new (ch, parent == NULL); /* is_server if no parent */

	if (parent == NULL)
	{
		/* This is a server (root level) - add to root store */
		g_list_store_append (tv->root_store, item);
		g_object_unref (item);
	}
	else
	{
		/* This is a channel - find parent server and add to its children */
		parent_item = cv_tree_find_server_item (cv, ch->family);
		if (parent_item && parent_item->children)
		{
			g_list_store_append (parent_item->children, item);
		}
		g_object_unref (item);
	}

	return NULL;
}

static void
cv_tree_change_orientation (chanview *cv)
{
}

static void
cv_tree_focus (chan *ch)
{
	treeview *tv = (treeview *)ch->cv;
	GtkSelectionModel *sel_model;
	GListModel *model;
	guint n_items, i;
	GtkTreeListRow *row;
	HcChanItem *item;

	sel_model = gtk_list_view_get_model (tv->view);
	model = G_LIST_MODEL (sel_model);
	n_items = g_list_model_get_n_items (model);

	/* Find the row for this channel and select it */
	for (i = 0; i < n_items; i++)
	{
		row = g_list_model_get_item (model, i);
		if (!row)
			continue;

		item = gtk_tree_list_row_get_item (row);
		g_object_unref (row);

		if (item && item->ch == ch)
		{
			/* Expand parent if this is a child */
			GtkTreeListRow *parent_row = gtk_tree_list_row_get_parent (row);
			if (parent_row)
			{
				gtk_tree_list_row_set_expanded (parent_row, TRUE);
				g_object_unref (parent_row);
			}

			/* Select this row */
			gtk_selection_model_select_item (sel_model, i, TRUE);

			/* Scroll to make it visible - GtkListView handles this automatically
			 * when using gtk_list_view_scroll_to */
			gtk_list_view_scroll_to (tv->view, i, GTK_LIST_SCROLL_SELECT, NULL);

			g_object_unref (item);
			return;
		}

		if (item)
			g_object_unref (item);
	}
}

static void
cv_tree_move_focus (chanview *cv, gboolean relative, int num)
{
	chan *ch;

	if (relative)
	{
		num += cv_find_number_of_chan (cv, cv->focused);
		num %= cv->size;
		/* make it wrap around at both ends */
		if (num < 0)
			num = cv->size - 1;
	}

	ch = cv_find_chan_by_number (cv, num);
	if (ch)
		cv_tree_focus (ch);
}

static void
cv_tree_remove (chan *ch)
{
}

static void
move_row (chan *ch, int delta, GtkTreeIter *parent)
{
	GtkTreeStore *store = ch->cv->store;
	GtkTreeIter *src = &ch->iter;
	GtkTreeIter dest = ch->iter;
	GtkTreePath *dest_path;

	if (delta < 0) /* down */
	{
		if (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &dest))
			gtk_tree_store_swap (store, src, &dest);
		else	/* move to top */
			gtk_tree_store_move_after (store, src, NULL);

	} else
	{
		dest_path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &dest);
		if (gtk_tree_path_prev (dest_path))
		{
			gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &dest, dest_path);
			gtk_tree_store_swap (store, src, &dest);
		} else
		{	/* move to bottom */
			gtk_tree_store_move_before (store, src, NULL);
		}

		gtk_tree_path_free (dest_path);
	}
}

static void
cv_tree_move (chan *ch, int delta)
{
	GtkTreeIter parent;

	/* do nothing if this is a server row */
	if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (ch->cv->store), &parent, &ch->iter))
		move_row (ch, delta, &parent);
}

static void
cv_tree_move_family (chan *ch, int delta)
{
	move_row (ch, delta, NULL);
}

static void
cv_tree_cleanup (chanview *cv)
{
	treeview *tv = (treeview *)cv;

	if (cv->box)
		/* kill the scrolled window */
		hc_widget_destroy (tv->scrollw);

	/* Clean up GTK4 model */
	g_clear_object (&tv->root_store);
	tv->tree_model = NULL; /* owned by list view */
	tv->view = NULL;
}

/*
 * Helper to trigger rebind of an item in the chanview tree while preserving selection.
 * The remove/insert pattern clears GtkSingleSelection, so we save and restore it.
 */
static void
cv_tree_rebind_item (chanview *cv, GListStore *store, guint position, HcChanItem *item)
{
	treeview *tv = (treeview *)cv;
	GtkSelectionModel *sel_model;
	GtkBitset *selection;
	guint saved_selection = GTK_INVALID_LIST_POSITION;

	if (!tv || !tv->view || !store || !item)
		return;

	/* Save current selection */
	sel_model = gtk_list_view_get_model (tv->view);
	if (sel_model)
	{
		selection = gtk_selection_model_get_selection (sel_model);
		if (!gtk_bitset_is_empty (selection))
			saved_selection = gtk_bitset_get_nth (selection, 0);
		gtk_bitset_unref (selection);
	}

	/* Perform the rebind via remove/insert */
	g_list_store_remove (store, position);
	g_list_store_insert (store, position, item);

	/* Restore selection if it was valid */
	if (sel_model && saved_selection != GTK_INVALID_LIST_POSITION)
	{
		gtk_selection_model_select_item (sel_model, saved_selection, TRUE);
	}
}

/*
 * Helper to find an HcChanItem in the tree and trigger rebind.
 * Searches both root store and children stores.
 */
static void
cv_tree_rebind_chan (chan *ch)
{
	chanview *cv = ch->cv;
	treeview *tv = (treeview *)cv;
	guint n_items, i;
	HcChanItem *item;
	GListStore *store;
	chan *parent_ch = cv_tree_get_parent (ch);

	if (parent_ch)
	{
		/* It's a channel - find in parent's children store */
		HcChanItem *parent_item = cv_tree_find_server_item (cv, ch->family);
		if (parent_item && parent_item->children)
		{
			store = parent_item->children;
			n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
			for (i = 0; i < n_items; i++)
			{
				item = g_list_model_get_item (G_LIST_MODEL (store), i);
				if (item && item->ch == ch)
				{
					cv_tree_rebind_item (cv, store, i, item);
					g_object_unref (item);
					return;
				}
				if (item)
					g_object_unref (item);
			}
		}
	}
	else
	{
		/* It's a server - find in root store */
		store = tv->root_store;
		n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
		for (i = 0; i < n_items; i++)
		{
			item = g_list_model_get_item (G_LIST_MODEL (store), i);
			if (item && item->ch == ch)
			{
				cv_tree_rebind_item (cv, store, i, item);
				g_object_unref (item);
				return;
			}
			if (item)
				g_object_unref (item);
		}
	}
}

static void
cv_tree_set_color (chan *ch, PangoAttrList *list)
{
	/* In GTK4, trigger a rebind to pick up the new color from the GtkTreeStore.
	 * The rebind helper saves and restores selection to prevent it being cleared. */
	cv_tree_rebind_chan (ch);
}

static void
cv_tree_rename (chan *ch, char *name)
{
	/* In GTK4, trigger a rebind to pick up the new name from the GtkTreeStore.
	 * The rebind helper saves and restores selection to prevent it being cleared. */
	cv_tree_rebind_chan (ch);
}

static chan *
cv_tree_get_parent (chan *ch)
{
	chan *parent_ch = NULL;
	GtkTreeIter parent;

	if (gtk_tree_model_iter_parent (GTK_TREE_MODEL (ch->cv->store), &parent, &ch->iter))
	{
		gtk_tree_model_get (GTK_TREE_MODEL (ch->cv->store), &parent, COL_CHAN, &parent_ch, -1);
	}

	return parent_ch;
}

static gboolean
cv_tree_is_collapsed (chan *ch)
{
	chan *parent_ch = cv_tree_get_parent (ch);
	treeview *tv;
	GtkSelectionModel *sel_model;
	GListModel *model;
	guint n_items, i;
	GtkTreeListRow *row;
	HcChanItem *item;
	gboolean collapsed = FALSE;

	if (parent_ch == NULL)
		return FALSE;

	tv = (treeview *)parent_ch->cv;
	sel_model = gtk_list_view_get_model (tv->view);
	model = G_LIST_MODEL (sel_model);
	n_items = g_list_model_get_n_items (model);

	/* Find the row for the parent and check if it's expanded */
	for (i = 0; i < n_items; i++)
	{
		row = g_list_model_get_item (model, i);
		if (!row)
			continue;

		item = gtk_tree_list_row_get_item (row);

		if (item && item->ch == parent_ch)
		{
			collapsed = !gtk_tree_list_row_get_expanded (row);
			g_object_unref (item);
			g_object_unref (row);
			return collapsed;
		}

		if (item)
			g_object_unref (item);
		g_object_unref (row);
	}

	return FALSE;
}

/*
 * GTK4: Helper functions for managing the GListStore-based model
 */

/*
 * Find a server item in the root store by family pointer
 */
static HcChanItem *
cv_tree_find_server_item (chanview *cv, void *family)
{
	treeview *tv = (treeview *)cv;
	guint n_items, i;
	HcChanItem *item;

	if (!tv->root_store)
		return NULL;

	n_items = g_list_model_get_n_items (G_LIST_MODEL (tv->root_store));

	for (i = 0; i < n_items; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (tv->root_store), i);
		if (item && item->ch && item->ch->family == family)
		{
			/* Return without unref - caller must unref */
			return item;
		}
		if (item)
			g_object_unref (item);
	}

	return NULL;
}

/*
 * Find an item in the model by chan pointer
 */
static HcChanItem *
cv_tree_find_item (chanview *cv, chan *ch)
{
	treeview *tv = (treeview *)cv;
	guint n_root, n_children, i, j;
	HcChanItem *server_item, *child_item;

	if (!tv->root_store)
		return NULL;

	n_root = g_list_model_get_n_items (G_LIST_MODEL (tv->root_store));

	for (i = 0; i < n_root; i++)
	{
		server_item = g_list_model_get_item (G_LIST_MODEL (tv->root_store), i);
		if (!server_item)
			continue;

		/* Check if this is the item we're looking for */
		if (server_item->ch == ch)
		{
			return server_item; /* caller must unref */
		}

		/* Search in children */
		if (server_item->children)
		{
			n_children = g_list_model_get_n_items (G_LIST_MODEL (server_item->children));
			for (j = 0; j < n_children; j++)
			{
				child_item = g_list_model_get_item (G_LIST_MODEL (server_item->children), j);
				if (child_item && child_item->ch == ch)
				{
					g_object_unref (server_item);
					return child_item; /* caller must unref */
				}
				if (child_item)
					g_object_unref (child_item);
			}
		}

		g_object_unref (server_item);
	}

	return NULL;
}

/*
 * Rebuild the GListStore model from the GtkTreeStore
 * This is called when the tree view is first created or when
 * the model needs to be refreshed.
 */
static void
cv_tree_rebuild_model (chanview *cv)
{
	treeview *tv = (treeview *)cv;
	GtkTreeIter iter, child_iter;
	chan *ch;
	HcChanItem *server_item;

	if (!tv->root_store)
		return;

	/* Clear existing items */
	g_list_store_remove_all (tv->root_store);

	/* Iterate through the GtkTreeStore and populate our GListStore */
	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cv->store), &iter))
		return;

	do
	{
		/* Get the channel for this row */
		gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &iter, COL_CHAN, &ch, -1);
		if (!ch)
			continue;

		/* Create server item */
		server_item = hc_chan_item_new (ch, TRUE);
		g_list_store_append (tv->root_store, server_item);

		/* Add children if this server has any */
		if (gtk_tree_model_iter_children (GTK_TREE_MODEL (cv->store), &child_iter, &iter))
		{
			do
			{
				gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &child_iter, COL_CHAN, &ch, -1);
				if (ch && server_item->children)
				{
					HcChanItem *child_item = hc_chan_item_new (ch, FALSE);
					g_list_store_append (server_item->children, child_item);
					g_object_unref (child_item);
				}
			}
			while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &child_iter));
		}

		g_object_unref (server_item);
	}
	while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &iter));
}
