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

/* System tray support for GTK4 using LizardByte/tray library
 * Windows implementation - Linux and macOS to be added later
 */

#include <string.h>
#include <stdarg.h>

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

#ifdef WIN32
#include <windows.h>
#include <gdk/win32/gdkwin32.h>
#include "tray.h"

#define TRAY_ICON_NORMAL    0
#define TRAY_ICON_MESSAGE   1
#define TRAY_ICON_HIGHLIGHT 2
#define TRAY_ICON_FILEOFFER 3

#define TIMEOUT 500

typedef enum	/* current icon status */
{
	TS_NONE,
	TS_MESSAGE,
	TS_HIGHLIGHT,
	TS_FILEOFFER,
	TS_CUSTOM /* plugin */
} TrayStatus;

typedef enum
{
	WS_FOCUSED,
	WS_NORMAL,
	WS_HIDDEN
} WinStatus;

static struct tray tray_instance;
static gboolean tray_initialized = FALSE;
static gint flash_tag = 0;
static TrayStatus tray_status = TS_NONE;
static guint tray_idle_tag = 0;

/* Icon file paths */
static char icon_path_normal[MAX_PATH];
static char icon_path_message[MAX_PATH];
static char icon_path_highlight[MAX_PATH];
static char icon_path_fileoffer[MAX_PATH];
static const char *current_flash_icon = NULL;
static gboolean flash_state = FALSE;

/* Custom icons for plugin use */
static char *custom_icon1_path = NULL;
static char *custom_icon2_path = NULL;

/* Message counters */
static int tray_priv_count = 0;
static int tray_pub_count = 0;
static int tray_hilight_count = 0;
static int tray_file_count = 0;

/* Window state for toggle */
static int saved_width = 0, saved_height = 0;
static gboolean was_maximized = FALSE;
static gboolean was_fullscreen = FALSE;

/* Menu items */
static struct tray_menu menu_items[10];

static hexchat_plugin *ph;

/* Forward declarations */
void tray_apply_setup (void);
static void tray_cleanup (void);
static void tray_init_impl (void);
static void tray_stop_flash (void);
static void tray_reset_counts (void);

static WinStatus
tray_get_window_status (void)
{
	const char *st;

	st = hexchat_get_info (ph, "win_status");

	if (!st)
		return WS_HIDDEN;

	if (strcmp (st, "active") == 0)
		return WS_FOCUSED;

	if (strcmp (st, "hidden") == 0)
		return WS_HIDDEN;

	return WS_NORMAL;
}

static int
tray_count_channels (void)
{
	int cons = 0;
	GSList *list;
	session *sess;

	for (list = sess_list; list; list = list->next)
	{
		sess = list->data;
		if (sess->server->connected && sess->channel[0] &&
			 sess->type == SESS_CHANNEL)
			cons++;
	}
	return cons;
}

static int
tray_count_networks (void)
{
	int cons = 0;
	GSList *list;

	for (list = serv_list; list; list = list->next)
	{
		if (((server *)list->data)->connected)
			cons++;
	}
	return cons;
}

static void
tray_set_tooltip (const char *text)
{
	if (tray_initialized)
	{
		tray_instance.tooltip = text;
		tray_update (&tray_instance);
	}
}

static void
tray_set_tipf (const char *format, ...)
{
	static char tooltip_buf[256];
	va_list args;

	va_start (args, format);
	g_vsnprintf (tooltip_buf, sizeof (tooltip_buf), format, args);
	va_end (args);

	tray_set_tooltip (tooltip_buf);
}

void
fe_tray_set_tooltip (const char *text)
{
	tray_set_tooltip (text);
}

static void
tray_reset_counts (void)
{
	tray_priv_count = 0;
	tray_pub_count = 0;
	tray_hilight_count = 0;
	tray_file_count = 0;
}

