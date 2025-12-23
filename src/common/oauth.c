/* HexChat OAuth2/OIDC Support
 * Copyright (C) 2024
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
 * Implements OAuth2 Authorization Code flow with PKCE (RFC 7636)
 * and SASL OAUTHBEARER mechanism (RFC 7628) for IRC authentication.
 */

#include "config.h"

#ifdef USE_LIBWEBSOCKETS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glib.h>

#ifdef USE_OPENSSL
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#endif

#ifdef USE_LIBSOUP
#include <libsoup/soup.h>
#include <jansson.h>
#endif

#include "oauth.h"
#include "ws-server.h"
#include "fe.h"
#include "hexchat.h"
#include "servlist.h"
#include "util.h"

/* PKCE code_verifier character set: unreserved URI characters */
static const char pkce_charset[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

/* Default OAuth callback path */
#define OAUTH_CALLBACK_PATH "/oauth/callback"

/* Default scopes if none specified */
#define OAUTH_DEFAULT_SCOPES "openid"

/* Timeout for OAuth flow (5 minutes) */
#define OAUTH_TIMEOUT_SECONDS 300

/* Active OAuth sessions */
static GList *active_sessions = NULL;
static GMutex sessions_mutex;

/* Forward declarations */
static gboolean oauth_http_callback(HcServer *server,
                                    const char *method,
                                    const char *uri,
                                    const char *query,
                                    gpointer user_data,
                                    char **response_out,
                                    const char **content_type_out,
                                    int *status_out);
static void oauth_exchange_code(oauth_session *session, const char *code);
static gboolean oauth_timeout_callback(gpointer user_data);

/*
 * Initialize OAuth subsystem
 */
void
oauth_init(void)
{
	g_mutex_init(&sessions_mutex);
}

/*
 * Cleanup OAuth subsystem
 */
void
oauth_cleanup(void)
{
	g_mutex_lock(&sessions_mutex);

	/* Cancel all active sessions */
	GList *iter;
	for (iter = active_sessions; iter; iter = iter->next)
	{
		oauth_session *session = (oauth_session *)iter->data;
		if (session->callback_server)
		{
			hc_server_destroy(session->callback_server);
			session->callback_server = NULL;
		}
	}
	g_list_free_full(active_sessions, (GDestroyNotify)oauth_session_free);
	active_sessions = NULL;

	g_mutex_unlock(&sessions_mutex);
	g_mutex_clear(&sessions_mutex);
}

/*
 * Provider configuration management
 */
oauth_provider_config *
oauth_provider_new(void)
{
	return g_new0(oauth_provider_config, 1);
}

oauth_provider_config *
oauth_provider_copy(const oauth_provider_config *src)
{
	oauth_provider_config *dest;

	if (!src)
		return NULL;

	dest = oauth_provider_new();
	dest->authorization_url = g_strdup(src->authorization_url);
	dest->token_url = g_strdup(src->token_url);
	dest->client_id = g_strdup(src->client_id);
	dest->client_secret = g_strdup(src->client_secret);
	dest->scopes = g_strdup(src->scopes);

	return dest;
}

void
oauth_provider_free(oauth_provider_config *provider)
{
	if (!provider)
		return;

	g_free(provider->authorization_url);
	g_free(provider->token_url);
	g_free(provider->client_id);
	g_free(provider->client_secret);
	g_free(provider->scopes);
	g_free(provider);
}

/*
 * Token management
 */
oauth_token *
oauth_token_new(void)
{
	return g_new0(oauth_token, 1);
}

oauth_token *
oauth_token_copy(const oauth_token *src)
{
	oauth_token *dest;

	if (!src)
		return NULL;

	dest = oauth_token_new();
	dest->access_token = g_strdup(src->access_token);
	dest->refresh_token = g_strdup(src->refresh_token);
	dest->token_type = g_strdup(src->token_type);
	dest->expires_at = src->expires_at;
	dest->scope = g_strdup(src->scope);

	return dest;
}

void
oauth_token_free(oauth_token *token)
{
	if (!token)
		return;

	/* Clear sensitive data before freeing */
	if (token->access_token)
	{
		memset(token->access_token, 0, strlen(token->access_token));
		g_free(token->access_token);
	}
	if (token->refresh_token)
	{
		memset(token->refresh_token, 0, strlen(token->refresh_token));
		g_free(token->refresh_token);
	}
	g_free(token->token_type);
	g_free(token->scope);
	g_free(token);
}

gboolean
oauth_token_is_valid(const oauth_token *token)
{
	if (!token || !token->access_token || token->access_token[0] == '\0')
		return FALSE;

	return TRUE;
}

gboolean
oauth_token_is_expired(const oauth_token *token)
{
	if (!token)
		return TRUE;

	/* If no expiry set, assume it doesn't expire */
	if (token->expires_at == 0)
		return FALSE;

	/* Add a small buffer (30 seconds) to avoid edge cases */
	return time(NULL) >= (token->expires_at - 30);
}

/*
 * PKCE support (RFC 7636)
 */
char *
oauth_generate_code_verifier(void)
{
#ifdef USE_OPENSSL
	unsigned char random_bytes[32];
	char *verifier;
	int i;

	/* Generate 32 random bytes */
	if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1)
		return NULL;

	/* Create 43-character verifier from random bytes */
	verifier = g_malloc(44);
	for (i = 0; i < 43; i++)
	{
		verifier[i] = pkce_charset[random_bytes[i % 32] % (sizeof(pkce_charset) - 1)];
	}
	verifier[43] = '\0';

	return verifier;
#else
	return NULL;
#endif
}

