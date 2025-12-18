/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/fe.h"
#include "../common/text.h"
#include "gtkutil.h"
#include "xtext.h"
#include "maingui.h"
#include "palette.h"
#include "textgui.h"

extern struct text_event te[];
extern char *pntevts_text[];
extern char *pntevts[];

static GtkWidget *pevent_dialog = NULL, *pevent_dialog_twid,
	*pevent_dialog_list, *pevent_dialog_hlist;

/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold event row data */
#define HC_TYPE_EVENT_ITEM (hc_event_item_get_type())
G_DECLARE_FINAL_TYPE (HcEventItem, hc_event_item, HC, EVENT_ITEM, GObject)

struct _HcEventItem {
	GObject parent;
	char *event_name;
	char *text;
	int row;       /* index into te[] array */
};

G_DEFINE_TYPE (HcEventItem, hc_event_item, G_TYPE_OBJECT)

static void
hc_event_item_finalize (GObject *obj)
{
	HcEventItem *item = HC_EVENT_ITEM (obj);
	g_free (item->event_name);
	g_free (item->text);
	G_OBJECT_CLASS (hc_event_item_parent_class)->finalize (obj);
}

static void
hc_event_item_class_init (HcEventItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_event_item_finalize;
}

static void
hc_event_item_init (HcEventItem *item)
{
	item->event_name = NULL;
	item->text = NULL;
	item->row = 0;
}

static HcEventItem *
hc_event_item_new (const char *event_name, const char *text, int row)
{
	HcEventItem *item = g_object_new (HC_TYPE_EVENT_ITEM, NULL);
	item->event_name = g_strdup (event_name ? event_name : "");
	item->text = g_strdup (text ? text : "");
	item->row = row;
	return item;
}

/* GObject to hold help row data */
#define HC_TYPE_HELP_ITEM (hc_help_item_get_type())
G_DECLARE_FINAL_TYPE (HcHelpItem, hc_help_item, HC, HELP_ITEM, GObject)

struct _HcHelpItem {
	GObject parent;
	int number;
	char *description;
};

G_DEFINE_TYPE (HcHelpItem, hc_help_item, G_TYPE_OBJECT)

static void
hc_help_item_finalize (GObject *obj)
{
	HcHelpItem *item = HC_HELP_ITEM (obj);
	g_free (item->description);
	G_OBJECT_CLASS (hc_help_item_parent_class)->finalize (obj);
}

static void
hc_help_item_class_init (HcHelpItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_help_item_finalize;
}

static void
hc_help_item_init (HcHelpItem *item)
{
	item->number = 0;
	item->description = NULL;
}

static HcHelpItem *
hc_help_item_new (int number, const char *description)
{
	HcHelpItem *item = g_object_new (HC_TYPE_HELP_ITEM, NULL);
	item->number = number;
	item->description = g_strdup (description ? description : "");
	return item;
}

static GListStore *pevent_store = NULL;
static GListStore *pevent_help_store = NULL;


/* this is only used in xtext.c for indented timestamping */
int
xtext_get_stamp_str (time_t tim, char **ret)
{
	return get_stamp_str (prefs.hex_stamp_text_format, tim, ret);
}

static void
PrintTextLine (xtext_buffer *xtbuf, unsigned char *text, int len, int indent, time_t timet)
{
	unsigned char *tab, *new_text;
	int leftlen;

	if (len == 0)
		len = 1;

	if (!indent)
	{
		if (prefs.hex_stamp_text)
		{
			int stamp_size;
			char *stamp;

			if (timet == 0)
				timet = time (0);

			stamp_size = get_stamp_str (prefs.hex_stamp_text_format, timet, &stamp);
			new_text = g_malloc (len + stamp_size + 1);
			memcpy (new_text, stamp, stamp_size);
			g_free (stamp);
			memcpy (new_text + stamp_size, text, len);
			gtk_xtext_append (xtbuf, new_text, len + stamp_size, timet);
			g_free (new_text);
		} else
			gtk_xtext_append (xtbuf, text, len, timet);
		return;
	}

	tab = strchr (text, '\t');
	if (tab && tab < (text + len))
	{
		leftlen = tab - text;
		gtk_xtext_append_indent (xtbuf,
										 text, leftlen, tab + 1, len - (leftlen + 1), timet);
	} else
		gtk_xtext_append_indent (xtbuf, 0, 0, text, len, timet);
}

void
PrintTextRaw (void *xtbuf, unsigned char *text, int indent, time_t stamp)
{
	char *last_text = text;
	int len = 0;
	int beep_done = FALSE;

	/* split the text into separate lines */
	while (1)
	{
		switch (*text)
		{
		case 0:
			PrintTextLine (xtbuf, last_text, len, indent, stamp);
			return;
		case '\n':
			PrintTextLine (xtbuf, last_text, len, indent, stamp);
			text++;
			if (*text == 0)
				return;
			last_text = text;
			len = 0;
			break;
		case ATTR_BEEP:
			*text = ' ';
			if (!beep_done) /* beeps may be slow, so only do 1 per line */
			{
				beep_done = TRUE;
				if (!prefs.hex_input_filter_beep)
					gdk_beep ();
			}
		default:
			text++;
			len++;
		}
	}
}

