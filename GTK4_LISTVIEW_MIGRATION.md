# HexChat GtkTreeView to GtkColumnView/GtkListView Migration Plan

## Overview

This plan details the migration of HexChat's 12 GtkTreeView implementations to GTK4's GtkColumnView/GtkListView, maintaining full GTK3 backward compatibility via `#if HC_GTK4` conditionals.

## Decision: Why Migrate Instead of Using Deprecated GtkTreeView?

While GtkTreeView still works in GTK4 (deprecated), migration is recommended because:
1. **Performance**: GtkColumnView uses widget recycling for better large-list performance
2. **Future-proofing**: GtkTreeView may be removed in future GTK versions
3. **Modern patterns**: Better integration with GTK4's architecture

## Component Inventory (by complexity)

| File | Purpose | Columns | Special Features | Complexity |
|------|---------|---------|------------------|------------|
| urlgrab.c | URL list | 1 | String only | Simple |
| plugingui.c | Plugins | 5 | Text only | Simple |
| editlist.c | Config lists | 2 | Editable | Simple |
| servlistgui.c | Server list | 2-3 | Editable | Medium |
| banlist.c | Ban list | 4 | Date sorting | Medium |
| notifygui.c | Notify list | 6 | Color | Medium |
| dccgui.c | Transfers | 11 | Color, real-time | Medium |
| ignoregui.c | Ignore list | 8 | Checkboxes | Medium |
| textgui.c | Events | 3 | Editable | Medium |
| userlistgui.c | User list | 5 | Icon+text+color, sorting, DND | High |
| chanlist.c | Channel list | 3 | Custom model, filtering | High |
| chanview-tree.c | Channel tree | 3 | Hierarchical (GtkTreeStore) | Very High |

## Phased Implementation Plan

### Phase 0: Infrastructure (gtk-compat.h helpers) - COMPLETE

Added reusable helpers to `src/fe-gtk/gtk-compat.h`:

```c
#if HC_GTK4
/* Create a simple GtkListView with factory */
GtkWidget *hc_list_view_new_simple(GListModel *model, GtkSelectionMode mode,
                                    GCallback setup_cb, GCallback bind_cb,
                                    gpointer user_data);

/* Create a GtkColumnView with selection model */
GtkWidget *hc_column_view_new_simple(GListModel *model, GtkSelectionMode mode);

/* Add column with factory to a GtkColumnView */
GtkColumnViewColumn *hc_column_view_add_column(GtkColumnView *view,
                                                const char *title,
                                                GCallback setup_cb,
                                                GCallback bind_cb,
                                                gpointer user_data);

/* Selection helpers */
gpointer hc_selection_model_get_selected_item(GtkSelectionModel *model);
guint hc_selection_model_get_selected_position(GtkSelectionModel *model);

/* Pixbuf to texture conversion */
GdkTexture *hc_pixbuf_to_texture(GdkPixbuf *pixbuf);
#endif
```

### Phase 1: Simple Text Lists - COMPLETE

**Files:** `urlgrab.c`, `plugingui.c`

#### urlgrab.c - COMPLETE

**Strategy:** Use `GtkStringList` (built-in for string-only lists) + `GtkListView`

**Implementation:**
- Created `url_listview_new()` using `GtkStringList` and `hc_list_view_new_simple()`
- Factory setup creates a `GtkLabel` with left alignment and ellipsize
- Factory bind sets label text from `GtkStringObject`
- Double-click activation handled via `activate` signal
- Right-click context menu via `GtkGestureClick` controller
- `fe_url_add()` uses `gtk_string_list_splice()` to prepend URLs
- Updated `menu_urlmenu()` signature for GTK4: `(GtkWidget *parent, double x, double y, char *url)`

#### plugingui.c - COMPLETE

**Strategy:** Use `GListStore` with custom `GObject` item + `GtkColumnView`

**Implementation:**
- Created `HcPluginItem` GObject subclass with fields: name, version, file, desc, filepath
- Used `G_DECLARE_FINAL_TYPE` and `G_DEFINE_TYPE` macros for proper GObject registration
- Created `plugingui_columnview_new()` using `hc_column_view_new_simple()` helper
- Added 4 columns with `hc_column_view_add_column()` - each with setup/bind callbacks
- Factory setup creates centered, ellipsized labels
- Factory bind extracts field from HcPluginItem and sets label text
- `fe_pluginlist_update()` clears store with `g_list_store_remove_all()` and appends items
- Selection via `plugingui_get_selected_item()` using `hc_selection_model_get_selected_item()`
- Unload/Reload buttons extract filepath from selected HcPluginItem

