/* X-Chat
 * Copyright (C) 1998-2007 Peter Zelezny.
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
#include <fcntl.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Debug logging - set to 1 to enable */
#define HC_DEBUG_LOG 0

#include "fe-gtk.h"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/ignore.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/notify.h"
#include "../common/util.h"
#include "../common/text.h"
#include "xtext.h"
#include "ascii.h"
#include "banlist.h"
#include "chanlist.h"
#include "editlist.h"
#include "fkeys.h"
#include "gtkutil.h"
#include "maingui.h"
#include "notifygui.h"
#include "pixmaps.h"
#include "rawlog.h"
#include "palette.h"
#include "plugingui.h"
#include "search.h"
#include "textgui.h"
#include "urlgrab.h"
#include "userlistgui.h"
#include "menu.h"
#include "servlistgui.h"

static GSList *submenu_list;

/* Forward declarations for GTK4 */
static void menu_popover_closed_cb (GtkPopover *popover, gpointer user_data);
static GMenu *menu_build_gmenu (int away, int toplevel);
static GSimpleActionGroup *menu_create_action_group (int away, int toplevel);

enum
{
	M_MENUITEM,
	M_NEWMENU,
	M_END,
	M_SEP,
	M_MENUTOG,
	M_MENURADIO,
	M_MENUSTOCK,
	M_MENUPIX,
	M_MENUSUB
};

struct mymenu
{
	char *text;
	void *callback;
	char *image;
	unsigned char type;	/* M_XXX */
	unsigned char id;		/* MENU_ID_XXX (menu.h) */
	unsigned char state;	/* ticked or not? */
	unsigned char sensitive;	/* shaded out? */
	guint key;				/* GDK_KEY_x */
};

#define XCMENU_DOLIST 1
#define XCMENU_SHADED 1
#define XCMENU_MARKUP 2
#define XCMENU_MNEMONIC 4

/* execute a userlistbutton/popupmenu command */

static void
nick_command (session * sess, char *cmd)
{
	if (*cmd == '!')
		hexchat_exec (cmd + 1);
	else
		handle_command (sess, cmd, TRUE);
}

/* fill in the %a %s %n etc and execute the command */

void
nick_command_parse (session *sess, char *cmd, char *nick, char *allnick)
{
	char *buf;
	char *host = _("Host unknown");
	char *account = _("Account unknown");
	struct User *user;
	int len;

/*	if (sess->type == SESS_DIALOG)
	{
		buf = (char *)(GTK_ENTRY (sess->gui->topic_entry)->text);
		buf = strrchr (buf, '@');
		if (buf)
			host = buf + 1;
	} else*/
	{
		user = userlist_find (sess, nick);
		if (user)
		{
			if (user->hostname)
				host = strchr (user->hostname, '@') + 1;
			if (user->account)
				account = user->account;
		}
	}

	/* this can't overflow, since popup->cmd is only 256 */
	len = strlen (cmd) + strlen (nick) + strlen (allnick) + 512;
	buf = g_malloc (len);

	auto_insert (buf, len, cmd, 0, 0, allnick, sess->channel, "",
					 server_get_network (sess->server, TRUE), host,
					 sess->server->nick, nick, account);

	nick_command (sess, buf);

	g_free (buf);
}

/* userlist button has been clicked */

void
userlist_button_cb (GtkWidget * button, char *cmd)
{
	int i, num_sel, using_allnicks = FALSE;
	char **nicks, *allnicks;
	char *nick = NULL;
	session *sess;

	sess = current_sess;

	if (strstr (cmd, "%a"))
		using_allnicks = TRUE;

	if (sess->type == SESS_DIALOG)
	{
		/* fake a selection */
		nicks = g_new (char *, 2);
		nicks[0] = g_strdup (sess->channel);
		nicks[1] = NULL;
		num_sel = 1;
	}
	else
	{
		/* find number of selected rows */
		nicks = userlist_selection_list (sess->gui->user_tree, &num_sel);
		if (num_sel < 1)
		{
			nick_command_parse (sess, cmd, "", "");

			g_free (nicks);
			return;
		}
	}

	/* create "allnicks" string */
	allnicks = g_malloc (((NICKLEN + 1) * num_sel) + 1);
	*allnicks = 0;

	i = 0;
	while (nicks[i])
	{
		if (i > 0)
			strcat (allnicks, " ");
		strcat (allnicks, nicks[i]);

		if (!nick)
			nick = nicks[0];

		/* if not using "%a", execute the command once for each nickname */
		if (!using_allnicks)
			nick_command_parse (sess, cmd, nicks[i], "");

		i++;
	}

	if (using_allnicks)
	{
		if (!nick)
			nick = "";
		nick_command_parse (sess, cmd, nick, allnicks);
	}

	while (num_sel)
	{
		num_sel--;
		g_free (nicks[num_sel]);
	}

	g_free (nicks);
	g_free (allnicks);
}

/* a popup-menu-item has been selected */

static void
popup_menu_cb (GtkWidget * item, char *cmd)
{
	char *nick;

	/* the userdata is set in menu_quick_item() */
	nick = g_object_get_data (G_OBJECT (item), "u");

	if (!nick)	/* userlist popup menu */
	{
		/* treat it just like a userlist button */
		userlist_button_cb (NULL, cmd);
		return;
	}

	if (!current_sess)	/* for url grabber window */
		nick_command_parse (sess_list->data, cmd, nick, nick);
	else
		nick_command_parse (current_sess, cmd, nick, nick);
}

GtkWidget *
menu_toggle_item (char *label, GtkWidget *menu, void *callback, void *userdata,
						int state)
{
	GtkWidget *item;

	item = gtk_check_menu_item_new_with_mnemonic (label);
	gtk_check_menu_item_set_active ((GtkCheckMenuItem*)item, state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (callback), userdata);
	gtk_widget_show (item);

	return item;
}

GtkWidget *
menu_quick_item (char *cmd, char *label, GtkWidget * menu, int flags,
					  gpointer userdata, char *icon)
{
	GtkWidget *item;

	if (!label)
		item = gtk_menu_item_new ();
	else
	{
		if (icon)
		{
			/* GtkImageMenuItem is deprecated in GTK3 and removed in GTK4.
			 * Menu items with icons are no longer directly supported.
			 * Using regular menu items instead. */
			if (flags & XCMENU_MNEMONIC)
				item = gtk_menu_item_new_with_mnemonic (label);
			else
				item = gtk_menu_item_new_with_label (label);
		}
		else
		{
			if (flags & XCMENU_MARKUP)
			{
				item = gtk_menu_item_new_with_label ("");
				if (flags & XCMENU_MNEMONIC)
					gtk_label_set_markup_with_mnemonic (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), label);
				else
					gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (item))), label);
			} else
			{
				if (flags & XCMENU_MNEMONIC)
					item = gtk_menu_item_new_with_mnemonic (label);
				else
					item = gtk_menu_item_new_with_label (label);
			}
		}
	}
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_object_set_data (G_OBJECT (item), "u", userdata);
	if (cmd)
		g_signal_connect (G_OBJECT (item), "activate",
								G_CALLBACK (popup_menu_cb), cmd);
	if (flags & XCMENU_SHADED)
		gtk_widget_set_sensitive (GTK_WIDGET (item), FALSE);
	hc_widget_show_all (item);

	return item;
}

static void
menu_quick_item_with_callback (void *callback, char *label, GtkWidget * menu,
										 void *arg)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (label);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (callback), arg);
	gtk_widget_show (item);
}

GtkWidget *
menu_quick_sub (char *name, GtkWidget *menu, GtkWidget **sub_item_ret, int flags, int pos)
{
	GtkWidget *sub_menu;
	GtkWidget *sub_item;

	if (!name)
		return menu;

	/* Code to add a submenu */
	sub_menu = gtk_menu_new ();
	if (flags & XCMENU_MARKUP)
	{
		sub_item = gtk_menu_item_new_with_label ("");
		gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child (GTK_BIN (sub_item))), name);
	}
	else
	{
		if (flags & XCMENU_MNEMONIC)
			sub_item = gtk_menu_item_new_with_mnemonic (name);
		else
			sub_item = gtk_menu_item_new_with_label (name);
	}
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), sub_item, pos);
	gtk_widget_show (sub_item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (sub_item), sub_menu);

	if (sub_item_ret)
		*sub_item_ret = sub_item;

	if (flags & XCMENU_DOLIST)
		/* We create a new element in the list */
		submenu_list = g_slist_prepend (submenu_list, sub_menu);
	return sub_menu;
}

static GtkWidget *
menu_quick_endsub (void)
{
	/* Just delete the first element in the linked list pointed to by first */
	if (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);

	if (submenu_list)
		return (submenu_list->data);
	else
		return NULL;
}

static void
toggle_cb (GtkWidget *item, char *pref_name)
{
	char buf[256];

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
		g_snprintf (buf, sizeof (buf), "set %s 1", pref_name);
	else
		g_snprintf (buf, sizeof (buf), "set %s 0", pref_name);

	handle_command (current_sess, buf, FALSE);
}

static int
is_in_path (char *cmd)
{
	char *orig = g_strdup (cmd + 1);	/* 1st char is "!" */
	char *prog = orig;
	char **argv;
	int argc;

	/* special-case these default entries. */
	/*                  123456789012345678 */
	if (strncmp (prog, "gnome-terminal -x ", 18) == 0)
	/* don't check for gnome-terminal, but the thing it's executing! */
		prog += 18;

	if (g_shell_parse_argv (prog, &argc, &argv, NULL))
	{
		char *path = g_find_program_in_path (argv[0]);
		g_strfreev (argv);
		if (path)
		{
			g_free (path);
			g_free (orig);
			return 1;
		}
	}

	g_free (orig);
	return 0;
}

/* syntax: "LABEL~ICON~STUFF~ADDED~LATER~" */

static void
menu_extract_icon (char *name, char **label, char **icon)
{
	char *p = name;
	char *start = NULL;
	char *end = NULL;

	while (*p)
	{
		if (*p == '~')
		{
			/* escape \~ */
			if (p == name || p[-1] != '\\')
			{
				if (!start)
					start = p + 1;
				else if (!end)
					end = p + 1;
			}
		}
		p++;
	}

	if (!end)
		end = p;

	if (start && start != end)
	{
		*label = g_strndup (name, (start - name) - 1);
		*icon = g_strndup (start, (end - start) - 1);
	}
	else
	{
		*label = g_strdup (name);
		*icon = NULL;
	}
}

/* append items to "menu" using the (struct popup*) list provided */

void
menu_create (GtkWidget *menu, GSList *list, char *target, int check_path)
{
	struct popup *pop;
	GtkWidget *tempmenu = menu, *subitem = NULL;
	int childcount = 0;

	submenu_list = g_slist_prepend (0, menu);
	while (list)
	{
		pop = (struct popup *) list->data;

		if (!g_ascii_strncasecmp (pop->name, "SUB", 3))
		{
			childcount = 0;
			tempmenu = menu_quick_sub (pop->cmd, tempmenu, &subitem, XCMENU_DOLIST|XCMENU_MNEMONIC, -1);

		} else if (!g_ascii_strncasecmp (pop->name, "TOGGLE", 6))
		{
			childcount++;
			menu_toggle_item (pop->name + 7, tempmenu, toggle_cb, pop->cmd,
									cfg_get_bool (pop->cmd));

		} else if (!g_ascii_strncasecmp (pop->name, "ENDSUB", 6))
		{
			/* empty sub menu due to no programs in PATH? */
			if (check_path && childcount < 1)
				hc_widget_destroy (subitem);
			subitem = NULL;

			if (tempmenu != menu)
				tempmenu = menu_quick_endsub ();
			/* If we get here and tempmenu equals menu that means we havent got any submenus to exit from */

		} else if (!g_ascii_strncasecmp (pop->name, "SEP", 3))
		{
			menu_quick_item (0, 0, tempmenu, XCMENU_SHADED, 0, 0);

		} else
		{
			char *icon, *label;

			/* default command in hexchat.c */
			if (pop->cmd[0] == 'n' && !strcmp (pop->cmd, "notify -n ASK %s"))
			{
				/* don't create this item if already in notify list */
				if (!target || notify_is_in_list (current_sess->server, target))
				{
					list = list->next;
					continue;
				}
			}

			menu_extract_icon (pop->name, &label, &icon);

			if (!check_path || pop->cmd[0] != '!')
			{
				menu_quick_item (pop->cmd, label, tempmenu, XCMENU_MNEMONIC, target, icon);
			/* check if the program is in path, if not, leave it out! */
			} else if (is_in_path (pop->cmd))
			{
				childcount++;
				menu_quick_item (pop->cmd, label, tempmenu, XCMENU_MNEMONIC, target, icon);
			}

			g_free (label);
			g_free (icon);
		}

		list = list->next;
	}

	/* Let's clean up the linked list from mem */
	while (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);
}

static char *str_copy = NULL;		/* for all pop-up menus */
static GtkWidget *nick_submenu = NULL;	/* user info submenu */

static void
menu_nickinfo_cb (GtkWidget *menu, session *sess)
{
	char buf[512];

	if (!is_session (sess))
		return;

	/* issue a /WHOIS */
	g_snprintf (buf, sizeof (buf), "WHOIS %s %s", str_copy, str_copy);
	handle_command (sess, buf, FALSE);
	/* and hide the output */
	sess->server->skip_next_whois = 1;
}

static void
copy_to_clipboard_cb (GtkWidget *item, char *url)
{
	gtkutil_copy_to_clipboard (item, FALSE, url);
}

/* returns boolean: Some data is missing */

static gboolean
menu_create_nickinfo_menu (struct User *user, GtkWidget *submenu)
{
	char buf[512];
	char unknown[96];
	char *real, *fmt, *users_country;
	struct away_msg *away;
	gboolean missing = FALSE;
	GtkWidget *item;

	/* let the translators tweak this if need be */
	fmt = _("<tt><b>%-11s</b></tt> %s");
	g_snprintf (unknown, sizeof (unknown), "<i>%s</i>", _("Unknown"));

	if (user->realname)
	{
		real = strip_color (user->realname, -1, STRIP_ALL|STRIP_ESCMARKUP);
		g_snprintf (buf, sizeof (buf), fmt, _("Real Name:"), real);
		g_free (real);
	} else
	{
		g_snprintf (buf, sizeof (buf), fmt, _("Real Name:"), unknown);
	}
	item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->realname ? user->realname : unknown);

	g_snprintf (buf, sizeof (buf), fmt, _("User:"),
				 user->hostname ? user->hostname : unknown);
	item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->hostname ? user->hostname : unknown);
	
	g_snprintf (buf, sizeof (buf), fmt, _("Account:"),
				 user->account ? user->account : unknown);
	item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->account ? user->account : unknown);

	users_country = country (user->hostname);
	if (users_country)
	{
		g_snprintf (buf, sizeof (buf), fmt, _ ("Country:"), users_country);
		item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
		g_signal_connect (G_OBJECT (item), "activate",
			G_CALLBACK (copy_to_clipboard_cb), users_country);
	}

	g_snprintf (buf, sizeof (buf), fmt, _("Server:"),
				 user->servername ? user->servername : unknown);
	item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->servername ? user->servername : unknown);

	if (user->lasttalk)
	{
		char min[96];

		g_snprintf (min, sizeof (min), _("%u minutes ago"),
					(unsigned int) ((time (0) - user->lasttalk) / 60));
		g_snprintf (buf, sizeof (buf), fmt, _("Last Msg:"), min);
	} else
	{
		g_snprintf (buf, sizeof (buf), fmt, _("Last Msg:"), unknown);
	}
	menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);

	if (user->away)
	{
		away = server_away_find_message (current_sess->server, user->nick);
		if (away)
		{
			char *msg = strip_color (away->message ? away->message : unknown, -1, STRIP_ALL|STRIP_ESCMARKUP);
			g_snprintf (buf, sizeof (buf), fmt, _("Away Msg:"), msg);
			g_free (msg);
			item = menu_quick_item (0, buf, submenu, XCMENU_MARKUP, 0, 0);
			g_signal_connect (G_OBJECT (item), "activate",
									G_CALLBACK (copy_to_clipboard_cb), 
									away->message ? away->message : unknown);
		}
		else
			missing = TRUE;
	}

	return missing;
}

