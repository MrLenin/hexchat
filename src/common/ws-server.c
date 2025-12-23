/* HexChat WebSocket/HTTP Server
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

#include "ws-server.h"

/* Maximum message size */
#define MAX_MESSAGE_SIZE 65536

/* Per-session data for WebSocket connections */
struct _HcClient {
	unsigned char buf[LWS_PRE + MAX_MESSAGE_SIZE];
	size_t len;
	GQueue *msg_queue;      /* Per-client message queue */
	GMutex queue_mutex;     /* Protect the queue */
	gpointer user_data;     /* User-associated data */
	struct lws *wsi;        /* libwebsockets handle */
	HcServer *server;       /* Back-pointer to server */
};

/* Server instance */
struct _HcServer {
	struct lws_context *context;
	struct lws_protocols *protocols;
	GList *clients;
	GMutex clients_mutex;

	/* Thread management */
	GThread *thread;
	volatile gboolean running;

	/* Outgoing message queue */
	GAsyncQueue *outgoing_queue;

	/* Configuration */
	int port;
	char *protocol_name;

	/* Callbacks */
	HcServerCallbacks callbacks;
	gpointer user_data;
};

/* Message wrapper for thread-safe communication */
struct ws_outgoing_msg {
	char *message;
	HcClient *target;  /* NULL = broadcast to all */
};

/* HTTP response state */
struct http_response {
	char *body;
	const char *content_type;
	int status;
	size_t sent;
};

/* Forward declarations */
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);
static int http_callback(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);

/* Process outgoing message queue - called from WS thread */
static void
process_outgoing_queue(HcServer *server)
{
	struct ws_outgoing_msg *msg;
	GList *iter;

	while ((msg = g_async_queue_try_pop(server->outgoing_queue)) != NULL)
	{
		g_mutex_lock(&server->clients_mutex);

		if (msg->target)
		{
			/* Send to specific client */
			HcClient *client = msg->target;
			g_mutex_lock(&client->queue_mutex);
			g_queue_push_tail(client->msg_queue, g_strdup(msg->message));
			g_mutex_unlock(&client->queue_mutex);
			lws_callback_on_writable(client->wsi);
		}
		else
		{
			/* Broadcast to all clients */
			for (iter = server->clients; iter; iter = iter->next)
			{
				HcClient *client = (HcClient *)iter->data;
				g_mutex_lock(&client->queue_mutex);
				g_queue_push_tail(client->msg_queue, g_strdup(msg->message));
				g_mutex_unlock(&client->queue_mutex);
				lws_callback_on_writable(client->wsi);
			}
		}

		g_mutex_unlock(&server->clients_mutex);

		g_free(msg->message);
		g_free(msg);
	}
}

/* WebSocket callback */
static int
ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
            void *user, void *in, size_t len)
{
	HcClient *client = (HcClient *)user;
	HcServer *server = NULL;

	/* Get server from protocol user data */
	if (lws_get_protocol(wsi))
	{
		server = (HcServer *)lws_get_protocol(wsi)->user;
	}

	switch (reason)
	{
	case LWS_CALLBACK_ESTABLISHED:
		if (!server)
			return -1;

		client->msg_queue = g_queue_new();
		g_mutex_init(&client->queue_mutex);
		client->wsi = wsi;
		client->server = server;
		client->user_data = NULL;

		g_mutex_lock(&server->clients_mutex);
		server->clients = g_list_append(server->clients, client);
		g_mutex_unlock(&server->clients_mutex);

		printf("WebSocket client connected (total: %d)\n",
		       g_list_length(server->clients));

		/* Notify via callback - schedule on main thread */
		if (server->callbacks.on_connect)
		{
			g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			                (GSourceFunc)server->callbacks.on_connect,
			                client, NULL);
		}
		break;

	case LWS_CALLBACK_CLOSED:
		if (!server || !client)
			return 0;

		g_mutex_lock(&server->clients_mutex);
		server->clients = g_list_remove(server->clients, client);
		g_mutex_unlock(&server->clients_mutex);

		/* Notify via callback */
		if (server->callbacks.on_disconnect)
		{
			server->callbacks.on_disconnect(server, client, server->user_data);
		}

		/* Clean up message queue */
		if (client->msg_queue)
		{
			g_mutex_lock(&client->queue_mutex);
			while (!g_queue_is_empty(client->msg_queue))
			{
				g_free(g_queue_pop_head(client->msg_queue));
			}
			g_queue_free(client->msg_queue);
			client->msg_queue = NULL;
			g_mutex_unlock(&client->queue_mutex);
			g_mutex_clear(&client->queue_mutex);
		}

		printf("WebSocket client disconnected (total: %d)\n",
		       g_list_length(server->clients));
		break;

	case LWS_CALLBACK_RECEIVE:
		if (!server || !client || len == 0 || !in)
			return 0;

		/* Notify via callback */
		if (server->callbacks.on_message)
		{
			server->callbacks.on_message(server, client, (const char *)in, len,
			                             server->user_data);
		}
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (!client || !client->msg_queue)
			return 0;

		g_mutex_lock(&client->queue_mutex);
		if (!g_queue_is_empty(client->msg_queue))
		{
			char *msg = g_queue_pop_head(client->msg_queue);
			if (msg)
			{
				size_t msg_len = strlen(msg);
				if (msg_len < MAX_MESSAGE_SIZE - LWS_PRE)
				{
					memcpy(&client->buf[LWS_PRE], msg, msg_len);
					lws_write(wsi, &client->buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
				}
				g_free(msg);

				/* If there are more messages, request another write callback */
				if (!g_queue_is_empty(client->msg_queue))
				{
					lws_callback_on_writable(wsi);
				}
			}
		}
		g_mutex_unlock(&client->queue_mutex);
		break;

	default:
		break;
	}

	return 0;
}