static void
pevent_dialog_close (GtkWidget *wid, gpointer arg)
{
	pevent_dialog = NULL;
	pevent_save (NULL);
}

/* GTK4: Called when text editing completes - validates and updates the event */
static void
pevent_text_edited (HcEventItem *event_item, const char *new_text)
{
	GtkXText *xtext = GTK_XTEXT (pevent_dialog_twid);
	int len, m;
	char *out;
	int sig;

	if (!event_item || !new_text)
		return;

	sig = event_item->row;
	len = strlen (new_text);

	if (pevt_build_string (new_text, &out, &m) != 0)
	{
		fe_message (_("There was an error parsing the string"), FE_MSG_ERROR);
		return;
	}
	if (m > (te[sig].num_args & 0x7f))
	{
		g_free (out);
		out = g_strdup_printf (
			_("This signal is only passed %d args, $%d is invalid"),
			te[sig].num_args & 0x7f, m);
		fe_message (out, FE_MSG_WARN);
		g_free (out);
		return;
	}

	/* Update the item's text */
	g_free (event_item->text);
	event_item->text = g_strdup (new_text);

	g_free (pntevts_text[sig]);
	g_free (pntevts[sig]);

	pntevts_text[sig] = g_strdup (new_text);
	pntevts[sig] = out;

	out = g_malloc (len + 2);
	memcpy (out, new_text, len + 1);
	out[len] = '\n';
	out[len + 1] = 0;
	check_special_chars (out, TRUE);

	PrintTextRaw (xtext->buffer, out, 0, 0);
	g_free (out);

	/* Scroll to bottom */
	gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj));

	/* save this when we exit */
	prefs.save_pevents = 1;
}

static void
pevent_dialog_hfill (int e)
{
	int i = 0;
	char *text;

	g_list_store_remove_all (pevent_help_store);

	while (i < (te[e].num_args & 0x7f))
	{
		text = _(te[e].help[i]);
		i++;
		if (text[0] == '\001')
			text++;
		HcHelpItem *item = hc_help_item_new (i, text);
		g_list_store_append (pevent_help_store, item);
		g_object_unref (item);
	}
}

static void
pevent_selection_changed (GtkSelectionModel *sel_model, guint position, guint n_items, gpointer userdata)
{
	HcEventItem *event_item;

	event_item = hc_selection_model_get_selected_item (sel_model);
	if (!event_item)
	{
		g_list_store_remove_all (pevent_help_store);
		return;
	}

	pevent_dialog_hfill (event_item->row);
	g_object_unref (event_item);
}

static void
pevent_dialog_fill (void)
{
	int i;

	g_list_store_remove_all (pevent_store);

	i = NUM_XP;
	do
	{
		i--;
		HcEventItem *item = hc_event_item_new (te[i].name, pntevts_text[i], i);
		g_list_store_insert (pevent_store, 0, item);
		g_object_unref (item);
	}
	while (i != 0);
}


static void
pevent_save_req_cb (void *arg1, char *file)
{
	if (file)
		pevent_save (file);
}

static void
pevent_save_cb (GtkWidget * wid, void *data)
{
	if (data)
	{
		gtkutil_file_req (NULL, _("Print Texts File"), pevent_save_req_cb, NULL,
								NULL, NULL, FRF_WRITE);
		return;
	}
	pevent_save (NULL);
}

static void
pevent_load_req_cb (void *arg1, char *file)
{
	if (file)
	{
		pevent_load (file);
		pevent_make_pntevts ();
		pevent_dialog_fill ();
		prefs.save_pevents = 1;
	}
}

static void
pevent_load_cb (GtkWidget * wid, void *data)
{
	gtkutil_file_req (NULL, _("Print Texts File"), pevent_load_req_cb, NULL, NULL, NULL, 0);
}

static void
pevent_ok_cb (GtkWidget * wid, void *data)
{
	hc_window_destroy (pevent_dialog);
}

static void
pevent_test_cb (GtkWidget * wid, GtkWidget * twid)
{
	int len, n;
	char *out, *text;

	for (n = 0; n < NUM_XP; n++)
	{
		text = _(pntevts_text[n]);
		len = strlen (text);

		out = g_malloc (len + 2);
		memcpy (out, text, len + 1);
		out[len] = '\n';
		out[len + 1] = 0;
		check_special_chars (out, TRUE);

		PrintTextRaw (GTK_XTEXT (twid)->buffer, out, 0, 0);
		g_free (out);
	}
}

/*
 * GTK4 column view factory callbacks
 */

/* Event column - read-only label */
static void
pevent_setup_event_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
pevent_bind_event_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcEventItem *event = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), event->event_name ? event->event_name : "");
}

