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


static GtkWidget *ignorewin = 0;

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

static GtkWidget *num_ctcp;
static GtkWidget *num_priv;
static GtkWidget *num_chan;
static GtkWidget *num_noti;
static GtkWidget *num_invi;


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

static void
ignore_delete_entry_clicked (GtkWidget * wid, struct session *sess)
{
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
}

static void
ignore_store_new (int cancel, char *mask, gpointer data)
{
	int flags = IG_CHAN | IG_PRIV | IG_NOTI | IG_CTCP | IG_DCC | IG_INVI;
	GtkColumnView *view = g_object_get_data (G_OBJECT (ignorewin), "view");
	GtkSelectionModel *sel_model = gtk_column_view_get_model (view);
	GListStore *store = get_store_gtk4 ();
	HcIgnoreItem *item;
	guint position;

	if (cancel)
		return;
	/* check if it already exists */
	if (ignore_exists (mask))
	{
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
		return;
	}

	ignore_add (mask, flags, TRUE);

	/* ignore everything by default (except unignore) */
	item = hc_ignore_item_new (mask, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE);
	g_list_store_append (store, item);
	g_object_unref (item);

	/* make sure the new row is visible and selected */
	position = g_list_model_get_n_items (G_LIST_MODEL (store)) - 1;
	gtk_selection_model_select_item (sel_model, position, TRUE);
}

static void
ignore_clear_cb (GtkDialog *dialog, gint response)
{
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
	GListStore *store;
	HcIgnoreItem *item;

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

	view = ignore_columnview_new (vbox, &store);
	g_object_set_data (G_OBJECT (ignorewin), "view", view);
	g_object_set_data (G_OBJECT (ignorewin), "store", store);

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
