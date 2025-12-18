# System Tray Integration Plan: dmikushin/tray Library

## Overview
Restore system tray functionality to HexChat GTK4 by integrating the [dmikushin/tray](https://github.com/dmikushin/tray) library. This replaces the deprecated GtkStatusIcon that was removed in GTK4.

**Current Status:** ✅ Windows implementation complete
**Integration Method:** Git submodule
**Icon Format:** Native .ico files for Windows

> **Note:** Linux and macOS support are required for this migration to be considered complete. Linux and macOS implementations will be added in future phases.

---

## Implementation Status

### ✅ Phase 1: Add Library as Git Submodule - COMPLETE

- Added `external/tray` submodule pointing to https://github.com/dmikushin/tray.git
- Created `.gitmodules` entry

### ✅ Phase 2: Create Windows .ico Icon Files - COMPLETE

Created icon files in `data/icons/`:
- `tray_normal.ico` - Normal state (green hexagon)
- `tray_message.ico` - Message notification (blue)
- `tray_highlight.ico` - Highlight notification (yellow/orange)
- `tray_fileoffer.ico` - DCC file offer (cyan/teal)

Icon specifications:
- Multi-resolution: 16x16 and 32x32 pixels
- 32-bit RGBA with transparency
- Generated from existing PNG files using Python Pillow

### ✅ Phase 3: Update Build System - COMPLETE

#### Visual Studio Project (`src/fe-gtk/fe-gtk.vcxproj`)
- Added `TRAY_EXPORTS` to preprocessor definitions
- Added `..\..\external\tray` to include directories
- Added `tray_windows.c` to ClCompile items
- Added `tray.h` to ClInclude items

#### Meson Build (`src/fe-gtk/meson.build`)
- Added conditional Windows tray source compilation
- Added tray include directory for Windows builds

#### Copy Project (`win32/copy/copy.vcxproj`)
- Added TrayIcons item group for ICO files
- Added copy command to deploy icons to `share\icons`
- Removed deprecated GTK3 conditional logic

#### GitHub Workflows
- Updated all workflows to use `actions/checkout@v4` with `submodules: recursive`
- Affected: `windows-build.yml`, `msys-build.yml`, `ubuntu-build.yml`, `flatpak-build.yml`

#### Installer (`win32/installer/hexchat.iss.tt`)
- Added tray icon files to installer

### ✅ Phase 4: Implement Tray Functionality - COMPLETE

Rewrote `src/fe-gtk/plugin-tray.c` with full Windows implementation:

#### Features Implemented
- Tray icon initialization and cleanup
- Left-click to toggle window visibility
- Right-click context menu (Restore/Hide, Away, Back, Preferences, Quit)
- Icon flashing on notifications (private messages, highlights, DCC offers)
- Flash stops when window is focused
- Message count tracking for tooltip
- Window state preservation (position, maximized state)
- Away/Back command execution on hide/show

#### Event Hooks Registered
- Channel Msg Hilight, Channel Action Hilight
- Private Message, Private Message to Dialog
- Private Action, Private Action to Dialog
- Channel Message, Channel Action (when `hex_input_tray_chans` enabled)
- Invited
- DCC Offer
- Focus Window

### ✅ Phase 5: Minimize to Tray - COMPLETE

Added GTK4-compatible minimize-to-tray in `src/fe-gtk/maingui.c`:

- Added `mg_surface_state_cb()` to monitor GdkToplevel surface state
- Added `mg_realize_cb()` to connect surface state signal after window realization
- Connected `realize` signal on main window
- When minimized: intercepts `GDK_TOPLEVEL_STATE_MINIMIZED`, unminimizes window, then hides to tray

---

## Testing Checklist

- [x] Tray icon appears on startup (when `hex_gui_tray` enabled)
- [x] Left-click toggles window visibility
- [x] Right-click shows context menu
- [x] Icon flashes on private message (when `hex_input_tray_priv` enabled)
- [x] Icon flashes on highlight (when `hex_input_tray_hilight` enabled)
- [x] Icon flashes on DCC offer
- [x] Flashing stops when window focused
- [x] "Close to tray" works (when `hex_gui_tray_close` enabled)
- [x] "Minimize to tray" works (when `hex_gui_tray_minimize` enabled)
- [x] Away/Back commands execute (when `hex_gui_tray_away` enabled)
- [x] Window position/state restored correctly
- [ ] Tooltip updates with message counts (needs verification)
- [x] Menu items work: Restore, Away, Back, Preferences, Quit
- [x] Tray icon removed on exit

---

## Files Modified/Created

| File | Action |
|------|--------|
| `.gitmodules` | Created - tray submodule |
| `external/tray/` | Added - git submodule |
| `data/icons/tray_*.ico` | Created - 4 Windows icon files |
| `src/fe-gtk/plugin-tray.c` | Rewritten - full Windows implementation |
| `src/fe-gtk/plugin-tray.h` | Modified - added function declarations |
| `src/fe-gtk/maingui.c` | Modified - minimize-to-tray support |
| `src/fe-gtk/fe-gtk.vcxproj` | Modified - tray sources, TRAY_EXPORTS |
| `src/fe-gtk/meson.build` | Modified - conditional Windows tray |
| `win32/copy/copy.vcxproj` | Modified - icon copying, removed GTK3 logic |
| `win32/hexchat.props` | Modified - removed HC_GTK4 flag |
| `win32/installer/hexchat.iss.tt` | Modified - tray icons in installer |
| `.github/workflows/*.yml` | Modified - submodule checkout |

---

## Future Work: Linux and macOS Support

### Linux Implementation (Future Phase)
The dmikushin/tray library uses Qt6 on Linux, which introduces a C++ dependency that may conflict with GTK4. Options to consider:

1. **Use dmikushin/tray Qt6 implementation** - Requires linking Qt6 alongside GTK4
2. **Use libayatana-appindicator** - GTK-native approach, better integration
3. **Direct DBus StatusNotifierItem** - No additional dependencies but more complex

### macOS Implementation (Future Phase)
The dmikushin/tray library uses Cocoa/AppKit via Objective-C (`tray_darwin.m`). This requires:

1. Adding Objective-C compilation support to the build system
2. Linking Cocoa framework
3. Converting PNG icons to proper macOS format (~22pt height or vector PDF)

### Implementation Order
1. ~~Windows support~~ ✅ COMPLETE
2. ~~Verify Windows implementation works correctly~~ ✅ COMPLETE
3. Linux support (evaluate Qt6 vs libayatana-appindicator)
4. macOS support (Objective-C integration)

---

## Technical Notes

### GTK4 Minimize Detection
GTK4 doesn't expose a `notify::minimized` property on GtkWindow. To detect window minimization:
1. Connect to the window's `realize` signal
2. In the realize handler, get the GdkSurface via `gtk_native_get_surface()`
3. Connect to the GdkToplevel's `notify::state` signal
4. Check for `GDK_TOPLEVEL_STATE_MINIMIZED` in the state callback

### dmikushin/tray Library Integration
- Uses `Shell_NotifyIconW` on Windows
- Requires `TRAY_EXPORTS` preprocessor define for proper DLL linkage
- Icon paths must be absolute paths to .ico files
- Menu items use `struct tray_menu_item` with text, disabled flag, checked flag, and callback

### Reference: Original GTK2 Implementation
Key patterns from commit `b544ac3350e85d4cc41fe3414cbdb82d75ce5d7a`:
- Used `GtkStatusIcon` with signals: `popup-menu`, `activate`, `notify::embedded`
- Flash timeout: 500ms
- Message count tracking for tooltip: `tray_priv_count`, `tray_pub_count`, etc.
- Window status via `hexchat_get_info(ph, "win_status")` returning "active", "normal", "hidden"
- Away status check via `tray_find_away_status()` iterating `serv_list`
