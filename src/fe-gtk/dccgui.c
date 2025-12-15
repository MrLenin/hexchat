/* X-Chat
 * Copyright (C) 1998-2006 Peter Zelezny.
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
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define WANTSOCKET
#define WANTARPA
#include "../common/inet.h"
#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/network.h"
#include "gtkutil.h"
#include "palette.h"
#include "maingui.h"


#if !HC_GTK4
enum	/* DCC SEND/RECV (GTK3 only) */
{
	COL_TYPE,
	COL_STATUS,
	COL_FILE,
	COL_SIZE,
	COL_POS,
	COL_PERC,
	COL_SPEED,
	COL_ETA,
	COL_NICK,
	COL_DCC, /* struct DCC * */
	COL_COLOR,	/* GdkRGBA * - foreground color */
	N_COLUMNS
};

enum	/* DCC CHAT (GTK3 only) */
{
	CCOL_STATUS,
	CCOL_NICK,
	CCOL_RECV,
	CCOL_SENT,
	CCOL_START,
	CCOL_DCC,	/* struct DCC * */
	CCOL_COLOR,	/* GdkRGBA * - foreground color */
	CN_COLUMNS
};
#endif /* !HC_GTK4 */

#if HC_GTK4
/*
 * GTK4 Implementation using GListStore + GtkColumnView
 */

/* GObject to hold DCC file transfer row data */
#define HC_TYPE_DCC_FILE_ITEM (hc_dcc_file_item_get_type())
G_DECLARE_FINAL_TYPE (HcDccFileItem, hc_dcc_file_item, HC, DCC_FILE_ITEM, GObject)

struct _HcDccFileItem {
	GObject parent;
	gboolean is_upload; /* TRUE=upload, FALSE=download */
	char *status;
	char *file;
	char *size;
	char *pos;
	char *perc;
	char *speed;
	char *eta;
	char *nick;
	struct DCC *dcc;
	GdkRGBA *colour;
};

G_DEFINE_TYPE (HcDccFileItem, hc_dcc_file_item, G_TYPE_OBJECT)

static void
hc_dcc_file_item_finalize (GObject *obj)
{
	HcDccFileItem *item = HC_DCC_FILE_ITEM (obj);
	g_free (item->status);
	g_free (item->file);
	g_free (item->size);
	g_free (item->pos);
	g_free (item->perc);
	g_free (item->speed);
	g_free (item->eta);
	g_free (item->nick);
	/* colour points to static colors[] array, don't free */
	G_OBJECT_CLASS (hc_dcc_file_item_parent_class)->finalize (obj);
}

static void
hc_dcc_file_item_class_init (HcDccFileItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_dcc_file_item_finalize;
}

static void
hc_dcc_file_item_init (HcDccFileItem *item)
{
	item->is_upload = FALSE;
	item->status = NULL;
	item->file = NULL;
	item->size = NULL;
	item->pos = NULL;
	item->perc = NULL;
	item->speed = NULL;
	item->eta = NULL;
	item->nick = NULL;
	item->dcc = NULL;
	item->colour = NULL;
}

static HcDccFileItem *
hc_dcc_file_item_new (gboolean is_upload, const char *status, const char *file,
                      const char *size, const char *pos, const char *perc,
                      const char *speed, const char *eta, const char *nick,
                      struct DCC *dcc, GdkRGBA *colour)
{
	HcDccFileItem *item = g_object_new (HC_TYPE_DCC_FILE_ITEM, NULL);
	item->is_upload = is_upload;
	item->status = g_strdup (status ? status : "");
	item->file = g_strdup (file ? file : "");
	item->size = g_strdup (size ? size : "");
	item->pos = g_strdup (pos ? pos : "");
	item->perc = g_strdup (perc ? perc : "");
	item->speed = g_strdup (speed ? speed : "");
	item->eta = g_strdup (eta ? eta : "");
	item->nick = g_strdup (nick ? nick : "");
	item->dcc = dcc;
	item->colour = colour;
	return item;
}

/* Update an existing file item */
static void
hc_dcc_file_item_update (HcDccFileItem *item, const char *status, const char *pos,
                         const char *perc, const char *speed, const char *eta,
                         GdkRGBA *colour)
{
	g_free (item->status);
	g_free (item->pos);
	g_free (item->perc);
	g_free (item->speed);
	g_free (item->eta);
	item->status = g_strdup (status ? status : "");
	item->pos = g_strdup (pos ? pos : "");
	item->perc = g_strdup (perc ? perc : "");
	item->speed = g_strdup (speed ? speed : "");
	item->eta = g_strdup (eta ? eta : "");
	item->colour = colour;
}

/* GObject to hold DCC chat row data */
#define HC_TYPE_DCC_CHAT_ITEM (hc_dcc_chat_item_get_type())
G_DECLARE_FINAL_TYPE (HcDccChatItem, hc_dcc_chat_item, HC, DCC_CHAT_ITEM, GObject)

struct _HcDccChatItem {
	GObject parent;
	char *status;
	char *nick;
	char *recv;
	char *sent;
	char *start;
	struct DCC *dcc;
	GdkRGBA *colour;
};

G_DEFINE_TYPE (HcDccChatItem, hc_dcc_chat_item, G_TYPE_OBJECT)

static void
hc_dcc_chat_item_finalize (GObject *obj)
{
	HcDccChatItem *item = HC_DCC_CHAT_ITEM (obj);
	g_free (item->status);
	g_free (item->nick);
	g_free (item->recv);
	g_free (item->sent);
	g_free (item->start);
	/* colour points to static colors[] array, don't free */
	G_OBJECT_CLASS (hc_dcc_chat_item_parent_class)->finalize (obj);
}

static void
hc_dcc_chat_item_class_init (HcDccChatItemClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = hc_dcc_chat_item_finalize;
}

static void
hc_dcc_chat_item_init (HcDccChatItem *item)
{
	item->status = NULL;
	item->nick = NULL;
	item->recv = NULL;
	item->sent = NULL;
	item->start = NULL;
	item->dcc = NULL;
	item->colour = NULL;
}

static HcDccChatItem *
hc_dcc_chat_item_new (const char *status, const char *nick, const char *recv,
                      const char *sent, const char *start, struct DCC *dcc,
                      GdkRGBA *colour)
{
	HcDccChatItem *item = g_object_new (HC_TYPE_DCC_CHAT_ITEM, NULL);
	item->status = g_strdup (status ? status : "");
	item->nick = g_strdup (nick ? nick : "");
	item->recv = g_strdup (recv ? recv : "");
	item->sent = g_strdup (sent ? sent : "");
	item->start = g_strdup (start ? start : "");
	item->dcc = dcc;
	item->colour = colour;
	return item;
}

/* Update an existing chat item */
static void
hc_dcc_chat_item_update (HcDccChatItem *item, const char *status, const char *recv,
                         const char *sent, GdkRGBA *colour)
{
	g_free (item->status);
	g_free (item->recv);
	g_free (item->sent);
	item->status = g_strdup (status ? status : "");
	item->recv = g_strdup (recv ? recv : "");
	item->sent = g_strdup (sent ? sent : "");
	item->colour = colour;
}

/* GTK4 stores and textures */
static GListStore *dcc_file_store = NULL;
static GListStore *dcc_chat_store = NULL;
static GdkTexture *tex_up = NULL;
static GdkTexture *tex_dn = NULL;
#endif /* HC_GTK4 */

struct dccwindow
{
	GtkWidget *window;

	GtkWidget *list;
#if HC_GTK4
	GtkSelectionModel *sel_model;
#else
	GtkListStore *store;
	GtkTreeSelection *sel;
#endif

	GtkWidget *abort_button;
	GtkWidget *accept_button;
	GtkWidget *resume_button;
	GtkWidget *open_button;
	GtkWidget *clear_button; /* clears aborted and completed requests */

	GtkWidget *file_label;
	GtkWidget *address_label;
};

struct my_dcc_send
{
	struct session *sess;
	char *nick;
	gint64 maxcps;
	int passive;
};

static struct dccwindow dccfwin = {NULL, };	/* file */
static struct dccwindow dcccwin = {NULL, };	/* chat */
#if !HC_GTK4
static GdkPixbuf *pix_up = NULL;	/* up arrow */
static GdkPixbuf *pix_dn = NULL;	/* down arrow */
#endif
static int win_width = 600;
static int win_height = 256;
static short view_mode;	/* 1=download 2=upload 3=both */
#define VIEW_DOWNLOAD 1
#define VIEW_UPLOAD 2
#define VIEW_BOTH 3


