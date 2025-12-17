/* X-Chat
 * Copyright (C) 1998-2005 Peter Zelezny.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/hexchatc.h"
#include "../common/outbound.h"
#include "../common/inbound.h"
#include "../common/plugin.h"
#include "../common/modes.h"
#include "../common/url.h"
#include "../common/util.h"
#include "../common/text.h"
#include "../common/chanopt.h"
#include "../common/cfgfiles.h"
#include "../common/servlist.h"

#include "fe-gtk.h"
#include "banlist.h"
#include "gtkutil.h"
#include "joind.h"
#include "palette.h"
#include "maingui.h"
#include "menu.h"
#include "fkeys.h"
#include "userlistgui.h"
#include "chanview.h"
#include "pixmaps.h"
#include "plugin-tray.h"
#include "xtext.h"
#include "sexy-spell-entry.h"
#include "gtkutil.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#define GUI_SPACING (3)
#define GUI_BORDER (0)

enum
{
	POS_INVALID = 0,
	POS_TOPLEFT = 1,
	POS_BOTTOMLEFT = 2,
	POS_TOPRIGHT = 3,
	POS_BOTTOMRIGHT = 4,
	POS_TOP = 5,	/* for tabs only */
	POS_BOTTOM = 6,
	POS_HIDDEN = 7
};

/* two different types of tabs */
#define TAG_IRC 0		/* server, channel, dialog */
#define TAG_UTIL 1	/* dcc, notify, chanlist */

static void mg_create_entry (session *sess, GtkWidget *box);
static void mg_create_search (session *sess, GtkWidget *box);
static void mg_link_irctab (session *sess, int focus);

static session_gui static_mg_gui;
static session_gui *mg_gui = NULL;	/* the shared irc tab */
static int ignore_chanmode = FALSE;
static const char chan_flags[] = { 'c', 'n', 't', 'i', 'm', 'l', 'k' };

static chan *active_tab = NULL;	/* active tab */
GtkWidget *parent_window = NULL;			/* the master window */

InputStyle *input_style;

static PangoAttrList *away_list;
static PangoAttrList *newdata_list;
static PangoAttrList *nickseen_list;
static PangoAttrList *newmsg_list;
static PangoAttrList *plain_list = NULL;

static PangoAttrList *
mg_attr_list_create (GdkRGBA *col, int size)
{
	PangoAttribute *attr;
	PangoAttrList *list;

	list = pango_attr_list_new ();

	if (col)
	{
		/* Convert GdkRGBA floats (0.0-1.0) to 16-bit integers (0-65535) for pango */
		attr = pango_attr_foreground_new ((guint16)(col->red * 65535),
		                                  (guint16)(col->green * 65535),
		                                  (guint16)(col->blue * 65535));
		attr->start_index = 0;
		attr->end_index = 0xffff;
		pango_attr_list_insert (list, attr);
	}

	if (size > 0)
	{
		attr = pango_attr_scale_new (size == 1 ? PANGO_SCALE_SMALL : PANGO_SCALE_X_SMALL);
		attr->start_index = 0;
		attr->end_index = 0xffff;
		pango_attr_list_insert (list, attr);
	}

	return list;
}

static void
mg_create_tab_colors (void)
{
	if (plain_list)
	{
		pango_attr_list_unref (plain_list);
		pango_attr_list_unref (newmsg_list);
		pango_attr_list_unref (newdata_list);
		pango_attr_list_unref (nickseen_list);
		pango_attr_list_unref (away_list);
	}

	plain_list = mg_attr_list_create (NULL, prefs.hex_gui_tab_small);
	newdata_list = mg_attr_list_create (&colors[COL_NEW_DATA], prefs.hex_gui_tab_small);
	nickseen_list = mg_attr_list_create (&colors[COL_HILIGHT], prefs.hex_gui_tab_small);
	newmsg_list = mg_attr_list_create (&colors[COL_NEW_MSG], prefs.hex_gui_tab_small);
	away_list = mg_attr_list_create (&colors[COL_AWAY], FALSE);
}

static void
set_window_urgency (GtkWidget *win, gboolean set)
{
	gtk_window_set_urgency_hint (GTK_WINDOW (win), set);
}

static void
flash_window (GtkWidget *win)
{
#ifdef HAVE_GTK_MAC
	gtkosx_application_attention_request (osx_app, INFO_REQUEST);
#endif
	set_window_urgency (win, TRUE);
}

static void
unflash_window (GtkWidget *win)
{
	set_window_urgency (win, FALSE);
}

/* flash the taskbar button */

void
fe_flash_window (session *sess)
{
	if (fe_gui_info (sess, 0) != 1)	/* only do it if not focused */
		flash_window (sess->gui->window);
}

/* set a tab plain, red, light-red, or blue */

void
fe_set_tab_color (struct session *sess, tabcolor col)
{
	struct session *server_sess = sess->server->server_session;
	int col_noflags = (col & ~FE_COLOR_ALLFLAGS);
	int col_shouldoverride = !(col & FE_COLOR_FLAG_NOOVERRIDE);

	if (sess->res->tab && sess->gui->is_tab && (col == 0 || sess != current_tab))
	{
		switch (col_noflags)
		{
		case 0:	/* no particular color (theme default) */
			sess->tab_state = TAB_STATE_NONE;
			chan_set_color (sess->res->tab, plain_list);
			break;
		case 1:	/* new data has been displayed (dark red) */
			if (col_shouldoverride || !((sess->tab_state & TAB_STATE_NEW_MSG)
										|| (sess->tab_state & TAB_STATE_NEW_HILIGHT))) {
				sess->tab_state = TAB_STATE_NEW_DATA;
				chan_set_color (sess->res->tab, newdata_list);
			}

			if (chan_is_collapsed (sess->res->tab)
				&& !((server_sess->tab_state & TAB_STATE_NEW_MSG)
					 || (server_sess->tab_state & TAB_STATE_NEW_HILIGHT))
				&& !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_DATA;
				chan_set_color (chan_get_parent (sess->res->tab), newdata_list);
			}

			break;
		case 2:	/* new message arrived in channel (light red) */
			if (col_shouldoverride || !(sess->tab_state & TAB_STATE_NEW_HILIGHT)) {
				sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (sess->res->tab, newmsg_list);
			}

			if (chan_is_collapsed (sess->res->tab)
				&& !(server_sess->tab_state & TAB_STATE_NEW_HILIGHT)
				&& !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (chan_get_parent (sess->res->tab), newmsg_list);
			}

			break;
		case 3:	/* your nick has been seen (blue) */
			sess->tab_state = TAB_STATE_NEW_HILIGHT;
			chan_set_color (sess->res->tab, nickseen_list);

			if (chan_is_collapsed (sess->res->tab) && !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (chan_get_parent (sess->res->tab), nickseen_list);
			}

			break;
		}
		lastact_update (sess);
		sess->last_tab_state = sess->tab_state; /* For plugins handling future prints */
	}
}

static void
mg_set_myself_away (session_gui *gui, gboolean away)
{
	gtk_label_set_attributes (GTK_LABEL (gtk_bin_get_child (GTK_BIN (gui->nick_label))),
									  away ? away_list : NULL);
}

/* change the little icon to the left of your nickname */

void
mg_set_access_icon (session_gui *gui, GdkPixbuf *pix, gboolean away)
{
	if (gui->op_xpm)
	{
		if (pix == gtk_image_get_pixbuf (GTK_IMAGE (gui->op_xpm))) /* no change? */
		{
			mg_set_myself_away (gui, away);
			return;
		}

		hc_widget_destroy (gui->op_xpm);
		gui->op_xpm = NULL;
	}

	if (pix && prefs.hex_gui_input_icon)
	{
		gui->op_xpm = gtk_image_new_from_pixbuf (pix);
		hc_box_pack_start (gui->nick_box, gui->op_xpm, 0, 0, 0);
		gtk_widget_show (gui->op_xpm);
	}

	mg_set_myself_away (gui, away);
}

#if HC_GTK4
static void
mg_inputbox_focus (GtkEventControllerFocus *controller, session_gui *gui)
#else
static gboolean
mg_inputbox_focus (GtkWidget *widget, GdkEventFocus *event, session_gui *gui)
#endif
{
	GSList *list;
	session *sess;

	if (gui->is_tab)
#if HC_GTK4
		return;
#else
		return FALSE;
#endif

	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (sess->gui == gui)
		{
			current_sess = sess;
			if (!sess->server->server_session)
				sess->server->server_session = sess;
			break;
		}
		list = list->next;
	}

#if !HC_GTK4
	return FALSE;
#endif
}

void
mg_inputbox_cb (GtkWidget *igad, session_gui *gui)
{
	char *cmd;
	static int ignore = FALSE;
	GSList *list;
	session *sess = NULL;

	if (ignore)
		return;

	cmd = SPELL_ENTRY_GET_TEXT (igad);
	if (cmd[0] == 0)
		return;

	cmd = g_strdup (cmd);

	/* avoid recursive loop */
	ignore = TRUE;
	SPELL_ENTRY_SET_TEXT (igad, "");
	ignore = FALSE;

	/* where did this event come from? */
	if (gui->is_tab)
	{
		sess = current_tab;
	} else
	{
		list = sess_list;
		while (list)
		{
			sess = list->data;
			if (sess->gui == gui)
				break;
			list = list->next;
		}
		if (!list)
			sess = NULL;
	}

	if (sess)
		handle_multiline (sess, cmd, TRUE, FALSE);

	g_free (cmd);
}

static gboolean
mg_spellcheck_cb (SexySpellEntry *entry, gchar *word, gpointer data)
{
	/* This can cause freezes on long words, nicks arn't very long anyway. */
	if (strlen (word) > 20)
		return TRUE;

	/* Ignore anything we think is a valid url */
	if (url_check_word (word) != 0)
		return FALSE;

	return TRUE;
}

#if 0
static gboolean
has_key (char *modes)
{
	if (!modes)
		return FALSE;
	/* this is a crude check, but "-k" can't exist, so it works. */
	while (*modes)
	{
		if (*modes == 'k')
			return TRUE;
		if (*modes == ' ')
			return FALSE;
		modes++;
	}
	return FALSE;
}
#endif

void
fe_set_title (session *sess)
{
	char tbuf[512];
	int type;

	if (sess->gui->is_tab && sess != current_tab)
		return;

	type = sess->type;

	if (sess->server->connected == FALSE && sess->type != SESS_DIALOG)
		goto def;

	switch (type)
	{
	case SESS_DIALOG:
		g_snprintf (tbuf, sizeof (tbuf), "%s %s @ %s - %s",
					 _("Dialog with"), sess->channel, server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	case SESS_SERVER:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "", server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	case SESS_CHANNEL:
		/* don't display keys in the titlebar */
			g_snprintf (tbuf, sizeof (tbuf),
					 "%s%s%s / %s%s%s%s - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "",
					 server_get_network (sess->server, TRUE), sess->channel,
					 prefs.hex_gui_win_modes && sess->current_modes ? " (" : "",
					 prefs.hex_gui_win_modes && sess->current_modes ? sess->current_modes : "",
					 prefs.hex_gui_win_modes && sess->current_modes ? ")" : "",
					 _(DISPLAY_NAME));
		if (prefs.hex_gui_win_ucount)
		{
			g_snprintf (tbuf + strlen (tbuf), 9, " (%d)", sess->total);
		}
		break;
	case SESS_NOTICES:
	case SESS_SNOTICES:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s (notices) - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "", server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	default:
	def:
		g_snprintf (tbuf, sizeof (tbuf), _(DISPLAY_NAME));
		gtk_window_set_title (GTK_WINDOW (sess->gui->window), tbuf);
		return;
	}

	gtk_window_set_title (GTK_WINDOW (sess->gui->window), tbuf);
}

#if HC_GTK4
/* GTK4: Window state changes are monitored differently - track via property notifications */
static void
mg_windowstate_cb (GObject *gobject, GParamSpec *pspec, gpointer userdata)
{
	GtkWindow *wid = GTK_WINDOW (gobject);
	GdkToplevelState state = 0;
	GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (wid));

	if (surface && GDK_IS_TOPLEVEL (surface))
		state = gdk_toplevel_get_state (GDK_TOPLEVEL (surface));

	/* GTK4 doesn't have minimize-to-tray in the same way - skip this check */

	prefs.hex_gui_win_state = 0;
	if (state & GDK_TOPLEVEL_STATE_MAXIMIZED)
		prefs.hex_gui_win_state = 1;

	prefs.hex_gui_win_fullscreen = 0;
	if (state & GDK_TOPLEVEL_STATE_FULLSCREEN)
		prefs.hex_gui_win_fullscreen = 1;

	menu_set_fullscreen (current_sess->gui, prefs.hex_gui_win_fullscreen);
}
#else
static gboolean
mg_windowstate_cb (GtkWindow *wid, GdkEventWindowState *event, gpointer userdata)
{
	if ((event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) &&
		 (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) &&
		 prefs.hex_gui_tray_minimize && prefs.hex_gui_tray &&
		 gtkutil_tray_icon_supported (wid))
	{
		tray_toggle_visibility (TRUE);
		gtk_window_deiconify (wid);
	}

	prefs.hex_gui_win_state = 0;
	if (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
		prefs.hex_gui_win_state = 1;

	prefs.hex_gui_win_fullscreen = 0;
	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
		prefs.hex_gui_win_fullscreen = 1;

	menu_set_fullscreen (current_sess->gui, prefs.hex_gui_win_fullscreen);

	return FALSE;
}
#endif

#if HC_GTK4
/* GTK4: Use "notify::default-width/height" signal - position isn't available */
static void
mg_configure_cb (GtkWidget *wid, GParamSpec *pspec, session *sess)
{
	if (sess == NULL)			/* for the main_window */
	{
		if (mg_gui)
		{
			if (prefs.hex_gui_win_save && !prefs.hex_gui_win_state && !prefs.hex_gui_win_fullscreen)
			{
				sess = current_sess;
				/* GTK4: Can't get position, only size */
				prefs.hex_gui_win_width = gtk_widget_get_width (wid);
				prefs.hex_gui_win_height = gtk_widget_get_height (wid);
			}
		}
	}

	if (sess)
	{
		if (sess->type == SESS_DIALOG && prefs.hex_gui_win_save)
		{
			/* GTK4: Can't get position, only size */
			prefs.hex_gui_dialog_width = gtk_widget_get_width (wid);
			prefs.hex_gui_dialog_height = gtk_widget_get_height (wid);
		}
	}
}
#else
static gboolean
mg_configure_cb (GtkWidget *wid, GdkEventConfigure *event, session *sess)
{
	if (sess == NULL)			/* for the main_window */
	{
		if (mg_gui)
		{
			if (prefs.hex_gui_win_save && !prefs.hex_gui_win_state && !prefs.hex_gui_win_fullscreen)
			{
				sess = current_sess;
				gtk_window_get_position (GTK_WINDOW (wid), &prefs.hex_gui_win_left,
												 &prefs.hex_gui_win_top);
				gtk_window_get_size (GTK_WINDOW (wid), &prefs.hex_gui_win_width,
											&prefs.hex_gui_win_height);
			}
		}
	}

	if (sess)
	{
		if (sess->type == SESS_DIALOG && prefs.hex_gui_win_save)
		{
			gtk_window_get_position (GTK_WINDOW (wid), &prefs.hex_gui_dialog_left,
											 &prefs.hex_gui_dialog_top);
			gtk_window_get_size (GTK_WINDOW (wid), &prefs.hex_gui_dialog_width,
										&prefs.hex_gui_dialog_height);
		}
	}

	return FALSE;
}
#endif

/* move to a non-irc tab */

static void
mg_show_generic_tab (GtkWidget *box)
{
	int num;
	GtkWidget *f = NULL;

	if (current_sess && gtk_widget_has_focus (current_sess->gui->input_box))
		f = current_sess->gui->input_box;

	num = hc_page_container_get_page_num (mg_gui->note_book, box);
	hc_page_container_set_current_page (mg_gui->note_book, num);
	gtk_tree_view_set_model (GTK_TREE_VIEW (mg_gui->user_tree), NULL);
	gtk_window_set_title (GTK_WINDOW (mg_gui->window),
								 g_object_get_data (G_OBJECT (box), "title"));
	gtk_widget_set_sensitive (mg_gui->menu, FALSE);

	if (f)
		gtk_widget_grab_focus (f);
}

/* a channel has been focused */

static void
mg_focus (session *sess)
{
	if (sess->gui->is_tab)
		current_tab = sess;
	current_sess = sess;

	/* dirty trick to avoid auto-selection */
	SPELL_ENTRY_SET_EDITABLE (sess->gui->input_box, FALSE);
	gtk_widget_grab_focus (sess->gui->input_box);
	SPELL_ENTRY_SET_EDITABLE (sess->gui->input_box, TRUE);

	sess->server->front_session = sess;

	if (sess->server->server_session != NULL)
	{
		if (sess->server->server_session->type != SESS_SERVER)
			sess->server->server_session = sess;
	} else
	{
		sess->server->server_session = sess;
	}

	/* when called via mg_changui_new, is_tab might be true, but
		sess->res->tab is still NULL. */
	if (sess->res->tab)
		fe_set_tab_color (sess, FE_COLOR_NONE);
}

static int
mg_progressbar_update (GtkWidget *bar)
{
	static int type = 0;
	static gdouble pos = 0;

	pos += 0.05;
	if (pos >= 0.99)
	{
		if (type == 0)
		{
			type = 1;
			/* GTK3: Use gtk_progress_bar_set_inverted instead of deprecated set_orientation */
			gtk_progress_bar_set_inverted ((GtkProgressBar *) bar, TRUE);
		} else
		{
			type = 0;
			gtk_progress_bar_set_inverted ((GtkProgressBar *) bar, FALSE);
		}
		pos = 0.05;
	}
	gtk_progress_bar_set_fraction ((GtkProgressBar *) bar, pos);
	return 1;
}

void
mg_progressbar_create (session_gui *gui)
{
	gui->bar = gtk_progress_bar_new ();
	hc_box_pack_start (gui->nick_box, gui->bar, 0, 0, 0);
	gtk_widget_show (gui->bar);
	gui->bartag = fe_timeout_add (50, mg_progressbar_update, gui->bar);
}

void
mg_progressbar_destroy (session_gui *gui)
{
	fe_timeout_remove (gui->bartag);
	hc_widget_destroy (gui->bar);
	gui->bar = 0;
	gui->bartag = 0;
}

/* switching tabs away from this one, so remember some info about it! */

static void
mg_unpopulate (session *sess)
{
	restore_gui *res;
	session_gui *gui;
	int i;

	gui = sess->gui;
	res = sess->res;

	res->input_text = g_strdup (SPELL_ENTRY_GET_TEXT (gui->input_box));
	res->topic_text = g_strdup (hc_entry_get_text (gui->topic_entry));
	res->limit_text = g_strdup (hc_entry_get_text (gui->limit_entry));
	res->key_text = g_strdup (hc_entry_get_text (gui->key_entry));
	if (gui->laginfo)
		res->lag_text = g_strdup (gtk_label_get_text (GTK_LABEL (gui->laginfo)));
	if (gui->throttleinfo)
		res->queue_text = g_strdup (gtk_label_get_text (GTK_LABEL (gui->throttleinfo)));

	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
		res->flag_wid_state[i] = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (gui->flag_wid[i]));

	res->old_ul_value = userlist_get_value (gui->user_tree);
	if (gui->lagometer)
		res->lag_value = gtk_progress_bar_get_fraction (
													GTK_PROGRESS_BAR (gui->lagometer));
	if (gui->throttlemeter)
		res->queue_value = gtk_progress_bar_get_fraction (
													GTK_PROGRESS_BAR (gui->throttlemeter));

	if (gui->bar)
	{
		res->c_graph = TRUE;	/* still have a graph, just not visible now */
		mg_progressbar_destroy (gui);
	}
}