void
fe_userlist_update (session *sess, struct User *user)
{
	GList *items, *next;

	if (!nick_submenu || !str_copy)
		return;

	/* not the same nick as the menu? */
	if (sess->server->p_cmp (user->nick, str_copy))
		return;

	/* get rid of the "show" signal */
	g_signal_handlers_disconnect_by_func (nick_submenu, menu_nickinfo_cb, sess);

	/* destroy all the old items */
	items = gtk_container_get_children (GTK_CONTAINER (nick_submenu));
	for (GList *l = items; l != NULL; l = l->next)
	{
		hc_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (items);

	/* and re-create them with new info */
	menu_create_nickinfo_menu (user, nick_submenu);
}

/* Action callbacks for nick menu */
static session *nick_menu_sess = NULL;  /* session for nick menu actions */
static char **nick_popup_cmds = NULL;   /* array of popup commands for current menu */
static int nick_popup_cmd_count = 0;    /* count of popup commands */
static char *nick_all_nicks = NULL;     /* all selected nicks (space-separated) */

/* Generic popup command action callback - uses action parameter as command index */
static void
nick_popup_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	int index;
	(void)action; (void)user_data;

	if (!parameter || !nick_menu_sess)
		return;

	index = g_variant_get_int32 (parameter);
	if (index < 0 || index >= nick_popup_cmd_count || !nick_popup_cmds[index])
		return;

	/* Use nick_command_parse to handle %s, %a, etc. placeholders */
	nick_command_parse (nick_menu_sess, nick_popup_cmds[index],
						str_copy ? str_copy : "",
						nick_all_nicks ? nick_all_nicks : (str_copy ? str_copy : ""));
}

static void
nick_popup_cmds_free (void)
{
	int i;
	if (nick_popup_cmds)
	{
		for (i = 0; i < nick_popup_cmd_count; i++)
			g_free (nick_popup_cmds[i]);
		g_free (nick_popup_cmds);
		nick_popup_cmds = NULL;
	}
	nick_popup_cmd_count = 0;
	g_free (nick_all_nicks);
	nick_all_nicks = NULL;
}

/* Static storage for user info to copy (freed on menu close) */
static char *nick_info_realname = NULL;
static char *nick_info_hostname = NULL;
static char *nick_info_account = NULL;
static char *nick_info_servername = NULL;
static char *nick_info_away = NULL;

static void
nick_action_copy_realname (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (nick_info_realname)
		gtkutil_copy_to_clipboard (NULL, FALSE, nick_info_realname);
}

static void
nick_action_copy_hostname (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (nick_info_hostname)
		gtkutil_copy_to_clipboard (NULL, FALSE, nick_info_hostname);
}

static void
nick_action_copy_account (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (nick_info_account)
		gtkutil_copy_to_clipboard (NULL, FALSE, nick_info_account);
}

static void
nick_action_copy_servername (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (nick_info_servername)
		gtkutil_copy_to_clipboard (NULL, FALSE, nick_info_servername);
}

static void
nick_action_copy_away (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (nick_info_away)
		gtkutil_copy_to_clipboard (NULL, FALSE, nick_info_away);
}

static void
nick_menu_free_info (void)
{
	g_free (nick_info_realname);
	g_free (nick_info_hostname);
	g_free (nick_info_account);
	g_free (nick_info_servername);
	g_free (nick_info_away);
	nick_info_realname = NULL;
	nick_info_hostname = NULL;
	nick_info_account = NULL;
	nick_info_servername = NULL;
	nick_info_away = NULL;
}

/* Data for nick menu popover cleanup */
typedef struct {
	GtkWidget *popover;           /* NULL if popover was destroyed */
	GSimpleActionGroup *action_group;
} NickMenuCleanupData;

/* Weak notify - called when popover is finalized */
static void
nick_menu_popover_weak_notify (gpointer data, GObject *where_the_object_was)
{
	NickMenuCleanupData *cleanup = data;
	(void)where_the_object_was;
	cleanup->popover = NULL;  /* Mark as destroyed */
}

/* Idle callback to clean up nick menu popover and action group */
static gboolean
nick_menu_popover_cleanup_idle (gpointer user_data)
{
	NickMenuCleanupData *cleanup = user_data;

	/* Clean up action group */
	if (cleanup->action_group)
		g_object_unref (cleanup->action_group);

	/* Free nick menu resources */
	nick_menu_free_info ();
	nick_popup_cmds_free ();

	/* Remove weak ref if popover still exists.
	 * We do NOT unparent the popover - GTK4 automatically cleans up popovers
	 * when their parent window is destroyed. Trying to unparent after the
	 * parent hierarchy is partially destroyed causes crashes. */
	if (cleanup->popover != NULL)
		g_object_weak_unref (G_OBJECT (cleanup->popover), nick_menu_popover_weak_notify, cleanup);

	g_free (cleanup);
	return G_SOURCE_REMOVE;
}

static void
nick_menu_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	GSimpleActionGroup *action_group = g_object_get_data (G_OBJECT (popover), "action-group");
	NickMenuCleanupData *cleanup = g_new0 (NickMenuCleanupData, 1);

	cleanup->popover = GTK_WIDGET (popover);
	cleanup->action_group = action_group;

	/* Use weak ref to detect if popover is destroyed before our idle runs */
	g_object_weak_ref (G_OBJECT (popover), nick_menu_popover_weak_notify, cleanup);

	/* Defer cleanup to allow action callbacks to complete */
	g_idle_add (nick_menu_popover_cleanup_idle, cleanup);
	(void)user_data;
}

/* Build popup_list menu items into a GMenu, creating actions in the action group.
 * Returns the number of items added (for sizing the command array).
 * This is a two-pass operation: first count items, then build menu.
 */
static int
nick_menu_count_popup_items (GSList *list)
{
	int count = 0;
	struct popup *pop;

	while (list)
	{
		pop = (struct popup *) list->data;
		/* Only count actual menu items (not SUB, ENDSUB, SEP, TOGGLE) */
		if (g_ascii_strncasecmp (pop->name, "SUB", 3) != 0 &&
			g_ascii_strncasecmp (pop->name, "ENDSUB", 6) != 0 &&
			g_ascii_strncasecmp (pop->name, "SEP", 3) != 0 &&
			g_ascii_strncasecmp (pop->name, "TOGGLE", 6) != 0)
		{
			count++;
		}
		list = list->next;
	}
	return count;
}

/* Extract label from popup name (removes ~icon~ part) */
static char *
nick_menu_extract_label (const char *name)
{
	const char *p = name;
	const char *tilde = NULL;

	/* Find first unescaped ~ */
	while (*p)
	{
		if (*p == '~' && (p == name || p[-1] != '\\'))
		{
			tilde = p;
			break;
		}
		p++;
	}

	if (tilde)
		return g_strndup (name, tilde - name);
	else
		return g_strdup (name);
}

/* Build popup menu items into a GMenu
 * target: nick for single selection, NULL for multi-selection
 * cmd_index: pointer to current command index (incremented as commands are added)
 */
static void
nick_menu_build_popup (GMenu *menu, GSList *list, const char *target,
					   GSimpleActionGroup *action_group, int *cmd_index)
{
	struct popup *pop;
	GMenu *current_menu = menu;
	GMenu *current_section = NULL;
	GSList *menu_stack = NULL;
	char action_name[64];
	char detailed_action[128];
	char *label;
	GSimpleAction *action;

	/* Push the root menu onto the stack */
	menu_stack = g_slist_prepend (NULL, menu);

	while (list)
	{
		pop = (struct popup *) list->data;

		if (g_ascii_strncasecmp (pop->name, "SUB", 3) == 0)
		{
			/* Create a new submenu */
			GMenu *submenu = g_menu_new ();
			label = nick_menu_extract_label (pop->cmd);

			/* Add submenu to current menu/section */
			if (current_section)
				g_menu_append_submenu (current_section, label, G_MENU_MODEL (submenu));
			else
				g_menu_append_submenu (current_menu, label, G_MENU_MODEL (submenu));

			g_free (label);

			/* Push current menu onto stack and make submenu current */
			menu_stack = g_slist_prepend (menu_stack, current_menu);
			current_menu = submenu;
			current_section = NULL;
			g_object_unref (submenu);
		}
		else if (g_ascii_strncasecmp (pop->name, "ENDSUB", 6) == 0)
		{
			/* Pop back to parent menu */
			if (menu_stack)
			{
				current_menu = menu_stack->data;
				menu_stack = g_slist_delete_link (menu_stack, menu_stack);
				current_section = NULL;
			}
		}
		else if (g_ascii_strncasecmp (pop->name, "SEP", 3) == 0)
		{
			/* Start a new section */
			current_section = g_menu_new ();
			g_menu_append_section (current_menu, NULL, G_MENU_MODEL (current_section));
			g_object_unref (current_section);
		}
		else if (g_ascii_strncasecmp (pop->name, "TOGGLE", 6) == 0)
		{
			/* TODO: Toggle items need stateful actions - skip for now */
			list = list->next;
			continue;
		}
		else
		{
			/* Regular menu item */
			/* Check notify condition: skip if already in notify list */
			if (pop->cmd[0] == 'n' && strcmp (pop->cmd, "notify -n ASK %s") == 0)
			{
				if (!target || notify_is_in_list (nick_menu_sess->server, target))
				{
					list = list->next;
					continue;
				}
			}

			label = nick_menu_extract_label (pop->name);

			/* Create action for this command */
			g_snprintf (action_name, sizeof action_name, "popup%d", *cmd_index);
			action = g_simple_action_new (action_name, G_VARIANT_TYPE_INT32);
			g_signal_connect (action, "activate", G_CALLBACK (nick_popup_action_cb), NULL);
			g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
			g_object_unref (action);

			/* Store the command */
			nick_popup_cmds[*cmd_index] = g_strdup (pop->cmd);

			/* Add menu item with action and parameter */
			g_snprintf (detailed_action, sizeof detailed_action, "nick.popup%d(%d)",
						*cmd_index, *cmd_index);

			if (current_section)
				g_menu_append (current_section, label, detailed_action);
			else
				g_menu_append (current_menu, label, detailed_action);

			g_free (label);
			(*cmd_index)++;
		}

		list = list->next;
	}

	g_slist_free (menu_stack);
}

void
menu_nickmenu (session *sess, GtkWidget *parent, double x, double y, char *nick, int num_sel)
{
	GMenu *gmenu;
	GMenu *info_submenu;
	GMenu *popup_section;
	GtkWidget *popover;
	GSimpleActionGroup *action_group;
	struct User *user = NULL;
	char buf[512];
	int cmd_index = 0;
	int popup_count;

	if (!sess)
		return;

	g_free (str_copy);
	str_copy = g_strdup (nick);
	nick_menu_sess = sess;

	/* Free any previous info and popup commands */
	nick_menu_free_info ();
	nick_popup_cmds_free ();

	/* Create action group for this menu */
	action_group = g_simple_action_group_new ();

	/* Add copy actions for user info */
	static const GActionEntry nick_info_actions[] = {
		{ "copy-realname", nick_action_copy_realname, NULL, NULL, NULL },
		{ "copy-hostname", nick_action_copy_hostname, NULL, NULL, NULL },
		{ "copy-account", nick_action_copy_account, NULL, NULL, NULL },
		{ "copy-servername", nick_action_copy_servername, NULL, NULL, NULL },
		{ "copy-away", nick_action_copy_away, NULL, NULL, NULL },
	};
	g_action_map_add_action_entries (G_ACTION_MAP (action_group), nick_info_actions,
									 G_N_ELEMENTS (nick_info_actions), NULL);

	/* Count popup items and allocate command array */
	popup_count = nick_menu_count_popup_items (popup_list);
	if (popup_count > 0)
	{
		nick_popup_cmds = g_new0 (char *, popup_count);
		nick_popup_cmd_count = popup_count;
	}

	/* Build allnicks string for multi-selection */
	if (num_sel > 1)
	{
		char **nicks;
		int i, num;
		nicks = userlist_selection_list (sess->gui->user_tree, &num);
		if (nicks && num > 0)
		{
			GString *all = g_string_new (NULL);
			for (i = 0; i < num && nicks[i]; i++)
			{
				if (i > 0)
					g_string_append_c (all, ' ');
				g_string_append (all, nicks[i]);
				g_free (nicks[i]);
			}
			g_free (nicks);
			nick_all_nicks = g_string_free (all, FALSE);
		}
	}
	else
	{
		nick_all_nicks = g_strdup (nick);
	}

	gmenu = g_menu_new ();

	/* Nick name or selection count as header */
	if (num_sel > 1)
	{
		g_snprintf (buf, sizeof buf, _("%d nicks selected."), num_sel);
		g_menu_append (gmenu, buf, NULL);
	}
	else
	{
		/* Try to find user info for single nick */
		user = userlist_find (sess, nick);
		if (!user)
			user = userlist_find_global (sess->server, nick);

		/* Create user info submenu if we have a user */
		if (user)
		{
			const char *unknown = _("Unknown");
			char *real;

			info_submenu = g_menu_new ();

			/* Store info for copy actions */
			nick_info_realname = user->realname ? g_strdup (user->realname) : NULL;
			nick_info_hostname = user->hostname ? g_strdup (user->hostname) : NULL;
			nick_info_account = user->account ? g_strdup (user->account) : NULL;
			nick_info_servername = user->servername ? g_strdup (user->servername) : NULL;

			/* Real Name */
			if (user->realname)
			{
				real = strip_color (user->realname, -1, STRIP_ALL);
				g_snprintf (buf, sizeof buf, "%s: %s", _("Real Name"), real);
				g_free (real);
			}
			else
				g_snprintf (buf, sizeof buf, "%s: %s", _("Real Name"), unknown);
			g_menu_append (info_submenu, buf, "nick.copy-realname");

			/* Hostname */
			g_snprintf (buf, sizeof buf, "%s: %s", _("User"),
						user->hostname ? user->hostname : unknown);
			g_menu_append (info_submenu, buf, "nick.copy-hostname");

			/* Account */
			g_snprintf (buf, sizeof buf, "%s: %s", _("Account"),
						user->account ? user->account : unknown);
			g_menu_append (info_submenu, buf, "nick.copy-account");

			/* Country (if available) */
			{
				const char *users_country = country (user->hostname);
				if (users_country)
				{
					g_snprintf (buf, sizeof buf, "%s: %s", _("Country"), users_country);
					g_menu_append (info_submenu, buf, NULL);
				}
			}

			/* Server */
			g_snprintf (buf, sizeof buf, "%s: %s", _("Server"),
						user->servername ? user->servername : unknown);
			g_menu_append (info_submenu, buf, "nick.copy-servername");

			/* Last message time */
			if (user->lasttalk)
			{
				g_snprintf (buf, sizeof buf, "%s: %u %s", _("Last Msg"),
							(unsigned int)((time (0) - user->lasttalk) / 60),
							_("minutes ago"));
			}
			else
				g_snprintf (buf, sizeof buf, "%s: %s", _("Last Msg"), unknown);
			g_menu_append (info_submenu, buf, NULL);

			/* Away message (if away) */
			if (user->away)
			{
				struct away_msg *away = server_away_find_message (sess->server, user->nick);
				if (away && away->message)
				{
					char *msg = strip_color (away->message, -1, STRIP_ALL);
					nick_info_away = g_strdup (away->message);
					g_snprintf (buf, sizeof buf, "%s: %s", _("Away Msg"), msg);
					g_free (msg);
					g_menu_append (info_submenu, buf, "nick.copy-away");
				}
			}

			g_menu_append_submenu (gmenu, nick, G_MENU_MODEL (info_submenu));
			g_object_unref (info_submenu);
		}
		else
		{
			g_menu_append (gmenu, nick, NULL);
		}
	}

	/* Build popup_list menu items in a new section */
	if (popup_list)
	{
		popup_section = g_menu_new ();
		nick_menu_build_popup (popup_section, popup_list,
							   num_sel > 1 ? NULL : str_copy,
							   action_group, &cmd_index);
		g_menu_append_section (gmenu, NULL, G_MENU_MODEL (popup_section));
		g_object_unref (popup_section);
	}

	/* Add plugin menu items for $NICK context */
	menu_add_plugin_items_gmenu (gmenu, action_group, "\x05$NICK", str_copy);

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "nick", G_ACTION_GROUP (action_group));
	gtk_widget_insert_action_group (popover, "popup", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, parent);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
								 &(GdkRectangle){ (int)x, (int)y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Store action group on popover for cleanup to find it */
	g_object_set_data (G_OBJECT (popover), "action-group", action_group);

	/* Clean up when popover is closed - deferred to allow actions to complete */
	g_signal_connect (popover, "closed", G_CALLBACK (nick_menu_popover_closed_cb), NULL);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}