static void
proper_unit (guint64 size, char *buf, size_t buf_len)
{
	gchar *formatted_str;
	GFormatSizeFlags format_flags = G_FORMAT_SIZE_DEFAULT;

#ifndef __APPLE__ /* OS X uses SI */
#ifndef WIN32 /* Windows uses IEC size (with SI format) */
	if (prefs.hex_gui_filesize_iec) /* Linux can't decide... */
#endif
		format_flags = G_FORMAT_SIZE_IEC_UNITS;
#endif

	formatted_str = g_format_size_full (size, format_flags);
	g_strlcpy (buf, formatted_str, buf_len);

	g_free (formatted_str);
}

static void
dcc_send_filereq_file (struct my_dcc_send *mdc, char *file)
{
	if (file)
		dcc_send (mdc->sess, mdc->nick, file, mdc->maxcps, mdc->passive);
	else
	{
		g_free (mdc->nick);
		g_free (mdc);
	}
}

void
fe_dcc_send_filereq (struct session *sess, char *nick, int maxcps, int passive)
{
	char* tbuf = g_strdup_printf (_("Send file to %s"), nick);

	struct my_dcc_send *mdc = g_new (struct my_dcc_send, 1);
	mdc->sess = sess;
	mdc->nick = g_strdup (nick);
	mdc->maxcps = maxcps;
	mdc->passive = passive;

	gtkutil_file_req (NULL, tbuf, dcc_send_filereq_file, mdc, prefs.hex_dcc_dir, NULL, FRF_MULTIPLE|FRF_FILTERISINITIAL);

	g_free (tbuf);
}

#if HC_GTK4
static HcDccChatItem *
dcc_prepare_chat_item (struct DCC *dcc)
{
	static char pos[16], size[16];
	char datebuf[64];
	char *date;
	GdkRGBA *colour;

	date = ctime (&dcc->starttime);
	g_strlcpy (datebuf, date, sizeof (datebuf));
	/* remove the \n */
	if (datebuf[0] && datebuf[strlen(datebuf) - 1] == '\n')
		datebuf[strlen(datebuf) - 1] = 0;

	proper_unit (dcc->pos, pos, sizeof (pos));
	proper_unit (dcc->size, size, sizeof (size));

	colour = dccstat[dcc->dccstat].color == 1 ? NULL : colors + dccstat[dcc->dccstat].color;

	return hc_dcc_chat_item_new (_(dccstat[dcc->dccstat].name),
	                             dcc->nick, pos, size, datebuf, dcc, colour);
}

static void
dcc_update_chat_item (HcDccChatItem *item, struct DCC *dcc)
{
	static char pos[16], size[16];
	GdkRGBA *colour;

	proper_unit (dcc->pos, pos, sizeof (pos));
	proper_unit (dcc->size, size, sizeof (size));

	colour = dccstat[dcc->dccstat].color == 1 ? NULL : colors + dccstat[dcc->dccstat].color;

	hc_dcc_chat_item_update (item, _(dccstat[dcc->dccstat].name), pos, size, colour);
}
#else /* GTK3 */
static void
dcc_prepare_row_chat (struct DCC *dcc, GtkListStore *store, GtkTreeIter *iter,
							 gboolean update_only)
{
	static char pos[16], size[16];
	char *date;

	date = ctime (&dcc->starttime);
	date[strlen (date) - 1] = 0;	/* remove the \n */

	proper_unit (dcc->pos, pos, sizeof (pos));
	proper_unit (dcc->size, size, sizeof (size));

	gtk_list_store_set (store, iter,
							  CCOL_STATUS, _(dccstat[dcc->dccstat].name),
							  CCOL_NICK, dcc->nick,
							  CCOL_RECV, pos,
							  CCOL_SENT, size,
							  CCOL_START, date,
							  CCOL_DCC, dcc,
							  CCOL_COLOR,
							  dccstat[dcc->dccstat].color == 1 ?
								NULL :
								colors + dccstat[dcc->dccstat].color,
							  -1);
}
#endif /* HC_GTK4 */

#if HC_GTK4
/* GTK4: Prepare file item data for send/recv */
static void
dcc_prepare_file_data (struct DCC *dcc, gboolean is_send, gboolean update_only,
                       char *status, char *file, char *size, char *pos,
                       char *perc, char *kbs, char *eta, GdkRGBA **colour)
{
	int to_go;
	float per;

	g_strlcpy (status, _(dccstat[dcc->dccstat].name), 64);
	proper_unit (dcc->size, size, 16);

	if (is_send)
	{
		/* percentage ack'ed */
		per = (float) ((dcc->ack * 100.00) / dcc->size);
		proper_unit (dcc->pos, pos, 16);
		if (dcc->cps != 0)
		{
			to_go = (dcc->size - dcc->ack) / dcc->cps;
			g_snprintf (eta, 16, "%.2d:%.2d:%.2d",
			            to_go / 3600, (to_go / 60) % 60, to_go % 60);
		}
		else
			strcpy (eta, "--:--:--");
	}
	else
	{
		/* receive */
		if (dcc->dccstat == STAT_QUEUED)
			proper_unit (dcc->resumable, pos, 16);
		else
			proper_unit (dcc->pos, pos, 16);
		/* percentage recv'ed */
		per = (float) ((dcc->pos * 100.00) / dcc->size);
		if (dcc->cps != 0)
		{
			to_go = (dcc->size - dcc->pos) / dcc->cps;
			g_snprintf (eta, 16, "%.2d:%.2d:%.2d",
			            to_go / 3600, (to_go / 60) % 60, to_go % 60);
		}
		else
			strcpy (eta, "--:--:--");
	}

	g_snprintf (kbs, 16, "%.1f", ((float)dcc->cps) / 1024);
	g_snprintf (perc, 16, "%.0f%%", per);

	if (!update_only)
		g_strlcpy (file, file_part (dcc->file), 256);

	*colour = dccstat[dcc->dccstat].color == 1 ? NULL : colors + dccstat[dcc->dccstat].color;
}

static HcDccFileItem *
dcc_prepare_file_item (struct DCC *dcc, gboolean is_send)
{
	char status[64], file[256], size[16], pos[16], perc[16], kbs[16], eta[16];
	GdkRGBA *colour;

	dcc_prepare_file_data (dcc, is_send, FALSE, status, file, size, pos, perc, kbs, eta, &colour);

	return hc_dcc_file_item_new (is_send, status, file, size, pos, perc, kbs, eta,
	                             dcc->nick, dcc, colour);
}

static void
dcc_update_file_item (HcDccFileItem *item, struct DCC *dcc, gboolean is_send)
{
	char status[64], file[256], size[16], pos[16], perc[16], kbs[16], eta[16];
	GdkRGBA *colour;

	dcc_prepare_file_data (dcc, is_send, TRUE, status, file, size, pos, perc, kbs, eta, &colour);

	hc_dcc_file_item_update (item, status, pos, perc, kbs, eta, colour);
}
#else /* GTK3 */
static void
dcc_prepare_row_send (struct DCC *dcc, GtkListStore *store, GtkTreeIter *iter,
							 gboolean update_only)
{
	static char pos[16], size[16], kbs[14], perc[14], eta[14];
	int to_go;
	float per;

	if (!pix_up)
		pix_up = gtk_widget_render_icon_pixbuf (dccfwin.window, "gtk-go-up",
													GTK_ICON_SIZE_MENU);

	/* percentage ack'ed */
	per = (float) ((dcc->ack * 100.00) / dcc->size);
	proper_unit (dcc->size, size, sizeof (size));
	proper_unit (dcc->pos, pos, sizeof (pos));
	g_snprintf (kbs, sizeof (kbs), "%.1f", ((float)dcc->cps) / 1024);
	g_snprintf (perc, sizeof (perc), "%.0f%%", per);
	if (dcc->cps != 0)
	{
		to_go = (dcc->size - dcc->ack) / dcc->cps;
		g_snprintf (eta, sizeof (eta), "%.2d:%.2d:%.2d",
					 to_go / 3600, (to_go / 60) % 60, to_go % 60);
	} else
		strcpy (eta, "--:--:--");

	if (update_only)
		gtk_list_store_set (store, iter,
								  COL_STATUS, _(dccstat[dcc->dccstat].name),
								  COL_POS, pos,
								  COL_PERC, perc,
								  COL_SPEED, kbs,
								  COL_ETA, eta,
								  COL_COLOR,
								  dccstat[dcc->dccstat].color == 1 ?
									NULL :
									colors + dccstat[dcc->dccstat].color,
									-1);
	else
		gtk_list_store_set (store, iter,
								  COL_TYPE, pix_up,
								  COL_STATUS, _(dccstat[dcc->dccstat].name),
								  COL_FILE, file_part (dcc->file),
								  COL_SIZE, size,
								  COL_POS, pos,
								  COL_PERC, perc,
								  COL_SPEED, kbs,
								  COL_ETA, eta,
								  COL_NICK, dcc->nick,
								  COL_DCC, dcc,
								  COL_COLOR,
								  dccstat[dcc->dccstat].color == 1 ?
									NULL :
									colors + dccstat[dcc->dccstat].color,
									-1);
}

