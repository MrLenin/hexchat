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
#include <string.h>
#include <stdlib.h>

#include "fe-gtk.h"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.h"
#include "../common/util.h"
#include "../common/userlist.h"
#include "../common/modes.h"
#include "../common/text.h"
#include "../common/notify.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "palette.h"
#include "maingui.h"
#include "menu.h"
#include "pixmaps.h"
#include "userlistgui.h"
#include "fkeys.h"

enum
{
	COL_PIX=0,		/* GdkPixbuf * */
	COL_NICK=1,		/* char * */
	COL_HOST=2,		/* char * */
	COL_USER=3,		/* struct User * */
	COL_GDKCOLOR=4	/* GdkRGBA * - foreground color */
};


GdkPixbuf *
get_user_icon (server *serv, struct User *user)
{
	char *pre;
	int level;

	if (!user)
		return NULL;

	/* these ones are hardcoded */
	switch (user->prefix[0])
	{
		case 0: return NULL;
		case '+': return pix_ulist_voice;
		case '%': return pix_ulist_halfop;
		case '@': return pix_ulist_op;
	}

	/* find out how many levels above Op this user is */
	pre = strchr (serv->nick_prefixes, '@');
	if (pre && pre != serv->nick_prefixes)
	{
		pre--;
		level = 0;
		while (1)
		{
			if (pre[0] == user->prefix[0])
			{
				switch (level)
				{
					case 0: return pix_ulist_owner;		/* 1 level above op */
					case 1: return pix_ulist_founder;	/* 2 levels above op */
					case 2: return pix_ulist_netop;		/* 3 levels above op */
				}
				break;	/* 4+, no icons */
			}
			level++;
			if (pre == serv->nick_prefixes)
				break;
			pre--;
		}
	}

	return NULL;
}

void
fe_userlist_numbers (session *sess)
{
	char tbuf[256];

	if (sess == current_tab || !sess->gui->is_tab)
	{
		if (sess->total)
		{
			g_snprintf (tbuf, sizeof (tbuf), _("%d ops, %d total"), sess->ops, sess->total);
			tbuf[sizeof (tbuf) - 1] = 0;
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), tbuf);
		} else
		{
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), NULL);
		}

		if (sess->type == SESS_CHANNEL && prefs.hex_gui_win_ucount)
			fe_set_title (sess);
	}
}

static void
scroll_to_iter (GtkTreeIter *iter, GtkTreeView *treeview, GtkTreeModel *model)
{
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);
	if (path)
	{
		gtk_tree_view_scroll_to_cell (treeview, path, NULL, TRUE, 0.5, 0.5);
		gtk_tree_path_free (path);
	}
}

/* select a row in the userlist by nick-name */

void
userlist_select (session *sess, char *name)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (sess->server->p_cmp (row_user->nick, name) == 0)
			{
				if (gtk_tree_selection_iter_is_selected (selection, &iter))
					gtk_tree_selection_unselect_iter (selection, &iter);
				else
					gtk_tree_selection_select_iter (selection, &iter);

				/* and make sure it's visible */
				scroll_to_iter (&iter, treeview, model);
				return;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}

char **
userlist_selection_list (GtkWidget *widget, int *num_ret)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = (GtkTreeView *) widget;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	struct User *user;
	int i, num_sel;
	char **nicks;

	*num_ret = 0;
	/* first, count the number of selections */
	num_sel = 0;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				num_sel++;
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	if (num_sel < 1)
		return NULL;

	nicks = g_new (char *, num_sel + 1);

	i = 0;
	gtk_tree_model_get_iter_first (model, &iter);
	do
	{
		if (gtk_tree_selection_iter_is_selected (selection, &iter))
		{
			gtk_tree_model_get (model, &iter, COL_USER, &user, -1);
			nicks[i] = g_strdup (user->nick);
			i++;
			nicks[i] = NULL;
		}
	}
	while (gtk_tree_model_iter_next (model, &iter));

	*num_ret = i;
	return nicks;
}

void
fe_userlist_set_selected (struct session *sess)
{
	GtkListStore *store = sess->res->user_model;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sess->gui->user_tree));
	GtkTreeIter iter;
	struct User *user;

	/* if it's not front-most tab it doesn't own the GtkTreeView! */
	if (store != (GtkListStore*) gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree)))
		return;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, COL_USER, &user, -1);

			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				user->selected = 1;
			else
				user->selected = 0;
				
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));
	}
}

