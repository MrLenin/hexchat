# HexChat GTK4 Migration Status

## Executive Summary

This document tracks the GTK4 migration status for HexChat. The migration uses conditional compilation (`#if HC_GTK4`) to support both GTK3 and GTK4 builds from a single codebase.

**Build Status (2024-12-15):**
- ✅ **GTK4 Compilation**: SUCCESSFUL (Visual Studio on Windows)
- ⚠️ **GTK4 Runtime**: NOT YET TESTED
- ✅ **GTK3 Compilation**: Working (needs re-verification after recent changes)

**Statistics:**
- 291 `#if HC_GTK4` conditional blocks
- 56 `#if !HC_GTK4` blocks (GTK3-only code)
- 36 files modified
- 1,707 lines in `gtk-compat.h` compatibility layer

---

## Feature Status Overview

### ✅ FULLY IMPLEMENTED (GTK4 feature parity with GTK3)

| Feature | Files | Notes |
|---------|-------|-------|
| Event Controllers | xtext.c, fkeys.c, chanview-*.c, userlistgui.c, etc. | All button/key/scroll/motion events converted |
| Container/Widget APIs | All fe-gtk files | 102+ deprecated calls converted via macros |
| List/Tree Views | 12 files | All migrated to GtkColumnView/GtkListView |
| Static Context Menus | menu.c, banlist.c, chanlist.c | URL, nick, channel, middle-click menus |
| Tab Context Menu | maingui.c | Detach/close actions |
| Spell Check Menu | sexy-spell-entry.c | Word suggestions, add to dictionary |
| DND - Layout Swapping | maingui.c, chanview-tree.c | Drag userlist/chanview to scrollbar |
| DND - File Drops to Users | userlistgui.c | Drag files onto user in list for DCC |
| Clipboard/Selection | xtext.c | Text selection and copy |
| Dialogs | fe-gtk.c, maingui.c | Async response handling (no gtk_dialog_run) |

### ⚠️ PARTIALLY IMPLEMENTED (Reduced functionality in GTK4)

| Feature | GTK3 Behavior | GTK4 Behavior | Impact |
|---------|---------------|---------------|--------|
| DND - File Drops to Channel | Drag file to xtext, DCC to dialog partner | Only works for dialog sessions; channel drops do nothing | Must use userlist for channel DCC |
| Drag Visual Feedback | Custom drag image during layout swap | Default GTK4 drag appearance | Cosmetic only |

### ❌ DISABLED IN GTK4 (Stubbed with TODO comments)

| Feature | Location | Stub Code | Impact |
|---------|----------|-----------|--------|
| **Plugin Menu Items** | menu.c:2875-2913 | `fe_menu_add()` returns NULL | Plugins cannot add menu items |
| **Plugin Menu Updates** | menu.c | `fe_menu_del()`, `fe_menu_update()` are no-ops | Plugin menus don't work |
| **Plugin Items in Context Menus** | menu.c | `menu_add_plugin_items()` is no-op | Nick/URL/channel menus lack plugin items |
| **Channel Favorites Menu** | chanlist.c:907 | TODO comment | Can't add favorites from channel list |

**Note:** The plugin menu system requires a complete rewrite for GTK4. GTK4 uses GMenu/GAction which doesn't support the dynamic menu insertion pattern used by HexChat plugins.

---

## Compatibility Layer (gtk-compat.h)

The compatibility layer provides macros and inline functions for API differences:

### Container/Widget Macros
- `hc_box_pack_start/end()` - GTK4 uses `gtk_box_append/prepend()`
- `hc_window_set_child()`, `hc_scrolled_window_set_child()`, etc.
- `hc_widget_destroy()`, `hc_window_destroy()`
- `hc_container_set_border_width()` - Uses margins in GTK4

### Event Controller Helpers
- `hc_add_click_gesture()` - Replaces button-press-event
- `hc_add_key_controller()` - Replaces key-press-event
- `hc_add_scroll_controller()` - Replaces scroll-event
- `hc_add_motion_controller()` - Replaces motion-notify-event
- `hc_add_crossing_controller()` - Replaces enter/leave-notify-event

### ListView/ColumnView Helpers
- `hc_column_view_new_simple()` - Create GtkColumnView with selection model
- `hc_column_view_add_column()` - Add column with factory callbacks
- `hc_selection_model_get_selected_item()` - Get selected item
- `hc_pixbuf_to_texture()` - Convert GdkPixbuf to GdkTexture

### DND Helpers
- `hc_add_file_drop_target()` - GtkDropTarget for file drops
- `hc_add_drag_source()` - GtkDragSource with prepare callback

