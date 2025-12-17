/* X-Chat
 * Copyright (C) 1998-2006 Peter Zelezny.
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

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/util.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "maingui.h"
#include "menu.h"

#include "custom-list.h"

enum
{
	COL_CHANNEL,
	COL_USERS,
	COL_TOPIC,
	N_COLUMNS
};

#if !HC_GTK4
#ifndef CUSTOM_LIST
typedef struct	/* this is now in custom-list.h */
{
	char *topic;
	char *collation_key;
	guint32	pos;
	guint32 users;
	/* channel string lives beyond "users" */
#define GET_CHAN(row) (((char *)row)+sizeof(chanlistrow))
}
chanlistrow;
#endif
#endif

#if HC_GTK4
/*
 * =============================================================================
 * GTK4: GtkColumnView with GListStore + GtkFilterListModel + GtkSortListModel
 * =============================================================================
 */

/* Get the GListStore from the chain: store -> filter -> sort -> selection -> view */
static GListStore *
chanlist_get_store (server *serv)
{
	return G_LIST_STORE (serv->gui->chanlist_store);
}

/* Get the sort model for triggering re-sort */
static GtkSortListModel *
chanlist_get_sort_model (server *serv)
{
	GtkSelectionModel *sel_model;
	GListModel *model;

	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (serv->gui->chanlist_list));
	model = gtk_single_selection_get_model (GTK_SINGLE_SELECTION (sel_model));

	return GTK_SORT_LIST_MODEL (model);
}

/* Get filter model for changing the filter */
static GtkFilterListModel *
chanlist_get_filter_model (server *serv)
{
	GtkSortListModel *sort_model = chanlist_get_sort_model (serv);
	return GTK_FILTER_LIST_MODEL (gtk_sort_list_model_get_model (sort_model));
}

#else /* GTK3 */

#define GET_MODEL(xserv) (gtk_tree_view_get_model(GTK_TREE_VIEW(xserv->gui->chanlist_list)))

#endif /* HC_GTK4 */


static gboolean
chanlist_match (server *serv, const char *str)
{
	switch (serv->gui->chanlist_search_type)
	{
	case 1:
		return match (hc_entry_get_text (serv->gui->chanlist_wild), str);
	case 2:
		if (!serv->gui->have_regex)
			return 0;

		return g_regex_match (serv->gui->chanlist_match_regex, str, 0, NULL);
	default:	/* case 0: */
		return nocasestrstr (str, hc_entry_get_text (serv->gui->chanlist_wild)) ? 1 : 0;
	}
}

/**
 * Updates the caption to reflect the number of users and channels
 */
static void
chanlist_update_caption (server *serv)
{
	gchar tbuf[256];

	g_snprintf (tbuf, sizeof tbuf,
				 _("Displaying %d/%d users on %d/%d channels."),
				 serv->gui->chanlist_users_shown_count,
				 serv->gui->chanlist_users_found_count,
				 serv->gui->chanlist_channels_shown_count,
				 serv->gui->chanlist_channels_found_count);

	gtk_label_set_text (GTK_LABEL (serv->gui->chanlist_label), tbuf);
	serv->gui->chanlist_caption_is_stale = FALSE;
}

static void
chanlist_update_buttons (server *serv)
{
	if (serv->gui->chanlist_channels_shown_count)
	{
		gtk_widget_set_sensitive (serv->gui->chanlist_join, TRUE);
		gtk_widget_set_sensitive (serv->gui->chanlist_savelist, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive (serv->gui->chanlist_join, FALSE);
		gtk_widget_set_sensitive (serv->gui->chanlist_savelist, FALSE);
	}
}

static void
chanlist_reset_counters (server *serv)
{
	serv->gui->chanlist_users_found_count = 0;
	serv->gui->chanlist_users_shown_count = 0;
	serv->gui->chanlist_channels_found_count = 0;
	serv->gui->chanlist_channels_shown_count = 0;

	chanlist_update_caption (serv);
	chanlist_update_buttons (serv);
}

/* free up our entire linked list and all the nodes */

static void
chanlist_data_free (server *serv)
{
	GSList *rows;
#if !HC_GTK4
	chanlistrow *data;
#endif

	if (serv->gui->chanlist_data_stored_rows)
	{
#if HC_GTK4
		/* In GTK4, stored rows are HcChannelItem objects */
		for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
			  rows = rows->next)
		{
			g_object_unref (rows->data);
		}
#else
		for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
			  rows = rows->next)
		{
			data = rows->data;
			g_free (data->topic);
			g_free (data->collation_key);
			g_free (data);
		}
#endif

		g_slist_free (serv->gui->chanlist_data_stored_rows);
		serv->gui->chanlist_data_stored_rows = NULL;
	}

#if HC_GTK4
	/* In GTK4, pending rows are HcChannelItem objects */
	for (rows = serv->gui->chanlist_pending_rows; rows != NULL;
		  rows = rows->next)
	{
		g_object_unref (rows->data);
	}
#endif
	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;
}

/* add any rows we received from the server in the last 0.25s to the GUI */

static void
chanlist_flush_pending (server *serv)
{
	GSList *list = serv->gui->chanlist_pending_rows;
#if HC_GTK4
	HcChannelItem *item;
	GListStore *store;
#else
	GtkTreeModel *model;
	chanlistrow *row;
#endif

	if (!list)
	{
		if (serv->gui->chanlist_caption_is_stale)
			chanlist_update_caption (serv);
		return;
	}

#if HC_GTK4
	store = chanlist_get_store (serv);
	while (list)
	{
		item = list->data;
		g_list_store_append (store, item);
		g_object_unref (item); /* store holds reference */
		list = list->next;
	}
#else
	model = GET_MODEL (serv);
	while (list)
	{
		row = list->data;
		custom_list_append (CUSTOM_LIST (model), row);
		list = list->next;
	}
#endif

	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;
	chanlist_update_caption (serv);
}

static gboolean
chanlist_timeout (server *serv)
{
	chanlist_flush_pending (serv);
	return TRUE;
}

#if HC_GTK4
/**
 * Places a data row into the gui GtkColumnView.
 * In GTK4, filtering is handled by GtkFilterListModel, so we add all rows.
 * The filter function will decide what to show.
 */
