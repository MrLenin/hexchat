/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * GTK3/GTK4 Compatibility Layer for HexChat
 *
 * This header provides macros and inline functions to abstract differences
 * between GTK3 and GTK4, allowing gradual migration with minimal code churn.
 */

#ifndef HEXCHAT_GTK_COMPAT_H
#define HEXCHAT_GTK_COMPAT_H

#include "config.h"
#include <gtk/gtk.h>

/*
 * Version detection
 * HC_GTK4 can be defined on the command line (e.g., from hexchat.props or meson)
 * or via USE_GTK4 from config.h
 */
#if !defined(HC_GTK4)
#  ifdef USE_GTK4
#    define HC_GTK4 1
#  else
#    define HC_GTK4 0
#  endif
#endif

#if HC_GTK4
#  define HC_GTK3 0
#else
#  define HC_GTK3 1
#endif

/*
 * =============================================================================
 * Box/Container Packing Macros
 * =============================================================================
 * GTK3: gtk_box_pack_start(box, child, expand, fill, padding)
 * GTK4: gtk_box_append(box, child) - no expand/fill/padding params
 *
 * In GTK4, use widget properties (hexpand, vexpand, halign, valign) instead.
 */

#if HC_GTK4

/*
 * In GTK4, box packing uses expand properties on the child widget.
 * We need to set the correct expand property based on box orientation:
 * - Horizontal box: set hexpand
 * - Vertical box: set vexpand
 */
static inline void
hc_box_pack_start_impl (GtkBox *box, GtkWidget *child, gboolean expand)
{
	if (expand)
	{
		GtkOrientation orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (box));
		if (orient == GTK_ORIENTATION_HORIZONTAL)
			gtk_widget_set_hexpand (child, TRUE);
		else
			gtk_widget_set_vexpand (child, TRUE);
	}
	gtk_box_append (box, child);
}

static inline void
hc_box_pack_end_impl (GtkBox *box, GtkWidget *child, gboolean expand)
{
	if (expand)
	{
		GtkOrientation orient = gtk_orientable_get_orientation (GTK_ORIENTABLE (box));
		if (orient == GTK_ORIENTATION_HORIZONTAL)
			gtk_widget_set_hexpand (child, TRUE);
		else
			gtk_widget_set_vexpand (child, TRUE);
	}
	gtk_box_prepend (box, child);
}

#define hc_box_pack_start(box, child, expand, fill, padding) \
	hc_box_pack_start_impl(GTK_BOX(box), child, expand)

#define hc_box_pack_end(box, child, expand, fill, padding) \
	hc_box_pack_end_impl(GTK_BOX(box), child, expand)

#define hc_box_remove(box, child) \
	gtk_box_remove(GTK_BOX(box), child)

#else /* GTK3 */

#define hc_box_pack_start(box, child, expand, fill, padding) \
	gtk_box_pack_start(GTK_BOX(box), child, expand, fill, padding)

#define hc_box_pack_end(box, child, expand, fill, padding) \
	gtk_box_pack_end(GTK_BOX(box), child, expand, fill, padding)

#define hc_box_remove(box, child) \
	gtk_container_remove(GTK_CONTAINER(box), child)

#endif

/*
 * =============================================================================
 * Container Add/Remove Macros
 * =============================================================================
 * GTK3: gtk_container_add(container, widget)
 * GTK4: Use specific container methods (gtk_box_append, gtk_window_set_child, etc.)
 */

#if HC_GTK4

/* For windows */
#define hc_window_set_child(window, child) \
	gtk_window_set_child(GTK_WINDOW(window), child)

/* For scrolled windows */
#define hc_scrolled_window_set_child(sw, child) \
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), child)

/* For frames */
#define hc_frame_set_child(frame, child) \
	gtk_frame_set_child(GTK_FRAME(frame), child)

/* For buttons with custom children (labels, images, etc.) */
#define hc_button_set_child(button, child) \
	gtk_button_set_child(GTK_BUTTON(button), child)

/* For event boxes - GTK4 doesn't have GtkEventBox, use GtkBox instead
 * and add controllers for events. For now, provide a simple wrapper. */
#define hc_event_box_set_child(eventbox, child) \
	gtk_box_append(GTK_BOX(eventbox), child)

/* Create an "event box" - in GTK4 this is just a regular box */
#define hc_event_box_new() \
	gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)

/* Generic add child to a box (no packing options) */
#define hc_box_add(box, child) \
	gtk_box_append(GTK_BOX(box), child)

/* For viewports */
#define hc_viewport_set_child(viewport, child) \
	gtk_viewport_set_child(GTK_VIEWPORT(viewport), child)

#else /* GTK3 */

#define hc_window_set_child(window, child) \
	gtk_container_add(GTK_CONTAINER(window), child)

#define hc_scrolled_window_set_child(sw, child) \
	gtk_container_add(GTK_CONTAINER(sw), child)

#define hc_frame_set_child(frame, child) \
	gtk_container_add(GTK_CONTAINER(frame), child)

/* For buttons with custom children */
#define hc_button_set_child(button, child) \
	gtk_container_add(GTK_CONTAINER(button), child)

/* For event boxes */
#define hc_event_box_set_child(eventbox, child) \
	gtk_container_add(GTK_CONTAINER(eventbox), child)

#define hc_event_box_new() \
	gtk_event_box_new()

/* Generic add child to a box (no packing options) */
#define hc_box_add(box, child) \
	gtk_container_add(GTK_CONTAINER(box), child)

/* For viewports */
#define hc_viewport_set_child(viewport, child) \
	gtk_container_add(GTK_CONTAINER(viewport), child)

#endif

/*
 * =============================================================================
 * Widget Visibility
 * =============================================================================
 * GTK3: Widgets start hidden, need gtk_widget_show()/gtk_widget_show_all()
 * GTK4: Widgets start visible, no gtk_widget_show_all()
 *
 * IMPORTANT: GTK4 removed gtk_widget_show_all(). In GTK3, this function
 * recursively showed all children. We implement a replacement that does
 * the same by iterating through children.
 */

#if HC_GTK4

#define hc_widget_show(widget) \
	gtk_widget_set_visible(widget, TRUE)

/*
 * GTK4 replacement for gtk_widget_show_all() - recursively shows widget and children
 * In GTK4, widgets start visible by default, but some may have been explicitly hidden.
 * Always set visible to ensure all children are shown.
 */
static inline void
hc_gtk4_widget_show_all_recursive (GtkWidget *widget)
{
	GtkWidget *child;

	if (widget == NULL)
		return;

	/* Always set visible to ensure widget is shown */
	gtk_widget_set_visible (widget, TRUE);

	/* Iterate through children using GTK4's child iteration API */
	for (child = gtk_widget_get_first_child (widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child))
	{
		hc_gtk4_widget_show_all_recursive (child);
	}
}

#define hc_widget_show_all(widget) \
	hc_gtk4_widget_show_all_recursive(widget)

#define hc_widget_hide(widget) \
	gtk_widget_set_visible(widget, FALSE)

#else /* GTK3 */

#define hc_widget_show(widget) \
	gtk_widget_show(widget)

#define hc_widget_show_all(widget) \
	gtk_widget_show_all(widget)

#define hc_widget_hide(widget) \
	gtk_widget_hide(widget)

#endif

/*
 * =============================================================================
 * Widget Destruction
 * =============================================================================
 * GTK3: gtk_widget_destroy() for all widgets
 * GTK4: gtk_window_destroy() for windows, unparent for others
 */

#if HC_GTK4

/*
 * In GTK4, widgets must be unparented before they can be destroyed.
 * For windows, use gtk_window_destroy().
 * For other widgets, unparent first (if parented) - this will destroy
 * the widget since the parent holds the only reference.
 * If not parented, the widget has a floating ref that needs to be sunk.
 */