static GtkTreeIter *
find_row (GtkTreeView *treeview, GtkTreeModel *model, struct User *user,
			 int *selected)
{
	static GtkTreeIter iter;
	struct User *row_user;

	*selected = FALSE;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (row_user == user)
			{
				if (gtk_tree_view_get_model (treeview) == model)
				{
					if (gtk_tree_selection_iter_is_selected (gtk_tree_view_get_selection (treeview), &iter))
						*selected = TRUE;
				}
				return &iter;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	return NULL;
}

void
userlist_set_value (GtkWidget *treeview, gfloat val)
{
	gtk_adjustment_set_value (
			hc_tree_view_get_vadjustment (treeview), val);
}

gfloat
userlist_get_value (GtkWidget *treeview)
{
	return gtk_adjustment_get_value (hc_tree_view_get_vadjustment (treeview));
}

int
fe_userlist_remove (session *sess, struct User *user)
{
	GtkTreeIter *iter;
/*	GtkAdjustment *adj;
	gfloat val, end;*/
	int sel;

	iter = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
						  GTK_TREE_MODEL(sess->res->user_model), user, &sel);
	if (!iter)
		return 0;

/*	adj = gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (sess->gui->user_tree));
	val = adj->value;*/

	gtk_list_store_remove (sess->res->user_model, iter);

	/* is it the front-most tab? */
/*	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == sess->res->user_model)
	{
		end = adj->upper - adj->lower - adj->page_size;
		if (val > end)
			val = end;
		gtk_adjustment_set_value (adj, val);
	}*/

	return sel;
}

void
fe_userlist_rehash (session *sess, struct User *user)
{
	GtkTreeIter *iter;
	int sel;
	int nick_color = 0;

	iter = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
						  GTK_TREE_MODEL(sess->res->user_model), user, &sel);
	if (!iter)
		return;

	if (prefs.hex_away_track && user->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(user->nick);

	gtk_list_store_set (GTK_LIST_STORE (sess->res->user_model), iter,
							  COL_HOST, user->hostname,
							  COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
							  -1);
}

void
fe_userlist_insert (session *sess, struct User *newuser, gboolean sel)
{
	GtkTreeModel *model = GTK_TREE_MODEL(sess->res->user_model);
	GdkPixbuf *pix = get_user_icon (sess->server, newuser);
	GtkTreeIter iter;
	char *nick;
	int nick_color = 0;

	if (prefs.hex_away_track && newuser->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(newuser->nick);

	nick = newuser->nick;
	if (!prefs.hex_gui_ulist_icons)
	{
		nick = g_malloc (strlen (newuser->nick) + 2);
		nick[0] = newuser->prefix[0];
		if (nick[0] == '\0' || nick[0] == ' ')
			strcpy (nick, newuser->nick);
		else
			strcpy (nick + 1, newuser->nick);
		pix = NULL;
	}

	gtk_list_store_insert_with_values (GTK_LIST_STORE (model), &iter, 0,
									COL_PIX, pix,
									COL_NICK, nick,
									COL_HOST, newuser->hostname,
									COL_USER, newuser,
									COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
								  -1);

	if (!prefs.hex_gui_ulist_icons)
	{
		g_free (nick);
	}

	/* is it me? */
	if (newuser->me && sess->gui->nick_box)
	{
		if (!sess->gui->is_tab || sess == current_tab)
			mg_set_access_icon (sess->gui, pix, sess->server->is_away);
	}

	/* is it the front-most tab? */
	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == model)
	{
		if (sel)
			gtk_tree_selection_select_iter (gtk_tree_view_get_selection
										(GTK_TREE_VIEW (sess->gui->user_tree)), &iter);
	}
}

void
fe_userlist_clear (session *sess)
{
	gtk_list_store_clear (sess->res->user_model);
}

#if HC_GTK4
/* GTK4: File drop handler for userlist - drops file on the user under cursor */
static gboolean
userlist_file_drop_cb (GtkDropTarget *target, const GValue *value,
                       double x, double y, gpointer user_data)
{
	GtkWidget *treeview = user_data;
	GFile *file;
	char *uri;
	struct User *user;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeIter iter;

	(void)target;

	if (!G_VALUE_HOLDS (value, G_TYPE_FILE))
		return FALSE;

	file = g_value_get_object (value);
	if (!file)
		return FALSE;

	/* Find user under drop position */
	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview), (int)x, (int)y,
	                                    &path, NULL, NULL, NULL))
		return FALSE;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	if (!gtk_tree_model_get_iter (model, &iter, path))
	{
		gtk_tree_path_free (path);
		return FALSE;
	}
	gtk_tree_path_free (path);

	gtk_tree_model_get (model, &iter, COL_USER, &user, -1);

	uri = g_file_get_uri (file);
	if (uri && user)
	{
		mg_dnd_drop_file (current_sess, user->nick, uri);
		g_free (uri);
	}

	return TRUE;
}

