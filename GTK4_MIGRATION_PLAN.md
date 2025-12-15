# HexChat GTK4 Migration Plan

## Executive Summary

This document outlines the plan for migrating HexChat from GTK3 to GTK4. The migration is substantial due to significant architectural changes in GTK4, particularly around event handling and list/tree views. The goal is to bring HexChat into the modern GTK era while improving maintainability.

**Current State**: GTK+ 3.22.0+ (with some GTK2 deprecation warnings disabled)
**Target**: GTK 4.x

**Estimated Effort**: 4-8 weeks for experienced GTK developer

---

## Phase 1: Preparation & Infrastructure

### 1.1 Build System Updates
- [x] Add GTK4 dependency option in meson.build (`-Dgtk4=true`)
- [x] Create conditional compilation paths for GTK3/GTK4 (USE_GTK4 define)
- [ ] Set up CI/testing for GTK4 builds
- [x] Create `gtk-compat.h` header for version-agnostic macros
- [ ] Consider: Transition Windows builds from VS solutions to Meson-only

### 1.2 Audit & Documentation
- [ ] Document all event signal handlers (30+ files affected)
- [ ] List all GtkTreeView/GtkListStore usages
- [ ] Identify all `gtk_container_add()` and packing calls (917+ occurrences)
- [ ] Document custom widget inheritance patterns

### 1.3 Compatibility Layer (IMPLEMENTED)
Created `src/fe-gtk/gtk-compat.h` with macros for:
- Box packing: `hc_box_pack_start()`, `hc_box_pack_end()`
- Container children: `hc_window_set_child()`, `hc_scrolled_window_set_child()`
- Widget visibility: `hc_widget_show()`, `hc_widget_show_all()`
- Widget destruction: `hc_widget_destroy()`, `hc_window_destroy()`
- Event controllers: `hc_add_click_gesture()`, `hc_add_key_controller()`, etc.
- CSS providers: `hc_style_context_add_provider_for_display()`
- Entry text: `hc_entry_get_text()`, `hc_entry_set_text()`
- Paned widgets: `hc_paned_pack1()`, `hc_paned_pack2()`

---

## Phase 2: Event System Migration

**Priority**: CRITICAL - Must be done early as it affects all interactive code

### 2.1 Event Controller Conversion Guide

| GTK3 Signal | GTK4 Replacement |
|-------------|------------------|
| `button-press-event` | `GtkGestureClick` |
| `button-release-event` | `GtkGestureClick` |
| `scroll-event` | `GtkEventControllerScroll` |
| `motion-notify-event` | `GtkEventControllerMotion` |
| `key-press-event` | `GtkEventControllerKey` |
| `key-release-event` | `GtkEventControllerKey` |
| `enter-notify-event` | `GtkEventControllerCrossing` |
| `leave-notify-event` | `GtkEventControllerCrossing` |
| `configure-event` | Widget `resize` signal / `GtkEventControllerFocus` |

### 2.2 Files Requiring Event Migration (Priority Order)
1. **xtext.c** - Custom text widget (most complex) - **DONE**
   - Motion notify, button press/release, scroll, leave notify handlers converted
   - Snapshot function implemented for GTK4 (replaces draw)
   - Clipboard/selection handling updated for GTK4
   - gdk_window_get_width/height → gtk_widget_get_width/height
   - Cairo context creation updated (GTK4 only creates during snapshot)
2. **fkeys.c** - Keyboard shortcuts - **DONE** (key handlers for input box and dialog)
3. **chanview-tabs.c** - Tab scrolling - **DONE** (scroll, click, enter/leave handlers)
4. **chanview-tree.c** - Tree view events - **DONE** (click, scroll handlers; DND TODO)
5. **userlistgui.c** - User list interactions - **DONE** (click, key handlers)
6. **banlist.c** - Context menus - **DONE** (right-click menu)
7. **chanlist.c** - Channel list - **DONE** (right-click context menu)
8. **dccgui.c** - DCC window - **DONE** (configure_event → notify signals)
9. **editlist.c** - Edit list dialogs - **DONE** (key handler)
10. **sexy-spell-entry.c** - Spell check widget - **DONE** (context menu TODO for GTK4)

### 2.3 Example Migration Pattern
```c
// GTK3
g_signal_connect(widget, "button-press-event",
                 G_CALLBACK(on_button_press), data);

// GTK4
GtkGesture *gesture = gtk_gesture_click_new();
gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
g_signal_connect(gesture, "pressed",
                 G_CALLBACK(on_click_pressed), data);
gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
```

---

## Phase 3: Widget & Container Updates

### 3.1 Container API Changes