/* HTTP callback */
static int
http_callback(struct lws *wsi, enum lws_callback_reasons reason,
              void *user, void *in, size_t len)
{
	struct http_response *resp = (struct http_response *)user;
	HcServer *server = NULL;

	/* Get server from protocol user data */
	if (lws_get_protocol(wsi))
	{
		server = (HcServer *)lws_get_protocol(wsi)->user;
	}

	switch (reason)
	{
	case LWS_CALLBACK_HTTP:
		if (!server)
			return -1;

		if (server->callbacks.on_http)
		{
			char *response_body = NULL;
			const char *content_type = "text/html";
			int status = 200;
			char uri[256];
			char *query = NULL;

			/* Extract URI and query string */
			lws_snprintf(uri, sizeof(uri), "%s", (const char *)in);
			query = strchr(uri, '?');
			if (query)
			{
				*query = '\0';
				query++;
			}

			if (server->callbacks.on_http(server, "GET", uri, query,
			                              server->user_data,
			                              &response_body, &content_type, &status))
			{
				/* Build HTTP response */
				unsigned char buffer[LWS_PRE + 512];
				unsigned char *p = buffer + LWS_PRE;
				unsigned char *end = buffer + sizeof(buffer);
				size_t body_len = response_body ? strlen(response_body) : 0;

				if (lws_add_http_common_headers(wsi, status, content_type,
				                                body_len, &p, end))
					return 1;

				if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE,
				                                   &p, end))
					return 1;

				/* Store response for body callback */
				if (resp && response_body)
				{
					resp->body = response_body;
					resp->content_type = content_type;
					resp->status = status;
					resp->sent = 0;
					lws_callback_on_writable(wsi);
					return 0;
				}

				g_free(response_body);
				return 0;
			}
		}

		/* Default 404 response */
		lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, "Not Found");
		return -1;

	case LWS_CALLBACK_HTTP_WRITEABLE:
		if (resp && resp->body)
		{
			size_t remaining = strlen(resp->body) - resp->sent;
			size_t to_send = remaining > 4096 ? 4096 : remaining;
			unsigned char buffer[LWS_PRE + 4096];

			memcpy(buffer + LWS_PRE, resp->body + resp->sent, to_send);

			int write_flags = LWS_WRITE_HTTP;
			if (to_send == remaining)
				write_flags |= LWS_WRITE_HTTP_FINAL;

			lws_write(wsi, buffer + LWS_PRE, to_send, write_flags);
			resp->sent += to_send;

			if (resp->sent < strlen(resp->body))
			{
				lws_callback_on_writable(wsi);
				return 0;
			}

			g_free(resp->body);
			resp->body = NULL;

			if (lws_http_transaction_completed(wsi))
				return -1;
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
	HcServer *server = (HcServer *)data;

	printf("WebSocket thread started\n");

	while (server->running)
	{
		/* Process any outgoing messages from main thread */
		process_outgoing_queue(server);

		/* Service libwebsockets - this blocks for up to 50ms */
		lws_service(server->context, 50);
	}

	printf("WebSocket thread exiting\n");
	return NULL;
}

