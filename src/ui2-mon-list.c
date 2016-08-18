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
 * \param lines_to_display are the number of entries to display (not including
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
		monster_list_section_t section, int lines_to_display, int max_width,
		const char *prefix, bool show_others, size_t *max_width_result)
{
	const char *punctuation = lines_to_display == 0 ? "." : ":";
	const char *others = show_others ? "other " : "";

	char line_buffer[200];

	if (list == NULL || list->entries == NULL) {
		return;
	}

	if (list->total_monsters[section] == 0) {
		size_t max_line_length = strnfmt(line_buffer, sizeof(line_buffer),
				"%s no monsters.\n", prefix);

		if (tb != NULL) {
			textblock_append(tb, "%s", line_buffer);
		}

		if (max_width_result != NULL) {
			*max_width_result = max_line_length;
		}

		return;
	}

	size_t max_line_length = strnfmt(line_buffer, sizeof(line_buffer),
			"%s %d %smonster%s%s\n",
			prefix,
			list->total_monsters[section],
			others,
			PLURAL(list->total_monsters[section]),
			punctuation);

	if (tb != NULL) {
		textblock_append(tb, "%s", line_buffer);
	}

	int entry;
	int line_count;

	for (entry = 0, line_count = 0;
			entry < list->distinct_entries && line_count < lines_to_display;
			entry++)
	{
		char asleep[20] = {0};
		char location[20] = {0};

		line_buffer[0] = 0;

		if (list->entries[entry].count[section] == 0) {
			continue;
		}

		/* Only display directions for the case of a single monster. */
		if (list->entries[entry].count[section] == 1) {
			const char *n_or_s = list->entries[entry].dy <= 0 ? "N" : "S";
			const char *w_or_e = list->entries[entry].dx <= 0 ? "W" : "E";
			strnfmt(location, sizeof(location),
					" %d %s %d %s",
					abs(list->entries[entry].dy), n_or_s,
					abs(list->entries[entry].dx), w_or_e);
		}

		/* Get width available for monster name and sleep tag: 2 for char and
		 * space; location includes padding; last -1 because textblock (in effect)
		 * adds one column of padding on the right side of any string */
		size_t full_width = max_width - 2 - utf8_strlen(location) - 1;

		u16b asleep_in_section = list->entries[entry].asleep[section];
		u16b count_in_section = list->entries[entry].count[section];

		if (asleep_in_section > 0 && count_in_section > 1) {
			strnfmt(asleep, sizeof(asleep), " (%d asleep)", asleep_in_section);
		} else if (asleep_in_section == 1 && count_in_section == 1) {
			strnfmt(asleep, sizeof(asleep), " (asleep)");
		}

		/* Clip the monster name to fit, and append the sleep tag. */
		size_t name_width = MIN(full_width - utf8_strlen(asleep), sizeof(line_buffer));
		get_mon_name(line_buffer, sizeof(line_buffer),
				list->entries[entry].race,
				list->entries[entry].count[section]);
		utf8_clipto(line_buffer, name_width);
		my_strcat(line_buffer, asleep, sizeof(line_buffer));

		/* Calculate the width of the line for dynamic sizing; use a fixed max
		 * width for location and monster char. */
		max_line_length = MAX(max_line_length, utf8_strlen(line_buffer) + 12 + 2);

		/* Append monster symbol in text mode
		 * (it's called textblock_append_pict() for legacy reasons :) */
		if (tb != NULL && !use_graphics) {
			textblock_append_pict(tb,
					list->entries[entry].attr,
					monster_x_char[list->entries[entry].race->ridx]);
			textblock_append(tb, " ");
		}

		/* Add the left-aligned and padded monster name which will align the
		 * location to the right. */
		if (tb != NULL) {
			uint32_t line_attr = monster_list_entry_line_color(&list->entries[entry]);
			/* Because monster race strings are UTF8, we have to add
			 * additional padding for any raw bytes that might be consolidated
			 * into one displayed character. */
			full_width += strlen(line_buffer) - utf8_strlen(line_buffer);
			textblock_append_c(tb, line_attr,
					"%-*s%s\n", full_width, line_buffer, location);
		}

		line_count++;
	}

	/* Don't worry about the "...others" line, since it's probably shorter
	 * than what's already printed. */
	if (max_width_result != NULL) {
		*max_width_result = max_line_length;
	}

	/* Bail since we don't have enough room to display the remaining count or
	 * since we've displayed them all. */
	if (lines_to_display <= 0
			|| lines_to_display >= list->total_entries[section])
	{
		return;
	}

	/* Sum the remaining monsters; start where we left off in the above loop. */
	int remaining_monster_total = 0;

	while (entry < list->distinct_entries) {
		remaining_monster_total += list->entries[entry].count[section];
		entry++;
	}

	if (tb != NULL) {
		textblock_append(tb, "%6s...and %d others.\n", " ",
						 remaining_monster_total);
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
static bool monster_list_format_special(const monster_list_t *list,
		textblock *tb, int max_lines, int max_width, size_t *max_height_result, size_t *max_width_result)
{
	(void) list;
	(void) max_lines;
	(void) max_width;

	if (player->timed[TMD_IMAGE] > 0) {
		/* Hack - message needs newline to calculate width properly. */
		const char *message = "Your hallucinations are too wild to see things clearly.\n";

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
 * \param max_lines is the maximum number of lines that can be displayed.
 * \param max_width is the maximum line width that can be displayed.
 * \param max_height_result is returned with the number of lines needed to
 * format the list without truncation.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void monster_list_format_textblock(const monster_list_t *list,
		textblock *tb, int max_lines, int max_width, size_t *max_height_result, size_t *max_width_result)
{
	if (list == NULL || list->entries == NULL) {
		return;
	}

	if (monster_list_format_special(list, tb,
				max_lines, max_width, max_height_result, max_width_result))
	{
		return;
	}

	int los_lines_to_display = list->total_entries[MONSTER_LIST_SECTION_LOS];
	int esp_lines_to_display = list->total_entries[MONSTER_LIST_SECTION_ESP];
	int header_lines = 1;

	if (list->total_entries[MONSTER_LIST_SECTION_ESP] > 0) {
		header_lines += 2;
	}

	if (max_height_result != NULL) {
		*max_height_result =
			header_lines + los_lines_to_display + esp_lines_to_display;
	}

	int lines_remaining =
		max_lines - header_lines - list->total_entries[MONSTER_LIST_SECTION_LOS];

	/* Remove ESP lines as needed. */
	if (lines_remaining < list->total_entries[MONSTER_LIST_SECTION_ESP]) {
		esp_lines_to_display = MAX(lines_remaining - 1, 0);
	}

	/* If we don't even have enough room for the ESP header, start removing
	 * LOS lines, leaving one for the "...others". */
	if (lines_remaining < 0) {
		los_lines_to_display =
			list->total_entries[MONSTER_LIST_SECTION_LOS] - abs(lines_remaining) - 1;
	}

	/* Display only headers if we don't have enough space. */
	if (header_lines >= max_lines) {
		los_lines_to_display = 0;
		esp_lines_to_display = 0;
	}

	size_t max_los_line = 0;
	size_t max_esp_line = 0;

	monster_list_format_section(list, tb,
			MONSTER_LIST_SECTION_LOS, los_lines_to_display, max_width,
			"You can see", false, &max_los_line);

	if (list->total_entries[MONSTER_LIST_SECTION_ESP] > 0) {
		bool show_others = list->total_monsters[MONSTER_LIST_SECTION_LOS] > 0;

		if (tb != NULL) {
			textblock_append(tb, "\n");
		}

		monster_list_format_section(list, tb,
				MONSTER_LIST_SECTION_ESP, esp_lines_to_display, max_width,
				"You are aware of", show_others, &max_esp_line);
	}

	if (max_width_result != NULL) {
		*max_width_result = MAX(max_los_line, max_esp_line);
	}
}

/**
 * Get correct monster glyphs.
 */
void monster_list_get_glyphs(monster_list_t *list)
{
	/* Run through all monsters in the list. */
	for (int i = 0; i < (int) list->entries_size; i++) {
		monster_list_entry_t *entry = &list->entries[i];
		if (entry->race == NULL) {
			continue;
		}

		/* If no monster attribute use the standard UI picture. */
		if (!entry->attr) {
			entry->attr = monster_x_attr[entry->race->ridx];
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

	/* Force an update if detected monsters */
	for (int i = 1, end = cave_monster_max(cave); i < end; i++) {
		if (mflag_has(cave_monster(cave, i)->mflag, MFLAG_MARK)) {
			list->creation_turn = -1;
			break;
		}
	}

	monster_list_reset(list);
	monster_list_collect(list);
	monster_list_get_glyphs(list);
	monster_list_sort(list, monster_list_standard_compare);

	/* Draw the list to exactly fit the subwindow. */
	monster_list_format_textblock(list, tb, height, width, NULL, NULL);

	region reg = {0, 0, 0, 0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}

/**
 * Display the monster list interactively. This will dynamically size the list
 * for the best appearance.
 *
 * \param height is the height limit for the list.
 * \param width is the width limit for the list.
 */
void monster_list_show_interactive(void)
{
	textblock *tb = textblock_new();
	monster_list_t *list = monster_list_new();

	monster_list_collect(list);
	monster_list_get_glyphs(list);
	monster_list_sort(list, monster_list_standard_compare);

	/*
	 * Large numbers are passed as the height and width limit so that we can
	 * calculate the maximum number of rows and columns to display the list nicely.
	 */
	size_t max_width = 0;
	size_t max_height = 0;
	monster_list_format_textblock(list, NULL, 1000, 1000, &max_height, &max_width);

	/*
	 * Actually draw the list. We pass in max_height to the format function so
	 * that all lines will be appended to the textblock. The textblock itself
	 * will handle fitting it into the region.
	 */
	monster_list_format_textblock(list, tb, max_height, max_width, NULL, NULL);

	region reg = {
		.w = max_width,
		.h = max_height
	};

	textui_textblock_show(tb, reg, NULL);

	textblock_free(tb);
	monster_list_free(list);
}

/**
 * Force an update to the monster list subwindow.
 *
 * There are conditions that monster_list_reset() can't catch, so we set the
 * turn an invalid value to force the list to update.
 */
void monster_list_force_subwindow_update(void)
{
	monster_list_t *list = monster_list_shared_instance();
	list->creation_turn = -1;
}