| GTK3 | GTK4 |
|------|------|
| `gtk_container_add()` | Use specific container methods |
| `gtk_box_pack_start()` | `gtk_box_append()` |
| `gtk_box_pack_end()` | `gtk_box_prepend()` |
| `gtk_widget_show()` | Still exists, but widgets visible by default |
| `gtk_widget_destroy()` | `gtk_window_destroy()` for windows |

### 3.2 Files with Heavy Container Usage
- maingui.c (~50 pack calls)
- setup.c (~30 pack calls)
- gtkutil.c (~20 container calls)
- All dialog files

### 3.3 Button/Label Changes
```c
// GTK3
GtkWidget *btn = gtk_button_new_with_label("OK");

// GTK4
GtkWidget *btn = gtk_button_new();
gtk_button_set_label(GTK_BUTTON(btn), "OK");
// Or use: gtk_button_new_with_mnemonic("_OK");
```

---

## Phase 4: List & Tree View Modernization

**Priority**: HIGH - Major paradigm shift required

### 4.1 GTK4 List Widget Architecture
- `GtkTreeView` → `GtkColumnView` or `GtkListView`
- `GtkListStore/GtkTreeStore` → `GListModel` implementations
- Cell renderers → `GtkListItemFactory`

### 4.2 Components Requiring Conversion

| Component | Current Widget | Lines | Complexity |
|-----------|---------------|-------|------------|
| User List | GtkTreeView + GtkListStore | ~400 | HIGH |
| Channel Tree | GtkTreeView + GtkTreeStore | ~300 | HIGH |
| Channel List | GtkTreeView + custom model | ~600 | VERY HIGH |
| Ban List | GtkTreeView + GtkListStore | ~200 | MEDIUM |
| DCC List | GtkTreeView | ~150 | MEDIUM |
| Ignore List | GtkTreeView | ~100 | LOW |

### 4.3 Migration Strategy for Lists
1. Create `GListModel` implementations to replace stores
2. Create `GtkListItemFactory` for each list type
3. Replace `GtkTreeView` with `GtkColumnView`
4. Update selection handling APIs

### 4.4 Example: Simple List Migration
```c
// GTK3
GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
    -1, "Name", renderer, "text", 0, NULL);

// GTK4
GListStore *store = g_list_store_new(MY_TYPE_ITEM);
GtkWidget *view = gtk_column_view_new(GTK_SELECTION_MODEL(
    gtk_single_selection_new(G_LIST_MODEL(store))));
GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
g_signal_connect(factory, "setup", G_CALLBACK(setup_item), NULL);
g_signal_connect(factory, "bind", G_CALLBACK(bind_item), NULL);
GtkColumnViewColumn *column = gtk_column_view_column_new("Name", factory);
gtk_column_view_append_column(GTK_COLUMN_VIEW(view), column);
```

---

## Phase 5: XText Custom Widget Migration

**Priority**: CRITICAL - Core chat display widget

### 5.1 Current Architecture
- Custom `GtkWidget` subclass (~2,400 lines)
- Cairo-based rendering (already modern)
- Complex event handling for selection, scrolling, clicking
- Custom scrollbar integration via `GtkAdjustment`

### 5.2 Required Changes
1. **Widget Structure**: Update to GTK4 widget patterns
2. **Event Handling**: Convert all event signals to controllers
   - Button press/release → `GtkGestureClick`
   - Motion → `GtkEventControllerMotion`
   - Scroll → `GtkEventControllerScroll`
   - Key → `GtkEventControllerKey`
3. **Drawing**: Cairo API largely compatible
4. **Allocation**: `gtk_widget_get_allocation()` still exists
5. **Scrolling**: `GtkAdjustment` API mostly stable

### 5.3 XText Event Handlers to Migrate
- `gtk_xtext_button_press()` - Selection start, word click
- `gtk_xtext_button_release()` - Selection end
- `gtk_xtext_selection_update()` - Drag selection
- `gtk_xtext_scroll_adjustments()` - Scroll handling
- Key press handling for navigation

---

## Phase 6: Window & Screen Management

### 6.1 Deprecated APIs to Replace

| Deprecated | Replacement |
|------------|-------------|
| `gdk_window_get_geometry()` | Widget allocation/size |
| `gdk_window_get_screen()` | `GdkDisplay` / `GdkMonitor` |
| `gtk_window_set_type_hint()` | CSS classes / properties |
| `gtk_menu_set_screen()` | Implicit from parent |
| `gdk_window_add_filter()` | Platform-specific or remove |

### 6.2 Affected Files
- chanview-tabs.c (6 geometry calls)
- maingui.c (event filter, screen queries)
- servlistgui.c (type hints)
- gtkutil.c (dialog hints)
- joind.c (type hints)

---

## Phase 7: Menu System Updates

