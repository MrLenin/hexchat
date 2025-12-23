# OAuth2/OIDC Implementation Plan for HexChat GTK Frontend

## Overview

Add OAuth2/OIDC support to HexChat using the WebSocket server's HTTP callback for OAuth redirects, implementing the SASL OAUTHBEARER mechanism per RFC 7628.

## Key Specifications
- **RFC 7628**: OAUTHBEARER SASL mechanism format
- **IRCv3 SASL 3.1**: AUTHENTICATE command, 400-byte chunking, numerics

---

## Implementation Status

### Completed

#### Files Created
- [x] **src/common/oauth.h** - OAuth2 data structures and API declarations
- [x] **src/common/oauth.c** - Core OAuth2 implementation with PKCE, token exchange
- [x] **src/common/secure-storage.h** - Cross-platform secure storage API
- [x] **src/common/secure-storage.c** - Platform-specific secure storage backends

#### Files Modified
- [x] **src/common/servlist.h** - Added OAuth fields to `ircnet` struct, `LOGIN_SASL_OAUTHBEARER` constant
- [x] **src/common/servlist.c** - OAuth field persistence in servlist.conf, token loading on connect
- [x] **src/common/hexchat.h** - Added `MECH_OAUTHBEARER` constant and OAuth fields to `server` struct
- [x] **src/common/inbound.c** - Full SASL OAUTHBEARER mechanism with 400-byte chunking
- [x] **src/fe-gtk/servlistgui.c** - OAuth configuration UI with Authorize button
- [x] **src/common/meson.build** - Added oauth.c, secure-storage.c, libsoup3, libsecret dependencies
- [x] **src/common/common.vcxproj** - Added oauth.c, oauth.h, secure-storage.c, secure-storage.h
- [x] **win32/hexchat.props** - Added USE_LIBCURL flag and libcurl paths

#### Features Implemented
- [x] OAuth2 Authorization Code flow with PKCE (RFC 7636)
- [x] OAUTHBEARER SASL encoding (RFC 7628)
- [x] HTTP callback server for OAuth redirect
- [x] Token exchange via libcurl (Windows) or libsoup3 (Linux)
- [x] GTK4 OAuth configuration UI in network editor
- [x] Secure token storage:
  - Windows: Credential Manager (CredWriteW/CredReadW)
  - Linux: libsecret (GNOME Keyring/KDE Wallet)
  - Portable mode: Memory-only (config file fallback)
- [x] Dialog focus fixes for OAuth error messages
- [x] SASL OAUTHBEARER mechanism in inbound.c
- [x] 400-byte chunking for AUTHENTICATE command
- [x] Token loading from secure storage on connect
- [x] Token expiration checking before authentication

### Pending

#### Testing
- [ ] Testing with a real OAuth2 provider

---

## Files Created

### 1. src/common/oauth.h - DONE
OAuth2 data structures and API declarations:
- `oauth_token` struct (access_token, refresh_token, expires_at)
- `oauth_provider_config` struct (authorization_url, token_url, client_id, client_secret, scopes)
- `oauth_session` struct (state machine, PKCE values, callback server, completion callback)
- Flow control functions: `oauth_begin_authorization()`, `oauth_cancel()`, `oauth_refresh_token()`
- PKCE helpers: `oauth_generate_code_verifier()`, `oauth_generate_code_challenge()`
- OAUTHBEARER encoding: `oauth_encode_sasl_oauthbearer()`
- Secure storage: `oauth_save_tokens()`, `oauth_load_tokens()`, `oauth_clear_tokens()`

### 2. src/common/oauth.c - DONE
Core OAuth2 implementation:
- Start temporary HcServer for OAuth callback
- Build authorization URL with PKCE (code_challenge_method=S256)
- Open browser via `fe_open_url()`
- HTTP callback handler to receive authorization code
- Token exchange using **libcurl** (Windows) or **libsoup3** (Linux)
- Token refresh logic (placeholder - needs stored refresh_token)
- OAUTHBEARER encoding per RFC 7628: `n,,\x01auth=Bearer <token>\x01\x01`
- **Generic provider only** - user provides all OAuth URLs (no presets)

### 3. src/common/secure-storage.h - DONE
Cross-platform secure storage API:
- `secure_storage_init()` / `secure_storage_shutdown()`
- `secure_storage_store()` / `secure_storage_retrieve()` / `secure_storage_delete()`
- `secure_storage_store_oauth_tokens()` / `secure_storage_retrieve_oauth_tokens()`

### 4. src/common/secure-storage.c - DONE
Platform-specific implementations:
- Windows: Credential Manager (CredWriteW/CredReadW/CredDeleteW)
- Linux: libsecret with GNOME Keyring/KDE Wallet schema
- Portable mode detection (disables Credential Manager on Windows)
- Fallback to memory-only when secure storage unavailable

---

## Files Modified

### 1. src/common/hexchat.h - DONE
Added after MECH_SCRAM_SHA_512:
```c
#define MECH_OAUTHBEARER 5
```

Added to `server` struct (inside `#ifdef USE_LIBWEBSOCKETS`):
```c
struct oauth_session *oauth_session;
char *oauth_access_token;
time_t oauth_token_expires;
```

### 2. src/common/servlist.h - DONE
Added `LOGIN_SASL_OAUTHBEARER` constant and OAuth fields to `ircnet` struct.

### 3. src/common/servlist.c - DONE
- OAuth field persistence implemented
- Token loading from secure storage on connect

### 4. src/common/inbound.c - DONE
**sasl_mechanisms array**: Added `"OAUTHBEARER"`

**inbound_toggle_caps()**: Added case for `LOGIN_SASL_OAUTHBEARER` to set `serv->sasl_mech = MECH_OAUTHBEARER`

