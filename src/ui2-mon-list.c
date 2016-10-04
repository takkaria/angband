/**
 * \file ui2-mon-list.c
 * \brief Monster list UI.
 *
 * Copyright (c) 1997-2007 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2013 Ben Semmler
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

#include "mon-desc.h"
#include "mon-list.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "player-timed.h"
#include "ui2-mon-list.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-term.h"

/**
 * Format a section of the monster list: a header followed by monster list
 * entry rows.
 *
 * This function will process each entry for the given section. It will display:
 * - monster char;
 * - number of monsters;
 * - monster name (truncated, if needed to fit the line);
 * - whether or not the monster is asleep (and how many if in a group);
 * - monster distance from the player (aligned to the right side of the list).
 * By passing in a NULL textblock, the maximum line width of the section can be
 * found.
 *
 * \param list is the monster list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param section is the section of the monster list to format.
 * \param max_height are the number of entries to display (not including
 * the header).
 * \param max_width is the maximum line width.
 * \param prefix is the beginning of the header; the remainder is appended with
 * the number of monsters.
 * \param show_others is used to append "other monsters" to the header,
 * after the number of monsters.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void monster_list_format_section(const monster_list_t *list, textblock *tb,
		monster_list_section_t section,
		int lines_to_display, int max_width,
		const char *prefix, bool show_others,
		size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (list->total_monsters[section] == 0) {
		size_t len = strnfmt(buf, sizeof(buf), "%s no monsters.", prefix);

		if (tb != NULL) {
			textblock_append(tb, "%s", buf);
		}

		if (max_width_result != NULL) {
			*max_width_result = len;
		}

		return;
	}

	size_t max_line_length = strnfmt(buf, sizeof(buf),
			"%s %d %smonster%s%s\n",
			prefix,
			list->total_monsters[section],
			show_others ? "other " : "",
			PLURAL(list->total_monsters[section]),
			lines_to_display == 0 ? "." : ":");

	if (tb != NULL) {
		textblock_append(tb, "%s", buf);
	}

	int entry_count;
	int line_count;

	for (entry_count = 0, line_count = 0;
			entry_count < list->distinct_entries && line_count < lines_to_display;
			entry_count++)
	{
		const monster_list_entry_t *entry = &list->entries[entry_count];

		if (entry->count[section] == 0) {
			continue;
		}

		char asleep[20] = "";
		char coords[20] = "";

		/* Only display directions for the case of a single monster. */
		if (entry->count[section] == 1) {
			const char *n_or_s = list->entries[entry_count].dy <= 0 ? "N" : "S";
			const char *w_or_e = list->entries[entry_count].dx <= 0 ? "W" : "E";
			strnfmt(coords, sizeof(coords),
					" %d %s %d %s",
					abs(list->entries[entry_count].dy), n_or_s,
					abs(list->entries[entry_count].dx), w_or_e);
		}

		if (entry->asleep[section] > 0 && entry->count[section] > 1) {
			strnfmt(asleep, sizeof(asleep), " (%d asleep)", entry->asleep[section]);
		} else if (entry->asleep[section] == 1 && entry->count[section] == 1) {
			strnfmt(asleep, sizeof(asleep), " (asleep)");
		}

		size_t pict_width   = 2; /* monster char and space */
		size_t coords_width = utf8_strlen(coords);
		size_t asleep_width = utf8_strlen(asleep);

		size_t desc_width; /* monster name (and count) and asleep tag */
		size_t name_width; /* monster name (and count) */

		/* Use monster pict and coordinates, if there is enough space */
		if (max_width < (int) (pict_width + coords_width)) {
			assert(max_width >= (int) pict_width);

			coords_width = 0;
			desc_width = max_width - pict_width;
		} else {
			desc_width = max_width - pict_width - coords_width;
		}

		/* Use as much of asleep tag as possible and maybe shorten name */
		if (desc_width < asleep_width) {
			name_width = 0;
			asleep_width = desc_width;
			utf8_clipto(asleep, asleep_width);
		} else {
			name_width = desc_width - asleep_width;
		}

		/* See get_mon_name(); we need 5 chars to show something reasonable */
		if (name_width >= 5) {
			get_mon_name(buf, sizeof(buf), entry->race, entry->count[section]);
			utf8_clipto(buf, name_width);
		} else if (name_width >= 3) {
			strnfmt(buf, sizeof(buf), "%3d", entry->count[section]);
		} else {
			buf[0] = 0;
		}

		if (asleep_width > 0) {
			my_strcat(buf, asleep, sizeof(buf));
		}

		/* Adding 2 has the effect of prepending 2 spaces to coords */
		size_t total_width = pict_width + utf8_strlen(buf) + coords_width + 2;

		/* Calculate the width of the line for dynamic sizing. */
		max_line_length = MAX(max_line_length, total_width);

		if (tb != NULL) {
			/* entry->attr is used to animate (shimmer)
			 * monsters; that doesn't work with tiles */
			uint32_t attr = use_graphics ?
				monster_x_attr[entry->race->ridx] : entry->attr;

			textblock_append_pict(tb,
					attr, monster_x_char[entry->race->ridx]);
			textblock_append(tb, " ");

			/* Because monster race strings are UTF8, we have to add
			 * additional padding for any raw bytes that might be
			 * consolidated into one displayed character. */
			int width = desc_width + strlen(buf) - utf8_strlen(buf);

			attr = monster_list_entry_line_color(entry);
			textblock_append_c(tb, attr, "%-*s", width, buf);
			if (coords_width > 0) {
				textblock_append_c(tb, attr, "%s", coords);
			}
			textblock_append(tb, "\n");
		}

		line_count++;
	}

	/* Don't worry about the "...others" line, since it's
	 * probably shorter than what's already printed. */
	if (max_width_result != NULL) {
		*max_width_result = max_line_length;
	}

	/* If we have some lines to display and haven't displayed them all */
	if (lines_to_display > 0
			&& lines_to_display < list->total_entries[section])
	{
		/* Sum the remaining monsters; start where we left off in the above loop. */
		int remaining_monster_total = 0;

		while (entry_count < list->distinct_entries) {
			remaining_monster_total += list->entries[entry_count].count[section];
			entry_count++;
		}

		if (tb != NULL) {
			textblock_append(tb, "%6s...and %d others.\n",
					" ", remaining_monster_total);
		}
	}
}