static void
mg_restore_label (GtkWidget *label, char **text)
{
	if (!label)
		return;

	if (*text)
	{
		gtk_label_set_text (GTK_LABEL (label), *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		gtk_label_set_text (GTK_LABEL (label), "");
	}
}

static void
mg_restore_entry (GtkWidget *entry, char **text)
{
	if (*text)
	{
		hc_entry_set_text (entry, *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		hc_entry_set_text (entry, "");
	}
	gtk_editable_set_position (GTK_EDITABLE (entry), -1);
}

static void
mg_restore_speller (GtkWidget *entry, char **text)
{
	if (*text)
	{
		SPELL_ENTRY_SET_TEXT (entry, *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		SPELL_ENTRY_SET_TEXT (entry, "");
	}
	SPELL_ENTRY_SET_POS (entry, -1);
}

void
mg_set_topic_tip (session *sess)
{
	char *text;

	switch (sess->type)
	{
	case SESS_CHANNEL:
		if (sess->topic)
		{
			text = g_strdup_printf (_("Topic for %s is: %s"), sess->channel,
						 sess->topic);
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, text);
			g_free (text);
		} else
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, _("No topic is set"));
		break;
	default:
		if (hc_entry_get_text (sess->gui->topic_entry) &&
			 hc_entry_get_text (sess->gui->topic_entry)[0])
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, (char *)hc_entry_get_text (sess->gui->topic_entry));
		else
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, NULL);
	}
}

static void
mg_hide_empty_pane (GtkPaned *pane)
{
	GtkWidget *child1 = gtk_paned_get_child1 (pane);
	GtkWidget *child2 = gtk_paned_get_child2 (pane);
	gboolean child1_visible = (child1 != NULL && gtk_widget_get_visible (child1));
	gboolean child2_visible = (child2 != NULL && gtk_widget_get_visible (child2));

	if (!child1_visible && !child2_visible)
	{
		gtk_widget_hide (GTK_WIDGET (pane));
		return;
	}

	gtk_widget_show (GTK_WIDGET (pane));
}

static void
mg_hide_empty_boxes (session_gui *gui)
{
	/* hide empty vpanes - so the handle is not shown */
	mg_hide_empty_pane ((GtkPaned*)gui->vpane_right);
	mg_hide_empty_pane ((GtkPaned*)gui->vpane_left);
}

static void
mg_userlist_showhide (session *sess, int show)
{
	session_gui *gui = sess->gui;
	int handle_size;
	int right_size;
	GtkAllocation allocation;

	right_size = MAX (prefs.hex_gui_pane_right_size, prefs.hex_gui_pane_right_size_min);

	if (show)
	{
		gtk_widget_show (gui->user_box);
		gui->ul_hidden = 0;

		gtk_widget_get_allocation (gui->hpane_right, &allocation);
		gtk_widget_style_get (GTK_WIDGET (gui->hpane_right), "handle-size", &handle_size, NULL);
		gtk_paned_set_position (GTK_PANED (gui->hpane_right), allocation.width - (right_size + handle_size));
	}
	else
	{
		gtk_widget_hide (gui->user_box);
		gui->ul_hidden = 1;
	}

	mg_hide_empty_boxes (gui);
}

static gboolean
mg_is_userlist_and_tree_combined (void)
{
	if (prefs.hex_gui_tab_pos == POS_TOPLEFT && prefs.hex_gui_ulist_pos == POS_BOTTOMLEFT)
		return TRUE;
	if (prefs.hex_gui_tab_pos == POS_BOTTOMLEFT && prefs.hex_gui_ulist_pos == POS_TOPLEFT)
		return TRUE;

	if (prefs.hex_gui_tab_pos == POS_TOPRIGHT && prefs.hex_gui_ulist_pos == POS_BOTTOMRIGHT)
		return TRUE;
	if (prefs.hex_gui_tab_pos == POS_BOTTOMRIGHT && prefs.hex_gui_ulist_pos == POS_TOPRIGHT)
		return TRUE;

	return FALSE;
}

/* decide if the userlist should be shown or hidden for this tab */

void
mg_decide_userlist (session *sess, gboolean switch_to_current)
{
	/* when called from menu.c we need this */
	if (sess->gui == mg_gui && switch_to_current)
		sess = current_tab;

	if (prefs.hex_gui_ulist_hide)
	{
		mg_userlist_showhide (sess, FALSE);
		return;
	}

	switch (sess->type)
	{
	case SESS_SERVER:
	case SESS_DIALOG:
	case SESS_NOTICES:
	case SESS_SNOTICES:
		if (mg_is_userlist_and_tree_combined ())
			mg_userlist_showhide (sess, TRUE);	/* show */
		else
			mg_userlist_showhide (sess, FALSE);	/* hide */
		break;
	default:		
		mg_userlist_showhide (sess, TRUE);	/* show */
	}
}

static int ul_tag = 0;

static gboolean
mg_populate_userlist (session *sess)
{
	if (!sess)
		sess = current_tab;

	if (is_session (sess))
	{
		if (sess->type == SESS_DIALOG)
			mg_set_access_icon (sess->gui, NULL, sess->server->is_away);
		else
			mg_set_access_icon (sess->gui, get_user_icon (sess->server, sess->me), sess->server->is_away);
		userlist_show (sess);
		userlist_set_value (sess->gui->user_tree, sess->res->old_ul_value);
	}

	ul_tag = 0;
	return 0;
}

/* fill the irc tab with a new channel */

static void
mg_populate (session *sess)
{
	session_gui *gui = sess->gui;
	restore_gui *res = sess->res;
	int i, render = TRUE;
	guint16 vis = gui->ul_hidden;
	GtkAllocation allocation;

	switch (sess->type)
	{
	case SESS_DIALOG:
		/* show the dialog buttons */
		gtk_widget_show (gui->dialogbutton_box);
		/* hide the chan-mode buttons */
		gtk_widget_hide (gui->topicbutton_box);
		/* hide the userlist */
		mg_decide_userlist (sess, FALSE);
		/* shouldn't edit the topic */
		gtk_editable_set_editable (GTK_EDITABLE (gui->topic_entry), FALSE);
		/* might be hidden from server tab */
		if (prefs.hex_gui_topicbar)
			gtk_widget_show (gui->topic_bar);
		break;
	case SESS_SERVER:
		if (prefs.hex_gui_mode_buttons)
			gtk_widget_show (gui->topicbutton_box);
		/* hide the dialog buttons */
		gtk_widget_hide (gui->dialogbutton_box);
		/* hide the userlist */
		mg_decide_userlist (sess, FALSE);
		/* servers don't have topics */
		gtk_widget_hide (gui->topic_bar);
		break;
	default:
		/* hide the dialog buttons */
		gtk_widget_hide (gui->dialogbutton_box);
		/* show the userlist */
		mg_decide_userlist (sess, FALSE);
		/* let the topic be editted */
		gtk_editable_set_editable (GTK_EDITABLE (gui->topic_entry), TRUE);
		if (prefs.hex_gui_topicbar)
			gtk_widget_show (gui->topic_bar);
		/* Show mode buttons after topic_bar is visible */
		if (prefs.hex_gui_mode_buttons)
		{
#if HC_GTK4
			/* GTK4: Force visibility state change by hiding then showing */
			gtk_widget_set_visible (gui->topicbutton_box, FALSE);
			gtk_widget_set_visible (gui->topicbutton_box, TRUE);
			/* Also force visibility of all children */
			{
				GtkWidget *child;
				for (child = gtk_widget_get_first_child (gui->topicbutton_box);
				     child != NULL;
				     child = gtk_widget_get_next_sibling (child))
				{
					gtk_widget_set_visible (child, TRUE);
				}
			}
#else
			gtk_widget_show (gui->topicbutton_box);
#endif
		}
	}

	/* move to THE irc tab */
	if (gui->is_tab)
		hc_page_container_set_current_page (gui->note_book, 0);

	/* xtext size change? Then don't render, wait for the expose caused
      by showing/hidding the userlist */
	gtk_widget_get_allocation (gui->user_box, &allocation);
	if (vis != gui->ul_hidden && allocation.width > 1)
		render = FALSE;

	gtk_xtext_buffer_show (GTK_XTEXT (gui->xtext), res->buffer, render);

	if (gui->is_tab)
		gtk_widget_set_sensitive (gui->menu, TRUE);

	/* restore all the GtkEntry's */
	mg_restore_entry (gui->topic_entry, &res->topic_text);
	mg_restore_speller (gui->input_box, &res->input_text);
	mg_restore_entry (gui->key_entry, &res->key_text);
	mg_restore_entry (gui->limit_entry, &res->limit_text);
	mg_restore_label (gui->laginfo, &res->lag_text);
	mg_restore_label (gui->throttleinfo, &res->queue_text);

	mg_focus (sess);
	fe_set_title (sess);

	/* this one flickers, so only change if necessary */
	if (strcmp (sess->server->nick, gtk_button_get_label (GTK_BUTTON (gui->nick_label))) != 0)
		gtk_button_set_label (GTK_BUTTON (gui->nick_label), sess->server->nick);

	/* this is slow, so make it a timeout event */
	if (!gui->is_tab)
	{
		mg_populate_userlist (sess);
	} else
	{
		if (ul_tag == 0)
			ul_tag = g_idle_add ((GSourceFunc)mg_populate_userlist, NULL);
	}

	fe_userlist_numbers (sess);

	/* restore all the channel mode buttons */
	ignore_chanmode = TRUE;
	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
	{
		/* Hide if mode not supported */
		if (sess->server && strchr (sess->server->chanmodes, chan_flags[i]) == NULL)
			gtk_widget_hide (sess->gui->flag_wid[i]);
		else
			gtk_widget_show (sess->gui->flag_wid[i]);

		/* Update state */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gui->flag_wid[i]),
									res->flag_wid_state[i]);
	}
	ignore_chanmode = FALSE;

	if (gui->lagometer)
	{
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (gui->lagometer),
												 res->lag_value);
		if (res->lag_tip)
			gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->lagometer), res->lag_tip);
	}
	if (gui->throttlemeter)
	{
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (gui->throttlemeter),
												 res->queue_value);
		if (res->queue_tip)
			gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->throttlemeter), res->queue_tip);
	}

	/* did this tab have a connecting graph? restore it.. */
	if (res->c_graph)
	{
		res->c_graph = FALSE;
		mg_progressbar_create (gui);
	}

	/* menu items */
	menu_set_away (gui, sess->server->is_away);
	gtk_widget_set_sensitive (gui->menu_item[MENU_ID_AWAY], sess->server->connected);
	gtk_widget_set_sensitive (gui->menu_item[MENU_ID_JOIN], sess->server->end_of_motd);
	gtk_widget_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT],
									  sess->server->connected || sess->server->recondelay_tag);

	mg_set_topic_tip (sess);

	plugin_emit_dummy_print (sess, "Focus Tab");
}

void
mg_bring_tofront_sess (session *sess)	/* IRC tab or window */
{
	if (sess->gui->is_tab)
		chan_focus (sess->res->tab);
	else
		gtk_window_present (GTK_WINDOW (sess->gui->window));
}

void
mg_bring_tofront (GtkWidget *vbox)	/* non-IRC tab or window */
{
	chan *ch;

	ch = g_object_get_data (G_OBJECT (vbox), "ch");
	if (ch)
		chan_focus (ch);
	else
		gtk_window_present (GTK_WINDOW (gtk_widget_get_toplevel (vbox)));
}

void
mg_switch_page (int relative, int num)
{
	if (mg_gui)
		chanview_move_focus (mg_gui->chanview, relative, num);
}

/* a toplevel IRC window was destroyed */

static void
mg_topdestroy_cb (GtkWidget *win, session *sess)
{
	session_free (sess);	/* tell hexchat.c about it */
}

/* cleanup an IRC tab */

static void
mg_ircdestroy (session *sess)
{
	GSList *list;

	session_free (sess);	/* tell hexchat.c about it */

	if (mg_gui == NULL)
	{
/*		puts("-> mg_gui is already NULL");*/
		return;
	}

	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (sess->gui->is_tab)
		{
/*			puts("-> some tabs still remain");*/
			return;
		}
		list = list->next;
	}

/*	puts("-> no tabs left, killing main tabwindow");*/
	hc_window_destroy (mg_gui->window);
	active_tab = NULL;
	mg_gui = NULL;
	parent_window = NULL;
}

static void
mg_tab_close_cb (GtkWidget *dialog, gint arg1, session *sess)
{
	GSList *list, *next;

	hc_window_destroy (dialog);
	if (arg1 == GTK_RESPONSE_OK && is_session (sess))
	{
		/* force it NOT to send individual PARTs */
		sess->server->sent_quit = TRUE;

		for (list = sess_list; list;)
		{
			next = list->next;
			if (((session *)list->data)->server == sess->server &&
				 ((session *)list->data) != sess)
				fe_close_window ((session *)list->data);
			list = next;
		}

		/* just send one QUIT - better for BNCs */
		sess->server->sent_quit = FALSE;
		fe_close_window (sess);
	}
}

void
mg_tab_close (session *sess)
{
	GtkWidget *dialog;
	GSList *list;
	int i;

	if (chan_remove (sess->res->tab, FALSE))
	{
		sess->res->tab = NULL;
		mg_ircdestroy (sess);
	}
	else
	{
		for (i = 0, list = sess_list; list; list = list->next)
		{
			session *s = (session*)list->data;
			if (s->server == sess->server && (s->type == SESS_CHANNEL || s->type == SESS_DIALOG))
				i++;
		}
		dialog = gtk_message_dialog_new (GTK_WINDOW (parent_window), 0,
						GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
						_("This server still has %d channels or dialogs associated with it. "
						  "Close them all?"), i);
		g_signal_connect (G_OBJECT (dialog), "response",
								G_CALLBACK (mg_tab_close_cb), sess);
		if (prefs.hex_gui_tab_layout)
		{
			hc_window_set_position (dialog, GTK_WIN_POS_MOUSE);
		}
		else
		{
			hc_window_set_position (dialog, GTK_WIN_POS_CENTER_ON_PARENT);
		}
		gtk_widget_show (dialog);
	}
}

/* GTK3-only: Menu destruction callback */
#if !HC_GTK4
static void
mg_menu_destroy (GtkWidget *menu, gpointer userdata)
{
	hc_widget_destroy (menu);
	g_object_unref (menu);
}
#endif

void
mg_create_icon_item (char *label, char *stock, GtkWidget *menu,
							void *callback, void *userdata)
{
	GtkWidget *item;

	item = create_icon_menu (label, stock, TRUE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (callback),
							userdata);
	gtk_widget_show (item);
}

static int
mg_count_networks (void)
{
	int cons = 0;
	GSList *list;

	for (list = serv_list; list; list = list->next)
	{
		if (((server *)list->data)->connected)
			cons++;
	}
	return cons;
}

static int
mg_count_dccs (void)
{
	GSList *list;
	struct DCC *dcc;
	int dccs = 0;

	list = dcc_list;
	while (list)
	{
		dcc = list->data;
		if ((dcc->type == TYPE_SEND || dcc->type == TYPE_RECV) &&
			 dcc->dccstat == STAT_ACTIVE)
			dccs++;
		list = list->next;
	}

	return dccs;
}

#if HC_GTK4
/*
 * GTK4 version of quit dialog - uses async response handling.
 * gtk_dialog_run(), gtk_dialog_get_action_area(), gtk_button_box_set_layout(),
 * gtk_container_set_border_width(), GTK_ICON_SIZE_DIALOG all removed in GTK4.
 */
static GtkWidget *quit_dialog = NULL;
static GtkWidget *quit_dialog_checkbox = NULL;

static void
mg_quit_dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	switch (response_id)
	{
	case 0: /* Quit */
		if (gtk_check_button_get_active (GTK_CHECK_BUTTON (quit_dialog_checkbox)))
			prefs.hex_gui_quit_dialog = 0;
		hexchat_exit ();
		break;
	case 1: /* minimize to tray */
		if (gtk_check_button_get_active (GTK_CHECK_BUTTON (quit_dialog_checkbox)))
		{
			prefs.hex_gui_tray_close = 1;
		}
		/* force tray icon ON, if not already */
		if (!prefs.hex_gui_tray)
		{
			prefs.hex_gui_tray = 1;
			tray_apply_setup ();
		}
		tray_toggle_visibility (TRUE);
		break;
	}

	hc_window_destroy (GTK_WIDGET (dialog));
	quit_dialog = NULL;
	quit_dialog_checkbox = NULL;
}