char *
oauth_generate_code_challenge(const char *code_verifier)
{
#ifdef USE_OPENSSL
	unsigned char hash[SHA256_DIGEST_LENGTH];
	char *challenge;
	gsize out_len;

	if (!code_verifier)
		return NULL;

	/* SHA256 hash of the verifier */
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	SHA256((unsigned char *)code_verifier, strlen(code_verifier), hash);
#else
	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, code_verifier, strlen(code_verifier));
	SHA256_Final(hash, &sha256);
#endif

	/* Base64url encode (RFC 4648 section 5) */
	challenge = g_base64_encode(hash, SHA256_DIGEST_LENGTH);

	/* Convert to base64url: replace + with -, / with _, remove = padding */
	char *p;
	for (p = challenge; *p; p++)
	{
		if (*p == '+')
			*p = '-';
		else if (*p == '/')
			*p = '_';
	}

	/* Remove padding */
	p = strchr(challenge, '=');
	if (p)
		*p = '\0';

	return challenge;
#else
	return NULL;
#endif
}

/*
 * State parameter for CSRF protection
 */
char *
oauth_generate_state(void)
{
#ifdef USE_OPENSSL
	unsigned char random_bytes[16];
	char *state;

	if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1)
		return NULL;

	/* Hex encode the random bytes */
	state = g_malloc(33);
	for (int i = 0; i < 16; i++)
	{
		sprintf(state + (i * 2), "%02x", random_bytes[i]);
	}
	state[32] = '\0';

	return state;
#else
	return NULL;
#endif
}

/*
 * OAUTHBEARER encoding (RFC 7628)
 * Format: n,,\x01auth=Bearer <token>\x01host=<host>\x01port=<port>\x01\x01
 */
char *
oauth_encode_sasl_oauthbearer(const char *token, const char *host, int port)
{
	char *raw;
	char *encoded;

	if (!token)
		return NULL;

	/* Build GS2 header + key-value pairs */
	if (host && port > 0)
	{
		raw = g_strdup_printf("n,,\x01auth=Bearer %s\x01host=%s\x01port=%d\x01\x01",
		                      token, host, port);
	}
	else
	{
		raw = g_strdup_printf("n,,\x01auth=Bearer %s\x01\x01", token);
	}

	encoded = g_base64_encode((guchar *)raw, strlen(raw));
	g_free(raw);

	return encoded;
}

