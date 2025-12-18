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

#ifndef HEXCHAT_CUSTOM_LIST_H
#define HEXCHAT_CUSTOM_LIST_H

#include "gtk-compat.h"

/*
 * =============================================================================
 * GTK4: HcChannelItem - GObject for channel list row data
 * =============================================================================
 * In GTK4, we use GListStore with GObject items instead of a custom GtkTreeModel.
 */

#define HC_TYPE_CHANNEL_ITEM (hc_channel_item_get_type())
G_DECLARE_FINAL_TYPE(HcChannelItem, hc_channel_item, HC, CHANNEL_ITEM, GObject)

struct _HcChannelItem
{
	GObject parent_instance;

	gchar *channel;
	gchar *topic;
	gchar *collation_key;
	guint users;
};

HcChannelItem *hc_channel_item_new (const gchar *channel, guint users, const gchar *topic);

/* Sorting column IDs */
enum
{
	SORT_ID_CHANNEL,
	SORT_ID_USERS,
	SORT_ID_TOPIC
};

/* Column indices for display */
enum
{
	CUSTOM_LIST_COL_NAME,
	CUSTOM_LIST_COL_USERS,
	CUSTOM_LIST_COL_TOPIC,
	CUSTOM_LIST_N_COLUMNS
};

#endif /* HEXCHAT_CUSTOM_LIST_H */