/**
 * Allow the standard list formatted to be bypassed for special cases.
 *
 * Returning true will bypass any other formatteding in
 * monster_list_format_textblock().
 *
 * \param list is the monster list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param max_lines is the maximum number of lines that can be displayed.
 * \param max_width is the maximum line width that can be displayed.
 * \param max_height_result is returned with the number of lines needed to
 * format the list without truncation.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 * \return true if further formatting should be bypassed.
 */
static bool monster_list_format_special(const monster_list_t *list, textblock *tb,
		int max_lines, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	(void) list;
	(void) max_lines;
	(void) max_width;

	if (player->timed[TMD_IMAGE] > 0) {
		/* Hack - message needs newline to calculate width properly. */
		const char *message = "Your hallucinations are too wild to see things clearly.";

		if (max_height_result != NULL) {
			*max_height_result = 1;
		}
		if (max_width_result != NULL) {
			*max_width_result = strlen(message);
		}
		if (tb != NULL) {
			textblock_append_c(tb, COLOUR_ORANGE, "%s", message);
		}

		return true;
	} else {
		return false;
	}
}

/**
 * Format the entire monster list with the given parameters.
 *
 * This function can be used to calculate the preferred dimensions for the list
 * by passing in a NULL textblock. The LOS section of the list will always be
 * shown, while the other section will be added conditionally. Also, this
 * function calls monster_list_format_special() first; if that function returns
 * true, it will bypass normal list formatting.
 *
 * \param list is the monster list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param max_height is the maximum number of lines that can be displayed.
 * \param max_width is the maximum line width that can be displayed.
 * \param max_height_result is returned with the number of lines needed to
 * format the list without truncation.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void monster_list_format_textblock(const monster_list_t *list, textblock *tb,
		int max_height, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);

	if (monster_list_format_special(list, tb,
				max_height, max_width, max_height_result, max_width_result))
	{
		return;
	}

	const int los_entries = list->total_entries[MONSTER_LIST_SECTION_LOS];
	const int esp_entries = list->total_entries[MONSTER_LIST_SECTION_ESP];

	int header_lines = 1 + (esp_entries > 0 ? 2 : 0);

	if (max_height_result != NULL) {
		*max_height_result = header_lines + los_entries + esp_entries;
	}

	int los_lines_to_display;
	int esp_lines_to_display;

	if (header_lines < max_height) {
		int lines_remaining = max_height - header_lines;

		if (lines_remaining < los_entries) {
			/* Remove some LOS lines, leaving room for "...others" */
			los_lines_to_display = MAX(lines_remaining - 1, 0);
			esp_lines_to_display = 0;
		} else if (lines_remaining < los_entries + esp_entries) {
			/* Remove some ESP lines, leaving room for "...others" */
			los_lines_to_display = los_entries;
			esp_lines_to_display = MAX(lines_remaining - 1, 0);
		} else {
			los_lines_to_display = los_entries;
			esp_lines_to_display = esp_entries;
		}
	} else {
		assert(header_lines == max_height);

		los_lines_to_display = 0;
		esp_lines_to_display = 0;
	}

	size_t max_los_line = 0;
	size_t max_esp_line = 0;

	monster_list_format_section(list, tb,
			MONSTER_LIST_SECTION_LOS,
			los_lines_to_display, max_width,
			"You can see", false,
			&max_los_line);

	if (esp_entries > 0) {
		if (tb != NULL) {
			textblock_append(tb, "\n");
		}

		monster_list_format_section(list, tb,
				MONSTER_LIST_SECTION_ESP,
				esp_lines_to_display, max_width,
				"You are aware of", los_entries > 0,
				&max_esp_line);
	}

	if (max_width_result != NULL) {
		*max_width_result = MAX(max_los_line, max_esp_line);
	}
}