static void
dcc_prepare_row_recv (struct DCC *dcc, GtkListStore *store, GtkTreeIter *iter,
							 gboolean update_only)
{
	static char size[16], pos[16], kbs[16], perc[14], eta[16];
	float per;
	int to_go;

	if (!pix_dn)
		pix_dn = gtk_widget_render_icon_pixbuf (dccfwin.window, "gtk-go-down",
													GTK_ICON_SIZE_MENU);

	proper_unit (dcc->size, size, sizeof (size));
	if (dcc->dccstat == STAT_QUEUED)
		proper_unit (dcc->resumable, pos, sizeof (pos));
	else
		proper_unit (dcc->pos, pos, sizeof (pos));
	g_snprintf (kbs, sizeof (kbs), "%.1f", ((float)dcc->cps) / 1024);
	/* percentage recv'ed */
	per = (float) ((dcc->pos * 100.00) / dcc->size);
	g_snprintf (perc, sizeof (perc), "%.0f%%", per);
	if (dcc->cps != 0)
	{
		to_go = (dcc->size - dcc->pos) / dcc->cps;
		g_snprintf (eta, sizeof (eta), "%.2d:%.2d:%.2d",
					 to_go / 3600, (to_go / 60) % 60, to_go % 60);
	} else
		strcpy (eta, "--:--:--");

	if (update_only)
		gtk_list_store_set (store, iter,
								  COL_STATUS, _(dccstat[dcc->dccstat].name),
								  COL_POS, pos,
								  COL_PERC, perc,
								  COL_SPEED, kbs,
								  COL_ETA, eta,
								  COL_COLOR,
								  dccstat[dcc->dccstat].color == 1 ?
									NULL :
									colors + dccstat[dcc->dccstat].color,
									-1);
	else
		gtk_list_store_set (store, iter,
								  COL_TYPE, pix_dn,
								  COL_STATUS, _(dccstat[dcc->dccstat].name),
								  COL_FILE, file_part (dcc->file),
								  COL_SIZE, size,
								  COL_POS, pos,
								  COL_PERC, perc,
								  COL_SPEED, kbs,
								  COL_ETA, eta,
								  COL_NICK, dcc->nick,
								  COL_DCC, dcc,
								  COL_COLOR,
								  dccstat[dcc->dccstat].color == 1 ?
									NULL :
									colors + dccstat[dcc->dccstat].color,
									-1);
}
#endif /* HC_GTK4 */

#if HC_GTK4
/* GTK4: Find a file item in the store by DCC pointer, return position or -1 */
static int
dcc_find_file_item (struct DCC *find_dcc)
{
	guint n, i;
	HcDccFileItem *item;

	if (!dcc_file_store)
		return -1;

	n = g_list_model_get_n_items (G_LIST_MODEL (dcc_file_store));
	for (i = 0; i < n; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (dcc_file_store), i);
		if (item && item->dcc == find_dcc)
		{
			g_object_unref (item);
			return (int)i;
		}
		if (item)
			g_object_unref (item);
	}
	return -1;
}

/* GTK4: Find a chat item in the store by DCC pointer, return position or -1 */
static int
dcc_find_chat_item (struct DCC *find_dcc)
{
	guint n, i;
	HcDccChatItem *item;

	if (!dcc_chat_store)
		return -1;

	n = g_list_model_get_n_items (G_LIST_MODEL (dcc_chat_store));
	for (i = 0; i < n; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (dcc_chat_store), i);
		if (item && item->dcc == find_dcc)
		{
			g_object_unref (item);
			return (int)i;
		}
		if (item)
			g_object_unref (item);
	}
	return -1;
}
#else /* GTK3 */
static gboolean
dcc_find_row (struct DCC *find_dcc, GtkTreeModel *model, GtkTreeIter *iter, int col)
{
	struct DCC *dcc;

	if (gtk_tree_model_get_iter_first (model, iter))
	{
		do
		{
			gtk_tree_model_get (model, iter, col, &dcc, -1);
			if (dcc == find_dcc)
				return TRUE;
		}
		while (gtk_tree_model_iter_next (model, iter));
	}

	return FALSE;
}
#endif /* HC_GTK4 */

static void
dcc_update_recv (struct DCC *dcc)
{
#if HC_GTK4
	int pos;
	HcDccFileItem *item;

	if (!dccfwin.window)
		return;

	pos = dcc_find_file_item (dcc);
	if (pos < 0)
		return;

	item = g_list_model_get_item (G_LIST_MODEL (dcc_file_store), pos);
	if (item)
	{
		dcc_update_file_item (item, dcc, FALSE);
		g_object_unref (item);
		/* Signal the model that item at pos changed */
		g_list_model_items_changed (G_LIST_MODEL (dcc_file_store), pos, 1, 1);
	}
#else /* GTK3 */
	GtkTreeIter iter;

	if (!dccfwin.window)
		return;

	if (!dcc_find_row (dcc, GTK_TREE_MODEL (dccfwin.store), &iter, COL_DCC))
		return;

	dcc_prepare_row_recv (dcc, dccfwin.store, &iter, TRUE);
#endif
}

static void
dcc_update_chat (struct DCC *dcc)
{
#if HC_GTK4
	int pos;
	HcDccChatItem *item;

	if (!dcccwin.window)
		return;

	pos = dcc_find_chat_item (dcc);
	if (pos < 0)
		return;

	item = g_list_model_get_item (G_LIST_MODEL (dcc_chat_store), pos);
	if (item)
	{
		dcc_update_chat_item (item, dcc);
		g_object_unref (item);
		/* Signal the model that item at pos changed */
		g_list_model_items_changed (G_LIST_MODEL (dcc_chat_store), pos, 1, 1);
	}
#else /* GTK3 */
	GtkTreeIter iter;

	if (!dcccwin.window)
		return;

	if (!dcc_find_row (dcc, GTK_TREE_MODEL (dcccwin.store), &iter, CCOL_DCC))
		return;

	dcc_prepare_row_chat (dcc, dcccwin.store, &iter, TRUE);
#endif
}

static void
dcc_update_send (struct DCC *dcc)
{
#if HC_GTK4
	int pos;
	HcDccFileItem *item;

	if (!dccfwin.window)
		return;

	pos = dcc_find_file_item (dcc);
	if (pos < 0)
		return;

	item = g_list_model_get_item (G_LIST_MODEL (dcc_file_store), pos);
	if (item)
	{
		dcc_update_file_item (item, dcc, TRUE);
		g_object_unref (item);
		/* Signal the model that item at pos changed */
		g_list_model_items_changed (G_LIST_MODEL (dcc_file_store), pos, 1, 1);
	}
#else /* GTK3 */
	GtkTreeIter iter;

	if (!dccfwin.window)
		return;

	if (!dcc_find_row (dcc, GTK_TREE_MODEL (dccfwin.store), &iter, COL_DCC))
		return;

	dcc_prepare_row_send (dcc, dccfwin.store, &iter, TRUE);
#endif
}

static void
close_dcc_file_window (GtkWindow *win, gpointer data)
{
	dccfwin.window = NULL;
}

