/* HexChat Electron Frontend
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * WebSocket-based frontend for Electron GUI.
 * Based on fe-text.c structure but broadcasts events via WebSocket.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/util.h"
#include "../common/fe.h"
#include "fe-electron.h"

static GMainLoop *main_loop = NULL;
static GHashTable *session_map = NULL;  /* session* -> session_id string */
static GHashTable *server_map = NULL;   /* server* -> server_id string */
static guint session_counter = 0;
static guint server_counter = 0;

/* Generate unique session ID */
char *
fe_electron_get_session_id (struct session *sess)
{
	return g_hash_table_lookup (session_map, sess);
}

/* Generate unique server ID */
char *
fe_electron_get_server_id (struct server *serv)
{
	return g_hash_table_lookup (server_map, serv);
}

static char *
generate_session_id (struct session *sess)
{
	char *id = g_strdup_printf ("sess_%u", ++session_counter);
	g_hash_table_insert (session_map, sess, id);
	return id;
}

static char *
generate_server_id (struct server *serv)
{
	char *id = g_strdup_printf ("serv_%u", ++server_counter);
	g_hash_table_insert (server_map, serv, id);
	return id;
}

/* Command handling from WebSocket clients */
void
handle_ws_command (const char *session_id, const char *command)
{
	GHashTableIter iter;
	gpointer key, value;
	struct session *sess = NULL;

	/* Find session by ID */
	g_hash_table_iter_init (&iter, session_map);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (g_strcmp0 ((char *)value, session_id) == 0)
		{
			sess = (struct session *)key;
			break;
		}
	}

	if (sess)
	{
		handle_command (sess, (char *)command, FALSE);
	}
}

void
handle_ws_input (const char *session_id, const char *text)
{
	GHashTableIter iter;
	gpointer key, value;
	struct session *sess = NULL;

	/* Find session by ID */
	g_hash_table_iter_init (&iter, session_map);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (g_strcmp0 ((char *)value, session_id) == 0)
		{
			sess = (struct session *)key;
			break;
		}
	}

	if (sess)
	{
		handle_multiline (sess, (char *)text, TRUE, FALSE);
	}
}

/* === Frontend Interface Implementation === */

