/* AlsaMixer - Commandline mixer for the ALSA project
 * Copyright (C) 1998 Jaroslav Kysela <perex@jcu.cz>,
 *                    Tim Janik <timj@gtk.org>,
 *                    Carl van Schaik <carl@dreamcoat.che.uct.ac.za>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <errno.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>

#ifndef CURSESINC
#include <ncurses.h>
#else
#include CURSESINC
#endif
#include <time.h>

#include <sys/asoundlib.h>

/* example compilation commandline:
 * clear; gcc -Wall -pipe -O2 alsamixer.c -o alsamixer -lncurses
 */

/* --- defines --- */
#define	PRGNAME "alsamixer"
#define	PRGNAME_UPPER "AlsaMixer"
#define	VERSION "v0.9"

#undef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#undef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#undef ABS
#define ABS(a)     (((a) < 0) ? -(a) : (a))
#undef CLAMP
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define MIXER_MIN_X	(23)	/* minimum: 23 */
#define	MIXER_MIN_Y	(19)	/* minimum: 19 */

#define MIXER_BLACK	(COLOR_BLACK)
#define MIXER_DARK_RED  (COLOR_RED)
#define MIXER_RED       (COLOR_RED | A_BOLD)
#define MIXER_GREEN     (COLOR_GREEN | A_BOLD)
#define MIXER_ORANGE    (COLOR_YELLOW)
#define MIXER_YELLOW    (COLOR_YELLOW | A_BOLD)
#define MIXER_MARIN     (COLOR_BLUE)
#define MIXER_BLUE      (COLOR_BLUE | A_BOLD)
#define MIXER_MAGENTA   (COLOR_MAGENTA)
#define MIXER_DARK_CYAN (COLOR_CYAN)
#define MIXER_CYAN      (COLOR_CYAN | A_BOLD)
#define MIXER_GREY      (COLOR_WHITE)
#define MIXER_GRAY      (MIXER_GREY)
#define MIXER_WHITE     (COLOR_WHITE | A_BOLD)


/* --- variables --- */
static WINDOW *mixer_window = NULL;
static int mixer_max_x = 0;
static int mixer_max_y = 0;
static int mixer_ofs_x = 0;
static float mixer_extra_space = 0;
static int mixer_ofs_y = 0;
static int mixer_cbar_height = 0;

static int card_id = 0;
static int mixer_id = 0;
static void *mixer_handle;
static char *mixer_card_name = NULL;
static char *mixer_device_name = NULL;

static int mixer_n_channels = 0;
static int mixer_n_vis_channels = 0;
static int mixer_first_vis_channel = 0;
static int mixer_focus_channel = 0;
static int mixer_exact = 0;

static int mixer_input_volumes = 0;

static int mixer_lvolume_delta = 0;
static int mixer_rvolume_delta = 0;
static int mixer_balance_volumes = 0;
static int mixer_toggle_mute_left = 0;
static int mixer_toggle_mute_right = 0;

/* By Carl */
static int mixer_toggle_record_left = 0;
static int mixer_toggle_record_right = 0;
static int mixer_route_ltor_in = 0;
static int mixer_route_rtol_in = 0;
#if 0
static int mixer_route_ltor_out = 0;
static int mixer_route_rtol_out = 0;
#endif


/* --- draw contexts --- */
enum {
	DC_DEFAULT,
	DC_BACK,
	DC_TEXT,
	DC_PROMPT,
	DC_CBAR_MUTE,
	DC_CBAR_NOMUTE,
	DC_CBAR_RECORD,
	DC_CBAR_NORECORD,
	DC_CBAR_EMPTY,
	DC_CBAR_FULL_1,
	DC_CBAR_FULL_2,
	DC_CBAR_FULL_3,
	DC_CBAR_LABEL,
	DC_CBAR_FOCUS_LABEL,
	DC_FOCUS,
	DC_LAST
};

static int dc_fg[DC_LAST] =
{0};
static int dc_attrib[DC_LAST] =
{0};
static int dc_char[DC_LAST] =
{0};
static int mixer_do_color = 1;