static inline void
hc_widget_destroy_impl (GtkWidget *widget)
{
	if (GTK_IS_WINDOW (widget))
	{
		gtk_window_destroy (GTK_WINDOW (widget));
	}
	else
	{
		if (gtk_widget_get_parent (widget) != NULL)
		{
			/* Unparenting will destroy the widget as the parent
			 * releases its reference */
			gtk_widget_unparent (widget);
		}
		else
		{
			/* Widget was never parented - it has a floating ref.
			 * Sink it and then unref to destroy. */
			g_object_ref_sink (widget);
			g_object_unref (widget);
		}
	}
}

#define hc_widget_destroy(widget) \
	hc_widget_destroy_impl(GTK_WIDGET(widget))

/*
 * In GTK4, gtk_window_close() allows proper cleanup and event processing
 * before destruction, which is safer than gtk_window_destroy() for windows
 * that may have pending events.
 */
#define hc_window_destroy(window) \
	gtk_window_close(GTK_WINDOW(window))

#else /* GTK3 */

#define hc_widget_destroy(widget) \
	gtk_widget_destroy(widget)

#define hc_window_destroy(window) \
	gtk_widget_destroy(GTK_WIDGET(window))

#endif

/*
 * =============================================================================
 * Box Creation
 * =============================================================================
 * GTK3: gtk_box_new(orientation, spacing)
 * GTK4: Same, but default spacing is different
 */

#define hc_hbox_new(spacing) \
	gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing)

#define hc_vbox_new(spacing) \
	gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing)

/*
 * =============================================================================
 * Button Creation
 * =============================================================================
 * Minor differences in button creation API
 */

#if HC_GTK4

static inline GtkWidget *
hc_button_new_with_label(const char *label)
{
	GtkWidget *btn = gtk_button_new();
	gtk_button_set_label(GTK_BUTTON(btn), label);
	return btn;
}

#else /* GTK3 */

#define hc_button_new_with_label(label) \
	gtk_button_new_with_label(label)

#endif

/*
 * =============================================================================
 * Scrolled Window
 * =============================================================================
 * GTK3: gtk_scrolled_window_new(hadjustment, vadjustment)
 * GTK4: gtk_scrolled_window_new() - no adjustment params
 */

#if HC_GTK4

#define hc_scrolled_window_new() \
	gtk_scrolled_window_new()

#else /* GTK3 */

#define hc_scrolled_window_new() \
	gtk_scrolled_window_new(NULL, NULL)

#endif

/*
 * =============================================================================
 * Event Handling Compatibility
 * =============================================================================
 * GTK3: Event signals (button-press-event, key-press-event, etc.)
 * GTK4: Event controllers (GtkGestureClick, GtkEventControllerKey, etc.)
 *
 * These are helper functions to create and attach event controllers.
 * The actual signal handlers need to be updated for GTK4's different signatures.
 */

#if HC_GTK4

/* Helper to add click gesture (replaces button-press/release-event) */
static inline GtkGesture *
hc_add_click_gesture(GtkWidget *widget,
                     GCallback pressed_cb,
                     GCallback released_cb,
                     gpointer user_data)
{
	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0); /* All buttons */
	if (pressed_cb)
		g_signal_connect(gesture, "pressed", pressed_cb, user_data);
	if (released_cb)
		g_signal_connect(gesture, "released", released_cb, user_data);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(gesture));
	return gesture;
}

/* Helper to add motion controller (replaces motion-notify-event) */
static inline GtkEventController *
hc_add_motion_controller(GtkWidget *widget,
                         GCallback enter_cb,
                         GCallback motion_cb,
                         GCallback leave_cb,
                         gpointer user_data)
{
	GtkEventController *controller = gtk_event_controller_motion_new();
	if (enter_cb)
		g_signal_connect(controller, "enter", enter_cb, user_data);
	if (motion_cb)
		g_signal_connect(controller, "motion", motion_cb, user_data);
	if (leave_cb)
		g_signal_connect(controller, "leave", leave_cb, user_data);
	gtk_widget_add_controller(widget, controller);
	return controller;
}

/* Helper to add scroll controller (replaces scroll-event) */
static inline GtkEventController *
hc_add_scroll_controller(GtkWidget *widget,
                         GCallback scroll_cb,
                         gpointer user_data)
{
	GtkEventController *controller = gtk_event_controller_scroll_new(
		GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	if (scroll_cb)
		g_signal_connect(controller, "scroll", scroll_cb, user_data);
	gtk_widget_add_controller(widget, controller);
	return controller;
}

/* Helper to add key controller (replaces key-press/release-event) */
static inline GtkEventController *
hc_add_key_controller(GtkWidget *widget,
                      GCallback pressed_cb,
                      GCallback released_cb,
                      gpointer user_data)
{
	GtkEventController *controller = gtk_event_controller_key_new();
	if (pressed_cb)
		g_signal_connect(controller, "key-pressed", pressed_cb, user_data);
	if (released_cb)
		g_signal_connect(controller, "key-released", released_cb, user_data);
	gtk_widget_add_controller(widget, controller);
	return controller;
}

/* Helper to add focus controller (replaces focus-in/out-event) */
static inline GtkEventController *
hc_add_focus_controller(GtkWidget *widget,
                        GCallback enter_cb,
                        GCallback leave_cb,
                        gpointer user_data)
{
	GtkEventController *controller = gtk_event_controller_focus_new();
	if (enter_cb)
		g_signal_connect(controller, "enter", enter_cb, user_data);
	if (leave_cb)
		g_signal_connect(controller, "leave", leave_cb, user_data);
	gtk_widget_add_controller(widget, controller);
	return controller;
}

/* Helper to add crossing controller (replaces enter/leave-notify-event) */
static inline GtkEventController *
hc_add_crossing_controller(GtkWidget *widget,
                           GCallback enter_cb,
                           GCallback leave_cb,
                           gpointer user_data)
{
	/* In GTK4, GtkEventControllerMotion handles enter/leave */
	GtkEventController *controller = gtk_event_controller_motion_new();
	if (enter_cb)
		g_signal_connect(controller, "enter", enter_cb, user_data);
	if (leave_cb)
		g_signal_connect(controller, "leave", leave_cb, user_data);
	gtk_widget_add_controller(widget, controller);
	return controller;
}

#endif /* HC_GTK4 */

/*
 * =============================================================================
 * Container Children Iteration
 * =============================================================================
 * GTK3: gtk_container_get_children() returns GList*
 * GTK4: Use gtk_widget_get_first_child()/gtk_widget_get_next_sibling()
 */

#if HC_GTK4

/* Helper to get children list in GTK4 - caller must free with g_list_free() */
static inline GList *
hc_container_get_children(GtkWidget *container)
{
	GList *list = NULL;
	GtkWidget *child;

	for (child = gtk_widget_get_first_child(container);
	     child != NULL;
	     child = gtk_widget_get_next_sibling(child))
	{
		list = g_list_append(list, child);
	}
	return list;
}

#else /* GTK3 */

#define hc_container_get_children(container) \
	gtk_container_get_children(GTK_CONTAINER(container))

#endif

/*
 * =============================================================================
 * GdkDisplay/Screen Compatibility
 * =============================================================================
 * GTK3: GdkScreen is primary
 * GTK4: GdkDisplay is primary, no GdkScreen
 */

#if HC_GTK4

#define hc_get_default_display() \
	gdk_display_get_default()

/* CSS provider uses GdkDisplay in GTK4 */
#define hc_style_context_add_provider_for_display(provider, priority) \
	gtk_style_context_add_provider_for_display( \
		gdk_display_get_default(), \
		GTK_STYLE_PROVIDER(provider), \
		priority)

#else /* GTK3 */

#define hc_get_default_display() \
	gdk_display_get_default()

#define hc_style_context_add_provider_for_display(provider, priority) \
	gtk_style_context_add_provider_for_screen( \
		gdk_screen_get_default(), \
		GTK_STYLE_PROVIDER(provider), \
		priority)

#endif

/*
 * =============================================================================
 * Message Dialog Compatibility
 * =============================================================================
 * GTK3: GtkMessageDialog with gtk_dialog_run()
 * GTK4: GtkAlertDialog (async) or GtkMessageDialog without gtk_dialog_run()
 */

#if HC_GTK4

/* In GTK4, dialogs are async-only. This requires callback-based handling. */
/* For now, provide a simple wrapper that creates a similar dialog. */
/* Actual usage will need refactoring for async patterns. */

#define hc_message_dialog_new(parent, flags, type, buttons, format, ...) \
	gtk_message_dialog_new(parent, flags, type, buttons, format, ##__VA_ARGS__)

/* GTK4 has no gtk_dialog_run - must use async response handling */
#define HC_DIALOG_REQUIRES_ASYNC 1

#else /* GTK3 */

#define hc_message_dialog_new(parent, flags, type, buttons, format, ...) \
	gtk_message_dialog_new(parent, flags, type, buttons, format, ##__VA_ARGS__)

#define HC_DIALOG_REQUIRES_ASYNC 0

#endif

/*
 * =============================================================================
 * Entry/Text Widget Compatibility
 * =============================================================================
 */

#if HC_GTK4

/* GtkEntry buffer access changes slightly */
#define hc_entry_get_text(entry) \
	gtk_editable_get_text(GTK_EDITABLE(entry))

#define hc_entry_set_text(entry, text) \
	gtk_editable_set_text(GTK_EDITABLE(entry), text)

#else /* GTK3 */

#define hc_entry_get_text(entry) \
	gtk_entry_get_text(GTK_ENTRY(entry))

#define hc_entry_set_text(entry, text) \
	gtk_entry_set_text(GTK_ENTRY(entry), text)

#endif

/*
 * =============================================================================
 * Clipboard Compatibility
 * =============================================================================
 * GTK3: gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)
 * GTK4: gdk_display_get_clipboard(display)
 */

#if HC_GTK4

#define hc_clipboard_get_default() \
	gdk_display_get_clipboard(gdk_display_get_default())

/* GTK4 clipboard is async-only */
#define HC_CLIPBOARD_ASYNC 1

#else /* GTK3 */

#define hc_clipboard_get_default() \
	gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)

#define HC_CLIPBOARD_ASYNC 0

#endif

/*
 * =============================================================================
 * Drag and Drop Compatibility
 * =============================================================================
 * GTK3: gtk_drag_dest_set(), etc.
 * GTK4: GtkDropTarget and GtkDragSource controllers
 *
 * GTK4 uses GtkDropTarget for receiving drops and GtkDragSource for initiating drags.
 * Helper functions provided here for common drop patterns.
 */

#if HC_GTK4
#define HC_DND_NEW_API 1

/*
 * hc_add_file_drop_target:
 * Adds a GtkDropTarget that accepts text/uri-list (file drops).
 * The callback receives: (GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data)
 * Returns: the GtkDropTarget controller (caller can connect additional signals if needed)
 */
static inline GtkDropTarget *
hc_add_file_drop_target (GtkWidget *widget, GCallback drop_cb, gpointer user_data)
{
	GtkDropTarget *target;

	target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (target, "drop", drop_cb, user_data);
	gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (target));

	return target;
}

/*
 * hc_add_string_drop_target:
 * Adds a GtkDropTarget that accepts a specific content type string.
 * Used for internal app DND like layout swapping.
 */
static inline GtkDropTarget *
hc_add_string_drop_target (GtkWidget *widget, const char *content_type,
                           GCallback drop_cb, gpointer user_data)
{
	GtkDropTarget *target;

	target = gtk_drop_target_new (G_TYPE_STRING, GDK_ACTION_MOVE);
	g_signal_connect (target, "drop", drop_cb, user_data);
	gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (target));

	return target;
}