### Removed API Stubs
- `gtk_window_set_type_hint()`, `gtk_window_set_position()` - No-ops
- `gtk_button_box_*()` - Uses GtkBox
- `GtkMenu/GtkMenuItem` typedefs - For compilation only
- Many more (see gtk-compat.h for full list)

---

## List/Tree View Migration Summary

All 12 list/tree views migrated from GtkTreeView to GtkColumnView/GtkListView:

| File | Model Type | View Type | Features |
|------|------------|-----------|----------|
| urlgrab.c | GtkStringList | GtkListView | Simple string list |
| plugingui.c | HcPluginItem | GtkColumnView | 4 columns |
| notifygui.c | GtkStringList | GtkListView | Simple list |
| editlist.c | HcEditItem | GtkColumnView | Editable cells |
| textgui.c | HcEventItem | GtkColumnView | Editable cells |
| ignoregui.c | HcIgnoreItem | GtkColumnView | Checkbox columns |
| banlist.c | HcBanItem | GtkColumnView | 3 columns, sorting |
| dccgui.c | HcDccItem | GtkColumnView | 7 columns, icons, colors |
| userlistgui.c | HcUserItem | GtkColumnView | Icons, colors, sorting |
| chanlist.c | HcChannelItem | GtkColumnView | Filtering, sorting |
| chanview-tree.c | HcChanItem | GtkTreeListModel + GtkListView | Hierarchical with expanders |

---

## Build Configuration

### Visual Studio (Windows)
`win32/hexchat.props` configured for GTK4:
- `HC_GTK4=1` preprocessor define
- GTK4 include paths (`gtk-4.0`)
- GTK4 libraries (`gtk-4.lib`, `graphene-1.0.lib`)
- Output: `hexchat-build-gtk4/`

### Meson (Linux/Cross-platform)
```bash
meson setup build -Dgtk4=true
ninja -C build
```
**Note:** Meson builds not verified on Windows.

---

## Known Issues and Limitations

### Runtime (Untested)
The GTK4 build compiles but has not been runtime tested. Expected areas needing fixes:
1. Widget initialization order
2. CSS styling differences
3. Window management behavior
4. Event propagation differences

### Plugin System
Plugin menu integration is completely non-functional in GTK4. This affects:
- `/menu add` command
- Plugin-provided context menu items
- Plugin main menu entries

A proper fix requires redesigning the menu system to use GAction/GMenu patterns.

### Spell Checking
`gtk_entry_get_layout()` returns NULL in GTK4, so spell-check underline positioning may not work correctly. The spell check popup menu itself works.

---

## Files Modified

**Core:**
- `src/fe-gtk/gtk-compat.h` - Compatibility layer (1,707 lines)
- `src/fe-gtk/fe-gtk.h` - Type definitions for GTK4

**Event Handling:**
- `src/fe-gtk/xtext.c` - Custom text widget
- `src/fe-gtk/fkeys.c` - Keyboard handling
- `src/fe-gtk/chanview-tabs.c`, `chanview-tree.c` - Tab/tree views
- `src/fe-gtk/userlistgui.c` - User list

**Menus:**
- `src/fe-gtk/menu.c` - Menu system (partial GTK4 implementation)
- `src/fe-gtk/menu.h` - Menu declarations

**List Views:**
- `src/fe-gtk/urlgrab.c`, `plugingui.c`, `notifygui.c`
- `src/fe-gtk/editlist.c`, `textgui.c`, `ignoregui.c`
- `src/fe-gtk/banlist.c`, `dccgui.c`, `chanlist.c`
- `src/fe-gtk/custom-list.c`, `custom-list.h`

**Dialogs/UI:**
- `src/fe-gtk/maingui.c` - Main window, DND, quit dialog
- `src/fe-gtk/gtkutil.c` - Utility functions
- `src/fe-gtk/servlistgui.c`, `setup.c`, `joind.c`
- `src/fe-gtk/fe-gtk.c` - Initialization, dialogs
- `src/fe-gtk/sexy-spell-entry.c` - Spell check widget

---

## Next Steps

### Immediate (Before Release)
1. **Runtime Testing** - Launch and test basic functionality
2. **Fix Critical Bugs** - Address any crashes or major UI issues
3. **GTK3 Regression Test** - Verify GTK3 build still works

### Future Work
1. **Plugin Menu System** - Redesign for GAction/GMenu
2. **Spell Check Layout** - Alternative approach for underlines
3. **DND Improvements** - File drops to channels
4. **Performance Testing** - Compare GTK3 vs GTK4

---

## Resources

- [GTK4 Migration Guide](https://docs.gtk.org/gtk4/migrating-3to4.html)
- [GTK4 API Reference](https://docs.gtk.org/gtk4/)
- [GLib/GObject Reference](https://docs.gtk.org/glib/)
