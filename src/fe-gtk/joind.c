/* X-Chat
 * Copyright (C) 2005 Peter Zelezny.
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

/* joind.c - The Join Dialog.

   Popups up when you connect without any autojoin channels and helps you
   to find or join a channel.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/fe.h"
#include "fe-gtk.h"
#include "chanlist.h"


static void
joind_radio2_cb (GtkWidget *radio, server *serv)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio)))
	{
		gtk_widget_grab_focus (serv->gui->joind_entry);
		gtk_editable_set_position (GTK_EDITABLE (serv->gui->joind_entry), 999);
	}
}

static void
joind_entryenter_cb (GtkWidget *entry, GtkWidget *ok)
{
	gtk_widget_grab_focus (ok);
}

static void
joind_entryfocus_cb (GtkEventControllerFocus *controller, server *serv)
{
	(void)controller;
	/* GTK4: Radio buttons are mapped to check buttons */
	gtk_check_button_set_active (GTK_CHECK_BUTTON (serv->gui->joind_radio2), TRUE);
}

static void
joind_destroy_cb (GtkWidget *win, server *serv)
{
	if (is_server (serv))
		serv->gui->joind_win = NULL;
}

static void
joind_ok_cb (GtkWidget *ok, server *serv)
{
	if (!is_server (serv))
	{
		hc_window_destroy (gtk_widget_get_toplevel (ok));
		return;
	}

	/* do nothing */
	/* GTK4: Radio buttons are mapped to check buttons */
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (serv->gui->joind_radio1)))
		goto xit;

	/* join specific channel */
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (serv->gui->joind_radio2)))
	{
		char *text = (char *)hc_entry_get_text (serv->gui->joind_entry);
		if (strlen (text) < 1)
		{
			fe_message (_("Channel name too short, try again."), FE_MSG_ERROR);
			return;
		}
		serv->p_join (serv, text, "");
		goto xit;
	}

	/* channel list */
	chanlist_opengui (serv, TRUE);

xit:
	prefs.hex_gui_join_dialog = 0;
	if (hc_check_button_get_active (serv->gui->joind_check))
		prefs.hex_gui_join_dialog = 1;

	hc_window_destroy (serv->gui->joind_win);
	serv->gui->joind_win = NULL;
}

