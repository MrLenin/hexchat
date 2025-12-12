/* X-Chat
 * Copyright (C) 2006-2007 Peter Zelezny.
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

/* GTK3 Note: GtkStatusIcon has been removed from GTK3 on Windows.
 * This file provides stub implementations to allow the build to succeed.
 * System tray functionality is disabled in this build.
 */

#include <string.h>

#include <gtk/gtk.h>

#include "../common/hexchat-plugin.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/inbound.h"
#include "../common/server.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/outbound.h"
#include "fe-gtk.h"
#include "pixmaps.h"
#include "maingui.h"
#include "menu.h"
#include "gtkutil.h"

#ifndef WIN32
#include <unistd.h>
#endif

static hexchat_plugin *ph;

void tray_apply_setup (void);

void
fe_tray_set_tooltip (const char *text)
{
	/* Stub - tray icon not available in this GTK3 build */
}

void
fe_tray_set_flash (const char *filename1, const char *filename2, int tout)
{
	/* Stub - tray icon not available in this GTK3 build */
}

void
fe_tray_set_icon (feicon icon)
{
	/* Stub - tray icon not available in this GTK3 build */
}

void
fe_tray_set_file (const char *filename)
{
	/* Stub - tray icon not available in this GTK3 build */
}

gboolean
tray_toggle_visibility (gboolean force_hide)
{
	/* Stub - tray icon not available in this GTK3 build */
	return FALSE;
}

void
tray_apply_setup (void)
{
	/* Stub - tray icon not available in this GTK3 build */
}

int
tray_plugin_init (hexchat_plugin *plugin_handle, char **plugin_name,
				char **plugin_desc, char **plugin_version, char *arg)
{
	/* we need to save this for use with any hexchat_* functions */
	ph = plugin_handle;

	*plugin_name = "";
	*plugin_desc = "";
	*plugin_version = "";

	/* Tray icon functionality disabled - GtkStatusIcon not available */
	return 1;       /* return 1 for success */
}

int
tray_plugin_deinit (hexchat_plugin *plugin_handle)
{
	return 1;
}
