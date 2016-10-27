/**
 * \file ui2-history.c
 * \brief Character auto-history display UI
 *
 * Copyright (c) 2007 J.D. White
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "player-history.h"
#include "ui2-input.h"
#include "ui2-output.h"

/**
 * Print the header for the history display
 */
static void print_history_header(void)
{
	Term_addws(0, 0, TERM_MAX_LEN,
			COLOUR_L_BLUE, L"      Turn   Depth  Note");
}

static void show_history_prompt(void)
{
	const char *prompt =
		"[<`dir`>, `p`/`PageUp` for previous page, `n`/`PageDown` for next page, `ESC`]";

	put_str_h_center(prompt, Term_height() - 1,
			COLOUR_WHITE, COLOUR_L_BLUE);
}

static int history_scroll_check(int item, int n_items, int scroll, region reg)
{
	assert(item >= 0);

	int new_scroll = scroll;

	if (n_items > reg.h) {
		const int new_item = item + scroll;
		const int end_item = n_items - reg.h;

		assert(item <= end_item);

		if (new_item > end_item) {
			new_scroll = end_item - item;
		}
		if (new_item < 0) {
			new_scroll = -item;
		}
	} else {
		new_scroll = 0;
	}

	return new_scroll;
}

static void history_scroll(int scroll, region reg,
		int *line, int *end_line, int *item)
{
	const int abs_scroll = ABS(scroll);

	assert(abs_scroll > 0);
	assert(abs_scroll < reg.h);

	const struct loc src = {
		.x = reg.x,
		.y = scroll > 0 ? reg.y + abs_scroll: reg.y
	};
	const struct loc dst = {
		.x = reg.x,
		.y = scroll > 0 ? reg.y : reg.y + abs_scroll
	};

	const int height = reg.h - abs_scroll;

	Term_move_points(dst.x, dst.y, src.x, src.y, reg.w, height);

	if (scroll > 0) {
		*item += reg.h;
		*line += height;
	} else {
		*item -= abs_scroll;
		*end_line -= height;
	}
}

static int history_dump(const struct history_info *history,
		int item, int n_items, region reg, int scroll, bool redraw)
{
	scroll = history_scroll_check(item, n_items, scroll, reg);

	if (scroll != 0 || redraw) {
		int cur_line = reg.y;
		int max_line = reg.y + reg.h;
		int cur_item = item;

		if (scroll != 0) {
			history_scroll(scroll, reg,
					&cur_line, &max_line, &cur_item);
		}

		while (cur_line < max_line && cur_item < n_items) {

			char buf[ANGBAND_TERM_STANDARD_WIDTH];
			int len =
				strnfmt(buf, sizeof(buf), "%10d%7d\'  %s",
					history[cur_item].turn,
					history[cur_item].dlev * 50,
					history[cur_item].event);

			if (hist_has(history[cur_item].type, HIST_ARTIFACT_LOST)) {
				len = my_strcat(buf, " (LOST)", sizeof(buf));
			}

			Term_erase_line(0, cur_line);
			Term_adds(0, cur_line, len, COLOUR_WHITE, buf);

			cur_line++;
			cur_item++;
		}
	}

	return item + scroll;
}

/**
 * Handles all of the display functionality for the history list.
 */
void history_display(void)
{
	struct history_info *history = NULL;

	int cur_item = 0;
	const int n_items = history_get_list(player, &history);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_TEXT,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Player history", COLOUR_WHITE, COLOUR_DARK);

	/* 3 lines provide space for the header, prompt,
	 * and separator line between prompt and text */
	region text_reg = {0, 1, Term_width(), Term_height() - 3};

	print_history_header();
	show_history_prompt();

	int scroll = 0;
	bool redraw = true;

	bool done = false;

	while (!done) {
		cur_item = history_dump(history,
				cur_item, n_items, text_reg, scroll, redraw);

		scroll = 0;
		redraw = false;
		Term_flush_output();

		struct keypress key = inkey_only_key();

		switch (key.code) {
			/* Use page size - 2 to scroll by less than full page */

			case 'n': case ' ': case KC_PGDOWN:
				scroll = text_reg.h - 2;
				break;

			case 'p': case '-': case KC_PGUP:
				scroll = -(text_reg.h - 2);
				break;

			case ARROW_DOWN:
				scroll = 1;
				break;

			case ARROW_UP:
				scroll = -1;
				break;

			case ESCAPE:
				done = true;
				break;
		}
	}

	Term_pop();
}

/**
 * Dump character history to a file, which we assume is already open.
 */
void dump_history(ang_file *file)
{
	struct history_info *history_list_local = NULL;
	size_t max_item = history_get_list(player, &history_list_local);

	file_putf(file, "  [Player history]\n");
	file_putf(file, "      Turn   Depth  Note\n");

	for (size_t i = 0; i < max_item; i++) {

		char buf[120];
		strnfmt(buf, sizeof(buf), "%10d%7d\'  %s",
				history_list_local[i].turn,
				history_list_local[i].dlev * 50,
				history_list_local[i].event);

		if (hist_has(history_list_local[i].type, HIST_ARTIFACT_LOST)) {
			my_strcat(buf, " (LOST)", sizeof(buf));
		}

		file_putf(file, "%s\n", buf);
	}
}
