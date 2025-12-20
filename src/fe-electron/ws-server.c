/* HexChat Electron Frontend - WebSocket Server
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * WebSocket server runs in a dedicated thread to avoid interfering with
 * GLib's main loop which handles IRC sockets.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libwebsockets.h>
#include <jansson.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "fe-electron.h"

/* Maximum message size */
#define MAX_MESSAGE_SIZE 65536

/* Per-session data for WebSocket connections */
struct ws_session_data {
	unsigned char buf[LWS_PRE + MAX_MESSAGE_SIZE];
	size_t len;
	GQueue *msg_queue;  /* Per-client message queue */
	GMutex queue_mutex; /* Protect the queue */
};

/* Global WebSocket context */
static struct lws_context *ws_context = NULL;
static GList *connected_clients = NULL;
static GMutex clients_mutex;

/* Thread management */
static GThread *ws_thread = NULL;
static volatile gboolean ws_running = FALSE;

/* Queue for outgoing messages from main thread to WS thread */
static GAsyncQueue *outgoing_queue = NULL;

/* Message wrapper for thread-safe communication */
struct ws_outgoing_msg {
	char *json;
	struct lws *target_wsi;  /* NULL = broadcast to all */
};

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

/* Handle incoming messages from clients - called from WS thread */
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
			/* Schedule on main thread via idle callback */
			char *sid = g_strdup(session_id);
			char *cmd = g_strdup(command);
			g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			                (GSourceFunc)handle_ws_command_idle,
			                g_strdup_printf("%s\t%s", sid, cmd),
			                g_free);
			g_free(sid);
			g_free(cmd);
		}
	}
	else if (g_strcmp0(type, "input.text") == 0)
	{
		json_t *payload = json_object_get(root, "payload");
		const char *session_id = json_string_value(json_object_get(payload, "sessionId"));
		const char *text = json_string_value(json_object_get(payload, "text"));

		if (session_id && text)
		{
			/* Schedule on main thread via idle callback */
			char *sid = g_strdup(session_id);
			char *txt = g_strdup(text);
			g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			                (GSourceFunc)handle_ws_input_idle,
			                g_strdup_printf("%s\t%s", sid, txt),
			                g_free);
			g_free(sid);
			g_free(txt);
		}
	}
	else if (g_strcmp0(type, "state.requestSync") == 0)
	{
		/* TODO: Send full state sync to this client */
	}

	json_decref(root);
}

/* Idle callback wrappers for main thread execution */
gboolean
handle_ws_command_idle(gpointer data)
{
	char *str = (char *)data;
	char **parts = g_strsplit(str, "\t", 2);
	if (parts[0] && parts[1])
	{
		handle_ws_command(parts[0], parts[1]);
	}
	g_strfreev(parts);
	return G_SOURCE_REMOVE;
}

gboolean
handle_ws_input_idle(gpointer data)
{
	char *str = (char *)data;
	char **parts = g_strsplit(str, "\t", 2);
	if (parts[0] && parts[1])
	{
		handle_ws_input(parts[0], parts[1]);
	}
	g_strfreev(parts);
	return G_SOURCE_REMOVE;
}

/* Process outgoing message queue - called from WS thread */
static void
process_outgoing_queue(void)
{
	struct ws_outgoing_msg *msg;
	GList *iter;

	while ((msg = g_async_queue_try_pop(outgoing_queue)) != NULL)
	{
		g_mutex_lock(&clients_mutex);

		if (msg->target_wsi)
		{
			/* Send to specific client */
			struct ws_session_data *pss = (struct ws_session_data *)lws_wsi_user(msg->target_wsi);
			if (pss)
			{
				g_mutex_lock(&pss->queue_mutex);
				g_queue_push_tail(pss->msg_queue, g_strdup(msg->json));
				g_mutex_unlock(&pss->queue_mutex);
				lws_callback_on_writable(msg->target_wsi);
			}
		}
		else
		{
			/* Broadcast to all clients */
			for (iter = connected_clients; iter; iter = iter->next)
			{
				struct lws *wsi = (struct lws *)iter->data;
				struct ws_session_data *pss = (struct ws_session_data *)lws_wsi_user(wsi);
				if (pss)
				{
					g_mutex_lock(&pss->queue_mutex);
					g_queue_push_tail(pss->msg_queue, g_strdup(msg->json));
					g_mutex_unlock(&pss->queue_mutex);
					lws_callback_on_writable(wsi);
				}
			}
		}

		g_mutex_unlock(&clients_mutex);

		g_free(msg->json);
		g_free(msg);
	}
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
		pss->msg_queue = g_queue_new();
		g_mutex_init(&pss->queue_mutex);
		g_mutex_lock(&clients_mutex);
		connected_clients = g_list_append(connected_clients, wsi);
		g_mutex_unlock(&clients_mutex);
		printf("WebSocket client connected (total: %d)\n",
		       g_list_length(connected_clients));
		/* Send current state to the new client */
		ws_send_state_sync(wsi);
		break;

	case LWS_CALLBACK_CLOSED:
		g_mutex_lock(&clients_mutex);
		connected_clients = g_list_remove(connected_clients, wsi);
		g_mutex_unlock(&clients_mutex);
		/* Clean up message queue */
		if (pss->msg_queue)
		{
			g_mutex_lock(&pss->queue_mutex);
			while (!g_queue_is_empty(pss->msg_queue))
			{
				g_free(g_queue_pop_head(pss->msg_queue));
			}
			g_queue_free(pss->msg_queue);
			pss->msg_queue = NULL;
			g_mutex_unlock(&pss->queue_mutex);
			g_mutex_clear(&pss->queue_mutex);
		}
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
		if (pss->msg_queue)
		{
			g_mutex_lock(&pss->queue_mutex);
			if (!g_queue_is_empty(pss->msg_queue))
			{
				char *msg = g_queue_pop_head(pss->msg_queue);
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
					if (!g_queue_is_empty(pss->msg_queue))
					{
						lws_callback_on_writable(wsi);
					}
				}
			}
			g_mutex_unlock(&pss->queue_mutex);
		}
		break;

	default:
		break;
	}

	return 0;
}