static void
chanlist_place_row_in_gui (server *serv, HcChannelItem *item, gboolean force)
{
	GListStore *store;

	/* First, update the 'found' counter values */
	serv->gui->chanlist_users_found_count += item->users;
	serv->gui->chanlist_channels_found_count++;

	if (serv->gui->chanlist_channels_shown_count == 1)
		/* join & save buttons become live */
		chanlist_update_buttons (serv);

	/* In GTK4, we always add the item to the store. Filtering is done by
	 * GtkFilterListModel. The shown counts are updated when the filter changes.
	 */
	if (force || serv->gui->chanlist_channels_shown_count < 20)
	{
		store = chanlist_get_store (serv);
		g_list_store_append (store, item);
		g_object_unref (item); /* store holds reference */
		chanlist_update_caption (serv);
	}
	else
	{
		/* add it to GUI at the next update interval */
		serv->gui->chanlist_pending_rows = g_slist_prepend (serv->gui->chanlist_pending_rows, item);
		/* Don't unref here - the pending list holds the reference */
	}

	/* Update the 'shown' counter values */
	serv->gui->chanlist_users_shown_count += item->users;
	serv->gui->chanlist_channels_shown_count++;
}

#else /* GTK3 */

/**
 * Places a data row into the gui GtkTreeView, if and only if the row matches
 * the user and regex/search requirements.
 */
static void
chanlist_place_row_in_gui (server *serv, chanlistrow *next_row, gboolean force)
{
	GtkTreeModel *model;

	/* First, update the 'found' counter values */
	serv->gui->chanlist_users_found_count += next_row->users;
	serv->gui->chanlist_channels_found_count++;

	if (serv->gui->chanlist_channels_shown_count == 1)
		/* join & save buttons become live */
		chanlist_update_buttons (serv);

	if (next_row->users < serv->gui->chanlist_minusers)
	{
		serv->gui->chanlist_caption_is_stale = TRUE;
		return;
	}

	if (next_row->users > serv->gui->chanlist_maxusers
		 && serv->gui->chanlist_maxusers > 0)
	{
		serv->gui->chanlist_caption_is_stale = TRUE;
		return;
	}

	if (hc_entry_get_text (serv->gui->chanlist_wild)[0])
	{
		/* Check what the user wants to match. If both buttons or _neither_
		 * button is checked, look for match in both by default.
		 */
		if (serv->gui->chanlist_match_wants_channel ==
			 serv->gui->chanlist_match_wants_topic)
		{
			if (!chanlist_match (serv, GET_CHAN (next_row))
				 && !chanlist_match (serv, next_row->topic))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}

		else if (serv->gui->chanlist_match_wants_channel)
		{
			if (!chanlist_match (serv, GET_CHAN (next_row)))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}

		else if (serv->gui->chanlist_match_wants_topic)
		{
			if (!chanlist_match (serv, next_row->topic))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}
	}

	if (force || serv->gui->chanlist_channels_shown_count < 20)
	{
		model = GET_MODEL (serv);
		/* makes it appear fast :) */
		custom_list_append (CUSTOM_LIST (model), next_row);
		chanlist_update_caption (serv);
	}
	else
		/* add it to GUI at the next update interval */
		serv->gui->chanlist_pending_rows = g_slist_prepend (serv->gui->chanlist_pending_rows, next_row);

	/* Update the 'shown' counter values */
	serv->gui->chanlist_users_shown_count += next_row->users;
	serv->gui->chanlist_channels_shown_count++;
}

#endif /* HC_GTK4 */

/* Performs the LIST download from the IRC server. */

static void
chanlist_do_refresh (server *serv)
{
	if (serv->gui->chanlist_flash_tag)
	{
		g_source_remove (serv->gui->chanlist_flash_tag);
		serv->gui->chanlist_flash_tag = 0;
	}

	if (!serv->connected)
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
		return;
	}

#if HC_GTK4
	g_list_store_remove_all (chanlist_get_store (serv));
#else
	custom_list_clear ((CustomList *)GET_MODEL (serv));
#endif
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, FALSE);

	chanlist_data_free (serv);
	chanlist_reset_counters (serv);

	/* can we request a list with minusers arg? */
	if (serv->use_listargs)
	{
		/* yes - it will download faster */
		serv->p_list_channels (serv, "", serv->gui->chanlist_minusers);
		/* don't allow the spin button below this value from now on */
		serv->gui->chanlist_minusers_downloaded = serv->gui->chanlist_minusers;
	}
	else
	{
		/* download all, filter minusers locally only */
		serv->p_list_channels (serv, "", 1);
		serv->gui->chanlist_minusers_downloaded = 1;
	}

/*	gtk_spin_button_set_range ((GtkSpinButton *)serv->gui->chanlist_min_spin,
										serv->gui->chanlist_minusers_downloaded, 999999);*/
}

static void
chanlist_refresh (GtkWidget * wid, server *serv)
{
	chanlist_do_refresh (serv);
}

#if HC_GTK4
/**
 * Fills the gui GtkColumnView with stored items from the GSList.
 * In GTK4, the filter handles visibility based on search criteria.
 */
static void
chanlist_build_gui_list (server *serv)
{
	GSList *rows;
	HcChannelItem *item;
	GtkFilterListModel *filter_model;

	/* first check if the list is present */
	if (serv->gui->chanlist_data_stored_rows == NULL)
	{
		/* start a download */
		chanlist_do_refresh (serv);
		return;
	}

	g_list_store_remove_all (chanlist_get_store (serv));

	/* discard pending rows */
	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;

	/* Reset the counters */
	chanlist_reset_counters (serv);

	/* Refill the list */
	for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
		  rows = rows->next)
	{
		item = rows->data;
		chanlist_place_row_in_gui (serv, g_object_ref (item), TRUE);
	}

	/* Trigger filter re-evaluation */
	filter_model = chanlist_get_filter_model (serv);
	gtk_filter_changed (gtk_filter_list_model_get_filter (filter_model),
	                    GTK_FILTER_CHANGE_DIFFERENT);
}

#else /* GTK3 */

/**
 * Fills the gui GtkTreeView with stored items from the GSList.
 */