/*
 * URL encoding helper
 */
static char *
oauth_url_encode(const char *str)
{
	return g_uri_escape_string(str, NULL, TRUE);
}

/*
 * Build authorization URL
 */
char *
oauth_build_authorization_url(const oauth_provider_config *provider,
                              const char *redirect_uri,
                              const char *state,
                              const char *code_challenge)
{
	char *url;
	char *encoded_redirect;
	char *encoded_scope;
	char *encoded_client_id;
	const char *scopes;

	if (!provider || !provider->authorization_url || !provider->client_id)
		return NULL;

	encoded_redirect = oauth_url_encode(redirect_uri);
	encoded_client_id = oauth_url_encode(provider->client_id);
	scopes = provider->scopes ? provider->scopes : OAUTH_DEFAULT_SCOPES;
	encoded_scope = oauth_url_encode(scopes);

	url = g_strdup_printf("%s?response_type=code&client_id=%s&redirect_uri=%s"
	                      "&scope=%s&state=%s&code_challenge=%s&code_challenge_method=S256",
	                      provider->authorization_url,
	                      encoded_client_id,
	                      encoded_redirect,
	                      encoded_scope,
	                      state,
	                      code_challenge);

	g_free(encoded_redirect);
	g_free(encoded_client_id);
	g_free(encoded_scope);

	return url;
}

/*
 * Parse query string to extract parameter
 */
static char *
oauth_get_query_param(const char *query, const char *param)
{
	char *search;
	char *start;
	char *end;
	char *value;

	if (!query || !param)
		return NULL;

	search = g_strdup_printf("%s=", param);
	start = strstr(query, search);
	g_free(search);

	if (!start)
		return NULL;

	start += strlen(param) + 1;
	end = strchr(start, '&');

	if (end)
		value = g_strndup(start, end - start);
	else
		value = g_strdup(start);

	/* URL decode */
	char *decoded = g_uri_unescape_string(value, NULL);
	g_free(value);

	return decoded;
}

/*
 * Session management
 */
void
oauth_session_free(oauth_session *session)
{
	if (!session)
		return;

	if (session->callback_server)
	{
		hc_server_destroy(session->callback_server);
	}

	/* Clear sensitive data */
	if (session->code_verifier)
	{
		memset(session->code_verifier, 0, strlen(session->code_verifier));
		g_free(session->code_verifier);
	}
	if (session->state_param)
	{
		memset(session->state_param, 0, strlen(session->state_param));
		g_free(session->state_param);
	}

	g_free(session->code_challenge);
	g_free(session->error_message);
	oauth_provider_free(session->provider);
	g_free(session);
}

/*
 * Find session by state parameter
 */
static oauth_session *
oauth_find_session_by_state(const char *state)
{
	GList *iter;

	g_mutex_lock(&sessions_mutex);

	for (iter = active_sessions; iter; iter = iter->next)
	{
		oauth_session *session = (oauth_session *)iter->data;
		if (session->state_param && g_strcmp0(session->state_param, state) == 0)
		{
			g_mutex_unlock(&sessions_mutex);
			return session;
		}
	}

	g_mutex_unlock(&sessions_mutex);
	return NULL;
}

/*
 * Remove session from active list
 */
static void
oauth_remove_session(oauth_session *session)
{
	g_mutex_lock(&sessions_mutex);
	active_sessions = g_list_remove(active_sessions, session);
	g_mutex_unlock(&sessions_mutex);
}

/*
 * Complete the OAuth session (success or error)
 */
static void
oauth_complete_session(oauth_session *session, oauth_token *token, const char *error)
{
	/* Stop the callback server */
	if (session->callback_server)
	{
		hc_server_destroy(session->callback_server);
		session->callback_server = NULL;
	}

	/* Remove from active sessions */
	oauth_remove_session(session);

	/* Call completion callback */
	if (session->completion_callback)
	{
		session->completion_callback(session->irc_server, token, error);
	}

	/* Free the token (callback should have copied it if needed) */
	oauth_token_free(token);

	/* Free session */
	oauth_session_free(session);
}