/*
 * hc_add_drag_source:
 * Adds a GtkDragSource for initiating drags.
 */
static inline GtkDragSource *
hc_add_drag_source (GtkWidget *widget, GCallback prepare_cb, gpointer user_data)
{
	GtkDragSource *source;

	source = gtk_drag_source_new ();
	g_signal_connect (source, "prepare", prepare_cb, user_data);
	gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (source));

	return source;
}

#else
#define HC_DND_NEW_API 0
#endif

/*
 * =============================================================================
 * Notebook/TabView Compatibility
 * =============================================================================
 * Minor API changes between versions
 */

#if HC_GTK4

#define hc_notebook_append_page(notebook, child, tab_label) \
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child, tab_label)

#define hc_notebook_remove_page(notebook, page_num) \
	gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page_num)

#else /* GTK3 */

#define hc_notebook_append_page(notebook, child, tab_label) \
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child, tab_label)

#define hc_notebook_remove_page(notebook, page_num) \
	gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), page_num)

#endif

/*
 * =============================================================================
 * Paned Widget Compatibility
 * =============================================================================
 */

#if HC_GTK4

/*
 * GTK4: gtk_paned_pack1/pack2 replaced with set_start_child/set_end_child
 * plus separate resize/shrink property setters
 */
static inline void
hc_gtk4_paned_pack1 (GtkPaned *paned, GtkWidget *child, gboolean resize, gboolean shrink)
{
	gtk_paned_set_start_child (paned, child);
	gtk_paned_set_resize_start_child (paned, resize);
	gtk_paned_set_shrink_start_child (paned, shrink);
}

static inline void
hc_gtk4_paned_pack2 (GtkPaned *paned, GtkWidget *child, gboolean resize, gboolean shrink)
{
	gtk_paned_set_end_child (paned, child);
	gtk_paned_set_resize_end_child (paned, resize);
	gtk_paned_set_shrink_end_child (paned, shrink);
}

#define hc_paned_pack1(paned, child, resize, shrink) \
	hc_gtk4_paned_pack1(GTK_PANED(paned), child, resize, shrink)

#define hc_paned_pack2(paned, child, resize, shrink) \
	hc_gtk4_paned_pack2(GTK_PANED(paned), child, resize, shrink)

#else /* GTK3 */

#define hc_paned_pack1(paned, child, resize, shrink) \
	gtk_paned_pack1(GTK_PANED(paned), child, resize, shrink)

#define hc_paned_pack2(paned, child, resize, shrink) \
	gtk_paned_pack2(GTK_PANED(paned), child, resize, shrink)

#endif

/*
 * =============================================================================
 * Widget Properties
 * =============================================================================
 * Helpers for common widget property settings
 */

static inline void
hc_widget_set_margin_all(GtkWidget *widget, int margin)
{
	gtk_widget_set_margin_start(widget, margin);
	gtk_widget_set_margin_end(widget, margin);
	gtk_widget_set_margin_top(widget, margin);
	gtk_widget_set_margin_bottom(widget, margin);
}

/*
 * =============================================================================
 * Color/Theme Compatibility
 * =============================================================================
 * GdkRGBA is the same in both, but usage contexts differ
 */

/* No changes needed - GdkRGBA works the same in GTK3 and GTK4 */

/*
 * =============================================================================
 * Cairo/Drawing Compatibility
 * =============================================================================
 * GTK3: Draw signal with cairo_t
 * GTK4: Snapshot API (GtkSnapshot), but can still use cairo via gtk_snapshot_append_cairo()
 */

#if HC_GTK4
#define HC_USE_SNAPSHOT_API 1
#else
#define HC_USE_SNAPSHOT_API 0
#endif

/*
 * =============================================================================
 * Widget Dimensions Compatibility
 * =============================================================================
 * GTK3: gdk_window_get_width/height(gtk_widget_get_window(widget))
 * GTK4: gtk_widget_get_width/height(widget)
 */

#if HC_GTK4

#define hc_widget_get_width(widget) \
	gtk_widget_get_width(widget)

#define hc_widget_get_height(widget) \
	gtk_widget_get_height(widget)

#else /* GTK3 */

#define hc_widget_get_width(widget) \
	gdk_window_get_width(gtk_widget_get_window(widget))

#define hc_widget_get_height(widget) \
	gdk_window_get_height(gtk_widget_get_window(widget))

#endif

/*
 * =============================================================================
 * Menu/Popup Compatibility
 * =============================================================================
 * GTK3: GtkMenu with gtk_menu_popup()
 * GTK4: GtkPopoverMenu with GMenu/GMenuModel
 *
 * For context menus, GTK4 uses a completely different approach:
 * - GtkPopoverMenu displays menus from GMenuModel
 * - Actions are registered on GtkApplication or widget action groups
 *
 * Strategy: Wrap GTK3-style menu code in #if !HC_GTK4 blocks.
 * GTK4 menu implementations will be added separately.
 *
 * NOTE: Context menus (right-click menus) require complete rewrite for GTK4.
 * The menu bar can potentially use GtkMenuBar replacement approaches.
 */