/* Text column - editable label */
static void
pevent_setup_text_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_editable_label_new ("");
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
pevent_text_changed_cb (GtkEditableLabel *label, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item = GTK_LIST_ITEM (user_data);
	HcEventItem *event = gtk_list_item_get_item (list_item);
	const char *new_text;

	if (!event)
		return;

	new_text = gtk_editable_get_text (GTK_EDITABLE (label));
	pevent_text_edited (event, new_text);
}

static void
pevent_bind_text_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcEventItem *event = gtk_list_item_get_item (item);

	gtk_editable_set_text (GTK_EDITABLE (label), event->text ? event->text : "");
	g_signal_connect (label, "notify::text", G_CALLBACK (pevent_text_changed_cb), item);
}

static void
pevent_unbind_text_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	g_signal_handlers_disconnect_by_func (label, pevent_text_changed_cb, item);
}

/* Help list columns */
static void
pevent_setup_number_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.5);
	gtk_list_item_set_child (item, label);
}

static void
pevent_bind_number_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcHelpItem *help = gtk_list_item_get_item (item);
	char buf[16];
	g_snprintf (buf, sizeof(buf), "%d", help->number);
	gtk_label_set_text (GTK_LABEL (label), buf);
}

static void
pevent_setup_desc_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
pevent_bind_desc_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcHelpItem *help = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), help->description ? help->description : "");
}

static GtkWidget *
pevent_columnview_new (GtkWidget *box)
{
	GtkWidget *scroll;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;
	GtkSelectionModel *sel_model;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, 250);

	/* Create list store for events */
	pevent_store = g_list_store_new (HC_TYPE_EVENT_ITEM);

	/* Create column view with single selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (pevent_store), GTK_SELECTION_SINGLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Connect selection changed signal */
	sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));
	g_signal_connect (sel_model, "selection-changed",
	                  G_CALLBACK (pevent_selection_changed), NULL);

	/* Add Event column (read-only) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (pevent_setup_event_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (pevent_bind_event_cb), NULL);
	col = gtk_column_view_column_new (_("Event"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, FALSE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Text column (editable) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (pevent_setup_text_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (pevent_bind_text_cb), NULL);
	g_signal_connect (factory, "unbind", G_CALLBACK (pevent_unbind_text_cb), NULL);
	col = gtk_column_view_column_new (_("Text"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_add (box, scroll);

	return view;
}

static GtkWidget *
pevent_hlist_columnview_new (GtkWidget *box)
{
	GtkWidget *scroll;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Create list store for help items */
	pevent_help_store = g_list_store_new (HC_TYPE_HELP_ITEM);

	/* Create column view (no selection needed for help list) */
	view = hc_column_view_new_simple (G_LIST_MODEL (pevent_help_store), GTK_SELECTION_NONE);
	gtk_widget_set_can_focus (view, FALSE);

	/* Add $ Number column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (pevent_setup_number_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (pevent_bind_number_cb), NULL);
	col = gtk_column_view_column_new (_("$ Number"), factory);
	gtk_column_view_column_set_expand (col, FALSE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Add Description column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (pevent_setup_desc_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (pevent_bind_desc_cb), NULL);
	col = gtk_column_view_column_new (_("Description"), factory);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_add (box, scroll);

	return view;
}

void
pevent_dialog_show ()
{
	GtkWidget *vbox, *hbox, *wid, *pane;

	if (pevent_dialog)
	{
		mg_bring_tofront (pevent_dialog);
		return;
	}

	pevent_dialog =
			  mg_create_generic_tab ("edit events", _("Edit Events"),
											 TRUE, FALSE, pevent_dialog_close, NULL,
											 600, 455, &vbox, 0);

	pane = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	hc_box_pack_start (vbox, pane, TRUE, TRUE, 0);

	pevent_dialog_list = pevent_columnview_new (pane);
	pevent_dialog_fill ();

	pevent_dialog_hlist = pevent_hlist_columnview_new (pane);

	wid = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (wid), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	hc_box_pack_start (vbox, wid, FALSE, TRUE, 0);

	pevent_dialog_twid = gtk_xtext_new (colors, 0);
	gtk_widget_set_sensitive (pevent_dialog_twid, FALSE);
	gtk_widget_set_size_request (pevent_dialog_twid, -1, 75);
	hc_scrolled_window_set_child (wid, pevent_dialog_twid);
	gtk_xtext_set_font (GTK_XTEXT (pevent_dialog_twid), prefs.hex_text_font);

	hbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (hbox, GTK_BUTTONBOX_SPREAD);
	hc_box_pack_start (vbox, hbox, FALSE, FALSE, 4);
	gtkutil_button (hbox, "document-save-as", NULL, pevent_save_cb,
						 (void *) 1, _("Save As..."));
	gtkutil_button (hbox, "document-open", NULL, pevent_load_cb,
						 NULL, _("Load From..."));
	gtkutil_button (hbox, NULL, NULL, pevent_test_cb,
						pevent_dialog_twid, _("Test All"));
	gtkutil_button (hbox, "emblem-ok", NULL, pevent_ok_cb,
						NULL, _("OK"));

	hc_widget_show_all (pevent_dialog);
}