### 7.1 Changes Required
- Remove any stock icon references
- Update menu construction patterns
- Consider GtkPopoverMenu for context menus

### 7.2 Current Menu Structure (menu.c)
- Custom menu item types: `M_MENUITEM`, `M_NEWMENU`, `M_MENUSTOCK`
- Dynamic menu building with callbacks
- Context menus for various widgets

---

## Phase 8: Dialogs & Preferences

### 8.1 Dialog Files to Update
- setup.c (~400 lines of UI)
- servlistgui.c (server list)
- banlist.c, chanlist.c
- dccgui.c, fkeys.c
- joind.c, notifygui.c
- rawlog.c, textgui.c
- urlgrab.c

### 8.2 Common Dialog Patterns
```c
// GTK3
GtkWidget *dialog = gtk_dialog_new_with_buttons(
    title, parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    _("_Cancel"), GTK_RESPONSE_CANCEL,
    _("_OK"), GTK_RESPONSE_OK,
    NULL);

// GTK4 - similar but some flag changes
// Also consider GtkAlertDialog for simple cases
```

---

## Already GTK4-Compatible

These areas require minimal or no changes:
- Cairo rendering throughout
- GdkRGBA color system
- Pango text layout
- GdkPixbuf image handling
- CSS styling approach
- Most GObject patterns

---

## Testing Strategy

### Unit Testing
- Test each event controller conversion
- Verify list model behavior
- Check custom widget rendering

### Integration Testing
- Full application launch and basic usage
- IRC connection and message display
- User list operations
- Channel switching
- DCC file transfers

### Platform Testing
- Linux/Wayland
- Linux/X11
- Windows
- macOS (if supported)

---

## Risk Mitigation

1. **Parallel Development**: Keep GTK3 version working during migration
2. **Incremental Migration**: Phase-based approach with testing at each phase
3. **Compatibility Layer**: Abstract common patterns for easier transition
4. **Community Input**: Coordinate with potential maintainers
5. **Documentation**: Document all changes for future maintenance

---

## Post-Migration Cleanup

- [ ] Remove GTK3 compatibility code
- [ ] Re-enable deprecation warnings
- [ ] Update build documentation
- [ ] Update minimum GTK version in README
- [ ] Performance optimization pass
- [ ] Memory leak audit

---

## Resources