static void
chanlist_build_gui_list (server *serv)
{
	GSList *rows;

	/* first check if the list is present */
	if (serv->gui->chanlist_data_stored_rows == NULL)
	{
		/* start a download */
		chanlist_do_refresh (serv);
		return;
	}

	custom_list_clear ((CustomList *)GET_MODEL (serv));

	/* discard pending rows FIXME: free the structs? */
	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;

	/* Reset the counters */
	chanlist_reset_counters (serv);

	/* Refill the list */
	for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
		  rows = rows->next)
	{
		chanlist_place_row_in_gui (serv, rows->data, TRUE);
	}

	custom_list_resort ((CustomList *)GET_MODEL (serv));
}

#endif /* HC_GTK4 */

#if HC_GTK4
/**
 * Accepts incoming channel data from inbound.c, creates an HcChannelItem,
 * adds it to our linked list and calls chanlist_place_row_in_gui.
 */
void
fe_add_chan_list (server *serv, char *chan, char *users, char *topic)
{
	HcChannelItem *item;
	char *stripped_topic;

	stripped_topic = strip_color (topic, -1, STRIP_ALL);
	item = hc_channel_item_new (chan, atoi (users), stripped_topic);
	g_free (stripped_topic);

	/* add this row to the data */
	serv->gui->chanlist_data_stored_rows =
		g_slist_prepend (serv->gui->chanlist_data_stored_rows, g_object_ref (item));

	/* _possibly_ add the row to the gui */
	chanlist_place_row_in_gui (serv, item, FALSE);
}

#else /* GTK3 */

/**
 * Accepts incoming channel data from inbound.c, allocates new space for a
 * chanlistrow, adds it to our linked list and calls chanlist_place_row_in_gui.
 */
void
fe_add_chan_list (server *serv, char *chan, char *users, char *topic)
{
	chanlistrow *next_row;
	int len = strlen (chan) + 1;

	/* we allocate the struct and channel string in one go */
	next_row = g_malloc (sizeof (chanlistrow) + len);
	memcpy (((char *)next_row) + sizeof (chanlistrow), chan, len);
	next_row->topic = strip_color (topic, -1, STRIP_ALL);
	next_row->collation_key = g_utf8_collate_key (chan, len-1);
	if (!(next_row->collation_key))
		next_row->collation_key = g_strdup (chan);
	next_row->users = atoi (users);

	/* add this row to the data */
	serv->gui->chanlist_data_stored_rows =
		g_slist_prepend (serv->gui->chanlist_data_stored_rows, next_row);

	/* _possibly_ add the row to the gui */
	chanlist_place_row_in_gui (serv, next_row, FALSE);
}

#endif /* HC_GTK4 */

void
fe_chan_list_end (server *serv)
{
	/* download complete */
	chanlist_flush_pending (serv);
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, TRUE);
#if HC_GTK4
	/* Sorting is handled by GtkSortListModel automatically */
#else
	custom_list_resort ((CustomList *)GET_MODEL (serv));
#endif
}

#if HC_GTK4
static void
chanlist_search_pressed (GtkButton * button, server *serv)
{
	GtkFilterListModel *filter_model;

	/* Trigger filter re-evaluation */
	filter_model = chanlist_get_filter_model (serv);
	gtk_filter_changed (gtk_filter_list_model_get_filter (filter_model),
	                    GTK_FILTER_CHANGE_DIFFERENT);
}
#else
static void
chanlist_search_pressed (GtkButton * button, server *serv)
{
	chanlist_build_gui_list (serv);
}
#endif

static void
chanlist_find_cb (GtkWidget * wid, server *serv)
{
	const char *pattern = hc_entry_get_text (wid);

	/* recompile the regular expression. */
	if (serv->gui->have_regex)
	{
		serv->gui->have_regex = 0;
		g_regex_unref (serv->gui->chanlist_match_regex);
	}

	serv->gui->chanlist_match_regex = g_regex_new (pattern, G_REGEX_CASELESS | G_REGEX_EXTENDED,
												G_REGEX_MATCH_NOTBOL, NULL);

	if (serv->gui->chanlist_match_regex)
		serv->gui->have_regex = 1;
}

static void
chanlist_match_channel_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_channel = hc_check_button_get_active (wid);
}

static void
chanlist_match_topic_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_topic = hc_check_button_get_active (wid);
}

#if HC_GTK4
static char *
chanlist_get_selected (server *serv, gboolean get_topic)
{
	GtkSelectionModel *sel_model;
	HcChannelItem *item;
	char *result = NULL;

	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (serv->gui->chanlist_list));
	item = hc_selection_model_get_selected_item (sel_model);

	if (item)
	{
		result = g_strdup (get_topic ? item->topic : item->channel);
		g_object_unref (item);
	}

	return result;
}
#else /* GTK3 */
static char *
chanlist_get_selected (server *serv, gboolean get_topic)
{
	char *chan;
	GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (serv->gui->chanlist_list));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected (sel, &model, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter, get_topic ? COL_TOPIC : COL_CHANNEL, &chan, -1);
	return chan;
}
#endif /* HC_GTK4 */

static void
chanlist_join (GtkWidget * wid, server *serv)
{
	char tbuf[CHANLEN + 6];
	char *chan = chanlist_get_selected (serv, FALSE);
	if (chan)
	{
		if (serv->connected && (strcmp (chan, "*") != 0))
		{
			g_snprintf (tbuf, sizeof (tbuf), "join %s", chan);
			handle_command (serv->server_session, tbuf, FALSE);
		} else
			gdk_beep ();
		g_free (chan);
	}
}

static void
chanlist_filereq_done (server *serv, char *file)
{
	time_t t = time (0);
	int fh;
	char buf[1024];
#if HC_GTK4
	GListStore *store;
	guint i, n_items;
	HcChannelItem *item;
#else
	int users;
	char *chan, *topic;
	GtkTreeModel *model = GET_MODEL (serv);
	GtkTreeIter iter;
#endif

	if (!file)
		return;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600,
								 XOF_DOMODE | XOF_FULLPATH);
	if (fh == -1)
		return;

	g_snprintf (buf, sizeof buf, "HexChat Channel List: %s - %s\n",
				 serv->servername, ctime (&t));
	write (fh, buf, strlen (buf));

#if HC_GTK4
	store = chanlist_get_store (serv);
	n_items = g_list_model_get_n_items (G_LIST_MODEL (store));

	for (i = 0; i < n_items; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		g_snprintf (buf, sizeof buf, "%-16s %-5u%s\n",
		            item->channel, item->users, item->topic);
		write (fh, buf, strlen (buf));
		g_object_unref (item);
	}
