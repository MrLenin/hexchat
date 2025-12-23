# OAuth2/OIDC Implementation Plan for HexChat GTK Frontend

## Overview

Add OAuth2/OIDC support to HexChat using the WebSocket server's HTTP callback for OAuth redirects, implementing the SASL OAUTHBEARER mechanism per RFC 7628.

## Key Specifications
- **RFC 7628**: OAUTHBEARER SASL mechanism format
- **IRCv3 SASL 3.1**: AUTHENTICATE command, 400-byte chunking, numerics

---

## Files to Create

### 1. src/common/oauth.h
OAuth2 data structures and API declarations:
- `oauth_token` struct (access_token, refresh_token, expires_at)
- `oauth_provider_config` struct (authorization_url, token_url, client_id, client_secret, scopes)
- `oauth_session` struct (state machine, PKCE values, callback server, completion callback)
- Flow control functions: `oauth_begin_authorization()`, `oauth_cancel()`, `oauth_refresh_token()`
- PKCE helpers: `oauth_generate_code_verifier()`, `oauth_generate_code_challenge()`
- OAUTHBEARER encoding: `encode_sasl_oauthbearer()`

### 2. src/common/oauth.c
Core OAuth2 implementation:
- Start temporary HcServer for OAuth callback
- Build authorization URL with PKCE (code_challenge_method=S256)
- Open browser via `fe_open_url()`
- HTTP callback handler to receive authorization code
- Token exchange using **libsoup3** (async HTTP POST to token endpoint)
- Token refresh logic
- OAUTHBEARER encoding per RFC 7628: `n,,\x01auth=Bearer <token>\x01\x01`
- **Generic provider only** - user provides all OAuth URLs (no presets)

---

## Files to Modify

### 3. src/common/hexchat.h
Add after line 436:
```c
#define MECH_OAUTHBEARER 5
```

Add to `server` struct (around line 592):
```c
struct oauth_session *oauth_session;
char *oauth_access_token;
time_t oauth_token_expires;
```

### 4. src/common/servlist.h
Add after line 84:
```c
#define LOGIN_SASL_OAUTHBEARER 14
```

Add to `ircnet` struct (after line 54):
```c
char *oauth_provider;
char *oauth_authorization_url;
char *oauth_token_url;
char *oauth_client_id;
char *oauth_client_secret;
char *oauth_scopes;
```

### 5. src/common/servlist.c
**servlist_load()** (~line 958): Add cases for OAuth fields (O=, a=, t=, c=, s=, o=)
**servlist_save()** (~line 1100): Write OAuth fields to servlist.conf
**servlist_net_add()**: Initialize OAuth fields to NULL
**servlist_cleanup()**: Free OAuth strings

### 6. src/common/inbound.c
**sasl_mechanisms array** (~line 1650): Add `"OAUTHBEARER"`

**inbound_toggle_caps()** (~line 1687): Add case for `LOGIN_SASL_OAUTHBEARER` to set `serv->sasl_mech = MECH_OAUTHBEARER`

**inbound_sasl_authenticate()** (~line 2034): Add case for `MECH_OAUTHBEARER`:
```c
case MECH_OAUTHBEARER:
    if (!serv->oauth_access_token) {
        /* Abort - no token available */
        tcp_sendf(serv, "AUTHENTICATE *\r\n");
        return;
    }
    char *encoded = encode_sasl_oauthbearer(serv->oauth_access_token,
                                            serv->servername, serv->port);
    /* Handle 400-byte chunking per IRCv3 SASL 3.1 */
    sasl_send_with_chunking(serv, encoded);
    g_free(encoded);
    break;
```

### 7. src/fe-gtk/servlistgui.c
**login_types_conf array** (~line 129): Add `LOGIN_SASL_OAUTHBEARER`
**login_types array** (~line 153): Add `"SASL OAUTHBEARER (OAuth2/OIDC)"`

**Network editor UI** (in servlist_open_edit ~line 1730):
- Add OAuth configuration widgets (shown when OAUTHBEARER selected):
  - Client ID entry (required)
  - Client Secret entry (optional, masked)
  - Scopes entry (default: "openid")
  - Authorization URL entry
  - Token URL entry
  - "Authorize" button
  - Status label showing token state

**Callbacks**:
- Show/hide OAuth section based on login method selection
- "Authorize" button triggers `oauth_begin_authorization()`
- Update status on OAuth completion

### 8. src/common/meson.build
Add oauth.c to common_sources (conditional on libwebsockets) and add libsoup3 dependency:
```meson
libsoup_dep = dependency('libsoup-3.0', required: get_option('with-libwebsockets'))

if libwebsockets_dep.found()
  common_sources += ['ws-server.c', 'oauth.c']
  common_deps += [libwebsockets_dep, libsoup_dep]
endif
```

### 9. src/common/common.vcxproj
Add oauth.c and oauth.h to the project

---

## OAuth Flow Sequence

### Authorization Flow (triggered by "Authorize" button)
1. Generate PKCE code_verifier (random 43-128 chars) and code_challenge (SHA256 hash)
2. Generate random state parameter for CSRF protection
3. Start temporary HcServer on random localhost port with HTTP callback
4. Build authorization URL with: client_id, redirect_uri, scope, state, code_challenge
5. Call `fe_open_url()` to open browser
6. User authenticates with OAuth provider
7. Provider redirects to `http://localhost:PORT/callback?code=...&state=...`
8. HTTP callback validates state, extracts authorization code
9. POST to token_url with: code, redirect_uri, client_id, client_secret, code_verifier
10. Parse response: access_token, refresh_token, expires_in
11. Store token in ircnet struct, update UI status
12. Stop temporary HcServer

### Connection Flow
1. User connects to network with LOGIN_SASL_OAUTHBEARER
2. CAP negotiation requests sasl capability
3. `inbound_toggle_caps()` sets `sasl_mech = MECH_OAUTHBEARER`
4. Check token validity:
   - If missing: abort with message "Please authorize first"
   - If expired: attempt refresh with refresh_token
5. Build OAUTHBEARER response per RFC 7628
6. Send via AUTHENTICATE with 400-byte chunking
7. Handle 903 (success) or 904 (failure)

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
| Token expired | Attempt refresh, if fails prompt re-auth |
| 904 SASLFAIL | Check token, prompt re-authorization |
| 905 SASLTOOLONG | Bug in chunking logic |
| OAuth callback missing code | Show error, clean up server |
| Token exchange HTTP error | Show error, allow retry |
| Invalid state parameter | Reject (CSRF attempt), show error |

---

## Implementation Order

1. **oauth.h/oauth.c** - Core structures and OAUTHBEARER encoding
2. **hexchat.h, servlist.h** - Add constants and struct fields
3. **servlist.c** - Config persistence
4. **inbound.c** - SASL OAUTHBEARER mechanism
5. **oauth.c** - HTTP callback handler and token exchange
6. **servlistgui.c** - GTK UI for OAuth configuration
7. **meson.build, vcxproj** - Build system updates
8. Testing with a real OAuth2 provider

---

## Security Considerations

- Always use PKCE (even for confidential clients)
- Validate state parameter to prevent CSRF
- Use HTTPS for token endpoint (warn on HTTP)
- Consider using GLib GSecret API for token storage (future enhancement)
- Clear sensitive data from memory after use
- Never log tokens or secrets
