# Electron Frontend Implementation Plan for HexChat

## Overview

Create an Electron-based frontend for HexChat that communicates with the C backend via WebSocket, supporting web technologies, extensive theming, and JavaScript plugins.

**Integration Method**: WebSocket Server (backend exposes WS, Electron connects as client)
**Platforms**: Windows, Linux, macOS from the start
**Relationship**: Complementary to GTK - focus on web technologies, theming, JS plugins
**UI Design**: Flexible/themeable - support multiple layouts, heavy user customization

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Electron Application                         │
│  ┌─────────────┐    ┌──────────────────────────────────────────┐│
│  │ Main Process│    │           Renderer (React)               ││
│  │             │    │  ┌─────────────────────────────────────┐ ││
│  │ - Spawn     │    │  │ WebSocket Client                    │ ││
│  │   backend   │◄───┼──┤ - State management (Zustand)        │ ││
│  │ - Window    │    │  │ - IRC formatting renderer           │ ││
│  │   manager   │    │  │ - Theming (CSS Variables)           │ ││
│  │             │    │  │ - JS Plugin system                  │ ││
│  └─────────────┘    │  └──────────────────┬──────────────────┘ ││
└─────────────────────┼────────────────────┼─────────────────────┘
                      │                    │ WebSocket
                      │                    ▼
┌─────────────────────┼────────────────────────────────────────────┐
│                     │  hexchat-electron (C Backend)              │
│  ┌──────────────────▼───────────────────────────────────────────┐│
│  │ WebSocket Server (libwebsockets)                             ││
│  │  - JSON protocol (Jansson)                                   ││
│  │  - Session state tracking                                    ││
│  └──────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ fe_* Interface Implementation                                ││
│  │  - fe_print_text → broadcast text.print event                ││
│  │  - fe_userlist_* → broadcast userlist.* events               ││
│  │  - fe_input_add/timeout_add → GLib event loop                ││
│  └──────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────┐│
│  │ HexChat Common Library (existing IRC engine)                 ││
│  └──────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
hexchat/
├── src/
│   ├── common/                    # Existing - unchanged
│   ├── fe-gtk/                    # Existing - unchanged
│   ├── fe-text/                   # Existing - unchanged
│   └── fe-electron/               # NEW - C backend
│       ├── meson.build
│       ├── fe-electron.c          # fe_* function implementations
│       ├── fe-electron.h
│       ├── ws-server.c            # libwebsockets server
│       ├── ws-server.h
│       ├── json-protocol.c        # JSON serialization (Jansson)
│       ├── json-protocol.h
│       ├── session-state.c        # State tracking for sync
│       └── session-state.h
└── electron/                      # NEW - Electron app
    ├── package.json
    ├── electron-builder.json
    ├── vite.config.ts
    ├── src/
    │   ├── main/                  # Electron main process
    │   │   ├── index.ts           # Entry point
    │   │   ├── backend-manager.ts # Spawn/manage hexchat-electron
    │   │   └── window-manager.ts
    │   ├── preload/
    │   │   └── index.ts
    │   └── renderer/              # React frontend
    │       ├── main.tsx
    │       ├── App.tsx
    │       ├── store/             # Zustand state management
    │       ├── components/        # React components
    │       ├── styles/themes/     # CSS theme files
    │       └── plugins/           # JS plugin system
    └── resources/