/*
 * HTTP callback for OAuth redirect
 */
static gboolean
oauth_http_callback(HcServer *server,
                    const char *method,
                    const char *uri,
                    const char *query,
                    gpointer user_data,
                    char **response_out,
                    const char **content_type_out,
                    int *status_out)
{
	oauth_session *session = (oauth_session *)user_data;
	char *code = NULL;
	char *state = NULL;
	char *error = NULL;

	/* Only handle GET requests to callback path */
	if (g_strcmp0(method, "GET") != 0)
		return FALSE;

	if (!g_str_has_prefix(uri, OAUTH_CALLBACK_PATH))
		return FALSE;

	/* Extract parameters from query string */
	if (query)
	{
		code = oauth_get_query_param(query, "code");
		state = oauth_get_query_param(query, "state");
		error = oauth_get_query_param(query, "error");
	}

	/* Validate state parameter */
	if (!state || g_strcmp0(state, session->state_param) != 0)
	{
		*response_out = g_strdup("<html><body><h1>Error</h1>"
		                         "<p>Invalid state parameter. This may be a CSRF attack.</p>"
		                         "<p>Please close this window and try again.</p></body></html>");
		*content_type_out = "text/html";
		*status_out = 400;

		g_free(code);
		g_free(state);
		g_free(error);

		/* Complete with error on idle */
		g_idle_add((GSourceFunc)oauth_complete_session, session);
		session->error_message = g_strdup("Invalid state parameter");

		return TRUE;
	}

	/* Check for OAuth error */
	if (error)
	{
		char *error_desc = oauth_get_query_param(query, "error_description");

		*response_out = g_strdup_printf("<html><body><h1>Authorization Failed</h1>"
		                                "<p>Error: %s</p><p>%s</p>"
		                                "<p>Please close this window.</p></body></html>",
		                                error, error_desc ? error_desc : "");
		*content_type_out = "text/html";
		*status_out = 400;

		session->error_message = g_strdup_printf("OAuth error: %s", error);

		g_free(code);
		g_free(state);
		g_free(error);
		g_free(error_desc);

		/* Schedule completion on idle */
		session->state = OAUTH_STATE_ERROR;
		g_idle_add((GSourceFunc)oauth_complete_session, session);

		return TRUE;
	}

	/* Check for authorization code */
	if (!code)
	{
		*response_out = g_strdup("<html><body><h1>Error</h1>"
		                         "<p>No authorization code received.</p>"
		                         "<p>Please close this window and try again.</p></body></html>");
		*content_type_out = "text/html";
		*status_out = 400;

		session->error_message = g_strdup("No authorization code received");
		session->state = OAUTH_STATE_ERROR;

		g_free(state);
		g_idle_add((GSourceFunc)oauth_complete_session, session);

		return TRUE;
	}

	/* Success - show success page and exchange code for token */
	*response_out = g_strdup("<html><body><h1>Authorization Successful</h1>"
	                         "<p>You can close this window and return to HexChat.</p>"
	                         "<script>window.close();</script></body></html>");
	*content_type_out = "text/html";
	*status_out = 200;

	/* Schedule token exchange on idle */
	session->state = OAUTH_STATE_EXCHANGING_TOKEN;

	/* Store code for exchange (will be freed in oauth_exchange_code) */
	char *code_copy = g_strdup(code);
	g_free(code);
	g_free(state);

	/* Use idle callback to exchange code */
	g_idle_add((GSourceFunc)oauth_exchange_code, session);

	/* Store code in session temporarily for the exchange */
	/* We'll pass it through user_data of the session */
	session->error_message = code_copy; /* Reuse field temporarily */

	return TRUE;
}