/* Create and start server */
HcServer *
hc_server_new(int port, const char *protocol_name,
              const HcServerCallbacks *callbacks,
              gpointer user_data)
{
	HcServer *server;
	struct lws_context_creation_info info;

	server = g_new0(HcServer, 1);
	server->port = port;
	server->protocol_name = g_strdup(protocol_name ? protocol_name : "hexchat");
	server->user_data = user_data;

	if (callbacks)
		server->callbacks = *callbacks;

	g_mutex_init(&server->clients_mutex);
	server->outgoing_queue = g_async_queue_new();

	/* Set up protocols - WebSocket and HTTP */
	server->protocols = g_new0(struct lws_protocols, 3);

	/* HTTP protocol */
	server->protocols[0].name = "http";
	server->protocols[0].callback = http_callback;
	server->protocols[0].per_session_data_size = sizeof(struct http_response);
	server->protocols[0].rx_buffer_size = 0;
	server->protocols[0].user = server;

	/* WebSocket protocol */
	server->protocols[1].name = server->protocol_name;
	server->protocols[1].callback = ws_callback;
	server->protocols[1].per_session_data_size = sizeof(HcClient);
	server->protocols[1].rx_buffer_size = MAX_MESSAGE_SIZE;
	server->protocols[1].user = server;

	/* Terminator */
	server->protocols[2].name = NULL;
	server->protocols[2].callback = NULL;

	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = server->protocols;
	info.gid = -1;
	info.uid = -1;
	info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

	server->context = lws_create_context(&info);
	if (!server->context)
	{
		fprintf(stderr, "Failed to create WebSocket context\n");
		g_free(server->protocol_name);
		g_free(server->protocols);
		g_async_queue_unref(server->outgoing_queue);
		g_mutex_clear(&server->clients_mutex);
		g_free(server);
		return NULL;
	}

	/* Start the WebSocket thread */
	server->running = TRUE;
	server->thread = g_thread_new("websocket", ws_thread_func, server);

	printf("WebSocket server started on port %d\n", port);
	return server;
}

/* Stop and destroy server */
void
hc_server_destroy(HcServer *server)
{
	struct ws_outgoing_msg *msg;

	if (!server)
		return;

	/* Signal thread to stop */
	server->running = FALSE;

	/* Wake up the service loop */
	if (server->context)
	{
		lws_cancel_service(server->context);
	}

	/* Wait for thread to finish */
	if (server->thread)
	{
		g_thread_join(server->thread);
		server->thread = NULL;
	}

	if (server->context)
	{
		lws_context_destroy(server->context);
		server->context = NULL;
	}

	g_mutex_lock(&server->clients_mutex);
	g_list_free(server->clients);
	server->clients = NULL;
	g_mutex_unlock(&server->clients_mutex);

	g_mutex_clear(&server->clients_mutex);

	/* Clean up outgoing queue */
	while ((msg = g_async_queue_try_pop(server->outgoing_queue)) != NULL)
	{
		g_free(msg->message);
		g_free(msg);
	}
	g_async_queue_unref(server->outgoing_queue);

	g_free(server->protocol_name);
	g_free(server->protocols);
	g_free(server);

	printf("WebSocket server stopped\n");
}

gboolean
hc_server_is_running(HcServer *server)
{
	return server && server->running;
}

int
hc_server_get_port(HcServer *server)
{
	return server ? server->port : 0;
}

void
hc_server_send(HcServer *server, HcClient *client, const char *message)
{
	struct ws_outgoing_msg *msg;

	if (!server || !client || !message || !server->running)
		return;

	msg = g_new0(struct ws_outgoing_msg, 1);
	msg->message = g_strdup(message);
	msg->target = client;

	g_async_queue_push(server->outgoing_queue, msg);

	/* Wake up the service loop to process the message */
	if (server->context)
	{
		lws_cancel_service(server->context);
	}
}

void
hc_server_broadcast(HcServer *server, const char *message)
{
	struct ws_outgoing_msg *msg;

	if (!server || !message || !server->running)
		return;

	msg = g_new0(struct ws_outgoing_msg, 1);
	msg->message = g_strdup(message);
	msg->target = NULL;  /* NULL = broadcast */

	g_async_queue_push(server->outgoing_queue, msg);

	/* Wake up the service loop to process the message */
	if (server->context)
	{
		lws_cancel_service(server->context);
	}
}

int
hc_server_get_client_count(HcServer *server)
{
	int count;

	if (!server)
		return 0;

	g_mutex_lock(&server->clients_mutex);
	count = g_list_length(server->clients);
	g_mutex_unlock(&server->clients_mutex);

	return count;
}

void
hc_client_set_data(HcClient *client, gpointer user_data)
{
	if (client)
		client->user_data = user_data;
}

gpointer
hc_client_get_data(HcClient *client)
{
	return client ? client->user_data : NULL;
}
