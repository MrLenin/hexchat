# HexChat GtkTreeView → GtkColumnView/GtkListView Migration

## Status: COMPLETE ✅

All 11 GtkTreeView implementations have been migrated to GTK4's GtkColumnView/GtkListView with full GTK3 backward compatibility via `#if HC_GTK4` conditionals.

**Build Status (2024-12-15):**
- ✅ GTK4 Compilation: SUCCESSFUL
- ⚠️ Runtime Testing: NOT YET DONE

---

## Migration Summary

| File | GTK3 Widget | GTK4 Model | GTK4 View | Features |
|------|-------------|------------|-----------|----------|
| urlgrab.c | GtkTreeView + GtkListStore | GtkStringList | GtkListView | Simple URL list |
| plugingui.c | GtkTreeView + GtkListStore | HcPluginItem (GObject) | GtkColumnView | 4 columns |
| notifygui.c | GtkTreeView + GtkListStore | GtkStringList | GtkListView | Simple notify list |
| editlist.c | GtkTreeView + GtkListStore | HcEditItem (GObject) | GtkColumnView | Inline editing |
| textgui.c | GtkTreeView + GtkListStore | HcEventItem (GObject) | GtkColumnView | Inline editing |
| ignoregui.c | GtkTreeView + GtkListStore | HcIgnoreItem (GObject) | GtkColumnView | Checkbox columns |
| banlist.c | GtkTreeView + GtkListStore | HcBanItem (GObject) | GtkColumnView | 3 columns, sorting |
| dccgui.c | GtkTreeView + GtkListStore | HcDccItem (GObject) | GtkColumnView | Icons, colors, real-time updates |
| userlistgui.c | GtkTreeView + GtkListStore | HcUserItem (GObject) | GtkColumnView | Icons, colors, sorting, DND |
| chanlist.c | GtkTreeView + Custom Model | HcChannelItem (GObject) | GtkColumnView | Filtering, sorting |
| chanview-tree.c | GtkTreeView + GtkTreeStore | HcChanItem (GObject) | GtkTreeListModel + GtkListView | Hierarchical with expanders |

---

## Custom GObject Types Created

Each migrated list view (except simple string lists) required a custom GObject type to hold row data:

```c
// Example: HcPluginItem
struct _HcPluginItem {
    GObject parent;
    gchar *name;
    gchar *version;
    gchar *file;
    gchar *desc;
    gchar *filepath;
};
G_DEFINE_TYPE(HcPluginItem, hc_plugin_item, G_TYPE_OBJECT)
```

**Types created:**
- `HcPluginItem` - Plugin name, version, file, description
- `HcEditItem` - Name/command pairs for editable lists
- `HcEventItem` - Event name, text, row index
- `HcIgnoreItem` - Mask + 7 boolean flags
- `HcBanItem` - Mask, who set, time
- `HcDccItem` - File, size, position, speed, etc.
- `HcUserItem` - Nick, hostname, icon texture, color
- `HcChannelItem` - Channel name, topic, user count
- `HcChanItem` - Chan pointer, children store, is_server flag

---

## Helper Functions (gtk-compat.h)

```c
#if HC_GTK4
/* Create views */
GtkWidget *hc_column_view_new_simple(GListModel *model, GtkSelectionMode mode);
GtkColumnViewColumn *hc_column_view_add_column(GtkColumnView *view,
    const char *title, GCallback setup_cb, GCallback bind_cb, gpointer user_data);

/* Selection */
gpointer hc_selection_model_get_selected_item(GtkSelectionModel *model);
guint hc_selection_model_get_selected_position(GtkSelectionModel *model);

/* Icons */
GdkTexture *hc_pixbuf_to_texture(GdkPixbuf *pixbuf);
#endif
```

---

## GTK4 List Architecture Pattern

```c
#if HC_GTK4

// 1. Define GObject item type
typedef struct { GObject parent; char *text; } HcMyItem;
G_DEFINE_TYPE(HcMyItem, hc_my_item, G_TYPE_OBJECT)

// 2. Factory setup - create widgets
static void setup_cb(GtkListItemFactory *f, GtkListItem *item) {
    gtk_list_item_set_child(item, gtk_label_new(NULL));
}

// 3. Factory bind - populate widgets from data
static void bind_cb(GtkListItemFactory *f, GtkListItem *item) {
    GtkWidget *label = gtk_list_item_get_child(item);
    HcMyItem *data = gtk_list_item_get_item(item);
    gtk_label_set_text(GTK_LABEL(label), data->text);
}

// 4. Create view with model
GListStore *store = g_list_store_new(HC_TYPE_MY_ITEM);
GtkWidget *view = hc_column_view_new_simple(G_LIST_MODEL(store), GTK_SELECTION_SINGLE);
hc_column_view_add_column(GTK_COLUMN_VIEW(view), "Column", setup_cb, bind_cb, NULL);

#else
// GTK3 code unchanged
#endif
```

---

## Key GTK4 APIs Used

- `GListStore` - Generic GObject-based list model
- `GtkStringList` - Simple string-only model
- `GtkListView` - Single-column list
- `GtkColumnView` - Multi-column table
- `GtkSignalListItemFactory` - Widget factory with setup/bind signals
- `GtkSingleSelection` / `GtkMultiSelection` - Selection handling
- `GtkSortListModel` + `GtkCustomSorter` - Sorting
- `GtkFilterListModel` + `GtkCustomFilter` - Filtering
- `GtkTreeListModel` + `GtkTreeExpander` - Hierarchical lists

---

## Files Modified

**List View Files:**
- `src/fe-gtk/urlgrab.c`
- `src/fe-gtk/plugingui.c`
- `src/fe-gtk/notifygui.c`
- `src/fe-gtk/editlist.c`
- `src/fe-gtk/textgui.c`
- `src/fe-gtk/ignoregui.c`
- `src/fe-gtk/banlist.c`
- `src/fe-gtk/dccgui.c`
- `src/fe-gtk/userlistgui.c`
- `src/fe-gtk/chanlist.c`
- `src/fe-gtk/chanview-tree.c`
- `src/fe-gtk/custom-list.c` (HcChannelItem GObject)
- `src/fe-gtk/custom-list.h`

**Support Files:**
- `src/fe-gtk/gtk-compat.h` - Helper functions
- `src/fe-gtk/fe-gtk.h` - Updated `sess->res->user_model` type