#ifdef USE_LIBSOUP
/*
 * Parse JSON token response using jansson
 * Returns oauth_token on success, NULL on error (sets error_out)
 */
static oauth_token *
oauth_parse_token_response(GBytes *body, char **error_out)
{
	const char *data;
	gsize size;
	oauth_token *token = NULL;
	json_t *root = NULL;
	json_error_t json_error;
	json_t *value;

	data = g_bytes_get_data(body, &size);
	if (!data || size == 0)
	{
		*error_out = g_strdup("Empty response from token endpoint");
		return NULL;
	}

	/* Parse JSON using jansson */
	root = json_loadb(data, size, 0, &json_error);
	if (!root)
	{
		*error_out = g_strdup_printf("Failed to parse token response: %s",
		                             json_error.text);
		return NULL;
	}

	if (!json_is_object(root))
	{
		*error_out = g_strdup("Token response is not a JSON object");
		json_decref(root);
		return NULL;
	}

	/* Check for error response */
	value = json_object_get(root, "error");
	if (value && json_is_string(value))
	{
		const char *err = json_string_value(value);
		json_t *desc_value = json_object_get(root, "error_description");
		const char *desc = (desc_value && json_is_string(desc_value))
		                   ? json_string_value(desc_value)
		                   : NULL;
		*error_out = g_strdup_printf("OAuth error: %s%s%s", err,
		                             desc ? " - " : "", desc ? desc : "");
		json_decref(root);
		return NULL;
	}

	/* Extract required access_token */
	value = json_object_get(root, "access_token");
	if (!value || !json_is_string(value))
	{
		*error_out = g_strdup("Token response missing access_token");
		json_decref(root);
		return NULL;
	}

	token = oauth_token_new();
	token->access_token = g_strdup(json_string_value(value));

	/* Extract optional fields */
	value = json_object_get(root, "refresh_token");
	if (value && json_is_string(value))
		token->refresh_token = g_strdup(json_string_value(value));

	value = json_object_get(root, "token_type");
	if (value && json_is_string(value))
		token->token_type = g_strdup(json_string_value(value));

	value = json_object_get(root, "scope");
	if (value && json_is_string(value))
		token->scope = g_strdup(json_string_value(value));

	value = json_object_get(root, "expires_in");
	if (value && json_is_integer(value))
	{
		json_int_t expires_in = json_integer_value(value);
		token->expires_at = time(NULL) + expires_in;
	}

	json_decref(root);
	return token;
}

/*
 * Callback for async token exchange completion
 */
static void
oauth_token_exchange_callback(GObject *source, GAsyncResult *result, gpointer user_data)
{
	oauth_session *session = (oauth_session *)user_data;
	SoupSession *soup_session = SOUP_SESSION(source);
	GBytes *body = NULL;
	GError *error = NULL;
	oauth_token *token = NULL;
	char *error_msg = NULL;

	body = soup_session_send_and_read_finish(soup_session, result, &error);

	if (error)
	{
		error_msg = g_strdup_printf("Token exchange failed: %s", error->message);
		g_error_free(error);
		oauth_complete_session(session, NULL, error_msg);
		g_free(error_msg);
		g_object_unref(soup_session);
		return;
	}

	/* Parse the token response */
	token = oauth_parse_token_response(body, &error_msg);
	g_bytes_unref(body);

	if (!token)
	{
		oauth_complete_session(session, NULL, error_msg);
		g_free(error_msg);
		g_object_unref(soup_session);
		return;
	}

	/* Success! */
	oauth_complete_session(session, token, NULL);
	g_object_unref(soup_session);
}

/*
 * Exchange authorization code for access token
 * This is called on the main thread via g_idle_add
 */
