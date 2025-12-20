/* HexChat Electron Frontend - JSON Protocol
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include <jansson.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/fe.h"
#include "fe-electron.h"

/* Helper to get current timestamp in milliseconds */
static json_int_t
get_timestamp_ms(void)
{
	return (json_int_t)(g_get_real_time() / 1000);
}

/* Helper to get session type string */
static const char *
get_session_type(struct session *sess)
{
	switch (sess->type)
	{
	case SESS_SERVER:
		return "server";
	case SESS_CHANNEL:
		return "channel";
	case SESS_DIALOG:
		return "dialog";
	case SESS_NOTICES:
		return "notices";
	case SESS_SNOTICES:
		return "snotices";
	default:
		return "unknown";
	}
}

/* Session created event */
char *
json_session_created(struct session *sess, int focus)
{
	json_t *root, *payload;
	char *result;
	const char *session_id, *server_id;

	session_id = fe_electron_get_session_id(sess);
	server_id = sess->server ? fe_electron_get_server_id(sess->server) : NULL;

	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "serverId", server_id ? json_string(server_id) : json_null());
	json_object_set_new(payload, "name", json_string(sess->channel));
	json_object_set_new(payload, "sessionType", json_string(get_session_type(sess)));
	json_object_set_new(payload, "focus", json_boolean(focus));

	root = json_object();
	json_object_set_new(root, "type", json_string("session.created"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Session closed event */
char *
json_session_closed(struct session *sess)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));

	root = json_object();
	json_object_set_new(root, "type", json_string("session.closed"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Print text event - the main message flow */
char *
json_print_text(struct session *sess, const char *text, time_t stamp, gboolean no_activity)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "text", json_string(text ? text : ""));
	json_object_set_new(payload, "timestamp", json_integer((json_int_t)stamp));
	json_object_set_new(payload, "noActivity", json_boolean(no_activity));

	root = json_object();
	json_object_set_new(root, "type", json_string("text.print"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Userlist insert event */
char *
json_userlist_insert(struct session *sess, struct User *user, gboolean selected)
{
	json_t *root, *payload, *user_obj;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id || !user)
		return NULL;

	user_obj = json_object();
	json_object_set_new(user_obj, "nick", json_string(user->nick));
	json_object_set_new(user_obj, "hostname", user->hostname ? json_string(user->hostname) : json_null());
	json_object_set_new(user_obj, "realname", user->realname ? json_string(user->realname) : json_null());
	json_object_set_new(user_obj, "account", user->account ? json_string(user->account) : json_null());
	json_object_set_new(user_obj, "prefix", json_string(user->prefix));
	json_object_set_new(user_obj, "away", json_boolean(user->away));
	json_object_set_new(user_obj, "op", json_boolean(user->op));
	json_object_set_new(user_obj, "hop", json_boolean(user->hop));
	json_object_set_new(user_obj, "voice", json_boolean(user->voice));

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "user", user_obj);
	json_object_set_new(payload, "selected", json_boolean(selected));

	root = json_object();
	json_object_set_new(root, "type", json_string("userlist.insert"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Userlist remove event */
char *
json_userlist_remove(struct session *sess, const char *nick)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "nick", json_string(nick ? nick : ""));

	root = json_object();
	json_object_set_new(root, "type", json_string("userlist.remove"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Userlist update event */
char *
json_userlist_update(struct session *sess, struct User *user)
{
	json_t *root, *payload, *user_obj;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id || !user)
		return NULL;

	user_obj = json_object();
	json_object_set_new(user_obj, "nick", json_string(user->nick));
	json_object_set_new(user_obj, "hostname", user->hostname ? json_string(user->hostname) : json_null());
	json_object_set_new(user_obj, "realname", user->realname ? json_string(user->realname) : json_null());
	json_object_set_new(user_obj, "account", user->account ? json_string(user->account) : json_null());
	json_object_set_new(user_obj, "prefix", json_string(user->prefix));
	json_object_set_new(user_obj, "away", json_boolean(user->away));
	json_object_set_new(user_obj, "op", json_boolean(user->op));
	json_object_set_new(user_obj, "hop", json_boolean(user->hop));
	json_object_set_new(user_obj, "voice", json_boolean(user->voice));

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "user", user_obj);

	root = json_object();
	json_object_set_new(root, "type", json_string("userlist.update"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Userlist clear event */
char *
json_userlist_clear(struct session *sess)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));

	root = json_object();
	json_object_set_new(root, "type", json_string("userlist.clear"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Topic changed event */
char *
json_set_topic(struct session *sess, const char *topic, const char *stripped_topic)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "topic", json_string(topic ? topic : ""));
	json_object_set_new(payload, "topicStripped", json_string(stripped_topic ? stripped_topic : ""));

	root = json_object();
	json_object_set_new(root, "type", json_string("channel.topic"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Channel changed event */
char *
json_set_channel(struct session *sess)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "channel", json_string(sess->channel));

	root = json_object();
	json_object_set_new(root, "type", json_string("session.channel"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Title changed event */
char *
json_set_title(struct session *sess)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;
	char title[512];

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	/* Build title similar to GTK frontend */
	if (sess->server && sess->server->network)
	{
		ircnet *net = (ircnet *)sess->server->network;
		g_snprintf(title, sizeof(title), "%s - %s", sess->channel,
		           net->name ? net->name : sess->server->servername);
	}
	else
		g_strlcpy(title, sess->channel, sizeof(title));

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "title", json_string(title));

	root = json_object();
	json_object_set_new(root, "type", json_string("ui.title"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Server event */
char *
json_server_event(struct server *serv, int type, int arg)
{
	json_t *root, *payload;
	char *result;
	const char *server_id;
	const char *event_type;

	server_id = fe_electron_get_server_id(serv);
	if (!server_id)
		return NULL;

	switch (type)
	{
	case FE_SE_CONNECT:
		event_type = "connected";
		break;
	case FE_SE_LOGGEDIN:
		event_type = "loggedin";
		break;
	case FE_SE_DISCONNECT:
		event_type = "disconnected";
		break;
	case FE_SE_RECONDELAY:
		event_type = "reconnect_delay";
		break;
	case FE_SE_CONNECTING:
		event_type = "connecting";
		break;
	default:
		event_type = "unknown";
		break;
	}

	payload = json_object();
	json_object_set_new(payload, "serverId", json_string(server_id));
	json_object_set_new(payload, "event", json_string(event_type));
	json_object_set_new(payload, "arg", json_integer(arg));

	root = json_object();
	json_object_set_new(root, "type", json_string("server.event"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Nick changed event */
char *
json_set_nick(struct server *serv, const char *nick)
{
	json_t *root, *payload;
	char *result;
	const char *server_id;

	server_id = fe_electron_get_server_id(serv);
	if (!server_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "serverId", json_string(server_id));
	json_object_set_new(payload, "nick", json_string(nick ? nick : ""));

	root = json_object();
	json_object_set_new(root, "type", json_string("server.nick"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Away status changed event */
char *
json_set_away(struct server *serv)
{
	json_t *root, *payload;
	char *result;
	const char *server_id;

	server_id = fe_electron_get_server_id(serv);
	if (!server_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "serverId", json_string(server_id));
	json_object_set_new(payload, "away", json_boolean(serv->is_away));
	json_object_set_new(payload, "reason", serv->last_away_reason ? json_string(serv->last_away_reason) : json_null());

	root = json_object();
	json_object_set_new(root, "type", json_string("server.away"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Lag update event */
char *
json_set_lag(struct server *serv, long lag)
{
	json_t *root, *payload;
	char *result;
	const char *server_id;

	server_id = fe_electron_get_server_id(serv);
	if (!server_id)
		return NULL;

	payload = json_object();
	json_object_set_new(payload, "serverId", json_string(server_id));
	json_object_set_new(payload, "lag", json_integer(lag));

	root = json_object();
	json_object_set_new(root, "type", json_string("server.lag"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* Tab color event */
char *
json_tab_color(struct session *sess, int color)
{
	json_t *root, *payload;
	char *result;
	const char *session_id;
	const char *color_str;

	session_id = fe_electron_get_session_id(sess);
	if (!session_id)
		return NULL;

	switch (color & ~FE_COLOR_ALLFLAGS)
	{
	case FE_COLOR_NONE:
		color_str = "none";
		break;
	case FE_COLOR_NEW_DATA:
		color_str = "data";
		break;
	case FE_COLOR_NEW_MSG:
		color_str = "message";
		break;
	case FE_COLOR_NEW_HILIGHT:
		color_str = "hilight";
		break;
	default:
		color_str = "none";
		break;
	}

	payload = json_object();
	json_object_set_new(payload, "sessionId", json_string(session_id));
	json_object_set_new(payload, "color", json_string(color_str));

	root = json_object();
	json_object_set_new(root, "type", json_string("ui.tabcolor"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}

/* System message event */
char *
json_message(const char *msg, int flags)
{
	json_t *root, *payload;
	char *result;

	payload = json_object();
	json_object_set_new(payload, "message", json_string(msg ? msg : ""));
	json_object_set_new(payload, "wait", json_boolean(flags & FE_MSG_WAIT));
	json_object_set_new(payload, "info", json_boolean(flags & FE_MSG_INFO));
	json_object_set_new(payload, "warn", json_boolean(flags & FE_MSG_WARN));
	json_object_set_new(payload, "error", json_boolean(flags & FE_MSG_ERROR));

	root = json_object();
	json_object_set_new(root, "type", json_string("ui.message"));
	json_object_set_new(root, "timestamp", json_integer(get_timestamp_ms()));
	json_object_set_new(root, "payload", payload);

	result = json_dumps(root, JSON_COMPACT);
	json_decref(root);

	return result;
}