#if HC_GTK4
static void
dcc_append_file_gtk4 (struct DCC *dcc, gboolean prepend)
{
	HcDccFileItem *item;
	gboolean is_send = (dcc->type == TYPE_SEND);

	item = dcc_prepare_file_item (dcc, is_send);
	if (prepend)
		g_list_store_insert (dcc_file_store, 0, item);
	else
		g_list_store_append (dcc_file_store, item);
	g_object_unref (item);
}
#else /* GTK3 */
static void
dcc_append (struct DCC *dcc, GtkListStore *store, gboolean prepend)
{
	GtkTreeIter iter;

	if (prepend)
		gtk_list_store_prepend (store, &iter);
	else
		gtk_list_store_append (store, &iter);

	if (dcc->type == TYPE_RECV)
		dcc_prepare_row_recv (dcc, store, &iter, FALSE);
	else
		dcc_prepare_row_send (dcc, store, &iter, FALSE);
}
#endif /* HC_GTK4 */

/* Returns aborted and completed transfers. */
static GSList *
dcc_get_completed (void)
{
	struct DCC *dcc;
	GSList *completed = NULL;

#if HC_GTK4
	guint n, i;
	HcDccFileItem *item;

	if (!dcc_file_store)
		return NULL;

	n = g_list_model_get_n_items (G_LIST_MODEL (dcc_file_store));
	for (i = 0; i < n; i++)
	{
		item = g_list_model_get_item (G_LIST_MODEL (dcc_file_store), i);
		if (item)
		{
			dcc = item->dcc;
			if (is_dcc_completed (dcc))
				completed = g_slist_prepend (completed, dcc);
			g_object_unref (item);
		}
	}
#else /* GTK3 */
	GtkTreeIter iter;
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (dccfwin.store);
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_DCC, &dcc, -1);
			if (is_dcc_completed (dcc))
				completed = g_slist_prepend (completed, dcc);

		} while (gtk_tree_model_iter_next (model, &iter));
	}
#endif

	return completed;
}

static gboolean
dcc_completed_transfer_exists (void)
{
	gboolean exist;
	GSList *comp_list;

	comp_list = dcc_get_completed ();
	exist = comp_list != NULL;

	g_slist_free (comp_list);
	return exist;
}

static void
update_clear_button_sensitivity (void)
{
	gboolean sensitive = dcc_completed_transfer_exists ();
	gtk_widget_set_sensitive (dccfwin.clear_button, sensitive);
}

static void
dcc_fill_window (int flags)
{
	struct DCC *dcc;
	GSList *list;
	int i = 0;

#if HC_GTK4
	g_list_store_remove_all (dcc_file_store);

	if (flags & VIEW_UPLOAD)
	{
		list = dcc_list;
		while (list)
		{
			dcc = list->data;
			if (dcc->type == TYPE_SEND)
			{
				dcc_append_file_gtk4 (dcc, FALSE);
				i++;
			}
			list = list->next;
		}
	}

	if (flags & VIEW_DOWNLOAD)
	{
		list = dcc_list;
		while (list)
		{
			dcc = list->data;
			if (dcc->type == TYPE_RECV)
			{
				dcc_append_file_gtk4 (dcc, FALSE);
				i++;
			}
			list = list->next;
		}
	}

	/* if only one entry, select it (so Accept button can work) */
	if (i == 1)
		gtk_selection_model_select_item (dccfwin.sel_model, 0, TRUE);
#else /* GTK3 */
	GtkTreeIter iter;

	gtk_list_store_clear (GTK_LIST_STORE (dccfwin.store));

	if (flags & VIEW_UPLOAD)
	{
		list = dcc_list;
		while (list)
		{
			dcc = list->data;
			if (dcc->type == TYPE_SEND)
			{
				dcc_append (dcc, dccfwin.store, FALSE);
				i++;
			}
			list = list->next;
		}
	}

	if (flags & VIEW_DOWNLOAD)
	{
		list = dcc_list;
		while (list)
		{
			dcc = list->data;
			if (dcc->type == TYPE_RECV)
			{
				dcc_append (dcc, dccfwin.store, FALSE);
				i++;
			}
			list = list->next;
		}
	}

	/* if only one entry, select it (so Accept button can work) */
	if (i == 1)
	{
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dccfwin.store), &iter);
		gtk_tree_selection_select_iter (dccfwin.sel, &iter);
	}
#endif

	update_clear_button_sensitivity ();
}

/* return list of selected DCCs */

#if HC_GTK4
static GSList *
dcc_get_selected_gtk4 (GtkSelectionModel *sel_model, GListStore *store)
{
	GSList *list = NULL;
	GtkBitset *selection;
	GtkBitsetIter biter;
	guint pos;

	selection = gtk_selection_model_get_selection (sel_model);
	if (gtk_bitset_iter_init_first (&biter, selection, &pos))
	{
		do
		{
			HcDccFileItem *item = g_list_model_get_item (G_LIST_MODEL (store), pos);
			if (item)
			{
				list = g_slist_prepend (list, item->dcc);
				g_object_unref (item);
			}
		}
		while (gtk_bitset_iter_next (&biter, &pos));
	}
	gtk_bitset_unref (selection);

	return g_slist_reverse (list);
}