static gboolean
oauth_exchange_code_impl(gpointer user_data)
{
	oauth_session *session = (oauth_session *)user_data;
	SoupSession *soup_session;
	SoupMessage *msg;
	char *redirect_uri;
	char *post_body;
	char *code;

	/* Retrieve the authorization code stored in error_message field */
	code = session->error_message;
	session->error_message = NULL;

	if (!code)
	{
		oauth_complete_session(session, NULL, "No authorization code available");
		return G_SOURCE_REMOVE;
	}

	/* Build redirect URI (must match what was used for authorization) */
	redirect_uri = g_strdup_printf("http://localhost:%d%s",
	                               session->callback_port, OAUTH_CALLBACK_PATH);

	/* Build POST body */
	if (session->provider->client_secret && session->provider->client_secret[0])
	{
		post_body = g_strdup_printf(
			"grant_type=authorization_code&code=%s&redirect_uri=%s"
			"&client_id=%s&client_secret=%s&code_verifier=%s",
			code,
			redirect_uri,
			session->provider->client_id,
			session->provider->client_secret,
			session->code_verifier);
	}
	else
	{
		/* Public client - no client_secret */
		post_body = g_strdup_printf(
			"grant_type=authorization_code&code=%s&redirect_uri=%s"
			"&client_id=%s&code_verifier=%s",
			code,
			redirect_uri,
			session->provider->client_id,
			session->code_verifier);
	}

	g_free(redirect_uri);
	g_free(code);

	/* Create libsoup session */
	soup_session = soup_session_new();

	/* Create POST request */
	msg = soup_message_new("POST", session->provider->token_url);
	if (!msg)
	{
		oauth_complete_session(session, NULL, "Failed to create token request");
		g_free(post_body);
		g_object_unref(soup_session);
		return G_SOURCE_REMOVE;
	}

	/* Set request body */
	soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
	                                          g_bytes_new_take(post_body, strlen(post_body)));

	/* Send async request */
	soup_session_send_and_read_async(soup_session, msg, G_PRIORITY_DEFAULT, NULL,
	                                  oauth_token_exchange_callback, session);

	g_object_unref(msg);

	return G_SOURCE_REMOVE;
}

static void
oauth_exchange_code(oauth_session *session, const char *code)
{
	/* The code parameter is unused here - we get it from session->error_message
	 * which was set in oauth_http_callback. This function is called via g_idle_add
	 * which only passes the session pointer.
	 */
	(void)code;
	oauth_exchange_code_impl(session);
}

#else /* !USE_LIBSOUP */

/*
 * Exchange authorization code for access token
 * Stub implementation when libsoup is not available
 */
static void
oauth_exchange_code(oauth_session *session, const char *code)
{
	(void)code;
	oauth_complete_session(session, NULL,
	                       "Token exchange not available (libsoup3 not installed)");
}

#endif /* USE_LIBSOUP */

/*
 * Timeout callback for OAuth flow
 */
static gboolean
oauth_timeout_callback(gpointer user_data)
{
	oauth_session *session = (oauth_session *)user_data;

	if (session->state == OAUTH_STATE_WAITING_FOR_CODE)
	{
		session->state = OAUTH_STATE_ERROR;
		session->error_message = g_strdup("OAuth flow timed out");
		oauth_complete_session(session, NULL, session->error_message);
	}

	return G_SOURCE_REMOVE;
}

/*
 * Begin OAuth authorization flow
 */
