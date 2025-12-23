/* HexChat
 * Copyright (C) 2024 John E
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HEXCHAT_SECURE_STORAGE_H
#define HEXCHAT_SECURE_STORAGE_H

#include <glib.h>

/**
 * Cross-platform secure storage for sensitive data like OAuth tokens.
 *
 * Platform implementations:
 * - Windows: Credential Manager (CredWriteW/CredReadW)
 * - Linux: libsecret (GNOME Keyring / KDE Wallet)
 * - macOS: Keychain Services (future)
 * - Fallback: Memory-only (tokens not persisted)
 */

/**
 * Initialize the secure storage subsystem.
 * Call once at application startup.
 *
 * @return TRUE if secure storage is available, FALSE for fallback mode
 */
gboolean secure_storage_init (void);

/**
 * Shutdown the secure storage subsystem.
 * Call at application exit.
 */
void secure_storage_shutdown (void);

/**
 * Check if secure storage is available on this system.
 *
 * @return TRUE if platform keychain is available
 */
gboolean secure_storage_available (void);

/**
 * Store a secret value securely.
 *
 * @param network_name  Network identifier (e.g., "Libera.Chat")
 * @param key           Key name (e.g., "oauth_access_token")
 * @param value         Secret value to store (will be encrypted)
 * @return TRUE on success, FALSE on failure
 */
gboolean secure_storage_store (const char *network_name,
                               const char *key,
                               const char *value);

/**
 * Retrieve a secret value.
 *
 * @param network_name  Network identifier
 * @param key           Key name
 * @return Secret value (caller must g_free), or NULL if not found
 */
char *secure_storage_retrieve (const char *network_name,
                               const char *key);

/**
 * Delete a stored secret.
 *
 * @param network_name  Network identifier
 * @param key           Key name
 * @return TRUE if deleted (or didn't exist), FALSE on error
 */
gboolean secure_storage_delete (const char *network_name,
                                const char *key);

/**
 * Delete all secrets for a network.
 *
 * @param network_name  Network identifier
 * @return TRUE on success, FALSE on error
 */
gboolean secure_storage_delete_all (const char *network_name);

/* Convenience functions for OAuth tokens */

/**
 * Store OAuth tokens for a network.
 *
 * @param network_name   Network identifier
 * @param access_token   OAuth access token
 * @param refresh_token  OAuth refresh token (may be NULL)
 * @param expires_at     Token expiration timestamp (0 if none)
 * @return TRUE on success
 */
gboolean secure_storage_store_oauth_tokens (const char *network_name,
                                            const char *access_token,
                                            const char *refresh_token,
                                            gint64 expires_at);

/**
 * Retrieve OAuth tokens for a network.
 *
 * @param network_name   Network identifier
 * @param access_token   (out) Access token, caller must g_free
 * @param refresh_token  (out) Refresh token, caller must g_free (may be NULL)
 * @param expires_at     (out) Expiration timestamp
 * @return TRUE if tokens found, FALSE otherwise
 */
gboolean secure_storage_retrieve_oauth_tokens (const char *network_name,
                                               char **access_token,
                                               char **refresh_token,
                                               gint64 *expires_at);

/**
 * Clear OAuth tokens for a network.
 *
 * @param network_name  Network identifier
 * @return TRUE on success
 */
gboolean secure_storage_clear_oauth_tokens (const char *network_name);

#endif /* HEXCHAT_SECURE_STORAGE_H */