static GSList *
dcc_get_selected (void)
{
	return dcc_get_selected_gtk4 (dccfwin.sel_model, dcc_file_store);
}
#else /* GTK3 */
static GSList *
treeview_get_selected (GtkTreeModel *model, GtkTreeSelection *sel, int column)
{
	GtkTreeIter iter;
	GSList *list = NULL;
	void *ptr;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			if (gtk_tree_selection_iter_is_selected (sel, &iter))
			{
				gtk_tree_model_get (model, &iter, column, &ptr, -1);
				list = g_slist_prepend (list, ptr);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	return g_slist_reverse (list);
}

static GSList *
dcc_get_selected (void)
{
	return treeview_get_selected (GTK_TREE_MODEL (dccfwin.store),
											dccfwin.sel, COL_DCC);
}
#endif /* HC_GTK4 */

static void
resume_clicked (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	char buf[512];
	GSList *list;

	list = dcc_get_selected ();
	if (!list)
		return;
	dcc = list->data;
	g_slist_free (list);

	if (dcc->type == TYPE_RECV && !dcc_resume (dcc))
	{
		switch (dcc->resume_error)
		{
		case 0:	/* unknown error */
			fe_message (_("That file is not resumable."), FE_MSG_ERROR);
			break;
		case 1:
			g_snprintf (buf, sizeof (buf),
						_(	"Cannot access file: %s\n"
							"%s.\n"
							"Resuming not possible."), dcc->destfile,	
							errorstring (dcc->resume_errno));
			fe_message (buf, FE_MSG_ERROR);
			break;
		case 2:
			fe_message (_("File in download directory is larger "
							"than file offered. Resuming not possible."), FE_MSG_ERROR);
			break;
		case 3:
			fe_message (_("Cannot resume the same file from two people."), FE_MSG_ERROR);
		}
	}
}

static void
abort_clicked (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	GSList *start, *list;

	start = list = dcc_get_selected ();
	for (; list; list = list->next)
	{
		dcc = list->data;
		dcc_abort (dcc->serv->front_session, dcc);
	}
	g_slist_free (start);
	
	/* Enable the clear button if it wasn't already enabled */
	update_clear_button_sensitivity ();
}

static void
accept_clicked (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	GSList *start, *list;

	start = list = dcc_get_selected ();
	for (; list; list = list->next)
	{
		dcc = list->data;
		if (dcc->type != TYPE_SEND)
			dcc_get (dcc);
	}
	g_slist_free (start);
}

static void
clear_completed (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	GSList *completed;

	/* Make a new list of only the completed items and abort each item.
	 * A new list is made because calling dcc_abort removes items from the original list,
	 * making it impossible to iterate over that list directly.
	*/
	for (completed = dcc_get_completed (); completed; completed = completed->next)
	{
		dcc = completed->data;
		dcc_abort (dcc->serv->front_session, dcc);
	}

	/* The data was freed by dcc_close */
	g_slist_free (completed);
	update_clear_button_sensitivity ();
}

static void
browse_folder (char *dir)
{
#ifdef WIN32
	/* no need for file:// in ShellExecute() */
	fe_open_url (dir);
#else
	char buf[512];

	g_snprintf (buf, sizeof (buf), "file://%s", dir);
	fe_open_url (buf);
#endif
}

static void
browse_dcc_folder (void)
{
	if (prefs.hex_dcc_completed_dir[0])
		browse_folder (prefs.hex_dcc_completed_dir);
	else
		browse_folder (prefs.hex_dcc_dir);
}

static void
dcc_details_populate (struct DCC *dcc)
{
	char buf[128];

	if (!dcc)
	{
		gtk_label_set_text (GTK_LABEL (dccfwin.file_label), NULL);
		gtk_label_set_text (GTK_LABEL (dccfwin.address_label), NULL);
		return;
	}

	/* full path */
	if (dcc->type == TYPE_RECV)
		gtk_label_set_text (GTK_LABEL (dccfwin.file_label), dcc->destfile);
	else
		gtk_label_set_text (GTK_LABEL (dccfwin.file_label), dcc->file);

	/* address and port */
	g_snprintf (buf, sizeof (buf), "%s : %d", net_ip (dcc->addr), dcc->port);
	gtk_label_set_text (GTK_LABEL (dccfwin.address_label), buf);
}

static void
dcc_row_cb_common (void)
{
	struct DCC *dcc;
	GSList *list;

	list = dcc_get_selected ();
	if (!list)
	{
		gtk_widget_set_sensitive (dccfwin.accept_button, FALSE);
		gtk_widget_set_sensitive (dccfwin.resume_button, FALSE);
		gtk_widget_set_sensitive (dccfwin.abort_button, FALSE);
		dcc_details_populate (NULL);
		return;
	}

	gtk_widget_set_sensitive (dccfwin.abort_button, TRUE);

	if (list->next)	/* multi selection */
	{
		gtk_widget_set_sensitive (dccfwin.accept_button, TRUE);
		gtk_widget_set_sensitive (dccfwin.resume_button, TRUE);
		dcc_details_populate (list->data);
	}
	else
	{
		/* turn OFF/ON appropriate buttons */
		dcc = list->data;
		if (dcc->dccstat == STAT_QUEUED && dcc->type == TYPE_RECV)
		{
			gtk_widget_set_sensitive (dccfwin.accept_button, TRUE);
			gtk_widget_set_sensitive (dccfwin.resume_button, TRUE);
		}
		else
		{
			gtk_widget_set_sensitive (dccfwin.accept_button, FALSE);
			gtk_widget_set_sensitive (dccfwin.resume_button, FALSE);
		}

		dcc_details_populate (dcc);
	}

	g_slist_free (list);
}

#if HC_GTK4
static void
dcc_row_cb (GtkSelectionModel *sel_model, guint position, guint n_items, gpointer user_data)
{
	dcc_row_cb_common ();
}
#else /* GTK3 */
static void
dcc_row_cb (GtkTreeSelection *sel, gpointer user_data)
{
	dcc_row_cb_common ();
}
#endif

static void
dcc_dclick_cb_common (void)
{
	struct DCC *dcc;
	GSList *list;

	list = dcc_get_selected ();
	if (!list)
		return;
	dcc = list->data;
	g_slist_free (list);

	if (dcc->type == TYPE_RECV)
	{
		accept_clicked (0, 0);
		return;
	}

	switch (dcc->dccstat)
	{
	case STAT_FAILED:
	case STAT_ABORTED:
	case STAT_DONE:
		dcc_abort (dcc->serv->front_session, dcc);
		break;
	case STAT_QUEUED:
	case STAT_ACTIVE:
	case STAT_CONNECTING:
		break;
	}
}

#if HC_GTK4
static void
dcc_dclick_cb (GtkColumnView *view, guint position, gpointer data)
{
	dcc_dclick_cb_common ();
}
#else /* GTK3 */
static void
dcc_dclick_cb (GtkTreeView *view, GtkTreePath *path,
					GtkTreeViewColumn *column, gpointer data)
{
	dcc_dclick_cb_common ();
}
#endif

#if !HC_GTK4
static void
dcc_add_column (GtkWidget *tree, int textcol, int colorcol, char *title, gboolean right_justified)
{
	GtkCellRenderer *renderer;

	renderer = gtk_cell_renderer_text_new ();
	if (right_justified)
		g_object_set (G_OBJECT (renderer), "xalign", (float) 1.0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree), -1, title, renderer,
																"text", textcol, "foreground-rgba", colorcol,
																NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
}
#endif /* !HC_GTK4 */

static GtkWidget *
dcc_detail_label (char *text, GtkWidget *box, int num)
{
	GtkWidget *label;
	char buf[64];

	label = gtk_label_new (NULL);
	g_snprintf (buf, sizeof (buf), "<b>%s</b>", text);
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (box), label, 0, num, 1, 1);

	label = gtk_label_new (NULL);
	gtk_label_set_selectable (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_START);
	gtk_grid_attach (GTK_GRID (box), label, 1, num, 1, 1);

	return label;
}

static void
dcc_exp_cb (GtkWidget *exp, GtkWidget *box)
{
	if (gtk_widget_get_visible (box))
	{
		gtk_widget_hide (box);
	}
	else
	{
		gtk_widget_show (box);
	}
}

static void
dcc_toggle (GtkWidget *item, gpointer data)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (item)))
	{
		view_mode = GPOINTER_TO_INT (data);
		dcc_fill_window (GPOINTER_TO_INT (data));
	}
}

/*
 * Window resize handler to remember window size
 * GTK3: Uses GdkEventConfigure from "configure-event" signal
 * GTK4: Uses "notify::default-width" and "notify::default-height" signals, or we can
 *       connect to "close-request" to save size when window closes
 */
#if HC_GTK4
static void
dcc_configure_cb (GtkWindow *win, GParamSpec *pspec, gpointer data)
{
	/* remember the window size - GTK4 uses properties */
	gtk_window_get_default_size (win, &win_width, &win_height);
}

/*
 * GTK4 Column View factory callbacks for file transfer list
 */

/* Helper to apply color to label via CSS */
static void
dcc_apply_colour (GtkWidget *label, GdkRGBA *colour)
{
	if (colour)
	{
		char css[128];
		GtkCssProvider *provider;
		g_snprintf (css, sizeof(css), "label { color: rgba(%d,%d,%d,%.2f); }",
		            (int)(colour->red * 255), (int)(colour->green * 255),
		            (int)(colour->blue * 255), colour->alpha);
		provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_string (provider, css);
		gtk_style_context_add_provider (gtk_widget_get_style_context (label),
		                                GTK_STYLE_PROVIDER (provider),
		                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (provider);
	}
}

/* Icon column factory */
static void
dcc_file_setup_icon_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *picture = gtk_picture_new ();
	gtk_widget_set_size_request (picture, 16, 16);
	gtk_list_item_set_child (item, picture);
}

static void
dcc_file_bind_icon_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *picture = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);

	/* Load textures on first use */
	if (!tex_up)
	{
		GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
		GtkIconPaintable *icon = gtk_icon_theme_lookup_icon (theme, "go-up", NULL, 16, 1, GTK_TEXT_DIR_NONE, 0);
		if (icon)
			tex_up = gdk_texture_new_from_file (g_file_new_for_path (gtk_icon_paintable_get_file (icon) ? g_file_get_path (gtk_icon_paintable_get_file (icon)) : ""), NULL);
	}
	if (!tex_dn)
	{
		GtkIconTheme *theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
		GtkIconPaintable *icon = gtk_icon_theme_lookup_icon (theme, "go-down", NULL, 16, 1, GTK_TEXT_DIR_NONE, 0);
		if (icon)
			tex_dn = gdk_texture_new_from_file (g_file_new_for_path (gtk_icon_paintable_get_file (icon) ? g_file_get_path (gtk_icon_paintable_get_file (icon)) : ""), NULL);
	}

	/* Use GtkImage for icons instead - simpler */
	gtk_picture_set_paintable (GTK_PICTURE (picture),
	                           GDK_PAINTABLE (dcc_item->is_upload ? tex_up : tex_dn));
}

/* Generic text column setup */
static void
dcc_file_setup_text_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gboolean right_align = GPOINTER_TO_INT (user_data);
	gtk_label_set_xalign (GTK_LABEL (label), right_align ? 1.0 : 0.0);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

/* Bind callbacks for each column */
static void
dcc_file_bind_status_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->status);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_file_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->file);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_size_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->size);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_pos_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->pos);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_perc_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->perc);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_speed_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->speed);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_eta_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->eta);
	dcc_apply_colour (label, dcc_item->colour);
}

static void
dcc_file_bind_nick_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccFileItem *dcc_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), dcc_item->nick);
	dcc_apply_colour (label, dcc_item->colour);
}