void
mg_open_quit_dialog (gboolean minimize_button)
{
	GtkWidget *dialog_vbox1;
	GtkWidget *table1;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *button;
	char *text, *connecttext;
	int cons;
	int dccs;

	if (quit_dialog)
	{
		gtk_window_present (GTK_WINDOW (quit_dialog));
		return;
	}

	dccs = mg_count_dccs ();
	cons = mg_count_networks ();
	if (dccs + cons == 0 || !prefs.hex_gui_quit_dialog)
	{
		hexchat_exit ();
		return;
	}

	quit_dialog = gtk_dialog_new ();
	/* GTK4: use CSS margins instead of gtk_container_set_border_width */
	gtk_widget_set_margin_start (quit_dialog, 6);
	gtk_widget_set_margin_end (quit_dialog, 6);
	gtk_widget_set_margin_top (quit_dialog, 6);
	gtk_widget_set_margin_bottom (quit_dialog, 6);
	gtk_window_set_title (GTK_WINDOW (quit_dialog), _("Quit HexChat?"));
	gtk_window_set_transient_for (GTK_WINDOW (quit_dialog), GTK_WINDOW (parent_window));
	gtk_window_set_resizable (GTK_WINDOW (quit_dialog), FALSE);

	dialog_vbox1 = gtk_dialog_get_content_area (GTK_DIALOG (quit_dialog));

	table1 = gtk_grid_new ();
	hc_box_add (GTK_BOX (dialog_vbox1), table1);
	gtk_widget_set_margin_start (table1, 6);
	gtk_widget_set_margin_end (table1, 6);
	gtk_widget_set_margin_top (table1, 6);
	gtk_widget_set_margin_bottom (table1, 6);
	gtk_grid_set_row_spacing (GTK_GRID (table1), 12);
	gtk_grid_set_column_spacing (GTK_GRID (table1), 12);

	/* GTK4: gtk_image_new_from_icon_name no longer takes icon size */
	image = gtk_image_new_from_icon_name ("dialog-warning");
	gtk_image_set_icon_size (GTK_IMAGE (image), GTK_ICON_SIZE_LARGE);
	gtk_grid_attach (GTK_GRID (table1), image, 0, 0, 1, 1);

	quit_dialog_checkbox = gtk_check_button_new_with_mnemonic (_("Don't ask next time."));
	gtk_widget_set_hexpand (quit_dialog_checkbox, TRUE);
	gtk_widget_set_margin_top (quit_dialog_checkbox, 4);
	gtk_grid_attach (GTK_GRID (table1), quit_dialog_checkbox, 0, 1, 2, 1);

	connecttext = g_strdup_printf (_("You are connected to %i IRC networks."), cons);
	text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n%s",
								_("Are you sure you want to quit?"),
								cons ? connecttext : "",
								dccs ? _("Some file transfers are still active.") : "");
	g_free (connecttext);
	label = gtk_label_new (text);
	g_free (text);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_vexpand (label, TRUE);
	gtk_grid_attach (GTK_GRID (table1), label, 1, 0, 1, 1);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	/* GTK4: Use gtk_dialog_add_button instead of accessing action area */
	if (minimize_button && gtkutil_tray_icon_supported (GTK_WINDOW(quit_dialog)))
	{
		gtk_dialog_add_button (GTK_DIALOG (quit_dialog), _("_Minimize to Tray"), 1);
	}

	button = gtk_dialog_add_button (GTK_DIALOG (quit_dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_widget_grab_focus (button);

	gtk_dialog_add_button (GTK_DIALOG (quit_dialog), _("_Quit"), 0);

	g_signal_connect (G_OBJECT (quit_dialog), "response",
					  G_CALLBACK (mg_quit_dialog_response_cb), NULL);

	gtk_window_present (GTK_WINDOW (quit_dialog));
}

#else /* GTK3 */

void
mg_open_quit_dialog (gboolean minimize_button)
{
	static GtkWidget *dialog = NULL;
	GtkWidget *dialog_vbox1;
	GtkWidget *table1;
	GtkWidget *image;
	GtkWidget *checkbutton1;
	GtkWidget *label;
	GtkWidget *dialog_action_area1;
	GtkWidget *button;
	char *text, *connecttext;
	int cons;
	int dccs;

	if (dialog)
	{
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	dccs = mg_count_dccs ();
	cons = mg_count_networks ();
	if (dccs + cons == 0 || !prefs.hex_gui_quit_dialog)
	{
		hexchat_exit ();
		return;
	}

	dialog = gtk_dialog_new ();
	hc_container_set_border_width (dialog, 6);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Quit HexChat?"));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	dialog_vbox1 = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_widget_show (dialog_vbox1);

	table1 = gtk_grid_new ();
	gtk_widget_show (table1);
	hc_box_pack_start (dialog_vbox1, table1, TRUE, TRUE, 0);
	hc_container_set_border_width (table1, 6);
	gtk_grid_set_row_spacing (GTK_GRID (table1), 12);
	gtk_grid_set_column_spacing (GTK_GRID (table1), 12);

	image = hc_image_new_from_icon_name ("dialog-warning", GTK_ICON_SIZE_DIALOG);
	gtk_widget_show (image);
	gtk_grid_attach (GTK_GRID (table1), image, 0, 0, 1, 1);

	checkbutton1 = gtk_check_button_new_with_mnemonic (_("Don't ask next time."));
	gtk_widget_show (checkbutton1);
	gtk_widget_set_hexpand (checkbutton1, TRUE);
	gtk_widget_set_margin_top (checkbutton1, 4);
	gtk_grid_attach (GTK_GRID (table1), checkbutton1, 0, 1, 2, 1);

	connecttext = g_strdup_printf (_("You are connected to %i IRC networks."), cons);
	text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n%s",
								_("Are you sure you want to quit?"),
								cons ? connecttext : "",
								dccs ? _("Some file transfers are still active.") : "");
	g_free (connecttext);
	label = gtk_label_new (text);
	g_free (text);
	gtk_widget_show (label);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_vexpand (label, TRUE);
	gtk_grid_attach (GTK_GRID (table1), label, 1, 0, 1, 1);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	dialog_action_area1 = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
	gtk_widget_show (dialog_action_area1);
	hc_button_box_set_layout (dialog_action_area1,
										GTK_BUTTONBOX_END);

	if (minimize_button && gtkutil_tray_icon_supported (GTK_WINDOW(dialog)))
	{
		button = gtk_button_new_with_mnemonic (_("_Minimize to Tray"));
		gtk_widget_show (button);
		gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, 1);
	}

	button = gtk_button_new_with_mnemonic (_("_Cancel"));
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
											GTK_RESPONSE_CANCEL);
	gtk_widget_grab_focus (button);

	button = gtk_button_new_with_mnemonic (_("_Quit"));
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, 0);

	gtk_widget_show (dialog);

	switch (gtk_dialog_run (GTK_DIALOG (dialog)))
	{
	case 0:
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton1)))
			prefs.hex_gui_quit_dialog = 0;
		hexchat_exit ();
		break;
	case 1: /* minimize to tray */
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton1)))
		{
			prefs.hex_gui_tray_close = 1;
			/*prefs.hex_gui_quit_dialog = 0;*/
		}
		/* force tray icon ON, if not already */
		if (!prefs.hex_gui_tray)
		{
			prefs.hex_gui_tray = 1;
			tray_apply_setup ();
		}
		tray_toggle_visibility (TRUE);
		break;
	}

	hc_window_destroy (dialog);
	dialog = NULL;
}
#endif /* HC_GTK4 */

void
mg_close_sess (session *sess)
{
	if (sess_list->next == NULL)
	{
		mg_open_quit_dialog (FALSE);
		return;
	}

	fe_close_window (sess);
}

static int
mg_chan_remove (chan *ch)
{
	/* remove the tab from chanview */
	chan_remove (ch, TRUE);
	/* any tabs left? */
	if (chanview_get_size (mg_gui->chanview) < 1)
	{
		/* if not, destroy the main tab window */
		hc_window_destroy (mg_gui->window);
		current_tab = NULL;
		active_tab = NULL;
		mg_gui = NULL;
		parent_window = NULL;
		return TRUE;
	}
	return FALSE;
}

/* destroy non-irc tab/window */

static void
mg_close_gen (chan *ch, GtkWidget *box)
{
	if (!ch)
		ch = g_object_get_data (G_OBJECT (box), "ch");
	if (ch)
	{
		/* remove from notebook */
		hc_widget_destroy (box);
		/* remove the tab from chanview */
		mg_chan_remove (ch);
	} else
	{
		hc_window_destroy (gtk_widget_get_toplevel (box));
	}
}

/* the "X" close button has been pressed (tab-view) */

static void
mg_xbutton_cb (chanview *cv, chan *ch, int tag, gpointer userdata)
{
	if (tag == TAG_IRC)	/* irc tab */
		mg_close_sess (userdata);
	else						/* non-irc utility tab */
		mg_close_gen (ch, userdata);
}

static void
mg_link_gentab (chan *ch, GtkWidget *box)
{
	int num;
	GtkWidget *win;

	g_object_ref (box);

	num = hc_page_container_get_page_num (mg_gui->note_book, box);
	hc_page_container_remove_page (mg_gui->note_book, num);
	mg_chan_remove (ch);

	win = gtkutil_window_new (g_object_get_data (G_OBJECT (box), "title"), "",
									  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "w")),
									  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "h")),
									  2);
	/* so it doesn't try to chan_remove (there's no tab anymore) */
	g_object_steal_data (G_OBJECT (box), "ch");
	gtk_container_set_border_width (GTK_CONTAINER (box), 0);
	hc_window_set_child (win, box);
	gtk_widget_show (win);

	g_object_unref (box);
}

static void
mg_detach_tab_cb (GtkWidget *item, chan *ch)
{
	if (chan_get_tag (ch) == TAG_IRC)	/* IRC tab */
	{
		/* userdata is session * */
		mg_link_irctab (chan_get_userdata (ch), 1);
		return;
	}

	/* userdata is GtkWidget * */
	mg_link_gentab (ch, chan_get_userdata (ch));	/* non-IRC tab */
}

static void
mg_destroy_tab_cb (GtkWidget *item, chan *ch)
{
	/* treat it just like the X button press */
	mg_xbutton_cb (mg_gui->chanview, ch, chan_get_tag (ch), chan_get_userdata (ch));
}

static void
mg_color_insert (GtkWidget *item, gpointer userdata)
{
	char buf[32];
	char *text;
	int num = GPOINTER_TO_INT (userdata);

	if (num > 99)
	{
		switch (num)
		{
		case 100:
			text = "\002"; break;
		case 101:
			text = "\037"; break;
		case 102:
			text = "\035"; break;
		case 103:
			text = "\036"; break;
		default:
			text = "\017"; break;
		}
		key_action_insert (current_sess->gui->input_box, 0, text, 0, 0);
	} else
	{
		sprintf (buf, "\003%02d", num);
		key_action_insert (current_sess->gui->input_box, 0, buf, 0, 0);
	}
}

static void
mg_markup_item (GtkWidget *menu, char *text, int arg)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label ("");
	gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), text);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (mg_color_insert), GINT_TO_POINTER (arg));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
}

GtkWidget *
mg_submenu (GtkWidget *menu, char *text)
{
	GtkWidget *submenu, *item;

	item = gtk_menu_item_new_with_mnemonic (text);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	submenu = gtk_menu_new ();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_widget_show (submenu);

	return submenu;
}

static void
mg_create_color_menu (GtkWidget *menu, session *sess)
{
	GtkWidget *submenu;
	GtkWidget *subsubmenu;
	char buf[256];
	int i;

	submenu = mg_submenu (menu, _("Insert Attribute or Color Code"));

	mg_markup_item (submenu, _("<b>Bold</b>"), 100);
	mg_markup_item (submenu, _("<u>Underline</u>"), 101);
	mg_markup_item (submenu, _("<i>Italic</i>"), 102);
	mg_markup_item (submenu, _("<s>Strikethrough</s>"), 103);
	mg_markup_item (submenu, _("Normal"), 999);

	subsubmenu = mg_submenu (submenu, _("Colors 0-7"));

	for (i = 0; i < 8; i++)
	{
		/* GdkRGBA uses 0.0-1.0 floats, convert to 0-255 for HTML color */
		sprintf (buf, "<tt><sup>%02d</sup> <span background=\"#%02x%02x%02x\">"
					"   </span></tt>",
				i, (int)(colors[i].red * 255), (int)(colors[i].green * 255), (int)(colors[i].blue * 255));
		mg_markup_item (subsubmenu, buf, i);
	}

	subsubmenu = mg_submenu (submenu, _("Colors 8-15"));

	for (i = 8; i < 16; i++)
	{
		/* GdkRGBA uses 0.0-1.0 floats, convert to 0-255 for HTML color */
		sprintf (buf, "<tt><sup>%02d</sup> <span background=\"#%02x%02x%02x\">"
					"   </span></tt>",
				i, (int)(colors[i].red * 255), (int)(colors[i].green * 255), (int)(colors[i].blue * 255));
		mg_markup_item (subsubmenu, buf, i);
	}
}

#if HC_GTK4
static void
mg_set_guint8 (GtkCheckButton *item, guint8 *setting)
{
	session *sess = current_sess;
	guint8 logging = sess->text_logging;

	*setting = SET_OFF;
	if (gtk_check_button_get_active (item))
		*setting = SET_ON;

	/* has the logging setting changed? */
	if (logging != sess->text_logging)
		log_open_or_close (sess);

	chanopt_save (sess);
	chanopt_save_all (FALSE);
}
#else
static void
mg_set_guint8 (GtkCheckMenuItem *item, guint8 *setting)
{
	session *sess = current_sess;
	guint8 logging = sess->text_logging;

	*setting = SET_OFF;
	if (gtk_check_menu_item_get_active (item))
		*setting = SET_ON;

	/* has the logging setting changed? */
	if (logging != sess->text_logging)
		log_open_or_close (sess);

	chanopt_save (sess);
	chanopt_save_all (FALSE);
}
#endif

static void
mg_perchan_menu_item (char *label, GtkWidget *menu, guint8 *setting, guint global)
{
	guint8 initial_value = *setting;

	/* if it's using global value, use that as initial state */
	if (initial_value == SET_DEFAULT)
		initial_value = global;

	menu_toggle_item (label, menu, mg_set_guint8, setting, initial_value);
}

static void
mg_create_perchannelmenu (session *sess, GtkWidget *menu)
{
	GtkWidget *submenu;

	submenu = menu_quick_sub (_("_Settings"), menu, NULL, XCMENU_MNEMONIC, -1);

	mg_perchan_menu_item (_("_Log to Disk"), submenu, &sess->text_logging, prefs.hex_irc_logging);
	mg_perchan_menu_item (_("_Reload Scrollback"), submenu, &sess->text_scrollback, prefs.hex_text_replay);
	if (sess->type == SESS_CHANNEL)
	{
		mg_perchan_menu_item (_("Strip _Colors"), submenu, &sess->text_strip, prefs.hex_text_stripcolor_msg);
		mg_perchan_menu_item (_("_Hide Join/Part Messages"), submenu, &sess->text_hidejoinpart, prefs.hex_irc_conf_mode);
	}
}

static void
mg_create_alertmenu (session *sess, GtkWidget *menu)
{
	GtkWidget *submenu;
	int hex_balloon, hex_beep, hex_tray, hex_flash;


	switch (sess->type) {
		case SESS_DIALOG:
			hex_balloon = prefs.hex_input_balloon_priv;
			hex_beep = prefs.hex_input_beep_priv;
			hex_tray = prefs.hex_input_tray_priv;
			hex_flash = prefs.hex_input_flash_priv;
			break;
		default:
			hex_balloon = prefs.hex_input_balloon_chans;
			hex_beep = prefs.hex_input_beep_chans;
			hex_tray = prefs.hex_input_tray_chans;
			hex_flash = prefs.hex_input_flash_chans;
	}

	submenu = menu_quick_sub(_("_Extra Alerts"), menu, NULL, XCMENU_MNEMONIC, -1);

	mg_perchan_menu_item(_("Show Notifications"), submenu, &sess->alert_balloon, hex_balloon);

	mg_perchan_menu_item(_("Beep on _Message"), submenu, &sess->alert_beep, hex_beep);

	mg_perchan_menu_item(_("Blink Tray _Icon"), submenu, &sess->alert_tray, hex_tray);

	mg_perchan_menu_item(_("Blink Task _Bar"), submenu, &sess->alert_taskbar, hex_flash);
}

/* GTK3-only: Tab context menu */
#if !HC_GTK4
static void
mg_create_tabmenu (session *sess, GdkEventButton *event, chan *ch)
{
	GtkWidget *menu, *item;
	char buf[256];

	menu = gtk_menu_new ();

	if (sess)
	{
		char *name = g_markup_escape_text (sess->channel[0] ? sess->channel : _("<none>"), -1);
		g_snprintf (buf, sizeof (buf), "<span foreground=\"#3344cc\"><b>%s</b></span>", name);
		g_free (name);

		item = gtk_menu_item_new_with_label ("");
		gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), buf);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);

		/* separator */
		menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);

		/* per-channel alerts */
		mg_create_alertmenu (sess, menu);

		/* per-channel settings */
		mg_create_perchannelmenu (sess, menu);

		/* separator */
		menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);

		if (sess->type == SESS_CHANNEL)
			menu_addfavoritemenu (sess->server, menu, sess->channel, TRUE);
		else if (sess->type == SESS_SERVER)
			menu_addconnectmenu (sess->server, menu);
	}

	mg_create_icon_item (_("_Detach"), "edit-redo", menu,
								mg_detach_tab_cb, ch);
	mg_create_icon_item (_("_Close"), "window-close", menu,
								mg_destroy_tab_cb, ch);
	if (sess && tabmenu_list)
		menu_create (menu, tabmenu_list, sess->channel, FALSE);
	if (sess)
		menu_add_plugin_items (menu, "\x4$TAB", sess->channel);

	if (event->window)
		gtk_menu_set_screen (GTK_MENU (menu), gdk_window_get_screen (event->window));
	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (G_OBJECT (menu), "selection-done",
							G_CALLBACK (mg_menu_destroy), NULL);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, event->time);
}
#else
/* GTK4: Tab menu action callbacks and context state */
static session *tab_menu_sess = NULL;
static chan *tab_menu_ch = NULL;

static void
tab_action_detach (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (tab_menu_ch)
	{
		if (chan_get_tag (tab_menu_ch) == TAG_IRC)
			mg_link_irctab (chan_get_userdata (tab_menu_ch), 1);
		else
			mg_link_gentab (tab_menu_ch, chan_get_userdata (tab_menu_ch));
	}
}

static void
tab_action_close (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (tab_menu_ch)
		mg_xbutton_cb (mg_gui->chanview, tab_menu_ch, chan_get_tag (tab_menu_ch),
						   chan_get_userdata (tab_menu_ch));
}