#if HC_GTK4

/*
 * Helper to create and show a popover menu at a given position
 * parent: widget to attach popover to
 * menu_model: GMenuModel describing the menu
 * x, y: position relative to parent widget
 */
static inline GtkWidget *
hc_popover_menu_popup_at (GtkWidget *parent, GMenuModel *menu_model, double x, double y)
{
	GtkWidget *popover = gtk_popover_menu_new_from_model (menu_model);
	GdkRectangle rect = { (int)x, (int)y, 1, 1 };

	gtk_widget_set_parent (popover, parent);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
	gtk_popover_popup (GTK_POPOVER (popover));

	return popover;
}

#endif

/*
 * =============================================================================
 * TreeView/ListView Compatibility
 * =============================================================================
 * GTK3: GtkTreeView with GtkListStore/GtkTreeStore
 * GTK4: GtkTreeView still works but is deprecated; GtkColumnView is the replacement
 *
 * For now, we keep using GtkTreeView in GTK4 as it's still functional.
 * Full migration to GtkColumnView/GtkListView would be a major rewrite.
 *
 * These macros handle APIs that were removed or changed in GTK4.
 */

#if HC_GTK4

/*
 * gtk_tree_view_set_rules_hint() - Removed in GTK4
 * This was used to enable alternating row colors (zebra striping).
 * In GTK4, use CSS styling instead: treeview row:nth-child(even) { ... }
 * For now, we just make it a no-op.
 */
#define hc_tree_view_set_rules_hint(view, setting) ((void)0)

/*
 * gtk_tree_view_get_vadjustment() - Deprecated, use GtkScrollable interface
 * GtkTreeView implements GtkScrollable in both GTK3 and GTK4.
 */
#define hc_tree_view_get_vadjustment(treeview) \
	gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(treeview))

#define hc_tree_view_get_hadjustment(treeview) \
	gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(treeview))

#else /* GTK3 */

#define hc_tree_view_set_rules_hint(view, setting) \
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), setting)

#define hc_tree_view_get_vadjustment(treeview) \
	gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(treeview))

#define hc_tree_view_get_hadjustment(treeview) \
	gtk_tree_view_get_hadjustment(GTK_TREE_VIEW(treeview))

#endif

/*
 * =============================================================================
 * Container Border Width Compatibility
 * =============================================================================
 * GTK3: gtk_container_set_border_width(container, border)
 * GTK4: Use widget margins instead (set_margin_start/end/top/bottom)
 */

#if HC_GTK4

#define hc_container_set_border_width(container, border) \
	do { \
		gtk_widget_set_margin_start(GTK_WIDGET(container), border); \
		gtk_widget_set_margin_end(GTK_WIDGET(container), border); \
		gtk_widget_set_margin_top(GTK_WIDGET(container), border); \
		gtk_widget_set_margin_bottom(GTK_WIDGET(container), border); \
	} while (0)

#else /* GTK3 */

#define hc_container_set_border_width(container, border) \
	gtk_container_set_border_width(GTK_CONTAINER(container), border)

#endif

/*
 * =============================================================================
 * Window Position Compatibility
 * =============================================================================
 * GTK3: gtk_window_set_position(window, position)
 * GTK4: Removed - window manager handles placement. No replacement.
 */

#if HC_GTK4

/* No-op in GTK4 - window manager handles positioning */
#define hc_window_set_position(window, position) ((void)0)

#else /* GTK3 */

#define hc_window_set_position(window, position) \
	gtk_window_set_position(GTK_WINDOW(window), position)

#endif

/*
 * =============================================================================
 * Scrolled Window Creation Compatibility
 * =============================================================================
 * GTK3: gtk_scrolled_window_new(hadjustment, vadjustment) - typically (NULL, NULL)
 * GTK4: gtk_scrolled_window_new() - no parameters
 */

#if HC_GTK4

#define hc_scrolled_window_new() \
	gtk_scrolled_window_new()

#else /* GTK3 */

#define hc_scrolled_window_new() \
	gtk_scrolled_window_new(NULL, NULL)

#endif

/*
 * =============================================================================
 * Icon Image Compatibility
 * =============================================================================
 * GTK3: gtk_image_new_from_icon_name(icon_name, size) - takes GtkIconSize
 * GTK4: gtk_image_new_from_icon_name(icon_name) - no size param, use set_icon_size
 */

#if HC_GTK4

/* In GTK4, create image and set size separately.
 * Note: GTK4 uses GTK_ICON_SIZE_NORMAL, GTK_ICON_SIZE_LARGE, GTK_ICON_SIZE_INHERIT
 * GTK3 GTK_ICON_SIZE_MENU/BUTTON/SMALL_TOOLBAR → GTK_ICON_SIZE_NORMAL
 * GTK3 GTK_ICON_SIZE_LARGE_TOOLBAR/DND → GTK_ICON_SIZE_LARGE
 * GTK3 GTK_ICON_SIZE_DIALOG → GTK_ICON_SIZE_LARGE
 */
static inline GtkWidget *
hc_image_new_from_icon_name(const char *icon_name, int gtk3_size)
{
	GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
	/* Map GTK3 icon sizes to GTK4 */
	GtkIconSize gtk4_size = GTK_ICON_SIZE_NORMAL;
	/* GTK3: MENU=1, SMALL_TOOLBAR=2, LARGE_TOOLBAR=3, BUTTON=4, DND=5, DIALOG=6 */
	if (gtk3_size >= 3) /* LARGE_TOOLBAR, DND, DIALOG */
		gtk4_size = GTK_ICON_SIZE_LARGE;
	gtk_image_set_icon_size(GTK_IMAGE(image), gtk4_size);
	return image;
}

#else /* GTK3 */

#define hc_image_new_from_icon_name(icon_name, size) \
	gtk_image_new_from_icon_name(icon_name, size)

#endif

/*
 * =============================================================================
 * ButtonBox Compatibility
 * =============================================================================
 * GTK3: GtkButtonBox with gtk_button_box_set_layout()
 * GTK4: GtkButtonBox removed - use GtkBox with CSS or widget halign/valign
 */

#if HC_GTK4

/* In GTK4, GtkButtonBox is removed. Use a regular GtkBox.
 * Layout hints are ignored - use CSS or widget alignment properties. */
#define hc_button_box_new(orientation) \
	gtk_box_new(orientation, 6)

#define hc_button_box_set_layout(bbox, layout) ((void)0)

#else /* GTK3 */

#define hc_button_box_new(orientation) \
	gtk_button_box_new(orientation)

#define hc_button_box_set_layout(bbox, layout) \
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), layout)

#endif

/*
 * =============================================================================
 * Dialog Action Area Compatibility
 * =============================================================================
 * GTK3: gtk_dialog_get_action_area() to access button area
 * GTK4: Removed - use gtk_dialog_add_button() or add_action_widget()
 */

#if HC_GTK4

/* In GTK4, action area is not directly accessible.
 * Code should use gtk_dialog_add_button() instead. */
/* No macro - code must be rewritten to use gtk_dialog_add_button() */

#endif

/*
 * =============================================================================
 * Shadow Type Compatibility
 * =============================================================================
 * GTK3: gtk_scrolled_window_set_shadow_type(), gtk_viewport_set_shadow_type()
 * GTK4: Removed - use CSS styling instead
 */

#if HC_GTK4

#define hc_scrolled_window_set_shadow_type(sw, type) ((void)0)
#define hc_viewport_set_shadow_type(viewport, type) ((void)0)

/*
 * GTK4 removed GtkShadowType enum - define them as no-ops for code compatibility.
 * In GTK4, shadow/frame styles are controlled via CSS.
 */
#ifndef GTK_SHADOW_NONE
#define GTK_SHADOW_NONE 0
#define GTK_SHADOW_IN 1
#define GTK_SHADOW_OUT 2
#define GTK_SHADOW_ETCHED_IN 3
#define GTK_SHADOW_ETCHED_OUT 4
#endif