static void
tray_stop_flash (void)
{
	int nets, chans;

	if (flash_tag)
	{
		g_source_remove (flash_tag);
		flash_tag = 0;
	}

	flash_state = FALSE;
	current_flash_icon = NULL;

	if (tray_initialized)
	{
		tray_instance.icon = icon_path_normal;
		tray_update (&tray_instance);

		nets = tray_count_networks ();
		chans = tray_count_channels ();
		if (nets)
			tray_set_tipf ("Connected to %u networks and %u channels - %s",
								nets, chans, DISPLAY_NAME);
		else
			tray_set_tipf ("Not connected. - %s", DISPLAY_NAME);
	}

	if (custom_icon1_path)
	{
		g_free (custom_icon1_path);
		custom_icon1_path = NULL;
	}

	if (custom_icon2_path)
	{
		g_free (custom_icon2_path);
		custom_icon2_path = NULL;
	}

	tray_status = TS_NONE;
}

static gboolean
tray_flash_timeout (gpointer data)
{
	if (!tray_initialized)
		return G_SOURCE_REMOVE;

	if (!prefs.hex_gui_tray_blink)
	{
		tray_stop_flash ();
		return G_SOURCE_REMOVE;
	}

	flash_state = !flash_state;

	if (custom_icon1_path)
	{
		if (flash_state)
			tray_instance.icon = custom_icon1_path;
		else if (custom_icon2_path)
			tray_instance.icon = custom_icon2_path;
		else
			tray_instance.icon = icon_path_normal;
	}
	else
	{
		if (flash_state && current_flash_icon)
			tray_instance.icon = current_flash_icon;
		else
			tray_instance.icon = icon_path_normal;
	}

	tray_update (&tray_instance);

	return G_SOURCE_CONTINUE;
}

static void
tray_set_flash (const char *icon_path)
{
	if (!tray_initialized)
		return;

	/* already flashing the same icon */
	if (flash_tag && current_flash_icon == icon_path)
		return;

	/* no flashing if window is focused */
	if (tray_get_window_status () == WS_FOCUSED)
		return;

	tray_stop_flash ();

	current_flash_icon = icon_path;
	tray_instance.icon = icon_path;
	tray_update (&tray_instance);

	if (prefs.hex_gui_tray_blink)
		flash_tag = g_timeout_add (TIMEOUT, tray_flash_timeout, NULL);
}

void
fe_tray_set_flash (const char *filename1, const char *filename2, int tout)
{
	tray_apply_setup ();
	if (!tray_initialized)
		return;

	tray_stop_flash ();

	if (tout == -1)
		tout = TIMEOUT;

	/* Store custom icon paths */
	custom_icon1_path = g_strdup (filename1);
	if (filename2)
		custom_icon2_path = g_strdup (filename2);

	tray_instance.icon = custom_icon1_path;
	tray_update (&tray_instance);

	flash_tag = g_timeout_add (tout, tray_flash_timeout, NULL);
	tray_status = TS_CUSTOM;
}

void
fe_tray_set_icon (feicon icon)
{
	tray_apply_setup ();
	if (!tray_initialized)
		return;

	tray_stop_flash ();

	switch (icon)
	{
	case FE_ICON_NORMAL:
		break;
	case FE_ICON_MESSAGE:
	case FE_ICON_PRIVMSG:
		tray_set_flash (icon_path_message);
		break;
	case FE_ICON_HIGHLIGHT:
		tray_set_flash (icon_path_highlight);
		break;
	case FE_ICON_FILEOFFER:
		tray_set_flash (icon_path_fileoffer);
		break;
	}
}

void
fe_tray_set_file (const char *filename)
{
	tray_apply_setup ();
	if (!tray_initialized)
		return;

	tray_stop_flash ();

	if (filename)
	{
		custom_icon1_path = g_strdup (filename);
		tray_instance.icon = custom_icon1_path;
		tray_update (&tray_instance);
		tray_status = TS_CUSTOM;
	}
}

/* returns 0-mixed 1-away 2-back */
static int
tray_find_away_status (void)
{
	GSList *list;
	server *serv;
	int away = 0;
	int back = 0;

	for (list = serv_list; list; list = list->next)
	{
		serv = list->data;

		if (serv->is_away || serv->reconnect_away)
			away++;
		else
			back++;
	}

	if (away && back)
		return 0;

	if (away)
		return 1;

	return 2;
}

/* Menu callbacks */
static void
tray_menu_restore_cb (struct tray_menu *item)
{
	tray_toggle_visibility (FALSE);
}