/* stuff for the View menu */

static void
menu_showhide_cb (session *sess)
{
	if (prefs.hex_gui_hide_menu)
		gtk_widget_hide (sess->gui->menu);
	else
		gtk_widget_show (sess->gui->menu);
}

static void
menu_topic_showhide_cb (session *sess)
{
	if (prefs.hex_gui_topicbar)
		gtk_widget_show (sess->gui->topic_bar);
	else
		gtk_widget_hide (sess->gui->topic_bar);
}

static void
menu_userlist_showhide_cb (session *sess)
{
	mg_decide_userlist (sess, TRUE);
}

static void
menu_ulbuttons_showhide_cb (session *sess)
{
	if (prefs.hex_gui_ulist_buttons)
		gtk_widget_show (sess->gui->button_box);
	else
		gtk_widget_hide (sess->gui->button_box);
}

static void
menu_cmbuttons_showhide_cb (session *sess)
{
	switch (sess->type)
	{
	case SESS_CHANNEL:
		if (prefs.hex_gui_mode_buttons)
		{
			gtk_widget_show (sess->gui->topicbutton_box);
			/* Explicitly show children after parent becomes visible */
			hc_widget_show_all (sess->gui->topicbutton_box);
		}
		else
			gtk_widget_hide (sess->gui->topicbutton_box);
		break;
	default:
		gtk_widget_hide (sess->gui->topicbutton_box);
	}
}

static void
menu_setting_foreach (void (*callback) (session *), int id, guint state)
{
	session *sess;
	GSList *list;
	int maindone = FALSE;	/* do it only once for EVERY tab */

	list = sess_list;
	while (list)
	{
		sess = list->data;

		if (!sess->gui->is_tab || !maindone)
		{
			if (sess->gui->is_tab)
				maindone = TRUE;
			if (id != -1)
				gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (sess->gui->menu_item[id]), state);
			if (callback)
				callback (sess);
		}

		list = list->next;
	}
}

void
menu_bar_toggle (void)
{
	prefs.hex_gui_hide_menu = !prefs.hex_gui_hide_menu;
	menu_setting_foreach (menu_showhide_cb, MENU_ID_MENUBAR, !prefs.hex_gui_hide_menu);
}

static void
menu_bar_toggle_cb (void)
{
	menu_bar_toggle ();
	if (prefs.hex_gui_hide_menu)
		fe_message (_("The Menubar is now hidden. You can show it again"
						  " by pressing Control+F9 or right-clicking in a blank part of"
						  " the main text area."), FE_MSG_INFO);
}

static void
menu_topicbar_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_topicbar = !prefs.hex_gui_topicbar;
	menu_setting_foreach (menu_topic_showhide_cb, MENU_ID_TOPICBAR,
								 prefs.hex_gui_topicbar);
}

static void
menu_userlist_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_hide = !prefs.hex_gui_ulist_hide;
	menu_setting_foreach (menu_userlist_showhide_cb, MENU_ID_USERLIST,
								 !prefs.hex_gui_ulist_hide);
}

static void
menu_ulbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_buttons = !prefs.hex_gui_ulist_buttons;
	menu_setting_foreach (menu_ulbuttons_showhide_cb, MENU_ID_ULBUTTONS,
								 prefs.hex_gui_ulist_buttons);
}

static void
menu_cmbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_mode_buttons = !prefs.hex_gui_mode_buttons;
	menu_setting_foreach (menu_cmbuttons_showhide_cb, MENU_ID_MODEBUTTONS,
								 prefs.hex_gui_mode_buttons);
}

static void
menu_fullscreen_toggle (GtkWidget *wid, gpointer ud)
{
	if (!prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW(parent_window));
	else
	{
		gtk_window_unfullscreen (GTK_WINDOW(parent_window));

#ifdef WIN32
		if (!prefs.hex_gui_win_state) /* not maximized */
		{
			/* other window managers seem to handle this */
			gtk_window_resize (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_width, prefs.hex_gui_win_height);
			gtk_window_move (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_left, prefs.hex_gui_win_top);
		}
#endif
	}
}

/* Action callbacks for middle-click menu */
static session *middle_menu_sess = NULL;

static void
middle_action_clear_text (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "CLEAR", FALSE);
}

static void
middle_action_search (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		mg_search_toggle (middle_menu_sess);
}

static void
middle_action_save_text (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "TEXTDUMP", FALSE);
}

static void
middle_action_copy_selection (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess && middle_menu_sess->gui && middle_menu_sess->gui->xtext)
		gtk_xtext_copy_selection (GTK_XTEXT (middle_menu_sess->gui->xtext));
}

static void
middle_action_reset_marker (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "RESETMARKER", FALSE);
}

static void
middle_action_move_to_marker (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "MOVETOMARKER", FALSE);
}

static void
middle_action_menubar_toggle (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_bar_toggle ();
}

static void
middle_action_disconnect (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "DISCON", FALSE);
}

static void
middle_action_reconnect (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		handle_command (middle_menu_sess, "RECONNECT", FALSE);
}

static void
middle_action_away_toggle (GSimpleAction *action, GVariant *value, gpointer user_data)
{
	gboolean new_state;
	(void)user_data;

	if (!middle_menu_sess)
		return;

	new_state = g_variant_get_boolean (value);
	if (new_state)
		handle_command (middle_menu_sess, "AWAY", FALSE);
	else
		handle_command (middle_menu_sess, "BACK", FALSE);

	g_simple_action_set_state (action, value);
}

static void
middle_action_settings (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	extern void setup_open (void);
	(void)action; (void)parameter; (void)user_data;
	setup_open ();
}

static void
middle_action_detach (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	hc_debug_log ("middle_action_detach: called, sess=%p", (void*)middle_menu_sess);
	if (middle_menu_sess)
	{
		hc_debug_log ("  -> calling mg_detach");
		mg_detach (middle_menu_sess, 0);
		hc_debug_log ("  -> mg_detach returned");
	}
}

static void
middle_action_close (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	if (middle_menu_sess)
		fe_close_window (middle_menu_sess);
}

