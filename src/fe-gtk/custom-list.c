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

#include <string.h>
#include <stdlib.h>
#include "custom-list.h"

/*
 * =============================================================================
 * GTK4: HcChannelItem GObject Implementation
 * =============================================================================
 */

G_DEFINE_TYPE(HcChannelItem, hc_channel_item, G_TYPE_OBJECT)

static void
hc_channel_item_finalize (GObject *object)
{
	HcChannelItem *self = HC_CHANNEL_ITEM (object);

	g_free (self->channel);
	g_free (self->topic);
	g_free (self->collation_key);

	G_OBJECT_CLASS (hc_channel_item_parent_class)->finalize (object);
}

static void
hc_channel_item_class_init (HcChannelItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hc_channel_item_finalize;
}

static void
hc_channel_item_init (HcChannelItem *self)
{
	self->channel = NULL;
	self->topic = NULL;
	self->collation_key = NULL;
	self->users = 0;
}

HcChannelItem *
hc_channel_item_new (const gchar *channel, guint users, const gchar *topic)
{
	HcChannelItem *item = g_object_new (HC_TYPE_CHANNEL_ITEM, NULL);

	item->channel = g_strdup (channel);
	item->users = users;
	item->topic = g_strdup (topic);
	item->collation_key = g_utf8_collate_key (channel, -1);
	if (!item->collation_key)
		item->collation_key = g_strdup (channel);

	return item;
}