#else
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter,
									  COL_CHANNEL, &chan,
									  COL_USERS, &users,
									  COL_TOPIC, &topic, -1);
			g_snprintf (buf, sizeof buf, "%-16s %-5d%s\n", chan, users, topic);
			g_free (chan);
			g_free (topic);
			write (fh, buf, strlen (buf));
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
#endif

	close (fh);
}

static void
chanlist_save (GtkWidget * wid, server *serv)
{
#if HC_GTK4
	GListStore *store = chanlist_get_store (serv);

	if (g_list_model_get_n_items (G_LIST_MODEL (store)) > 0)
		gtkutil_file_req (NULL, _("Select an output filename"), chanlist_filereq_done,
								serv, NULL, NULL, FRF_WRITE);
#else
	GtkTreeIter iter;
	GtkTreeModel *model = GET_MODEL (serv);

	if (gtk_tree_model_get_iter_first (model, &iter))
		gtkutil_file_req (NULL, _("Select an output filename"), chanlist_filereq_done,
								serv, NULL, NULL, FRF_WRITE);
#endif
}

static gboolean
chanlist_flash (server *serv)
{
	if (gtk_widget_get_state (serv->gui->chanlist_refresh) != GTK_STATE_ACTIVE)
		gtk_widget_set_state (serv->gui->chanlist_refresh, GTK_STATE_ACTIVE);
	else
		gtk_widget_set_state (serv->gui->chanlist_refresh, GTK_STATE_PRELIGHT);

	return TRUE;
}

static void
chanlist_minusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_minusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_minusers = serv->gui->chanlist_minusers;
	save_config();

	if (serv->gui->chanlist_minusers < serv->gui->chanlist_minusers_downloaded)
	{
		if (serv->gui->chanlist_flash_tag == 0)
			serv->gui->chanlist_flash_tag = g_timeout_add (500, (GSourceFunc)chanlist_flash, serv);
	}
	else
	{
		if (serv->gui->chanlist_flash_tag)
		{
			g_source_remove (serv->gui->chanlist_flash_tag);
			serv->gui->chanlist_flash_tag = 0;
		}
	}
}

static void
chanlist_maxusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_maxusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_maxusers = serv->gui->chanlist_maxusers;
	save_config();
}

static void
chanlist_dclick_cb (GtkTreeView *view, GtkTreePath *path,
						  GtkTreeViewColumn *column, gpointer data)
{
	chanlist_join (0, (server *) data);	/* double clicked a row */
}

static void
chanlist_menu_destroy (GtkWidget *menu, gpointer userdata)
{
	hc_widget_destroy (menu);
	g_object_unref (menu);
}

static void
chanlist_copychannel (GtkWidget *item, server *serv)
{
	char *chan = chanlist_get_selected (serv, FALSE);
	if (chan)
	{
#if HC_GTK4
		gtkutil_copy_to_clipboard (item, FALSE, chan);
#else
		gtkutil_copy_to_clipboard (item, NULL, chan);
#endif
		g_free (chan);
	}
}

static void
chanlist_copytopic (GtkWidget *item, server *serv)
{
	char *topic = chanlist_get_selected (serv, TRUE);
	if (topic)
	{
#if HC_GTK4
		gtkutil_copy_to_clipboard (item, FALSE, topic);
#else
		gtkutil_copy_to_clipboard (item, NULL, topic);
#endif
		g_free (topic);
	}
}

/*
 * Right-click context menu handler for channel list
 * GTK3: Uses GdkEventButton from "button-press-event" signal
 * GTK4: Uses GtkGestureClick and GtkPopoverMenu
 */
#if HC_GTK4

/* Store server pointer and channel for GTK4 menu actions */
static server *chanlist_menu_serv = NULL;
static gchar *chanlist_menu_chan = NULL;

/*
 * Helper to find position at coordinates in GtkColumnView
 * Returns the position or GTK_INVALID_LIST_POSITION if not found.
 * Uses gtk_widget_pick to find the widget at coordinates, then
 * retrieves the HcChannelItem stored on the label widget.
 */
static guint
chanlist_get_position_at_coords (GtkColumnView *view, double x, double y)
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

	/* Walk up to find a widget with our channel item data */
	widget = child;
	while (widget != NULL && widget != GTK_WIDGET (view))
	{
		HcChannelItem *item = g_object_get_data (G_OBJECT (widget), "hc-channel-item");
		if (item)
		{
			/* Found the item - now find its position in the selection model */
			sel_model = gtk_column_view_get_model (view);
			model = G_LIST_MODEL (sel_model);
			n_items = g_list_model_get_n_items (model);

			for (guint i = 0; i < n_items; i++)
			{
				HcChannelItem *model_item = g_list_model_get_item (model, i);
				if (model_item == item)
				{
					g_object_unref (model_item);
					return i;
				}
				if (model_item)
					g_object_unref (model_item);
			}
		}
		widget = gtk_widget_get_parent (widget);
	}

	return GTK_INVALID_LIST_POSITION;
}

static void
chanlist_action_join (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (chanlist_menu_serv)
		chanlist_join (NULL, chanlist_menu_serv);
}

static void
chanlist_action_copy_channel (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GtkWidget *widget = GTK_WIDGET (user_data);
	(void)action; (void)parameter;
	if (chanlist_menu_serv)
		chanlist_copychannel (widget, chanlist_menu_serv);
}

static void
chanlist_action_copy_topic (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GtkWidget *widget = GTK_WIDGET (user_data);
	(void)action; (void)parameter;
	if (chanlist_menu_serv)
		chanlist_copytopic (widget, chanlist_menu_serv);
}

static void
chanlist_action_autojoin (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (chanlist_menu_serv && chanlist_menu_serv->network && chanlist_menu_chan)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		servlist_autojoinedit (chanlist_menu_serv->network, chanlist_menu_chan, new_state);
		g_variant_unref (state);
	}
}

static gboolean
chanlist_popover_cleanup_idle (gpointer user_data)
{
	GSimpleActionGroup *action_group = G_SIMPLE_ACTION_GROUP (user_data);

	/* Clean up action group */
	if (action_group)
		g_object_unref (action_group);

	/* Free the stored channel name */
	g_free (chanlist_menu_chan);
	chanlist_menu_chan = NULL;
	chanlist_menu_serv = NULL;

	return G_SOURCE_REMOVE;
}