oauth_session *
oauth_begin_authorization(struct ircnet *network,
                          oauth_completion_callback callback,
                          gpointer user_data)
{
	oauth_session *session;
	oauth_provider_config *provider;
	char *redirect_uri;
	char *auth_url;
	HcServerCallbacks server_callbacks = {0};

	if (!network)
		return NULL;

	/* Build provider config from network settings */
	provider = oauth_provider_new();
	provider->authorization_url = g_strdup(network->oauth_authorization_url);
	provider->token_url = g_strdup(network->oauth_token_url);
	provider->client_id = g_strdup(network->oauth_client_id);
	provider->client_secret = g_strdup(network->oauth_client_secret);
	provider->scopes = g_strdup(network->oauth_scopes);

	/* Validate required fields */
	if (!provider->authorization_url || !provider->token_url || !provider->client_id)
	{
		oauth_provider_free(provider);
		if (callback)
			callback(NULL, NULL, "Missing OAuth configuration (authorization URL, token URL, or client ID)");
		return NULL;
	}

	/* Create session */
	session = g_new0(oauth_session, 1);
	session->state = OAUTH_STATE_IDLE;
	session->network = network;
	session->provider = provider;
	session->completion_callback = callback;
	session->user_data = user_data;

	/* Generate PKCE values */
	session->code_verifier = oauth_generate_code_verifier();
	if (!session->code_verifier)
	{
		oauth_session_free(session);
		if (callback)
			callback(NULL, NULL, "Failed to generate PKCE code verifier");
		return NULL;
	}

	session->code_challenge = oauth_generate_code_challenge(session->code_verifier);
	if (!session->code_challenge)
	{
		oauth_session_free(session);
		if (callback)
			callback(NULL, NULL, "Failed to generate PKCE code challenge");
		return NULL;
	}

	/* Generate state parameter */
	session->state_param = oauth_generate_state();
	if (!session->state_param)
	{
		oauth_session_free(session);
		if (callback)
			callback(NULL, NULL, "Failed to generate state parameter");
		return NULL;
	}

	/* Start temporary HTTP server for callback */
	server_callbacks.on_http = oauth_http_callback;

	/* Use a random high port */
	session->callback_port = 49152 + (g_random_int() % 16384);
	session->callback_server = hc_server_new(session->callback_port, "oauth",
	                                         &server_callbacks, session);

	if (!session->callback_server)
	{
		/* Try a few more ports */
		int attempts = 5;
		while (attempts-- > 0 && !session->callback_server)
		{
			session->callback_port = 49152 + (g_random_int() % 16384);
			session->callback_server = hc_server_new(session->callback_port, "oauth",
			                                         &server_callbacks, session);
		}

		if (!session->callback_server)
		{
			oauth_session_free(session);
			if (callback)
				callback(NULL, NULL, "Failed to start OAuth callback server");
			return NULL;
		}
	}

	/* Build redirect URI */
	redirect_uri = g_strdup_printf("http://localhost:%d%s",
	                               session->callback_port, OAUTH_CALLBACK_PATH);

	/* Build authorization URL */
	auth_url = oauth_build_authorization_url(provider, redirect_uri,
	                                         session->state_param,
	                                         session->code_challenge);
	g_free(redirect_uri);

	if (!auth_url)
	{
		oauth_session_free(session);
		if (callback)
			callback(NULL, NULL, "Failed to build authorization URL");
		return NULL;
	}

	/* Add to active sessions */
	g_mutex_lock(&sessions_mutex);
	active_sessions = g_list_prepend(active_sessions, session);
	g_mutex_unlock(&sessions_mutex);

	/* Set timeout */
	g_timeout_add_seconds(OAUTH_TIMEOUT_SECONDS, oauth_timeout_callback, session);

	/* Update state */
	session->state = OAUTH_STATE_WAITING_FOR_CODE;

	/* Open browser */
	fe_open_url(auth_url);
	g_free(auth_url);

	return session;
}

/*
 * Cancel an in-progress OAuth flow
 */
void
oauth_cancel(oauth_session *session)
{
	if (!session)
		return;

	session->state = OAUTH_STATE_ERROR;
	session->error_message = g_strdup("OAuth flow cancelled");
	oauth_complete_session(session, NULL, session->error_message);
}

#ifdef USE_LIBSOUP

/* Context for token refresh callback */
typedef struct {
	oauth_completion_callback callback;
	gpointer user_data;
	SoupSession *soup_session;
} oauth_refresh_context;

/*
 * Callback for async token refresh completion
 */