void
fe_new_window (struct session *sess, int focus)
{
	char *json;

	generate_session_id (sess);

	current_sess = sess;

	if (!sess->server->front_session)
		sess->server->front_session = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;
	if (!current_tab || focus)
		current_tab = sess;

	/* Broadcast session created event */
	json = json_session_created (sess, focus);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_new_server (struct server *serv)
{
	generate_server_id (serv);
}

void
fe_print_text (struct session *sess, char *text, time_t stamp,
               gboolean no_activity)
{
	char *json;

	json = json_print_text (sess, text, stamp, no_activity);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_close_window (struct session *sess)
{
	char *json;

	json = json_session_closed (sess);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}

	g_hash_table_remove (session_map, sess);
	session_free (sess);
}

void
fe_set_topic (struct session *sess, char *topic, char *stripped_topic)
{
	char *json;

	json = json_set_topic (sess, topic, stripped_topic);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_channel (struct session *sess)
{
	char *json;

	json = json_set_channel (sess);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_title (struct session *sess)
{
	char *json;

	json = json_set_title (sess);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_nick (struct server *serv, char *newnick)
{
	char *json;

	json = json_set_nick (serv, newnick);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_away (server *serv)
{
	char *json;

	json = json_set_away (serv);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_lag (server *serv, long lag)
{
	char *json;

	json = json_set_lag (serv, lag);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_set_tab_color (struct session *sess, tabcolor col)
{
	char *json;

	json = json_tab_color (sess, col);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_server_event (server *serv, int type, int arg)
{
	char *json;

	json = json_server_event (serv, type, arg);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_message (char *msg, int flags)
{
	char *json;

	json = json_message (msg, flags);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_userlist_insert (struct session *sess, struct User *newuser, gboolean sel)
{
	char *json;

	json = json_userlist_insert (sess, newuser, sel);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

int
fe_userlist_remove (struct session *sess, struct User *user)
{
	char *json;

	json = json_userlist_remove (sess, user->nick);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
	return 0;
}

void
fe_userlist_update (struct session *sess, struct User *user)
{
	char *json;

	json = json_userlist_update (sess, user);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

void
fe_userlist_clear (struct session *sess)
{
	char *json;

	json = json_userlist_clear (sess);
	if (json)
	{
		ws_server_broadcast_json (json);
		g_free (json);
	}
}

/* === Event Loop === */

int
fe_timeout_add (int interval, void *callback, void *userdata)
{
	return g_timeout_add (interval, (GSourceFunc) callback, userdata);
}

int
fe_timeout_add_seconds (int interval, void *callback, void *userdata)
{
	return g_timeout_add_seconds (interval, (GSourceFunc) callback, userdata);
}

void
fe_timeout_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_input_add (int sok, int flags, void *func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifdef G_OS_WIN32
	if (flags & FIA_FD)
		channel = g_io_channel_win32_new_fd (sok);
	else
		channel = g_io_channel_win32_new_socket (sok);
#else
	channel = g_io_channel_unix_new (sok);
#endif

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch (channel, type, (GIOFunc) func, data);
	g_io_channel_unref (channel);

	return tag;
}

void
fe_input_remove (int tag)
{
	g_source_remove (tag);
}

void
fe_idle_add (void *func, void *data)
{
	g_idle_add (func, data);
}

/* === Command Line Arguments === */

static char *arg_cfgdir = NULL;
static gint arg_show_autoload = 0;
static gint arg_show_config = 0;
static gint arg_show_version = 0;
static gint arg_ws_port = ELECTRON_WS_PORT;

static const GOptionEntry gopt_entries[] =
{
	{"no-auto", 'a', 0, G_OPTION_ARG_NONE, &arg_dont_autoconnect, N_("Don't auto connect to servers"), NULL},
	{"cfgdir", 'd', 0, G_OPTION_ARG_STRING, &arg_cfgdir, N_("Use a different config directory"), "PATH"},
	{"no-plugins", 'n', 0, G_OPTION_ARG_NONE, &arg_skip_plugins, N_("Don't auto load any plugins"), NULL},
	{"plugindir", 'p', 0, G_OPTION_ARG_NONE, &arg_show_autoload, N_("Show plugin/script auto-load directory"), NULL},
	{"configdir", 'u', 0, G_OPTION_ARG_NONE, &arg_show_config, N_("Show user config directory"), NULL},
	{"url", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &arg_url, N_("Open an irc://server:port/channel URL"), "URL"},
	{"version", 'v', 0, G_OPTION_ARG_NONE, &arg_show_version, N_("Show version information"), NULL},
	{"ws-port", 'w', 0, G_OPTION_ARG_INT, &arg_ws_port, N_("WebSocket server port"), "PORT"},
	{G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, N_("Open an irc://server:port/channel?key URL"), "URL"},
	{NULL}
};

int
fe_args (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);

	if (error)
	{
		if (error->message)
			printf ("%s\n", error->message);
		return 1;
	}

	g_option_context_free (context);

	if (arg_show_version)
	{
		printf (PACKAGE_NAME " " PACKAGE_VERSION " (Electron Frontend)\n");
		return 0;
	}

	if (arg_show_autoload)
	{
#ifndef USE_PLUGIN
		printf (PACKAGE_NAME " was built without plugin support\n");
		return 1;
#else
#ifdef WIN32
		char *sl, *exe = g_strdup (argv[0]);
		sl = strrchr (exe, '\\');
		if (sl)
		{
			*sl = 0;
			printf ("%s\\plugins\n", exe);
		}
		g_free (exe);
#else
		printf ("%s\n", HEXCHATLIBDIR);
#endif
#endif
		return 0;
	}

	if (arg_show_config)
	{
		printf ("%s\n", get_xdir ());
		return 0;
	}

	if (arg_cfgdir)
	{
		g_free (xdir);
		xdir = strdup (arg_cfgdir);
		if (xdir[strlen (xdir) - 1] == '/')
			xdir[strlen (xdir) - 1] = 0;
		g_free (arg_cfgdir);
	}

	return -1;
}

void
fe_init (void)
{
	/* Initialize session/server maps */
	session_map = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	server_map = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

	/* Disable GUI-specific features */
	prefs.hex_gui_tab_server = 0;
	prefs.hex_gui_autoopen_dialog = 0;
	prefs.hex_gui_lagometer = 0;
	prefs.hex_gui_slist_skip = 1;

	/* Initialize WebSocket server */
	if (ws_server_init (arg_ws_port) != 0)
	{
		fprintf (stderr, "Failed to start WebSocket server on port %d\n", arg_ws_port);
	}
	else
	{
		printf ("HexChat Electron backend started\n");
		printf ("WebSocket server listening on ws://localhost:%d\n", arg_ws_port);
	}
}

void
fe_main (void)
{
	main_loop = g_main_loop_new (NULL, FALSE);

	/* WebSocket server runs in its own thread - no polling needed here.
	 * The GLib main loop now only handles IRC sockets and timers. */

	g_main_loop_run (main_loop);
}

void
fe_exit (void)
{
	ws_server_shutdown ();
	g_main_loop_quit (main_loop);
}

void
fe_cleanup (void)
{
	if (session_map)
		g_hash_table_destroy (session_map);
	if (server_map)
		g_hash_table_destroy (server_map);
}

/* === Stub Implementations === */

void fe_add_rawlog (struct server *serv, char *text, int len, int outbound) {}
void fe_update_mode_buttons (struct session *sess, char mode, char sign) {}
void fe_update_channel_key (struct session *sess) {}
void fe_update_channel_limit (struct session *sess) {}
int fe_is_chanwindow (struct server *serv) { return 0; }
void fe_add_chan_list (struct server *serv, char *chan, char *users, char *topic) {}
void fe_chan_list_end (struct server *serv) {}
gboolean fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode) { return 0; }
gboolean fe_ban_list_end (struct session *sess, int rplcode) { return 0; }
void fe_notify_update (char *name) {}
void fe_notify_ask (char *name, char *networks) {}
void fe_text_clear (struct session *sess, int lines) {}
void fe_progressbar_start (struct session *sess) {}
void fe_progressbar_end (struct server *serv) {}
void fe_userlist_rehash (struct session *sess, struct User *user) {}
void fe_userlist_numbers (struct session *sess) {}
void fe_userlist_set_selected (struct session *sess) {}
void fe_dcc_add (struct DCC *dcc) {}
void fe_dcc_update (struct DCC *dcc) {}
void fe_dcc_remove (struct DCC *dcc) {}
void fe_clear_channel (struct session *sess) {}
void fe_session_callback (struct session *sess) {}
void fe_server_callback (struct server *serv) {}
void fe_url_add (const char *text) {}
void fe_pluginlist_update (void) {}
void fe_buttons_update (struct session *sess) {}
void fe_dlgbuttons_update (struct session *sess) {}
void fe_dcc_send_filereq (struct session *sess, char *nick, int maxcps, int passive) {}
void fe_set_nonchannel (struct session *sess, int state) {}
void fe_change_nick (struct server *serv, char *nick, char *newnick) {}
void fe_ignore_update (int level) {}
void fe_beep (session *sess) {}
int fe_dcc_open_recv_win (int passive) { return FALSE; }
int fe_dcc_open_send_win (int passive) { return FALSE; }
int fe_dcc_open_chat_win (int passive) { return FALSE; }
void fe_userlist_hide (session *sess) {}
void fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags) {}
void fe_set_throttle (server *serv) {}
void fe_serverlist_open (session *sess) {}
void fe_get_bool (char *title, char *prompt, void *callback, void *userdata) {}
void fe_get_str (char *prompt, char *def, void *callback, void *ud) {}
void fe_get_int (char *prompt, int def, void *callback, void *ud) {}
void fe_ctrl_gui (session *sess, fe_gui_action action, int arg) {}
int fe_gui_info (session *sess, int info_type) { return -1; }
void *fe_gui_info_ptr (session *sess, int info_type) { return NULL; }
void fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud) {}
char *fe_get_inputbox_contents (struct session *sess) { return NULL; }
void fe_set_inputbox_contents (struct session *sess, char *text) {}
int fe_get_inputbox_cursor (struct session *sess) { return 0; }
void fe_set_inputbox_cursor (struct session *sess, int delta, int pos) {}
void fe_open_url (const char *url) {}
void fe_menu_del (menu_entry *me) {}
char *fe_menu_add (menu_entry *me) { return NULL; }
void fe_menu_update (menu_entry *me) {}
void fe_uselect (struct session *sess, char *word[], int do_clear, int scroll_to) {}
void fe_flash_window (struct session *sess) {}
void fe_get_file (const char *title, char *initial, void (*callback)(void *userdata, char *file), void *userdata, int flags) {}
void fe_tray_set_flash (const char *filename1, const char *filename2, int timeout) {}
void fe_tray_set_file (const char *filename) {}
void fe_tray_set_icon (feicon icon) {}
void fe_tray_set_tooltip (const char *text) {}
void fe_open_chan_list (server *serv, char *filter, int do_refresh) { serv->p_list_channels (serv, filter, 1); }
const char *fe_get_default_font (void) { return NULL; }