/* Create GTK4 column view for file transfers */
static GtkWidget *
dcc_file_columnview_new (GtkWidget *vbox)
{
	GtkWidget *scroll;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scroll, TRUE);

	/* Create list store */
	dcc_file_store = g_list_store_new (HC_TYPE_DCC_FILE_ITEM);

	/* Create column view with multi-selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (dcc_file_store), GTK_SELECTION_MULTIPLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Save selection model reference */
	dccfwin.sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));

	/* Icon column (upload/download arrow) */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_icon_cb), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_icon_cb), NULL);
	col = gtk_column_view_column_new (NULL, factory);
	gtk_column_view_column_set_fixed_width (col, 24);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Status column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_status_cb), NULL);
	col = gtk_column_view_column_new (_("Status"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* File column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_file_cb), NULL);
	col = gtk_column_view_column_new (_("File"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Size column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_size_cb), NULL);
	col = gtk_column_view_column_new (_("Size"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Position column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_pos_cb), NULL);
	col = gtk_column_view_column_new (_("Position"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Percent column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_perc_cb), NULL);
	col = gtk_column_view_column_new ("%", factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Speed column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_speed_cb), NULL);
	col = gtk_column_view_column_new ("KB/s", factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* ETA column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_eta_cb), NULL);
	col = gtk_column_view_column_new (_("ETA"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Nick column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_file_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_file_bind_nick_cb), NULL);
	col = gtk_column_view_column_new (_("Nick"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (vbox, scroll, TRUE, TRUE, 0);

	/* Connect selection changed signal */
	g_signal_connect (dccfwin.sel_model, "selection-changed",
	                  G_CALLBACK (dcc_row_cb), NULL);

	/* Double-click activation */
	g_signal_connect (view, "activate",
	                  G_CALLBACK (dcc_dclick_cb), NULL);

	return view;
}
#else /* GTK3 */
static gboolean
dcc_configure_cb (GtkWindow *win, GdkEventConfigure *event, gpointer data)
{
	/* remember the window size */
	gtk_window_get_size (win, &win_width, &win_height);
	return FALSE;
}
#endif

int
fe_dcc_open_recv_win (int passive)
{
	GtkWidget *table, *vbox, *bbox, *view, *exp, *detailbox;
#if HC_GTK4
	GtkWidget *check;
#else
	GtkWidget *radio;
	GtkListStore *store;
	GSList *group;
#endif
	char buf[128];

	if (dccfwin.window)
	{
		if (!passive)
			mg_bring_tofront (dccfwin.window);
		return TRUE;
	}
	g_snprintf(buf, sizeof(buf), _("Uploads and Downloads - %s"), _(DISPLAY_NAME));
	dccfwin.window = mg_create_generic_tab ("Transfers", buf, FALSE, TRUE, close_dcc_file_window,
														 NULL, win_width, win_height, &vbox, 0);
	gtkutil_destroy_on_esc (dccfwin.window);
#if !HC_GTK4
	hc_container_set_border_width (dccfwin.window, 3);
#endif
	gtk_box_set_spacing (GTK_BOX (vbox), 3);

#if HC_GTK4
	view = dcc_file_columnview_new (vbox);
	dccfwin.list = view;
	view_mode = VIEW_BOTH;

	if (!prefs.hex_gui_tab_utils)
	{
		/* GTK4: Use notify signals for window size changes */
		g_signal_connect (G_OBJECT (dccfwin.window), "notify::default-width",
								G_CALLBACK (dcc_configure_cb), 0);
		g_signal_connect (G_OBJECT (dccfwin.window), "notify::default-height",
								G_CALLBACK (dcc_configure_cb), 0);
	}
#else /* GTK3 */
	store = gtk_list_store_new (N_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_POINTER, GDK_TYPE_RGBA);
	view = gtkutil_treeview_new (vbox, GTK_TREE_MODEL (store), NULL, -1);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);
	/* Up/Down Icon column */
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), -1, NULL,
																gtk_cell_renderer_pixbuf_new (),
																"pixbuf", COL_TYPE, NULL);
	dcc_add_column (view, COL_STATUS, COL_COLOR, _("Status"), FALSE);
	dcc_add_column (view, COL_FILE,   COL_COLOR, _("File"), FALSE);
	dcc_add_column (view, COL_SIZE,   COL_COLOR, _("Size"), TRUE);
	dcc_add_column (view, COL_POS,    COL_COLOR, _("Position"), TRUE);
	dcc_add_column (view, COL_PERC,   COL_COLOR, "%", TRUE);
	dcc_add_column (view, COL_SPEED,  COL_COLOR, "KB/s", TRUE);
	dcc_add_column (view, COL_ETA,    COL_COLOR, _("ETA"), FALSE);
	dcc_add_column (view, COL_NICK,   COL_COLOR, _("Nick"), FALSE);

	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), COL_FILE), TRUE);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), COL_NICK), TRUE);

	dccfwin.list = view;
	dccfwin.store = store;
	dccfwin.sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	view_mode = VIEW_BOTH;
	gtk_tree_selection_set_mode (dccfwin.sel, GTK_SELECTION_MULTIPLE);

	if (!prefs.hex_gui_tab_utils)
	{
		g_signal_connect (G_OBJECT (dccfwin.window), "configure_event",
								G_CALLBACK (dcc_configure_cb), 0);
	}
	g_signal_connect (G_OBJECT (dccfwin.sel), "changed",
							G_CALLBACK (dcc_row_cb), NULL);
	/* double click */
	g_signal_connect (G_OBJECT (view), "row-activated",
							G_CALLBACK (dcc_dclick_cb), NULL);
#endif /* HC_GTK4 */

	table = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (table), 16);
	hc_box_pack_start (vbox, table, FALSE, FALSE, 0);

#if HC_GTK4
	/* GTK4: Use GtkCheckButton with groups */
	check = gtk_check_button_new_with_mnemonic (_("Both"));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (check), TRUE);
	g_signal_connect (G_OBJECT (check), "toggled",
							G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_BOTH));
	gtk_grid_attach (GTK_GRID (table), check, 3, 0, 1, 1);

	{
		GtkWidget *check2 = gtk_check_button_new_with_mnemonic (_("Uploads"));
		gtk_check_button_set_group (GTK_CHECK_BUTTON (check2), GTK_CHECK_BUTTON (check));
		g_signal_connect (G_OBJECT (check2), "toggled",
								G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_UPLOAD));
		gtk_grid_attach (GTK_GRID (table), check2, 1, 0, 1, 1);

		GtkWidget *check3 = gtk_check_button_new_with_mnemonic (_("Downloads"));
		gtk_check_button_set_group (GTK_CHECK_BUTTON (check3), GTK_CHECK_BUTTON (check));
		g_signal_connect (G_OBJECT (check3), "toggled",
								G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_DOWNLOAD));
		gtk_grid_attach (GTK_GRID (table), check3, 2, 0, 1, 1);
	}
#else /* GTK3 */
	radio = gtk_radio_button_new_with_mnemonic (NULL, _("Both"));
	g_signal_connect (G_OBJECT (radio), "toggled",
							G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_BOTH));
	gtk_grid_attach (GTK_GRID (table), radio, 3, 0, 1, 1);
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));

	radio = gtk_radio_button_new_with_mnemonic (group, _("Uploads"));
	g_signal_connect (G_OBJECT (radio), "toggled",
							G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_UPLOAD));
	gtk_grid_attach (GTK_GRID (table), radio, 1, 0, 1, 1);
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));

	radio = gtk_radio_button_new_with_mnemonic (group, _("Downloads"));
	g_signal_connect (G_OBJECT (radio), "toggled",
							G_CALLBACK (dcc_toggle), GINT_TO_POINTER (VIEW_DOWNLOAD));
	gtk_grid_attach (GTK_GRID (table), radio, 2, 0, 1, 1);
#endif /* HC_GTK4 */

	exp = gtk_expander_new (_("Details"));
	gtk_widget_set_hexpand (exp, TRUE);
	gtk_grid_attach (GTK_GRID (table), exp, 0, 0, 1, 1);

	detailbox = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (detailbox), 6);
	gtk_grid_set_row_spacing (GTK_GRID (detailbox), 2);
#if !HC_GTK4
	hc_container_set_border_width (detailbox, 6);