static void
chanlist_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	(void)popover;

	/* Defer cleanup so action callbacks can run first */
	g_idle_add (chanlist_popover_cleanup_idle, user_data);
}

static void
chanlist_button_cb (GtkGestureClick *gesture, int n_press, double x, double y, server *serv)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	GtkColumnView *view = GTK_COLUMN_VIEW (widget);
	GtkSelectionModel *sel_model;
	GtkWidget *popover;
	GMenu *gmenu;
	GSimpleActionGroup *action_group;
	GSimpleAction *action;
	guint position;
	int button;

	(void)n_press;
	button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));

	if (button != 3)
		return;

	/* Find the position at click coordinates and select it */
	position = chanlist_get_position_at_coords (view, x, y);
	if (position == GTK_INVALID_LIST_POSITION)
		return;

	/* Select the clicked row */
	sel_model = gtk_column_view_get_model (view);
	gtk_selection_model_select_item (sel_model, position, TRUE);

	/* Store server and get channel for action callbacks */
	chanlist_menu_serv = serv;
	g_free (chanlist_menu_chan);
	chanlist_menu_chan = chanlist_get_selected (serv, FALSE);

	/* Don't show menu if nothing is selected (shouldn't happen now) */
	if (chanlist_menu_chan == NULL)
		return;

	/* Create action group for menu actions */
	action_group = g_simple_action_group_new ();

	action = g_simple_action_new ("join", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (chanlist_action_join), NULL);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new ("copy-channel", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (chanlist_action_copy_channel), widget);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new ("copy-topic", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (chanlist_action_copy_topic), widget);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
	g_object_unref (action);

	/* GTK4: Use GMenu and GtkPopoverMenu for context menus */
	gmenu = g_menu_new ();
	g_menu_append (gmenu, _("_Join Channel"), "chanlist.join");
	g_menu_append (gmenu, _("_Copy Channel Name"), "chanlist.copy-channel");
	g_menu_append (gmenu, _("Copy _Topic Text"), "chanlist.copy-topic");

	/* Add Autojoin toggle if we have a network */
	if (serv->network && chanlist_menu_chan)
	{
		gboolean is_autojoin = joinlist_is_in_list (serv, chanlist_menu_chan);
		action = g_simple_action_new_stateful ("autojoin", NULL,
			g_variant_new_boolean (is_autojoin));
		g_signal_connect (action, "activate", G_CALLBACK (chanlist_action_autojoin), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (gmenu, _("_Autojoin Channel"), "chanlist.autojoin");
	}

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "chanlist", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, widget);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
		&(GdkRectangle){ (int)x, (int)y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Clean up action group when popover is closed (deferred to allow actions to run) */
	g_signal_connect (popover, "closed", G_CALLBACK (chanlist_popover_closed_cb), action_group);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}
#else /* GTK3 */
static gboolean
chanlist_button_cb (GtkTreeView *tree, GdkEventButton *event, server *serv)
{
	GtkWidget *menu;
	GtkTreeSelection *sel;
	GtkTreePath *path;
	char *chan;

	if (event->button != 3)
		return FALSE;

	if (!gtk_tree_view_get_path_at_pos (tree, event->x, event->y, &path, 0, 0, 0))
		return FALSE;

	/* select what they right-clicked on */
	sel = gtk_tree_view_get_selection (tree);
	gtk_tree_selection_unselect_all (sel);
	gtk_tree_selection_select_path (sel, path);
	gtk_tree_path_free (path);

	menu = gtk_menu_new ();
	if (event->window)
		gtk_menu_set_screen (GTK_MENU (menu), gdk_window_get_screen (event->window));
	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (G_OBJECT (menu), "selection-done",
							G_CALLBACK (chanlist_menu_destroy), NULL);
	mg_create_icon_item (_("_Join Channel"), "go-jump", menu,
								chanlist_join, serv);
	mg_create_icon_item (_("_Copy Channel Name"), "edit-copy", menu,
								chanlist_copychannel, serv);
	mg_create_icon_item (_("Copy _Topic Text"), "edit-copy", menu,
								chanlist_copytopic, serv);

	chan = chanlist_get_selected (serv, FALSE);
	menu_addfavoritemenu (serv, menu, chan, FALSE);
	g_free (chan);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, event->time);

	return TRUE;
}
#endif

static void
chanlist_destroy_widget (GtkWidget *wid, server *serv)
{
#if HC_GTK4
	g_list_store_remove_all (chanlist_get_store (serv));
	g_clear_object (&serv->gui->chanlist_store);
#else
	custom_list_clear ((CustomList *)GET_MODEL (serv));
#endif
	chanlist_data_free (serv);

	if (serv->gui->chanlist_flash_tag)
	{
		g_source_remove (serv->gui->chanlist_flash_tag);
		serv->gui->chanlist_flash_tag = 0;
	}

	if (serv->gui->chanlist_tag)
	{
		g_source_remove (serv->gui->chanlist_tag);
		serv->gui->chanlist_tag = 0;
	}

	if (serv->gui->have_regex)
	{
		g_regex_unref (serv->gui->chanlist_match_regex);
		serv->gui->have_regex = 0;
	}
}

#if HC_GTK4
/* Idle callback to restore focus to parent window after dialog closes.
 * This is scheduled from the destroy callback to ensure the window
 * destruction is fully processed before we try to focus the parent. */
static gboolean
chanlist_restore_focus_cb (gpointer user_data)
{
	GtkWindow *parent = GTK_WINDOW (user_data);
	if (GTK_IS_WINDOW (parent) && gtk_widget_get_visible (GTK_WIDGET (parent)))
	{
		gtk_window_present (parent);
	}
	return G_SOURCE_REMOVE;
}
#endif

static void
chanlist_closegui (GtkWidget *wid, server *serv)
{
	if (is_server (serv))
	{
#if HC_GTK4
		/* GTK4: Schedule focus return to main window.
		 * On Windows, transient window destruction doesn't always
		 * properly return focus to the parent window.
		 * Use idle callback to ensure destruction completes first. */
		if (serv->front_session && serv->front_session->gui &&
		    serv->front_session->gui->window)
		{
			g_idle_add (chanlist_restore_focus_cb,
			            serv->front_session->gui->window);
		}
#endif
		serv->gui->chanlist_window = NULL;
	}
}

