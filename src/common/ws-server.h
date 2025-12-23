/* HexChat WebSocket/HTTP Server
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Generic WebSocket and HTTP server for HexChat.
 * Used by Electron frontend for real-time communication and by
 * GTK frontend for OAuth2/OIDC callbacks.
 */

#ifndef HEXCHAT_WS_SERVER_H
#define HEXCHAT_WS_SERVER_H

#include <glib.h>

/* Opaque handle for a WebSocket/HTTP server instance */
typedef struct _HcServer HcServer;

/* Opaque handle for a client connection */
typedef struct _HcClient HcClient;

/* Callback types */

/**
 * Called when a WebSocket client connects.
 * @param server The server instance
 * @param client The newly connected client
 * @param user_data User data passed to hc_server_new()
 */
typedef void (*HcServerConnectFunc)(HcServer *server, HcClient *client, gpointer user_data);

/**
 * Called when a WebSocket client disconnects.
 * @param server The server instance
 * @param client The disconnecting client
 * @param user_data User data passed to hc_server_new()
 */
typedef void (*HcServerDisconnectFunc)(HcServer *server, HcClient *client, gpointer user_data);

/**
 * Called when a WebSocket message is received.
 * @param server The server instance
 * @param client The client that sent the message
 * @param message The message data (text)
 * @param len Length of the message
 * @param user_data User data passed to hc_server_new()
 */
typedef void (*HcServerMessageFunc)(HcServer *server, HcClient *client,
                                    const char *message, size_t len,
                                    gpointer user_data);

/**
 * Called when an HTTP request is received.
 * Return TRUE if the request was handled, FALSE to let libwebsockets handle it.
 * @param server The server instance
 * @param method HTTP method (GET, POST, etc.)
 * @param uri Request URI path
 * @param query Query string (may be NULL)
 * @param user_data User data passed to hc_server_new()
 * @param response_out If handled, set to the response body (will be freed by server)
 * @param content_type_out If handled, set to content type (e.g., "text/html")
 * @param status_out If handled, set to HTTP status code
 * @return TRUE if request was handled
 */
typedef gboolean (*HcServerHttpFunc)(HcServer *server,
                                     const char *method,
                                     const char *uri,
                                     const char *query,
                                     gpointer user_data,
                                     char **response_out,
                                     const char **content_type_out,
                                     int *status_out);

/* Server callbacks structure */
typedef struct {
	HcServerConnectFunc on_connect;       /* WebSocket client connected */
	HcServerDisconnectFunc on_disconnect; /* WebSocket client disconnected */
	HcServerMessageFunc on_message;       /* WebSocket message received */
	HcServerHttpFunc on_http;             /* HTTP request received */
} HcServerCallbacks;

/**
 * Create and start a new WebSocket/HTTP server.
 * @param port Port to listen on
 * @param protocol_name WebSocket protocol name (e.g., "hexchat-protocol")
 * @param callbacks Callback functions for events
 * @param user_data User data passed to callbacks
 * @return Server instance, or NULL on failure
 */
HcServer *hc_server_new(int port, const char *protocol_name,
                        const HcServerCallbacks *callbacks,
                        gpointer user_data);

/**
 * Stop and destroy a server.
 * @param server Server instance to destroy
 */
void hc_server_destroy(HcServer *server);

/**
 * Check if the server is running.
 * @param server Server instance
 * @return TRUE if running
 */
gboolean hc_server_is_running(HcServer *server);

/**
 * Get the port the server is listening on.
 * @param server Server instance
 * @return Port number
 */
int hc_server_get_port(HcServer *server);

/**
 * Send a message to a specific WebSocket client.
 * Thread-safe - can be called from any thread.
 * @param server Server instance
 * @param client Target client
 * @param message Message to send (will be copied)
 */
void hc_server_send(HcServer *server, HcClient *client, const char *message);

/**
 * Broadcast a message to all connected WebSocket clients.
 * Thread-safe - can be called from any thread.
 * @param server Server instance
 * @param message Message to send (will be copied)
 */
void hc_server_broadcast(HcServer *server, const char *message);

/**
 * Get the number of connected WebSocket clients.
 * @param server Server instance
 * @return Number of clients
 */
int hc_server_get_client_count(HcServer *server);

/**
 * Associate user data with a client connection.
 * @param client Client connection
 * @param user_data User data to associate
 */
void hc_client_set_data(HcClient *client, gpointer user_data);

/**
 * Get user data associated with a client connection.
 * @param client Client connection
 * @return User data, or NULL if none set
 */
gpointer hc_client_get_data(HcClient *client);

#endif /* HEXCHAT_WS_SERVER_H */
