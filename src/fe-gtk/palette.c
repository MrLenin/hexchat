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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"
#include "palette.h"

#include "../common/hexchat.h"
#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "../common/typedef.h"

/* Helper macro to convert 16-bit color (0-65535) to gdouble (0.0-1.0) */
#define C16(x) ((x) / 65535.0)

GdkRGBA colors[] = {
	/* colors for xtext - GdkRGBA uses {red, green, blue, alpha} with 0.0-1.0 range */
	{C16(0xd3d3), C16(0xd7d7), C16(0xcfcf), 1.0}, /* 0 white */
	{C16(0x2e2e), C16(0x3434), C16(0x3636), 1.0}, /* 1 black */
	{C16(0x3434), C16(0x6565), C16(0xa4a4), 1.0}, /* 2 blue */
	{C16(0x4e4e), C16(0x9a9a), C16(0x0606), 1.0}, /* 3 green */
	{C16(0xcccc), C16(0x0000), C16(0x0000), 1.0}, /* 4 red */
	{C16(0x8f8f), C16(0x3939), C16(0x0202), 1.0}, /* 5 light red */
	{C16(0x5c5c), C16(0x3535), C16(0x6666), 1.0}, /* 6 purple */
	{C16(0xcece), C16(0x5c5c), C16(0x0000), 1.0}, /* 7 orange */
	{C16(0xc4c4), C16(0xa0a0), C16(0x0000), 1.0}, /* 8 yellow */
	{C16(0x7373), C16(0xd2d2), C16(0x1616), 1.0}, /* 9 green */
	{C16(0x1111), C16(0xa8a8), C16(0x7979), 1.0}, /* 10 aqua */
	{C16(0x5858), C16(0xa1a1), C16(0x9d9d), 1.0}, /* 11 light aqua */
	{C16(0x5757), C16(0x7979), C16(0x9e9e), 1.0}, /* 12 blue */
	{C16(0xa0d0), C16(0x42d4), C16(0x6562), 1.0}, /* 13 light purple */
	{C16(0x5555), C16(0x5757), C16(0x5353), 1.0}, /* 14 grey */
	{C16(0x8888), C16(0x8a8a), C16(0x8585), 1.0}, /* 15 light grey */

	{C16(0xd3d3), C16(0xd7d7), C16(0xcfcf), 1.0}, /* 16 white */
	{C16(0x2e2e), C16(0x3434), C16(0x3636), 1.0}, /* 17 black */
	{C16(0x3434), C16(0x6565), C16(0xa4a4), 1.0}, /* 18 blue */
	{C16(0x4e4e), C16(0x9a9a), C16(0x0606), 1.0}, /* 19 green */
	{C16(0xcccc), C16(0x0000), C16(0x0000), 1.0}, /* 20 red */
	{C16(0x8f8f), C16(0x3939), C16(0x0202), 1.0}, /* 21 light red */
	{C16(0x5c5c), C16(0x3535), C16(0x6666), 1.0}, /* 22 purple */
	{C16(0xcece), C16(0x5c5c), C16(0x0000), 1.0}, /* 23 orange */
	{C16(0xc4c4), C16(0xa0a0), C16(0x0000), 1.0}, /* 24 yellow */
	{C16(0x7373), C16(0xd2d2), C16(0x1616), 1.0}, /* 25 green */
	{C16(0x1111), C16(0xa8a8), C16(0x7979), 1.0}, /* 26 aqua */
	{C16(0x5858), C16(0xa1a1), C16(0x9d9d), 1.0}, /* 27 light aqua */
	{C16(0x5757), C16(0x7979), C16(0x9e9e), 1.0}, /* 28 blue */
	{C16(0xa0d0), C16(0x42d4), C16(0x6562), 1.0}, /* 29 light purple */
	{C16(0x5555), C16(0x5757), C16(0x5353), 1.0}, /* 30 grey */
	{C16(0x8888), C16(0x8a8a), C16(0x8585), 1.0}, /* 31 light grey */

	{C16(0xd3d3), C16(0xd7d7), C16(0xcfcf), 1.0}, /* 32 marktext Fore (white) */
	{C16(0x2020), C16(0x4a4a), C16(0x8787), 1.0}, /* 33 marktext Back (blue) */
	{C16(0x2512), C16(0x29e8), C16(0x2b85), 1.0}, /* 34 foreground (black) */
	{C16(0xfae0), C16(0xfae0), C16(0xf8c4), 1.0}, /* 35 background (white) */
	{C16(0x8f8f), C16(0x3939), C16(0x0202), 1.0}, /* 36 marker line (red) */

	/* colors for GUI */
	{C16(0x3434), C16(0x6565), C16(0xa4a4), 1.0}, /* 37 tab New Data (dark red) */
	{C16(0x4e4e), C16(0x9a9a), C16(0x0606), 1.0}, /* 38 tab Nick Mentioned (blue) */
	{C16(0xcece), C16(0x5c5c), C16(0x0000), 1.0}, /* 39 tab New Message (red) */
	{C16(0x8888), C16(0x8a8a), C16(0x8585), 1.0}, /* 40 away user (grey) */
	{C16(0xa4a4), C16(0x0000), C16(0x0000), 1.0}, /* 41 spell checker color (red) */
};

#undef C16

void
palette_alloc (GtkWidget * widget)
{
	/* In GTK3+, GdkRGBA colors don't need allocation.
	 * This function is kept for API compatibility but does nothing. */
	(void)widget;
}

void
palette_load (void)
{
	int i, j, fh;
	char prefname[256];
	struct stat st;
	char *cfg;
	guint16 red, green, blue;

	fh = hexchat_open_file ("colors.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		fstat (fh, &st);
		cfg = g_malloc0 (st.st_size + 1);
		read (fh, cfg, st.st_size);

		/* mIRC colors 0-31 are here */
		for (i = 0; i < 32; i++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_get_color (cfg, prefname, &red, &green, &blue);
			/* Convert 16-bit values to 0.0-1.0 range */
			colors[i].red = red / 65535.0;
			colors[i].green = green / 65535.0;
			colors[i].blue = blue / 65535.0;
			colors[i].alpha = 1.0;
		}

		/* our special colors are mapped at 256+ */
		for (i = 256, j = 32; j < MAX_COL+1; i++, j++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_get_color (cfg, prefname, &red, &green, &blue);
			/* Convert 16-bit values to 0.0-1.0 range */
			colors[j].red = red / 65535.0;
			colors[j].green = green / 65535.0;
			colors[j].blue = blue / 65535.0;
			colors[j].alpha = 1.0;
		}
		g_free (cfg);
		close (fh);
	}
}

void
palette_save (void)
{
	int i, j, fh;
	char prefname[256];

	fh = hexchat_open_file ("colors.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		/* mIRC colors 0-31 are here */
		/* Convert 0.0-1.0 back to 16-bit for file storage */
		for (i = 0; i < 32; i++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_put_color (fh,
				(guint16)(colors[i].red * 65535.0),
				(guint16)(colors[i].green * 65535.0),
				(guint16)(colors[i].blue * 65535.0),
				prefname);
		}

		/* our special colors are mapped at 256+ */
		for (i = 256, j = 32; j < MAX_COL+1; i++, j++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_put_color (fh,
				(guint16)(colors[j].red * 65535.0),
				(guint16)(colors[j].green * 65535.0),
				(guint16)(colors[j].blue * 65535.0),
				prefname);
		}

		close (fh);
	}
}