/*
 * GTK4 removed gtk_scrolled_window_set_shadow_type - provide a no-op wrapper
 * so that code using the raw GTK function still compiles.
 */
#define gtk_scrolled_window_set_shadow_type(sw, type) ((void)0)
#define gtk_frame_set_shadow_type(frame, type) ((void)0)

/*
 * GTK4 removed GTK_WIN_POS_* constants (window manager handles placement).
 */
#ifndef GTK_WIN_POS_NONE
#define GTK_WIN_POS_NONE 0
#define GTK_WIN_POS_CENTER 1
#define GTK_WIN_POS_MOUSE 2
#define GTK_WIN_POS_CENTER_ALWAYS 3
#define GTK_WIN_POS_CENTER_ON_PARENT 4
#endif

/*
 * GTK4 removed GtkButtonBox and its layout enum.
 * These are no-ops since hc_button_box_* macros handle the abstraction.
 */
#ifndef GTK_BUTTONBOX_SPREAD
#define GTK_BUTTONBOX_SPREAD 0
#define GTK_BUTTONBOX_EDGE 1
#define GTK_BUTTONBOX_START 2
#define GTK_BUTTONBOX_END 3
#define GTK_BUTTONBOX_CENTER 4
#define GTK_BUTTONBOX_EXPAND 5
#endif

/*
 * GTK4 removed GtkStateType enum (use state flags instead).
 * Define for compatibility - actual state changes use different mechanism.
 */
#ifndef GTK_STATE_NORMAL
#define GTK_STATE_NORMAL 0
#define GTK_STATE_ACTIVE 1
#define GTK_STATE_PRELIGHT 2
#define GTK_STATE_SELECTED 3
#define GTK_STATE_INSENSITIVE 4
#define GTK_STATE_INCONSISTENT 5
#define GTK_STATE_FOCUSED 6
#endif

/*
 * GTK4 removed gtk_widget_get_state/set_state - provide compatibility wrappers.
 * In GTK4, widget states are managed through CSS classes and GtkStateFlags.
 */
#define gtk_widget_get_state(widget) GTK_STATE_NORMAL
#define gtk_widget_set_state(widget, state) ((void)0)

#else /* GTK3 */

#define hc_scrolled_window_set_shadow_type(sw, type) \
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), type)

#define hc_viewport_set_shadow_type(viewport, type) \
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), type)

#endif

/*
 * =============================================================================
 * Misc/Label Alignment Compatibility
 * =============================================================================
 * GTK3: gtk_misc_set_alignment(misc, xalign, yalign)
 * GTK4: GtkMisc removed - use gtk_widget_set_halign/valign instead
 */

#if HC_GTK4

/* Map alignment values (0.0-1.0) to GtkAlign enum */
static inline void
hc_misc_set_alignment(GtkWidget *widget, float xalign, float yalign)
{
	/* Map xalign: 0.0 = START, 0.5 = CENTER, 1.0 = END */
	GtkAlign halign = GTK_ALIGN_CENTER;
	if (xalign < 0.25) halign = GTK_ALIGN_START;
	else if (xalign > 0.75) halign = GTK_ALIGN_END;

	/* Map yalign: 0.0 = START, 0.5 = CENTER, 1.0 = END */
	GtkAlign valign = GTK_ALIGN_CENTER;
	if (yalign < 0.25) valign = GTK_ALIGN_START;
	else if (yalign > 0.75) valign = GTK_ALIGN_END;

	gtk_widget_set_halign(widget, halign);
	gtk_widget_set_valign(widget, valign);
}

#else /* GTK3 */

#define hc_misc_set_alignment(widget, xalign, yalign) \
	gtk_misc_set_alignment(GTK_MISC(widget), xalign, yalign)

#endif

/*
 * =============================================================================
 * Entry Text Compatibility
 * =============================================================================
 * GTK3: gtk_entry_get_text() / gtk_entry_set_text()
 * GTK4: gtk_editable_get_text() / gtk_editable_set_text()
 */

#if HC_GTK4

#define hc_entry_get_text(entry) \
	gtk_editable_get_text(GTK_EDITABLE(entry))

#define hc_entry_set_text(entry, text) \
	gtk_editable_set_text(GTK_EDITABLE(entry), text)

#else /* GTK3 */

#define hc_entry_get_text(entry) \
	gtk_entry_get_text(GTK_ENTRY(entry))

#define hc_entry_set_text(entry, text) \
	gtk_entry_set_text(GTK_ENTRY(entry), text)

#endif

/*
 * =============================================================================
 * CheckButton/ToggleButton Compatibility
 * =============================================================================
 * GTK3: GtkCheckButton inherits from GtkToggleButton, use gtk_toggle_button_*
 * GTK4: GtkCheckButton no longer inherits from GtkToggleButton, use gtk_check_button_*
 *
 * Note: These macros are for GtkCheckButton widgets only.
 * For actual GtkToggleButton widgets, the gtk_toggle_button_* functions still work.
 */

#if HC_GTK4

#define hc_check_button_get_active(button) \
	gtk_check_button_get_active(GTK_CHECK_BUTTON(button))

#define hc_check_button_set_active(button, active) \
	gtk_check_button_set_active(GTK_CHECK_BUTTON(button), active)

#else /* GTK3 */

#define hc_check_button_get_active(button) \
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))

#define hc_check_button_set_active(button, active) \
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active)

#endif

/*
 * =============================================================================
 * GtkListView / GtkColumnView Compatibility (GTK4 only)
 * =============================================================================
 * GTK3: GtkTreeView with GtkListStore/GtkTreeStore
 * GTK4: GtkListView/GtkColumnView with GListModel
 *
 * These helpers simplify creating list views in GTK4. GtkTreeView still works
 * in GTK4 (deprecated) but GtkListView/GtkColumnView are preferred for new code.
 */

#if HC_GTK4

/*
 * hc_list_view_new_simple:
 * Create a simple GtkListView with a signal-based factory.
 *
 * @model: A GListModel (e.g., GtkStringList, GListStore)
 * @selection_mode: GTK_SELECTION_NONE, GTK_SELECTION_SINGLE, GTK_SELECTION_MULTIPLE
 * @setup_cb: Factory setup callback (GtkListItemFactory*, GtkListItem*, gpointer)
 * @bind_cb: Factory bind callback (GtkListItemFactory*, GtkListItem*, gpointer)
 * @user_data: User data passed to callbacks
 *
 * Returns: (transfer full): A new GtkListView widget
 */
static inline GtkWidget *
hc_list_view_new_simple (GListModel *model,
                         GtkSelectionMode selection_mode,
                         GCallback setup_cb,
                         GCallback bind_cb,
                         gpointer user_data)
{
	GtkSelectionModel *sel_model;
	GtkListItemFactory *factory;
	GtkWidget *view;

	/* Create selection model based on mode */
	if (selection_mode == GTK_SELECTION_MULTIPLE)
		sel_model = GTK_SELECTION_MODEL (gtk_multi_selection_new (model));
	else if (selection_mode == GTK_SELECTION_SINGLE)
		sel_model = GTK_SELECTION_MODEL (gtk_single_selection_new (model));
	else
		sel_model = GTK_SELECTION_MODEL (gtk_no_selection_new (model));

	/* Create factory */
	factory = gtk_signal_list_item_factory_new ();
	if (setup_cb)
		g_signal_connect (factory, "setup", setup_cb, user_data);
	if (bind_cb)
		g_signal_connect (factory, "bind", bind_cb, user_data);

	view = gtk_list_view_new (sel_model, factory);

	return view;
}

/*
 * hc_column_view_new_simple:
 * Create a GtkColumnView with a selection model.
 *
 * @model: A GListModel
 * @selection_mode: GTK_SELECTION_NONE, GTK_SELECTION_SINGLE, GTK_SELECTION_MULTIPLE
 *
 * Returns: (transfer full): A new GtkColumnView widget
 * Note: Caller must add columns using hc_column_view_add_*_column()
 */