static void
chanlist_add_column (GtkWidget *tree, int textcol, int size, char *title, gboolean right_justified)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *col;

	renderer = gtk_cell_renderer_text_new ();
	if (right_justified)
		g_object_set (G_OBJECT (renderer), "xalign", (gfloat) 1.0, NULL);
	g_object_set (G_OBJECT (renderer), "ypad", (gint) 0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree), -1, title,
																renderer, "text", textcol, NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree), textcol);
	gtk_tree_view_column_set_sort_column_id (col, textcol);
	gtk_tree_view_column_set_resizable (col, TRUE);
	if (textcol == COL_CHANNEL)
	{
		gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width (col, size);
	}
	else if (textcol == COL_USERS)
	{
		gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		gtk_tree_view_column_set_resizable (col, FALSE);
	}
}

static void
chanlist_combo_cb (GtkWidget *combo, server *serv)
{
	serv->gui->chanlist_search_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
}

#if HC_GTK4

/*
 * =============================================================================
 * GTK4: Column factory callbacks for GtkColumnView
 * =============================================================================
 */

/* Channel column setup */
static void
chanlist_channel_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_list_item_set_child (item, label);
}

static void
chanlist_channel_bind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcChannelItem *channel_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), channel_item->channel);
	/* Store reference for position lookup during right-click */
	g_object_set_data (G_OBJECT (label), "hc-channel-item", channel_item);
}

/* Users column setup */
static void
chanlist_users_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 1.0); /* Right-align */
	gtk_list_item_set_child (item, label);
}

static void
chanlist_users_bind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcChannelItem *channel_item = gtk_list_item_get_item (item);
	char buf[32];
	g_snprintf (buf, sizeof buf, "%u", channel_item->users);
	gtk_label_set_text (GTK_LABEL (label), buf);
	/* Store reference for position lookup during right-click */
	g_object_set_data (G_OBJECT (label), "hc-channel-item", channel_item);
}

/* Topic column setup */
static void
chanlist_topic_setup_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_list_item_set_child (item, label);
}

static void
chanlist_topic_bind_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcChannelItem *channel_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), channel_item->topic ? channel_item->topic : "");
	/* Store reference for position lookup during right-click */
	g_object_set_data (G_OBJECT (label), "hc-channel-item", channel_item);
}

/*
 * =============================================================================
 * GTK4: Filter function for channel list
 * =============================================================================
 */
static gboolean
chanlist_filter_func (gpointer item, gpointer user_data)
{
	HcChannelItem *channel_item = HC_CHANNEL_ITEM (item);
	server *serv = user_data;

	/* User count filter */
	if (channel_item->users < serv->gui->chanlist_minusers)
		return FALSE;

	if (channel_item->users > serv->gui->chanlist_maxusers
		 && serv->gui->chanlist_maxusers > 0)
		return FALSE;

	/* Text/regex filter */
	if (hc_entry_get_text (serv->gui->chanlist_wild)[0])
	{
		if (serv->gui->chanlist_match_wants_channel ==
			 serv->gui->chanlist_match_wants_topic)
		{
			/* Both or neither checked - search both */
			if (!chanlist_match (serv, channel_item->channel)
				 && !chanlist_match (serv, channel_item->topic))
				return FALSE;
		}
		else if (serv->gui->chanlist_match_wants_channel)
		{
			if (!chanlist_match (serv, channel_item->channel))
				return FALSE;
		}
		else if (serv->gui->chanlist_match_wants_topic)
		{
			if (!chanlist_match (serv, channel_item->topic))
				return FALSE;
		}
	}

	return TRUE;
}

/*
 * =============================================================================
 * GTK4: Sorter functions for channel list columns
 * =============================================================================
 */

/* fast as possible compare func for sorting (same as GTK3 version) */
#define TOSML(c) (((c) >= 'A' && (c) <= 'Z') ? (c) - 'A' + 'a' : (c))

static inline int
fast_ascii_stricmp (const char *s1, const char *s2)
{
	int c1, c2;

	while (*s1 && *s2)
	{
		c1 = (int) (unsigned char) TOSML (*s1);
		c2 = (int) (unsigned char) TOSML (*s2);
		if (c1 != c2)
			return (c1 - c2);
		s1++;
		s2++;
	}

	return (((int) (unsigned char) *s1) - ((int) (unsigned char) *s2));
}

static int
chanlist_channel_sorter_func (gconstpointer a, gconstpointer b, gpointer user_data)
{
	HcChannelItem *item_a = HC_CHANNEL_ITEM ((gpointer) a);
	HcChannelItem *item_b = HC_CHANNEL_ITEM ((gpointer) b);

	return strcmp (item_a->collation_key, item_b->collation_key);
}

static int
chanlist_users_sorter_func (gconstpointer a, gconstpointer b, gpointer user_data)
{
	HcChannelItem *item_a = HC_CHANNEL_ITEM ((gpointer) a);
	HcChannelItem *item_b = HC_CHANNEL_ITEM ((gpointer) b);

	return (int) item_a->users - (int) item_b->users;
}

static int
chanlist_topic_sorter_func (gconstpointer a, gconstpointer b, gpointer user_data)
{
	HcChannelItem *item_a = HC_CHANNEL_ITEM ((gpointer) a);
	HcChannelItem *item_b = HC_CHANNEL_ITEM ((gpointer) b);

	return fast_ascii_stricmp (item_a->topic ? item_a->topic : "",
	                           item_b->topic ? item_b->topic : "");
}

/*
 * =============================================================================
 * GTK4: Create the channel list GtkColumnView
 * =============================================================================
 */
