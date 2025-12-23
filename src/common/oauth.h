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

#ifndef HEXCHAT_OAUTH_H
#define HEXCHAT_OAUTH_H

#include <glib.h>
#include <time.h>

/* Forward declarations */
struct server;
struct ircnet;
typedef struct _HcServer HcServer;

/* OAuth token storage */
typedef struct oauth_token {
	char *access_token;      /* Bearer token for OAUTHBEARER */
	char *refresh_token;     /* For token refresh */
	char *token_type;        /* Should be "Bearer" */
	time_t expires_at;       /* Unix timestamp when token expires */
	char *scope;             /* Granted scopes (space-separated) */
} oauth_token;

/* OAuth provider configuration */
typedef struct oauth_provider_config {
	char *authorization_url; /* OAuth2 authorization endpoint */
	char *token_url;         /* OAuth2 token endpoint */
	char *client_id;         /* Client ID */
	char *client_secret;     /* Client secret (may be NULL for public clients) */
	char *scopes;            /* Space-separated scopes to request */
} oauth_provider_config;

/* OAuth flow state machine */
typedef enum {
	OAUTH_STATE_IDLE = 0,
	OAUTH_STATE_WAITING_FOR_CODE,    /* Browser opened, waiting for callback */
	OAUTH_STATE_EXCHANGING_TOKEN,    /* Got code, exchanging for token */
	OAUTH_STATE_REFRESHING_TOKEN,    /* Refreshing expired token */
	OAUTH_STATE_COMPLETE,            /* Token ready for use */
	OAUTH_STATE_ERROR
} oauth_state;

/* Callback function type for OAuth completion */
typedef void (*oauth_completion_callback)(struct server *serv,
                                          oauth_token *token,
                                          const char *error);

/* OAuth session - active authorization flow */
typedef struct oauth_session {
	oauth_state state;
	char *state_param;               /* CSRF state parameter */
	char *code_verifier;             /* PKCE code_verifier */
	char *code_challenge;            /* PKCE code_challenge (S256) */
	HcServer *callback_server;       /* Temporary HTTP server for callback */
	int callback_port;               /* Port for OAuth callback */
	struct server *irc_server;       /* Associated IRC server (may be NULL) */
	struct ircnet *network;          /* Associated network config */
	oauth_provider_config *provider; /* Provider configuration (borrowed) */
	oauth_completion_callback completion_callback;
	gpointer user_data;              /* User data for callback */
	char *error_message;             /* Error message if state == ERROR */
} oauth_session;

/*
 * Initialize/cleanup OAuth subsystem
 */
void oauth_init(void);
void oauth_cleanup(void);

/*
 * Provider configuration management
 */
oauth_provider_config *oauth_provider_new(void);
oauth_provider_config *oauth_provider_copy(const oauth_provider_config *src);
void oauth_provider_free(oauth_provider_config *provider);

/*
 * Token management
 */
oauth_token *oauth_token_new(void);
oauth_token *oauth_token_copy(const oauth_token *src);
void oauth_token_free(oauth_token *token);
gboolean oauth_token_is_valid(const oauth_token *token);
gboolean oauth_token_is_expired(const oauth_token *token);

/*
 * Secure token storage
 * Tokens are stored in platform keychain (Windows Credential Manager,
 * Linux libsecret/GNOME Keyring, etc.)
 */
gboolean oauth_save_tokens(const char *network_name, const oauth_token *token);
oauth_token *oauth_load_tokens(const char *network_name);
gboolean oauth_clear_tokens(const char *network_name);

/*
 * PKCE support (RFC 7636)
 * code_verifier: 43-128 character random string
 * code_challenge: BASE64URL(SHA256(code_verifier))
 */
char *oauth_generate_code_verifier(void);
char *oauth_generate_code_challenge(const char *code_verifier);

/*
 * State parameter for CSRF protection
 */
char *oauth_generate_state(void);

/*
 * OAUTHBEARER encoding (RFC 7628)
 * Format: n,,\x01auth=Bearer <token>\x01host=<host>\x01port=<port>\x01\x01
 * Returns base64-encoded string ready for AUTHENTICATE command
 */
char *oauth_encode_sasl_oauthbearer(const char *token, const char *host, int port);

/*
 * Build authorization URL
 * Returns URL to open in browser, caller must free
 */
char *oauth_build_authorization_url(const oauth_provider_config *provider,
                                    const char *redirect_uri,
                                    const char *state,
                                    const char *code_challenge);

/*
 * OAuth authorization flow
 *
 * oauth_begin_authorization:
 *   Starts the OAuth flow:
 *   1. Generates PKCE values and state
 *   2. Starts temporary HTTP server for callback
 *   3. Opens browser to authorization URL
 *   4. Waits for callback with authorization code
 *   5. Exchanges code for token
 *   6. Calls completion callback
 *
 * Returns: oauth_session on success, NULL on error
 * The session is automatically freed after completion callback
 */
oauth_session *oauth_begin_authorization(struct ircnet *network,
                                         oauth_completion_callback callback,
                                         gpointer user_data);

/*
 * Cancel an in-progress OAuth flow
 */
void oauth_cancel(oauth_session *session);

/*
 * Refresh an expired token
 * Calls completion_callback with new token or error
 */
gboolean oauth_refresh_token(struct ircnet *network,
                             oauth_completion_callback callback,
                             gpointer user_data);

/*
 * Session management (internal use)
 */
void oauth_session_free(oauth_session *session);

#endif /* HEXCHAT_OAUTH_H */