void
menu_middlemenu (session *sess, GtkWidget *parent, double x, double y)
{
	GtkWidget *xtext_widget;
	GtkXText *xtext;
	GMenu *gmenu;
	GMenu *section;
	GtkWidget *popover;
	GSimpleActionGroup *action_group;
	GSimpleAction *away_action;
	gboolean is_away;

	(void)parent; (void)x; (void)y;

	if (!sess || !sess->gui || !sess->gui->xtext)
		return;

	xtext_widget = sess->gui->xtext;
	xtext = GTK_XTEXT (xtext_widget);
	middle_menu_sess = sess;

	/* When the menubar is hidden, show the full main menu as a popup instead
	 * of the simplified middle-click menu. This gives users full access to
	 * all menu functions when the menubar is not visible. */
	if (prefs.hex_gui_hide_menu)
	{
		is_away = sess->server ? sess->server->is_away : FALSE;

		/* Build the full main menu */
		gmenu = menu_build_gmenu (is_away, !sess->gui->is_tab);
		action_group = menu_create_action_group (is_away, !sess->gui->is_tab);

		/* Create popover from the main menu model */
		popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
		gtk_widget_insert_action_group (popover, "menu", G_ACTION_GROUP (action_group));
		gtk_widget_set_parent (popover, xtext_widget);
		gtk_popover_set_pointing_to (GTK_POPOVER (popover),
									 &(GdkRectangle){ xtext->last_click_x, xtext->last_click_y, 1, 1 });
		gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

		/* Store action group on popover for cleanup */
		g_object_set_data (G_OBJECT (popover), "action-group", action_group);

		/* Clean up when popover is closed - deferred to allow actions to complete */
		g_signal_connect (popover, "closed", G_CALLBACK (menu_popover_closed_cb), NULL);

		gtk_popover_popup (GTK_POPOVER (popover));
		g_object_unref (gmenu);
		return;
	}

	/* Simplified middle-click menu when menubar is visible */

	/* Create action group for this menu */
	action_group = g_simple_action_group_new ();

	static const GActionEntry middle_actions[] = {
		{ "clear-text", middle_action_clear_text, NULL, NULL, NULL },
		{ "search", middle_action_search, NULL, NULL, NULL },
		{ "save-text", middle_action_save_text, NULL, NULL, NULL },
		{ "copy-selection", middle_action_copy_selection, NULL, NULL, NULL },
		{ "reset-marker", middle_action_reset_marker, NULL, NULL, NULL },
		{ "move-to-marker", middle_action_move_to_marker, NULL, NULL, NULL },
		{ "menubar-toggle", middle_action_menubar_toggle, NULL, NULL, NULL },
		{ "disconnect", middle_action_disconnect, NULL, NULL, NULL },
		{ "reconnect", middle_action_reconnect, NULL, NULL, NULL },
		{ "settings", middle_action_settings, NULL, NULL, NULL },
		{ "detach", middle_action_detach, NULL, NULL, NULL },
		{ "close", middle_action_close, NULL, NULL, NULL },
	};
	g_action_map_add_action_entries (G_ACTION_MAP (action_group), middle_actions,
									 G_N_ELEMENTS (middle_actions), NULL);

	/* Add away toggle as stateful action */
	is_away = sess->server ? sess->server->is_away : FALSE;
	away_action = g_simple_action_new_stateful ("away", NULL, g_variant_new_boolean (is_away));
	g_signal_connect (away_action, "change-state", G_CALLBACK (middle_action_away_toggle), NULL);
	g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (away_action));
	g_object_unref (away_action);

	gmenu = g_menu_new ();

	/* Window operations section */
	section = g_menu_new ();
	g_menu_append (section, _("Copy Selection"), "middle.copy-selection");
	g_menu_append (section, _("Clear Text"), "middle.clear-text");
	g_menu_append (section, _("Search Text" ELLIPSIS), "middle.search");
	g_menu_append (section, _("Save Text" ELLIPSIS), "middle.save-text");
	g_menu_append_section (gmenu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	/* Marker line section */
	section = g_menu_new ();
	g_menu_append (section, _("Reset Marker Line"), "middle.reset-marker");
	g_menu_append (section, _("Move to Marker Line"), "middle.move-to-marker");
	g_menu_append_section (gmenu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	/* Server actions section */
	section = g_menu_new ();
	g_menu_append (section, _("Disconnect"), "middle.disconnect");
	g_menu_append (section, _("Reconnect"), "middle.reconnect");
	g_menu_append (section, _("Marked Away"), "middle.away");
	g_menu_append_section (gmenu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	/* View options section - only show Hide Menubar since menubar is visible */
	section = g_menu_new ();
	g_menu_append (section, _("Hide Menubar"), "middle.menubar-toggle");
	g_menu_append (section, _("Preferences"), "middle.settings");
	g_menu_append_section (gmenu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	/* Window management section */
	section = g_menu_new ();
	g_menu_append (section, sess->gui->is_tab ? _("Detach") : _("Attach"), "middle.detach");
	g_menu_append (section, _("Close"), "middle.close");
	g_menu_append_section (gmenu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "middle", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, xtext_widget);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
								 &(GdkRectangle){ xtext->last_click_x, xtext->last_click_y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Store action group on popover for cleanup to find it */
	g_object_set_data (G_OBJECT (popover), "action-group", action_group);

	/* Clean up when popover is closed - deferred to allow actions to complete */
	g_signal_connect (popover, "closed", G_CALLBACK (menu_popover_closed_cb), NULL);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}

static void
open_url_cb (GtkWidget *item, char *url)
{
	char buf[512];

	/* pass this to /URL so it can handle irc:// */
	g_snprintf (buf, sizeof (buf), "URL %s", url);
	handle_command (current_sess, buf, FALSE);
}

/* Action callbacks for URL menu */
static char **url_handler_cmds = NULL;   /* array of URL handler commands */
static int url_handler_cmd_count = 0;    /* count of URL handler commands */

static void
url_action_open (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char buf[512];
	(void)action; (void)parameter; (void)user_data;
	if (str_copy)
	{
		g_snprintf (buf, sizeof (buf), "URL %s", str_copy);
		handle_command (current_sess, buf, FALSE);
	}
}

static void
url_action_copy (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter;
	if (str_copy)
		gtkutil_copy_to_clipboard (GTK_WIDGET (user_data), FALSE, str_copy);
}

/* Generic URL handler command callback */
static void
url_handler_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	int index;
	char *buf;
	int len;
	(void)action; (void)user_data;

	if (!parameter || !str_copy)
		return;

	index = g_variant_get_int32 (parameter);
	if (index < 0 || index >= url_handler_cmd_count || !url_handler_cmds[index])
		return;

	/* Handle command execution with %s substitution */
	len = strlen (url_handler_cmds[index]) + strlen (str_copy) + 8;
	buf = g_malloc (len);
	auto_insert (buf, len, url_handler_cmds[index], 0, 0, "", "",
				 str_copy, "", "", "", "", "");

	if (buf[0] == '!')
		hexchat_exec (buf + 1);
	else
		handle_command (current_sess, buf, FALSE);

	g_free (buf);
}

static void
url_handler_cmds_free (void)
{
	int i;
	if (url_handler_cmds)
	{
		for (i = 0; i < url_handler_cmd_count; i++)
			g_free (url_handler_cmds[i]);
		g_free (url_handler_cmds);
		url_handler_cmds = NULL;
	}
	url_handler_cmd_count = 0;
}

/* Data for middle menu popover cleanup */
typedef struct {
	GtkWidget *popover;           /* NULL if popover was destroyed */
	GSimpleActionGroup *action_group;
} MiddleMenuCleanupData;

/* Weak notify - called when popover is finalized */
static void
menu_popover_weak_notify (gpointer data, GObject *where_the_object_was)
{
	MiddleMenuCleanupData *cleanup = data;
	hc_debug_log ("menu_popover_weak_notify: popover %p finalized", where_the_object_was);
	cleanup->popover = NULL;  /* Mark as destroyed */
}

/* Idle callback to clean up action group after actions complete */
static gboolean
menu_popover_cleanup_idle (gpointer user_data)
{
	MiddleMenuCleanupData *cleanup = user_data;

	hc_debug_log ("menu_popover_cleanup_idle: cleanup=%p, popover=%p, action_group=%p",
	              (void*)cleanup, (void*)cleanup->popover, (void*)cleanup->action_group);

	/* Clean up action group */
	if (cleanup->action_group)
	{
		hc_debug_log ("  -> unreffing action_group");
		g_object_unref (cleanup->action_group);
	}

	/* Remove weak ref if popover still exists.
	 * We do NOT unparent the popover - GTK4 automatically cleans up popovers
	 * when their parent window is destroyed. Trying to unparent after the
	 * parent hierarchy is partially destroyed causes crashes. */
	if (cleanup->popover != NULL)
	{
		hc_debug_log ("  -> popover still valid, removing weak ref");
		g_object_weak_unref (G_OBJECT (cleanup->popover), menu_popover_weak_notify, cleanup);
	}
	else
	{
		hc_debug_log ("  -> popover was destroyed (weak notify fired)");
	}

	g_free (cleanup);
	hc_debug_log ("  -> cleanup complete");
	return G_SOURCE_REMOVE;
}

static void
menu_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	GSimpleActionGroup *action_group = g_object_get_data (G_OBJECT (popover), "action-group");
	MiddleMenuCleanupData *cleanup = g_new0 (MiddleMenuCleanupData, 1);

	hc_debug_log ("menu_popover_closed_cb: popover=%p, action_group=%p",
	              (void*)popover, (void*)action_group);

	cleanup->popover = GTK_WIDGET (popover);
	cleanup->action_group = action_group;

	/* Use weak ref to detect if popover is destroyed before our idle runs */
	g_object_weak_ref (G_OBJECT (popover), menu_popover_weak_notify, cleanup);

	/* Defer cleanup to allow action callbacks to complete.
	 * The action fires AFTER the closed signal, so we can't clean up here. */
	g_idle_add (menu_popover_cleanup_idle, cleanup);
	hc_debug_log ("  -> scheduled idle cleanup");
	(void)user_data;
}

/* Data for URL menu popover cleanup */
typedef struct {
	GtkWidget *popover;           /* NULL if popover was destroyed */
	GSimpleActionGroup *action_group;
} UrlMenuCleanupData;

/* Weak notify - called when popover is finalized */
static void
url_menu_popover_weak_notify (gpointer data, GObject *where_the_object_was)
{
	UrlMenuCleanupData *cleanup = data;
	(void)where_the_object_was;
	cleanup->popover = NULL;  /* Mark as destroyed */
}

/* Idle callback to clean up URL menu popover and action group */
static gboolean
url_menu_popover_cleanup_idle (gpointer user_data)
{
	UrlMenuCleanupData *cleanup = user_data;

	/* Clean up action group */
	if (cleanup->action_group)
		g_object_unref (cleanup->action_group);

	/* Free URL handler resources */
	url_handler_cmds_free ();

	/* Remove weak ref if popover still exists.
	 * We do NOT unparent the popover - GTK4 automatically cleans up popovers
	 * when their parent window is destroyed. Trying to unparent after the
	 * parent hierarchy is partially destroyed causes crashes. */
	if (cleanup->popover != NULL)
		g_object_weak_unref (G_OBJECT (cleanup->popover), url_menu_popover_weak_notify, cleanup);

	g_free (cleanup);
	return G_SOURCE_REMOVE;
}

static void
url_menu_popover_closed_cb (GtkPopover *popover, gpointer user_data)
{
	GSimpleActionGroup *action_group = g_object_get_data (G_OBJECT (popover), "action-group");
	UrlMenuCleanupData *cleanup = g_new0 (UrlMenuCleanupData, 1);

	cleanup->popover = GTK_WIDGET (popover);
	cleanup->action_group = action_group;

	/* Use weak ref to detect if popover is destroyed before our idle runs */
	g_object_weak_ref (G_OBJECT (popover), url_menu_popover_weak_notify, cleanup);

	/* Defer cleanup to allow action callbacks to complete */
	g_idle_add (url_menu_popover_cleanup_idle, cleanup);
	(void)user_data;
}

/* Build URL handler menu items, similar to nick popup but simpler */
static void
url_menu_build_handlers (GMenu *menu, GSList *list, GSimpleActionGroup *action_group, int *cmd_index)
{
	struct popup *pop;
	char action_name[64];
	char detailed_action[128];
	char *label;
	GSimpleAction *action;

	while (list)
	{
		pop = (struct popup *) list->data;

		/* Skip SUB/ENDSUB/SEP/TOGGLE for URL menu - keep it simple */
		if (g_ascii_strncasecmp (pop->name, "SUB", 3) == 0 ||
			g_ascii_strncasecmp (pop->name, "ENDSUB", 6) == 0 ||
			g_ascii_strncasecmp (pop->name, "SEP", 3) == 0 ||
			g_ascii_strncasecmp (pop->name, "TOGGLE", 6) == 0)
		{
			list = list->next;
			continue;
		}

		/* Check if program is in path for ! commands */
		if (pop->cmd[0] == '!' && !is_in_path (pop->cmd))
		{
			list = list->next;
			continue;
		}

		label = nick_menu_extract_label (pop->name);

		/* Create action for this handler */
		g_snprintf (action_name, sizeof action_name, "handler%d", *cmd_index);
		action = g_simple_action_new (action_name, G_VARIANT_TYPE_INT32);
		g_signal_connect (action, "activate", G_CALLBACK (url_handler_action_cb), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
		g_object_unref (action);

		/* Store the command */
		url_handler_cmds[*cmd_index] = g_strdup (pop->cmd);

		/* Add menu item */
		g_snprintf (detailed_action, sizeof detailed_action, "url.handler%d(%d)",
					*cmd_index, *cmd_index);
		g_menu_append (menu, label, detailed_action);

		g_free (label);
		(*cmd_index)++;

		list = list->next;
	}
}

void
menu_urlmenu (GtkWidget *parent, double x, double y, char *url)
{
	GMenu *gmenu;
	GMenu *handlers_section;
	GtkWidget *popover;
	GSimpleActionGroup *action_group;
	char *tmp, *chop;
	int cmd_index = 0;
	int handler_count;

	g_free (str_copy);
	str_copy = g_strdup (url);

	/* Free any previous handler commands */
	url_handler_cmds_free ();

	/* Create action group for this menu */
	action_group = g_simple_action_group_new ();

	static const GActionEntry url_actions[] = {
		{ "open", url_action_open, NULL, NULL, NULL },
		{ "copy", url_action_copy, NULL, NULL, NULL },
	};
	g_action_map_add_action_entries (G_ACTION_MAP (action_group), url_actions,
									 G_N_ELEMENTS (url_actions), parent);

	/* Count and allocate URL handlers */
	handler_count = nick_menu_count_popup_items (urlhandler_list);
	if (handler_count > 0)
	{
		url_handler_cmds = g_new0 (char *, handler_count);
		url_handler_cmd_count = handler_count;
	}

	gmenu = g_menu_new ();

	/* Display URL (truncated if needed) - as a disabled label */
	if (g_utf8_strlen (str_copy, -1) >= 52)
	{
		tmp = g_strdup (str_copy);
		chop = g_utf8_offset_to_pointer (tmp, 48);
		chop[0] = chop[1] = chop[2] = '.';
		chop[3] = 0;
		/* Add as section label */
		g_menu_append (gmenu, tmp, NULL);
		g_free (tmp);
	}
	else
	{
		g_menu_append (gmenu, str_copy, NULL);
	}

	/* Add action items */
	if (strncmp (str_copy, "irc://", 6) == 0 ||
		strncmp (str_copy, "ircs://", 7) == 0)
		g_menu_append (gmenu, _("Connect"), "url.open");
	else
		g_menu_append (gmenu, _("Open Link in Browser"), "url.open");

	g_menu_append (gmenu, _("Copy Selected Link"), "url.copy");

	/* Add custom URL handlers from urlhandlers.conf */
	if (urlhandler_list)
	{
		handlers_section = g_menu_new ();
		url_menu_build_handlers (handlers_section, urlhandler_list, action_group, &cmd_index);
		if (g_menu_model_get_n_items (G_MENU_MODEL (handlers_section)) > 0)
			g_menu_append_section (gmenu, NULL, G_MENU_MODEL (handlers_section));
		g_object_unref (handlers_section);
	}

	/* Add plugin menu items for $URL context */
	menu_add_plugin_items_gmenu (gmenu, action_group, "\x04$URL", str_copy);

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "url", G_ACTION_GROUP (action_group));
	gtk_widget_insert_action_group (popover, "popup", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, parent);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
								 &(GdkRectangle){ (int)x, (int)y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Store action group on popover for cleanup to find it */
	g_object_set_data (G_OBJECT (popover), "action-group", action_group);

	/* Clean up when popover is closed - deferred to allow actions to complete */
	g_signal_connect (popover, "closed", G_CALLBACK (url_menu_popover_closed_cb), NULL);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}

static void
menu_chan_cycle (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "CYCLE %s", chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_part (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "part %s", chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_focus (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "doat %s gui focus", chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_join (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "join %s", chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

/* Action callbacks for channel menu */
static server *chan_menu_server = NULL;  /* server for autojoin toggle */

static void
chan_action_join (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char tbuf[256];
	(void)action; (void)parameter; (void)user_data;
	if (current_sess && str_copy)
	{
		g_snprintf (tbuf, sizeof tbuf, "join %s", str_copy);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
chan_action_focus (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char tbuf[256];
	(void)action; (void)parameter; (void)user_data;
	if (current_sess && str_copy)
	{
		g_snprintf (tbuf, sizeof tbuf, "doat %s gui focus", str_copy);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
chan_action_part (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char tbuf[256];
	(void)action; (void)parameter; (void)user_data;
	if (current_sess && str_copy)
	{
		g_snprintf (tbuf, sizeof tbuf, "part %s", str_copy);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
chan_action_cycle (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	char tbuf[256];
	(void)action; (void)parameter; (void)user_data;
	if (current_sess && str_copy)
	{
		g_snprintf (tbuf, sizeof tbuf, "CYCLE %s", str_copy);
		handle_command (current_sess, tbuf, FALSE);
	}
}

/* Toggle autojoin action for channel menu */
static void
chan_action_autojoin_toggled (GSimpleAction *action, GVariant *value, gpointer user_data)
{
	gboolean new_state;
	(void)user_data;

	if (!chan_menu_server || !chan_menu_server->network || !str_copy)
		return;

	new_state = g_variant_get_boolean (value);
	servlist_autojoinedit (chan_menu_server->network, str_copy, new_state);

	/* Update the action state */
	g_simple_action_set_state (action, value);
}

void
menu_chanmenu (session *sess, GtkWidget *parent, double x, double y, char *chan)
{
	GtkWidget *xtext_widget;
	GtkXText *xtext;
	GMenu *gmenu;
	GMenu *options_section;
	GtkWidget *popover;
	GSimpleActionGroup *action_group;
	GSimpleAction *autojoin_action;
	int is_joined = FALSE;
	session *chan_session;
	gboolean is_autojoin;

	(void)parent; (void)x; (void)y;

	if (!sess || !sess->gui || !sess->gui->xtext)
		return;

	xtext_widget = sess->gui->xtext;
	xtext = GTK_XTEXT (xtext_widget);

	chan_session = find_channel (sess->server, chan);
	if (chan_session)
		is_joined = TRUE;

	g_free (str_copy);
	str_copy = g_strdup (chan);
	chan_menu_server = sess->server;

	/* Create action group for this menu */
	action_group = g_simple_action_group_new ();

	static const GActionEntry chan_actions[] = {
		{ "join", chan_action_join, NULL, NULL, NULL },
		{ "focus", chan_action_focus, NULL, NULL, NULL },
		{ "part", chan_action_part, NULL, NULL, NULL },
		{ "cycle", chan_action_cycle, NULL, NULL, NULL },
	};
	g_action_map_add_action_entries (G_ACTION_MAP (action_group), chan_actions,
									 G_N_ELEMENTS (chan_actions), NULL);

	/* Add autojoin toggle action if we have a network */
	if (sess->server && sess->server->network)
	{
		is_autojoin = joinlist_is_in_list (sess->server, chan);
		autojoin_action = g_simple_action_new_stateful ("autojoin", NULL,
														g_variant_new_boolean (is_autojoin));
		g_signal_connect (autojoin_action, "change-state",
						  G_CALLBACK (chan_action_autojoin_toggled), NULL);
		g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (autojoin_action));
		g_object_unref (autojoin_action);
	}

	gmenu = g_menu_new ();

	/* Channel name as label */
	g_menu_append (gmenu, chan, NULL);

	/* Add action items based on join state */
	if (!is_joined)
	{
		g_menu_append (gmenu, _("Join Channel"), "chan.join");
	}
	else
	{
		if (chan_session != current_sess)
			g_menu_append (gmenu, _("Focus Channel"), "chan.focus");
		g_menu_append (gmenu, _("Part Channel"), "chan.part");
		g_menu_append (gmenu, _("Cycle Channel"), "chan.cycle");
	}

	/* Add autojoin toggle in a separate section if we have a network */
	if (sess->server && sess->server->network)
	{
		options_section = g_menu_new ();
		g_menu_append (options_section, _("Autojoin Channel"), "chan.autojoin");
		g_menu_append_section (gmenu, NULL, G_MENU_MODEL (options_section));
		g_object_unref (options_section);
	}

	/* Add plugin menu items for $CHAN context */
	menu_add_plugin_items_gmenu (gmenu, action_group, "\x05$CHAN", str_copy);

	/* Create and configure the popover */
	popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (gmenu));
	gtk_widget_insert_action_group (popover, "chan", G_ACTION_GROUP (action_group));
	gtk_widget_insert_action_group (popover, "popup", G_ACTION_GROUP (action_group));
	gtk_widget_set_parent (popover, xtext_widget);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover),
								 &(GdkRectangle){ xtext->last_click_x, xtext->last_click_y, 1, 1 });
	gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

	/* Clean up action group when popover is closed */
	g_signal_connect (popover, "closed", G_CALLBACK (menu_popover_closed_cb), action_group);

	gtk_popover_popup (GTK_POPOVER (popover));
	g_object_unref (gmenu);
}

static void
menu_delfav_cb (GtkWidget *item, server *serv)
{
	servlist_autojoinedit (serv->network, str_copy, FALSE);
}

static void
menu_addfav_cb (GtkWidget *item, server *serv)
{
	servlist_autojoinedit (serv->network, str_copy, TRUE);
}

void
menu_addfavoritemenu (server *serv, GtkWidget *menu, char *channel, gboolean istree)
{
	char *str;
	
	if (!serv->network)
		return;

	if (channel != str_copy)
	{
		g_free (str_copy);
		str_copy = g_strdup (channel);
	}
	
	if (istree)
		str = _("_Autojoin");
	else
		str = _("Autojoin Channel");

	if (joinlist_is_in_list (serv, channel))
	{
		menu_toggle_item (str, menu, menu_delfav_cb, serv, TRUE);
	}
	else
	{
		menu_toggle_item (str, menu, menu_addfav_cb, serv, FALSE);
	}
}

static void
menu_delautoconn_cb (GtkWidget *item, server *serv)
{
	((ircnet*)serv->network)->flags &= ~FLAG_AUTO_CONNECT;
	servlist_save ();
}

static void
menu_addautoconn_cb (GtkWidget *item, server *serv)
{
	((ircnet*)serv->network)->flags |= FLAG_AUTO_CONNECT;
	servlist_save ();
}

void
menu_addconnectmenu (server *serv, GtkWidget *menu)
{
	if (!serv->network)
		return;

	if (((ircnet*)serv->network)->flags & FLAG_AUTO_CONNECT)
	{
		menu_toggle_item (_("_Auto-Connect"), menu, menu_delautoconn_cb, serv, TRUE);
	}
	else
	{
		menu_toggle_item (_("_Auto-Connect"), menu, menu_addautoconn_cb, serv, FALSE);
	}
}

static void
menu_open_server_list (GtkWidget *wid, gpointer none)
{
	fe_serverlist_open (current_sess);
}

static void
menu_settings (GtkWidget * wid, gpointer none)
{
	extern void setup_open (void);
	setup_open ();
}

static void
menu_usermenu (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("User menu - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, usermenu_list, buf, "usermenu", "usermenu.conf", 0);
}

static void
usermenu_create (GtkWidget *menu)
{
	menu_create (menu, usermenu_list, "", FALSE);
	menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);	/* sep */
	menu_quick_item_with_callback (menu_usermenu, _("Edit This Menu" ELLIPSIS), menu, 0);
}

static void
usermenu_destroy (GtkWidget * menu)
{
	GList *items = gtk_container_get_children (GTK_CONTAINER (menu));
	GList *l;

	for (l = items; l != NULL; l = l->next)
	{
		hc_widget_destroy (GTK_WIDGET (l->data));
	}
	g_list_free (items);
}

void
usermenu_update (void)
{
	int done_main = FALSE;
	GSList *list = sess_list;
	session *sess;
	GtkWidget *menu;

	while (list)
	{
		sess = list->data;
		menu = sess->gui->menu_item[MENU_ID_USERMENU];
		if (sess->gui->is_tab)
		{
			if (!done_main && menu)
			{
				usermenu_destroy (menu);
				usermenu_create (menu);
				done_main = TRUE;
			}
		} else if (menu)
		{
			usermenu_destroy (menu);
			usermenu_create (menu);
		}
		list = list->next;
	}
}

static void
menu_newserver_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newchannel_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (current_sess->server, NULL, SESS_CHANNEL, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newserver_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;
	int oldf = prefs.hex_gui_tab_newtofront;

	prefs.hex_gui_tab_chans = 1;
	/* force focus if setting is "only requested tabs" */
	if (prefs.hex_gui_tab_newtofront == 2)
		prefs.hex_gui_tab_newtofront = 1;
	new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	prefs.hex_gui_tab_chans = old;
	prefs.hex_gui_tab_newtofront = oldf;
}

static void
menu_newchannel_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 1;
	new_ircwindow (current_sess->server, NULL, SESS_CHANNEL, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_rawlog (GtkWidget * wid, gpointer none)
{
	open_rawlog (current_sess->server);
}

static void
menu_detach (GtkWidget * wid, gpointer none)
{
	mg_detach (current_sess, 0);
}

static void
menu_close (GtkWidget * wid, gpointer none)
{
	mg_close_sess (current_sess);
}

static void
menu_quit (GtkWidget * wid, gpointer none)
{
	mg_open_quit_dialog (FALSE);
}

static void
menu_search (void)
{
	mg_search_toggle (current_sess);
}

static void
menu_search_next (GtkWidget *wid)
{
	mg_search_handle_next(wid, current_sess);
}

static void
menu_search_prev (GtkWidget *wid)
{
	mg_search_handle_previous(wid, current_sess);
}

static void
menu_resetmarker (GtkWidget * wid, gpointer none)
{
	gtk_xtext_reset_marker_pos (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_movetomarker (GtkWidget *wid, gpointer none)
{
	marker_reset_reason reason;
	char *str;

	if (!prefs.hex_text_show_marker)
		PrintText (current_sess, _("Marker line disabled."));
	else
	{
		reason = gtk_xtext_moveto_marker_pos (GTK_XTEXT (current_sess->gui->xtext));
		switch (reason) {
		case MARKER_WAS_NEVER_SET:
			str = _("Marker line never set."); break;
		case MARKER_IS_SET:
			str = ""; break;
		case MARKER_RESET_MANUALLY:
			str = _("Marker line reset manually."); break;
		case MARKER_RESET_BY_KILL:
			str = _("Marker line reset because exceeded scrollback limit."); break;
		case MARKER_RESET_BY_CLEAR:
			str = _("Marker line reset by CLEAR command."); break;
		default:
			str = _("Marker line state unknown."); break;
		}
		if (str[0])
			PrintText (current_sess, str);
	}
}

static void
menu_copy_selection (GtkWidget * wid, gpointer none)
{
	gtk_xtext_copy_selection (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_flushbuffer (GtkWidget * wid, gpointer none)
{
	fe_text_clear (current_sess, 0);
}

static void
savebuffer_req_done (session *sess, char *file)
{
	int fh;

	if (!file)
		return;

	fh = g_open (file, O_TRUNC | O_WRONLY | O_CREAT, 0600);
	if (fh != -1)
	{
		gtk_xtext_save (GTK_XTEXT (sess->gui->xtext), fh);
		close (fh);
	}
}

static void
menu_savebuffer (GtkWidget * wid, gpointer none)
{
	gtkutil_file_req (NULL, _("Select an output filename"), savebuffer_req_done,
							current_sess, NULL, NULL, FRF_WRITE);
}

static void
menu_disconnect (GtkWidget * wid, gpointer none)
{
	handle_command (current_sess, "DISCON", FALSE);
}

static void
menu_reconnect (GtkWidget * wid, gpointer none)
{
	if (current_sess->server->hostname[0])
		handle_command (current_sess, "RECONNECT", FALSE);
	else
		fe_serverlist_open (current_sess);
}

static void
menu_join_cb (GtkWidget *dialog, gint response, GtkEntry *entry)
{
	switch (response)
	{
	case GTK_RESPONSE_ACCEPT:
		menu_chan_join (NULL, (char *)hc_entry_get_text (GTK_WIDGET(entry)));
		break;

	case GTK_RESPONSE_HELP:
		chanlist_opengui (current_sess->server, TRUE);
		break;
	}

	hc_window_destroy (dialog);
}

static void
menu_join_entry_cb (GtkWidget *entry, GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_ACCEPT);
}

static void
menu_join (GtkWidget * wid, gpointer none)
{
	GtkWidget *hbox, *dialog, *entry, *label;

	dialog = gtk_dialog_new_with_buttons (_("Join Channel"),
									GTK_WINDOW (parent_window), 0,
									_("Retrieve channel list"), GTK_RESPONSE_HELP,
									_("_Cancel"), GTK_RESPONSE_REJECT,
									_("_OK"), GTK_RESPONSE_ACCEPT,
									NULL);
	gtk_box_set_homogeneous (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), TRUE);
	hc_window_set_position (dialog, GTK_WIN_POS_MOUSE);
	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	entry = gtk_entry_new ();
	gtk_editable_set_editable (GTK_EDITABLE (entry), FALSE);	/* avoid auto-selection */
	hc_entry_set_text (entry, "#");
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (menu_join_entry_cb), dialog);
	hc_box_pack_end (hbox, entry, 0, 0, 0);

	label = gtk_label_new (_("Enter Channel to Join:"));
	hc_box_pack_end (hbox, label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (menu_join_cb), entry);

	hc_box_add (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), hbox);

	hc_widget_show_all (dialog);

	gtk_editable_set_editable (GTK_EDITABLE (entry), TRUE);
	gtk_editable_set_position (GTK_EDITABLE (entry), 1);
}

static void
menu_away (GtkCheckMenuItem *item, gpointer none)
{
	handle_command (current_sess, gtk_check_menu_item_get_active (item) ? "away" : "back", FALSE);
}

static void
menu_chanlist (GtkWidget * wid, gpointer none)
{
	chanlist_opengui (current_sess->server, FALSE);
}

static void
menu_banlist (GtkWidget * wid, gpointer none)
{
	banlist_opengui (current_sess);
}

#ifdef USE_PLUGIN

static void
menu_loadplugin (void)
{
	plugingui_load ();
}

static void
menu_pluginlist (void)
{
	plugingui_open ();
}

#else

static void
menu_noplugin_info (void)
{
	fe_message (_(DISPLAY_NAME " has been build without plugin support."), FE_MSG_INFO);
}

#define menu_loadplugin menu_noplugin_info
#define menu_pluginlist menu_noplugin_info

#endif

#define usercommands_help  _("User Commands - Special codes:\n\n"\
                           "%c  =  current channel\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
                           "%n  =  your nick\n"\
									"%t  =  time/date\n"\
                           "%v  =  HexChat version\n"\
                           "%2  =  word 2\n"\
                           "%3  =  word 3\n"\
                           "&2  =  word 2 to the end of line\n"\
                           "&3  =  word 3 to the end of line\n\n"\
                           "eg:\n"\
                           "/cmd john hello\n\n"\
                           "%2 would be \042john\042\n"\
                           "&2 would be \042john hello\042.")

#define ulbutton_help       _("Userlist Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define dlgbutton_help      _("Dialog Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define ctcp_help          _("CTCP Replies - Special codes:\n\n"\
                           "%d  =  data (the whole ctcp)\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
                           "%s  =  nick who sent the ctcp\n"\
                           "%t  =  time/date\n"\
                           "%2  =  word 2\n"\
                           "%3  =  word 3\n"\
                           "&2  =  word 2 to the end of line\n"\
                           "&3  =  word 3 to the end of line\n\n")

#define url_help           _("URL Handlers - Special codes:\n\n"\
                           "%s  =  the URL string\n\n"\
                           "Putting a ! in front of the command\n"\
                           "indicates it should be sent to a\n"\
                           "shell instead of HexChat")

static void
menu_usercommands (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("User Defined Commands - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, command_list, buf, "commands", "commands.conf",
							usercommands_help);
}

static void
menu_ulpopup (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Userlist Popup menu -  %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, popup_list, buf, "popup", "popup.conf", ulbutton_help);
}

static void
menu_rpopup (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Replace - %s"), _(DISPLAY_NAME));
	editlist_gui_open (_("Text"), _("Replace with"), replace_list, buf, "replace", "replace.conf", 0);
}

static void
menu_urlhandlers (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("URL Handlers - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, urlhandler_list, buf, "urlhandlers", "urlhandlers.conf", url_help);
}

static void
menu_evtpopup (void)
{
	pevent_dialog_show ();
}

static void
menu_keypopup (void)
{
	key_dialog_show ();
}

static void
menu_ulbuttons (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Userlist buttons - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, button_list, buf, "buttons", "buttons.conf", ulbutton_help);
}

static void
menu_dlgbuttons (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Dialog buttons - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, dlgbutton_list, buf, "dlgbuttons", "dlgbuttons.conf",
							 dlgbutton_help);
}

static void
menu_ctcpguiopen (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("CTCP Replies - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, ctcp_list, buf, "ctcpreply", "ctcpreply.conf", ctcp_help);
}

static void
menu_docs (GtkWidget *wid, gpointer none)
{
	fe_open_url ("http://hexchat.readthedocs.org");
}

/*static void
menu_webpage (GtkWidget *wid, gpointer none)
{
	fe_open_url ("http://xchat.org");
}*/

static void
menu_dcc_win (GtkWidget *wid, gpointer none)
{
	fe_dcc_open_recv_win (FALSE);
	fe_dcc_open_send_win (FALSE);
}

static void
menu_dcc_chat_win (GtkWidget *wid, gpointer none)
{
	fe_dcc_open_chat_win (FALSE);
}

void
menu_change_layout (void)
{
	if (prefs.hex_gui_tab_layout == 0)
	{
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TABS, 1);
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TREE, 0);
		mg_change_layout (0);
	} else
	{
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TABS, 0);
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TREE, 1);
		mg_change_layout (2);
	}
}

static void
menu_layout_cb (GtkWidget *item, gpointer none)
{
	prefs.hex_gui_tab_layout = 2;
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
		prefs.hex_gui_tab_layout = 0;

	menu_change_layout ();
}

static void
menu_apply_metres_cb (session *sess)
{
	mg_update_meters (sess->gui);
}

static void
menu_metres_off (GtkWidget *item, gpointer none)
{
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
	{
		prefs.hex_gui_lagometer = 0;
		prefs.hex_gui_throttlemeter = 0;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_text (GtkWidget *item, gpointer none)
{
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
	{
		prefs.hex_gui_lagometer = 2;
		prefs.hex_gui_throttlemeter = 2;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_graph (GtkWidget *item, gpointer none)
{
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
	{
		prefs.hex_gui_lagometer = 1;
		prefs.hex_gui_throttlemeter = 1;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_both (GtkWidget *item, gpointer none)
{
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
	{
		prefs.hex_gui_lagometer = 3;
		prefs.hex_gui_throttlemeter = 3;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
about_dialog_close (GtkDialog *dialog, int response, gpointer data)
{
	hc_window_destroy (GTK_WIDGET(dialog));
}

static gboolean
about_dialog_openurl (GtkAboutDialog *dialog, char *uri, gpointer data)
{
	fe_open_url (uri);
	return TRUE;
}

static void
menu_about (GtkWidget *wid, gpointer sess)
{
	GtkAboutDialog *dialog = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
	char comment[512];
	char *license = "This program is free software; you can redistribute it and/or modify\n" \
					"it under the terms of the GNU General Public License as published by\n" \
					"the Free Software Foundation; version 2.\n\n" \
					"This program is distributed in the hope that it will be useful,\n" \
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n" \
					"GNU General Public License for more details.\n\n" \
					"You should have received a copy of the GNU General Public License\n" \
					"along with this program. If not, see <http://www.gnu.org/licenses/>";

	g_snprintf  (comment, sizeof(comment), ""
#ifdef WIN32
				"Portable Mode: %s\n"
				"Build Type: x%d\n"
#endif
				"OS: %s",
#ifdef WIN32
				(portable_mode () ? "Yes" : "No"),
				get_cpu_arch (),
#endif
				get_sys_str (0));

	gtk_about_dialog_set_program_name (dialog, _(DISPLAY_NAME));
	gtk_about_dialog_set_version (dialog, PACKAGE_VERSION);
	gtk_about_dialog_set_license (dialog, license); /* gtk3 can use GTK_LICENSE_GPL_2_0 */
	gtk_about_dialog_set_website (dialog, "http://hexchat.github.io");
	gtk_about_dialog_set_website_label (dialog, "Website");
	{
		GdkTexture *texture = hc_pixbuf_to_texture (pix_hexchat);
		if (texture)
		{
			gtk_about_dialog_set_logo (dialog, GDK_PAINTABLE (texture));
			g_object_unref (texture);
		}
	}
	gtk_about_dialog_set_copyright (dialog, "\302\251 1998-2010 Peter \305\275elezn\303\275\n\302\251 2009-2014 Berke Viktor");
	gtk_about_dialog_set_comments (dialog, comment);

	gtk_window_set_transient_for (GTK_WINDOW(dialog), GTK_WINDOW(parent_window));
	g_signal_connect (G_OBJECT(dialog), "response", G_CALLBACK(about_dialog_close), NULL);
	g_signal_connect (G_OBJECT(dialog), "activate-link", G_CALLBACK(about_dialog_openurl), NULL);
	
	hc_widget_show_all (GTK_WIDGET(dialog));
}

static struct mymenu mymenu[] = {
	{N_("He_xChat"), 0, 0, M_NEWMENU, MENU_ID_HEXCHAT, 0, 1},
	{N_("Network Li_st"), menu_open_server_list, (char *)&pix_book, M_MENUPIX, 0, 0, 1, GDK_KEY_s},
	{0, 0, 0, M_SEP, 0, 0, 0},

	{N_("_New"), 0, "document-new", M_MENUSUB, 0, 0, 1},
		{N_("Server Tab"), menu_newserver_tab, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_t},
		{N_("Channel Tab"), menu_newchannel_tab, 0, M_MENUITEM, 0, 0, 1},
		{N_("Server Window"), menu_newserver_window, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_n},
		{N_("Channel Window"), menu_newchannel_window, 0, M_MENUITEM, 0, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},
	{0, 0, 0, M_SEP, 0, 0, 0},

	{N_("_Load Plugin or Script" ELLIPSIS), menu_loadplugin, "document-revert", M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},	/* 11 */
#define DETACH_OFFSET (12)
	{0, menu_detach, "edit-redo", M_MENUSTOCK, 0, 0, 1},	/* 12 */
#define CLOSE_OFFSET (13)
	{0, menu_close, "window-close", M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("_Quit"), menu_quit, "application-exit", M_MENUSTOCK, 0, 0, 1, GDK_KEY_q},	/* 15 */

	{N_("_View"), 0, 0, M_NEWMENU, 0, 0, 1},
#define MENUBAR_OFFSET (17)
	{N_("_Menu Bar"), menu_bar_toggle_cb, 0, M_MENUTOG, MENU_ID_MENUBAR, 0, 1, GDK_KEY_F9},
	{N_("_Topic Bar"), menu_topicbar_toggle, 0, M_MENUTOG, MENU_ID_TOPICBAR, 0, 1},
	{N_("_User List"), menu_userlist_toggle, 0, M_MENUTOG, MENU_ID_USERLIST, 0, 1, GDK_KEY_F7},
	{N_("U_ser List Buttons"), menu_ulbuttons_toggle, 0, M_MENUTOG, MENU_ID_ULBUTTONS, 0, 1},
	{N_("M_ode Buttons"), menu_cmbuttons_toggle, 0, M_MENUTOG, MENU_ID_MODEBUTTONS, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("_Channel Switcher"), 0, 0, M_MENUSUB, 0, 0, 1},	/* 23 */
#define TABS_OFFSET (24)
		{N_("_Tabs"), menu_layout_cb, 0, M_MENURADIO, MENU_ID_LAYOUT_TABS, 0, 1},
		{N_("T_ree"), 0, 0, M_MENURADIO, MENU_ID_LAYOUT_TREE, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},
	{N_("_Network Meters"), 0, 0, M_MENUSUB, 0, 0, 1},	/* 27 */
#define METRE_OFFSET (28)
		{N_("Off"), menu_metres_off, 0, M_MENURADIO, 0, 0, 1},
		{N_("Graph"), menu_metres_graph, 0, M_MENURADIO, 0, 0, 1},
		{N_("Text"), menu_metres_text, 0, M_MENURADIO, 0, 0, 1},
		{N_("Both"), menu_metres_both, 0, M_MENURADIO, 0, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},	/* 32 */
	{ 0, 0, 0, M_SEP, 0, 0, 0 },
	{N_ ("_Fullscreen"), menu_fullscreen_toggle, 0, M_MENUTOG, MENU_ID_FULLSCREEN, 0, 1, GDK_KEY_F11},

	{N_("_Server"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Disconnect"), menu_disconnect, "network-offline", M_MENUSTOCK, MENU_ID_DISCONNECT, 0, 1},
	{N_("_Reconnect"), menu_reconnect, "network-transmit-receive", M_MENUSTOCK, MENU_ID_RECONNECT, 0, 1},
	{N_("_Join a Channel" ELLIPSIS), menu_join, "go-jump", M_MENUSTOCK, MENU_ID_JOIN, 0, 1},
	{N_("Channel _List"), menu_chanlist, "format-justify-fill", M_MENUITEM, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
#define AWAY_OFFSET (41)
	{N_("Marked _Away"), menu_away, 0, M_MENUTOG, MENU_ID_AWAY, 0, 1, GDK_KEY_a},

	{N_("_Usermenu"), 0, 0, M_NEWMENU, MENU_ID_USERMENU, 0, 1},	/* 40 */

	{N_("S_ettings"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Preferences"), menu_settings, "preferences-system", M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("Auto Replace"), menu_rpopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("CTCP Replies"), menu_ctcpguiopen, 0, M_MENUITEM, 0, 0, 1},
	{N_("Dialog Buttons"), menu_dlgbuttons, 0, M_MENUITEM, 0, 0, 1},
	{N_("Keyboard Shortcuts"), menu_keypopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("Text Events"), menu_evtpopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("URL Handlers"), menu_urlhandlers, 0, M_MENUITEM, 0, 0, 1},
	{N_("User Commands"), menu_usercommands, 0, M_MENUITEM, 0, 0, 1},
	{N_("User List Buttons"), menu_ulbuttons, 0, M_MENUITEM, 0, 0, 1},
	{N_("User List Popup"), menu_ulpopup, 0, M_MENUITEM, 0, 0, 1},	/* 52 */

	{N_("_Window"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Ban List"), menu_banlist, 0, M_MENUITEM, 0, 0, 1},
	{N_("Character Chart"), ascii_open, 0, M_MENUITEM, 0, 0, 1},
	{N_("Direct Chat"), menu_dcc_chat_win, 0, M_MENUITEM, 0, 0, 1},
	{N_("File _Transfers"), menu_dcc_win, 0, M_MENUITEM, 0, 0, 1},
	{N_("Friends List"), notify_opengui, 0, M_MENUITEM, 0, 0, 1},
	{N_("Ignore List"), ignore_gui_open, 0, M_MENUITEM, 0, 0, 1},
	{N_("_Plugins and Scripts"), menu_pluginlist, 0, M_MENUITEM, 0, 0, 1},
	{N_("_Raw Log"), menu_rawlog, 0, M_MENUITEM, 0, 0, 1},	/* 61 */
	{N_("_URL Grabber"), url_opengui, 0, M_MENUITEM, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("Reset Marker Line"), menu_resetmarker, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_m},
	{N_("Move to Marker Line"), menu_movetomarker, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_M},
	{N_("_Copy Selection"), menu_copy_selection, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_C},
	{N_("C_lear Text"), menu_flushbuffer, "edit-clear", M_MENUSTOCK, 0, 0, 1},
	{N_("Save Text" ELLIPSIS), menu_savebuffer, "document-save", M_MENUSTOCK, 0, 0, 1},
#define SEARCH_OFFSET (70)
	{N_("Search"), 0, "format-justify-left", M_MENUSUB, 0, 0, 1},
		{N_("Search Text" ELLIPSIS), menu_search, "edit-find", M_MENUSTOCK, 0, 0, 1, GDK_KEY_f},
		{N_("Search Next"   ), menu_search_next, "edit-find", M_MENUSTOCK, 0, 0, 1, GDK_KEY_g},
		{N_("Search Previous"   ), menu_search_prev, "edit-find", M_MENUSTOCK, 0, 0, 1, GDK_KEY_G},
		{0, 0, 0, M_END, 0, 0, 0},

	{N_("_Help"), 0, 0, M_NEWMENU, 0, 0, 1},	/* 74 */
	{N_("_Contents"), menu_docs, "help-browser", M_MENUSTOCK, 0, 0, 1, GDK_KEY_F1},
	{N_("_About"), menu_about, "help-about", M_MENUSTOCK, 0, 0, 1},

	{0, 0, 0, M_END, 0, 0, 0},
};

GtkWidget *
create_icon_menu (char *labeltext, void *icon_name, int is_icon_name)
{
	GtkWidget *item;

	/* GtkImageMenuItem is deprecated in GTK3 and removed in GTK4.
	 * Using regular menu items instead. */
	item = gtk_menu_item_new_with_mnemonic (labeltext);

	return item;
}

/* Override the default GTK2.4 handler, which would make menu
   bindings not work when the menu-bar is hidden. */
static gboolean
menu_canacaccel (GtkWidget *widget, guint signal_id, gpointer user_data)
{
	/* GTK2.2 behaviour */
	return gtk_widget_is_sensitive (widget);
}

/* === STUFF FOR /MENU === */

static GtkMenuItem *
menu_find_item (GtkWidget *menu, char *name)
{
	GList *items, *iter;
	GtkMenuItem *item;
	GtkMenuItem *result = NULL;
	GtkWidget *child;
	const char *labeltext;

	items = gtk_container_get_children (GTK_CONTAINER (menu));

	for (iter = items; iter != NULL; iter = iter->next)
	{
		item = iter->data;
		child = gtk_bin_get_child (GTK_BIN (item));
		if (child)	/* separators arn't labels, skip them */
		{
			labeltext = g_object_get_data (G_OBJECT (item), "name");
			if (!labeltext)
				labeltext = gtk_label_get_text (GTK_LABEL (child));
			if (!menu_streq (labeltext, name, 1))
			{
				result = item;
				break;
			}
		} else if (name == NULL)
		{
			result = item;
			break;
		}
	}

	g_list_free (items);
	return result;
}

static GtkWidget *
menu_find_path (GtkWidget *menu, char *path)
{
	GtkMenuItem *item;
	char *s;
	char name[128];
	int len;

	/* grab the next part of the path */
	s = strchr (path, '/');
	len = s - path;
	if (!s)
		len = strlen (path);
	len = MIN (len, sizeof (name) - 1);
	memcpy (name, path, len);
	name[len] = 0;

	item = menu_find_item (menu, name);
	if (!item)
		return NULL;

	menu = gtk_menu_item_get_submenu (item);
	if (!menu)
		return NULL;

	path += len;
	if (*path == 0)
		return menu;

	return menu_find_path (menu, path + 1);
}

static GtkWidget *
menu_find (GtkWidget *menu, char *path, char *label)
{
	GtkWidget *item = NULL;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
		item = (GtkWidget *)menu_find_item (menu, label);
	return item;
}

static void
menu_foreach_gui (menu_entry *me, void (*callback) (GtkWidget *, menu_entry *, char *))
{
	GSList *list = sess_list;
	int tabdone = FALSE;
	session *sess;

	if (!me->is_main)
		return;	/* not main menu */

	while (list)
	{
		sess = list->data;
		/* do it only once for tab sessions, since they share a GUI */
		if (!sess->gui->is_tab || !tabdone)
		{
			callback (sess->gui->menu, me, NULL);
			if (sess->gui->is_tab)
				tabdone = TRUE;
		}
		list = list->next;
	}
}

static void
menu_update_cb (GtkWidget *menu, menu_entry *me, char *target)
{
	GtkWidget *item;

	item = menu_find (menu, me->path, me->label);
	if (item)
	{
		gtk_widget_set_sensitive (item, me->enable);
		/* must do it without triggering the callback */
		if (GTK_IS_CHECK_MENU_ITEM (item))
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), me->state);
	}
}

/* radio state changed via mouse click */
static void
menu_radio_cb (GtkCheckMenuItem *item, menu_entry *me)
{
	me->state = 0;
	if (gtk_check_menu_item_get_active (item))
		me->state = 1;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui (me, menu_update_cb);

	if (me->state && me->cmd)
		handle_command (current_sess, me->cmd, FALSE);
}

/* toggle state changed via mouse click */
static void
menu_toggle_cb (GtkCheckMenuItem *item, menu_entry *me)
{
	me->state = 0;
	if (gtk_check_menu_item_get_active (item))
		me->state = 1;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui (me, menu_update_cb);

	if (me->state)
		handle_command (current_sess, me->cmd, FALSE);
	else
		handle_command (current_sess, me->ucmd, FALSE);
}

/* === END STUFF FOR /MENU === */

/*
 * =============================================================================
 * GTK4 Plugin Menu System
 * =============================================================================
 * In GTK4, menus use GMenu/GAction instead of GtkMenu/GtkMenuItem.
 *
 * For the main menu bar:
 * - We track plugin menu entries and their corresponding GActions
 * - Actions are created dynamically with unique names based on path+label
 * - The main menu bar is rebuilt when plugin items change
 *
 * For context menus (popups):
 * - Plugin items are added to the GMenu during menu construction
 * - Each popup menu builds its GMenu fresh, including any plugin items
 */

/* Generate a unique action name from path and label */
static char *
menu_plugin_action_name (menu_entry *me)
{
	GString *name = g_string_new ("plugin.");
	const char *p;

	/* Sanitize path - replace invalid chars with underscores */
	for (p = me->path; *p; p++)
	{
		if (g_ascii_isalnum (*p))
			g_string_append_c (name, g_ascii_tolower (*p));
		else
			g_string_append_c (name, '_');
	}

	g_string_append_c (name, '.');

	/* Sanitize label */
	for (p = me->label ? me->label : "item"; *p; p++)
	{
		if (g_ascii_isalnum (*p))
			g_string_append_c (name, g_ascii_tolower (*p));
		else
			g_string_append_c (name, '_');
	}

	return g_string_free (name, FALSE);
}

/* Store the current target for plugin action callbacks */
static char *plugin_menu_target = NULL;

/* Callback for plugin menu item activation */
static void
menu_plugin_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	menu_entry *me = user_data;

	(void)action;
	(void)parameter;

	if (me && me->cmd)
	{
		/* Substitute %s with target if present */
		if (plugin_menu_target && strstr (me->cmd, "%s"))
		{
			char *cmd = g_strdup (me->cmd);
			char *pos = strstr (cmd, "%s");
			if (pos)
			{
				GString *new_cmd = g_string_new ("");
				g_string_append_len (new_cmd, cmd, pos - cmd);
				g_string_append (new_cmd, plugin_menu_target);
				g_string_append (new_cmd, pos + 2);
				handle_command (current_sess, new_cmd->str, FALSE);
				g_string_free (new_cmd, TRUE);
			}
			else
			{
				handle_command (current_sess, me->cmd, FALSE);
			}
			g_free (cmd);
		}
		else
		{
			handle_command (current_sess, me->cmd, FALSE);
		}
	}
}

/* Callback for plugin toggle menu item */
static void
menu_plugin_toggle_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	menu_entry *me = user_data;
	GVariant *state;
	gboolean active;

	(void)parameter;

	state = g_action_get_state (G_ACTION (action));
	active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	me->state = active ? 1 : 0;

	if (me)
	{
		if (active && me->cmd)
			handle_command (current_sess, me->cmd, FALSE);
		else if (!active && me->ucmd)
			handle_command (current_sess, me->ucmd, FALSE);
	}
}

/*
 * Add plugin menu items to a GMenu for context menus.
 * This is called during popup menu construction.
 *
 * menu: The GMenu to add items to
 * action_group: The action group to add actions to
 * root: The root path to match (e.g., "\x05$NICK" for nick menu)
 * target: The target string (e.g., nickname) for %s substitution
 */
void
menu_add_plugin_items_gmenu (GMenu *menu, GSimpleActionGroup *action_group,
                             const char *root, const char *target)
{
	GSList *list;
	menu_entry *me;
	GMenu *section = NULL;
	gboolean has_items = FALSE;

	/* Store target for action callbacks */
	g_free (plugin_menu_target);
	plugin_menu_target = g_strdup (target);

	list = menu_list;
	while (list)
	{
		me = list->data;

		/* Check if this entry matches the root path */
		if (!me->is_main && root && me->path)
		{
			int root_len = (int)(guchar)root[0];
			if (strncmp (me->path, root + 1, root_len) == 0)
			{
				char *action_name;
				char *full_action;
				GSimpleAction *action;

				/* Create section if this is the first item */
				if (!has_items)
				{
					section = g_menu_new ();
					has_items = TRUE;
				}

				/* Create action for this menu entry */
				action_name = menu_plugin_action_name (me);

				if (me->ucmd)
				{
					/* Toggle item */
					action = g_simple_action_new_stateful (action_name, NULL,
						g_variant_new_boolean (me->state));
					g_signal_connect (action, "activate",
						G_CALLBACK (menu_plugin_toggle_cb), me);
				}
				else
				{
					/* Regular item */
					action = g_simple_action_new (action_name, NULL);
					g_signal_connect (action, "activate",
						G_CALLBACK (menu_plugin_action_cb), me);
				}

				g_simple_action_set_enabled (action, me->enable);
				g_action_map_add_action (G_ACTION_MAP (action_group), G_ACTION (action));
				g_object_unref (action);

				/* Add menu item - use "popup." prefix for context menus */
				full_action = g_strdup_printf ("popup.%s", action_name);

				if (me->label == NULL || me->label[0] == '\0')
				{
					/* Separator - GMenu doesn't have separators, so skip */
				}
				else
				{
					g_menu_append (section, me->label, full_action);
				}

				g_free (full_action);
				g_free (action_name);
			}
		}

		list = list->next;
	}

	/* Append the section if we added any items */
	if (has_items && section)
	{
		g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
		g_object_unref (section);
	}
}

/* Compatibility wrapper for GTK3 API */
void
menu_add_plugin_items (GtkWidget *menu, char *root, char *target)
{
	/* In GTK4, this is handled during GMenu construction.
	 * Context menu code should call menu_add_plugin_items_gmenu() instead.
	 * This stub exists for any code that still calls the old API. */
	(void)menu; (void)root; (void)target;
}

static void
menu_add_plugin_mainmenu_items (GtkWidget *menu)
{
	/* Main menu bar items require rebuilding the menu bar.
	 * For now, main menu plugin items are not yet supported in GTK4.
	 * This would require storing the GMenu and rebuilding it on changes. */
	(void)menu;
}

/*
 * fe_menu_add - Add a plugin menu entry (GTK4 version)
 *
 * For context menu items ($NICK, $URL, etc.), the items are added
 * dynamically when the context menu is built via menu_add_plugin_items_gmenu().
 *
 * For main menu items, we would need to rebuild the menu bar, which is
 * complex and rarely used by plugins. For now, main menu items are stored
 * but not displayed until a menu bar rebuild.
 */
char *
fe_menu_add (menu_entry *me)
{
	char *text = NULL;

	/* Main menu items would require rebuilding the menu bar.
	 * Context menu items ($NICK, $URL, etc.) are added when the menu is built. */

	if (me->markup && me->label)
	{
		if (!pango_parse_markup (me->label, -1, 0, NULL, &text, NULL, NULL))
			return NULL;
	}

	/* Return the label with markup stripped */
	return text;
}

void
fe_menu_del (menu_entry *me)
{
	/* For context menus, items are added fresh each time, so no cleanup needed.
	 * For main menu, we would need to remove the action and rebuild. */
	(void)me;
}

void
fe_menu_update (menu_entry *me)
{
	/* For context menus, items get fresh state each time they're built.
	 * For main menu, we would need to update action state. */
	(void)me;
}

/*
 * =============================================================================
 * GTK4 Main Menu Implementation using GMenu and GtkPopoverMenuBar
 * =============================================================================
 */

/* Store the action group globally so we can update toggle states */
static GSimpleActionGroup *main_menu_action_group = NULL;

/* Action callbacks - GSimpleAction signature */
static void
menu_action_server_list (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_open_server_list (NULL, NULL);
}

static void
menu_action_new_server_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_newserver_tab (NULL, NULL);
}

static void
menu_action_new_channel_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_newchannel_tab (NULL, NULL);
}

static void
menu_action_new_server_window (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_newserver_window (NULL, NULL);
}

static void
menu_action_new_channel_window (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_newchannel_window (NULL, NULL);
}

static void
menu_action_load_plugin (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_loadplugin ();
}

static void
menu_action_detach (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_detach (NULL, NULL);
}

static void
menu_action_close (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_close (NULL, NULL);
}

static void
menu_action_quit (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_quit (NULL, NULL);
}

/* View menu toggle actions */
static void
menu_action_toggle_menubar (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	/* Update preference and apply to all sessions */
	prefs.hex_gui_hide_menu = !active;
	menu_setting_foreach (menu_showhide_cb, -1, 0);  /* -1 skips GTK3 widget state update */

	if (prefs.hex_gui_hide_menu)
		fe_message (_("The Menubar is now hidden. You can show it again"
						  " by pressing Control+F9 or right-clicking in a blank part of"
						  " the main text area."), FE_MSG_INFO);
}

static void
menu_action_toggle_topicbar (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	prefs.hex_gui_topicbar = active;
	menu_setting_foreach (menu_topic_showhide_cb, MENU_ID_TOPICBAR, active);
}

static void
menu_action_toggle_userlist (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	prefs.hex_gui_ulist_hide = !active;
	menu_setting_foreach (menu_userlist_showhide_cb, MENU_ID_USERLIST, active);
}

static void
menu_action_toggle_ulbuttons (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	prefs.hex_gui_ulist_buttons = active;
	menu_setting_foreach (menu_ulbuttons_showhide_cb, MENU_ID_ULBUTTONS, active);
}

static void
menu_action_toggle_modebuttons (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	prefs.hex_gui_mode_buttons = active;
	menu_setting_foreach (menu_cmbuttons_showhide_cb, MENU_ID_MODEBUTTONS, active);
}

static void
menu_action_toggle_fullscreen (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	if (active)
		gtk_window_fullscreen (GTK_WINDOW (parent_window));
	else
		gtk_window_unfullscreen (GTK_WINDOW (parent_window));
}

/* Channel Switcher radio actions */
static void
menu_action_layout (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	const char *layout = g_variant_get_string (parameter, NULL);
	g_simple_action_set_state (action, g_variant_ref (parameter));

	if (g_strcmp0 (layout, "tabs") == 0)
		prefs.hex_gui_tab_layout = 0;
	else
		prefs.hex_gui_tab_layout = 2;

	menu_change_layout ();
}

/* Network Meters radio actions */
static void
menu_action_metres (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	const char *mode = g_variant_get_string (parameter, NULL);
	g_simple_action_set_state (action, g_variant_ref (parameter));

	if (g_strcmp0 (mode, "off") == 0)
		prefs.hex_gui_lagometer = 0;
	else if (g_strcmp0 (mode, "graph") == 0)
		prefs.hex_gui_lagometer = 1;
	else if (g_strcmp0 (mode, "text") == 0)
		prefs.hex_gui_lagometer = 2;
	else /* both */
		prefs.hex_gui_lagometer = 3;

	menu_setting_foreach (menu_apply_metres_cb, -1, 0);
}

/* Server menu actions */
static void
menu_action_disconnect (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_disconnect (NULL, NULL);
}

static void
menu_action_reconnect (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_reconnect (NULL, NULL);
}

static void
menu_action_join (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_join (NULL, NULL);
}

static void
menu_action_chanlist (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_chanlist (NULL, NULL);
}

static void
menu_action_toggle_away (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GVariant *state = g_action_get_state (G_ACTION (action));
	gboolean active = !g_variant_get_boolean (state);
	g_simple_action_set_state (action, g_variant_new_boolean (active));
	g_variant_unref (state);

	handle_command (current_sess, active ? "away" : "back", FALSE);
}

/* Settings menu actions */
static void
menu_action_preferences (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_settings (NULL, NULL);
}

static void
menu_action_auto_replace (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_rpopup ();
}

static void
menu_action_ctcp_replies (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_ctcpguiopen ();
}

static void
menu_action_dialog_buttons (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_dlgbuttons ();
}

static void
menu_action_keyboard_shortcuts (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_keypopup ();
}

static void
menu_action_text_events (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_evtpopup ();
}

static void
menu_action_url_handlers (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_urlhandlers ();
}

static void
menu_action_user_commands (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_usercommands ();
}

static void
menu_action_userlist_buttons (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_ulbuttons ();
}

static void
menu_action_userlist_popup (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_ulpopup ();
}

/* Window menu actions */
static void
menu_action_banlist (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_banlist (NULL, NULL);
}

static void
menu_action_ascii (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	ascii_open ();
}

static void
menu_action_dcc_chat (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_dcc_chat_win (NULL, NULL);
}

static void
menu_action_dcc_transfers (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_dcc_win (NULL, NULL);
}

static void
menu_action_friends_list (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	notify_opengui ();
}

static void
menu_action_ignore_list (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	ignore_gui_open ();
}

static void
menu_action_plugins (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_pluginlist ();
}

static void
menu_action_rawlog (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_rawlog (NULL, NULL);
}

static void
menu_action_url_grabber (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	url_opengui ();
}

static void
menu_action_reset_marker (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_resetmarker (NULL, NULL);
}

static void
menu_action_move_to_marker (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_movetomarker (NULL, NULL);
}

static void
menu_action_copy_selection (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_copy_selection (NULL, NULL);
}

static void
menu_action_clear_text (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_flushbuffer (NULL, NULL);
}

static void
menu_action_save_text (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_savebuffer (NULL, NULL);
}

static void
menu_action_search (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_search ();
}

static void
menu_action_search_next (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_search_next (NULL);
}

static void
menu_action_search_prev (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_search_prev (NULL);
}

/* Help menu actions */
static void
menu_action_contents (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_docs (NULL, NULL);
}

static void
menu_action_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	(void)action; (void)parameter; (void)user_data;
	menu_about (NULL, NULL);
}

/* Build the GMenu model for the main menu bar */
static GMenu *
menu_build_gmenu (int away, int toplevel)
{
	GMenu *menubar;
	GMenu *menu, *section, *submenu;

	(void)away;  /* Initial state is set in actions, not menu model */

	menubar = g_menu_new ();

	/* === HexChat Menu === */
	menu = g_menu_new ();
	g_menu_append (menu, _("Network Li_st"), "menu.server-list");

	section = g_menu_new ();
	submenu = g_menu_new ();
	g_menu_append (submenu, _("Server Tab"), "menu.new-server-tab");
	g_menu_append (submenu, _("Channel Tab"), "menu.new-channel-tab");
	g_menu_append (submenu, _("Server Window"), "menu.new-server-window");
	g_menu_append (submenu, _("Channel Window"), "menu.new-channel-window");
	g_menu_append_submenu (section, _("_New"), G_MENU_MODEL (submenu));
	g_object_unref (submenu);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, _("_Load Plugin or Script" ELLIPSIS), "menu.load-plugin");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, toplevel ? _("_Attach") : _("_Detach"), "menu.detach");
	g_menu_append (section, _("_Close"), "menu.close");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, _("_Quit"), "menu.quit");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	g_menu_append_submenu (menubar, _("He_xChat"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	/* === View Menu === */
	menu = g_menu_new ();
	section = g_menu_new ();
	g_menu_append (section, _("_Menu Bar"), "menu.toggle-menubar");
	g_menu_append (section, _("_Topic Bar"), "menu.toggle-topicbar");
	g_menu_append (section, _("_User List"), "menu.toggle-userlist");
	g_menu_append (section, _("U_ser List Buttons"), "menu.toggle-ulbuttons");
	g_menu_append (section, _("M_ode Buttons"), "menu.toggle-modebuttons");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	submenu = g_menu_new ();
	{
		GMenuItem *item;
		item = g_menu_item_new (_("_Tabs"), NULL);
		g_menu_item_set_action_and_target (item, "menu.layout", "s", "tabs");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
		item = g_menu_item_new (_("T_ree"), NULL);
		g_menu_item_set_action_and_target (item, "menu.layout", "s", "tree");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
	}
	g_menu_append_submenu (section, _("_Channel Switcher"), G_MENU_MODEL (submenu));
	g_object_unref (submenu);

	submenu = g_menu_new ();
	{
		GMenuItem *item;
		item = g_menu_item_new (_("Off"), NULL);
		g_menu_item_set_action_and_target (item, "menu.metres", "s", "off");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
		item = g_menu_item_new (_("Graph"), NULL);
		g_menu_item_set_action_and_target (item, "menu.metres", "s", "graph");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
		item = g_menu_item_new (_("Text"), NULL);
		g_menu_item_set_action_and_target (item, "menu.metres", "s", "text");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
		item = g_menu_item_new (_("Both"), NULL);
		g_menu_item_set_action_and_target (item, "menu.metres", "s", "both");
		g_menu_append_item (submenu, item);
		g_object_unref (item);
	}
	g_menu_append_submenu (section, _("_Network Meters"), G_MENU_MODEL (submenu));
	g_object_unref (submenu);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, _("_Fullscreen"), "menu.toggle-fullscreen");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	g_menu_append_submenu (menubar, _("_View"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	/* === Server Menu === */
	menu = g_menu_new ();
	g_menu_append (menu, _("_Disconnect"), "menu.disconnect");
	g_menu_append (menu, _("_Reconnect"), "menu.reconnect");
	g_menu_append (menu, _("_Join a Channel" ELLIPSIS), "menu.join");
	g_menu_append (menu, _("Channel _List"), "menu.chanlist");

	section = g_menu_new ();
	g_menu_append (section, _("Marked _Away"), "menu.toggle-away");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	g_menu_append_submenu (menubar, _("_Server"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	/* === Usermenu (only if enabled) === */
	if (prefs.hex_gui_usermenu)
	{
		menu = g_menu_new ();
		/* TODO: Populate user menu items */
		g_menu_append_submenu (menubar, _("_Usermenu"), G_MENU_MODEL (menu));
		g_object_unref (menu);
	}

	/* === Settings Menu === */
	menu = g_menu_new ();
	g_menu_append (menu, _("_Preferences"), "menu.preferences");

	section = g_menu_new ();
	g_menu_append (section, _("Auto Replace"), "menu.auto-replace");
	g_menu_append (section, _("CTCP Replies"), "menu.ctcp-replies");
	g_menu_append (section, _("Dialog Buttons"), "menu.dialog-buttons");
	g_menu_append (section, _("Keyboard Shortcuts"), "menu.keyboard-shortcuts");
	g_menu_append (section, _("Text Events"), "menu.text-events");
	g_menu_append (section, _("URL Handlers"), "menu.url-handlers");
	g_menu_append (section, _("User Commands"), "menu.user-commands");
	g_menu_append (section, _("User List Buttons"), "menu.userlist-buttons");
	g_menu_append (section, _("User List Popup"), "menu.userlist-popup");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	g_menu_append_submenu (menubar, _("S_ettings"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	/* === Window Menu === */
	menu = g_menu_new ();
	g_menu_append (menu, _("_Ban List"), "menu.banlist");
	g_menu_append (menu, _("Character Chart"), "menu.ascii");
	g_menu_append (menu, _("Direct Chat"), "menu.dcc-chat");
	g_menu_append (menu, _("File _Transfers"), "menu.dcc-transfers");
	g_menu_append (menu, _("Friends List"), "menu.friends-list");
	g_menu_append (menu, _("Ignore List"), "menu.ignore-list");
	g_menu_append (menu, _("_Plugins and Scripts"), "menu.plugins");
	g_menu_append (menu, _("_Raw Log"), "menu.rawlog");
	g_menu_append (menu, _("_URL Grabber"), "menu.url-grabber");

	section = g_menu_new ();
	g_menu_append (section, _("Reset Marker Line"), "menu.reset-marker");
	g_menu_append (section, _("Move to Marker Line"), "menu.move-to-marker");
	g_menu_append (section, _("_Copy Selection"), "menu.copy-selection");
	g_menu_append (section, _("C_lear Text"), "menu.clear-text");
	g_menu_append (section, _("Save Text" ELLIPSIS), "menu.save-text");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	submenu = g_menu_new ();
	g_menu_append (submenu, _("Search Text" ELLIPSIS), "menu.search");
	g_menu_append (submenu, _("Search Next"), "menu.search-next");
	g_menu_append (submenu, _("Search Previous"), "menu.search-prev");
	g_menu_append_submenu (section, _("Search"), G_MENU_MODEL (submenu));
	g_object_unref (submenu);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	g_menu_append_submenu (menubar, _("_Window"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	/* === Help Menu === */
	menu = g_menu_new ();
	g_menu_append (menu, _("_Contents"), "menu.contents");
	g_menu_append (menu, _("_About"), "menu.about");
	g_menu_append_submenu (menubar, _("_Help"), G_MENU_MODEL (menu));
	g_object_unref (menu);

	return menubar;
}

/* Create the action group for the main menu */
static GSimpleActionGroup *
menu_create_action_group (int away, int toplevel)
{
	GSimpleActionGroup *group;
	const char *layout_state;
	const char *metres_state;

	/* Action entries for simple (non-stateful) actions */
	static const GActionEntry simple_actions[] = {
		{ "server-list", menu_action_server_list, NULL, NULL, NULL },
		{ "new-server-tab", menu_action_new_server_tab, NULL, NULL, NULL },
		{ "new-channel-tab", menu_action_new_channel_tab, NULL, NULL, NULL },
		{ "new-server-window", menu_action_new_server_window, NULL, NULL, NULL },
		{ "new-channel-window", menu_action_new_channel_window, NULL, NULL, NULL },
		{ "load-plugin", menu_action_load_plugin, NULL, NULL, NULL },
		{ "detach", menu_action_detach, NULL, NULL, NULL },
		{ "close", menu_action_close, NULL, NULL, NULL },
		{ "quit", menu_action_quit, NULL, NULL, NULL },
		{ "disconnect", menu_action_disconnect, NULL, NULL, NULL },
		{ "reconnect", menu_action_reconnect, NULL, NULL, NULL },
		{ "join", menu_action_join, NULL, NULL, NULL },
		{ "chanlist", menu_action_chanlist, NULL, NULL, NULL },
		{ "preferences", menu_action_preferences, NULL, NULL, NULL },
		{ "auto-replace", menu_action_auto_replace, NULL, NULL, NULL },
		{ "ctcp-replies", menu_action_ctcp_replies, NULL, NULL, NULL },
		{ "dialog-buttons", menu_action_dialog_buttons, NULL, NULL, NULL },
		{ "keyboard-shortcuts", menu_action_keyboard_shortcuts, NULL, NULL, NULL },
		{ "text-events", menu_action_text_events, NULL, NULL, NULL },
		{ "url-handlers", menu_action_url_handlers, NULL, NULL, NULL },
		{ "user-commands", menu_action_user_commands, NULL, NULL, NULL },
		{ "userlist-buttons", menu_action_userlist_buttons, NULL, NULL, NULL },
		{ "userlist-popup", menu_action_userlist_popup, NULL, NULL, NULL },
		{ "banlist", menu_action_banlist, NULL, NULL, NULL },
		{ "ascii", menu_action_ascii, NULL, NULL, NULL },
		{ "dcc-chat", menu_action_dcc_chat, NULL, NULL, NULL },
		{ "dcc-transfers", menu_action_dcc_transfers, NULL, NULL, NULL },
		{ "friends-list", menu_action_friends_list, NULL, NULL, NULL },
		{ "ignore-list", menu_action_ignore_list, NULL, NULL, NULL },
		{ "plugins", menu_action_plugins, NULL, NULL, NULL },
		{ "rawlog", menu_action_rawlog, NULL, NULL, NULL },
		{ "url-grabber", menu_action_url_grabber, NULL, NULL, NULL },
		{ "reset-marker", menu_action_reset_marker, NULL, NULL, NULL },
		{ "move-to-marker", menu_action_move_to_marker, NULL, NULL, NULL },
		{ "copy-selection", menu_action_copy_selection, NULL, NULL, NULL },
		{ "clear-text", menu_action_clear_text, NULL, NULL, NULL },
		{ "save-text", menu_action_save_text, NULL, NULL, NULL },
		{ "search", menu_action_search, NULL, NULL, NULL },
		{ "search-next", menu_action_search_next, NULL, NULL, NULL },
		{ "search-prev", menu_action_search_prev, NULL, NULL, NULL },
		{ "contents", menu_action_contents, NULL, NULL, NULL },
		{ "about", menu_action_about, NULL, NULL, NULL },
	};

	group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (group), simple_actions,
									 G_N_ELEMENTS (simple_actions), NULL);

	/* Add stateful toggle actions */
	{
		GSimpleAction *action;

		action = g_simple_action_new_stateful ("toggle-menubar", NULL,
			g_variant_new_boolean (!prefs.hex_gui_hide_menu));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_menubar), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-topicbar", NULL,
			g_variant_new_boolean (prefs.hex_gui_topicbar));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_topicbar), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-userlist", NULL,
			g_variant_new_boolean (!prefs.hex_gui_ulist_hide));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_userlist), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-ulbuttons", NULL,
			g_variant_new_boolean (prefs.hex_gui_ulist_buttons));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_ulbuttons), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-modebuttons", NULL,
			g_variant_new_boolean (prefs.hex_gui_mode_buttons));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_modebuttons), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-fullscreen", NULL,
			g_variant_new_boolean (FALSE));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_fullscreen), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		action = g_simple_action_new_stateful ("toggle-away", NULL,
			g_variant_new_boolean (away));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_toggle_away), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);
	}

	/* Add radio actions for layout and metres */
	{
		GSimpleAction *action;

		layout_state = (prefs.hex_gui_tab_layout == 0) ? "tabs" : "tree";
		action = g_simple_action_new_stateful ("layout", G_VARIANT_TYPE_STRING,
			g_variant_new_string (layout_state));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_layout), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);

		switch (prefs.hex_gui_lagometer)
		{
		case 0: metres_state = "off"; break;
		case 1: metres_state = "graph"; break;
		case 2: metres_state = "text"; break;
		default: metres_state = "both"; break;
		}
		action = g_simple_action_new_stateful ("metres", G_VARIANT_TYPE_STRING,
			g_variant_new_string (metres_state));
		g_signal_connect (action, "activate", G_CALLBACK (menu_action_metres), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);
	}

	return group;
}