static inline GtkWidget *
hc_column_view_new_simple (GListModel *model, GtkSelectionMode selection_mode)
{
	GtkSelectionModel *sel_model;

	if (selection_mode == GTK_SELECTION_MULTIPLE)
		sel_model = GTK_SELECTION_MODEL (gtk_multi_selection_new (model));
	else if (selection_mode == GTK_SELECTION_SINGLE)
		sel_model = GTK_SELECTION_MODEL (gtk_single_selection_new (model));
	else
		sel_model = GTK_SELECTION_MODEL (gtk_no_selection_new (model));

	return gtk_column_view_new (sel_model);
}

/*
 * hc_column_view_add_column:
 * Add a column to a GtkColumnView with custom factory callbacks.
 *
 * @view: The GtkColumnView
 * @title: Column title (can be NULL)
 * @setup_cb: Factory setup callback
 * @bind_cb: Factory bind callback
 * @user_data: User data for callbacks
 *
 * Returns: (transfer none): The new GtkColumnViewColumn
 */
static inline GtkColumnViewColumn *
hc_column_view_add_column (GtkColumnView *view,
                           const char *title,
                           GCallback setup_cb,
                           GCallback bind_cb,
                           gpointer user_data)
{
	GtkListItemFactory *factory;
	GtkColumnViewColumn *column;

	factory = gtk_signal_list_item_factory_new ();
	if (setup_cb)
		g_signal_connect (factory, "setup", setup_cb, user_data);
	if (bind_cb)
		g_signal_connect (factory, "bind", bind_cb, user_data);

	column = gtk_column_view_column_new (title, factory);
	gtk_column_view_append_column (view, column);

	return column;
}

/*
 * hc_selection_model_get_selected_item:
 * Get the first selected item from a selection model.
 *
 * @model: A GtkSelectionModel
 *
 * Returns: (transfer full) (nullable): The selected item, or NULL if none selected.
 *          Caller should g_object_unref() when done.
 */
static inline gpointer
hc_selection_model_get_selected_item (GtkSelectionModel *model)
{
	GtkBitset *selection;
	guint position;
	gpointer item = NULL;

	selection = gtk_selection_model_get_selection (model);
	if (!gtk_bitset_is_empty (selection))
	{
		position = gtk_bitset_get_nth (selection, 0);
		/* In GTK4, we get items directly from the selection model which is a GListModel */
		item = g_list_model_get_item (G_LIST_MODEL (model), position);
	}
	gtk_bitset_unref (selection);

	return item;
}

/*
 * hc_selection_model_get_selected_position:
 * Get the position of the first selected item.
 *
 * @model: A GtkSelectionModel
 *
 * Returns: Position of selected item, or GTK_INVALID_LIST_POSITION if none selected.
 */
static inline guint
hc_selection_model_get_selected_position (GtkSelectionModel *model)
{
	GtkBitset *selection;
	guint position = GTK_INVALID_LIST_POSITION;

	selection = gtk_selection_model_get_selection (model);
	if (!gtk_bitset_is_empty (selection))
		position = gtk_bitset_get_nth (selection, 0);
	gtk_bitset_unref (selection);

	return position;
}

/*
 * hc_pixbuf_to_texture:
 * Convert a GdkPixbuf to a GdkTexture (for use in GtkPicture).
 *
 * @pixbuf: The source GdkPixbuf
 *
 * Returns: (transfer full): A new GdkTexture, or NULL if pixbuf is NULL.
 */
static inline GdkTexture *
hc_pixbuf_to_texture (GdkPixbuf *pixbuf)
{
	if (!pixbuf)
		return NULL;
	return gdk_texture_new_for_pixbuf (pixbuf);
}

#endif /* HC_GTK4 */

/*
 * =============================================================================
 * GTK4 Removed APIs - Compatibility Macros/Functions
 * =============================================================================
 * These provide no-op or compatible replacements for removed GTK3 APIs
 */

#if HC_GTK4

/*
 * GtkBin - removed in GTK4
 * GTK4 doesn't have GtkBin - containers have explicit child accessors
 */
#define GTK_BIN(x) (x)
#define gtk_bin_get_child(bin) NULL

/*
 * Viewport adjustments - use GtkScrollable interface in GTK4
 */
#define gtk_viewport_get_vadjustment(viewport) \
	gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(viewport))
#define gtk_viewport_get_hadjustment(viewport) \
	gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(viewport))

/*
 * gtk_box_reorder_child - removed in GTK4
 * Use gtk_box_reorder_child_after() instead, but signature differs
 */
#define gtk_box_reorder_child(box, child, position) ((void)0)

/*
 * gtk_widget_get_toplevel - removed in GTK4
 * Use gtk_widget_get_root() instead
 */
#define gtk_widget_get_toplevel(widget) \
	GTK_WIDGET(gtk_widget_get_root(widget))

/*
 * gtk_window_set_type_hint - removed in GTK4
 * Window hints are handled differently in GTK4
 */
#define gtk_window_set_type_hint(window, hint) ((void)0)
#define GDK_WINDOW_TYPE_HINT_DIALOG 0
#define GDK_WINDOW_TYPE_HINT_UTILITY 0
#define GDK_WINDOW_TYPE_HINT_NORMAL 0

/*
 * gtk_window_set_wmclass / gtk_window_set_role - removed in GTK4
 */
#define gtk_window_set_wmclass(window, name, wclass) ((void)0)
#define gtk_window_set_role(window, role) ((void)0)

/*
 * gtk_window_set_urgency_hint - removed in GTK4
 */
#define gtk_window_set_urgency_hint(window, setting) ((void)0)

/*
 * GTK_WINDOW_TOPLEVEL - removed in GTK4
 * gtk_window_new() no longer takes a type argument
 */
#define GTK_WINDOW_TOPLEVEL 0

/*
 * gtk_label_set_line_wrap - renamed to gtk_label_set_wrap in GTK4
 */
#define gtk_label_set_line_wrap(label, wrap) \
	gtk_label_set_wrap(GTK_LABEL(label), wrap)

/*
 * gtk_widget_set_can_default - removed in GTK4
 */
#define gtk_widget_set_can_default(widget, can_default) ((void)0)

/*
 * gtk_adjustment_changed - removed in GTK4
 * Adjustments now emit "changed" signal automatically
 */
#define gtk_adjustment_changed(adj) ((void)0)

/*
 * gtk_button_set_image - removed in GTK4
 * Use gtk_button_set_child() with a GtkImage instead
 */
#define gtk_button_set_image(button, image) \
	gtk_button_set_child(GTK_BUTTON(button), image)

/*
 * GTK_ICON_SIZE_* - removed in GTK4, use GTK_ICON_SIZE_NORMAL/LARGE/INHERIT
 */
#define GTK_ICON_SIZE_MENU GTK_ICON_SIZE_NORMAL
#define GTK_ICON_SIZE_SMALL_TOOLBAR GTK_ICON_SIZE_NORMAL
#define GTK_ICON_SIZE_LARGE_TOOLBAR GTK_ICON_SIZE_LARGE
#define GTK_ICON_SIZE_BUTTON GTK_ICON_SIZE_NORMAL
#define GTK_ICON_SIZE_DND GTK_ICON_SIZE_LARGE
#define GTK_ICON_SIZE_DIALOG GTK_ICON_SIZE_LARGE

/*
 * gtk_image_get_pixbuf - removed in GTK4
 * GTK4 uses GdkPaintable instead of GdkPixbuf
 */
#define gtk_image_get_pixbuf(image) NULL

/*
 * gtk_paned_get_child1/2 - renamed in GTK4
 */
#define gtk_paned_get_child1(paned) gtk_paned_get_start_child(GTK_PANED(paned))
#define gtk_paned_get_child2(paned) gtk_paned_get_end_child(GTK_PANED(paned))

/*
 * gtk_paned_pack1/pack2 - use the inline functions defined earlier
 */
#define gtk_paned_pack1(paned, child, resize, shrink) \
	hc_gtk4_paned_pack1(GTK_PANED(paned), child, resize, shrink)