**inbound_sasl_authenticate()**: Added case for `MECH_OAUTHBEARER` with:
- Token availability check
- Token expiration check
- OAUTHBEARER encoding via `oauth_encode_sasl_oauthbearer()`
- 400-byte chunking per IRCv3 SASL 3.1

### 5. src/fe-gtk/servlistgui.c - DONE
- OAuth configuration widgets implemented
- Show/hide OAuth section based on login method
- "Authorize" button triggers `oauth_begin_authorization()`
- Tokens saved to secure storage on completion
- Dialog focus issues fixed

### 6. src/common/meson.build - DONE
```meson
if libwebsockets_dep.found()
  common_sources += ['ws-server.c', 'oauth.c', 'secure-storage.c']
  common_deps += libwebsockets_dep

  libsoup_dep = dependency('libsoup-3.0', required: false)
  jansson_dep = dependency('jansson', version: '>= 2.10', required: false)
  if libsoup_dep.found() and jansson_dep.found()
    common_deps += [libsoup_dep, jansson_dep]
    common_cflags += '-DUSE_LIBSOUP'
  endif

  libsecret_dep = dependency('libsecret-1', required: false)
  if libsecret_dep.found()
    common_deps += libsecret_dep
    common_cflags += '-DUSE_LIBSECRET'
  endif
endif
```

### 7. src/common/common.vcxproj - DONE
Added oauth.c, oauth.h, secure-storage.c, secure-storage.h to the project.

---

## OAuth Flow Sequence

### Authorization Flow (triggered by "Authorize" button) - IMPLEMENTED
1. Generate PKCE code_verifier (random 43-128 chars) and code_challenge (SHA256 hash)
2. Generate random state parameter for CSRF protection
3. Start temporary HcServer on random localhost port with HTTP callback
4. Build authorization URL with: client_id, redirect_uri, scope, state, code_challenge
5. Call `fe_open_url()` to open browser
6. User authenticates with OAuth provider
7. Provider redirects to `http://localhost:PORT/oauth/callback?code=...&state=...`
8. HTTP callback validates state, extracts authorization code
9. POST to token_url with: code, redirect_uri, client_id, client_secret, code_verifier
10. Parse response: access_token, refresh_token, expires_in
11. Store token in secure storage + ircnet struct, update UI status
12. Stop temporary HcServer

### Connection Flow - IMPLEMENTED
1. User connects to network with LOGIN_SASL_OAUTHBEARER
2. Token loaded from secure storage (with fallback to config file)
3. CAP negotiation requests sasl capability
4. `inbound_toggle_caps()` sets `sasl_mech = MECH_OAUTHBEARER`
5. Check token validity:
   - If missing: abort with message "Please authorize first"
   - If expired: abort with message "Token has expired. Please re-authorize"
6. Build OAUTHBEARER response per RFC 7628
7. Send via AUTHENTICATE with 400-byte chunking
8. Handle 903 (success) or 904 (failure)

---

## OAUTHBEARER Format (RFC 7628)

```
n,,\x01auth=Bearer <access_token>\x01host=<hostname>\x01port=<port>\x01\x01
```
- `n,,` = GS2 header (no channel binding, no authzid)
- Fields separated by 0x01 (Ctrl-A)
- Entire string base64 encoded
- Chunked to 400 bytes for AUTHENTICATE command

---

## Error Handling

| Error | Action |
|-------|--------|
| No token on connect | Abort SASL, show "Please authorize first" |
| Token expired | Abort SASL, show "Token has expired. Please re-authorize" |
| 904 SASLFAIL | Check token, prompt re-authorization |
| 905 SASLTOOLONG | Bug in chunking logic |
| OAuth callback missing code | Show error, clean up server |
| Token exchange HTTP error | Show error, allow retry |
| Invalid state parameter | Reject (CSRF attempt), show error |

---

## Implementation Order

1. DONE - **oauth.h/oauth.c** - Core structures and OAUTHBEARER encoding
2. DONE - **secure-storage.h/secure-storage.c** - Platform keychain integration
3. DONE - **servlist.h** - Add constants and struct fields
4. DONE - **servlist.c** - Config persistence + token loading on connect
5. DONE - **hexchat.h** - Add MECH_OAUTHBEARER constant and server struct fields
6. DONE - **inbound.c** - SASL OAUTHBEARER mechanism with 400-byte chunking
7. DONE - **servlistgui.c** - GTK UI for OAuth configuration
8. DONE - **meson.build, vcxproj** - Build system updates
9. TODO - Testing with a real OAuth2 provider

---

## Security Considerations

- DONE - Always use PKCE (even for confidential clients)
- DONE - Validate state parameter to prevent CSRF
- TODO - Use HTTPS for token endpoint (warn on HTTP) - not yet implemented
- DONE - Secure token storage via platform keychain
- DONE - Clear sensitive data from memory after use
- DONE - Never log tokens or secrets

---

## Future Enhancements

### High Priority
1. **Token refresh implementation** - Use stored refresh_token to get new access_token when expired
2. **HTTPS warning** - Warn user if token endpoint doesn't use HTTPS

### Medium Priority
3. **macOS Keychain support** - Add Security framework backend for macOS builds
4. **Token status indicator** - Show in UI whether tokens are stored securely vs. config file
5. **Clear tokens button** - Allow user to remove stored tokens from UI

### Low Priority
6. **OAuth provider presets** - Pre-configured settings for common providers (if any IRC networks adopt standard OAuth)
7. **Token introspection** - Display token expiration time in UI
8. **Multiple account support** - Handle different tokens for different usernames on same network
