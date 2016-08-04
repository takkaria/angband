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
	struct loc loc;

	loc.x = 0;
	loc.y = 0;
	c_put_str(COLOUR_WHITE, "[Player history]", loc);

	loc.x = 0;
	loc.y = 1;
	c_put_str(COLOUR_L_BLUE, "      Turn   Depth  Note", loc);
}


/**
 * Handles all of the display functionality for the history list.
 */
void history_display(void)
{
	struct history_info *history_list_local = NULL;

	static int first_item = 0;
	int max_item = history_get_list(&history_list_local);

	const int term_width = 80;
	const int term_height = 24;

	struct term_hints hints = {
		.width = term_width,
		.height = term_height,
		.purpose = TERM_PURPOSE_TEXT,
		.position = TERM_POSITION_CENTER
	};

	Term_push_new(&hints);

	/* Four lines provide space for the header and footer */
	const int page_size = term_height - 4;

	bool done = false;

	while (!done) {
		Term_clear();

		/* Print everything to screen */
		print_history_header();

		/* Size of header 2 lines */
		struct loc loc = {0, 2};

		for (int i = first_item, y = 0; i < max_item && y < page_size; i++, y++, loc.y++) {
			char buf[80];
			strnfmt(buf, sizeof(buf), "%10d%7d\'  %s",
					history_list_local[i].turn,
					history_list_local[i].dlev * 50,
					history_list_local[i].event);

			if (hist_has(history_list_local[i].type, HIST_ARTIFACT_LOST)) {
				my_strcat(buf, " (LOST)", sizeof(buf));
			}

			prt(buf, loc);
		}

		loc.y = term_height - 1;
		prt("[Arrow keys scroll, p/PgUp for previous page, n/PgDn for next page, ESC to exit.]", loc);

		struct keypress key = inkey_only_key();

		switch (key.code) {
			/* Use page_size - 1 to scroll by less than full page */

			case 'n': case ' ': case KC_PGDOWN:
				first_item = MIN(max_item, first_item + (page_size - 1));
				break;

			case 'p': case '-': case KC_PGUP:
				first_item = MAX(0, first_item - (page_size - 1));
				break;

			case ARROW_DOWN:
				first_item = MIN(max_item, first_item + 1);
				break;

			case ARROW_UP:
				first_item = MAX(0, first_item - 1);
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
	size_t max_item = history_get_list(&history_list_local);

	file_putf(file, "[Player history]\n");
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

		file_putf(file, "%s", buf);
		file_put(file, "\n");
	}
}