static GtkWidget *
chanlist_create_columnview (server *serv)
{
	GListStore *store;
	GtkCustomFilter *custom_filter;
	GtkFilterListModel *filter_model;
	GtkSortListModel *sort_model;
	GtkSingleSelection *sel_model;
	GtkWidget *view;
	GtkColumnViewColumn *column;
	GtkListItemFactory *factory;
	GtkSorter *sorter;

	/* Create the base store */
	store = g_list_store_new (HC_TYPE_CHANNEL_ITEM);
	serv->gui->chanlist_store = G_OBJECT (store);

	/* Wrap with filter model */
	custom_filter = gtk_custom_filter_new (chanlist_filter_func, serv, NULL);
	filter_model = gtk_filter_list_model_new (G_LIST_MODEL (store),
	                                          GTK_FILTER (custom_filter));

	/* Wrap with sort model - initially no sorter, columns will set it */
	sort_model = gtk_sort_list_model_new (G_LIST_MODEL (filter_model), NULL);

	/* Selection model */
	sel_model = gtk_single_selection_new (G_LIST_MODEL (sort_model));

	/* Create the column view */
	view = gtk_column_view_new (GTK_SELECTION_MODEL (sel_model));
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Enable sorting on the column view */
	gtk_sort_list_model_set_sorter (sort_model,
		gtk_column_view_get_sorter (GTK_COLUMN_VIEW (view)));

	/* Channel column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (chanlist_channel_setup_cb), serv);
	g_signal_connect (factory, "bind", G_CALLBACK (chanlist_channel_bind_cb), serv);
	column = gtk_column_view_column_new (_("Channel"), factory);
	gtk_column_view_column_set_resizable (column, TRUE);
	gtk_column_view_column_set_fixed_width (column, 150);
	sorter = GTK_SORTER (gtk_custom_sorter_new (chanlist_channel_sorter_func, NULL, NULL));
	gtk_column_view_column_set_sorter (column, sorter);
	g_object_unref (sorter);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);
	g_object_unref (column);

	/* Users column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (chanlist_users_setup_cb), serv);
	g_signal_connect (factory, "bind", G_CALLBACK (chanlist_users_bind_cb), serv);
	column = gtk_column_view_column_new (_("Users"), factory);
	gtk_column_view_column_set_resizable (column, FALSE);
	sorter = GTK_SORTER (gtk_custom_sorter_new (chanlist_users_sorter_func, NULL, NULL));
	gtk_column_view_column_set_sorter (column, sorter);
	g_object_unref (sorter);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);
	g_object_unref (column);

	/* Topic column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (chanlist_topic_setup_cb), serv);
	g_signal_connect (factory, "bind", G_CALLBACK (chanlist_topic_bind_cb), serv);
	column = gtk_column_view_column_new (_("Topic"), factory);
	gtk_column_view_column_set_resizable (column, TRUE);
	gtk_column_view_column_set_expand (column, TRUE);
	sorter = GTK_SORTER (gtk_custom_sorter_new (chanlist_topic_sorter_func, NULL, NULL));
	gtk_column_view_column_set_sorter (column, sorter);
	g_object_unref (sorter);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);
	g_object_unref (column);

	return view;
}

/* Double-click handler for GTK4 */
static void
chanlist_activate_cb (GtkColumnView *view, guint position, server *serv)
{
	chanlist_join (NULL, serv);
}

#endif /* HC_GTK4 */

void
chanlist_opengui (server *serv, int do_refresh)
{
	GtkWidget *vbox, *hbox, *table, *wid, *view;
	char tbuf[256];
#if HC_GTK4
	GtkWidget *scrolled;
#else
	GtkListStore *store;
#endif

	if (serv->gui->chanlist_window)
	{
		mg_bring_tofront (serv->gui->chanlist_window);
		return;
	}

	g_snprintf (tbuf, sizeof tbuf, _("Channel List (%s) - %s"),
				 server_get_network (serv, TRUE), _(DISPLAY_NAME));

	serv->gui->chanlist_pending_rows = NULL;
	serv->gui->chanlist_tag = 0;
	serv->gui->chanlist_flash_tag = 0;
	serv->gui->chanlist_data_stored_rows = NULL;

	if (!serv->gui->chanlist_minusers)
	{
		if (prefs.hex_gui_chanlist_minusers < 1 || prefs.hex_gui_chanlist_minusers > 999999)
		{
			prefs.hex_gui_chanlist_minusers = 5;
			save_config();
		}

		serv->gui->chanlist_minusers = prefs.hex_gui_chanlist_minusers;
	}

	if (!serv->gui->chanlist_maxusers)
	{
		if (prefs.hex_gui_chanlist_maxusers < 1 || prefs.hex_gui_chanlist_maxusers > 999999)
		{
			prefs.hex_gui_chanlist_maxusers = 9999;
			save_config();
		}

		serv->gui->chanlist_maxusers = prefs.hex_gui_chanlist_maxusers;
	}

	serv->gui->chanlist_window =
		mg_create_generic_tab ("ChanList", tbuf, FALSE, TRUE, chanlist_closegui,
								serv, 640, 480, &vbox, serv);
	gtkutil_destroy_on_esc (serv->gui->chanlist_window);

#if !HC_GTK4
	hc_container_set_border_width (vbox, 6);
#endif
	gtk_box_set_spacing (GTK_BOX (vbox), 12);

	/* make a label to store the user/channel info */
	wid = gtk_label_new (NULL);
	hc_box_pack_start (vbox, wid, FALSE, FALSE, 0);
	hc_widget_show (wid);
	serv->gui->chanlist_label = wid;

	/* ============================================================= */

#if HC_GTK4
	/* GTK4: Create GtkColumnView */
	view = chanlist_create_columnview (serv);
	serv->gui->chanlist_list = view;

	/* Put column view in scrolled window */
	scrolled = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
	                                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	hc_scrolled_window_set_child (scrolled, view);
	gtk_widget_set_vexpand (scrolled, TRUE);
	gtk_widget_set_hexpand (scrolled, TRUE);
	hc_box_pack_start (vbox, scrolled, TRUE, TRUE, 0);
	hc_widget_show (scrolled);

	/* Double-click handler */
	g_signal_connect (G_OBJECT (view), "activate",
	                  G_CALLBACK (chanlist_activate_cb), serv);

	/* Right-click context menu */
	hc_add_click_gesture (view, G_CALLBACK (chanlist_button_cb), NULL, serv);

#else /* GTK3 */

	store = (GtkListStore *) custom_list_new();
	view = gtkutil_treeview_new (vbox, GTK_TREE_MODEL (store), NULL, -1);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (gtk_widget_get_parent (view)),
													 GTK_SHADOW_IN);
	serv->gui->chanlist_list = view;

	g_signal_connect (G_OBJECT (view), "row_activated",
							G_CALLBACK (chanlist_dclick_cb), serv);
	g_signal_connect (G_OBJECT (view), "button-press-event",
							G_CALLBACK (chanlist_button_cb), serv);

	chanlist_add_column (view, COL_CHANNEL, 96, _("Channel"), FALSE);
	chanlist_add_column (view, COL_USERS,   50, _("Users"),   TRUE);
	chanlist_add_column (view, COL_TOPIC,   50, _("Topic"),   FALSE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);
	/* this is a speed up, but no horizontal scrollbar :( */
	/*gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view), TRUE);*/
#endif

	gtk_widget_show (view);

	/* ============================================================= */

	table = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (table), 12);
	gtk_grid_set_row_spacing (GTK_GRID (table), 3);
	hc_box_pack_start (vbox, table, FALSE, TRUE, 0);
	gtk_widget_show (table);

	wid = gtkutil_button (NULL, "edit-find", 0, chanlist_search_pressed, serv,
								 _("_Search"));
	serv->gui->chanlist_search = wid;
	gtk_grid_attach (GTK_GRID (table), wid, 3, 3, 1, 1);

	wid = gtkutil_button (NULL, "view-refresh", 0, chanlist_refresh, serv,
								 _("_Download List"));
	serv->gui->chanlist_refresh = wid;
	gtk_grid_attach (GTK_GRID (table), wid, 3, 2, 1, 1);

	wid = gtkutil_button (NULL, "document-save-as", 0, chanlist_save, serv,
								 _("Save _List..."));
	serv->gui->chanlist_savelist = wid;
	gtk_grid_attach (GTK_GRID (table), wid, 3, 1, 1, 1);

	wid = gtkutil_button (NULL, "go-jump", 0, chanlist_join, serv,
						 _("_Join Channel"));
	serv->gui->chanlist_join = wid;
	gtk_grid_attach (GTK_GRID (table), wid, 3, 0, 1, 1);

	/* ============================================================= */

	wid = gtk_label_new (_("Show only:"));
	gtk_widget_set_halign (wid, GTK_ALIGN_START);
	gtk_widget_set_valign (wid, GTK_ALIGN_CENTER);
	gtk_grid_attach (GTK_GRID (table), wid, 0, 3, 1, 1);
	gtk_widget_show (wid);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing (GTK_BOX (hbox), 9);
	gtk_grid_attach (GTK_GRID (table), hbox, 1, 3, 1, 1);
	gtk_widget_show (hbox);

	wid = gtk_label_new (_("channels with"));
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_minusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_minusers), serv);
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);
	serv->gui->chanlist_min_spin = wid;

	wid = gtk_label_new (_("to"));
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_maxusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_maxusers), serv);
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	wid = gtk_label_new (_("users."));
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Look in:"));
	gtk_widget_set_halign (wid, GTK_ALIGN_START);
	gtk_widget_set_valign (wid, GTK_ALIGN_CENTER);
	gtk_grid_attach (GTK_GRID (table), wid, 0, 2, 1, 1);
	gtk_widget_show (wid);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing (GTK_BOX (hbox), 12);
	gtk_grid_attach (GTK_GRID (table), hbox, 1, 2, 1, 1);
	gtk_widget_show (hbox);

	wid = gtk_check_button_new_with_label (_("Channel name"));
	hc_check_button_set_active (wid, TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK(chanlist_match_channel_button_toggled), serv);
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	wid = gtk_check_button_new_with_label (_("Topic"));
	hc_check_button_set_active (wid, TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK (chanlist_match_topic_button_toggled),
							  serv);
	hc_box_pack_start (hbox, wid, FALSE, FALSE, 0);
	gtk_widget_show (wid);

	serv->gui->chanlist_match_wants_channel = 1;
	serv->gui->chanlist_match_wants_topic = 1;

	/* ============================================================= */

	wid = gtk_label_new (_("Search type:"));
	gtk_widget_set_halign (wid, GTK_ALIGN_START);
	gtk_widget_set_valign (wid, GTK_ALIGN_CENTER);
	gtk_grid_attach (GTK_GRID (table), wid, 0, 1, 1, 1);
	gtk_widget_show (wid);

	wid = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Simple Search"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Pattern Match (Wildcards)"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Regular Expression"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (wid), serv->gui->chanlist_search_type);
	gtk_grid_attach (GTK_GRID (table), wid, 1, 1, 1, 1);
	g_signal_connect (G_OBJECT (wid), "changed",
							G_CALLBACK (chanlist_combo_cb), serv);
	gtk_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Find:"));
	gtk_widget_set_halign (wid, GTK_ALIGN_START);
	gtk_widget_set_valign (wid, GTK_ALIGN_CENTER);
	gtk_grid_attach (GTK_GRID (table), wid, 0, 0, 1, 1);
	gtk_widget_show (wid);

	wid = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY(wid), 255);
	g_signal_connect (G_OBJECT (wid), "changed",
							  G_CALLBACK (chanlist_find_cb), serv);
	g_signal_connect (G_OBJECT (wid), "activate",
							  G_CALLBACK (chanlist_search_pressed),
							  (gpointer) serv);
	gtk_widget_set_hexpand (wid, TRUE);
	gtk_grid_attach (GTK_GRID (table), wid, 1, 0, 1, 1);
	gtk_widget_show (wid);
	serv->gui->chanlist_wild = wid;

	chanlist_find_cb (wid, serv);

	/* ============================================================= */

	wid = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
	gtk_widget_set_vexpand (wid, TRUE);
	gtk_grid_attach (GTK_GRID (table), wid, 2, 0, 1, 5);
	gtk_widget_show (wid);

	g_signal_connect (G_OBJECT (serv->gui->chanlist_window), "destroy",
							G_CALLBACK (chanlist_destroy_widget), serv);

	/* reset the counters. */
	chanlist_reset_counters (serv);

	serv->gui->chanlist_tag = g_timeout_add (250, (GSourceFunc)chanlist_timeout, serv);

	if (do_refresh)
		chanlist_do_refresh (serv);

	chanlist_update_buttons (serv);
	gtk_widget_show (serv->gui->chanlist_window);
	gtk_widget_grab_focus (serv->gui->chanlist_refresh);
}