GtkWidget *
menu_create_main (void *accel_group, int bar, int away, int toplevel,
						GtkWidget **menu_widgets)
{
	GtkWidget *menu_bar;
	GMenu *gmenu;
	GSimpleActionGroup *action_group;

	(void)accel_group;  /* Not used in GTK4 - accelerators handled differently */
	(void)bar;          /* Always create menu bar style in GTK4 */

	/* Build the menu model */
	gmenu = menu_build_gmenu (away, toplevel);

	/* Create the menu bar widget */
	menu_bar = gtk_popover_menu_bar_new_from_model (G_MENU_MODEL (gmenu));
	g_object_unref (gmenu);

	/* Create and attach the action group */
	action_group = menu_create_action_group (away, toplevel);
	gtk_widget_insert_action_group (menu_bar, "menu", G_ACTION_GROUP (action_group));

	/* Store action group for state updates */
	g_object_set_data_full (G_OBJECT (menu_bar), "action-group", action_group, g_object_unref);
	main_menu_action_group = action_group;

	/* Store menu_widgets for compatibility - in GTK4 we use action states instead */
	if (menu_widgets)
	{
		/* Clear menu_widgets array - we don't use widget pointers in GTK4 */
		memset (menu_widgets, 0, sizeof (GtkWidget *) * (MENU_ID_HEXCHAT + 1));
	}

	return menu_bar;
}

