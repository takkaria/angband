/**
 * \file ui2-score.c
 * \brief Highscore display for Angband
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "buildid.h"
#include "game-world.h"
#include "score.h"
#include "ui2-input.h"
#include "ui2-output.h"
#include "ui2-score.h"
#include "ui2-term.h"

#define SCORE_ENTRY_LINES      3
#define SCORE_ENTRIES_PER_PAGE 5
#define SCORE_INDENT_AMOUNT   15

static const char *skip_spaces(const char *str)
{
	while (isspace((unsigned char) *str)) {
		str++;
	}

	return str;
}

static void display_score(const high_score *score, int place,
		struct loc loc, bool highlight)
{
	char out_val[ANGBAND_TERM_STANDARD_WIDTH];
	char tmp_val[ANGBAND_TERM_STANDARD_WIDTH];

	const int start_y = loc.y; /* just for checking number of lines */

	uint32_t attr = highlight ? COLOUR_L_GREEN : COLOUR_WHITE;

	struct player_race *race = player_id2race(atoi(score->p_r));
	struct player_class *class = player_id2class(atoi(score->p_c));

	/* Extract the level info */
	int clev = atoi(score->cur_lev);
	int mlev = atoi(score->max_lev);
	int cdun = atoi(score->cur_dun);
	int mdun = atoi(score->max_dun);

	/* Extract the gold and such */
	const char *user = skip_spaces(score->uid);
	const char *when = skip_spaces(score->day);
	const char *gold = skip_spaces(score->gold);
	const char *aged = skip_spaces(score->turns);

	/* Dump some info */
	strnfmt(out_val, sizeof(out_val),
			"%3d.%9s  %s the %s %s, level %d",
			place, score->pts, score->who,
			race  ? race->name : "<none>",
			class ? class->name : "<none>",
			clev);

	/* Append maximum level */
	if (mlev > clev) {
		my_strcat(out_val, format(" (Max %d)", mlev), sizeof(out_val));
	}

	/* Dump the first line */
	c_put_str(attr, out_val, loc);
	loc.y++;

	/* Died where? */
	if (!cdun) {
		strnfmt(out_val, sizeof(out_val),
				"Killed by %s in the town", score->how);
	} else {
		strnfmt(out_val, sizeof(out_val),
				"Killed by %s on dungeon level %d", score->how, cdun);
	}

	/* Append a maximum level */
	if (mdun > cdun) {
		my_strcat(out_val, format(" (Max %d)", mdun), sizeof(out_val));
	}

	/* Second and third lines are indented to the left */
	loc.x = SCORE_INDENT_AMOUNT;

	/* Dump the second line */
	c_put_str(attr, out_val, loc);
	loc.y++;

	/* Clean up standard encoded form of "when" */
	if (*when == '@' && strlen(when) == 9) {
		strnfmt(tmp_val, sizeof(tmp_val),
				"%.4s-%.2s-%.2s", when + 1, when + 5, when + 7);
		when = tmp_val;
	}

	/* And still another line of info */
	strnfmt(out_val, sizeof(out_val),
			"(User %s, Date %s, Gold %s, Turn %s).",
			user, when, gold, aged);

	/* Dump the third line */
	c_put_str(attr, out_val, loc);
	loc.y++;

	assert(loc.y - start_y == SCORE_ENTRY_LINES);
}

static void clear_score(struct loc loc)
{
	for (int i = 0; i < SCORE_ENTRY_LINES; i++, loc.y++) {
		Term_erase_line(loc.x, loc.y);
	}
}

/**
 * Display the scores in a given range.
 */
static void display_scores_aux(const high_score *scores,
		int from, int to, int highlight, bool *show_rest)
{
	/* Assume we will show the first 10 */
	if (from < 0) {
		from = 0;
	}
	if (to < 0) {
		to = 10;
	}
	if (to > MAX_HISCORES) {
		to = MAX_HISCORES;
	}

	/* Count the high scores */
	int n_scores = 0;
	while (n_scores < to && scores[n_scores].what[0] != 0) {
		n_scores++;
	}

	put_str_h_simple("[Press `ESC` to exit, any other key to continue.]",
			loc(SCORE_INDENT_AMOUNT - 1, Term_height() - 1));

	bool stop = false;

	/* Show 5 per page, until done */
	for (int page = from, cur_score = from;
			!stop && page < n_scores;
			page += SCORE_ENTRIES_PER_PAGE)
	{
		struct loc loc = {0, 2};

		/* Dump 5 entries on this page */
		for (int entry = 0; entry < SCORE_ENTRIES_PER_PAGE; entry++, cur_score++) {
			clear_score(loc);

			if (cur_score < n_scores) {
				display_score(&scores[cur_score], cur_score + 1,
						loc, cur_score == highlight);
			}

			loc.y += SCORE_ENTRY_LINES + 1; /* add empty line */
		}

		Term_flush_output();

		struct keypress key = inkey_only_key();
		if (key.code == ESCAPE) {
			stop = true;
		}
	}

	if (show_rest != NULL) {
		*show_rest = !stop;
	}
}

/**
 * Predict the players location, and display it.
 */
void predict_score(void)
{
	high_score the_score;
	high_score scores[MAX_HISCORES];

	/* Read scores, place current score */
	highscore_read(scores, N_ELEMENTS(scores));
	build_score(&the_score, "nobody (yet!)", NULL);

	int pos;
	if (player->is_dead) {
		pos = highscore_where(&the_score, scores, N_ELEMENTS(scores));
	} else {
		pos = highscore_add(&the_score, scores, N_ELEMENTS(scores));
	}

	/* Top fifteen scores if on the top ten,
	 * otherwise top five and ten surrounding */
	if (pos < 10) {
		display_scores_aux(scores, 0, 15, pos, NULL);
	} else {
		bool show_rest = true;
		display_scores_aux(scores, 0, 5, -1, &show_rest);

		if (show_rest) {
			display_scores_aux(scores, pos - 2, pos + 8, pos, NULL);
		}
	}
}

/**
 * Show scores.
 */
void show_scores(void)
{
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	Term_add_tab(0, format("%s Hall of Fame", VERSION_NAME), COLOUR_WHITE, COLOUR_DARK);

	/* Display the scores */
	if (character_generated) {
		predict_score();
	} else {
		high_score scores[MAX_HISCORES];
		highscore_read(scores, N_ELEMENTS(scores));
		display_scores_aux(scores, 0, MAX_HISCORES, -1, NULL);
	}

	Term_pop();
}