/**
 * Get correct monster glyphs.
 */
static void monster_list_get_glyphs(monster_list_t *list)
{
	/* Run through all monsters in the list. */
	for (int i = 0; i < (int) list->entries_size; i++) {
		monster_list_entry_t *entry = &list->entries[i];

		if (entry->race != NULL) {
			/* If no monster attribute use the standard UI picture. */
			if (entry->attr == 0) {
				entry->attr = monster_x_attr[entry->race->ridx];
			}
		}
	}
}

/**
 * Display the monster list statically. This will force the list to be
 * displayed to the provided dimensions. Contents will be adjusted accordingly.
 *
 * In order to support more efficient monster flicker animations, this function
 * uses a shared list object so that it's not constantly allocating and freeing
 * the list.
 *
 * \param height is the height of the list.
 * \param width is the width of the list.
 */
void monster_list_show_subwindow(void)
{
	int width;
	int height;
	Term_get_size(&width, &height);

	textblock *tb = textblock_new();
	monster_list_t *list = monster_list_shared_instance();

	monster_list_reset(list);
	monster_list_collect(list);
	monster_list_get_glyphs(list);
	monster_list_sort(list, monster_list_standard_compare);

	/* Draw the list to exactly fit the subwindow. */
	monster_list_format_textblock(list, tb, height, width, NULL, NULL);

	region reg = {0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}

/**
 * Display the monster list interactively. This will dynamically size the list
 * for the best appearance.
 */
void monster_list_show_interactive(void)
{
	textblock *tb = textblock_new();
	monster_list_t *list = monster_list_new();

	monster_list_collect(list);
	monster_list_get_glyphs(list);
	monster_list_sort(list, monster_list_standard_compare);

	/* Large numbers are passed as the height and width limit so that we can
	 * calculate the maximum number of rows and columns to display the list nicely. */
	size_t max_width = 0;
	size_t max_height = 0;
	monster_list_format_textblock(list, NULL, 1000, 1000, &max_height, &max_width);

	/* Actually draw the list. We pass in max_height to the format function so
	 * that all lines will be appended to the textblock. The textblock itself
	 * will handle fitting it into the region. */
	monster_list_format_textblock(list, tb, max_height, max_width, NULL, NULL);

	region reg = {
		.w = MAX(ANGBAND_TERM_STANDARD_WIDTH / 2, max_width),
		.h = MIN(ANGBAND_TERM_STANDARD_HEIGHT, max_height)
	};

	textui_textblock_show(tb, TERM_POSITION_TOP_LEFT, reg, NULL);

	textblock_free(tb);
	monster_list_free(list);
}