static void
oauth_token_refresh_callback(GObject *source, GAsyncResult *result, gpointer user_data)
{
	oauth_refresh_context *ctx = (oauth_refresh_context *)user_data;
	SoupSession *soup_session = SOUP_SESSION(source);
	GBytes *body = NULL;
	GError *error = NULL;
	oauth_token *token = NULL;
	char *error_msg = NULL;

	body = soup_session_send_and_read_finish(soup_session, result, &error);

	if (error)
	{
		error_msg = g_strdup_printf("Token refresh failed: %s", error->message);
		g_error_free(error);
		if (ctx->callback)
			ctx->callback(NULL, NULL, error_msg);
		g_free(error_msg);
		g_object_unref(soup_session);
		g_free(ctx);
		return;
	}

	/* Parse the token response */
	token = oauth_parse_token_response(body, &error_msg);
	g_bytes_unref(body);

	if (!token)
	{
		if (ctx->callback)
			ctx->callback(NULL, NULL, error_msg);
		g_free(error_msg);
		g_object_unref(soup_session);
		g_free(ctx);
		return;
	}

	/* Success! */
	if (ctx->callback)
		ctx->callback(NULL, token, NULL);

	oauth_token_free(token);
	g_object_unref(soup_session);
	g_free(ctx);
}

/*
 * Refresh an expired token
 */
gboolean
oauth_refresh_token(struct ircnet *network,
                    oauth_completion_callback callback,
                    gpointer user_data)
{
	(void)user_data; /* Not used until token storage is implemented */

	if (!network)
	{
		if (callback)
			callback(NULL, NULL, "No network specified");
		return FALSE;
	}

	/* Validate required fields */
	if (!network->oauth_token_url || !network->oauth_client_id)
	{
		if (callback)
			callback(NULL, NULL, "Missing OAuth configuration (token URL or client ID)");
		return FALSE;
	}

	/* Note: We need a refresh_token stored somewhere to refresh.
	 * For now, this function expects the caller to have stored the refresh_token.
	 * A proper implementation would store tokens in a secure location.
	 * This is a placeholder until token storage is implemented.
	 */
	if (callback)
		callback(NULL, NULL, "Token refresh requires stored refresh_token (not yet implemented)");
	return FALSE;

	/* The code below is ready for when token storage is implemented:
	 *
	 * Build POST body
	 * if (network->oauth_client_secret && network->oauth_client_secret[0])
	 * {
	 *     post_body = g_strdup_printf(
	 *         "grant_type=refresh_token&refresh_token=%s"
	 *         "&client_id=%s&client_secret=%s",
	 *         stored_refresh_token,
	 *         network->oauth_client_id,
	 *         network->oauth_client_secret);
	 * }
	 * else
	 * {
	 *     post_body = g_strdup_printf(
	 *         "grant_type=refresh_token&refresh_token=%s&client_id=%s",
	 *         stored_refresh_token,
	 *         network->oauth_client_id);
	 * }
	 *
	 * soup_session = soup_session_new();
	 * msg = soup_message_new("POST", network->oauth_token_url);
	 * soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
	 *                                           g_bytes_new_take(post_body, strlen(post_body)));
	 *
	 * ctx = g_new0(oauth_refresh_context, 1);
	 * ctx->callback = callback;
	 * ctx->user_data = user_data;
	 * ctx->soup_session = soup_session;
	 *
	 * soup_session_send_and_read_async(soup_session, msg, G_PRIORITY_DEFAULT, NULL,
	 *                                   oauth_token_refresh_callback, ctx);
	 * g_object_unref(msg);
	 * return TRUE;
	 */
}

#else /* !USE_LIBSOUP */

/*
 * Refresh an expired token
 * Stub implementation when libsoup is not available
 */
gboolean
oauth_refresh_token(struct ircnet *network,
                    oauth_completion_callback callback,
                    gpointer user_data)
{
	(void)network;
	(void)user_data;

	if (callback)
		callback(NULL, NULL, "Token refresh not available (libsoup3 not installed)");

	return FALSE;
}

#endif /* USE_LIBSOUP */

#endif /* USE_LIBWEBSOCKETS */
