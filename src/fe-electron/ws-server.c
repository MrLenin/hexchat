/* HexChat Electron Frontend - WebSocket Server
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
#include <glib.h>
#include <libwebsockets.h>
#include <jansson.h>

#include "../common/hexchat.h"
#include "fe-electron.h"

/* Maximum message size */
#define MAX_MESSAGE_SIZE 65536

/* Per-session data for WebSocket connections */
struct ws_session_data {
	unsigned char buf[LWS_PRE + MAX_MESSAGE_SIZE];
	size_t len;
};

/* Global WebSocket context */
static struct lws_context *ws_context = NULL;
static GList *connected_clients = NULL;

/* Message queue for broadcasting */
static GQueue *message_queue = NULL;
static GMutex queue_mutex;

/* Forward declarations */
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);

/* WebSocket protocol definition */
static struct lws_protocols protocols[] = {
	{
		"hexchat-protocol",
		ws_callback,
		sizeof(struct ws_session_data),
		MAX_MESSAGE_SIZE,
		0, NULL, 0
	},
	{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

/* Handle incoming messages from clients */
static void
handle_client_message(struct lws *wsi, const char *msg, size_t len)
{
	json_t *root;
	json_error_t error;
	const char *type;

	root = json_loadb(msg, len, 0, &error);
	if (!root)
	{
		fprintf(stderr, "JSON parse error: %s\n", error.text);
		return;
	}

	type = json_string_value(json_object_get(root, "type"));
	if (!type)
	{
		json_decref(root);
		return;
	}

	if (g_strcmp0(type, "command.execute") == 0)
	{
		json_t *payload = json_object_get(root, "payload");
		const char *session_id = json_string_value(json_object_get(payload, "sessionId"));
		const char *command = json_string_value(json_object_get(payload, "command"));

		if (session_id && command)
		{
			handle_ws_command(session_id, command);
		}
	}
	else if (g_strcmp0(type, "input.text") == 0)
	{
		json_t *payload = json_object_get(root, "payload");
		const char *session_id = json_string_value(json_object_get(payload, "sessionId"));
		const char *text = json_string_value(json_object_get(payload, "text"));

		if (session_id && text)
		{
			handle_ws_input(session_id, text);
		}
	}
	else if (g_strcmp0(type, "state.requestSync") == 0)
	{
		/* TODO: Send full state sync to this client */
	}

	json_decref(root);
}

/* WebSocket callback */
static int
ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
            void *user, void *in, size_t len)
{
	struct ws_session_data *pss = (struct ws_session_data *)user;

	switch (reason)
	{
	case LWS_CALLBACK_ESTABLISHED:
		connected_clients = g_list_append(connected_clients, wsi);
		printf("WebSocket client connected (total: %d)\n",
		       g_list_length(connected_clients));
		break;

	case LWS_CALLBACK_CLOSED:
		connected_clients = g_list_remove(connected_clients, wsi);
		printf("WebSocket client disconnected (total: %d)\n",
		       g_list_length(connected_clients));
		break;

	case LWS_CALLBACK_RECEIVE:
		if (len > 0 && in)
		{
			handle_client_message(wsi, (const char *)in, len);
		}
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		g_mutex_lock(&queue_mutex);
		if (!g_queue_is_empty(message_queue))
		{
			char *msg = g_queue_pop_head(message_queue);
			if (msg)
			{
				size_t msg_len = strlen(msg);
				if (msg_len < MAX_MESSAGE_SIZE - LWS_PRE)
				{
					memcpy(&pss->buf[LWS_PRE], msg, msg_len);
					lws_write(wsi, &pss->buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
				}
				g_free(msg);

				/* If there are more messages, request another write callback */
				if (!g_queue_is_empty(message_queue))
				{
					lws_callback_on_writable(wsi);
				}
			}
		}
		g_mutex_unlock(&queue_mutex);
		break;

	default:
		break;
	}

	return 0;
}

/* Initialize WebSocket server */
int
ws_server_init(int port)
{
	struct lws_context_creation_info info;

	g_mutex_init(&queue_mutex);
	message_queue = g_queue_new();

	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = protocols;
	info.gid = -1;
	info.uid = -1;
	info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

	ws_context = lws_create_context(&info);
	if (!ws_context)
	{
		fprintf(stderr, "Failed to create WebSocket context\n");
		return -1;
	}

	return 0;
}

/* Shutdown WebSocket server */
void
ws_server_shutdown(void)
{
	if (ws_context)
	{
		lws_context_destroy(ws_context);
		ws_context = NULL;
	}

	g_mutex_lock(&queue_mutex);
	while (!g_queue_is_empty(message_queue))
	{
		g_free(g_queue_pop_head(message_queue));
	}
	g_queue_free(message_queue);
	message_queue = NULL;
	g_mutex_unlock(&queue_mutex);

	g_mutex_clear(&queue_mutex);

	g_list_free(connected_clients);
	connected_clients = NULL;
}

/* Broadcast JSON message to all connected clients */
void
ws_server_broadcast_json(const char *json)
{
	GList *iter;

	if (!json || !ws_context)
		return;

	g_mutex_lock(&queue_mutex);

	/* Add message to queue for each client */
	for (iter = connected_clients; iter; iter = iter->next)
	{
		g_queue_push_tail(message_queue, g_strdup(json));
		lws_callback_on_writable((struct lws *)iter->data);
	}

	g_mutex_unlock(&queue_mutex);
}

/* Poll WebSocket server - called from main loop */
gboolean
ws_server_poll(gpointer data)
{
	if (ws_context)
	{
		lws_service(ws_context, 0);
	}
	return G_SOURCE_CONTINUE;
}