static void
joind_show_dialog (server *serv)
{
	GtkWidget *dialog1;
	GtkWidget *dialog_vbox1;
	GtkWidget *vbox1;
	GtkWidget *hbox1;
	GtkWidget *image1;
	GtkWidget *vbox2;
	GtkWidget *label;
	GtkWidget *radiobutton1;
	GtkWidget *radiobutton2;
	GtkWidget *radiobutton3;
	GSList *radiobutton1_group;
	GtkWidget *hbox2;
	GtkWidget *entry1;
	GtkWidget *checkbutton1;
	GtkWidget *okbutton1;
	char buf[256];
	char buf2[256];

	/* GTK4: GtkDialog is deprecated, use GtkWindow with manual layout */
	serv->gui->joind_win = dialog1 = gtk_window_new ();
	g_snprintf(buf, sizeof(buf), _("Connection Complete - %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (dialog1), buf);
	gtk_window_set_transient_for (GTK_WINDOW(dialog1), GTK_WINDOW(serv->front_session->gui->window));
	gtk_window_set_modal (GTK_WINDOW (dialog1), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (dialog1), FALSE);

	/* Create content area manually */
	dialog_vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child (GTK_WINDOW (dialog1), dialog_vbox1);

	vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show (vbox1);
	hc_box_pack_start (dialog_vbox1, vbox1, TRUE, TRUE, 0);

	hbox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show (hbox1);
	hc_box_pack_start (vbox1, hbox1, TRUE, TRUE, 0);

	image1 = hc_image_new_from_icon_name ("network-workgroup", GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_widget_show (image1);
	hc_box_pack_start (hbox1, image1, FALSE, TRUE, 24);
	gtk_widget_set_halign (image1, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (image1, GTK_ALIGN_START);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	hc_container_set_border_width (vbox2, 6);
	gtk_widget_show (vbox2);
	hc_box_pack_start (hbox1, vbox2, TRUE, TRUE, 0);

	g_snprintf (buf2, sizeof (buf2), _("Connection to %s complete."),
				 server_get_network (serv, TRUE));
	g_snprintf (buf, sizeof (buf), "\n<b>%s</b>", buf2);
	label = gtk_label_new (buf);
	gtk_widget_show (label);
	hc_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	label = gtk_label_new (_("In the server list window, no channel (chat room) has been entered to be automatically joined for this network."));
	gtk_widget_show (label);
	hc_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	label = gtk_label_new (_("What would you like to do next?"));
	gtk_widget_show (label);
	hc_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	serv->gui->joind_radio1 = radiobutton1 = gtk_radio_button_new_with_mnemonic (NULL, _("_Nothing, I'll join a channel later."));
	gtk_widget_show (radiobutton1);
	hc_box_pack_start (vbox2, radiobutton1, FALSE, FALSE, 0);
	radiobutton1_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radiobutton1));

	hbox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_show (hbox2);
	hc_box_pack_start (vbox2, hbox2, FALSE, FALSE, 0);

	serv->gui->joind_radio2 = radiobutton2 = gtk_radio_button_new_with_mnemonic (NULL, _("_Join this channel:"));
	gtk_widget_show (radiobutton2);
	hc_box_pack_start (hbox2, radiobutton2, FALSE, FALSE, 0);
	gtk_radio_button_set_group (GTK_RADIO_BUTTON (radiobutton2), radiobutton1_group);
	radiobutton1_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radiobutton2));

	serv->gui->joind_entry = entry1 = gtk_entry_new ();
	hc_entry_set_text (entry1, "#");
	gtk_widget_show (entry1);
	hc_box_pack_start (hbox2, entry1, TRUE, TRUE, 8);

	g_snprintf (buf, sizeof (buf), "<small>     %s</small>",
				 _("If you know the name of the channel you want to join, enter it here."));
	label = gtk_label_new (buf);
	gtk_widget_show (label);
	hc_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	radiobutton3 = gtk_radio_button_new_with_mnemonic (NULL, _("O_pen the channel list."));
	gtk_widget_show (radiobutton3);
	hc_box_pack_start (vbox2, radiobutton3, FALSE, FALSE, 0);
	gtk_radio_button_set_group (GTK_RADIO_BUTTON (radiobutton3), radiobutton1_group);

	g_snprintf (buf, sizeof (buf), "<small>     %s</small>",
				 _("Retrieving the channel list may take a minute or two."));
	label = gtk_label_new (buf);
	gtk_widget_show (label);
	hc_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	serv->gui->joind_check = checkbutton1 = gtk_check_button_new_with_mnemonic (_("_Always show this dialog after connecting."));
	if (prefs.hex_gui_join_dialog)
		hc_check_button_set_active (checkbutton1, TRUE);
	gtk_widget_show (checkbutton1);
	hc_box_pack_start (vbox1, checkbutton1, FALSE, FALSE, 0);

	/* GTK4: Create OK button manually since we're using GtkWindow */
	{
		GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_halign (button_box, GTK_ALIGN_END);
		gtk_widget_set_margin_top (button_box, 12);
		gtk_widget_set_margin_bottom (button_box, 12);
		gtk_widget_set_margin_end (button_box, 12);
		hc_box_pack_start (dialog_vbox1, button_box, FALSE, FALSE, 0);

		okbutton1 = gtk_button_new_with_mnemonic (_("_OK"));
		gtk_box_append (GTK_BOX (button_box), okbutton1);
	}

	g_signal_connect (G_OBJECT (dialog1), "destroy",
							G_CALLBACK (joind_destroy_cb), serv);
	/* GTK4: Use focus controller instead of focus_in_event signal */
	{
		GtkEventController *focus_controller = gtk_event_controller_focus_new ();
		g_signal_connect (focus_controller, "enter", G_CALLBACK (joind_entryfocus_cb), serv);
		gtk_widget_add_controller (entry1, focus_controller);
	}
	g_signal_connect (G_OBJECT (entry1), "activate",
							G_CALLBACK (joind_entryenter_cb), okbutton1);
	g_signal_connect (G_OBJECT (okbutton1), "clicked",
							G_CALLBACK (joind_ok_cb), serv);
							
	if (serv->network)
		if (g_ascii_strcasecmp(((ircnet*)serv->network)->name, "Libera.Chat") == 0)
		{
			hc_entry_set_text (entry1, "#hexchat");
		}

	gtk_widget_grab_focus (okbutton1);
	/* GTK4: Use gtk_window_present instead of show_all for dialogs */
	gtk_window_present (GTK_WINDOW (dialog1));
}

void
joind_open (server *serv)
{
	if (prefs.hex_gui_join_dialog)
		joind_show_dialog (serv);
}

void
joind_close (server *serv)
{
	if (serv->gui->joind_win)
	{
		hc_window_destroy (serv->gui->joind_win);
		serv->gui->joind_win = NULL;
	}
}