/* Update away state in the menu */
void
menu_set_away (session_gui *gui, int away)
{
	GSimpleActionGroup *group;
	GAction *action;

	if (!gui || !gui->menu)
		return;

	group = g_object_get_data (G_OBJECT (gui->menu), "action-group");
	if (!group)
		return;

	action = g_action_map_lookup_action (G_ACTION_MAP (group), "toggle-away");
	if (action)
		g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (away));
}

void
menu_set_fullscreen (session_gui *gui, int full)
{
	GSimpleActionGroup *group;
	GAction *action;

	if (!gui || !gui->menu)
		return;

	group = g_object_get_data (G_OBJECT (gui->menu), "action-group");
	if (!group)
		return;

	action = g_action_map_lookup_action (G_ACTION_MAP (group), "toggle-fullscreen");
	if (action)
		g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (full));
}

/*
 * Set up keyboard shortcuts for GTK4
 * GTK4 doesn't use GtkAccelGroup, so we need to use GtkShortcutController
 * to connect keyboard shortcuts to menu actions.
 */
void
menu_add_shortcuts (GtkWidget *window, GtkWidget *menu_bar)
{
	GtkEventController *controller;
	GtkShortcut *shortcut;
	GtkShortcutTrigger *trigger;
	GtkShortcutAction *action;
	GSimpleActionGroup *action_group;

	/* Get the action group from the menu bar and also insert it on the window
	 * so that GtkNamedAction can find it when shortcuts are triggered */
	action_group = g_object_get_data (G_OBJECT (menu_bar), "action-group");
	if (action_group)
		gtk_widget_insert_action_group (window, "menu", G_ACTION_GROUP (action_group));

	controller = gtk_shortcut_controller_new ();
	gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller),
	                                   GTK_SHORTCUT_SCOPE_GLOBAL);
	gtk_widget_add_controller (window, controller);

	/* Search: Ctrl+F */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>f");
	action = gtk_named_action_new ("menu.search");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Search Next: Ctrl+G */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>g");
	action = gtk_named_action_new ("menu.search-next");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Search Previous: Ctrl+Shift+G */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary><Shift>g");
	action = gtk_named_action_new ("menu.search-prev");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Help Contents: F1 */
	trigger = gtk_shortcut_trigger_parse_string ("F1");
	action = gtk_named_action_new ("menu.help-contents");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Copy Selection: Ctrl+Shift+C */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary><Shift>c");
	action = gtk_named_action_new ("menu.copy-selection");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Reset Marker Line: Ctrl+M */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>m");
	action = gtk_named_action_new ("menu.reset-marker");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Move to Marker Line: Ctrl+Shift+M */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary><Shift>m");
	action = gtk_named_action_new ("menu.move-to-marker");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Disconnect: Ctrl+D - only if not in emacs key mode */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>d");
	action = gtk_named_action_new ("menu.disconnect");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Reconnect: Ctrl+R */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>r");
	action = gtk_named_action_new ("menu.reconnect");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	/* Close Tab: Ctrl+W or Ctrl+Shift+W depending on key theme */
	trigger = gtk_shortcut_trigger_parse_string ("<Primary>w");
	action = gtk_named_action_new ("menu.close-tab");
	shortcut = gtk_shortcut_new (trigger, action);
	gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);

	(void)menu_bar; /* May be used for action group lookup if needed */
}