/* Alert toggle actions - use stateful actions with boolean state */
static void
tab_action_alert_balloon (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->alert_balloon = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_alert_beep (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->alert_beep = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_alert_tray (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->alert_tray = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_alert_taskbar (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->alert_taskbar = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

/* Settings toggle actions */
static void
tab_action_logging (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		guint8 old_logging = tab_menu_sess->text_logging;
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->text_logging = new_state ? SET_ON : SET_OFF;
		if (old_logging != tab_menu_sess->text_logging)
			log_open_or_close (tab_menu_sess);
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_scrollback (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->text_scrollback = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_strip_colors (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->text_strip = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

static void
tab_action_hide_joinpart (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		tab_menu_sess->text_hidejoinpart = new_state ? SET_ON : SET_OFF;
		chanopt_save (tab_menu_sess);
		chanopt_save_all (FALSE);
		g_variant_unref (state);
	}
}

/* Autojoin toggle action */
static void
tab_action_autojoin (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess && tab_menu_sess->server && tab_menu_sess->server->network)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		servlist_autojoinedit (tab_menu_sess->server->network, tab_menu_sess->channel, new_state);
		g_variant_unref (state);
	}
}

/* Auto-connect toggle action (for server tabs) */
static void
tab_action_autoconnect (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)parameter; (void)user_data;
	if (tab_menu_sess && tab_menu_sess->server && tab_menu_sess->server->network)
	{
		GVariant *state = g_action_get_state (G_ACTION (action));
		gboolean new_state = !g_variant_get_boolean (state);
		g_simple_action_set_state (action, g_variant_new_boolean (new_state));
		if (new_state)
			((ircnet*)tab_menu_sess->server->network)->flags |= FLAG_AUTO_CONNECT;
		else
			((ircnet*)tab_menu_sess->server->network)->flags &= ~FLAG_AUTO_CONNECT;
		servlist_save ();
		g_variant_unref (state);
	}
}

static void
tab_menu_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	GSimpleActionGroup *action_group = G_SIMPLE_ACTION_GROUP (user_data);
	(void)popover;
	if (action_group)
		g_object_unref (action_group);
}

/* Helper to get the effective boolean state for per-channel settings */
static gboolean
tab_get_setting_state (guint8 setting, guint global_default)
{
	if (setting == SET_DEFAULT)
		return global_default ? TRUE : FALSE;
	return (setting == SET_ON);
}

static void
mg_create_tabmenu (session *sess, chan *ch, GtkWidget *parent, double x, double y)
{
	GMenu *gmenu;
	GMenu *alerts_submenu;
	GMenu *settings_submenu;
	GtkWidget *popover;
	GtkWidget *parent_widget;
	GSimpleActionGroup *action_group;
	GSimpleAction *action;
	char buf[256];

	tab_menu_sess = sess;
	tab_menu_ch = ch;

	/* Use provided parent widget, or find one */
	parent_widget = parent;
	if (!parent_widget)
		parent_widget = chan_get_impl_widget (ch);
	if (!parent_widget)
	{
		/* Fallback to chanview box if no tab widget */
		if (mg_gui && mg_gui->chanview)
			parent_widget = chanview_get_box (mg_gui->chanview);
	}
	if (!parent_widget)
		return;

	/* Create action group */
	action_group = g_simple_action_group_new ();

	/* Basic actions */
	action = g_simple_action_new ("detach", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (tab_action_detach), NULL);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new ("close", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (tab_action_close), NULL);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
	g_object_unref (action);

	gmenu = g_menu_new ();

	/* Channel name header */
	if (sess)
	{
		char *name = g_markup_escape_text (sess->channel[0] ? sess->channel : _("<none>"), -1);
		g_snprintf (buf, sizeof (buf), "%s", name);
		g_free (name);
		g_menu_append (gmenu, buf, NULL);  /* Header item (no action) */

		/* Get default values based on session type */
		int hex_balloon, hex_beep, hex_tray, hex_flash;
		switch (sess->type) {
			case SESS_DIALOG:
				hex_balloon = prefs.hex_input_balloon_priv;
				hex_beep = prefs.hex_input_beep_priv;
				hex_tray = prefs.hex_input_tray_priv;
				hex_flash = prefs.hex_input_flash_priv;
				break;
			default:
				hex_balloon = prefs.hex_input_balloon_chans;
				hex_beep = prefs.hex_input_beep_chans;
				hex_tray = prefs.hex_input_tray_chans;
				hex_flash = prefs.hex_input_flash_chans;
		}

		/* Extra Alerts submenu */
		alerts_submenu = g_menu_new ();

		action = g_simple_action_new_stateful ("alert_balloon", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->alert_balloon, hex_balloon)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_alert_balloon), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (alerts_submenu, _("Show Notifications"), "tab.alert_balloon");

		action = g_simple_action_new_stateful ("alert_beep", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->alert_beep, hex_beep)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_alert_beep), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (alerts_submenu, _("Beep on _Message"), "tab.alert_beep");

		action = g_simple_action_new_stateful ("alert_tray", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->alert_tray, hex_tray)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_alert_tray), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (alerts_submenu, _("Blink Tray _Icon"), "tab.alert_tray");

		action = g_simple_action_new_stateful ("alert_taskbar", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->alert_taskbar, hex_flash)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_alert_taskbar), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (alerts_submenu, _("Blink Task _Bar"), "tab.alert_taskbar");

		g_menu_append_submenu (gmenu, _("_Extra Alerts"), G_MENU_MODEL (alerts_submenu));
		g_object_unref (alerts_submenu);

		/* Per-channel Settings submenu */
		settings_submenu = g_menu_new ();

		action = g_simple_action_new_stateful ("logging", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->text_logging, prefs.hex_irc_logging)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_logging), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (settings_submenu, _("_Log to Disk"), "tab.logging");

		action = g_simple_action_new_stateful ("scrollback", NULL,
			g_variant_new_boolean (tab_get_setting_state (sess->text_scrollback, prefs.hex_text_replay)));
		g_signal_connect (action, "activate", G_CALLBACK (tab_action_scrollback), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);
		g_menu_append (settings_submenu, _("_Reload Scrollback"), "tab.scrollback");

		if (sess->type == SESS_CHANNEL)
		{
			action = g_simple_action_new_stateful ("strip_colors", NULL,
				g_variant_new_boolean (tab_get_setting_state (sess->text_strip, prefs.hex_text_stripcolor_msg)));
			g_signal_connect (action, "activate", G_CALLBACK (tab_action_strip_colors), NULL);
			g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
			g_object_unref (action);
			g_menu_append (settings_submenu, _("Strip _Colors"), "tab.strip_colors");

			action = g_simple_action_new_stateful ("hide_joinpart", NULL,
				g_variant_new_boolean (tab_get_setting_state (sess->text_hidejoinpart, prefs.hex_irc_conf_mode)));
			g_signal_connect (action, "activate", G_CALLBACK (tab_action_hide_joinpart), NULL);
			g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
			g_object_unref (action);
			g_menu_append (settings_submenu, _("_Hide Join/Part Messages"), "tab.hide_joinpart");
		}

		g_menu_append_submenu (gmenu, _("_Settings"), G_MENU_MODEL (settings_submenu));
		g_object_unref (settings_submenu);

		/* Autojoin for channels */
		if (sess->type == SESS_CHANNEL && sess->server && sess->server->network)
		{
			gboolean is_autojoin = joinlist_is_in_list (sess->server, sess->channel);
			action = g_simple_action_new_stateful ("autojoin", NULL,
				g_variant_new_boolean (is_autojoin));
			g_signal_connect (action, "activate", G_CALLBACK (tab_action_autojoin), NULL);
			g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
			g_object_unref (action);
			g_menu_append (gmenu, _("_Autojoin"), "tab.autojoin");
		}
		/* Auto-connect for server tabs */
		else if (sess->type == SESS_SERVER && sess->server && sess->server->network)
		{
			gboolean is_autoconnect = (((ircnet*)sess->server->network)->flags & FLAG_AUTO_CONNECT) != 0;
			action = g_simple_action_new_stateful ("autoconnect", NULL,
				g_variant_new_boolean (is_autoconnect));
			g_signal_connect (action, "activate", G_CALLBACK (tab_action_autoconnect), NULL);
			g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
			g_object_unref (action);
			g_menu_append (gmenu, _("_Auto-Connect"), "tab.autoconnect");
		}
	}

	/* Main actions */
	g_menu_append (gmenu, _("_Detach"), "tab.detach");
	g_menu_append (gmenu, _("_Close"), "tab.close");

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "tab", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, parent_widget);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
								 &(GdkRectangle){ (int)x, (int)y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Clean up action group when popover is closed */
	g_signal_connect (popover, "closed", G_CALLBACK (tab_menu_popover_closed_cb), action_group);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}
#endif

#if HC_GTK4
static gboolean
mg_tab_contextmenu_cb (chanview *cv, chan *ch, int tag, gpointer ud, GtkWidget *parent, double x, double y)
{
	(void)cv;
	if (tag == TAG_IRC)
		mg_create_tabmenu (ud, ch, parent, x, y);
	else
		mg_create_tabmenu (NULL, ch, parent, x, y);
	return TRUE;
}
#else
static gboolean
mg_tab_contextmenu_cb (chanview *cv, chan *ch, int tag, gpointer ud, GdkEventButton *event)
{
	/* middle-click to close a tab */
	if (((prefs.hex_gui_tab_middleclose && event->button == 2))
		&& event->type == GDK_BUTTON_PRESS)
	{
		mg_xbutton_cb (cv, ch, tag, ud);
		return TRUE;
	}

	if (event->button != 3)
		return FALSE;

	if (tag == TAG_IRC)
		mg_create_tabmenu (ud, event, ch);
	else
		mg_create_tabmenu (NULL, event, ch);

	return TRUE;
}
#endif

void
mg_dnd_drop_file (session *sess, char *target, char *uri)
{
	char *p, *data, *next, *fname;

	p = data = g_strdup (uri);
	while (*p)
	{
		next = strchr (p, '\r');
		if (g_ascii_strncasecmp ("file:", p, 5) == 0)
		{
			if (next)
				*next = 0;
			fname = g_filename_from_uri (p, NULL, NULL);
			if (fname)
			{
				/* dcc_send() expects utf-8 */
				p = g_filename_from_utf8 (fname, -1, 0, 0, 0);
				if (p)
				{
					dcc_send (sess, target, p, prefs.hex_dcc_max_send_cps, 0);
					g_free (p);
				}
				g_free (fname);
			}
		}
		if (!next)
			break;
		p = next + 1;
		if (*p == '\n')
			p++;
	}
	g_free (data);

}

#if !HC_GTK4
static void
mg_dialog_dnd_drop (GtkWidget * widget, GdkDragContext * context, gint x,
						  gint y, GtkSelectionData * selection_data, guint info,
						  guint32 time, gpointer ud)
{
	if (current_sess->type == SESS_DIALOG)
		/* sess->channel is really the nickname of dialogs */
		mg_dnd_drop_file (current_sess, current_sess->channel, (char *)gtk_selection_data_get_data (selection_data));
}
#endif

/* add a tabbed channel */

static void
mg_add_chan (session *sess)
{
	GdkPixbuf *icon;
	char *name = _("<none>");

	if (sess->channel[0])
		name = sess->channel;

	switch (sess->type)
	{
	case SESS_CHANNEL:
		icon = pix_tree_channel;
		break;
	case SESS_SERVER:
		icon = pix_tree_server;
		break;
	default:
		icon = pix_tree_dialog;
	}

	sess->res->tab = chanview_add (sess->gui->chanview, name, sess->server, sess,
											 sess->type == SESS_SERVER ? FALSE : TRUE,
											 TAG_IRC, icon);
	if (plain_list == NULL)
		mg_create_tab_colors ();

	chan_set_color (sess->res->tab, plain_list);

	if (sess->res->buffer == NULL)
	{
		sess->res->buffer = gtk_xtext_buffer_new (GTK_XTEXT (sess->gui->xtext));
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		sess->res->user_model = userlist_create_model (sess);
	}
}

static void
mg_userlist_button (GtkWidget * box, char *label, char *cmd,
						  int a, int b, int c, int d)
{
	GtkWidget *wid = gtk_button_new_with_label (label);
	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (userlist_button_cb), cmd);
	gtk_widget_set_hexpand (wid, TRUE);
	gtk_widget_set_vexpand (wid, TRUE);
	gtk_grid_attach (GTK_GRID (box), wid, a, c, b - a, d - c);
	show_and_unfocus (wid);
}

static GtkWidget *
mg_create_userlistbuttons (GtkWidget *box)
{
	struct popup *pop;
	GSList *list = button_list;
	int a = 0, b = 0;
	GtkWidget *tab;

	tab = gtk_grid_new ();
	hc_box_pack_end (box, tab, FALSE, FALSE, 0);

	while (list)
	{
		pop = list->data;
		if (pop->cmd[0])
		{
			mg_userlist_button (tab, pop->name, pop->cmd, a, a + 1, b, b + 1);
			a++;
			if (a == 2)
			{
				a = 0;
				b++;
			}
		}
		list = list->next;
	}

	return tab;
}

static void
mg_topic_cb (GtkWidget *entry, gpointer userdata)
{
	session *sess = current_sess;
	char *text;

	if (sess->channel[0] && sess->server->connected && sess->type == SESS_CHANNEL)
	{
		text = (char *)hc_entry_get_text (entry);
		if (text[0] == 0)
			text = NULL;
		sess->server->p_topic (sess->server, sess->channel, text);
	} else
		hc_entry_set_text (entry, "");
	/* restore focus to the input widget, where the next input will most
likely be */
	gtk_widget_grab_focus (sess->gui->input_box);
}

static void
mg_tabwindow_kill_cb (GtkWidget *win, gpointer userdata)
{
	GSList *list, *next;
	session *sess;

	hexchat_is_quitting = TRUE;

	/* see if there's any non-tab windows left */
	list = sess_list;
	while (list)
	{
		sess = list->data;
		next = list->next;
		if (!sess->gui->is_tab)
		{
			hexchat_is_quitting = FALSE;
/*			puts("-> will not exit, some toplevel windows left");*/
		} else
		{
			mg_ircdestroy (sess);
		}
		list = next;
	}

	current_tab = NULL;
	active_tab = NULL;
	mg_gui = NULL;
	parent_window = NULL;
}

static GtkWidget *
mg_changui_destroy (session *sess)
{
	GtkWidget *ret = NULL;

	if (sess->gui->is_tab)
	{
		/* avoid calling the "destroy" callback */
		g_signal_handlers_disconnect_by_func (G_OBJECT (sess->gui->window),
														  mg_tabwindow_kill_cb, 0);
		/* remove the tab from the chanview */
		if (!mg_chan_remove (sess->res->tab))
			/* if the window still exists, restore the signal handler */
			g_signal_connect (G_OBJECT (sess->gui->window), "destroy",
									G_CALLBACK (mg_tabwindow_kill_cb), 0);
	} else
	{
		/* avoid calling the "destroy" callback */
		g_signal_handlers_disconnect_by_func (G_OBJECT (sess->gui->window),
														  mg_topdestroy_cb, sess);
		/*gtk_widget_destroy (sess->gui->window);*/
		/* don't destroy until the new one is created. Not sure why, but */
		/* it fixes: Gdk-CRITICAL **: gdk_colormap_get_screen: */
		/*           assertion `GDK_IS_COLORMAP (cmap)' failed */
		ret = sess->gui->window;
		g_free (sess->gui);
		sess->gui = NULL;
	}
	return ret;
}

static void
mg_link_irctab (session *sess, int focus)
{
	GtkWidget *win;

	if (sess->gui->is_tab)
	{
		win = mg_changui_destroy (sess);
		mg_changui_new (sess, sess->res, 0, focus);
		mg_populate (sess);
		hexchat_is_quitting = FALSE;
		if (win)
			hc_window_destroy (win);
		return;
	}

	mg_unpopulate (sess);
	win = mg_changui_destroy (sess);
	mg_changui_new (sess, sess->res, 1, focus);
	/* the buffer is now attached to a different widget */
	((xtext_buffer *)sess->res->buffer)->xtext = (GtkXText *)sess->gui->xtext;
	if (win)
		hc_window_destroy (win);
}

void
mg_detach (session *sess, int mode)
{
	switch (mode)
	{
	/* detach only */
	case 1:
		if (sess->gui->is_tab)
			mg_link_irctab (sess, 1);
		break;
	/* attach only */
	case 2:
		if (!sess->gui->is_tab)
			mg_link_irctab (sess, 1);
		break;
	/* toggle */
	default:
		mg_link_irctab (sess, 1);
	}
}

static int
check_is_number (char *t)
{
	while (*t)
	{
		if (*t < '0' || *t > '9')
			return FALSE;
		t++;
	}
	return TRUE;
}

static void
mg_change_flag (GtkWidget * wid, session *sess, char flag)
{
	server *serv = sess->server;
	char mode[3];

	mode[1] = flag;
	mode[2] = '\0';
	if (serv->connected && sess->channel[0])
	{
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wid)))
			mode[0] = '+';
		else
			mode[0] = '-';
		serv->p_mode (serv, sess->channel, mode);
		serv->p_join_info (serv, sess->channel);
		sess->ignore_mode = TRUE;
		sess->ignore_date = TRUE;
	}
}

static void
flagl_hit (GtkWidget * wid, struct session *sess)
{
	char modes[512];
	const char *limit_str;
	server *serv = sess->server;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wid)))
	{
		if (serv->connected && sess->channel[0])
		{
			limit_str = hc_entry_get_text (sess->gui->limit_entry);
			if (check_is_number ((char *)limit_str) == FALSE)
			{
				fe_message (_("User limit must be a number!\n"), FE_MSG_ERROR);
				hc_entry_set_text (sess->gui->limit_entry, "");
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wid), FALSE);
				return;
			}
			g_snprintf (modes, sizeof (modes), "+l %d", atoi (limit_str));
			serv->p_mode (serv, sess->channel, modes);
			serv->p_join_info (serv, sess->channel);
		}
	} else
		mg_change_flag (wid, sess, 'l');
}

static void
flagk_hit (GtkWidget * wid, struct session *sess)
{
	char modes[512];
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		g_snprintf (modes, sizeof (modes), "-k %s", 
			  hc_entry_get_text (sess->gui->key_entry));

		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wid)))
			modes[0] = '+';

		serv->p_mode (serv, sess->channel, modes);
	}
}

static void
mg_flagbutton_cb (GtkWidget *but, char *flag)
{
	session *sess;
	char mode;

	if (ignore_chanmode)
		return;

	sess = current_sess;
	mode = tolower ((unsigned char) flag[0]);

	switch (mode)
	{
	case 'l':
		flagl_hit (but, sess);
		break;
	case 'k':
		flagk_hit (but, sess);
		break;
	case 'b':
		ignore_chanmode = TRUE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sess->gui->flag_b), FALSE);
		ignore_chanmode = FALSE;
		banlist_opengui (sess);
		break;
	default:
		mg_change_flag (but, sess, mode);
	}
}

static GtkWidget *
mg_create_flagbutton (char *tip, GtkWidget *box, char *face)
{
	GtkWidget *btn, *lbl;
	char label_markup[16];

	g_snprintf (label_markup, sizeof(label_markup), "<tt>%s</tt>", face);
	lbl = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL(lbl), label_markup);

	btn = gtk_toggle_button_new ();
#if !HC_GTK4
	/* GTK3: Allow shrinking below natural height */
	gtk_widget_set_size_request (btn, -1, 0);
#endif
	gtk_widget_set_tooltip_text (btn, tip);
	hc_button_set_child (btn, lbl);

	hc_box_pack_start (box, btn, 0, 0, 0);
	g_signal_connect (G_OBJECT (btn), "toggled",
							G_CALLBACK (mg_flagbutton_cb), face);
	show_and_unfocus (btn);

	return btn;
}

static void
mg_key_entry_cb (GtkWidget * igad, gpointer userdata)
{
	char modes[512];
	session *sess = current_sess;
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		g_snprintf (modes, sizeof (modes), "+k %s",
				hc_entry_get_text (igad));
		serv->p_mode (serv, sess->channel, modes);
		serv->p_join_info (serv, sess->channel);
	}
}