```

---

## WebSocket Protocol

### Message Envelope
```typescript
interface WSMessage {
  type: string;        // Event type identifier
  id?: string;         // Request/response correlation ID
  timestamp: number;   // Unix timestamp (ms)
  payload: object;     // Type-specific data
}
```

### Key Server → Client Events

| Event | Purpose |
|-------|---------|
| `state.sync` | Full state sync on connection |
| `session.created` | New channel/dialog window |
| `session.closed` | Window closed |
| `text.print` | Display message (main event flow) |
| `userlist.insert/remove/update/clear` | User list changes |
| `channel.topic` | Topic changed |
| `channel.modes` | Mode buttons update |
| `server.event` | Connect/disconnect/etc. |
| `server.state` | Nick, lag, away status |
| `ui.tabcolor` | Tab activity color |
| `dialog.request` | Backend needs user input |

### Key Client → Server Commands

| Command | Purpose |
|---------|---------|
| `command.execute` | Run IRC command (e.g., "MSG #chan hello") |
| `input.text` | Direct text input |
| `session.focus` | Set active session |
| `session.close` | Close session |
| `preference.set` | Update preference |
| `dialog.response` | Answer dialog request |

---

## Technology Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| WS Library (C) | libwebsockets | Cross-platform, lightweight |
| JSON Library (C) | Jansson | Pure C, simple API |
| Frontend | React 18 | Component model, ecosystem |
| State | Zustand | Minimal boilerplate |
| Build | Vite | Fast HMR, ESM-native |
| Virtualization | @tanstack/virtual | Large message lists |
| Styling | CSS Variables | Native theming |

---

## Key Files to Create/Modify

### New Files

| File | Purpose |
|------|---------|
| `src/fe-electron/fe-electron.c` | Main frontend, implements ~90 fe_* functions |
| `src/fe-electron/ws-server.c` | WebSocket server using libwebsockets |
| `src/fe-electron/json-protocol.c` | JSON serialization with Jansson |
| `src/fe-electron/session-state.c` | Track state for late-joining clients |
| `src/fe-electron/meson.build` | Build configuration |
| `electron/` | Complete Electron application |

### Modify

| File | Change |
|------|--------|
| `meson_options.txt` | Add `electron-frontend` option |
| `src/meson.build` | Conditional subdir for fe-electron |

---

## Reference Files

| File | Purpose |
|------|---------|
| `src/common/fe.h` | Frontend interface (~90 functions) |
| `src/fe-text/fe-text.c` | Minimal implementation reference |
| `src/common/hexchat.h` | Core data structures |
| `src/common/textevents.in` | Text event definitions |

---

## Implementation Phases

### Phase 1: Minimal Viable Connection
- Create fe-electron directory structure
- Implement minimal fe_* functions (based on fe-text)
- Integrate libwebsockets server
- Basic Electron app with WebSocket client
- Display messages in single window
- **Deliverable**: Connect to server, see messages

### Phase 2: Core IRC Experience
- Complete userlist events
- Topic/mode events
- Server tree with networks/channels
- IRC formatting renderer (colors, bold)
- Input box with nick completion
- Tab system with activity colors
- **Deliverable**: Fully functional multi-channel client

### Phase 3: Theming System
- CSS variable-based themes
- Built-in themes (light, dark, classic)
- Custom theme loader
- Layout presets and customization
- Font settings
- **Deliverable**: Highly customizable appearance

### Phase 4: JavaScript Plugin System
- Plugin API (mirrors C plugin API)
- Plugin loader with sandboxing
- Hook system (command, print, server, timer)
- Plugin manager UI
- Example plugins
- **Deliverable**: Working plugin ecosystem

### Phase 5: Advanced Features
- DCC file transfer UI
- Channel list dialog
- Server list editor
- Ignore list management
- System notifications
- **Deliverable**: Near-complete IRC client

### Phase 6: Polish and Release
- Cross-platform testing
- Performance optimization
- Installers for all platforms
- Auto-update system
- Documentation
- **Deliverable**: v1.0 release

---

## Build System Integration

### meson_options.txt
```meson
option('electron-frontend', type: 'boolean', value: false,
       description: 'Build WebSocket backend for Electron frontend')
```

### src/fe-electron/meson.build
```meson
libwebsockets_dep = dependency('libwebsockets', version: '>= 4.0')
jansson_dep = dependency('jansson', version: '>= 2.10')

hexchat_electron_sources = [
  'fe-electron.c',
  'ws-server.c',
  'json-protocol.c',
  'session-state.c',
]

executable('hexchat-electron',
  sources: hexchat_electron_sources,
  dependencies: [hexchat_common_dep, libwebsockets_dep, jansson_dep],
  install: true,
)
```

---

## Cross-Platform Notes

| Concern | Windows | Linux | macOS |
|---------|---------|-------|-------|
| Binary | hexchat-electron.exe | hexchat-electron | hexchat-electron |
| Config | %APPDATA%\HexChat-Electron | ~/.config/hexchat-electron | ~/Library/Application Support/HexChat-Electron |
| Dependencies | vcpkg/bundled | System packages | Homebrew/bundled |

---

## Key Implementation Notes

### fe_* Functions (C Backend)

**Critical** (must implement properly):
- `fe_args`, `fe_init`, `fe_main`, `fe_exit`, `fe_cleanup`
- `fe_new_window`, `fe_close_window`, `fe_new_server`
- `fe_print_text` - Primary message flow
- `fe_input_add`, `fe_timeout_add` (and remove variants)
- `fe_userlist_*` (6 functions)
- `fe_set_topic`, `fe_set_title`, `fe_set_channel`

**Important** (should emit events):
- `fe_set_tab_color`, `fe_flash_window`
- `fe_update_mode_buttons`
- `fe_set_lag`, `fe_set_throttle`, `fe_set_away`
- `fe_message`

**Stubs OK initially**:
- DCC GUI functions
- Menu functions
- Channel list, ban list functions

### IRC Formatting (Renderer)

Parse control codes and render as styled spans:
- `\x02` - Bold
- `\x03NN,MM` - Foreground/background color
- `\x1D` - Italic
- `\x1F` - Underline
- `\x0F` - Reset

### Theming

Use CSS variables for all colors, with theme files that override:
```css
:root {
  --bg-primary: #ffffff;
  --irc-color-0: #ffffff;
  --irc-color-1: #000000;
  /* ... */
  --tab-hilight: #ff00ff;
}
```

### JS Plugin API

Mirror HexChat C plugin API:
```typescript
interface HexChatPluginAPI {
  hookCommand(name: string, cb: CommandCallback): HookHandle;
  hookPrint(event: string, cb: PrintCallback): HookHandle;
  command(cmd: string): void;
  getInfo(id: string): string | null;
  // ...
}
```