/* GTK4: Highlight user under cursor during drag */
static GdkDragAction
userlist_drop_motion_cb (GtkDropTarget *target, double x, double y, gpointer user_data)
{
	GtkWidget *treeview = user_data;
	GtkTreePath *path;
	GtkTreeSelection *sel;

	(void)target;

	if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview), (int)x, (int)y,
	                                   &path, NULL, NULL, NULL))
	{
		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
		gtk_tree_path_free (path);
	}

	return GDK_ACTION_COPY;
}

/* GTK4: Clear selection when drag leaves */
static void
userlist_drop_leave_cb (GtkDropTarget *target, gpointer user_data)
{
	GtkWidget *treeview = user_data;

	(void)target;
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)));
}

#else /* GTK3 */

static void
userlist_dnd_drop (GtkTreeView *widget, GdkDragContext *context,
						 gint x, gint y, GtkSelectionData *selection_data,
						 guint info, guint ttime, gpointer userdata)
{
	struct User *user;
	gchar *data;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
		return;

	model = gtk_tree_view_get_model (widget);
	if (!gtk_tree_model_get_iter (model, &iter, path))
		return;
	gtk_tree_model_get (model, &iter, COL_USER, &user, -1);

	data = (char *)gtk_selection_data_get_data (selection_data);

	if (data)
		mg_dnd_drop_file (current_sess, user->nick, data);
}

static gboolean
userlist_dnd_motion (GtkTreeView *widget, GdkDragContext *context, gint x,
							gint y, guint ttime, gpointer tree)
{
	GtkTreePath *path;
	GtkTreeSelection *sel;

	if (!tree)
		return FALSE;

	if (gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
	{
		sel = gtk_tree_view_get_selection (widget);
		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
	}

	return FALSE;
}

static gboolean
userlist_dnd_leave (GtkTreeView *widget, GdkDragContext *context, guint ttime)
{
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (widget));
	return TRUE;
}

#endif /* HC_GTK4 */

static int
userlist_alpha_cmp (GtkTreeModel *model, GtkTreeIter *iter_a, GtkTreeIter *iter_b, gpointer userdata)
{
	struct User *user_a, *user_b;

	gtk_tree_model_get (model, iter_a, COL_USER, &user_a, -1);
	gtk_tree_model_get (model, iter_b, COL_USER, &user_b, -1);

	return nick_cmp_alpha (user_a, user_b, ((session*)userdata)->server);
}

static int
userlist_ops_cmp (GtkTreeModel *model, GtkTreeIter *iter_a, GtkTreeIter *iter_b, gpointer userdata)
{
	struct User *user_a, *user_b;

	gtk_tree_model_get (model, iter_a, COL_USER, &user_a, -1);
	gtk_tree_model_get (model, iter_b, COL_USER, &user_b, -1);

	return nick_cmp_az_ops (((session*)userdata)->server, user_a, user_b);
}

GtkListStore *
userlist_create_model (session *sess)
{
	GtkListStore *store;
	GtkTreeIterCompareFunc cmp_func;
	GtkSortType sort_type;

	store = gtk_list_store_new (5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
										G_TYPE_POINTER, GDK_TYPE_RGBA);

	switch (prefs.hex_gui_ulist_sort)
	{
	case 0:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 1:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 2:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	case 3:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	default:
		/* No sorting */
		gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE(store), NULL, NULL, NULL);
		return store;
	}

	gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE(store), cmp_func, sess, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store),
						GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, sort_type);

	return store;
}

static void
userlist_add_columns (GtkTreeView * treeview)
{
	GtkCellRenderer *renderer;

	/* icon column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
																"pixbuf", COL_PIX, NULL);

	/* nick column */
	renderer = gtk_cell_renderer_text_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
																"text", COL_NICK,
																"foreground-rgba", COL_GDKCOLOR, NULL);

	if (prefs.hex_gui_ulist_show_hosts)
	{
		/* hostname column */
		renderer = gtk_cell_renderer_text_new ();
		if (prefs.hex_gui_compact)
			g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
		gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
		gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																	-1, NULL, renderer,
																	"text", COL_HOST, NULL);
	}
}