#endif
	g_signal_connect (G_OBJECT (exp), "activate",
							G_CALLBACK (dcc_exp_cb), detailbox);
	gtk_widget_set_hexpand (detailbox, TRUE);
	gtk_grid_attach (GTK_GRID (table), detailbox, 0, 1, 4, 1);

	dccfwin.file_label = dcc_detail_label (_("File:"), detailbox, 0);
	dccfwin.address_label = dcc_detail_label (_("Address:"), detailbox, 1);

	bbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (bbox, GTK_BUTTONBOX_SPREAD);
	hc_box_pack_end (vbox, bbox, FALSE, FALSE, 2);

	dccfwin.abort_button = gtkutil_button (bbox, "process-stop", 0, abort_clicked, 0, _("Abort"));
	dccfwin.accept_button = gtkutil_button (bbox, "emblem-default", 0, accept_clicked, 0, _("Accept"));
	dccfwin.resume_button = gtkutil_button (bbox, "view-refresh", 0, resume_clicked, 0, _("Resume"));
	dccfwin.clear_button = gtkutil_button (bbox, "edit-clear", 0, clear_completed, 0, _("Clear"));
	dccfwin.open_button = gtkutil_button (bbox, 0, 0, browse_dcc_folder, 0, _("Open Folder..."));
	gtk_widget_set_sensitive (dccfwin.accept_button, FALSE);
	gtk_widget_set_sensitive (dccfwin.resume_button, FALSE);
	gtk_widget_set_sensitive (dccfwin.abort_button, FALSE);

	dcc_fill_window (3);
	hc_widget_show_all (dccfwin.window);
	hc_widget_hide (detailbox);

	return FALSE;
}

int
fe_dcc_open_send_win (int passive)
{
	/* combined send/recv GUI */
	return fe_dcc_open_recv_win (passive);
}


/* DCC CHAT GUIs BELOW */

#if HC_GTK4
static GSList *
dcc_chat_get_selected_gtk4 (void)
{
	GSList *list = NULL;
	GtkBitset *selection;
	GtkBitsetIter biter;
	guint pos;

	if (!dcccwin.sel_model || !dcc_chat_store)
		return NULL;

	selection = gtk_selection_model_get_selection (dcccwin.sel_model);
	if (gtk_bitset_iter_init_first (&biter, selection, &pos))
	{
		do
		{
			HcDccChatItem *item = g_list_model_get_item (G_LIST_MODEL (dcc_chat_store), pos);
			if (item)
			{
				list = g_slist_prepend (list, item->dcc);
				g_object_unref (item);
			}
		}
		while (gtk_bitset_iter_next (&biter, &pos));
	}
	gtk_bitset_unref (selection);

	return g_slist_reverse (list);
}

static GSList *
dcc_chat_get_selected (void)
{
	return dcc_chat_get_selected_gtk4 ();
}
#else /* GTK3 */
static GSList *
dcc_chat_get_selected (void)
{
	return treeview_get_selected (GTK_TREE_MODEL (dcccwin.store),
											dcccwin.sel, CCOL_DCC);
}
#endif /* HC_GTK4 */

static void
accept_chat_clicked (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	GSList *start, *list;

	start = list = dcc_chat_get_selected ();
	for (; list; list = list->next)
	{
		dcc = list->data;
		dcc_get (dcc);
	}
	g_slist_free (start);
}

static void
abort_chat_clicked (GtkWidget * wid, gpointer none)
{
	struct DCC *dcc;
	GSList *start, *list;

	start = list = dcc_chat_get_selected ();
	for (; list; list = list->next)
	{
		dcc = list->data;
		dcc_abort (dcc->serv->front_session, dcc);
	}
	g_slist_free (start);
}

static void
dcc_chat_close_cb (void)
{
	dcccwin.window = NULL;
}

#if HC_GTK4
static void
dcc_chat_append_gtk4 (struct DCC *dcc, gboolean prepend)
{
	HcDccChatItem *item = dcc_prepare_chat_item (dcc);
	if (prepend)
		g_list_store_insert (dcc_chat_store, 0, item);
	else
		g_list_store_append (dcc_chat_store, item);
	g_object_unref (item);
}
#else /* GTK3 */
static void
dcc_chat_append (struct DCC *dcc, GtkListStore *store, gboolean prepend)
{
	GtkTreeIter iter;

	if (prepend)
		gtk_list_store_prepend (store, &iter);
	else
		gtk_list_store_append (store, &iter);

	dcc_prepare_row_chat (dcc, store, &iter, FALSE);
}
#endif /* HC_GTK4 */

static void
dcc_chat_fill_win (void)
{
	struct DCC *dcc;
	GSList *list;
	int i = 0;

#if HC_GTK4
	if (!dcc_chat_store)
		return;

	g_list_store_remove_all (dcc_chat_store);

	list = dcc_list;
	while (list)
	{
		dcc = list->data;
		if (dcc->type == TYPE_CHATSEND || dcc->type == TYPE_CHATRECV)
		{
			dcc_chat_append_gtk4 (dcc, FALSE);
			i++;
		}
		list = list->next;
	}

	/* if only one entry, select it (so Accept button can work) */
	if (i == 1)
		gtk_selection_model_select_item (dcccwin.sel_model, 0, TRUE);
#else /* GTK3 */
	GtkTreeIter iter;

	gtk_list_store_clear (GTK_LIST_STORE (dcccwin.store));

	list = dcc_list;
	while (list)
	{
		dcc = list->data;
		if (dcc->type == TYPE_CHATSEND || dcc->type == TYPE_CHATRECV)
		{
			dcc_chat_append (dcc, dcccwin.store, FALSE);
			i++;
		}
		list = list->next;
	}

	/* if only one entry, select it (so Accept button can work) */
	if (i == 1)
	{
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dcccwin.store), &iter);
		gtk_tree_selection_select_iter (dcccwin.sel, &iter);
	}
#endif
}

static void
dcc_chat_row_cb_common (void)
{
	struct DCC *dcc;
	GSList *list;

	list = dcc_chat_get_selected ();
	if (!list)
	{
		gtk_widget_set_sensitive (dcccwin.accept_button, FALSE);
		gtk_widget_set_sensitive (dcccwin.abort_button, FALSE);
		return;
	}

	gtk_widget_set_sensitive (dcccwin.abort_button, TRUE);

	if (list->next)	/* multi selection */
		gtk_widget_set_sensitive (dcccwin.accept_button, TRUE);
	else
	{
		/* turn OFF/ON appropriate buttons */
		dcc = list->data;
		if (dcc->dccstat == STAT_QUEUED && dcc->type == TYPE_CHATRECV)
			gtk_widget_set_sensitive (dcccwin.accept_button, TRUE);
		else
			gtk_widget_set_sensitive (dcccwin.accept_button, FALSE);
	}

	g_slist_free (list);
}

#if HC_GTK4
static void
dcc_chat_row_cb (GtkSelectionModel *sel_model, guint position, guint n_items, gpointer user_data)
{
	dcc_chat_row_cb_common ();
}
#else /* GTK3 */
static void
dcc_chat_row_cb (GtkTreeSelection *sel, gpointer user_data)
{
	dcc_chat_row_cb_common ();
}
#endif

#if HC_GTK4
static void
dcc_chat_dclick_cb (GtkColumnView *view, guint position, gpointer data)
{
	accept_chat_clicked (0, 0);
}
#else /* GTK3 */
static void
dcc_chat_dclick_cb (GtkTreeView *view, GtkTreePath *path,
						  GtkTreeViewColumn *column, gpointer data)
{
	accept_chat_clicked (0, 0);
}
#endif

#if HC_GTK4
/*
 * GTK4 Column View factory callbacks for chat list
 */
static void
dcc_chat_setup_text_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_label_new (NULL);
	gboolean right_align = GPOINTER_TO_INT (user_data);
	gtk_label_set_xalign (GTK_LABEL (label), right_align ? 1.0 : 0.0);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_list_item_set_child (item, label);
}

static void
dcc_chat_bind_status_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccChatItem *chat_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), chat_item->status);
	dcc_apply_colour (label, chat_item->colour);
}

static void
dcc_chat_bind_nick_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccChatItem *chat_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), chat_item->nick);
	dcc_apply_colour (label, chat_item->colour);
}

static void
dcc_chat_bind_recv_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccChatItem *chat_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), chat_item->recv);
	dcc_apply_colour (label, chat_item->colour);
}

static void
dcc_chat_bind_sent_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccChatItem *chat_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), chat_item->sent);
	dcc_apply_colour (label, chat_item->colour);
}