#define gtk_paned_pack2(paned, child, resize, shrink) \
	hc_gtk4_paned_pack2(GTK_PANED(paned), child, resize, shrink)

/*
 * gtk_widget_style_get - removed in GTK4
 * Style properties are gone - use CSS
 */
#define gtk_widget_style_get(widget, ...) ((void)0)

/*
 * gtk_widget_override_* - removed in GTK4
 * Use CSS instead
 */
#define gtk_widget_override_background_color(widget, state, color) ((void)0)
#define gtk_widget_override_color(widget, state, color) ((void)0)
#define gtk_widget_override_font(widget, font_desc) ((void)0)

/*
 * gtk_container_remove - removed in GTK4
 * Use specific container methods based on container type
 */
static inline void
hc_container_remove (GtkWidget *container, GtkWidget *widget)
{
	if (GTK_IS_BOX (container))
		gtk_box_remove (GTK_BOX (container), widget);
	else if (GTK_IS_PANED (container))
	{
		/* In GTK4, use the paned API to properly clear children.
		 * Note: The paned widget handles NULL children correctly for
		 * size requests, but we need to use set_start/end_child. */
		GtkPaned *paned = GTK_PANED (container);
		if (gtk_paned_get_start_child (paned) == widget)
			gtk_paned_set_start_child (paned, NULL);
		else if (gtk_paned_get_end_child (paned) == widget)
			gtk_paned_set_end_child (paned, NULL);
	}
	else if (GTK_IS_GRID (container))
		gtk_grid_remove (GTK_GRID (container), widget);
	else if (GTK_IS_WINDOW (container))
		gtk_window_set_child (GTK_WINDOW (container), NULL);
	else
	{
		/* Fallback: try to unparent the widget */
		gtk_widget_unparent (widget);
	}
}

#define gtk_container_remove(container, widget) \
	hc_container_remove(GTK_WIDGET(container), widget)

/*
 * gtk_container_set_border_width - removed in GTK4
 * Use widget margins
 */
#define gtk_container_set_border_width(container, border) \
	hc_container_set_border_width(container, border)
#define GTK_CONTAINER(x) (x)

/*
 * GtkMenu/GtkMenuItem - removed in GTK4
 * Use GtkPopoverMenu with GMenuModel instead
 * These are placeholders that will cause compile errors
 */
#define GTK_MENU_SHELL(x) (x)
#define GTK_MENU_ITEM(x) (x)
#define gtk_menu_shell_append(shell, item) ((void)0)
#define gtk_menu_item_new_with_label(label) NULL
#define gtk_menu_item_new_with_mnemonic(label) NULL
#define gtk_menu_new() NULL
#define gtk_menu_item_set_submenu(item, submenu) ((void)0)

/*
 * GtkRadioButton - removed in GTK4
 * Use GtkCheckButton with groups instead
 */
#define GTK_RADIO_BUTTON(x) GTK_CHECK_BUTTON(x)
#define gtk_radio_button_new_with_mnemonic(group, label) \
	gtk_check_button_new_with_mnemonic(label)
#define gtk_radio_button_get_group(button) NULL
#define gtk_radio_button_set_group(button, group) ((void)0)

/*
 * gdk_beep - removed in GTK4
 */
#define gdk_beep() ((void)0)

/*
 * GDK modifier masks - renamed in GTK4
 * GTK4 renamed GDK_MOD1_MASK to GDK_ALT_MASK
 */
#ifndef GDK_MOD1_MASK
#define GDK_MOD1_MASK GDK_ALT_MASK
#endif

/*
 * GtkAccelGroup - removed in GTK4
 * GTK4 uses GtkShortcut and GtkShortcutController instead
 */
typedef void *GtkAccelGroup;
#define gtk_accel_group_new() NULL
#define gtk_window_add_accel_group(window, accel_group) ((void)0)
#define gtk_widget_add_accelerator(widget, signal, accel_group, key, mods, flags) ((void)0)
#define GTK_ACCEL_VISIBLE 0

/*
 * GtkReliefStyle - removed in GTK4
 * Use CSS styling instead. In GTK4, add "flat" CSS class for borderless look.
 */
#ifndef GTK_RELIEF_NONE
#define GTK_RELIEF_NONE 0
#define GTK_RELIEF_NORMAL 1
#endif

static inline void
hc_button_set_relief (GtkButton *button, int relief)
{
	if (relief == GTK_RELIEF_NONE)
		gtk_widget_add_css_class (GTK_WIDGET (button), "flat");
	else
		gtk_widget_remove_css_class (GTK_WIDGET (button), "flat");
}
#define gtk_button_set_relief(button, relief) hc_button_set_relief(GTK_BUTTON(button), relief)

/*
 * GtkCheckMenuItem, GtkRadioMenuItem - removed in GTK4
 * Use GtkPopoverMenu with GMenuModel instead
 */
typedef GtkWidget GtkCheckMenuItem;
typedef GtkWidget GtkRadioMenuItem;
typedef GtkWidget GtkMenuItem;
#define GTK_CHECK_MENU_ITEM(x) GTK_WIDGET(x)
#define GTK_RADIO_MENU_ITEM(x) GTK_WIDGET(x)
#define GTK_MENU(x) (x)
#define gtk_check_menu_item_new_with_mnemonic(label) gtk_button_new_with_label(label)
#define gtk_check_menu_item_set_active(item, active) ((void)0)
#define gtk_check_menu_item_get_active(item) FALSE
#define GTK_IS_CHECK_MENU_ITEM(x) FALSE
#define gtk_menu_item_new() gtk_button_new()
#define gtk_menu_shell_insert(shell, item, pos) ((void)0)
#define gtk_radio_menu_item_get_group(item) NULL
#define gtk_radio_menu_item_new_with_label(group, label) gtk_button_new_with_label(label)
#define gtk_radio_menu_item_new_with_mnemonic(group, label) gtk_button_new_with_label(label)
#define gtk_menu_item_get_submenu(item) NULL
#define gtk_menu_bar_new() gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)

/*
 * gtk_init - signature changed in GTK4
 * GTK4: gtk_init() takes no arguments
 * Note: Don't redefine gtk_init since GTK4 already provides the correct signature.
 * Code calling gtk_init(&argc, &argv) needs to use:
 *   #if HC_GTK4
 *   gtk_init();
 *   #else
 *   gtk_init(&argc, &argv);
 *   #endif
 */
/* Compatibility: wrap the old-style call */
#define hc_gtk_init(argc, argv) gtk_init()

/*
 * gtk_get_option_group - removed in GTK4
 * Application command line handling is different in GTK4
 */
#define gtk_get_option_group(open_default) NULL

/*
 * gtk_main / gtk_main_quit - removed in GTK4
 * GTK4 uses GApplication main loop
 * These are implemented directly in fe-gtk.c with proper main loop handling
 */

/*
 * gtk_css_provider_load_from_data - signature changed in GTK4
 * GTK4 removed the length and error parameters
 */
#define gtk_css_provider_load_from_data_compat(provider, data, length, error) \
	gtk_css_provider_load_from_data(provider, data, length)

/*
 * gdk_screen_get_default - removed in GTK4
 * Use gdk_display_get_default() instead
 */
#define gdk_screen_get_default() gdk_display_get_default()

/*
 * gtk_style_context_add_provider_for_screen - renamed in GTK4
 */
#define gtk_style_context_add_provider_for_screen(screen, provider, priority) \
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), provider, priority)

/*
 * gtk_window functions removed in GTK4
 */
#define gtk_window_iconify(window) gtk_window_minimize(GTK_WINDOW(window))
#define gtk_window_move(window, x, y) ((void)0)
#define gtk_window_resize(window, w, h) gtk_window_set_default_size(GTK_WINDOW(window), w, h)
#define gtk_window_set_opacity(window, opacity) ((void)0)

/*
 * gtk_container_get_children - removed in GTK4
 */
#define gtk_container_get_children(container) hc_container_get_children(GTK_WIDGET(container))

/*
 * GdkPixbuf to cairo surface - removed in GTK4
 * Use GdkTexture instead
 */
#define gdk_cairo_surface_create_from_pixbuf(pixbuf, scale, for_window) NULL

