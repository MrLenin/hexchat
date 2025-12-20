/* HexChat Electron Frontend
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HEXCHAT_FE_ELECTRON_H
#define HEXCHAT_FE_ELECTRON_H

#include <glib.h>

/* Default WebSocket server port */
#define ELECTRON_WS_PORT 9867

/* Session ID generation */
char *fe_electron_get_session_id(struct session *sess);
char *fe_electron_get_server_id(struct server *serv);

/* WebSocket server functions */
int ws_server_init(int port);
void ws_server_shutdown(void);
void ws_server_broadcast_json(const char *json);
gboolean ws_server_poll(gpointer data);

/* JSON protocol functions */
char *json_session_created(struct session *sess, int focus);
char *json_session_closed(struct session *sess);
char *json_print_text(struct session *sess, const char *text, time_t stamp, gboolean no_activity);
char *json_userlist_insert(struct session *sess, struct User *user, gboolean selected);
char *json_userlist_remove(struct session *sess, const char *nick);
char *json_userlist_update(struct session *sess, struct User *user);
char *json_userlist_clear(struct session *sess);
char *json_set_topic(struct session *sess, const char *topic, const char *stripped_topic);
char *json_set_channel(struct session *sess);
char *json_set_title(struct session *sess);
char *json_server_event(struct server *serv, int type, int arg);
char *json_set_nick(struct server *serv, const char *nick);
char *json_set_away(struct server *serv);
char *json_set_lag(struct server *serv, long lag);
char *json_tab_color(struct session *sess, int color);
char *json_message(const char *msg, int flags);

/* Command handling from WebSocket clients */
void handle_ws_command(const char *session_id, const char *command);
void handle_ws_input(const char *session_id, const char *text);

/* Send full state to a newly connected client */
void ws_send_state_sync(struct lws *wsi);

#endif /* HEXCHAT_FE_ELECTRON_H */