/* WebSocket thread main function */
static gpointer
ws_thread_func(gpointer data)
{
	printf("WebSocket thread started\n");

	while (ws_running)
	{
		/* Process any outgoing messages from main thread */
		process_outgoing_queue();

		/* Service libwebsockets - this blocks for up to 50ms */
		lws_service(ws_context, 50);
	}

	printf("WebSocket thread exiting\n");
	return NULL;
}

/* Initialize WebSocket server */
int
ws_server_init(int port)
{
	struct lws_context_creation_info info;

	g_mutex_init(&clients_mutex);
	outgoing_queue = g_async_queue_new();

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

	/* Start the WebSocket thread */
	ws_running = TRUE;
	ws_thread = g_thread_new("websocket", ws_thread_func, NULL);

	return 0;
}

/* Shutdown WebSocket server */
void
ws_server_shutdown(void)
{
	/* Signal thread to stop */
	ws_running = FALSE;

	/* Wake up the service loop */
	if (ws_context)
	{
		lws_cancel_service(ws_context);
	}

	/* Wait for thread to finish */
	if (ws_thread)
	{
		g_thread_join(ws_thread);
		ws_thread = NULL;
	}

	if (ws_context)
	{
		lws_context_destroy(ws_context);
		ws_context = NULL;
	}

	g_mutex_lock(&clients_mutex);
	g_list_free(connected_clients);
	connected_clients = NULL;
	g_mutex_unlock(&clients_mutex);

	g_mutex_clear(&clients_mutex);

	/* Clean up outgoing queue */
	if (outgoing_queue)
	{
		struct ws_outgoing_msg *msg;
		while ((msg = g_async_queue_try_pop(outgoing_queue)) != NULL)
		{
			g_free(msg->json);
			g_free(msg);
		}
		g_async_queue_unref(outgoing_queue);
		outgoing_queue = NULL;
	}
}

/* Broadcast JSON message to all connected clients - thread-safe */
void
ws_server_broadcast_json(const char *json)
{
	struct ws_outgoing_msg *msg;

	if (!json || !ws_running)
		return;

	msg = g_new0(struct ws_outgoing_msg, 1);
	msg->json = g_strdup(json);
	msg->target_wsi = NULL;  /* NULL = broadcast */

	g_async_queue_push(outgoing_queue, msg);

	/* Wake up the service loop to process the message */
	if (ws_context)
	{
		lws_cancel_service(ws_context);
	}
}

/* Send message to a single client - thread-safe */
static void
ws_send_to_client(struct lws *wsi, const char *json)
{
	struct ws_outgoing_msg *msg;

	if (!json || !wsi || !ws_running)
		return;

	msg = g_new0(struct ws_outgoing_msg, 1);
	msg->json = g_strdup(json);
	msg->target_wsi = wsi;

	g_async_queue_push(outgoing_queue, msg);

	/* Wake up the service loop to process the message */
	if (ws_context)
	{
		lws_cancel_service(ws_context);
	}
}

/* Send full state sync to newly connected client */
void
ws_send_state_sync(struct lws *wsi)
{
	GSList *list;
	struct session *sess;
	char *json;

	/* Send all existing sessions */
	for (list = sess_list; list; list = list->next)
	{
		sess = (struct session *)list->data;
		json = json_session_created(sess, FALSE);
		if (json)
		{
			ws_send_to_client(wsi, json);
			g_free(json);
		}
	}

	printf("Sent state sync to client (%d sessions)\n", g_slist_length(sess_list));
}

/* Poll WebSocket server - no longer needed with threaded approach */
gboolean
ws_server_poll(gpointer data)
{
	/* This is now a no-op - WebSocket runs in its own thread */
	return G_SOURCE_REMOVE;
}