static void
mg_limit_entry_cb (GtkWidget * igad, gpointer userdata)
{
	char modes[512];
	session *sess = current_sess;
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		if (check_is_number ((char *)hc_entry_get_text (igad)) == FALSE)
		{
			hc_entry_set_text (igad, "");
			fe_message (_("User limit must be a number!\n"), FE_MSG_ERROR);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sess->gui->flag_l), FALSE);
			return;
		}
		g_snprintf (modes, sizeof(modes), "+l %d",
				atoi (hc_entry_get_text (igad)));
		serv->p_mode (serv, sess->channel, modes);
		serv->p_join_info (serv, sess->channel);
	}
}

static void
mg_apply_entry_style (GtkWidget *entry)
{
	gtk_widget_override_background_color (entry, GTK_STATE_FLAG_NORMAL, &colors[COL_BG]);
	gtk_widget_override_color (entry, GTK_STATE_FLAG_NORMAL, &colors[COL_FG]);
	gtk_widget_override_font (entry, input_style->font_desc);
}

static void
mg_create_chanmodebuttons (session_gui *gui, GtkWidget *box)
{
	gui->flag_c = mg_create_flagbutton (_("Filter Colors"), box, "c");
	gui->flag_n = mg_create_flagbutton (_("No outside messages"), box, "n");
	gui->flag_t = mg_create_flagbutton (_("Topic Protection"), box, "t");
	gui->flag_i = mg_create_flagbutton (_("Invite Only"), box, "i");
	gui->flag_m = mg_create_flagbutton (_("Moderated"), box, "m");
	gui->flag_b = mg_create_flagbutton (_("Ban List"), box, "b");

	gui->flag_k = mg_create_flagbutton (_("Keyword"), box, "k");
	gui->key_entry = gtk_entry_new ();
	gtk_widget_set_name (gui->key_entry, "hexchat-inputbox");
	gtk_entry_set_max_length (GTK_ENTRY (gui->key_entry), 23);
	gtk_widget_set_size_request (gui->key_entry, 115, -1);
	hc_box_pack_start (box, gui->key_entry, 0, 0, 0);
	g_signal_connect (G_OBJECT (gui->key_entry), "activate",
							G_CALLBACK (mg_key_entry_cb), NULL);
	gtk_widget_show (gui->key_entry);

	if (prefs.hex_gui_input_style)
		mg_apply_entry_style (gui->key_entry);

	gui->flag_l = mg_create_flagbutton (_("User Limit"), box, "l");
	gui->limit_entry = gtk_entry_new ();
	gtk_widget_set_name (gui->limit_entry, "hexchat-inputbox");
	gtk_entry_set_max_length (GTK_ENTRY (gui->limit_entry), 10);
	gtk_widget_set_size_request (gui->limit_entry, 30, -1);
	hc_box_pack_start (box, gui->limit_entry, 0, 0, 0);
	g_signal_connect (G_OBJECT (gui->limit_entry), "activate",
							G_CALLBACK (mg_limit_entry_cb), NULL);
	gtk_widget_show (gui->limit_entry);

	if (prefs.hex_gui_input_style)
		mg_apply_entry_style (gui->limit_entry);
}

/*static void
mg_create_link_buttons (GtkWidget *box, gpointer userdata)
{
	gtkutil_button (box, "window-close", _("Close this tab/window"),
						 mg_x_click_cb, userdata, 0);

	if (!userdata)
	gtkutil_button (box, "edit-redo", _("Attach/Detach this tab"),
						 mg_link_cb, userdata, 0);
}*/

static void
mg_dialog_button_cb (GtkWidget *wid, char *cmd)
{
	/* the longest cmd is 12, and the longest nickname is 64 */
	char buf[128];
	char *host = "";
	char *topic;

	if (!current_sess)
		return;

	topic = (char *)(hc_entry_get_text (current_sess->gui->topic_entry));
	topic = strrchr (topic, '@');
	if (topic)
		host = topic + 1;

	auto_insert (buf, sizeof (buf), cmd, 0, 0, "", "", "",
					 server_get_network (current_sess->server, TRUE), host, "",
					 current_sess->channel, "");

	handle_command (current_sess, buf, TRUE);

	/* dirty trick to avoid auto-selection */
	SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
	gtk_widget_grab_focus (current_sess->gui->input_box);
	SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
}

static void
mg_dialog_button (GtkWidget *box, char *name, char *cmd)
{
	GtkWidget *wid;

	wid = gtk_button_new_with_label (name);
	hc_box_pack_start (box, wid, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (mg_dialog_button_cb), cmd);
#if !HC_GTK4
	/* GTK3: Allow shrinking below natural height */
	gtk_widget_set_size_request (wid, -1, 0);
#endif
}

static void
mg_create_dialogbuttons (GtkWidget *box)
{
	struct popup *pop;
	GSList *list = dlgbutton_list;

	while (list)
	{
		pop = list->data;
		if (pop->cmd[0])
			mg_dialog_button (box, pop->name, pop->cmd);
		list = list->next;
	}
}

static void
mg_create_topicbar (session *sess, GtkWidget *box)
{
	GtkWidget *hbox, *topic, *bbox;
	session_gui *gui = sess->gui;

	gui->topic_bar = hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (box, hbox, 0, 0, 0);

	if (!gui->is_tab)
		sess->res->tab = NULL;

	gui->topic_entry = topic = sexy_spell_entry_new ();
	gtk_widget_set_name (topic, "hexchat-inputbox");
	sexy_spell_entry_set_checked (SEXY_SPELL_ENTRY (topic), FALSE);
	gtk_widget_set_hexpand (topic, TRUE);
	hc_box_add (hbox, topic);
	g_signal_connect (G_OBJECT (topic), "activate",
							G_CALLBACK (mg_topic_cb), 0);

	if (prefs.hex_gui_input_style)
		mg_apply_entry_style (topic);

	gui->topicbutton_box = bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (hbox, bbox, 0, 0, 0);
	mg_create_chanmodebuttons (gui, bbox);

	gui->dialogbutton_box = bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (hbox, bbox, 0, 0, 0);
	mg_create_dialogbuttons (bbox);
}

/* check if a word is clickable */

static int
mg_word_check (GtkWidget * xtext, char *word)
{
	session *sess = current_sess;
	int ret;

	ret = url_check_word (word);
	if (ret == 0 && sess->type == SESS_DIALOG)
		return WORD_DIALOG;

	return ret;
}

/* mouse click inside text area */

#if HC_GTK4
static void
mg_word_clicked (GtkWidget *xtext_widget, char *word, gpointer event_unused)
{
	session *sess = current_sess;
	int word_type = 0, start, end;
	char *tmp;
	GtkXText *xtext = GTK_XTEXT (xtext_widget);

	/* GTK4: Get click info from xtext structure (stored before signal emission) */
	guint button = xtext->last_click_button;
	GdkModifierType state = xtext->last_click_state;
	int n_press = xtext->last_click_n_press;
	double x = xtext->last_click_x;
	double y = xtext->last_click_y;

	(void)event_unused;  /* Unused in GTK4 */

	if (word)
	{
		word_type = mg_word_check (xtext_widget, word);
		url_last (&start, &end);
	}

	if (button == 1)			/* left button */
	{
		if (word == NULL)
		{
			mg_focus (sess);
			return;
		}

		if ((state & 13) == (GdkModifierType)prefs.hex_gui_url_mod)
		{
			switch (word_type)
			{
			case WORD_URL:
			case WORD_HOST6:
			case WORD_HOST:
				word[end] = 0;
				fe_open_url (word + start);
			}
		}
		return;
	}

	if (button == 2)
	{
		if (sess->type == SESS_DIALOG)
			menu_middlemenu (sess, xtext_widget, x, y);
		else if (n_press == 2)
			userlist_select (sess, word);
		return;
	}
	if (word == NULL)
		return;

	switch (word_type)
	{
	case 0:
	case WORD_PATH:
		menu_middlemenu (sess, xtext_widget, x, y);
		break;
	case WORD_URL:
	case WORD_HOST6:
	case WORD_HOST:
		word[end] = 0;
		word += start;
		menu_urlmenu (xtext_widget, x, y, word);
		break;
	case WORD_NICK:
		word[end] = 0;
		word += start;
		menu_nickmenu (sess, xtext_widget, x, y, word, FALSE);
		break;
	case WORD_CHANNEL:
		word[end] = 0;
		word += start;
		menu_chanmenu (sess, xtext_widget, x, y, word);
		break;
	case WORD_EMAIL:
		word[end] = 0;
		word += start;
		tmp = g_strdup_printf ("mailto:%s", word + (ispunct (*word) ? 1 : 0));
		menu_urlmenu (xtext_widget, x, y, tmp);
		g_free (tmp);
		break;
	case WORD_DIALOG:
		menu_nickmenu (sess, xtext_widget, x, y, sess->channel, FALSE);
		break;
	}
}
#else
static void
mg_word_clicked (GtkWidget *xtext, char *word, GdkEventButton *even)
{
	session *sess = current_sess;
	int word_type = 0, start, end;
	char *tmp;
	guint button = even->button;
	guint state = even->state;

	if (word)
	{
		word_type = mg_word_check (xtext, word);
		url_last (&start, &end);
	}

	if (button == 1)			/* left button */
	{
		if (word == NULL)
		{
			mg_focus (sess);
			return;
		}

		if ((state & 13) == prefs.hex_gui_url_mod)
		{
			switch (word_type)
			{
			case WORD_URL:
			case WORD_HOST6:
			case WORD_HOST:
				word[end] = 0;
				fe_open_url (word + start);
			}
		}
		return;
	}

	if (button == 2)
	{
		if (sess->type == SESS_DIALOG)
			menu_middlemenu (sess, even);
		else if (even->type == GDK_2BUTTON_PRESS)
			userlist_select (sess, word);
		return;
	}
	if (word == NULL)
		return;

	switch (word_type)
	{
	case 0:
	case WORD_PATH:
		menu_middlemenu (sess, even);
		break;
	case WORD_URL:
	case WORD_HOST6:
	case WORD_HOST:
		word[end] = 0;
		word += start;
		menu_urlmenu (even, word);
		break;
	case WORD_NICK:
		word[end] = 0;
		word += start;
		menu_nickmenu (sess, even, word, FALSE);
		break;
	case WORD_CHANNEL:
		word[end] = 0;
		word += start;
		menu_chanmenu (sess, even, word);
		break;
	case WORD_EMAIL:
		word[end] = 0;
		word += start;
		tmp = g_strdup_printf ("mailto:%s", word + (ispunct (*word) ? 1 : 0));
		menu_urlmenu (even, tmp);
		g_free (tmp);
		break;
	case WORD_DIALOG:
		menu_nickmenu (sess, even, sess->channel, FALSE);
		break;
	}
}
#endif

void
mg_update_xtext (GtkWidget *wid)
{
	GtkXText *xtext = GTK_XTEXT (wid);

	gtk_xtext_set_palette (xtext, colors);
	gtk_xtext_set_max_lines (xtext, prefs.hex_text_max_lines);
	gtk_xtext_set_background (xtext, channelwin_pix);
	gtk_xtext_set_wordwrap (xtext, prefs.hex_text_wordwrap);
	gtk_xtext_set_show_marker (xtext, prefs.hex_text_show_marker);
	gtk_xtext_set_show_separator (xtext, prefs.hex_text_indent ? prefs.hex_text_show_sep : 0);
	gtk_xtext_set_indent (xtext, prefs.hex_text_indent);
	if (!gtk_xtext_set_font (xtext, prefs.hex_text_font))
	{
		fe_message ("Failed to open any font. I'm out of here!", FE_MSG_WAIT | FE_MSG_ERROR);
		exit (1);
	}

	gtk_xtext_refresh (xtext);
}

static void
mg_create_textarea (session *sess, GtkWidget *box)
{
	GtkWidget *inbox, *vbox, *frame;
	GtkXText *xtext;
	session_gui *gui = sess->gui;
#if !HC_GTK4
	static const GtkTargetEntry dnd_targets[] =
	{
		{"text/uri-list", 0, 1}
	};
	static const GtkTargetEntry dnd_dest_targets[] =
	{
		{"HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 },
		{"HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 }
	};
#endif

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	/* Set small minimum sizes to allow shrinking in paned */
	gtk_widget_set_size_request (vbox, 1, -1);
	hc_box_pack_start (box, vbox, TRUE, TRUE, 0);
	inbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_size_request (inbox, 1, -1);
	hc_box_pack_start (vbox, inbox, TRUE, TRUE, 0);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	/* Set small minimum size to allow shrinking in paned */
	gtk_widget_set_size_request (frame, 1, -1);
	/* GTK3: Ensure frame fills and is anchored at origin */
	gtk_widget_set_halign (frame, GTK_ALIGN_FILL);
	gtk_widget_set_valign (frame, GTK_ALIGN_FILL);
	hc_box_pack_start (inbox, frame, TRUE, TRUE, 0);

	gui->xtext = gtk_xtext_new (colors, TRUE);
	/* GTK3: Ensure xtext fills its container and is anchored at origin */
	gtk_widget_set_halign (gui->xtext, GTK_ALIGN_FILL);
	gtk_widget_set_valign (gui->xtext, GTK_ALIGN_FILL);
	xtext = GTK_XTEXT (gui->xtext);
	gtk_xtext_set_max_indent (xtext, prefs.hex_text_max_indent);
	gtk_xtext_set_thin_separator (xtext, prefs.hex_text_thin_sep);
	gtk_xtext_set_urlcheck_function (xtext, mg_word_check);
	gtk_xtext_set_max_lines (xtext, prefs.hex_text_max_lines);
	hc_frame_set_child (frame, GTK_WIDGET (xtext));

	mg_update_xtext (GTK_WIDGET (xtext));

	g_signal_connect (G_OBJECT (xtext), "word_click",
							G_CALLBACK (mg_word_clicked), NULL);

	gui->vscrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, GTK_XTEXT (xtext)->adj);
	hc_box_pack_start (inbox, gui->vscrollbar, FALSE, TRUE, 0);

#if !HC_GTK4
	/* GTK3: DND for scrollbar (accepts chanview/userlist drops for layout swapping) */
	gtk_drag_dest_set (gui->vscrollbar, 5, dnd_dest_targets, 2,
							 GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);
	g_signal_connect (G_OBJECT (gui->vscrollbar), "drag_begin",
							G_CALLBACK (mg_drag_begin_cb), NULL);
	g_signal_connect (G_OBJECT (gui->vscrollbar), "drag_drop",
							G_CALLBACK (mg_drag_drop_cb), NULL);
	g_signal_connect (G_OBJECT (gui->vscrollbar), "drag_motion",
							G_CALLBACK (mg_drag_motion_cb), gui->vscrollbar);
	g_signal_connect (G_OBJECT (gui->vscrollbar), "drag_end",
							G_CALLBACK (mg_drag_end_cb), NULL);

	/* GTK3: DND for xtext (accepts file drops for DCC) */
	gtk_drag_dest_set (gui->xtext, GTK_DEST_DEFAULT_ALL, dnd_targets, 1,
							 GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);
	g_signal_connect (G_OBJECT (gui->xtext), "drag_data_received",
							G_CALLBACK (mg_dialog_dnd_drop), NULL);
#else
	/* GTK4: DND for scrollbar (layout swapping) and xtext (file drops for DCC) */
	mg_setup_scrollbar_dnd (gui->vscrollbar);
	mg_setup_xtext_dnd (gui->xtext);
#endif
}

static GtkWidget *
mg_create_infoframe (GtkWidget *box)
{
	GtkWidget *frame, *label, *hbox;

	frame = gtk_frame_new (0);
	gtk_frame_set_shadow_type ((GtkFrame*)frame, GTK_SHADOW_OUT);
	hc_box_add (box, frame);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_frame_set_child (frame, hbox);

	label = gtk_label_new (NULL);
	hc_box_add (hbox, label);

	return label;
}

static void
mg_create_meters (session_gui *gui, GtkWidget *parent_box)
{
	GtkWidget *infbox, *wid, *box;

	gui->meter_box = infbox = box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	hc_box_pack_start (parent_box, box, 0, 0, 0);

	if ((prefs.hex_gui_lagometer & 2) || (prefs.hex_gui_throttlemeter & 2))
	{
		infbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		hc_box_pack_start (box, infbox, 0, 0, 0);
	}

	if (prefs.hex_gui_lagometer & 1)
	{
		gui->lagometer = wid = gtk_progress_bar_new ();
#ifdef WIN32
		gtk_widget_set_size_request (wid, 1, 10);
#else
		gtk_widget_set_size_request (wid, 1, 8);
#endif

		wid = hc_event_box_new ();
		hc_event_box_set_child (wid, gui->lagometer);
		hc_box_pack_start (box, wid, 0, 0, 0);
	}
	if (prefs.hex_gui_lagometer & 2)
	{
		gui->laginfo = wid = mg_create_infoframe (infbox);
		gtk_label_set_text ((GtkLabel *) wid, "Lag");
	}

	if (prefs.hex_gui_throttlemeter & 1)
	{
		gui->throttlemeter = wid = gtk_progress_bar_new ();
#ifdef WIN32
		gtk_widget_set_size_request (wid, 1, 10);
#else
		gtk_widget_set_size_request (wid, 1, 8);
#endif

		wid = hc_event_box_new ();
		hc_event_box_set_child (wid, gui->throttlemeter);
		hc_box_pack_start (box, wid, 0, 0, 0);
	}
	if (prefs.hex_gui_throttlemeter & 2)
	{
		gui->throttleinfo = wid = mg_create_infoframe (infbox);
		gtk_label_set_text ((GtkLabel *) wid, "Throttle");
	}
}

void
mg_update_meters (session_gui *gui)
{
	hc_widget_destroy (gui->meter_box);
	gui->lagometer = NULL;
	gui->laginfo = NULL;
	gui->throttlemeter = NULL;
	gui->throttleinfo = NULL;

	mg_create_meters (gui, gui->button_box_parent);
	hc_widget_show_all (gui->meter_box);
}

static void
mg_create_userlist (session_gui *gui, GtkWidget *box)
{
	GtkWidget *frame, *ulist, *vbox;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	gtk_widget_set_vexpand (vbox, TRUE);
	hc_box_add (box, vbox);

	frame = gtk_frame_new (NULL);
	if (prefs.hex_gui_ulist_count)
		hc_box_pack_start (vbox, frame, 0, 0, GUI_SPACING);

	gui->namelistinfo = gtk_label_new (NULL);
	hc_frame_set_child (frame, gui->namelistinfo);

	gui->user_tree = ulist = userlist_create (vbox);

	if (prefs.hex_gui_ulist_style)
	{
		gtk_widget_override_font (ulist, input_style->font_desc);
		gtk_widget_override_background_color (ulist, GTK_STATE_FLAG_NORMAL, &colors[COL_BG]);
	}

	mg_create_meters (gui, vbox);

	gui->button_box_parent = vbox;
	gui->button_box = mg_create_userlistbuttons (vbox);
}

static void
mg_vpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	prefs.hex_gui_pane_divider_position = gtk_paned_get_position (pane);
}