static void
tray_menu_away_cb (struct tray_menu *item)
{
	GSList *list;
	server *serv;

	for (list = serv_list; list; list = list->next)
	{
		serv = list->data;
		if (serv->connected)
			handle_command (serv->server_session, "away", FALSE);
	}
}

static void
tray_menu_back_cb (struct tray_menu *item)
{
	GSList *list;
	server *serv;

	for (list = serv_list; list; list = list->next)
	{
		serv = list->data;
		if (serv->connected)
			handle_command (serv->server_session, "back", FALSE);
	}
}

static void
tray_menu_prefs_cb (struct tray_menu *item)
{
	extern void setup_open (void);
	setup_open ();
}

static void
tray_menu_quit_cb (struct tray_menu *item)
{
	mg_open_quit_dialog (FALSE);
}

static void
tray_build_menu (void)
{
	int i = 0;
	int away_status = tray_find_away_status ();

	if (tray_get_window_status () == WS_HIDDEN)
		menu_items[i].text = "Restore Window";
	else
		menu_items[i].text = "Hide Window";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = tray_menu_restore_cb;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "-";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = NULL;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "Away";
	menu_items[i].disabled = (away_status == 1);
	menu_items[i].checked = 0;
	menu_items[i].cb = tray_menu_away_cb;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "Back";
	menu_items[i].disabled = (away_status == 2);
	menu_items[i].checked = 0;
	menu_items[i].cb = tray_menu_back_cb;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "-";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = NULL;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "Preferences...";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = tray_menu_prefs_cb;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "-";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = NULL;
	menu_items[i].submenu = NULL;
	i++;

	menu_items[i].text = "Quit";
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = tray_menu_quit_cb;
	menu_items[i].submenu = NULL;
	i++;

	/* Terminator */
	menu_items[i].text = NULL;
	menu_items[i].disabled = 0;
	menu_items[i].checked = 0;
	menu_items[i].cb = NULL;
	menu_items[i].submenu = NULL;
}