- [GTK4 Migration Guide](https://docs.gtk.org/gtk4/migrating-3to4.html)
- [GTK4 API Reference](https://docs.gtk.org/gtk4/)
- [GLib/GObject Reference](https://docs.gtk.org/glib/)
- [Pango Reference](https://docs.gtk.org/Pango/)
- Original GTK2 HexChat source code: `C:\Users\johne\source\repos\hexchat-hexchat` (reference only)

---

## Version History

- 2024-12-14: Initial plan created based on codebase analysis
- 2024-12-14: Phase 1 infrastructure completed:
  - Added `-Dgtk4=true` meson option
  - Created `gtk-compat.h` compatibility layer
  - Updated `fe-gtk.h` to include compatibility macros
  - Meson build system updated for conditional GTK3/GTK4
- 2024-12-14: Phase 2 event migration progress:
  - `editlist.c`: key_press_event → GtkEventControllerKey
  - `banlist.c`: button-press-event → GtkGestureClick, gtk_menu_popup → GtkPopoverMenu
  - `userlistgui.c`: button_press_event → GtkGestureClick, key_press_event → GtkEventControllerKey
  - Container/widget calls updated with compatibility macros
  - DND in userlistgui.c marked TODO for GTK4 (requires GtkDropTarget)
- 2024-12-14: Phase 2 chanview files migration:
  - `chanview-tabs.c`: scroll_event → GtkEventControllerScroll, button_press_event → GtkGestureClick
  - `chanview-tabs.c`: gdk_window_get_geometry → gtk_widget_get_width/height for GTK4
  - `chanview-tabs.c`: gtk_container_get_children → hc_container_get_children helper
  - `chanview-tree.c`: button-press-event → GtkGestureClick, scroll_event → GtkEventControllerScroll
  - `chanview-tree.c`: DND marked TODO for GTK4 (requires GtkDropTarget/GtkDragSource)
  - Added `hc_container_get_children()` helper to gtk-compat.h
  - Added `hc_add_crossing_controller()` helper to gtk-compat.h
- 2024-12-14: Phase 2 additional files migration:
  - `fkeys.c`: key_press_event → GtkEventControllerKey for both input box and key dialog
  - `fkeys.c`: key action handlers pass NULL for evt in GTK4 (handlers don't use it)
  - `chanlist.c`: button-press-event → GtkGestureClick for right-click context menu
  - `dccgui.c`: configure_event → notify::default-width/height for window size tracking
  - Multiple deprecated API calls wrapped with compatibility macros:
    - gtk_container_set_border_width, gtk_tree_view_set_rules_hint
    - gtk_box_pack_start → hc_box_pack_start, gtk_widget_show_all → hc_widget_show_all
- 2024-12-14: Phase 2 sexy-spell-entry.c migration:
  - `sexy_spell_entry_draw` → `sexy_spell_entry_snapshot` for GTK4
  - `button_press_event` vfunc → GtkGestureClick controller
  - `gdk_window_invalidate_rect` → `gtk_widget_queue_draw`
  - Menu-based spell check suggestions wrapped in `#if !HC_GTK4` (TODO: rewrite with GtkPopoverMenu)
  - `gtk_widget_show`/`gtk_widget_show_all` calls → compat macros
- 2024-12-14: Phase 2 xtext.c/maingui.c signal handling:
  - Added `last_click_button`, `last_click_state`, `last_click_n_press` fields to GtkXText struct
  - GTK4 button handlers now populate click info before emitting `word_click` signal
  - `mg_word_clicked` in maingui.c updated to extract button/state from xtext fields in GTK4
  - Fixed popup menu mnemonic display (added XCMENU_MNEMONIC flag to menu_create)
- 2024-12-14: Phase 3 Container/Widget Updates COMPLETE:
  - Converted all `gtk_container_add` calls to specific container methods (hc_window_set_child, hc_frame_set_child, hc_scrolled_window_set_child, hc_box_add, hc_viewport_set_child)
  - Converted all `gtk_widget_destroy` calls to hc_widget_destroy or hc_window_destroy
  - Files updated: maingui.c, gtkutil.c, servlistgui.c, setup.c, menu.c, textgui.c, ascii.c, fe-gtk.c, ignoregui.c, joind.c, notifygui.c, rawlog.c
  - Added compile-time guards in gtk-compat.h to catch accidental use of deprecated APIs in GTK4 mode
- 2024-12-14: Phase 7 Menu System - GTK3 code wrapped:
  - Added `hc_popover_menu_popup_at()` helper in gtk-compat.h for GTK4 popover menus
  - Wrapped GTK3-only menu functions in `#if !HC_GTK4` blocks:
    - menu.c: `menu_destroy()`, `menu_popup()`, `menu_nickmenu()`, `menu_middlemenu()`, `menu_urlmenu()`, `menu_chanmenu()`
    - maingui.c: `mg_menu_destroy()`, `mg_create_tabmenu()`
  - Added GTK4 stub implementations with TODO comments for all wrapped functions
  - banlist.c and chanlist.c already had full GTK4 implementations using GtkPopoverMenu
  - sexy-spell-entry.c spell menu already wrapped in `#if !HC_GTK4`
- 2024-12-14: Phase 7 Menu System - GTK4 context menus implemented:
  - Added `last_click_x`, `last_click_y` fields to GtkXText struct for popover positioning
  - Updated xtext.c button handlers to store click position in GTK4 mode
  - Implemented `menu_urlmenu()` GTK4 version with GMenu + GtkPopoverMenu:
    - Actions: open URL, copy URL
    - Supports IRC/IRCS URLs (Connect) vs regular URLs (Open in Browser)
  - Implemented `menu_chanmenu()` GTK4 version:
    - Actions: join, focus, part, cycle channel
    - Dynamic menu based on whether channel is joined
  - Implemented `menu_nickmenu()` GTK4 version:
    - Actions: query, whois, notify, ignore, kick, ban, op, deop, voice, devoice
    - Operator actions in separate section
  - Implemented `menu_middlemenu()` GTK4 version:
    - Actions: clear text, search, save text, menubar toggle, settings
  - All GTK4 menus use GSimpleActionGroup for proper action handling
  - Action groups cleaned up when popover is closed
- 2024-12-14: Phase 4 TreeView/ListView - Deprecated API compatibility:
  - Added `hc_tree_view_set_rules_hint()` macro (no-op in GTK4, use CSS instead)
  - Added `hc_tree_view_get_vadjustment()` macro (uses GtkScrollable in GTK4)
  - Added `hc_tree_view_get_hadjustment()` macro (uses GtkScrollable in GTK4)
  - Updated files with deprecated API calls:
    - ignoregui.c, plugingui.c, setup.c, textgui.c: gtk_tree_view_set_rules_hint → hc_tree_view_set_rules_hint
    - userlistgui.c: gtk_tree_view_get_vadjustment → hc_tree_view_get_vadjustment
  - Note: GtkTreeView still works in GTK4 (deprecated but functional)
  - Full migration to GtkColumnView/GtkListView deferred as major future work
- 2024-12-14: DND (Drag and Drop) - GTK3 code wrapped:
  - All GTK3 DND code wrapped in `#if !HC_GTK4` blocks:
    - maingui.c: DND handler functions (mg_drag_begin_cb, mg_drag_end_cb, mg_drag_drop_cb, mg_drag_motion_cb)
    - maingui.c: DND setup for vscrollbar (layout swapping) and xtext (file drops for DCC)
    - maingui.h: DND function declarations wrapped
    - userlistgui.c: Already wrapped - DND for file drops on userlist
    - chanview-tree.c: Already wrapped - DND for tree view layout swapping
  - DND features disabled in GTK4 build (stubs with TODO comments)
  - TODO: Implement GTK4 DND using GtkDropTarget/GtkDragSource controllers
- 2024-12-14: Dialog Updates - gtk_dialog_run() removal:
  - `gtk_dialog_run()` is removed in GTK4 - dialogs must use async response handling
  - fe-gtk.c `create_msg_dialog()` (WIN32 only):
    - GTK4: Uses response signal + mini event loop for blocking behavior at startup
  - fe-gtk.c `fe_message()`:
    - GTK4: FE_MSG_WAIT flag uses mini event loop; gtk_window_set_position removed
  - maingui.c `mg_open_quit_dialog()`:
    - GTK4: Complete rewrite using async response callback
    - Removed deprecated APIs: gtk_dialog_get_action_area, gtk_button_box_set_layout,
      gtk_container_set_border_width, GTK_ICON_SIZE_DIALOG
    - Uses gtk_dialog_add_button instead of accessing action area
    - Uses widget margins instead of container border width
    - Uses GTK_ICON_SIZE_LARGE + gtk_image_set_icon_size instead of icon size param
    - Uses gtk_check_button_get_active instead of gtk_toggle_button_get_active
- 2024-12-14: Deprecated API Compatibility - Mass replacement with compat macros:
  - Added new compatibility macros to gtk-compat.h:
    - `hc_container_set_border_width()` - uses widget margins in GTK4
    - `hc_window_set_position()` - no-op in GTK4 (window manager handles)
    - `hc_scrolled_window_new()` - no params in GTK4
    - `hc_image_new_from_icon_name()` - maps GTK3 icon sizes to GTK4
    - `hc_button_box_new()` - uses GtkBox in GTK4 (GtkButtonBox removed)
    - `hc_button_box_set_layout()` - no-op in GTK4
    - `hc_scrolled_window_set_shadow_type()` - no-op in GTK4 (use CSS)
    - `hc_viewport_set_shadow_type()` - no-op in GTK4 (use CSS)
  - Replaced 102 deprecated API calls across 20 files:
    - gtk_container_set_border_width: 33 replacements
    - gtk_window_set_position: 14 replacements
    - gtk_scrolled_window_new(NULL, NULL): 14 replacements
    - gtk_button_box_set_layout: 17 replacements
    - gtk_button_box_new: 16 replacements
    - gtk_image_new_from_icon_name: 8 replacements
  - Files updated: gtkutil.c, maingui.c, servlistgui.c, setup.c, menu.c, textgui.c,
    fe-gtk.c, ignoregui.c, joind.c, notifygui.c, rawlog.c, banlist.c, chanlist.c,
    dccgui.c, editlist.c, fkeys.c, plugingui.c, urlgrab.c, chanview-tabs.c, chanview-tree.c, ascii.c
- 2024-12-14: Entry and CheckButton API Compatibility:
  - Added new compatibility macros to gtk-compat.h:
    - `hc_entry_get_text()` / `hc_entry_set_text()` - GTK4 uses GtkEditable interface
    - `hc_check_button_get_active()` / `hc_check_button_set_active()` - GTK4 separates GtkCheckButton from GtkToggleButton
  - Replaced 78 gtk_entry_get_text/set_text calls across 10 files:
    - chanlist.c (4), fe-gtk.c (3), gtkutil.c (2), ignoregui.c (6), joind.c (3),
      maingui.c (21), menu.c (1), notifygui.c (4), servlistgui.c (20), setup.c (14)
  - Replaced 20 gtk_toggle_button_get_active/set_active calls for checkbuttons across 6 files:
    - banlist.c (3), chanlist.c (4), joind.c (2), maingui.c (3), servlistgui.c (3), setup.c (5)
- 2024-12-14: Additional deprecated API migration (session recovery):
  - Replaced all gtk_box_pack_start/end calls with hc_box_pack_start/end (80+ calls)
  - Files updated: ascii.c, chanview-tabs.c, editlist.c, ignoregui.c, gtkutil.c,
    joind.c, maingui.c, servlistgui.c, setup.c, textgui.c, menu.c, notifygui.c,
    plugingui.c, rawlog.c, urlgrab.c
  - Replaced gtk_widget_show_all calls with hc_widget_show_all (20+ calls)
  - Added hc_misc_set_alignment() macro for GtkMisc deprecated API
  - Replaced remaining gtk_entry_get_text calls in menu.c and sexy-spell-entry.c
- 2024-12-14: Tab context menu and spell check menu GTK4 implementation:
  - Implemented mg_create_tabmenu() GTK4 version in maingui.c:
    - Actions: detach tab, close tab
    - Uses GMenu + GtkPopoverMenu + GSimpleActionGroup
    - Added static context (tab_menu_sess, tab_menu_ch) for action callbacks
    - Added tab_menu_popover_closed_cb for cleanup
  - Added chan_get_impl_widget() to chanview.c/h for popover parent widget access
  - Updated chanview-tabs.c tab_click_cb to handle middle-click close in GTK4
  - Updated mg_tab_contextmenu_cb with GTK4 branch (event is NULL in GTK4)
  - Implemented spell check context menu in sexy-spell-entry.c for GTK4:
    - Actions: replace word (with parameter for suggestion index), add to dictionary, ignore all
    - Dynamic suggestions from Enchant dictionary (up to 10 shown)
    - Uses GMenu + GtkPopoverMenu + GSimpleActionGroup
    - Static context for action callbacks (spell_menu_entry, spell_menu_word, etc.)
    - Proper cleanup of suggestions in popover closed callback
    - Right-click on misspelled word shows popup
- 2024-12-14: DND (Drag and Drop) - GTK4 implementation complete:
  - Added helper functions to gtk-compat.h:
    - `hc_add_file_drop_target()` - Creates GtkDropTarget for file drops (G_TYPE_FILE)
    - `hc_add_string_drop_target()` - Creates GtkDropTarget for string content
    - `hc_add_drag_source()` - Creates GtkDragSource with prepare callback
  - Implemented GTK4 DND in maingui.c:
    - `mg_xtext_file_drop_cb()` - File drops on xtext for DCC send to dialog partner
    - `mg_scrollbar_drop_cb()` - Layout swapping (receives chanview/userlist drops)
    - `mg_userlist_drag_prepare_cb()` / `mg_chanview_drag_prepare_cb()` - Drag source data
    - `mg_handle_drop_gtk4()` - Calculate drop position for layout swapping
    - Setup functions: `mg_setup_xtext_dnd()`, `mg_setup_scrollbar_dnd()`,
      `mg_setup_userlist_drag_source()`, `mg_setup_chanview_drag_source()`
  - Implemented GTK4 DND in userlistgui.c:
    - `userlist_file_drop_cb()` - File drops on user for DCC send
    - `userlist_drop_motion_cb()` - Highlights user under cursor during drag
    - `userlist_drop_leave_cb()` - Clears selection when drag leaves
  - Updated chanview-tree.c to use `mg_setup_chanview_drag_source()`
  - Added GTK4 function declarations to maingui.h
- 2024-12-14: Clipboard/Selection handling in xtext.c - Verified complete:
  - GTK4 clipboard handling already implemented in `gtk_xtext_set_clip_owner()`:
    - Uses `gtk_widget_get_clipboard()` and `gdk_clipboard_set_text()` for main clipboard
    - Uses `gtk_widget_get_primary_clipboard()` for PRIMARY selection
  - GTK3 `selection_add_targets()` not needed in GTK4 - clipboard handles formats automatically
  - GTK3 `selection_clear_event` callback not available in GTK4 - visual selection persists until user clicks
  - Updated TODO comment in xtext.c to document GTK4 clipboard behavior
- 2024-12-14: GtkColumnView/GtkListView Migration - Phase 0 helpers added:
  - Added helper functions to gtk-compat.h for GTK4 list views:
    - `hc_list_view_new_simple()` - Create GtkListView with signal factory
    - `hc_column_view_new_simple()` - Create GtkColumnView with selection model
    - `hc_column_view_add_column()` - Add column with custom factory callbacks
    - `hc_selection_model_get_selected_item()` - Get selected item from model
    - `hc_selection_model_get_selected_position()` - Get position of selected item
    - `hc_pixbuf_to_texture()` - Convert GdkPixbuf to GdkTexture for icons
- 2024-12-14: GtkColumnView/GtkListView Migration - Phase 1 (urlgrab.c) complete:
  - urlgrab.c migrated from GtkTreeView to GtkStringList + GtkListView
  - New GTK4 functions: `url_listview_new()`, factory callbacks, click handlers
  - Uses `GtkStringList` for simple string storage (no custom GObject needed)
  - Double-click via `activate` signal; right-click via `GtkGestureClick`
  - Updated `menu_urlmenu()` signature for GTK4: `(GtkWidget *parent, double x, double y, char *url)`
  - Updated maingui.c calls to menu_urlmenu() to pass xtext widget and click position
  - Created GTK4_LISTVIEW_MIGRATION.md with detailed migration plan
- 2024-12-14: GtkColumnView/GtkListView Migration - Phase 1 (plugingui.c) complete:
  - plugingui.c migrated from GtkTreeView to GListStore + GtkColumnView
  - Created HcPluginItem GObject subclass to hold plugin data (name, version, file, desc, filepath)
  - New GTK4 functions: `plugingui_columnview_new()`, factory setup/bind callbacks per column
  - Uses `hc_column_view_new_simple()` and `hc_column_view_add_column()` helpers
  - 4 visible columns (Name, Version, File, Description) with resizable columns
  - `fe_pluginlist_update()` uses `g_list_store_remove_all()` and `g_list_store_append()`
  - Selection via `plugingui_get_selected_item()` using helper function

---

## Current Status Summary

### Completed Work:
1. **Phase 1 (Infrastructure)**: COMPLETE
   - Meson build system with `-Dgtk4=true` option
   - `gtk-compat.h` compatibility layer with macros for containers, events, widgets

2. **Phase 2 (Event Migration)**: COMPLETE
   - All major event handlers converted: xtext.c, fkeys.c, chanview-tabs.c, chanview-tree.c,
     userlistgui.c, banlist.c, chanlist.c, dccgui.c, editlist.c, sexy-spell-entry.c
   - Event controller helpers in gtk-compat.h: hc_add_click_gesture, hc_add_key_controller,
     hc_add_scroll_controller, hc_add_motion_controller

3. **Phase 3 (Container/Widget Updates)**: COMPLETE
   - All `gtk_container_add` and `gtk_widget_destroy` calls converted to compatibility macros
   - Files converted:
     - maingui.c (36 calls)
     - gtkutil.c (23 calls)
     - servlistgui.c (23 calls)
     - setup.c (15 calls)
     - menu.c (8 calls)
     - textgui.c (6 calls)
     - ascii.c (3 calls)
     - fe-gtk.c (3 calls)
     - ignoregui.c (2 calls)
     - joind.c (3 calls)
     - notifygui.c (2 calls)
     - rawlog.c (2 calls)
   - New compatibility macros added:
     - `hc_viewport_set_child()` - for viewport containers
     - Compile-time guards in gtk-compat.h to catch accidental use of deprecated APIs

4. **Phase 7 (Menu System)**: COMPLETE
   - All GTK3-only menu code wrapped in `#if !HC_GTK4` conditionals
   - Full GTK4 implementations using GMenu + GtkPopoverMenu + GSimpleActionGroup:
     - banlist.c, chanlist.c: Copy menu items
     - menu.c: URL menu (open/copy), channel menu (join/part/cycle/focus), nick menu (query/whois/op/kick/etc), middle-click menu (clear/search/save/settings)
     - maingui.c: Tab context menu (detach/close)
     - sexy-spell-entry.c: Spell check suggestions menu with word replacement
   - Helper function `hc_popover_menu_popup_at()` added to gtk-compat.h

5. **DND (Drag and Drop)**: COMPLETE
   - All GTK3 DND code wrapped in `#if !HC_GTK4` blocks
   - GTK4 implementation using GtkDropTarget and GtkDragSource:
     - File drops on xtext for DCC to dialog partner (mg_xtext_file_drop_cb)
     - File drops on userlist for DCC to specific user (userlist_file_drop_cb)
     - Layout swapping via drag/drop (userlist and chanview as drag sources, scrollbar as drop target)
   - Files: maingui.c, maingui.h, userlistgui.c, chanview-tree.c, gtk-compat.h

6. **Dialog Updates**: gtk_dialog_run() REMOVAL HANDLED
   - fe-gtk.c: `create_msg_dialog()` and `fe_message()` converted for GTK4
   - maingui.c: `mg_open_quit_dialog()` fully rewritten for GTK4
   - Uses async response handling via signals instead of gtk_dialog_run()
   - Multiple deprecated dialog APIs handled (action area, button box, border width)

7. **Deprecated API Compatibility**: COMPLETE
   - New macros added to gtk-compat.h and all calls converted:
     - `hc_container_set_border_width()` - 33 calls converted
     - `hc_window_set_position()` - 14 calls converted
     - `hc_scrolled_window_new()` - 14 calls converted
     - `hc_button_box_new()` / `hc_button_box_set_layout()` - 33 calls converted
     - `hc_image_new_from_icon_name()` - 8 calls converted
     - `hc_scrolled_window_set_shadow_type()` / `hc_viewport_set_shadow_type()` - macros added

8. **GTK3 Build**: NEEDS VERIFICATION
   - Previous phases verified working
   - Latest deprecated API changes need testing

### Remaining Work (Priority Order):

1. **Phase 7: Menu System** - COMPLETE
   - All context menus implemented for GTK4
   - Consider: Custom popup menus from plugins (menu_add_plugin_items not yet ported)

2. **Phase 4: TreeView/ListView** - DEPRECATED API COMPATIBILITY DONE
   - GtkTreeView still works in GTK4 (deprecated but functional)
   - Compatibility macros added for removed/changed APIs:
     - `hc_tree_view_set_rules_hint()` - no-op in GTK4
     - `hc_tree_view_get_vadjustment()` - uses GtkScrollable in GTK4
   - **Future Work**: Full migration to GtkColumnView/GtkListView with GListModel
     - Would be a major rewrite affecting 13+ files
     - Affects: userlist, channel tree, channel list, ban list, DCC list, plugins, etc.

3. **DND (Drag and Drop)** - COMPLETE
   - GTK4 implementation using GtkDropTarget and GtkDragSource
   - File drops for DCC on xtext and userlist
   - Layout swapping (drag userlist/chanview to scrollbar)

4. **Remaining Deprecated APIs** - COMPLETE
   - ~~`gtk_container_set_border_width`~~ - DONE (hc_container_set_border_width)
   - ~~`gtk_scrolled_window_new(NULL, NULL)`~~ - DONE (hc_scrolled_window_new)
   - ~~`gtk_tree_view_set_rules_hint`~~ - DONE (hc_tree_view_set_rules_hint)
   - ~~`gtk_viewport_set_shadow_type`~~ - DONE (hc_viewport_set_shadow_type)
   - ~~`gtk_scrolled_window_set_shadow_type`~~ - DONE (hc_scrolled_window_set_shadow_type)
   - ~~`gtk_window_set_position`~~ - DONE (hc_window_set_position)
   - ~~`gtk_dialog_get_action_area`~~ - DONE (joind.c rewritten to use gtk_dialog_add_button)
   - ~~`gtk_button_box_set_layout` / GtkButtonBox~~ - DONE (hc_button_box_*)
   - ~~`gtk_entry_get_text` / `gtk_entry_set_text`~~ - DONE (hc_entry_get_text / hc_entry_set_text)
   - ~~`gtk_toggle_button_get/set_active` for GtkCheckButton~~ - DONE (hc_check_button_get/set_active)

### Key Files Modified:
- `src/fe-gtk/gtk-compat.h` - Added viewport_set_child, compile-time guards, hc_popover_menu_popup_at() helper, TreeView compat macros
- `src/fe-gtk/xtext.h` - Added click info fields (button, state, n_press, x, y)
- `src/fe-gtk/xtext.c` - GTK4 button handlers store click info including position, clipboard handling uses gdk_clipboard_set_text()
- `src/fe-gtk/maingui.c` - mg_word_clicked uses xtext click info, container calls converted, tab menu wrapped for GTK4, DND wrapped for GTK3, quit dialog rewritten for GTK4
- `src/fe-gtk/maingui.h` - DND function declarations wrapped for GTK3
- `src/fe-gtk/menu.c` - Added XCMENU_MNEMONIC, container/destroy calls converted, context menus wrapped for GTK4, full GTK4 implementations for urlmenu/chanmenu/nickmenu/middlemenu
- `src/fe-gtk/gtkutil.c` - All container/destroy calls converted
- `src/fe-gtk/servlistgui.c` - All container/destroy calls converted
- `src/fe-gtk/setup.c` - All container/destroy calls converted
- `src/fe-gtk/textgui.c` - All container/destroy calls converted
- `src/fe-gtk/ascii.c` - All container calls converted
- `src/fe-gtk/fe-gtk.c` - All destroy calls converted, create_msg_dialog and fe_message rewritten for GTK4
- `src/fe-gtk/ignoregui.c` - All container/destroy calls converted
- `src/fe-gtk/joind.c` - All destroy calls converted, gtk_dialog_get_action_area replaced with gtk_dialog_add_button
- `src/fe-gtk/notifygui.c` - All container/destroy calls converted
- `src/fe-gtk/rawlog.c` - All container calls converted
- `src/fe-gtk/banlist.c` - Full GTK4 context menu implementation
- `src/fe-gtk/chanlist.c` - Full GTK4 context menu implementation
- `src/fe-gtk/sexy-spell-entry.c` - gtk_entry_get_text calls converted to hc_entry_get_text

### Build Note:
Meson builds do not yet work on Windows. User must manually build/test using Visual Studio.
