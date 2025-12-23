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

#include "secure-storage.h"
#include "util.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

#ifdef USE_LIBSECRET
#include <libsecret/secret.h>
#endif

/* Credential target prefix for Windows Credential Manager */
#define CRED_PREFIX "HexChat:"

/* libsecret schema for Linux */
#ifdef USE_LIBSECRET
static SecretSchema hexchat_schema = {
    "org.hexchat.oauth",
    SECRET_SCHEMA_NONE,
    {
        { "network", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { "key", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, 0 }
    }
};
#endif

static gboolean storage_initialized = FALSE;
static gboolean storage_available = FALSE;

gboolean
secure_storage_init (void)
{
    if (storage_initialized)
        return storage_available;

    storage_initialized = TRUE;

#ifdef _WIN32
    /* In portable mode, don't use Credential Manager - keep config self-contained */
    if (portable_mode ())
    {
        storage_available = FALSE;
        g_message ("Portable mode: using in-memory token storage only");
    }
    else
    {
        /* Windows Credential Manager is always available */
        storage_available = TRUE;
    }
#elif defined(USE_LIBSECRET)
    /* Check if libsecret service is available */
    GError *error = NULL;
    SecretService *service = secret_service_get_sync (SECRET_SERVICE_NONE, NULL, &error);
    if (service)
    {
        g_object_unref (service);
        storage_available = TRUE;
    }
    else
    {
        if (error)
        {
            g_warning ("Secure storage unavailable: %s", error->message);
            g_error_free (error);
        }
        storage_available = FALSE;
    }
#else
    /* No secure storage backend compiled in */
    storage_available = FALSE;
#endif

    if (!storage_available)
        g_message ("Secure storage not available - OAuth tokens will only be stored in memory");

    return storage_available;
}

void
secure_storage_shutdown (void)
{
    storage_initialized = FALSE;
    storage_available = FALSE;
}

gboolean
secure_storage_available (void)
{
    return storage_available;
}

#ifdef _WIN32
/* Build a Windows credential target name */
static wchar_t *
build_target_name (const char *network_name, const char *key)
{
    char *target_utf8 = g_strdup_printf ("%s%s:%s", CRED_PREFIX, network_name, key);
    wchar_t *target_wide = g_utf8_to_utf16 (target_utf8, -1, NULL, NULL, NULL);
    g_free (target_utf8);
    return target_wide;
}
#endif

gboolean
secure_storage_store (const char *network_name,
                      const char *key,
                      const char *value)
{
    if (!network_name || !key || !value)
        return FALSE;

    if (!storage_available)
        return FALSE;

#ifdef _WIN32
    wchar_t *target = build_target_name (network_name, key);
    if (!target)
        return FALSE;

    CREDENTIALW cred = { 0 };
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target;
    cred.CredentialBlobSize = (DWORD)strlen (value);
    cred.CredentialBlob = (LPBYTE)value;
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    /* Set a user-friendly comment */
    char *comment_utf8 = g_strdup_printf ("HexChat OAuth token for %s", network_name);
    wchar_t *comment_wide = g_utf8_to_utf16 (comment_utf8, -1, NULL, NULL, NULL);
    g_free (comment_utf8);
    cred.Comment = comment_wide;

    BOOL result = CredWriteW (&cred, 0);

    g_free (target);
    g_free (comment_wide);

    if (!result)
    {
        DWORD err = GetLastError ();
        g_warning ("Failed to store credential: error %lu", err);
        return FALSE;
    }

    return TRUE;

#elif defined(USE_LIBSECRET)
    GError *error = NULL;
    gboolean result;
    char *label = g_strdup_printf ("HexChat %s for %s", key, network_name);

    result = secret_password_store_sync (&hexchat_schema,
                                         SECRET_COLLECTION_DEFAULT,
                                         label,
                                         value,
                                         NULL,
                                         &error,
                                         "network", network_name,
                                         "key", key,
                                         NULL);
    g_free (label);

    if (error)
    {
        g_warning ("Failed to store secret: %s", error->message);
        g_error_free (error);
        return FALSE;
    }

    return result;
#else
    return FALSE;
#endif
}

char *
secure_storage_retrieve (const char *network_name,
                         const char *key)
{
    if (!network_name || !key)
        return NULL;

    if (!storage_available)
        return NULL;

#ifdef _WIN32
    wchar_t *target = build_target_name (network_name, key);
    if (!target)
        return NULL;

    PCREDENTIALW cred = NULL;
    BOOL result = CredReadW (target, CRED_TYPE_GENERIC, 0, &cred);
    g_free (target);

    if (!result || !cred)
    {
        /* Credential not found is not an error, just return NULL */
        return NULL;
    }

    /* Copy the credential blob as a null-terminated string */
    char *value = NULL;
    if (cred->CredentialBlobSize > 0)
    {
        value = g_malloc (cred->CredentialBlobSize + 1);
        memcpy (value, cred->CredentialBlob, cred->CredentialBlobSize);
        value[cred->CredentialBlobSize] = '\0';
    }

    CredFree (cred);
    return value;

#elif defined(USE_LIBSECRET)
    GError *error = NULL;
    char *value;

    value = secret_password_lookup_sync (&hexchat_schema,
                                         NULL,
                                         &error,
                                         "network", network_name,
                                         "key", key,
                                         NULL);
    if (error)
    {
        g_warning ("Failed to retrieve secret: %s", error->message);
        g_error_free (error);
        return NULL;
    }

    /* libsecret returns a copy that must be freed with secret_password_free,
     * but we return g_malloc'd memory, so copy and free */
    if (value)
    {
        char *copy = g_strdup (value);
        secret_password_free (value);
        return copy;
    }

    return NULL;
#else
    return NULL;
#endif
}

gboolean
secure_storage_delete (const char *network_name,
                       const char *key)
{
    if (!network_name || !key)
        return FALSE;

    if (!storage_available)
        return TRUE; /* Nothing to delete */

#ifdef _WIN32
    wchar_t *target = build_target_name (network_name, key);
    if (!target)
        return FALSE;

    BOOL result = CredDeleteW (target, CRED_TYPE_GENERIC, 0);
    g_free (target);

    if (!result)
    {
        DWORD err = GetLastError ();
        if (err == ERROR_NOT_FOUND)
            return TRUE; /* Already deleted */
        g_warning ("Failed to delete credential: error %lu", err);
        return FALSE;
    }

    return TRUE;

#elif defined(USE_LIBSECRET)
    GError *error = NULL;
    gboolean result;

    result = secret_password_clear_sync (&hexchat_schema,
                                         NULL,
                                         &error,
                                         "network", network_name,
                                         "key", key,
                                         NULL);
    if (error)
    {
        g_warning ("Failed to delete secret: %s", error->message);
        g_error_free (error);
        return FALSE;
    }

    return TRUE; /* result is FALSE if nothing was deleted, which is fine */
#else
    return TRUE;
#endif
}

gboolean
secure_storage_delete_all (const char *network_name)
{
    if (!network_name)
        return FALSE;

    /* Delete all known OAuth keys for this network */
    secure_storage_delete (network_name, "oauth_access_token");
    secure_storage_delete (network_name, "oauth_refresh_token");
    secure_storage_delete (network_name, "oauth_expires_at");

    return TRUE;
}

/* OAuth token convenience functions */

gboolean
secure_storage_store_oauth_tokens (const char *network_name,
                                   const char *access_token,
                                   const char *refresh_token,
                                   gint64 expires_at)
{
    gboolean success = TRUE;

    if (access_token)
    {
        if (!secure_storage_store (network_name, "oauth_access_token", access_token))
            success = FALSE;
    }

    if (refresh_token)
    {
        if (!secure_storage_store (network_name, "oauth_refresh_token", refresh_token))
            success = FALSE;
    }

    if (expires_at > 0)
    {
        char *expires_str = g_strdup_printf ("%" G_GINT64_FORMAT, expires_at);
        if (!secure_storage_store (network_name, "oauth_expires_at", expires_str))
            success = FALSE;
        g_free (expires_str);
    }

    return success;
}

gboolean
secure_storage_retrieve_oauth_tokens (const char *network_name,
                                      char **access_token,
                                      char **refresh_token,
                                      gint64 *expires_at)
{
    char *token = secure_storage_retrieve (network_name, "oauth_access_token");

    if (!token)
        return FALSE;

    if (access_token)
        *access_token = token;
    else
        g_free (token);

    if (refresh_token)
        *refresh_token = secure_storage_retrieve (network_name, "oauth_refresh_token");

    if (expires_at)
    {
        char *expires_str = secure_storage_retrieve (network_name, "oauth_expires_at");
        if (expires_str)
        {
            *expires_at = g_ascii_strtoll (expires_str, NULL, 10);
            g_free (expires_str);
        }
        else
        {
            *expires_at = 0;
        }
    }

    return TRUE;
}

gboolean
secure_storage_clear_oauth_tokens (const char *network_name)
{
    return secure_storage_delete_all (network_name);
}