/*
 * Click handler for userlist
 * GTK3: Uses GdkEventButton from "button_press_event" signal
 * GTK4: Uses GtkGestureClick with different signature
 */
#if HC_GTK4
static void
userlist_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	char **nicks;
	int i;
	GtkTreeSelection *sel;
	GtkTreePath *path;
	int button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
	GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));

	if (!(state & GDK_CONTROL_MASK) &&
		n_press == 2 && prefs.hex_gui_ulist_doubleclick[0])
	{
		nicks = userlist_selection_list (widget, &i);
		if (nicks)
		{
			nick_command_parse (current_sess, prefs.hex_gui_ulist_doubleclick, nicks[0],
									  nicks[0]);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
		}
		return;
	}

	if (button == 3)
	{
		/* do we have a multi-selection? */
		nicks = userlist_selection_list (widget, &i);
		if (nicks && i > 1)
		{
			menu_nickmenu (current_sess, NULL, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
			return;
		}
		if (nicks)
		{
			g_free (nicks[0]);
			g_free (nicks);
		}

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
			 (int)x, (int)y, &path, 0, 0, 0))
		{
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
			gtk_tree_path_free (path);
			nicks = userlist_selection_list (widget, &i);
			if (nicks)
			{
				menu_nickmenu (current_sess, NULL, nicks[0], i);
				while (i)
				{
					i--;
					g_free (nicks[i]);
				}
				g_free (nicks);
			}
		} else
		{
			gtk_tree_selection_unselect_all (sel);
		}
	}
}
#else /* GTK3 */
static gint
userlist_click_cb (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
	char **nicks;
	int i;
	GtkTreeSelection *sel;
	GtkTreePath *path;

	if (!event)
		return FALSE;

	if (!(event->state & STATE_CTRL) &&
		event->type == GDK_2BUTTON_PRESS && prefs.hex_gui_ulist_doubleclick[0])
	{
		nicks = userlist_selection_list (widget, &i);
		if (nicks)
		{
			nick_command_parse (current_sess, prefs.hex_gui_ulist_doubleclick, nicks[0],
									  nicks[0]);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
		}
		return TRUE;
	}

	if (event->button == 3)
	{
		/* do we have a multi-selection? */
		nicks = userlist_selection_list (widget, &i);
		if (nicks && i > 1)
		{
			menu_nickmenu (current_sess, event, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
			return TRUE;
		}
		if (nicks)
		{
			g_free (nicks[0]);
			g_free (nicks);
		}

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
			 event->x, event->y, &path, 0, 0, 0))
		{
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
			gtk_tree_path_free (path);
			nicks = userlist_selection_list (widget, &i);
			if (nicks)
			{
				menu_nickmenu (current_sess, event, nicks[0], i);
				while (i)
				{
					i--;
					g_free (nicks[i]);
				}
				g_free (nicks);
			}
		} else
		{
			gtk_tree_selection_unselect_all (sel);
		}

		return TRUE;
	}

	return FALSE;
}
#endif

/*
 * Key handler for userlist - forwards printable keys to input box
 * GTK3: Uses GdkEventKey from "key_press_event" signal
 * GTK4: Uses GtkEventControllerKey with different signature
 */
#if HC_GTK4
static gboolean
userlist_key_cb (GtkEventControllerKey *controller, guint keyval,
                 guint keycode, GdkModifierType state, gpointer userdata)
{
	if (keyval >= GDK_KEY_asterisk && keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		/* GTK4: Cannot forward events directly, insert the character instead */
		if (keyval >= GDK_KEY_space && keyval <= GDK_KEY_asciitilde)
		{
			char buf[2] = { (char)keyval, 0 };
			int pos = -1;
			gtk_editable_insert_text (GTK_EDITABLE (current_sess->gui->input_box), buf, 1, &pos);
		}
		return TRUE;
	}

	return FALSE;
}
#else /* GTK3 */
static gboolean
userlist_key_cb (GtkWidget *wid, GdkEventKey *evt, gpointer userdata)
{
	if (evt->keyval >= GDK_KEY_asterisk && evt->keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		gtk_widget_event (current_sess->gui->input_box, (GdkEvent *)evt);
		return TRUE;
	}

	return FALSE;
}
#endif

GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *treeview;
#if !HC_GTK4
	static const GtkTargetEntry dnd_dest_targets[] =
	{
		{"text/uri-list", 0, 1},
		{"HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 }
	};
	static const GtkTargetEntry dnd_src_target[] =
	{
		{"HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 }
	};
#endif

	sw = hc_scrolled_window_new ();
#if !HC_GTK4
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
													 GTK_SHADOW_ETCHED_IN);
#endif
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
											  prefs.hex_gui_ulist_show_hosts ?
												GTK_POLICY_AUTOMATIC :
												GTK_POLICY_NEVER,
											  GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand (sw, TRUE);
	hc_box_pack_start (box, sw, TRUE, TRUE, 0);
	hc_widget_show (sw);

	treeview = gtk_tree_view_new ();
	gtk_widget_set_name (treeview, "hexchat-userlist");
	gtk_widget_set_can_focus (treeview, FALSE);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection
										  (GTK_TREE_VIEW (treeview)),
										  GTK_SELECTION_MULTIPLE);

#if HC_GTK4
	{
		/* GTK4: DND using GtkDropTarget and GtkDragSource */
		GtkDropTarget *drop_target;

		/* File drops for DCC (drop file on user to send) */
		drop_target = gtk_drop_target_new (G_TYPE_FILE, GDK_ACTION_COPY | GDK_ACTION_MOVE);
		g_signal_connect (drop_target, "drop", G_CALLBACK (userlist_file_drop_cb), treeview);
		g_signal_connect (drop_target, "motion", G_CALLBACK (userlist_drop_motion_cb), treeview);
		g_signal_connect (drop_target, "leave", G_CALLBACK (userlist_drop_leave_cb), treeview);
		gtk_widget_add_controller (treeview, GTK_EVENT_CONTROLLER (drop_target));

		/* Layout swapping drag source (drag userlist to reposition) */
		mg_setup_userlist_drag_source (treeview);
	}

	/* GTK4: Use event controllers for click and key events */
	hc_add_click_gesture (treeview, G_CALLBACK (userlist_click_cb), NULL, NULL);
	hc_add_key_controller (treeview, G_CALLBACK (userlist_key_cb), NULL, NULL);
#else
	/* GTK3: set up drops */
	gtk_drag_dest_set (treeview, GTK_DEST_DEFAULT_ALL, dnd_dest_targets, 2,
							 GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);
	gtk_drag_source_set (treeview, GDK_BUTTON1_MASK, dnd_src_target, 1, GDK_ACTION_MOVE);

	/* file DND (for DCC) */
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (userlist_dnd_motion), treeview);
	g_signal_connect (G_OBJECT (treeview), "drag_leave",
							G_CALLBACK (userlist_dnd_leave), 0);
	g_signal_connect (G_OBJECT (treeview), "drag_data_received",
							G_CALLBACK (userlist_dnd_drop), treeview);

	g_signal_connect (G_OBJECT (treeview), "button_press_event",
							G_CALLBACK (userlist_click_cb), 0);
	g_signal_connect (G_OBJECT (treeview), "key_press_event",
							G_CALLBACK (userlist_key_cb), 0);

	/* tree/chanview DND */
	g_signal_connect (G_OBJECT (treeview), "drag_begin",
							G_CALLBACK (mg_drag_begin_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_drop",
							G_CALLBACK (mg_drag_drop_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (mg_drag_motion_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_end",
							G_CALLBACK (mg_drag_end_cb), NULL);
#endif

	userlist_add_columns (GTK_TREE_VIEW (treeview));

	hc_scrolled_window_set_child (sw, treeview);
	hc_widget_show (treeview);

	return treeview;
}

void
userlist_show (session *sess)
{
	gtk_tree_view_set_model (GTK_TREE_VIEW (sess->gui->user_tree),
									 GTK_TREE_MODEL(sess->res->user_model));
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	int thisname;
	char *name;
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		if (do_clear)
			gtk_tree_selection_unselect_all (selection);

		do
		{
			if (*word[0])
			{
				gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
				thisname = 0;
				while ( *(name = word[thisname++]) )
				{
					if (sess->server->p_cmp (row_user->nick, name) == 0)
					{
						gtk_tree_selection_select_iter (selection, &iter);
						if (scroll_to)
							scroll_to_iter (&iter, treeview, model);
						break;
					}
				}
			}

		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}
