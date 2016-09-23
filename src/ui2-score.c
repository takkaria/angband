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

static const char *skip_spaces(const char *str)
{
	while (isspace((unsigned char) *str)) {
		str++;
	}

	return str;
}

/**
 * Display the scores in a given range.
 */
static void display_scores_aux(const high_score *scores,
		int from, int to, int highlight)
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
	int count = 0;
	while (count < MAX_HISCORES
			&& count < to
			&& scores[count].what[0] != 0)
	{
		count++;
	}

	/* Show 5 per page, until done */
	for (int page = from, pos = from, place = from + 1; page < count; page += 5) {
		char out_val[ANGBAND_TERM_STANDARD_WIDTH];
		char tmp_val[ANGBAND_TERM_STANDARD_WIDTH];

		Term_clear();

		struct loc loc = {0, 2};

		/* Dump 5 entries on this page */
		for (int entry = 0; entry < 5 && pos < count; entry++, pos++, place++) {
			const high_score *score = &scores[pos];

			uint32_t attr = pos == highlight ? COLOUR_L_GREEN : COLOUR_WHITE;

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
			loc.x = 0;
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
			loc.x = 15;

			/* Dump the info */
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
			c_put_str(attr, out_val, loc);

			/* Empty line */
			loc.y += 2;
		}

		Term_flush_output();

		show_prompt("[Press ESC to exit, any other key to continue.]", false);
		struct keypress key = inkey_only_key();
		clear_prompt();

		if (key.code == ESCAPE) {
			break;
		}
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
		display_scores_aux(scores, 0, 15, pos);
	} else {
		display_scores_aux(scores, 0, 5, -1);
		display_scores_aux(scores, pos - 2, pos + 8, pos);
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
		display_scores_aux(scores, 0, MAX_HISCORES, -1);
	}

	Term_pop();
}