static void mixer_init_dc(int c,
			  int n,
			  int f,
			  int b,
			  int a)
{
	dc_fg[n] = f;
	dc_attrib[n] = a;
	dc_char[n] = c;
	if (n > 0)
		init_pair(n, dc_fg[n] & 0xf, b & 0x0f);
}

static int mixer_dc(int n)
{
	if (mixer_do_color)
		attrset(COLOR_PAIR(n) | (dc_fg[n] & 0xfffffff0));
	else
		attrset(dc_attrib[n]);

	return dc_char[n];
}

static void mixer_init_draw_contexts(void)
{
	start_color();

	mixer_init_dc('.', DC_BACK, MIXER_WHITE, MIXER_BLACK, A_NORMAL);
	mixer_init_dc('.', DC_TEXT, MIXER_YELLOW, MIXER_BLACK, A_BOLD);
	mixer_init_dc('.', DC_PROMPT, MIXER_DARK_CYAN, MIXER_BLACK, A_NORMAL);
	mixer_init_dc('M', DC_CBAR_MUTE, MIXER_CYAN, MIXER_BLACK, A_BOLD);
	mixer_init_dc('-', DC_CBAR_NOMUTE, MIXER_CYAN, MIXER_BLACK, A_NORMAL);
	mixer_init_dc('x', DC_CBAR_RECORD, MIXER_DARK_RED, MIXER_BLACK, A_BOLD);
	mixer_init_dc('-', DC_CBAR_NORECORD, MIXER_GRAY, MIXER_BLACK, A_NORMAL);
	mixer_init_dc(' ', DC_CBAR_EMPTY, MIXER_GRAY, MIXER_BLACK, A_DIM);
	mixer_init_dc('#', DC_CBAR_FULL_1, MIXER_WHITE, MIXER_BLACK, A_BOLD);
	mixer_init_dc('#', DC_CBAR_FULL_2, MIXER_GREEN, MIXER_BLACK, A_BOLD);
	mixer_init_dc('#', DC_CBAR_FULL_3, MIXER_RED, MIXER_BLACK, A_BOLD);
	mixer_init_dc('.', DC_CBAR_LABEL, MIXER_WHITE, MIXER_BLUE, A_REVERSE | A_BOLD);
	mixer_init_dc('.', DC_CBAR_FOCUS_LABEL, MIXER_RED, MIXER_BLUE, A_REVERSE | A_BOLD);
	mixer_init_dc('.', DC_FOCUS, MIXER_RED, MIXER_BLACK, A_BOLD);
}

#define	DC_CBAR_FRAME	(DC_CBAR_MUTE)
#define	DC_FRAME	(DC_PROMPT)


/* --- error types --- */
typedef enum {
	ERR_NONE,
	ERR_OPEN,
	ERR_FCN,
	ERR_SIGNAL,
	ERR_WINSIZE,
} ErrType;


/* --- prototypes --- */
static void mixer_abort(ErrType error,
			const char *err_string)
 __attribute__
 ((noreturn));


/* --- functions --- */
static void mixer_clear(void)
{
	int x, y;

	mixer_dc(DC_BACK);
	clear();

	/* buggy ncurses doesn't really write spaces with the specified
	 * color into the screen on clear ();
	 */
	for (x = 0; x < mixer_max_x; x++)
		for (y = 0; y < mixer_max_y; y++)
			mvaddch(y, x, ' ');
	refresh();
}

static void mixer_abort(ErrType error,
			const char *err_string)
{
	if (mixer_window) {
		mixer_clear();
		endwin();
		mixer_window = NULL;
	}
	printf("\n");

	switch (error) {
	case ERR_OPEN:
		fprintf(stderr,
			PRGNAME ": failed to open mixer #%i/#%i: %s\n",
			card_id,
			mixer_id,
			snd_strerror(errno));
		break;
	case ERR_FCN:
		fprintf(stderr,
			PRGNAME ": function %s failed: %s\n",
			err_string,
			snd_strerror(errno));
		break;
	case ERR_SIGNAL:
		fprintf(stderr,
			PRGNAME ": aborting due to signal `%s'\n",
			err_string);
		break;
	case ERR_WINSIZE:
		fprintf(stderr,
			PRGNAME ": screen size too small (%dx%d)\n",
			mixer_max_x,
			mixer_max_y);
		break;
	default:
		break;
	}

	exit(error);
}