### Phase 2: Editable Lists - COMPLETE

**Files:** `editlist.c`, `textgui.c`

#### editlist.c - COMPLETE

**Strategy:** Use `GListStore` with custom `GObject` item + `GtkColumnView` with `GtkEditableLabel`

**Implementation:**
- Created `HcEditItem` GObject subclass with fields: name, cmd
- Used `G_DECLARE_FINAL_TYPE` and `G_DEFINE_TYPE` macros for proper GObject registration
- Created `editlist_columnview_new()` with two columns using `GtkEditableLabel` for inline editing
- Factory setup creates `GtkEditableLabel` widgets
- Factory bind sets label text and connects `notify::text` signal for live editing
- Factory unbind disconnects signals to avoid issues on row recycling
- Shift+Up/Down row reordering implemented via key controller using `g_list_store_remove` and `g_list_store_insert`
- `editlist_save()` iterates GListStore to write to file
- `editlist_load()` populates store from GSList

#### textgui.c - COMPLETE

**Strategy:** Use `GListStore` with custom `GObject` items + `GtkColumnView` with `GtkEditableLabel`

**Implementation:**
- Created `HcEventItem` GObject subclass with fields: event_name, text, row (index into te[] array)
- Created `HcHelpItem` GObject subclass with fields: number, description
- Two GListStores: `pevent_store` (events), `pevent_help_store` (help info)
- Main event list uses `GtkColumnView` with:
  - Event column: read-only `GtkLabel`
  - Text column: `GtkEditableLabel` for inline editing with validation
- Help list uses `GtkColumnView` (GTK_SELECTION_NONE) with:
  - $ Number column: `GtkLabel`
  - Description column: `GtkLabel`
- Selection changes on event list trigger `pevent_selection_changed()` to update help list
- Text editing validates format strings via `pevt_build_string()` before accepting changes
- Live preview in xtext widget when editing completes

### Phase 3: Multi-Type Columns with Sorting

**Files:** `banlist.c`, `notifygui.c`, `dccgui.c`

Key techniques:
- Use `GtkSortListModel` wrapper for sorting
- Use `GtkCustomSorter` for custom sort logic (e.g., date parsing)
- For colors: Use CSS classes (`gtk_widget_add_css_class`)
- For real-time updates: Call `g_list_model_items_changed(model, pos, 1, 1)`

### Phase 4: Checkbox Columns - COMPLETE

**Files:** `ignoregui.c`

**Strategy:** Use `GListStore` with custom `GObject` item + `GtkColumnView` with `GtkCheckButton` for toggles

**Implementation:**
- Created `HcIgnoreItem` GObject subclass with fields: mask, chan, priv, notice, ctcp, dcc, invite, unignore
- Used `G_DECLARE_FINAL_TYPE` and `G_DEFINE_TYPE` macros for proper GObject registration
- Created `ignore_columnview_new()` with 8 columns:
  - Mask column: `GtkEditableLabel` for inline editing
  - 7 toggle columns: `GtkCheckButton` widgets centered in cells
- Factory setup creates `GtkCheckButton` with `gtk_widget_set_halign(check, GTK_ALIGN_CENTER)`
- Factory bind:
  - Gets appropriate boolean field based on column ID passed via user_data
  - Sets check button state with `gtk_check_button_set_active()`
  - Stores column ID on widget with `g_object_set_data()` for callback
  - Connects `toggled` signal
- Factory unbind disconnects signals to avoid issues on row recycling
- Toggle callback:
  - Retrieves column ID from widget data
  - Updates appropriate field in `HcIgnoreItem`
  - Calls `ignore_add()` to update internal ignore list
- Mask editing callback validates for duplicates and updates both item and ignore list
- `ignore_gui_open()` creates column view and populates from `ignore_list`
- Delete/Clear callbacks iterate using GListStore APIs

### Phase 5: Complex Lists (Icons, DND, Sorting)

**Files:** `userlistgui.c`

Key challenges:
1. **Icons**: Use `GtkPicture` with `GdkPaintable` (convert `GdkPixbuf` via `hc_pixbuf_to_texture`)
2. **Colors**: Apply via CSS classes or `PangoAttrList` on labels
3. **Sorting**: Use `GtkCustomSorter` with existing sort functions
4. **DND**: Already have GTK4 DND implemented - adapt for new widget

### Phase 6: Custom Model with Filtering

**Files:** `chanlist.c`, `custom-list.c`

Major work:
- Rewrite `CustomList` to implement `GListModel` instead of `GtkTreeModel`
- Use `GtkFilterListModel` + `GtkCustomFilter` for filtering
- Use `GtkSortListModel` for sorting
- Batch updates via `g_list_model_items_changed(model, start, 0, count)`