/*
 * GtkFileChooser API changes in GTK4
 * These functions now use GFile* instead of strings
 */
#define gtk_file_chooser_get_filename(chooser) \
	g_file_get_path(gtk_file_chooser_get_file(GTK_FILE_CHOOSER(chooser)))
#define gtk_file_chooser_get_filenames(chooser) \
	hc_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser))
#define gtk_file_chooser_set_do_overwrite_confirmation(chooser, confirm) ((void)0)

/* Helper for gtk_file_chooser_get_filenames in GTK4 */
static inline GSList *
hc_file_chooser_get_filenames (GtkFileChooser *chooser)
{
	GListModel *files = gtk_file_chooser_get_files (chooser);
	GSList *list = NULL;
	guint n = g_list_model_get_n_items (files);
	for (guint i = 0; i < n; i++)
	{
		GFile *file = g_list_model_get_item (files, i);
		list = g_slist_append (list, g_file_get_path (file));
		g_object_unref (file);
	}
	g_object_unref (files);
	return list;
}

/* Helper for gtk_file_chooser_set_current_folder in GTK4 */
static inline gboolean
hc_file_chooser_set_current_folder (GtkFileChooser *chooser, const char *folder)
{
	GFile *file = g_file_new_for_path (folder);
	gboolean result = gtk_file_chooser_set_current_folder (chooser, file, NULL);
	g_object_unref (file);
	return result;
}

/*
 * gtk_window_new - signature changed in GTK4
 * GTK4 version takes no arguments
 */
#define gtk_window_new_compat(type) gtk_window_new()

/*
 * gtk_entry_get_layout / gtk_entry_get_layout_offsets - removed in GTK4
 * GtkEntry no longer exposes PangoLayout directly in GTK4.
 * For now, provide stubs that return NULL/do nothing.
 * Spell-checking code will need significant rewrite for GTK4.
 */
#define gtk_entry_get_layout(entry) ((PangoLayout*)NULL)
static inline void hc_entry_get_layout_offsets(GtkEntry *entry, gint *x, gint *y)
{
	(void)entry;
	if (x) *x = 0;
	if (y) *y = 0;
}
#define gtk_entry_get_layout_offsets(entry, x, y) hc_entry_get_layout_offsets(entry, x, y)

/*
 * gtk_selection_model_get_model - doesn't exist, use G_LIST_MODEL
 */
#define gtk_selection_model_get_model(sel) G_LIST_MODEL(sel)

/*
 * gtk_grab_add / gtk_grab_remove - removed in GTK4
 * GTK4 uses GtkEventController for event handling, no explicit grabbing needed
 */
#define gtk_grab_add(widget) ((void)0)
#define gtk_grab_remove(widget) ((void)0)

/*
 * gtk_window_has_toplevel_focus - renamed in GTK4
 */
#define gtk_window_has_toplevel_focus(window) gtk_window_is_active(GTK_WINDOW(window))

/*
 * gtk_widget_grab_default - removed in GTK4
 * Use gtk_widget_set_receives_default(widget, TRUE) and CSS instead
 */
#define gtk_widget_grab_default(widget) ((void)0)

#endif /* HC_GTK4 */

/*
 * =============================================================================
 * Deprecated/Removed Functions - Compile-time errors
 * =============================================================================
 * Track functions that need complete replacement
 */

#if HC_GTK4

/* These simply don't exist in GTK4 - code must be rewritten */
#define gtk_widget_show_all COMPILE_ERROR_USE_gtk_widget_set_visible
#define gtk_widget_destroy COMPILE_ERROR_USE_appropriate_method
/* Note: gtk_container_add is now mapped above to cause build issues for debugging */

/*
 * gtk_widget_show - In GTK4, widgets are visible by default.
 * Calling gtk_widget_show on a toplevel window requires the window
 * to be properly initialized. Map to gtk_widget_set_visible which is safer.
 * For windows, calling gtk_widget_set_visible(w, TRUE) is equivalent.
 */
#define gtk_widget_show(widget) gtk_widget_set_visible(widget, TRUE)
#define gtk_widget_hide(widget) gtk_widget_set_visible(widget, FALSE)

#endif

/*
 * =============================================================================
 * GtkStack as GtkNotebook replacement for GTK4
 * =============================================================================
 * HexChat uses GtkNotebook without tabs (show_tabs=FALSE) as a page container.
 * In GTK4, GtkNotebook has an internal header that takes space even with tabs hidden.
 * GtkStack is the proper GTK4 widget for this use case.
 *
 * These macros allow using GtkStack in GTK4 while keeping GtkNotebook in GTK3.
 */

#if HC_GTK4

/* Create a new page container - GtkStack in GTK4 */
static inline GtkWidget *
hc_page_container_new (void)
{
	GtkWidget *stack = gtk_stack_new ();
	gtk_stack_set_hhomogeneous (GTK_STACK (stack), FALSE);
	gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
	return stack;
}

/* Add a page to the container */
static inline void
hc_page_container_append (GtkWidget *container, GtkWidget *child)
{
	/* Use the widget pointer as a unique name */
	char name[32];
	g_snprintf (name, sizeof (name), "page_%p", (void *)child);
	gtk_stack_add_named (GTK_STACK (container), child, name);
}

/* Get page number (index) for a child widget - returns -1 if not found */
static inline int
hc_page_container_get_page_num (GtkWidget *container, GtkWidget *child)
{
	GtkSelectionModel *pages = gtk_stack_get_pages (GTK_STACK (container));
	guint n = g_list_model_get_n_items (G_LIST_MODEL (pages));
	for (guint i = 0; i < n; i++)
	{
		GtkStackPage *page = GTK_STACK_PAGE (g_list_model_get_item (G_LIST_MODEL (pages), i));
		if (gtk_stack_page_get_child (page) == child)
		{
			g_object_unref (page);
			return (int)i;
		}
		g_object_unref (page);
	}
	return -1;
}

/* Set the visible page by index */
static inline void
hc_page_container_set_current_page (GtkWidget *container, int page_num)
{
	GtkSelectionModel *pages = gtk_stack_get_pages (GTK_STACK (container));
	guint n = g_list_model_get_n_items (G_LIST_MODEL (pages));
	if (page_num >= 0 && (guint)page_num < n)
	{
		GtkStackPage *page = GTK_STACK_PAGE (g_list_model_get_item (G_LIST_MODEL (pages), page_num));
		gtk_stack_set_visible_child (GTK_STACK (container), gtk_stack_page_get_child (page));
		g_object_unref (page);
	}
}

/* Remove a page by index */
static inline void
hc_page_container_remove_page (GtkWidget *container, int page_num)
{
	GtkSelectionModel *pages = gtk_stack_get_pages (GTK_STACK (container));
	guint n = g_list_model_get_n_items (G_LIST_MODEL (pages));
	if (page_num >= 0 && (guint)page_num < n)
	{
		GtkStackPage *page = GTK_STACK_PAGE (g_list_model_get_item (G_LIST_MODEL (pages), page_num));
		GtkWidget *child = gtk_stack_page_get_child (page);
		g_object_unref (page);
		gtk_stack_remove (GTK_STACK (container), child);
	}
}

#else /* GTK3 */

/* In GTK3, use GtkNotebook */
static inline GtkWidget *
hc_page_container_new (void)
{
	GtkWidget *notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
	return notebook;
}

static inline void
hc_page_container_append (GtkWidget *container, GtkWidget *child)
{
	gtk_notebook_append_page (GTK_NOTEBOOK (container), child, NULL);
}

static inline int
hc_page_container_get_page_num (GtkWidget *container, GtkWidget *child)
{
	return gtk_notebook_page_num (GTK_NOTEBOOK (container), child);
}

static inline void
hc_page_container_set_current_page (GtkWidget *container, int page_num)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (container), page_num);
}

static inline void
hc_page_container_remove_page (GtkWidget *container, int page_num)
{
	gtk_notebook_remove_page (GTK_NOTEBOOK (container), page_num);
}

#endif /* HC_GTK4 */

#endif /* HEXCHAT_GTK_COMPAT_H */