static gboolean
tray_idle_handler (gpointer data)
{
	if (!tray_initialized)
		return G_SOURCE_REMOVE;

	if (tray_loop (0) == -1)
	{
		tray_initialized = FALSE;
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static void
tray_init_icon_paths (void)
{
	wchar_t exe_path_w[MAX_PATH];
	char exe_path[MAX_PATH];
	char *exe_dir;
	int len;

	GetModuleFileNameW (NULL, exe_path_w, MAX_PATH);

	/* Convert wide char to UTF-8 */
	len = WideCharToMultiByte (CP_UTF8, 0, exe_path_w, -1, exe_path, MAX_PATH, NULL, NULL);
	if (len == 0)
		return;

	/* Get directory from path */
	exe_dir = g_path_get_dirname (exe_path);

	/* Build icon paths */
	g_snprintf (icon_path_normal, MAX_PATH, "%s\\share\\icons\\tray_normal.ico", exe_dir);
	g_snprintf (icon_path_message, MAX_PATH, "%s\\share\\icons\\tray_message.ico", exe_dir);
	g_snprintf (icon_path_highlight, MAX_PATH, "%s\\share\\icons\\tray_highlight.ico", exe_dir);
	g_snprintf (icon_path_fileoffer, MAX_PATH, "%s\\share\\icons\\tray_fileoffer.ico", exe_dir);

	g_free (exe_dir);
}

static void
tray_init_impl (void)
{
	if (tray_initialized)
		return;

	tray_init_icon_paths ();

	flash_tag = 0;
	tray_status = TS_NONE;
	custom_icon1_path = NULL;
	custom_icon2_path = NULL;

	/* Build menu */
	tray_build_menu ();

	/* Initialize tray instance
	 * Note: Both left and right clicks show the context menu.
	 * The "Restore/Hide Window" menu item provides toggle functionality.
	 * This is consistent with Linux AppIndicator behavior.
	 */
	memset (&tray_instance, 0, sizeof (tray_instance));
	tray_instance.icon = icon_path_normal;
	tray_instance.tooltip = DISPLAY_NAME;
	tray_instance.menu = menu_items;

	if (tray_init (&tray_instance) < 0)
	{
		g_warning ("Failed to initialize system tray");
		return;
	}

	tray_initialized = TRUE;

	/* Set up idle handler to process tray events */
	tray_idle_tag = g_idle_add (tray_idle_handler, NULL);

	/* Set initial tooltip */
	tray_stop_flash ();
}

static void
tray_cleanup (void)
{
	tray_stop_flash ();

	if (tray_idle_tag)
	{
		g_source_remove (tray_idle_tag);
		tray_idle_tag = 0;
	}

	if (tray_initialized)
	{
		tray_exit ();
		tray_initialized = FALSE;
	}
}

gboolean
tray_toggle_visibility (gboolean force_hide)
{
	GtkWindow *win;

	if (!tray_initialized)
		return FALSE;

	/* ph may have an invalid context now */
	hexchat_set_context (ph, hexchat_find_context (ph, NULL, NULL));

	win = GTK_WINDOW (hexchat_get_info (ph, "gtkwin_ptr"));

	tray_stop_flash ();
	tray_reset_counts ();

	if (!win)
		return FALSE;

	if (force_hide || gtk_widget_get_visible (GTK_WIDGET (win)))
	{
		/* Save window state before hiding */
		gtk_window_get_default_size (win, &saved_width, &saved_height);
		was_maximized = gtk_window_is_maximized (win);
		was_fullscreen = gtk_window_is_fullscreen (win);

		/* Execute away command if enabled */
		if (prefs.hex_gui_tray_away)
			hexchat_command (ph, "ALLSERV AWAY");

		/* Hide using GTK - this keeps GTK's visibility state in sync */
		gtk_widget_set_visible (GTK_WIDGET (win), FALSE);
	}
	else
	{
		/* Execute back command if enabled */
		if (prefs.hex_gui_tray_away)
			hexchat_command (ph, "ALLSERV BACK");

		/* Restore window using GTK */
		gtk_widget_set_visible (GTK_WIDGET (win), TRUE);

		if (was_maximized)
			gtk_window_maximize (win);

		if (was_fullscreen)
			gtk_window_fullscreen (win);

		/* Bring to front */
		gtk_window_present (win);
	}

	/* Rebuild menu (to toggle "Hide/Restore" text) */
	tray_build_menu ();
	tray_update (&tray_instance);

	return TRUE;
}

void
tray_apply_setup (void)
{
	if (tray_initialized)
	{
		if (!prefs.hex_gui_tray)
			tray_cleanup ();
	}
	else
	{
		GtkWindow *window = GTK_WINDOW (hexchat_get_info (ph, "gtkwin_ptr"));
		if (prefs.hex_gui_tray && gtkutil_tray_icon_supported (window))
			tray_init_impl ();
	}
}

/* Event hook callbacks */
static int
tray_hilight_cb (char *word[], void *userdata)
{
	if (prefs.hex_input_tray_hilight)
	{
		tray_set_flash (icon_path_highlight);

		tray_hilight_count++;
		if (tray_hilight_count == 1)
			tray_set_tipf ("Highlighted message from: %s (%s) - %s",
								word[1], hexchat_get_info (ph, "channel"), DISPLAY_NAME);
		else
			tray_set_tipf ("%u highlighted messages, latest from: %s (%s) - %s",
								tray_hilight_count, word[1], hexchat_get_info (ph, "channel"),
								DISPLAY_NAME);
	}

	return HEXCHAT_EAT_NONE;
}

static int
tray_message_cb (char *word[], void *userdata)
{
	if (tray_status == TS_HIGHLIGHT)
		return HEXCHAT_EAT_NONE;

	if (prefs.hex_input_tray_chans)
	{
		tray_set_flash (icon_path_message);

		tray_pub_count++;
		if (tray_pub_count == 1)
			tray_set_tipf ("Channel message from: %s (%s) - %s",
								word[1], hexchat_get_info (ph, "channel"), DISPLAY_NAME);
		else
			tray_set_tipf ("%u channel messages. - %s", tray_pub_count, DISPLAY_NAME);
	}

	return HEXCHAT_EAT_NONE;
}

static void
tray_priv (char *from, char *text)
{
	const char *network;

	if (alert_match_word (from, prefs.hex_irc_no_hilight))
		return;

	network = hexchat_get_info (ph, "network");
	if (!network)
		network = hexchat_get_info (ph, "server");

	if (prefs.hex_input_tray_priv)
	{
		tray_set_flash (icon_path_message);

		tray_priv_count++;
		if (tray_priv_count == 1)
			tray_set_tipf ("Private message from: %s (%s) - %s", from,
								network, DISPLAY_NAME);
		else
			tray_set_tipf ("%u private messages, latest from: %s (%s) - %s",
								tray_priv_count, from, network, DISPLAY_NAME);
	}
}

static int
tray_priv_cb (char *word[], void *userdata)
{
	tray_priv (word[1], word[2]);

	return HEXCHAT_EAT_NONE;
}

static int
tray_invited_cb (char *word[], void *userdata)
{
	if (!prefs.hex_away_omit_alerts || tray_find_away_status () != 1)
		tray_priv (word[2], "Invited");

	return HEXCHAT_EAT_NONE;
}

static int
tray_dcc_cb (char *word[], void *userdata)
{
	const char *network;

	network = hexchat_get_info (ph, "network");
	if (!network)
		network = hexchat_get_info (ph, "server");

	if (prefs.hex_input_tray_priv && (!prefs.hex_away_omit_alerts || tray_find_away_status () != 1))
	{
		tray_set_flash (icon_path_fileoffer);

		tray_file_count++;
		if (tray_file_count == 1)
			tray_set_tipf ("File offer from: %s (%s) - %s", word[1], network,
								DISPLAY_NAME);
		else
			tray_set_tipf ("%u file offers, latest from: %s (%s) - %s",
								tray_file_count, word[1], network, DISPLAY_NAME);
	}

	return HEXCHAT_EAT_NONE;
}

static int
tray_focus_cb (char *word[], void *userdata)
{
	tray_stop_flash ();
	tray_reset_counts ();
	return HEXCHAT_EAT_NONE;
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

	hexchat_hook_print (ph, "Channel Msg Hilight", -1, tray_hilight_cb, NULL);
	hexchat_hook_print (ph, "Channel Action Hilight", -1, tray_hilight_cb, NULL);

	hexchat_hook_print (ph, "Channel Message", -1, tray_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Action", -1, tray_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Notice", -1, tray_message_cb, NULL);

	hexchat_hook_print (ph, "Private Message", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Message to Dialog", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action to Dialog", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Notice", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Invited", -1, tray_invited_cb, NULL);

	hexchat_hook_print (ph, "DCC Offer", -1, tray_dcc_cb, NULL);

	hexchat_hook_print (ph, "Focus Window", -1, tray_focus_cb, NULL);

	GtkWindow *window = GTK_WINDOW (hexchat_get_info (ph, "gtkwin_ptr"));
	if (prefs.hex_gui_tray && gtkutil_tray_icon_supported (window))
		tray_init_impl ();

	return 1;       /* return 1 for success */
}

int
tray_plugin_deinit (hexchat_plugin *plugin_handle)
{
	tray_cleanup ();
	return 1;
}

#else /* !WIN32 - Stub implementations for non-Windows platforms */

static hexchat_plugin *ph;

void
fe_tray_set_tooltip (const char *text)
{
	/* Stub - tray icon not yet available on this platform */
}

void
fe_tray_set_flash (const char *filename1, const char *filename2, int tout)
{
	/* Stub - tray icon not yet available on this platform */
}

void
fe_tray_set_icon (feicon icon)
{
	/* Stub - tray icon not yet available on this platform */
}

void
fe_tray_set_file (const char *filename)
{
	/* Stub - tray icon not yet available on this platform */
}

gboolean
tray_toggle_visibility (gboolean force_hide)
{
	/* Stub - tray icon not yet available on this platform */
	return FALSE;
}

void
tray_apply_setup (void)
{
	/* Stub - tray icon not yet available on this platform */
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

	/* Tray icon functionality not yet available on this platform */
	return 1;       /* return 1 for success */
}

int
tray_plugin_deinit (hexchat_plugin *plugin_handle)
{
	return 1;
}

#endif /* WIN32 */