### Phase 7: Hierarchical Tree

**Files:** `chanview-tree.c`

Strategy:
- Use `GtkTreeListModel` (wraps a flat model and adds hierarchy)
- Server rows return child model (channel list) via callback
- Use `GtkTreeExpander` widget for expand/collapse UI
- Bind expander to `GtkTreeListRow` for expand state

## Implementation Pattern (per file)

```c
#if HC_GTK4

/* 1. Define GObject item type */
typedef struct {
    GObject parent;
    /* row data fields */
} HcMyItem;
G_DEFINE_TYPE(HcMyItem, hc_my_item, G_TYPE_OBJECT)

/* 2. Factory setup callback */
static void my_setup_cb(GtkListItemFactory *factory, GtkListItem *item) {
    GtkWidget *label = gtk_label_new(NULL);
    gtk_list_item_set_child(item, label);
}

/* 3. Factory bind callback */
static void my_bind_cb(GtkListItemFactory *factory, GtkListItem *item) {
    GtkWidget *label = gtk_list_item_get_child(item);
    HcMyItem *data = gtk_list_item_get_item(item);
    gtk_label_set_text(GTK_LABEL(label), data->text);
}

/* 4. Create view */
static GtkWidget *my_view_new(void) {
    GListStore *store = g_list_store_new(HC_TYPE_MY_ITEM);
    GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(my_setup_cb), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(my_bind_cb), NULL);

    return gtk_list_view_new(GTK_SELECTION_MODEL(sel), factory);
}

#else
/* Existing GTK3 code unchanged */
#endif
```

## Files Modified So Far

1. **gtk-compat.h** - Added helper macros/functions (Phase 0)
2. **urlgrab.c** - Migrated to GtkStringList + GtkListView (Phase 1)
3. **plugingui.c** - Migrated to GListStore + GtkColumnView (Phase 1)
4. **menu.c** - Updated menu_urlmenu() signature for GTK4
5. **menu.h** - Updated declaration with HC_GTK4 conditional
6. **maingui.c** - Updated menu_urlmenu() calls for GTK4
7. **editlist.c** - Migrated to GListStore + GtkColumnView with GtkEditableLabel (Phase 2)
8. **textgui.c** - Migrated to GListStore + GtkColumnView with GtkEditableLabel (Phase 2)
9. **banlist.c** - Migrated to GListStore + GtkColumnView with sorting, multi-selection (Phase 3)
10. **notifygui.c** - Migrated to GListStore + GtkColumnView with CSS colors (Phase 3)
11. **dccgui.c** - Migrated to GListStore + GtkColumnView with icons, colors, real-time updates (Phase 3)
12. **ignoregui.c** - Migrated to GListStore + GtkColumnView with GtkCheckButton toggle columns (Phase 4)

## Files Remaining

1. **userlistgui.c** - Icons, colors, sorting, DND (Phase 5)
2. **custom-list.c** - GListModel implementation (Phase 6)
3. **chanlist.c** - Uses custom model, filtering (Phase 6)
4. **chanview-tree.c** - Hierarchical with GtkTreeListModel (Phase 7)

## Progress Tracking

| Phase | Status | Files | Notes |
|-------|--------|-------|-------|
| 0 | COMPLETE | gtk-compat.h | Helper functions added |
| 1 | COMPLETE | urlgrab.c, plugingui.c | GtkStringList + GtkListView, GListStore + GtkColumnView |
| 2 | COMPLETE | editlist.c, textgui.c | GtkEditableLabel for inline editing |
| 3 | COMPLETE | banlist.c, notifygui.c, dccgui.c | CSS for colors, GtkCustomSorter, GtkBitset for multi-select |
| 4 | COMPLETE | ignoregui.c | GtkCheckButton for toggle columns |
| 5 | PENDING | userlistgui.c | |
| 6 | PENDING | custom-list.c, chanlist.c | |
| 7 | PENDING | chanview-tree.c | |

## Key GTK4 APIs Used

- `GtkStringList` - Simple string-based GListModel
- `GListStore` - Generic GObject-based GListModel
- `GtkListView` - Single-column list view
- `GtkColumnView` - Multi-column list view
- `GtkSignalListItemFactory` - Factory for creating list item widgets
- `GtkSingleSelection` / `GtkMultiSelection` - Selection models
- `GtkSortListModel` - Sorting wrapper
- `GtkFilterListModel` - Filtering wrapper
- `GtkTreeListModel` - Hierarchical model (for tree views)
- `GtkTreeExpander` - Expand/collapse widget for tree items