static void
mg_leftpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	prefs.hex_gui_pane_left_size = gtk_paned_get_position (pane);
}

static void
mg_rightpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	int handle_size;
	GtkAllocation allocation;

	gtk_widget_style_get (GTK_WIDGET (pane), "handle-size", &handle_size, NULL);
	/* record the position from the RIGHT side */
	gtk_widget_get_allocation (GTK_WIDGET(pane), &allocation);
	prefs.hex_gui_pane_right_size = allocation.width - gtk_paned_get_position (pane) - handle_size;
}

static gboolean
mg_add_pane_signals (session_gui *gui)
{
	g_signal_connect (G_OBJECT (gui->hpane_right), "notify::position",
							G_CALLBACK (mg_rightpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->hpane_left), "notify::position",
							G_CALLBACK (mg_leftpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->vpane_left), "notify::position",
							G_CALLBACK (mg_vpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->vpane_right), "notify::position",
							G_CALLBACK (mg_vpane_cb), gui);
	return FALSE;
}

static void
mg_create_center (session *sess, session_gui *gui, GtkWidget *box)
{
	GtkWidget *vbox, *hbox, *book;

	/* sep between top and bottom of left side */
	gui->vpane_left = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
#if HC_GTK4
	/* GTK4: Hide empty paned widgets initially to prevent crashes during
	 * size computation. They will be shown by mg_hide_empty_pane when
	 * children are added by mg_place_userlist_and_chanview. */
	gtk_widget_set_visible (gui->vpane_left, FALSE);
#endif

	/* sep between top and bottom of right side */
	gui->vpane_right = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
#if HC_GTK4
	gtk_widget_set_visible (gui->vpane_right, FALSE);
#endif

	/* sep between left and xtext */
	gui->hpane_left = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position (GTK_PANED (gui->hpane_left), prefs.hex_gui_pane_left_size);

	/* sep between xtext and right side */
	gui->hpane_right = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);

	if (prefs.hex_gui_win_swap)
	{
		gtk_paned_pack2 (GTK_PANED (gui->hpane_left), gui->vpane_left, FALSE, TRUE);
		gtk_paned_pack1 (GTK_PANED (gui->hpane_left), gui->hpane_right, TRUE, TRUE);
	}
	else
	{
		gtk_paned_pack1 (GTK_PANED (gui->hpane_left), gui->vpane_left, FALSE, TRUE);
		gtk_paned_pack2 (GTK_PANED (gui->hpane_left), gui->hpane_right, TRUE, TRUE);
	}
	gtk_paned_pack2 (GTK_PANED (gui->hpane_right), gui->vpane_right, FALSE, FALSE);

	/* GTK3: Ensure main paned fills its container for proper anchoring */
	gtk_widget_set_halign (gui->hpane_left, GTK_ALIGN_FILL);
	gtk_widget_set_valign (gui->hpane_left, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (gui->hpane_left, TRUE);
	gtk_widget_set_vexpand (gui->hpane_left, TRUE);
	hc_box_add (box, gui->hpane_left);

	gui->note_book = book = hc_page_container_new ();
	/* Set a small minimum size to allow shrinking below child minimum sizes */
	gtk_widget_set_size_request (book, 1, -1);
#if HC_GTK4
	/* GTK4: Ensure page container fills its container and content is anchored at top */
	gtk_widget_set_halign (book, GTK_ALIGN_FILL);
	gtk_widget_set_valign (book, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (book, TRUE);
	gtk_widget_set_vexpand (book, TRUE);
#endif
	gtk_paned_pack1 (GTK_PANED (gui->hpane_right), book, TRUE, TRUE);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#if !HC_GTK4
	/* In GTK3, pack the userlist here. In GTK4, we defer to mg_place_userlist_and_chanview
	 * to avoid issues with reparenting widgets in paned containers. GTK4's paned widget
	 * has stricter requirements about child management. */
	gtk_paned_pack1 (GTK_PANED (gui->vpane_right), hbox, FALSE, FALSE);
#endif
	mg_create_userlist (gui, hbox);

	gui->user_box = hbox;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
#if HC_GTK4
	/* GTK4: Ensure vbox fills page container and is anchored at top-left */
	gtk_widget_set_halign (vbox, GTK_ALIGN_FILL);
	gtk_widget_set_valign (vbox, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (vbox, TRUE);
	gtk_widget_set_vexpand (vbox, TRUE);
#endif
	hc_page_container_append (book, vbox);
	mg_create_topicbar (sess, vbox);

	if (prefs.hex_gui_search_pos)
	{
		mg_create_search (sess, vbox);
		mg_create_textarea (sess, vbox);
	}
	else
	{
		mg_create_textarea (sess, vbox);
		mg_create_search (sess, vbox);
	}

	mg_create_entry (sess, vbox);

	mg_add_pane_signals (gui);
}

static void
mg_change_nick (int cancel, char *text, gpointer userdata)
{
	char buf[256];

	if (!cancel)
	{
		g_snprintf (buf, sizeof (buf), "nick %s", text);
		handle_command (current_sess, buf, FALSE);
	}
}

static void
mg_nickclick_cb (GtkWidget *button, gpointer userdata)
{
	fe_get_str (_("Enter new nickname:"), current_sess->server->nick,
					mg_change_nick, (void *) 1);
}

/* make sure chanview and userlist positions are sane */

static void
mg_sanitize_positions (int *cv, int *ul)
{
	if (prefs.hex_gui_tab_layout == 2)
	{
		/* treeview can't be on TOP or BOTTOM */
		if (*cv == POS_TOP || *cv == POS_BOTTOM)
			*cv = POS_TOPLEFT;
	}

	/* userlist can't be on TOP or BOTTOM */
	if (*ul == POS_TOP || *ul == POS_BOTTOM)
		*ul = POS_TOPRIGHT;

	/* can't have both in the same place */
	if (*cv == *ul)
	{
		*cv = POS_TOPRIGHT;
		if (*ul == POS_TOPRIGHT)
			*cv = POS_BOTTOMRIGHT;
	}
}

static void
mg_place_userlist_and_chanview_real (session_gui *gui, GtkWidget *userlist, GtkWidget *chanview)
{
	int unref_userlist = FALSE;
	int unref_chanview = FALSE;

	/* first, remove userlist/treeview from their containers */
	if (userlist && gtk_widget_get_parent (userlist))
	{
		g_object_ref (userlist);
		gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (userlist)), userlist);
		unref_userlist = TRUE;
	}

	if (chanview && gtk_widget_get_parent (chanview))
	{
		g_object_ref (chanview);
		gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (chanview)), chanview);
		unref_chanview = TRUE;
	}

	if (chanview)
	{
		/* incase the previous pos was POS_HIDDEN */
		gtk_widget_show (chanview);

		/* reset margins that may have been set by previous position */
		gtk_widget_set_margin_top (chanview, 0);
		gtk_widget_set_margin_bottom (chanview, 0);

		/* then place them back in their new positions */
		switch (prefs.hex_gui_tab_pos)
		{
		case POS_TOPLEFT:
			gtk_paned_pack1 (GTK_PANED (gui->vpane_left), chanview, FALSE, FALSE);
			break;
		case POS_BOTTOMLEFT:
			gtk_paned_pack2 (GTK_PANED (gui->vpane_left), chanview, FALSE, FALSE);
			break;
		case POS_TOPRIGHT:
			gtk_paned_pack1 (GTK_PANED (gui->vpane_right), chanview, FALSE, FALSE);
			break;
		case POS_BOTTOMRIGHT:
			gtk_paned_pack2 (GTK_PANED (gui->vpane_right), chanview, FALSE, FALSE);
			break;
		case POS_TOP:
			gtk_widget_set_margin_bottom (chanview, GUI_SPACING-1);
			gtk_widget_set_hexpand (chanview, TRUE);
			gtk_widget_set_vexpand (chanview, FALSE);
			gtk_grid_attach (GTK_GRID (gui->main_table), chanview, 1, 1, 1, 1);
			break;
		case POS_HIDDEN:
			gtk_widget_hide (chanview);
			gtk_widget_set_hexpand (chanview, TRUE);
			gtk_widget_set_vexpand (chanview, FALSE);
			/* always attach it to something to avoid ref_count=0 */
			if (prefs.hex_gui_ulist_pos == POS_TOP)
				gtk_grid_attach (GTK_GRID (gui->main_table), chanview, 1, 3, 1, 1);
			else
				gtk_grid_attach (GTK_GRID (gui->main_table), chanview, 1, 1, 1, 1);
			break;
		default:/* POS_BOTTOM */
			gtk_widget_set_margin_top (chanview, 3);
			gtk_widget_set_hexpand (chanview, TRUE);
			gtk_widget_set_vexpand (chanview, FALSE);
			gtk_grid_attach (GTK_GRID (gui->main_table), chanview, 1, 3, 1, 1);
		}
	}

	if (userlist)
	{
		switch (prefs.hex_gui_ulist_pos)
		{
		case POS_TOPLEFT:
			gtk_paned_pack1 (GTK_PANED (gui->vpane_left), userlist, FALSE, FALSE);
			break;
		case POS_BOTTOMLEFT:
			gtk_paned_pack2 (GTK_PANED (gui->vpane_left), userlist, FALSE, FALSE);
			break;
		case POS_BOTTOMRIGHT:
			gtk_paned_pack2 (GTK_PANED (gui->vpane_right), userlist, FALSE, FALSE);
			break;
		/*case POS_HIDDEN:
			break;*/	/* Hide using the VIEW menu instead */
		default:/* POS_TOPRIGHT */
			gtk_paned_pack1 (GTK_PANED (gui->vpane_right), userlist, FALSE, FALSE);
		}
	}

	if (mg_is_userlist_and_tree_combined () && prefs.hex_gui_pane_divider_position != 0)
	{
		gtk_paned_set_position (GTK_PANED (gui->vpane_left), prefs.hex_gui_pane_divider_position);
		gtk_paned_set_position (GTK_PANED (gui->vpane_right), prefs.hex_gui_pane_divider_position);
	}

	if (unref_chanview)
		g_object_unref (chanview);
	if (unref_userlist)
		g_object_unref (userlist);

	mg_hide_empty_boxes (gui);
}

static void
mg_place_userlist_and_chanview (session_gui *gui)
{
	GtkOrientation orientation;
	GtkWidget *chanviewbox = NULL;
	int pos;

	mg_sanitize_positions (&prefs.hex_gui_tab_pos, &prefs.hex_gui_ulist_pos);

	if (gui->chanview)
	{
		pos = prefs.hex_gui_tab_pos;

		orientation = chanview_get_orientation (gui->chanview);
		if ((pos == POS_BOTTOM || pos == POS_TOP) && orientation == GTK_ORIENTATION_VERTICAL)
			chanview_set_orientation (gui->chanview, FALSE);
		else if ((pos == POS_TOPLEFT || pos == POS_BOTTOMLEFT || pos == POS_TOPRIGHT || pos == POS_BOTTOMRIGHT) && orientation == GTK_ORIENTATION_HORIZONTAL)
			chanview_set_orientation (gui->chanview, TRUE);
		chanviewbox = chanview_get_box (gui->chanview);
	}

	mg_place_userlist_and_chanview_real (gui, gui->user_box, chanviewbox);
}

void
mg_change_layout (int type)
{
	if (mg_gui)
	{
		/* put tabs at the bottom */
		if (type == 0 && prefs.hex_gui_tab_pos != POS_BOTTOM && prefs.hex_gui_tab_pos != POS_TOP)
			prefs.hex_gui_tab_pos = POS_BOTTOM;

		mg_place_userlist_and_chanview (mg_gui);
		chanview_set_impl (mg_gui->chanview, type);
	}
}

static void
mg_inputbox_rightclick (GtkEntry *entry, GtkWidget *menu)
{
	mg_create_color_menu (menu, NULL);
}

/* Search bar adapted from Conspire's by William Pitcock */

#define SEARCH_CHANGE		1
#define SEARCH_NEXT			2
#define SEARCH_PREVIOUS		3
#define SEARCH_REFRESH		4

static void
search_handle_event(int search_type, session *sess)
{
	textentry *last;
	const gchar *text = NULL;
	gtk_xtext_search_flags flags;
	GError *err = NULL;
	gboolean backwards = FALSE;

	/* When just typing show most recent first */
	if (search_type == SEARCH_PREVIOUS || search_type == SEARCH_CHANGE)
		backwards = TRUE;

	flags = ((prefs.hex_text_search_case_match == 1? case_match: 0) |
				(backwards? backward: 0) |
				(prefs.hex_text_search_highlight_all == 1? highlight: 0) |
				(prefs.hex_text_search_follow == 1? follow: 0) |
				(prefs.hex_text_search_regexp == 1? regexp: 0));

	if (search_type != SEARCH_REFRESH)
		text = hc_entry_get_text (sess->gui->shentry);
	last = gtk_xtext_search (GTK_XTEXT (sess->gui->xtext), text, flags, &err);

	if (err)
	{
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, "dialog-error");
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _(err->message));
		g_error_free (err);
	}
	else if (!last)
	{
		if (text && text[0] == 0) /* empty string, no error */
		{
			gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, NULL);
		}
		else
		{
			/* Either end of search or not found, try again to wrap if only end */
			last = gtk_xtext_search (GTK_XTEXT (sess->gui->xtext), text, flags, &err);
			if (!last) /* Not found error */
			{
				gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, "dialog-error");
				gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _("No results found."));
			}
		}
	}
	else
	{
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, NULL);
	}
}

static void
search_handle_change(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_CHANGE, sess);
}

static void
search_handle_refresh(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_REFRESH, sess);
}

void
mg_search_handle_previous(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_PREVIOUS, sess);
}

void
mg_search_handle_next(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_NEXT, sess);
}

static void
search_set_option (GtkToggleButton *but, guint *pref)
{
	*pref = gtk_toggle_button_get_active(but);
	save_config();
}

void
mg_search_toggle(session *sess)
{
	if (gtk_widget_get_visible(sess->gui->shbox))
	{
		gtk_widget_hide(sess->gui->shbox);
		gtk_widget_grab_focus(sess->gui->input_box);
		hc_entry_set_text(sess->gui->shentry, "");
	}
	else
	{
		/* Reset search state */
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, NULL);

		/* Show and focus */
		gtk_widget_show(sess->gui->shbox);
		gtk_widget_grab_focus(sess->gui->shentry);
	}
}

#if HC_GTK4
static gboolean
search_handle_esc (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, session *sess)
{
	(void)controller; (void)keycode; (void)state;
	if (keyval == GDK_KEY_Escape)
		mg_search_toggle(sess);
	return FALSE;
}
#else
static gboolean
search_handle_esc (GtkWidget *win, GdkEventKey *key, session *sess)
{
	if (key->keyval == GDK_KEY_Escape)
		mg_search_toggle(sess);

	return FALSE;
}
#endif