static int mixer_cbar_get_pos(int channel_index,
			      int *x_p,
			      int *y_p)
{
	int x;
	int y;

	if (channel_index < mixer_first_vis_channel ||
	 channel_index - mixer_first_vis_channel >= mixer_n_vis_channels)
		return FALSE;

	channel_index -= mixer_first_vis_channel;

	x = mixer_ofs_x + 1;
	y = mixer_ofs_y;
	x += channel_index * (3 + 2 + 3 + 1 + mixer_extra_space);
	y += mixer_max_y / 2;
	y += mixer_cbar_height / 2 + 1;

	if (x_p)
		*x_p = x;
	if (y_p)
		*y_p = y;

	return TRUE;
}

static void mixer_update_cbar(int channel_index)
{
	char string[64];
	char c;
	snd_mixer_channel_info_t cinfo =
	{0};
	snd_mixer_channel_direction_info_t cpinfo =
	{0};
	snd_mixer_channel_direction_info_t crinfo =
	{0};
	snd_mixer_channel_direction_t cpdata =
	{0};
	snd_mixer_channel_direction_t crdata =
	{0};
	int vleft, vright;
	int x, y, i;
	int output = 0, input = 0, volume;

	/* set specified EXACT mode
	 */
	if (snd_mixer_exact_mode(mixer_handle, mixer_exact) < 0)
		mixer_abort(ERR_FCN, "snd_mixer_exact");

	/* set new channel indices and read info
	 */
	if (snd_mixer_channel_info(mixer_handle, channel_index, &cinfo) < 0)
		mixer_abort(ERR_FCN, "snd_mixer_channel_info");
	if (cinfo.caps & SND_MIXER_CINFO_CAP_OUTPUT) {
		if (snd_mixer_channel_output_info(mixer_handle, channel_index, &cpinfo) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_output_info");
		output = 1;
	}
	if (cinfo.caps & SND_MIXER_CINFO_CAP_INPUT) {
		if (snd_mixer_channel_input_info(mixer_handle, channel_index, &crinfo) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_input_info");
		input = 1;
	}
	if (mixer_input_volumes)
		volume=(input && (crinfo.caps & SND_MIXER_CINFO_DCAP_VOLUME));
	else
		volume=(output && (cpinfo.caps & SND_MIXER_CINFO_DCAP_VOLUME));

	/* set new channel values
	 */
	if (channel_index == mixer_focus_channel &&
	    (mixer_lvolume_delta || mixer_rvolume_delta ||
	     mixer_toggle_mute_left || mixer_toggle_mute_right ||
	     mixer_balance_volumes ||
	     mixer_toggle_record_left || mixer_toggle_record_right ||
	     mixer_route_rtol_in || mixer_route_ltor_in)) {
		if (output && snd_mixer_channel_output_read(mixer_handle, channel_index, &cpdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_output_read");
		if (input && snd_mixer_channel_input_read(mixer_handle, channel_index, &crdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_input_read");

		cpdata.flags &= ~SND_MIXER_DFLG_DECIBEL;
		crdata.flags &= ~SND_MIXER_DFLG_DECIBEL;
		if (volume) {
			if (mixer_input_volumes) {
				crdata.left = CLAMP(crdata.left + mixer_lvolume_delta, crinfo.min, crinfo.max);
				crdata.right = CLAMP(crdata.right + mixer_rvolume_delta, crinfo.min, crinfo.max);
				if (mixer_balance_volumes) {
					crdata.left = (crdata.left + crdata.right) / 2;
					crdata.right = crdata.left;
				}
			}
			else {
				cpdata.left = CLAMP(cpdata.left + mixer_lvolume_delta, cpinfo.min, cpinfo.max);
				cpdata.right = CLAMP(cpdata.right + mixer_rvolume_delta, cpinfo.min, cpinfo.max);
				if (mixer_balance_volumes) {
					cpdata.left = (cpdata.left + cpdata.right) / 2;
					cpdata.right = cpdata.left;
				}
			}
		}
		mixer_lvolume_delta = 0;
		mixer_rvolume_delta = 0;
		mixer_balance_volumes = 0;

		if (output) {
			if (mixer_toggle_mute_left) {
				cpdata.flags ^= SND_MIXER_DFLG_MUTE_LEFT;
			}
			if (mixer_toggle_mute_right) {
				cpdata.flags ^= SND_MIXER_DFLG_MUTE_RIGHT;
			}
		}
		mixer_toggle_mute_left = mixer_toggle_mute_right = 0;

		if (input) {
			if (mixer_toggle_record_left) {
				crdata.flags ^= SND_MIXER_DFLG_MUTE_LEFT;
			}
			if (mixer_toggle_record_right) {
				crdata.flags ^= SND_MIXER_DFLG_MUTE_RIGHT;
			}

			if (mixer_route_ltor_in) {
				crdata.flags ^= SND_MIXER_DFLG_LTOR;
			}
			if (mixer_route_rtol_in) {
				crdata.flags ^= SND_MIXER_DFLG_RTOL;
			}
		}
		mixer_toggle_record_left = mixer_toggle_record_right = 0;
		mixer_route_ltor_in = mixer_route_rtol_in = 0;

		if (output &&
		    snd_mixer_channel_output_write(mixer_handle, channel_index, &cpdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_output_write");
		if (input &&
		    snd_mixer_channel_input_write(mixer_handle, channel_index, &crdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_input_write");
	}
	/* first, read values for the numbers to be displayed in
	 * specified EXACT mode
	 */
	if (output &&
	    snd_mixer_channel_output_read(mixer_handle, channel_index, &cpdata) < 0)
		mixer_abort(ERR_FCN, "snd_mixer_ioctl_channel_output_read");
	if (input &&
	    snd_mixer_channel_input_read(mixer_handle, channel_index, &crdata) < 0)
		mixer_abort(ERR_FCN, "snd_mixer_channel_input_read");
	if (mixer_input_volumes) {
		if (input) {
			vleft = crdata.left;
			vright = crdata.right;
		}
		else {
			vleft = vright = 0;
		}
	}
	else {
		if (output) {
			vleft = cpdata.left;
			vright = cpdata.right;
		}
		else {
			vleft = vright = 0;
		}
	}

	/* then, always use percentage values for the bars. if we don't do
	 * this, we will see aliasing effects on specific circumstances.
	 * (actually they don't really dissapear, but they are transfered
	 *  to bar<->smaller-scale ambiguities).
	 */
	if (mixer_exact) {
		i = 0;
		if (snd_mixer_exact_mode(mixer_handle, 0) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_exact");
		if (output &&
		    snd_mixer_channel_output_read(mixer_handle, channel_index, &cpdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_output_read");
		if (input &&
		    snd_mixer_channel_input_read(mixer_handle, channel_index, &crdata) < 0)
			mixer_abort(ERR_FCN, "snd_mixer_channel_input_read");
	}
	/* get channel bar position
	 */
	if (!mixer_cbar_get_pos(channel_index, &x, &y))
		return;

	/* channel bar name
	 */
	mixer_dc(channel_index == mixer_focus_channel ? DC_CBAR_FOCUS_LABEL : DC_CBAR_LABEL);
	cinfo.name[8] = 0;
	for (i = 0; i < 8; i++) {
		string[i] = ' ';
	}
	sprintf(string + (8 - strlen(cinfo.name)) / 2, "%s          ", cinfo.name);
	string[8] = 0;
	mvaddstr(y, x, string);
	y--;

	/* current channel values
	 */
	mixer_dc(DC_BACK);
	mvaddstr(y, x, "         ");
	mixer_dc(DC_TEXT);
	sprintf(string, "%d", vleft);
	mvaddstr(y, x + 3 - strlen(string), string);
	mixer_dc(DC_CBAR_FRAME);
	mvaddch(y, x + 3, '<');
	mvaddch(y, x + 4, '>');
	mixer_dc(DC_TEXT);
	sprintf(string, "%d", vright);
	mvaddstr(y, x + 5, string);
	y--;

	/* left/right bar
	 */
	mixer_dc(DC_CBAR_FRAME);
	mvaddstr(y, x, "         ");
	mvaddch(y, x + 2, ACS_LLCORNER);
	mvaddch(y, x + 3, ACS_HLINE);
	mvaddch(y, x + 4, ACS_HLINE);
	mvaddch(y, x + 5, ACS_LRCORNER);
	y--;
	for (i = 0; i < mixer_cbar_height; i++) {
		mvaddstr(y - i, x, "         ");
		mvaddch(y - i, x + 2, ACS_VLINE);
		mvaddch(y - i, x + 5, ACS_VLINE);
	}
	string[2] = 0;
	for (i = 0; i < mixer_cbar_height; i++) {
		int dc;

		if (i + 1 >= 0.8 * mixer_cbar_height)
			dc = DC_CBAR_FULL_3;
		else if (i + 1 >= 0.4 * mixer_cbar_height)
			dc = DC_CBAR_FULL_2;
		else
			dc = DC_CBAR_FULL_1;
		mvaddch(y, x + 3, mixer_dc(vleft > i * 100 / mixer_cbar_height ? dc : DC_CBAR_EMPTY));
		mvaddch(y, x + 4, mixer_dc(vright > i * 100 / mixer_cbar_height ? dc : DC_CBAR_EMPTY));
		y--;
	}

	/* muted?
	 */
	mixer_dc(DC_BACK);
	mvaddstr(y, x, "         ");
	if (output) {
		c = (cpinfo.caps & SND_MIXER_CINFO_DCAP_MUTE) ? '-' : ' ';
		mixer_dc(DC_CBAR_FRAME);
		mvaddch(y, x + 2, ACS_ULCORNER);
		mvaddch(y, x + 3, mixer_dc(cpdata.flags & SND_MIXER_DFLG_MUTE_LEFT ?
					   DC_CBAR_MUTE : DC_CBAR_NOMUTE));
		mvaddch(y, x + 4, mixer_dc(cpdata.flags & SND_MIXER_DFLG_MUTE_RIGHT ?
					   DC_CBAR_MUTE : DC_CBAR_NOMUTE));
		mixer_dc(DC_CBAR_FRAME);
		mvaddch(y, x + 5, ACS_URCORNER);
	}
	y--;

	/* record input?
	 */
	mixer_dc(DC_BACK);
	mvaddstr(y, x, "         ");
	if (input) {
		if ((crdata.flags & SND_MIXER_DFLG_MUTE) != SND_MIXER_DFLG_MUTE) {
			mixer_dc(DC_CBAR_RECORD);
			mvaddstr(y, x + 1, "RECORD");
			if (!(crdata.flags & SND_MIXER_DFLG_MUTE_LEFT)) {
				if (crdata.flags & SND_MIXER_DFLG_LTOR)
					mvaddstr(y + 2, x + 6, "L");
				else
					mvaddstr(y + 1, x + 1, "L");
			}
			if (!(crdata.flags & SND_MIXER_DFLG_MUTE_RIGHT)) {
				if (crdata.flags & SND_MIXER_DFLG_RTOL)
					mvaddstr(y + 2, x + 1, "R");
				else
					mvaddstr(y + 1, x + 6, "R");
			}
		} else {
			for (i = 0; i < 6; i++)
				mvaddch(y, x + 1 + i, mixer_dc(DC_CBAR_NORECORD));
		}
	}
	y--;
}

static void mixer_update_cbars(void)
{
	static int o_x = 0;
	static int o_y = 0;
	int i, x, y;

	if (!mixer_cbar_get_pos(mixer_focus_channel, &x, &y)) {
		if (mixer_focus_channel < mixer_first_vis_channel)
			mixer_first_vis_channel = mixer_focus_channel;
		else if (mixer_focus_channel >= mixer_first_vis_channel + mixer_n_vis_channels)
			mixer_first_vis_channel = mixer_focus_channel - mixer_n_vis_channels + 1;
		mixer_cbar_get_pos(mixer_focus_channel, &x, &y);
	}
	for (i = 0; i < mixer_n_vis_channels; i++)
		mixer_update_cbar(i + mixer_first_vis_channel);

	/* draw focused cbar
	 */
	mixer_dc(DC_BACK);
	mvaddstr(o_y, o_x, " ");
	mvaddstr(o_y, o_x + 9, " ");
	o_x = x - 1;
	o_y = y;
	mixer_dc(DC_FOCUS);
	mvaddstr(o_y, o_x, "<");
	mvaddstr(o_y, o_x + 9, ">");
}

static void mixer_draw_frame(void)
{
	char string[128];
	int i;
	int max_len;

	mixer_dc(DC_FRAME);

	/* corners
	 */
	mvaddch(0, 0, ACS_ULCORNER);
	mvaddch(mixer_max_y - 1, 0, ACS_LLCORNER);
	mvaddch(mixer_max_y - 1, mixer_max_x - 1, ACS_LRCORNER);
	mvaddch(0, mixer_max_x - 1, ACS_URCORNER);

	/* lines
	 */
	for (i = 1; i < mixer_max_y - 1; i++) {
		mvaddch(i, 0, ACS_VLINE);
		mvaddch(i, mixer_max_x - 1, ACS_VLINE);
	}
	for (i = 1; i < mixer_max_x - 1; i++) {
		mvaddch(0, i, ACS_HLINE);
		mvaddch(mixer_max_y - 1, i, ACS_HLINE);
	}

	/* program title
	 */
	sprintf(string, "%s %s", PRGNAME_UPPER, VERSION);
	max_len = strlen(string);
	mvaddch(0, mixer_max_x / 2 - max_len / 2 - 1, '[');
	mvaddch(0, mixer_max_x / 2 - max_len / 2 + max_len, ']');
	mixer_dc(DC_TEXT);
	mvaddstr(0, mixer_max_x / 2 - max_len / 2, string);

	/* card name
	 */
	mixer_dc(DC_PROMPT);
	mvaddstr(1, 2, "Card:");
	mixer_dc(DC_TEXT);
	sprintf(string, "%s", mixer_card_name);
	max_len = mixer_max_x - 2 - 6 - 2;
	if (strlen(string) > max_len)
		string[max_len] = 0;
	mvaddstr(1, 2 + 6, string);

	/* device name
	 */
	mixer_dc(DC_PROMPT);
	mvaddstr(2, 2, "Chip: ");
	mixer_dc(DC_TEXT);
	sprintf(string, "%s", mixer_device_name);
	max_len = mixer_max_x - 2 - 6 - 2;
	if (strlen(string) > max_len)
		string[max_len] = 0;
	mvaddstr(2, 2 + 6, string);
	if (mixer_input_volumes)
		mvaddstr(3, 2, "Record mixer");
	else
		mvaddstr(3, 2, "            ");
}

static void mixer_init(void)
{
	static snd_mixer_info_t mixer_info =
	{0};
	static struct snd_ctl_hw_info hw_info;
	void *ctl_handle;

	if (snd_ctl_open(&ctl_handle, card_id) < 0)
		mixer_abort(ERR_OPEN, "snd_ctl_open");
	if (snd_ctl_hw_info(ctl_handle, &hw_info) < 0)
		mixer_abort(ERR_FCN, "snd_ctl_hw_info");
	snd_ctl_close(ctl_handle);
	/* open mixer device
	 */
	if (snd_mixer_open(&mixer_handle, card_id, mixer_id) < 0)
		mixer_abort(ERR_OPEN, "snd_mixer_open");

	/* setup global variables
	 */
	if (snd_mixer_info(mixer_handle, &mixer_info) < 0)
		mixer_abort(ERR_FCN, "snd_mixer_info");
	mixer_n_channels = mixer_info.channels;
	mixer_card_name = hw_info.name;
	mixer_device_name = mixer_info.name;
}

static void mixer_iteration_update(void *dummy, int channel)
{
#if 0
	fprintf(stderr, "*** channel = %i\n", channel);
#endif
	mixer_update_cbar(channel);
	refresh();
}

static int mixer_iteration(void)
{
	snd_mixer_callbacks_t callbacks;
	int key;
	int finished = 0;
	int mixer_fd;
	fd_set in;

	bzero(&callbacks, sizeof(callbacks));
	callbacks.channel_was_changed = mixer_iteration_update;
	callbacks.output_channel_was_changed = mixer_iteration_update;
	callbacks.input_channel_was_changed = mixer_iteration_update;
	mixer_fd = snd_mixer_file_descriptor(mixer_handle);
	while (1) {
		FD_ZERO(&in);
		FD_SET(fileno(stdin), &in);
		FD_SET(mixer_fd, &in);
		if (select(mixer_fd + 1, &in, NULL, NULL, NULL) <= 0)
			return 1;
		if (FD_ISSET(mixer_fd, &in))
			snd_mixer_read(mixer_handle, &callbacks);
		if (FD_ISSET(fileno(stdin), &in))
			break;
	}
	key = getch();
	switch (key) {
	case 27:		/* Escape */
		finished = 1;
		break;
	case 9:		/* Tab */
		mixer_exact = !mixer_exact;
		break;
	case KEY_RIGHT:
	case 'n':
		mixer_focus_channel += 1;
		break;
	case KEY_LEFT:
	case 'p':
		mixer_focus_channel -= 1;
		break;
	case KEY_PPAGE:
		if (mixer_exact) {
			mixer_lvolume_delta = 8;
			mixer_rvolume_delta = 8;
		} else {
			mixer_lvolume_delta = 10;
			mixer_rvolume_delta = 10;
		}
		break;
	case KEY_NPAGE:
		if (mixer_exact) {
			mixer_lvolume_delta = -8;
			mixer_rvolume_delta = -8;
		} else {
			mixer_lvolume_delta = -10;
			mixer_rvolume_delta = -10;
		}
		break;
	case KEY_BEG:
	case KEY_HOME:
		mixer_lvolume_delta = 512;
		mixer_rvolume_delta = 512;
		break;
	case KEY_LL:
	case KEY_END:
		mixer_lvolume_delta = -512;
		mixer_rvolume_delta = -512;
		break;
	case '+':
		mixer_lvolume_delta = 1;
		mixer_rvolume_delta = 1;
		break;
	case '-':
		mixer_lvolume_delta = -1;
		mixer_rvolume_delta = -1;
		break;
	case 'w':
	case KEY_UP:
		mixer_lvolume_delta = 1;
		mixer_rvolume_delta = 1;
	case 'W':
		mixer_lvolume_delta += 1;
		mixer_rvolume_delta += 1;
		break;
	case 'x':
	case KEY_DOWN:
		mixer_lvolume_delta = -1;
		mixer_rvolume_delta = -1;
	case 'X':
		mixer_lvolume_delta += -1;
		mixer_rvolume_delta += -1;
		break;
	case 'q':
		mixer_lvolume_delta = 1;
	case 'Q':
		mixer_lvolume_delta += 1;
		break;
	case 'y':
	case 'z':
		mixer_lvolume_delta = -1;
	case 'Y':
	case 'Z':
		mixer_lvolume_delta += -1;
		break;
	case 'e':
		mixer_rvolume_delta = 1;
	case 'E':
		mixer_rvolume_delta += 1;
		break;
	case 'c':
		mixer_rvolume_delta = -1;
	case 'C':
		mixer_rvolume_delta += -1;
		break;
	case 'm':
	case 'M':
		mixer_input_volumes = 0;
		mixer_toggle_mute_left = 1;
		mixer_toggle_mute_right = 1;
		break;
	case 'b':
	case 'B':
	case '=':
		mixer_balance_volumes = 1;
		break;
	case '<':
	case ',':
		mixer_input_volumes = 0;
		mixer_toggle_mute_left = 1;
		break;
	case '>':
	case '.':
		mixer_input_volumes = 0;
		mixer_toggle_mute_right = 1;
		break;
	case 'R':
	case 'r':
		mixer_input_volumes = !mixer_input_volumes;
		break;
	case 'L':
	case 'l':
		mixer_clear();
		break;
	case ' ':
		mixer_input_volumes = 1;
		mixer_toggle_record_left = 1;
		mixer_toggle_record_right = 1;
		break;
	case KEY_IC:
	case ';':
		mixer_input_volumes = 1;
		mixer_toggle_record_left = 1;
		break;
	case '\'':
	case KEY_DC:
		mixer_input_volumes = 1;
		mixer_toggle_record_right = 1;
		break;
	case '1':
		mixer_input_volumes = 1;
		mixer_route_rtol_in = 1;
		break;
	case '2':
		mixer_input_volumes = 1;
		mixer_route_ltor_in = 1;
		break;
	}
	mixer_focus_channel = CLAMP(mixer_focus_channel, 0, mixer_n_channels - 1);

	return finished;
}

static void mixer_init_screen(void)
{
	signal(SIGWINCH, (void *) mixer_init_screen);

	getmaxyx(mixer_window, mixer_max_y, mixer_max_x);
	mixer_clear();
	mixer_max_x = MAX(MIXER_MIN_X, mixer_max_x);
	mixer_max_y = MAX(MIXER_MIN_Y, mixer_max_y);
	mixer_clear();
	mixer_ofs_x = 2;
	mixer_ofs_y = 2;
	mixer_extra_space = 0;
	mixer_n_vis_channels = MIN((mixer_max_x - 2 * mixer_ofs_x + 1) / (9 + mixer_extra_space),
				   mixer_n_channels);
	mixer_extra_space = ((mixer_max_x - 2 * mixer_ofs_x - 1 - mixer_n_vis_channels * 9.0) /
			     (mixer_n_vis_channels - 1));
	if (mixer_n_vis_channels < mixer_n_channels) {
		/* recalc
		 */
		mixer_extra_space = MAX(mixer_extra_space, 1);
		mixer_n_vis_channels = MIN((mixer_max_x - 2 * mixer_ofs_x + 1) / (9 + mixer_extra_space),
					   mixer_n_channels);
		mixer_extra_space = ((mixer_max_x - 2 * mixer_ofs_x - 1 - mixer_n_vis_channels * 9.0) /
				     (mixer_n_vis_channels - 1));
	}
	mixer_first_vis_channel = 0;
	mixer_cbar_height = 10 + MAX(0, (mixer_max_y - MIXER_MIN_Y - 1)) / 2;
}

static void mixer_signal_handler(int signal)
{
	mixer_abort(ERR_SIGNAL, sys_siglist[signal]);
}

int main(int argc,
	 char **argv)
{
	int opt;

	/* parse args
	 */
	do {
		opt = getopt(argc, argv, "c:m:ehg");
		switch (opt) {
		case '?':
		case 'h':
			fprintf(stderr, "%s %s\n", PRGNAME_UPPER, VERSION);
			fprintf(stderr, "Usage: %s [-e] [-c <card: 1..%i>] [-m <mixer: 0..1>]\n", PRGNAME, snd_cards());
			mixer_abort(ERR_NONE, "");
		case 'c':
			card_id = snd_card_name(optarg);
			break;
		case 'e':
			mixer_exact = !mixer_exact;
			break;
		case 'g':
			mixer_do_color = !mixer_do_color;
			break;
		case 'm':
			mixer_id = CLAMP(optarg[0], '0', '1') - '0';
			break;
		}
	}
	while (opt > 0);

	/* initialize mixer
	 */
	mixer_init();

	/* setup signal handlers
	 */
	signal(SIGINT, mixer_signal_handler);
	signal(SIGTRAP, mixer_signal_handler);
	signal(SIGABRT, mixer_signal_handler);
	signal(SIGQUIT, mixer_signal_handler);
	signal(SIGBUS, mixer_signal_handler);
	signal(SIGSEGV, mixer_signal_handler);
	signal(SIGPIPE, mixer_signal_handler);
	signal(SIGTERM, mixer_signal_handler);

	/* initialize ncurses
	 */
	mixer_window = initscr();
	if (mixer_do_color)
		mixer_do_color = has_colors();
	mixer_init_draw_contexts();
	mixer_init_screen();
	if (mixer_max_x < MIXER_MIN_X ||
	    mixer_max_y < MIXER_MIN_Y)
		mixer_abort(ERR_WINSIZE, "");

	/* react on key presses
	 * and draw window
	 */
	keypad(mixer_window, TRUE);
	leaveok(mixer_window, TRUE);
	cbreak();
	noecho();
	do {
		mixer_update_cbars();
		mixer_draw_frame();
		refresh();
	}
	while (!mixer_iteration());

	mixer_abort(ERR_NONE, "");
};