static void
dcc_chat_bind_start_cb (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{
	GtkWidget *label = gtk_list_item_get_child (item);
	HcDccChatItem *chat_item = gtk_list_item_get_item (item);
	gtk_label_set_text (GTK_LABEL (label), chat_item->start);
	dcc_apply_colour (label, chat_item->colour);
}

static GtkWidget *
dcc_chat_columnview_new (GtkWidget *vbox)
{
	GtkWidget *scroll;
	GtkWidget *view;
	GtkColumnViewColumn *col;
	GtkListItemFactory *factory;

	scroll = hc_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scroll, TRUE);

	/* Create list store */
	dcc_chat_store = g_list_store_new (HC_TYPE_DCC_CHAT_ITEM);

	/* Create column view with multi-selection */
	view = hc_column_view_new_simple (G_LIST_MODEL (dcc_chat_store), GTK_SELECTION_MULTIPLE);
	gtk_column_view_set_show_column_separators (GTK_COLUMN_VIEW (view), TRUE);
	gtk_column_view_set_show_row_separators (GTK_COLUMN_VIEW (view), TRUE);

	/* Save selection model reference */
	dcccwin.sel_model = gtk_column_view_get_model (GTK_COLUMN_VIEW (view));

	/* Status column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_chat_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_chat_bind_status_cb), NULL);
	col = gtk_column_view_column_new (_("Status"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Nick column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_chat_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_chat_bind_nick_cb), NULL);
	col = gtk_column_view_column_new (_("Nick"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_column_set_expand (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Recv column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_chat_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_chat_bind_recv_cb), NULL);
	col = gtk_column_view_column_new (_("Recv"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Sent column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_chat_setup_text_cb), GINT_TO_POINTER (TRUE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_chat_bind_sent_cb), NULL);
	col = gtk_column_view_column_new (_("Sent"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	/* Start Time column */
	factory = gtk_signal_list_item_factory_new ();
	g_signal_connect (factory, "setup", G_CALLBACK (dcc_chat_setup_text_cb), GINT_TO_POINTER (FALSE));
	g_signal_connect (factory, "bind", G_CALLBACK (dcc_chat_bind_start_cb), NULL);
	col = gtk_column_view_column_new (_("Start Time"), factory);
	gtk_column_view_column_set_resizable (col, TRUE);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (view), col);
	g_object_unref (col);

	hc_scrolled_window_set_child (scroll, view);
	hc_box_pack_start (vbox, scroll, TRUE, TRUE, 0);

	/* Connect selection changed signal */
	g_signal_connect (dcccwin.sel_model, "selection-changed",
	                  G_CALLBACK (dcc_chat_row_cb), NULL);

	/* Double-click activation */
	g_signal_connect (view, "activate",
	                  G_CALLBACK (dcc_chat_dclick_cb), NULL);

	return view;
}
#endif /* HC_GTK4 */

int
fe_dcc_open_chat_win (int passive)
{
	GtkWidget *view, *vbox, *bbox;
#if !HC_GTK4
	GtkListStore *store;
#endif
	char buf[128];

	if (dcccwin.window)
	{
		if (!passive)
			mg_bring_tofront (dcccwin.window);
		return TRUE;
	}

	g_snprintf(buf, sizeof(buf), _("DCC Chat List - %s"), _(DISPLAY_NAME));
	dcccwin.window =
			  mg_create_generic_tab ("DCCChat", buf, FALSE, TRUE, dcc_chat_close_cb,
						NULL, 550, 180, &vbox, 0);
	gtkutil_destroy_on_esc (dcccwin.window);
#if !HC_GTK4
	hc_container_set_border_width (dcccwin.window, 3);
#endif
	gtk_box_set_spacing (GTK_BOX (vbox), 3);

#if HC_GTK4
	view = dcc_chat_columnview_new (vbox);
	dcccwin.list = view;
#else /* GTK3 */
	store = gtk_list_store_new (CN_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_POINTER, GDK_TYPE_RGBA);
	view = gtkutil_treeview_new (vbox, GTK_TREE_MODEL (store), NULL, -1);

	dcc_add_column (view, CCOL_STATUS, CCOL_COLOR, _("Status"), FALSE);
	dcc_add_column (view, CCOL_NICK,   CCOL_COLOR, _("Nick"), FALSE);
	dcc_add_column (view, CCOL_RECV,   CCOL_COLOR, _("Recv"), TRUE);
	dcc_add_column (view, CCOL_SENT,   CCOL_COLOR, _("Sent"), TRUE);
	dcc_add_column (view, CCOL_START,  CCOL_COLOR, _("Start Time"), FALSE);

	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), 1), TRUE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);

	dcccwin.list = view;
	dcccwin.store = store;
	dcccwin.sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	gtk_tree_selection_set_mode (dcccwin.sel, GTK_SELECTION_MULTIPLE);

	g_signal_connect (G_OBJECT (dcccwin.sel), "changed",
							G_CALLBACK (dcc_chat_row_cb), NULL);
	/* double click */
	g_signal_connect (G_OBJECT (view), "row-activated",
							G_CALLBACK (dcc_chat_dclick_cb), NULL);
#endif /* HC_GTK4 */

	bbox = hc_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	hc_button_box_set_layout (bbox, GTK_BUTTONBOX_SPREAD);
	hc_box_pack_end (vbox, bbox, FALSE, FALSE, 2);

	dcccwin.abort_button = gtkutil_button (bbox, "process-stop", 0, abort_chat_clicked, 0, _("Abort"));
	dcccwin.accept_button = gtkutil_button (bbox, "emblem-default", 0, accept_chat_clicked, 0, _("Accept"));
	gtk_widget_set_sensitive (dcccwin.accept_button, FALSE);
	gtk_widget_set_sensitive (dcccwin.abort_button, FALSE);

	dcc_chat_fill_win ();
	hc_widget_show_all (dcccwin.window);

	return FALSE;
}

void
fe_dcc_add (struct DCC *dcc)
{
	switch (dcc->type)
	{
	case TYPE_RECV:
		if (dccfwin.window && (view_mode & VIEW_DOWNLOAD))
#if HC_GTK4
			dcc_append_file_gtk4 (dcc, TRUE);
#else
			dcc_append (dcc, dccfwin.store, TRUE);
#endif
		break;

	case TYPE_SEND:
		if (dccfwin.window && (view_mode & VIEW_UPLOAD))
#if HC_GTK4
			dcc_append_file_gtk4 (dcc, TRUE);
#else
			dcc_append (dcc, dccfwin.store, TRUE);
#endif
		break;

	default: /* chat */
		if (dcccwin.window)
#if HC_GTK4
			dcc_chat_append_gtk4 (dcc, TRUE);
#else
			dcc_chat_append (dcc, dcccwin.store, TRUE);
#endif
	}
}

void
fe_dcc_update (struct DCC *dcc)
{
	switch (dcc->type)
	{
	case TYPE_SEND:
		dcc_update_send (dcc);
		break;

	case TYPE_RECV:
		dcc_update_recv (dcc);
		break;

	default:
		dcc_update_chat (dcc);
	}

	if (dccfwin.window)
		update_clear_button_sensitivity();
}

void
fe_dcc_remove (struct DCC *dcc)
{
#if HC_GTK4
	int pos;

	switch (dcc->type)
	{
	case TYPE_SEND:
	case TYPE_RECV:
		if (dccfwin.window && dcc_file_store)
		{
			pos = dcc_find_file_item (dcc);
			if (pos >= 0)
				g_list_store_remove (dcc_file_store, pos);
		}
		break;

	default:	/* chat */
		if (dcccwin.window && dcc_chat_store)
		{
			pos = dcc_find_chat_item (dcc);
			if (pos >= 0)
				g_list_store_remove (dcc_chat_store, pos);
		}
		break;
	}
#else /* GTK3 */
	GtkTreeIter iter;

	switch (dcc->type)
	{
	case TYPE_SEND:
	case TYPE_RECV:
		if (dccfwin.window)
		{
			if (dcc_find_row (dcc, GTK_TREE_MODEL (dccfwin.store), &iter, COL_DCC))
				gtk_list_store_remove (dccfwin.store, &iter);
		}
		break;

	default:	/* chat */
		if (dcccwin.window)
		{
			if (dcc_find_row (dcc, GTK_TREE_MODEL (dcccwin.store), &iter, CCOL_DCC))
				gtk_list_store_remove (dcccwin.store, &iter);
		}
		break;
	}
#endif
}