static void
mg_create_search(session *sess, GtkWidget *box)
{
	GtkWidget *entry, *label, *next, *previous, *highlight, *matchcase, *regex, *close;
	session_gui *gui = sess->gui;

	gui->shbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	hc_box_pack_start(box, gui->shbox, FALSE, FALSE, 0);

	close = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (close), hc_image_new_from_icon_name ("window-close", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (close, FALSE);
	hc_box_pack_start(gui->shbox, close, FALSE, FALSE, 0);
	g_signal_connect_swapped(G_OBJECT(close), "clicked", G_CALLBACK(mg_search_toggle), sess);

	label = gtk_label_new(_("Find:"));
	hc_box_pack_start(gui->shbox, label, FALSE, FALSE, 0);

	gui->shentry = entry = gtk_entry_new();
	hc_box_pack_start(gui->shbox, entry, FALSE, FALSE, 0);
	gtk_widget_set_size_request (gui->shentry, 180, -1);
	gui->search_changed_signal = g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(search_handle_change), sess);
#if HC_GTK4
	{
		GtkEventController *key_controller = gtk_event_controller_key_new ();
		g_signal_connect (key_controller, "key-pressed", G_CALLBACK (search_handle_esc), sess);
		gtk_widget_add_controller (entry, key_controller);
	}
#else
	g_signal_connect (G_OBJECT (entry), "key_press_event", G_CALLBACK (search_handle_esc), sess);
#endif
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(mg_search_handle_next), sess);
	gtk_entry_set_icon_activatable (GTK_ENTRY (entry), GTK_ENTRY_ICON_SECONDARY, FALSE);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _("Search hit end or not found."));

	previous = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (previous), hc_image_new_from_icon_name ("go-previous", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(GTK_BUTTON(previous), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (previous, FALSE);
	hc_box_pack_start(gui->shbox, previous, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(previous), "clicked", G_CALLBACK(mg_search_handle_previous), sess);

	next = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (next), hc_image_new_from_icon_name ("go-next", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(GTK_BUTTON(next), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (next, FALSE);
	hc_box_pack_start(gui->shbox, next, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(next), "clicked", G_CALLBACK(mg_search_handle_next), sess);

	highlight = gtk_check_button_new_with_mnemonic (_("_Highlight all"));
	hc_check_button_set_active (highlight, prefs.hex_text_search_highlight_all);
	gtk_widget_set_can_focus (highlight, FALSE);
	g_signal_connect (G_OBJECT (highlight), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_highlight_all);
	g_signal_connect (G_OBJECT (highlight), "toggled", G_CALLBACK (search_handle_refresh), sess);
	hc_box_pack_start(gui->shbox, highlight, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (highlight, _("Highlight all occurrences, and underline the current occurrence."));

	matchcase = gtk_check_button_new_with_mnemonic (_("Mat_ch case"));
	hc_check_button_set_active (matchcase, prefs.hex_text_search_case_match);
	gtk_widget_set_can_focus (matchcase, FALSE);
	g_signal_connect (G_OBJECT (matchcase), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_case_match);
	hc_box_pack_start(gui->shbox, matchcase, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (matchcase, _("Perform a case-sensitive search."));

	regex = gtk_check_button_new_with_mnemonic (_("_Regex"));
	hc_check_button_set_active (regex, prefs.hex_text_search_regexp);
	gtk_widget_set_can_focus (regex, FALSE);
	g_signal_connect (G_OBJECT (regex), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_regexp);
	hc_box_pack_start(gui->shbox, regex, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (regex, _("Regard search string as a regular expression."));
}

static void
mg_create_entry (session *sess, GtkWidget *box)
{
	GtkWidget *hbox, *but, *entry;
	session_gui *gui = sess->gui;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (box, hbox, 0, 0, 0);

	gui->nick_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (hbox, gui->nick_box, 0, 0, 0);

	gui->nick_label = but = gtk_button_new_with_label (sess->server->nick);
	gtk_button_set_relief (GTK_BUTTON (but), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus (but, FALSE);
	hc_box_pack_end (gui->nick_box, but, 0, 0, 0);
	g_signal_connect (G_OBJECT (but), "clicked",
							G_CALLBACK (mg_nickclick_cb), NULL);

	gui->input_box = entry = sexy_spell_entry_new ();
	sexy_spell_entry_set_checked ((SexySpellEntry *)entry, prefs.hex_gui_input_spell);
	sexy_spell_entry_set_parse_attributes ((SexySpellEntry *)entry, prefs.hex_gui_input_attr);

	gtk_entry_set_max_length (GTK_ENTRY (gui->input_box), 0);
	g_signal_connect (G_OBJECT (entry), "activate",
							G_CALLBACK (mg_inputbox_cb), gui);
	gtk_widget_set_hexpand (entry, TRUE);
	hc_box_add (hbox, entry);

	gtk_widget_set_name (entry, "hexchat-inputbox");
#if HC_GTK4
	{
		GtkEventController *key_controller = gtk_event_controller_key_new ();
		/* Use capture phase to handle keys before focus navigation */
		gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
		g_signal_connect (key_controller, "key-pressed",
								G_CALLBACK (key_handle_key_press), NULL);
		gtk_widget_add_controller (entry, key_controller);
	}
	{
		GtkEventController *focus_controller = gtk_event_controller_focus_new ();
		g_signal_connect (focus_controller, "enter",
								G_CALLBACK (mg_inputbox_focus), gui);
		gtk_widget_add_controller (entry, focus_controller);
	}
	/* GTK4: populate_popup doesn't exist, context menu handled differently */
#else
	g_signal_connect (G_OBJECT (entry), "key_press_event",
							G_CALLBACK (key_handle_key_press), NULL);
	g_signal_connect (G_OBJECT (entry), "focus_in_event",
							G_CALLBACK (mg_inputbox_focus), gui);
	g_signal_connect (G_OBJECT (entry), "populate_popup",
							G_CALLBACK (mg_inputbox_rightclick), NULL);
#endif
	g_signal_connect (G_OBJECT (entry), "word-check",
							G_CALLBACK (mg_spellcheck_cb), NULL);
	gtk_widget_grab_focus (entry);

	if (prefs.hex_gui_input_style)
		mg_apply_entry_style (entry);
}

static void
mg_switch_tab_cb (chanview *cv, chan *ch, int tag, gpointer ud)
{
	chan *old;
	session *sess = ud;

	old = active_tab;
	active_tab = ch;

	if (tag == TAG_IRC)
	{
		if (active_tab != old)
		{
			if (old && current_tab)
				mg_unpopulate (current_tab);
			mg_populate (sess);
		}
	} else if (old != active_tab)
	{
		/* userdata for non-irc tabs is actually the GtkBox */
		mg_show_generic_tab (ud);
		if (!mg_is_userlist_and_tree_combined ())
			mg_userlist_showhide (current_sess, FALSE);	/* hide */
	}
}

/* compare two tabs (for tab sorting function) */

static int
mg_tabs_compare (session *a, session *b)
{
	/* server tabs always go first */
	if (a->type == SESS_SERVER)
		return -1;

	/* then channels */
	if (a->type == SESS_CHANNEL && b->type != SESS_CHANNEL)
		return -1;
	if (a->type != SESS_CHANNEL && b->type == SESS_CHANNEL)
		return 1;

	return g_ascii_strcasecmp (a->channel, b->channel);
}

static void
mg_create_tabs (session_gui *gui)
{
	gboolean use_icons = FALSE;

	/* if any one of these PNGs exist, the chanview will create
	 * the extra column for icons. */
	if (prefs.hex_gui_tab_icons && (pix_tree_channel || pix_tree_dialog || pix_tree_server || pix_tree_util))
	{
		use_icons = TRUE;
	}

	gui->chanview = chanview_new (prefs.hex_gui_tab_layout, prefs.hex_gui_tab_trunc,
											prefs.hex_gui_tab_sort, use_icons);
	chanview_set_callbacks (gui->chanview, mg_switch_tab_cb, mg_xbutton_cb,
									mg_tab_contextmenu_cb, (void *)mg_tabs_compare);
	mg_place_userlist_and_chanview (gui);
}

#if HC_GTK4
static void
mg_tabwin_focus_cb (GtkEventControllerFocus *controller, gpointer userdata)
{
	GtkWidget *win = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));
	(void)userdata;
	current_sess = current_tab;
	if (current_sess)
	{
		gtk_xtext_check_marker_visibility (GTK_XTEXT (current_sess->gui->xtext));
		plugin_emit_dummy_print (current_sess, "Focus Window");
	}
	unflash_window (win);
}

static void
mg_topwin_focus_cb (GtkEventControllerFocus *controller, session *sess)
{
	GtkWidget *win = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));
	current_sess = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;
	gtk_xtext_check_marker_visibility(GTK_XTEXT (current_sess->gui->xtext));
	unflash_window (win);
	plugin_emit_dummy_print (sess, "Focus Window");
}
#else
static gboolean
mg_tabwin_focus_cb (GtkWindow * win, GdkEventFocus *event, gpointer userdata)
{
	current_sess = current_tab;
	if (current_sess)
	{
		gtk_xtext_check_marker_visibility (GTK_XTEXT (current_sess->gui->xtext));
		plugin_emit_dummy_print (current_sess, "Focus Window");
	}
	unflash_window (GTK_WIDGET (win));
	return FALSE;
}

static gboolean
mg_topwin_focus_cb (GtkWindow * win, GdkEventFocus *event, session *sess)
{
	current_sess = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;
	gtk_xtext_check_marker_visibility(GTK_XTEXT (current_sess->gui->xtext));
	unflash_window (GTK_WIDGET (win));
	plugin_emit_dummy_print (sess, "Focus Window");
	return FALSE;
}
#endif

static void
mg_create_menu (session_gui *gui, GtkWidget *table, int away_state)
{
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (gtk_widget_get_toplevel (table)),
										 accel_group);
	g_object_unref (accel_group);

	gui->menu = menu_create_main (accel_group, TRUE, away_state, !gui->is_tab,
											gui->menu_item);
	gtk_widget_set_hexpand (gui->menu, TRUE);
	gtk_grid_attach (GTK_GRID (table), gui->menu, 0, 0, 3, 1);
}

static void
mg_create_irctab (session *sess, GtkWidget *table)
{
	GtkWidget *vbox;
	session_gui *gui = sess->gui;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand (vbox, TRUE);
	gtk_widget_set_vexpand (vbox, TRUE);
	gtk_grid_attach (GTK_GRID (table), vbox, 1, 2, 1, 1);
	mg_create_center (sess, gui, vbox);
}

static void
mg_create_topwindow (session *sess)
{
	GtkWidget *win;
	GtkWidget *table;

	if (sess->type == SESS_DIALOG)
		win = gtkutil_window_new ("HexChat", NULL,
										  prefs.hex_gui_dialog_width, prefs.hex_gui_dialog_height, 0);
	else
		win = gtkutil_window_new ("HexChat", NULL,
										  prefs.hex_gui_win_width,
										  prefs.hex_gui_win_height, 0);
	sess->gui->window = win;
	hc_container_set_border_width (win, GUI_BORDER);
	gtk_window_set_opacity (GTK_WINDOW (win), (prefs.hex_gui_transparency / 255.));

#if HC_GTK4
	{
		GtkEventController *focus_controller = gtk_event_controller_focus_new ();
		g_signal_connect (focus_controller, "enter",
								G_CALLBACK (mg_topwin_focus_cb), sess);
		gtk_widget_add_controller (win, focus_controller);
	}
	g_signal_connect (G_OBJECT (win), "destroy",
							G_CALLBACK (mg_topdestroy_cb), sess);
	g_signal_connect (G_OBJECT (win), "notify::default-width",
							G_CALLBACK (mg_configure_cb), sess);
	g_signal_connect (G_OBJECT (win), "notify::default-height",
							G_CALLBACK (mg_configure_cb), sess);
#else
	g_signal_connect (G_OBJECT (win), "focus_in_event",
							G_CALLBACK (mg_topwin_focus_cb), sess);
	g_signal_connect (G_OBJECT (win), "destroy",
							G_CALLBACK (mg_topdestroy_cb), sess);
	g_signal_connect (G_OBJECT (win), "configure_event",
							G_CALLBACK (mg_configure_cb), sess);
#endif

	palette_alloc (win);

	table = gtk_grid_new ();
	/* spacing under the menubar */
	gtk_grid_set_row_spacing (GTK_GRID (table), GUI_SPACING);
	/* left and right borders */
	gtk_grid_set_column_spacing (GTK_GRID (table), 1);
	hc_window_set_child (win, table);

	mg_create_irctab (sess, table);
	mg_create_menu (sess->gui, table, sess->server->is_away);

#if HC_GTK4
	/* Set up keyboard shortcuts for menu actions */
	menu_add_shortcuts (win, sess->gui->menu);
#endif

	if (sess->res->buffer == NULL)
	{
		sess->res->buffer = gtk_xtext_buffer_new (GTK_XTEXT (sess->gui->xtext));
		gtk_xtext_buffer_show (GTK_XTEXT (sess->gui->xtext), sess->res->buffer, TRUE);
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		sess->res->user_model = userlist_create_model (sess);
	}

	userlist_show (sess);

	hc_widget_show_all (table);

	if (prefs.hex_gui_hide_menu)
		gtk_widget_hide (sess->gui->menu);

	/* Will be shown when needed */
	gtk_widget_hide (sess->gui->topic_bar);

	if (!prefs.hex_gui_ulist_buttons)
		gtk_widget_hide (sess->gui->button_box);

	if (!prefs.hex_gui_input_nick)
		gtk_widget_hide (sess->gui->nick_box);

	gtk_widget_hide(sess->gui->shbox);

	mg_decide_userlist (sess, FALSE);

	if (sess->type == SESS_DIALOG)
	{
		/* hide the chan-mode buttons */
		gtk_widget_hide (sess->gui->topicbutton_box);
	} else
	{
		gtk_widget_hide (sess->gui->dialogbutton_box);

		if (!prefs.hex_gui_mode_buttons)
			gtk_widget_hide (sess->gui->topicbutton_box);
	}

	mg_place_userlist_and_chanview (sess->gui);

#if HC_GTK4
	gtk_window_present (GTK_WINDOW (win));
#else
	gtk_widget_show (win);
#endif
}

#if HC_GTK4
static gboolean
mg_tabwindow_de_cb (GtkWindow *win)
{
	GSList *list;
	session *sess;

	if (prefs.hex_gui_tray_close && gtkutil_tray_icon_supported (win) && tray_toggle_visibility (FALSE))
		return TRUE;

	/* check for remaining toplevel windows */
	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (!sess->gui->is_tab)
		{
			return FALSE;
		}
		list = list->next;
	}

	mg_open_quit_dialog (TRUE);
	return TRUE;
}
#else
static gboolean
mg_tabwindow_de_cb (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	GSList *list;
	session *sess;
	GtkWindow *win = GTK_WINDOW(gtk_widget_get_toplevel (widget));

	if (prefs.hex_gui_tray_close && gtkutil_tray_icon_supported (win) && tray_toggle_visibility (FALSE))
		return TRUE;

	/* check for remaining toplevel windows */
	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (!sess->gui->is_tab)
			return FALSE;
		list = list->next;
	}

	mg_open_quit_dialog (TRUE);
	return TRUE;
}
#endif

/* GTK3 only: Windows time change detection via native event filter */
#if defined(G_OS_WIN32) && !HC_GTK4
static GdkFilterReturn
mg_time_change (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	MSG *msg = (MSG*)xevent;

	if (msg->message == WM_TIMECHANGE)
	{
		_tzset();
	}

	return GDK_FILTER_CONTINUE;
}
#endif

static void
mg_create_tabwindow (session *sess)
{
	GtkWidget *win;
	GtkWidget *table;
#if defined(G_OS_WIN32) && !HC_GTK4
	GdkWindow *parent_win;
#endif

	win = gtkutil_window_new ("HexChat", NULL, prefs.hex_gui_win_width,
									  prefs.hex_gui_win_height, 0);
	sess->gui->window = win;
	gtk_window_move (GTK_WINDOW (win), prefs.hex_gui_win_left,
						  prefs.hex_gui_win_top);
	if (prefs.hex_gui_win_state)
		gtk_window_maximize (GTK_WINDOW (win));
	if (prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW (win));
	gtk_window_set_opacity (GTK_WINDOW (win), (prefs.hex_gui_transparency / 255.));
	hc_container_set_border_width (win, GUI_BORDER);

#if HC_GTK4
	g_signal_connect (G_OBJECT (win), "close-request",
						   G_CALLBACK (mg_tabwindow_de_cb), 0);
	g_signal_connect (G_OBJECT (win), "destroy",
						   G_CALLBACK (mg_tabwindow_kill_cb), 0);
	{
		GtkEventController *focus_controller = gtk_event_controller_focus_new ();
		g_signal_connect (focus_controller, "enter",
								G_CALLBACK (mg_tabwin_focus_cb), NULL);
		gtk_widget_add_controller (win, focus_controller);
	}
	g_signal_connect (G_OBJECT (win), "notify::default-width",
							G_CALLBACK (mg_configure_cb), NULL);
	g_signal_connect (G_OBJECT (win), "notify::default-height",
							G_CALLBACK (mg_configure_cb), NULL);
	g_signal_connect (G_OBJECT (win), "notify::maximized",
							G_CALLBACK (mg_windowstate_cb), NULL);
	g_signal_connect (G_OBJECT (win), "notify::fullscreened",
							G_CALLBACK (mg_windowstate_cb), NULL);
#else
	g_signal_connect (G_OBJECT (win), "delete_event",
						   G_CALLBACK (mg_tabwindow_de_cb), 0);
	g_signal_connect (G_OBJECT (win), "destroy",
						   G_CALLBACK (mg_tabwindow_kill_cb), 0);
	g_signal_connect (G_OBJECT (win), "focus_in_event",
							G_CALLBACK (mg_tabwin_focus_cb), NULL);
	g_signal_connect (G_OBJECT (win), "configure_event",
							G_CALLBACK (mg_configure_cb), NULL);
	g_signal_connect (G_OBJECT (win), "window_state_event",
							G_CALLBACK (mg_windowstate_cb), NULL);
#endif

	palette_alloc (win);

	sess->gui->main_table = table = gtk_grid_new ();
	/* spacing under the menubar */
	gtk_grid_set_row_spacing (GTK_GRID (table), GUI_SPACING);
	/* left and right borders */
	gtk_grid_set_column_spacing (GTK_GRID (table), 1);
	hc_window_set_child (win, table);

	mg_create_irctab (sess, table);
	mg_create_tabs (sess->gui);
	mg_create_menu (sess->gui, table, sess->server->is_away);

#if HC_GTK4
	/* Set up keyboard shortcuts for menu actions */
	menu_add_shortcuts (win, sess->gui->menu);
#endif

	mg_focus (sess);

	hc_widget_show_all (table);

	if (prefs.hex_gui_hide_menu)
		gtk_widget_hide (sess->gui->menu);

	mg_decide_userlist (sess, FALSE);

	/* Will be shown when needed */
	gtk_widget_hide (sess->gui->topic_bar);

	if (!prefs.hex_gui_mode_buttons)
		gtk_widget_hide (sess->gui->topicbutton_box);

	if (!prefs.hex_gui_ulist_buttons)
		gtk_widget_hide (sess->gui->button_box);

	if (!prefs.hex_gui_input_nick)
		gtk_widget_hide (sess->gui->nick_box);

	gtk_widget_hide (sess->gui->shbox);

	mg_place_userlist_and_chanview (sess->gui);

#if HC_GTK4
	/* GTK4: Ensure topic bar children are visible before presenting window */
	if (prefs.hex_gui_mode_buttons)
	{
		gtk_widget_set_visible (sess->gui->topicbutton_box, TRUE);
		hc_widget_show_all (sess->gui->topicbutton_box);
	}
	gtk_window_present (GTK_WINDOW (win));
#else
	gtk_widget_show (win);
#endif
}

void
mg_apply_setup (void)
{
	GSList *list = sess_list;
	session *sess;
	int done_main = FALSE;

	mg_create_tab_colors ();

	while (list)
	{
		sess = list->data;
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		((xtext_buffer *)sess->res->buffer)->needs_recalc = TRUE;
		if (!sess->gui->is_tab || !done_main)
			mg_place_userlist_and_chanview (sess->gui);
		if (sess->gui->is_tab)
			done_main = TRUE;
		list = list->next;
	}
}

static chan *
mg_add_generic_tab (char *name, char *title, void *family, GtkWidget *box)
{
	chan *ch;

	hc_page_container_append (mg_gui->note_book, box);
	gtk_widget_show (box);

	ch = chanview_add (mg_gui->chanview, name, NULL, box, TRUE, TAG_UTIL, pix_tree_util);
	chan_set_color (ch, plain_list);

	g_object_set_data_full (G_OBJECT (box), "title", g_strdup (title), g_free);
	g_object_set_data (G_OBJECT (box), "ch", ch);

	if (prefs.hex_gui_tab_newtofront)
		chan_focus (ch);

	return ch;
}

void
fe_buttons_update (session *sess)
{
	session_gui *gui = sess->gui;

	hc_widget_destroy (gui->button_box);
	gui->button_box = mg_create_userlistbuttons (gui->button_box_parent);

	if (prefs.hex_gui_ulist_buttons)
		gtk_widget_show (sess->gui->button_box);
	else
		gtk_widget_hide (sess->gui->button_box);
}

void
fe_clear_channel (session *sess)
{
	char tbuf[CHANLEN+6];
	session_gui *gui = sess->gui;

	if (sess->gui->is_tab)
	{
		if (sess->waitchannel[0])
		{
			if (prefs.hex_gui_tab_trunc > 2 && g_utf8_strlen (sess->waitchannel, -1) > prefs.hex_gui_tab_trunc)
			{
				/* truncate long channel names */
				tbuf[0] = '(';
				strcpy (tbuf + 1, sess->waitchannel);
				g_utf8_offset_to_pointer(tbuf, prefs.hex_gui_tab_trunc)[0] = 0;
				strcat (tbuf, "..)");
			} else
			{
				sprintf (tbuf, "(%s)", sess->waitchannel);
			}
		}
		else
			strcpy (tbuf, _("<none>"));
		chan_rename (sess->res->tab, tbuf, prefs.hex_gui_tab_trunc);
	}

	if (!sess->gui->is_tab || sess == current_tab)
	{
		hc_entry_set_text (gui->topic_entry, "");

		if (gui->op_xpm)
		{
			hc_widget_destroy (gui->op_xpm);
			gui->op_xpm = 0;
		}
	} else
	{
		if (sess->res->topic_text)
		{
			g_free (sess->res->topic_text);
			sess->res->topic_text = NULL;
		}
	}
}

void
fe_set_nonchannel (session *sess, int state)
{
}

void
fe_dlgbuttons_update (session *sess)
{
	GtkWidget *box;
	session_gui *gui = sess->gui;

	hc_widget_destroy (gui->dialogbutton_box);

	gui->dialogbutton_box = box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	hc_box_pack_start (gui->topic_bar, box, 0, 0, 0);
	gtk_box_reorder_child (GTK_BOX (gui->topic_bar), box, 3);
	mg_create_dialogbuttons (box);

	hc_widget_show_all (box);

	if (current_tab && current_tab->type != SESS_DIALOG)
		gtk_widget_hide (current_tab->gui->dialogbutton_box);
}

void
fe_update_mode_buttons (session *sess, char mode, char sign)
{
	int state, i;

	if (sign == '+')
		state = TRUE;
	else
		state = FALSE;

	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
	{
		if (chan_flags[i] == mode)
		{
			if (!sess->gui->is_tab || sess == current_tab)
			{
				ignore_chanmode = TRUE;
				if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sess->gui->flag_wid[i])) != state)
					gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sess->gui->flag_wid[i]), state);
				ignore_chanmode = FALSE;
			} else
			{
				sess->res->flag_wid_state[i] = state;
			}
			return;
		}
	}
}

void
fe_set_nick (server *serv, char *newnick)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (current_tab == sess || !sess->gui->is_tab)
				gtk_button_set_label (GTK_BUTTON (sess->gui->nick_label), newnick);
		}
		list = list->next;
	}
}

void
fe_set_away (server *serv)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (!sess->gui->is_tab || sess == current_tab)
			{
				menu_set_away (sess->gui, serv->is_away);
				/* gray out my nickname */
				mg_set_myself_away (sess->gui, serv->is_away);
			}
		}
		list = list->next;
	}
}

void
fe_set_channel (session *sess)
{
	if (sess->res->tab != NULL)
		chan_rename (sess->res->tab, sess->channel, prefs.hex_gui_tab_trunc);
}

void
mg_changui_new (session *sess, restore_gui *res, int tab, int focus)
{
	int first_run = FALSE;
	session_gui *gui;

	if (res == NULL)
	{
		res = g_new0 (restore_gui, 1);
	}

	sess->res = res;

	if (sess->server->front_session == NULL)
	{
		sess->server->front_session = sess;
	}

	if (!tab)
	{
		gui = g_new0 (session_gui, 1);
		gui->is_tab = FALSE;
		sess->gui = gui;
		mg_create_topwindow (sess);
		fe_set_title (sess);
		return;
	}

	if (mg_gui == NULL)
	{
		first_run = TRUE;
		gui = &static_mg_gui;
		memset (gui, 0, sizeof (session_gui));
		gui->is_tab = TRUE;
		sess->gui = gui;
		mg_create_tabwindow (sess);
		mg_gui = gui;
		parent_window = gui->window;
	} else
	{
		sess->gui = gui = mg_gui;
		gui->is_tab = TRUE;
	}

	mg_add_chan (sess);

	if (first_run || (prefs.hex_gui_tab_newtofront == FOCUS_NEW_ONLY_ASKED && focus)
			|| prefs.hex_gui_tab_newtofront == FOCUS_NEW_ALL )
		chan_focus (res->tab);
}

GtkWidget *
mg_create_generic_tab (char *name, char *title, int force_toplevel,
							  int link_buttons,
							  void *close_callback, void *userdata,
							  int width, int height, GtkWidget **vbox_ret,
							  void *family)
{
	GtkWidget *vbox, *win;

	if (prefs.hex_gui_tab_pos == POS_HIDDEN && prefs.hex_gui_tab_utils)
		prefs.hex_gui_tab_utils = 0;

	if (force_toplevel || !prefs.hex_gui_tab_utils)
	{
		win = gtkutil_window_new (title, name, width, height, 2);
		vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		*vbox_ret = vbox;
		hc_window_set_child (win, vbox);
		gtk_widget_show (vbox);
		if (close_callback)
			g_signal_connect (G_OBJECT (win), "destroy",
									G_CALLBACK (close_callback), userdata);
		return win;
	}

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	g_object_set_data (G_OBJECT (vbox), "w", GINT_TO_POINTER (width));
	g_object_set_data (G_OBJECT (vbox), "h", GINT_TO_POINTER (height));
	hc_container_set_border_width (vbox, 3);
	*vbox_ret = vbox;

	if (close_callback)
		g_signal_connect (G_OBJECT (vbox), "destroy",
								G_CALLBACK (close_callback), userdata);

	mg_add_generic_tab (name, title, family, vbox);

/*	if (link_buttons)
	{
		hbox = gtk_hbox_new (FALSE, 0);
		hc_box_pack_start (vbox, hbox, 0, 0, 0);
		mg_create_link_buttons (hbox, ch);
		gtk_widget_show (hbox);
	}*/

	return vbox;
}

void
mg_move_tab (session *sess, int delta)
{
	if (sess->gui->is_tab)
		chan_move (sess->res->tab, delta);
}

void
mg_move_tab_family (session *sess, int delta)
{
	if (sess->gui->is_tab)
		chan_move_family (sess->res->tab, delta);
}

void
mg_set_title (GtkWidget *vbox, char *title) /* for non-irc tab/window only */
{
	char *old;

	old = g_object_get_data (G_OBJECT (vbox), "title");
	if (old)
	{
		g_object_set_data_full (G_OBJECT (vbox), "title", g_strdup (title), g_free);
	} else
	{
		gtk_window_set_title (GTK_WINDOW (vbox), title);
	}
}

void
fe_server_callback (server *serv)
{
	joind_close (serv);

	if (serv->gui->chanlist_window)
		mg_close_gen (NULL, serv->gui->chanlist_window);

	if (serv->gui->rawlog_window)
		mg_close_gen (NULL, serv->gui->rawlog_window);

	g_free (serv->gui);
}

/* called when a session is being killed */

void
fe_session_callback (session *sess)
{
	gtk_xtext_buffer_free (sess->res->buffer);
	g_object_unref (G_OBJECT (sess->res->user_model));

	if (sess->res->banlist && sess->res->banlist->window)
		mg_close_gen (NULL, sess->res->banlist->window);

	g_free (sess->res->input_text);
	g_free (sess->res->topic_text);
	g_free (sess->res->limit_text);
	g_free (sess->res->key_text);
	g_free (sess->res->queue_text);
	g_free (sess->res->queue_tip);
	g_free (sess->res->lag_text);
	g_free (sess->res->lag_tip);

	if (sess->gui->bartag)
		fe_timeout_remove (sess->gui->bartag);

	if (sess->gui != &static_mg_gui)
		g_free (sess->gui);
	g_free (sess->res);
}

/* ===== DRAG AND DROP STUFF ===== */
/*
 * GTK3: Uses gtk_drag_* API with GdkDragContext
 * GTK4: Uses GtkDropTarget and GtkDragSource controllers
 *
 * For now, DND is wrapped for GTK3 only. Layout swapping via DND
 * and file drops for DCC are disabled in GTK4 builds.
 * TODO: Implement GTK4 DND using GtkDropTarget/GtkDragSource
 */

#if !HC_GTK4

static gboolean
is_child_of (GtkWidget *widget, GtkWidget *parent)
{
	while (widget)
	{
		if (gtk_widget_get_parent (widget) == parent)
			return TRUE;
		widget = gtk_widget_get_parent (widget);
	}
	return FALSE;
}

static void
mg_handle_drop (GtkWidget *widget, int y, int *pos, int *other_pos)
{
	int height;
	session_gui *gui = current_sess->gui;

	height = gdk_window_get_height (gtk_widget_get_window (widget));

	if (y < height / 2)
	{
		if (is_child_of (widget, gui->vpane_left))
			*pos = 1;	/* top left */
		else
			*pos = 3;	/* top right */
	}
	else
	{
		if (is_child_of (widget, gui->vpane_left))
			*pos = 2;	/* bottom left */
		else
			*pos = 4;	/* bottom right */
	}

	/* both in the same pos? must move one */
	if (*pos == *other_pos)
	{
		switch (*other_pos)
		{
		case 1:
			*other_pos = 2;
			break;
		case 2:
			*other_pos = 1;
			break;
		case 3:
			*other_pos = 4;
			break;
		case 4:
			*other_pos = 3;
			break;
		}
	}

	mg_place_userlist_and_chanview (gui);
}

static gboolean
mg_is_gui_target (GdkDragContext *context)
{
	char *target_name;

	if (!context || !gdk_drag_context_list_targets (context) || !gdk_drag_context_list_targets (context)->data)
		return FALSE;

	target_name = gdk_atom_name (gdk_drag_context_list_targets (context)->data);
	if (target_name)
	{
		/* if it's not HEXCHAT_CHANVIEW or HEXCHAT_USERLIST */
		/* we should ignore it. */
		if (target_name[0] != 'H')
		{
			g_free (target_name);
			return FALSE;
		}
		g_free (target_name);
	}

	return TRUE;
}

/* this begin callback just creates an nice of the source */

gboolean
mg_drag_begin_cb (GtkWidget *widget, GdkDragContext *context, gpointer userdata)
{
	int width, height;
	GdkPixbuf *pix, *pix2;
	GdkWindow *window;

	/* ignore file drops */
	if (!mg_is_gui_target (context))
		return FALSE;

	window = gtk_widget_get_window (widget);
	width = gdk_window_get_width (window);
	height = gdk_window_get_height (window);

	/* GTK3: Use gdk_pixbuf_get_from_window instead of deprecated gdk_pixbuf_get_from_drawable */
	pix = gdk_pixbuf_get_from_window (window, 0, 0, width, height);
	if (pix == NULL)
		return FALSE;

	pix2 = gdk_pixbuf_scale_simple (pix, width * 4 / 5, height / 2, GDK_INTERP_HYPER);
	g_object_unref (pix);

	gtk_drag_set_icon_pixbuf (context, pix2, 0, 0);
	g_object_set_data (G_OBJECT (widget), "ico", pix2);

	return TRUE;
}

void
mg_drag_end_cb (GtkWidget *widget, GdkDragContext *context, gpointer userdata)
{
	/* ignore file drops */
	if (!mg_is_gui_target (context))
		return;

	g_object_unref (g_object_get_data (G_OBJECT (widget), "ico"));
}

/* drop complete */

gboolean
mg_drag_drop_cb (GtkWidget *widget, GdkDragContext *context, int x, int y, guint time, gpointer user_data)
{
	/* ignore file drops */
	if (!mg_is_gui_target (context))
		return FALSE;

	switch (gdk_drag_context_get_selected_action (context))
	{
	case GDK_ACTION_MOVE:
		/* from userlist */
		mg_handle_drop (widget, y, &prefs.hex_gui_ulist_pos, &prefs.hex_gui_tab_pos);
		break;
	case GDK_ACTION_COPY:
		/* from tree - we use GDK_ACTION_COPY for the tree */
		mg_handle_drop (widget, y, &prefs.hex_gui_tab_pos, &prefs.hex_gui_ulist_pos);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

/* draw highlight rectangle in the destination */

gboolean
mg_drag_motion_cb (GtkWidget *widget, GdkDragContext *context, int x, int y, guint time, gpointer scbar)
{
	int half, width, height;
	int ox, oy;
	GdkWindow *window;
	GtkAllocation allocation;
	cairo_t *cr;

	/* ignore file drops */
	if (!mg_is_gui_target (context))
		return FALSE;

	window = gtk_widget_get_window (widget);
	if (!window)
		return FALSE;

	if (scbar)	/* scrollbar */
	{
		gtk_widget_get_allocation (widget, &allocation);
		ox = allocation.x;
		oy = allocation.y;
		width = allocation.width;
		height = allocation.height;
	}
	else
	{
		ox = oy = 0;
		width = gdk_window_get_width (window);
		height = gdk_window_get_height (window);
	}

	half = height / 2;

	/* GTK3: Use Cairo for drawing instead of deprecated GdkGC */
	cr = gdk_cairo_create (window);

	/* Set random highlight color */
	cairo_set_source_rgb (cr,
		(double)(rand() % 256) / 255.0,
		(double)(rand() % 256) / 255.0,
		(double)(rand() % 256) / 255.0);
	cairo_set_line_width (cr, 2.0);

	if (y < half)
	{
		cairo_rectangle (cr, 1 + ox, 2 + oy, width - 3, half - 4);
		cairo_stroke (cr);
		cairo_rectangle (cr, 0 + ox, 1 + oy, width - 1, half - 2);
		cairo_stroke (cr);
		gtk_widget_queue_draw_area (widget, ox, half + oy, width, height - half);
	}
	else
	{
		cairo_rectangle (cr, 0 + ox, half + 1 + oy, width - 1, half - 2);
		cairo_stroke (cr);
		cairo_rectangle (cr, 1 + ox, half + 2 + oy, width - 3, half - 4);
		cairo_stroke (cr);
		gtk_widget_queue_draw_area (widget, ox, oy, width, half);
	}

	cairo_destroy (cr);

	return TRUE;
}

#else /* HC_GTK4 - GTK4 DND using GtkDropTarget/GtkDragSource */

/*
 * GTK4 DND Implementation
 *
 * Uses GtkDropTarget for receiving drops and GtkDragSource for initiating drags.
 * Two main use cases:
 * 1. File drops on xtext/userlist for DCC file transfers
 * 2. Layout swapping by dragging userlist/chanview to scrollbar positions
 */

/* File drop handler for xtext (DCC send to current channel/dialog) */
static gboolean
mg_xtext_file_drop_cb (GtkDropTarget *target, const GValue *value,
                       double x, double y, gpointer user_data)
{
	GFile *file;
	char *uri;

	(void)target; (void)x; (void)y; (void)user_data;

	if (!G_VALUE_HOLDS (value, G_TYPE_FILE))
		return FALSE;

	file = g_value_get_object (value);
	if (!file)
		return FALSE;

	uri = g_file_get_uri (file);
	if (uri)
	{
		if (current_sess->type == SESS_DIALOG)
		{
			/* sess->channel is really the nickname of dialogs */
			mg_dnd_drop_file (current_sess, current_sess->channel, uri);
		}
		else if (current_sess->type == SESS_CHANNEL)
		{
			/* For channels, need to select a user first - just show a message */
			/* This matches GTK3 behavior - file drops on channel require selecting a user */
		}
		g_free (uri);
	}

	return TRUE;
}

/* Internal layout swapping target types */
#define DND_TARGET_CHANVIEW "HEXCHAT_CHANVIEW"
#define DND_TARGET_USERLIST "HEXCHAT_USERLIST"

/* Helper to determine drop position based on y coordinate */
static void
mg_handle_drop_gtk4 (GtkWidget *widget, double y, int *pos, int *other_pos)
{
	int height;
	session_gui *gui = current_sess->gui;

	height = gtk_widget_get_height (widget);

	if (y < height / 2)
	{
		if (gtk_widget_is_ancestor (widget, gui->vpane_left))
			*pos = 1;	/* top left */
		else
			*pos = 3;	/* top right */
	}
	else
	{
		if (gtk_widget_is_ancestor (widget, gui->vpane_left))
			*pos = 2;	/* bottom left */
		else
			*pos = 4;	/* bottom right */
	}

	/* both in the same pos? must move one */
	if (*pos == *other_pos)
	{
		switch (*other_pos)
		{
		case 1:
			*other_pos = 2;
			break;
		case 2:
			*other_pos = 1;
			break;
		case 3:
			*other_pos = 4;
			break;
		case 4:
			*other_pos = 3;
			break;
		}
	}

	mg_place_userlist_and_chanview (gui);
}

/* Drop handler for scrollbar (receives chanview/userlist layout drops) */
static gboolean
mg_scrollbar_drop_cb (GtkDropTarget *target, const GValue *value,
                      double x, double y, gpointer user_data)
{
	const char *drop_type;

	(void)target; (void)x; (void)user_data;

	if (!G_VALUE_HOLDS (value, G_TYPE_STRING))
		return FALSE;

	drop_type = g_value_get_string (value);
	if (!drop_type)
		return FALSE;

	if (g_strcmp0 (drop_type, DND_TARGET_USERLIST) == 0)
	{
		/* from userlist */
		mg_handle_drop_gtk4 (current_sess->gui->vscrollbar, y,
		                     &prefs.hex_gui_ulist_pos, &prefs.hex_gui_tab_pos);
	}
	else if (g_strcmp0 (drop_type, DND_TARGET_CHANVIEW) == 0)
	{
		/* from chanview/tree */
		mg_handle_drop_gtk4 (current_sess->gui->vscrollbar, y,
		                     &prefs.hex_gui_tab_pos, &prefs.hex_gui_ulist_pos);
	}
	else
	{
		return FALSE;
	}

	return TRUE;
}

/* Prepare callback for userlist drag source */
static GdkContentProvider *
mg_userlist_drag_prepare_cb (GtkDragSource *source, double x, double y, gpointer user_data)
{
	(void)source; (void)x; (void)y; (void)user_data;

	return gdk_content_provider_new_typed (G_TYPE_STRING, DND_TARGET_USERLIST);
}

/* Prepare callback for chanview drag source */
static GdkContentProvider *
mg_chanview_drag_prepare_cb (GtkDragSource *source, double x, double y, gpointer user_data)
{
	(void)source; (void)x; (void)y; (void)user_data;

	return gdk_content_provider_new_typed (G_TYPE_STRING, DND_TARGET_CHANVIEW);
}

/* Set up file drop target for xtext widget */
void
mg_setup_xtext_dnd (GtkWidget *xtext)
{
	GtkDropTarget *target;

	target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (target, "drop", G_CALLBACK (mg_xtext_file_drop_cb), NULL);
	gtk_widget_add_controller (xtext, GTK_EVENT_CONTROLLER (target));
}

/* Set up scrollbar as drop target for layout swapping */
void
mg_setup_scrollbar_dnd (GtkWidget *scrollbar)
{
	GtkDropTarget *target;

	target = gtk_drop_target_new (G_TYPE_STRING, GDK_ACTION_MOVE);
	g_signal_connect (target, "drop", G_CALLBACK (mg_scrollbar_drop_cb), NULL);
	gtk_widget_add_controller (scrollbar, GTK_EVENT_CONTROLLER (target));
}

/* Set up userlist as drag source for layout swapping */
void
mg_setup_userlist_drag_source (GtkWidget *treeview)
{
	GtkDragSource *source;

	source = gtk_drag_source_new ();
	gtk_drag_source_set_actions (source, GDK_ACTION_MOVE);
	g_signal_connect (source, "prepare", G_CALLBACK (mg_userlist_drag_prepare_cb), NULL);
	gtk_widget_add_controller (treeview, GTK_EVENT_CONTROLLER (source));
}

/* Set up chanview as drag source for layout swapping */
void
mg_setup_chanview_drag_source (GtkWidget *widget)
{
	GtkDragSource *source;

	source = gtk_drag_source_new ();
	gtk_drag_source_set_actions (source, GDK_ACTION_MOVE);
	g_signal_connect (source, "prepare", G_CALLBACK (mg_chanview_drag_prepare_cb), NULL);
	gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (source));
}

#endif /* HC_GTK4 - end of GTK4 DND functions */
