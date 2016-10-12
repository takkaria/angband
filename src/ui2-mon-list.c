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
 * Helper function for monlist display;
 * variant of mon-desc.c:get_mon_name().
 */
static size_t get_monster_name(char *buf, size_t bufsize,
		const struct monster_race *race, int num)
{
	assert(race != NULL);

	size_t len;

	my_strcpy(buf, " ", bufsize);

    /* Unique names don't have a number */
	if (rf_has(race->flags, RF_UNIQUE)) {
		my_strcat(buf, "[U] ", bufsize);
        len = my_strcat(buf, race->name, bufsize);
    } else if (num == 1) {
        len = my_strcat(buf, race->name, bufsize);
    } else if (race->plural != NULL) {
        len = my_strcat(buf, race->plural, bufsize);
    } else {
        char race_name[ANGBAND_TERM_TEXTBLOCK_WIDTH];
		my_strcpy(race_name, race->name, sizeof(race_name));
        plural_aux(race_name, sizeof(race_name));

        len = my_strcat(buf, race_name, bufsize);
    }

	return len;
}

/**
 * As an optimization, we don't want to do anything with the
 * buffer if there is no textblock (since the buffer will not
 * be appended to it; see monster_list_show_interactive()).
 */
static void maybe_clipto(char *buf, size_t clip, const textblock *tb)
{
	if (tb != NULL) {
		utf8_clipto(buf, clip);
	}
}

/**
 * This function is called from monster_list_format_section()
 *
 * \param entry is the monster list entry to process
 * \param tb is the textblock to add text to or NULL if only the dimensions
 * need to be calculated
 * \param section is the section of the entry (LOS or ESP)
 * \param max_width is the maximum line width that can be displayed
 * \param max_line_length is updated with the length of the string to display
 * \param need_coords means that all entries, including groups should pretend
 * to have coordinates when calculating max_line_length
 */
static void monster_list_process_entry(const monster_list_entry_t *entry,
		textblock *tb, monster_list_section_t section,
		size_t max_width, size_t *max_line_length, bool need_coords)
{
	char name[ANGBAND_TERM_STANDARD_WIDTH] = "";
	char count[ANGBAND_TERM_STANDARD_WIDTH / 2] = "";
	char asleep[ANGBAND_TERM_STANDARD_WIDTH / 2] = "";
	char coords[ANGBAND_TERM_STANDARD_WIDTH / 2] = "";

	/* monster tile */
	size_t pict_w = 1;

	/* number of monsters; single monsters dont display it */
	size_t count_w = 0;
	if (entry->count[section] > 1) {
		count_w =
			strnfmt(count, sizeof(count), " %d", entry->count[section]);
	}

	/* name of monster(s) */
	size_t name_w =
		get_monster_name(name, sizeof(name), entry->race, entry->count[section]);

	/* "(asleep)" tag */
	size_t asleep_w = 0;
	if (entry->asleep[section] > 0 && entry->count[section] > 1) {
		asleep_w =
			strnfmt(asleep, sizeof(asleep), " (%d asleep)", entry->asleep[section]);
	} else if (entry->asleep[section] == 1 && entry->count[section] == 1) {
		asleep_w =
			my_strcpy(asleep, " (asleep)", sizeof(asleep));
	}

	/* coordinates of a monster (groups dont display it) */
	size_t coords_w = 0;
	if (entry->count[section] == 1) {
		const char *n_or_s = entry->dy <= 0 ? "N" : "S";
		const char *w_or_e = entry->dx <= 0 ? "W" : "E";

		coords_w =
			strnfmt(coords, sizeof(coords),
				" %d %s %d %s", abs(entry->dy), n_or_s, abs(entry->dx), w_or_e);
	}

	if (pict_w + count_w + name_w + asleep_w + coords_w <= max_width) {
		/* There is enough space for everything */;
	} else if (pict_w + count_w + asleep_w + coords_w < max_width) {
		name_w = max_width - (pict_w + count_w + asleep_w + coords_w);
		maybe_clipto(name, name_w, tb);
	} else if (pict_w + count_w + coords_w < max_width) {
		name_w = 0;
		maybe_clipto(name, name_w, tb);

		asleep_w = max_width - (pict_w + count_w + coords_w);
		maybe_clipto(asleep, asleep_w, tb);
	} else if (pict_w + coords_w < max_width) {
		name_w = 0;
		maybe_clipto(name, name_w, tb);

		asleep_w = 0;
		maybe_clipto(asleep, asleep_w, tb);

		count_w = max_width - (pict_w + coords_w);
		maybe_clipto(count, count_w, tb);
	} else {
		assert(max_width >= pict_w);

		name_w = 0;
		maybe_clipto(name, name_w, tb);

		asleep_w = 0;
		maybe_clipto(asleep, asleep_w, tb);

		count_w = 0;
		maybe_clipto(count, count_w, tb);

		coords_w = max_width - pict_w;
		maybe_clipto(coords, coords_w, tb);
	}

	/* calculate the width of the line for dynamic sizing */
	*max_line_length = MAX(*max_line_length,
			pict_w + count_w + name_w + asleep_w + (need_coords ? 10 : coords_w));

	if (tb != NULL) {
		/* entry->attr is used to animate (shimmer) monsters; that doesn't
		 * work with tiles, or with non-shimmering monsters (obviously) */
		uint32_t attr = use_graphics || entry->attr == 0 ?
			monster_x_attr[entry->race->ridx] : entry->attr;
		textblock_append_pict(tb, attr, monster_x_char[entry->race->ridx]);

		textblock_append_c(tb, monster_list_entry_line_color(entry),
				"%s%s%s", count, name, asleep);

		assert(max_width >= pict_w + count_w + name_w + asleep_w + coords_w);

		/* Hack - because monster race strings are UTF8, we have to add some padding
		 * for any raw bytes that might be consolidated into one displayed character */
		int coords_width = (max_width - pict_w - count_w - name_w - asleep_w) +
			(strlen(name) - utf8_strlen(name));
		textblock_append(tb, "%*s\n", coords_width, coords);
	}
}

/**
 * This function returns true if there is at least one entry
 * with a single monster. Such entries display coordinates of
 * the monster; to make textblock look better we will format it
 * (textblock) as if all entries had coordinates (groups don't
 * actually have them).
 */
static bool monster_list_need_coords(const monster_list_t *list,
		monster_list_section_t section, int lines_to_display)
{
	for (int entry_count = 0, line_count = 0;
			entry_count < list->distinct_entries && line_count < lines_to_display;
			entry_count++)
	{
		const monster_list_entry_t *entry = &list->entries[entry_count];

		if (entry->count[section] == 1) {
			return true;
		} else if (entry->count[section] > 0) {
			line_count++;
		}
	}

	return false;
}

/**
 * Format a section of the monster list:
 * a header followed by monster list entry rows.
 *
 * This function will process each entry for the given section. It will display:
 * - monster char;
 * - number of monsters;
 *   monster count (number of monsters) if there is a group
 * - monster name (truncated, if needed to fit the line);
 * - whether or not the monster is asleep (and how many if in a group);
 * - monster distance from the player (aligned to the right side of the list).
 * By passing in a NULL textblock, the maximum line width of the section can be found.
 *
 * \param list is the monster list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param section is the section of the monster list to format.
 * \param max_height are the number of entries to display (not including the header).
 * \param max_width is the maximum line width.
 * \param prefix is the beginning of the header; the remainder is appended with
 * the number of monsters.
 * \param show_others is used to append "other monsters" to the header, after
 * the number of monsters.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void monster_list_format_section(const monster_list_t *list,
		textblock *tb,
		monster_list_section_t section,
		size_t lines_to_display, size_t max_width,
		const char *prefix, bool show_others,
		size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);
	assert(max_width > 0);

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

	/* If textblock is NULL, the caller is just trying to calculate the
	 * maximum width of the monster text line (instead of displaying it) */
	bool need_coords = tb == NULL ?
		monster_list_need_coords(list, section, lines_to_display) : false;

	unsigned entry_count;
	unsigned line_count;

	for (entry_count = 0, line_count = 0;
			entry_count < list->distinct_entries && line_count < lines_to_display;
			entry_count++)
	{
		const monster_list_entry_t *entry = &list->entries[entry_count];

		if (entry->count[section] > 0) {
			monster_list_process_entry(entry, tb, section,
					max_width, &max_line_length, need_coords);
			line_count++;
		}
	}

	/* Don't worry about the "...others" line, since it's probably shorter than
	 * what's already printed, and if not, it will be split into several lines
	 * by the textblock display functions (textui_textblock_show/place()) */
	if (max_width_result != NULL) {
		*max_width_result = max_line_length;
	}

	if (tb != NULL
			&& lines_to_display > 0
			&& lines_to_display < list->total_entries[section])
	{
		/* If we have some lines to display and haven't displayed them all, sum
		 * the remaining monsters; start where we left off in the above loop */
		unsigned remaining_monster_total = 0;

		while (entry_count < list->distinct_entries) {
			remaining_monster_total += list->entries[entry_count].count[section];
			entry_count++;
		}

		textblock_append(tb, "  ...and %u others.\n",
				remaining_monster_total);
	}
}

/**
 * Allow the standard list formatted to be bypassed for special cases.
 *
 * Returning true will bypass any other formatting in monster_list_format_textblock().
 *
 * \param list is the monster list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need
 * to be calculated.
 * \param max_lines is the maximum number of lines that can be displayed.
 * \param max_width is the maximum line width that can be displayed.
 * \param max_height_result is returned with the number of lines needed
 * to format the list without truncation.
 * \param max_width_result is returned with the width needed
 * to format the list without truncation.
 */
static bool monster_list_format_special(const monster_list_t *list,
		textblock *tb,
		int max_lines, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	(void) list;
	(void) max_lines;
	(void) max_width;

	if (player->timed[TMD_IMAGE] > 0) {
		const char *message =
			"Your hallucinations are too wild to see things clearly.";

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
static void monster_list_format_textblock(const monster_list_t *list,
		textblock *tb,
		size_t max_height, size_t max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);

	if (monster_list_format_special(list, tb,
				max_height, max_width, max_height_result, max_width_result))
	{
		return;
	}

	const unsigned los_entries =
		list->total_entries[MONSTER_LIST_SECTION_LOS];
	const unsigned esp_entries =
		list->total_entries[MONSTER_LIST_SECTION_ESP];

	unsigned header_lines = 1 + (esp_entries > 0 ? 2 : 0);

	if (max_height_result != NULL) {
		*max_height_result = header_lines + los_entries + esp_entries;
	}

	unsigned los_lines_to_display;
	unsigned esp_lines_to_display;

	if (header_lines < max_height) {
		size_t lines_remaining = max_height - header_lines;

		if (los_entries + esp_entries <= lines_remaining) {
			los_lines_to_display = los_entries;
			esp_lines_to_display = esp_entries;
		} else if (los_entries < lines_remaining) {
			/* Remove some ESP lines, leaving room for "...others" */
			los_lines_to_display = los_entries;
			esp_lines_to_display = lines_remaining - los_entries - 1;
		} else {
			/* Remove some LOS lines, leaving room for "...others" */
			los_lines_to_display = los_entries == lines_remaining ?
				lines_remaining : lines_remaining - 1;
			esp_lines_to_display = 0;
		}
	} else {
		assert(max_height == header_lines);

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
 * Display the object list statically. Contents will be adjusted accordingly.
 *
 * In order to be more efficient, this function uses a shared list object so
 * that it's not constantly allocating and freeing the list.
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
	monster_list_sort(list, monster_list_standard_compare);

	/* Draw the list to exactly fit the subwindow. */
	monster_list_format_textblock(list, tb, height, width, NULL, NULL);

	region reg = {0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}

/**
 * Display the monster list interactively.
 * This will dynamically size the list for the best appearance.
 */
void monster_list_show_interactive(void)
{
	textblock *tb = textblock_new();
	monster_list_t *list = monster_list_new();

	monster_list_collect(list);
	monster_list_sort(list, monster_list_standard_compare);

	/* Sufficiently large numbers are passed as the height and width limit so that
	 * we can calculate the number of rows and columns to display the list nicely */
	size_t max_width = ANGBAND_TERM_TEXTBLOCK_WIDTH;
	size_t max_height =
		list->total_entries[MONSTER_LIST_SECTION_LOS] +
		list->total_entries[MONSTER_LIST_SECTION_ESP] + 3;

	monster_list_format_textblock(list, NULL,
			max_height, max_width, &max_height, &max_width);

	/* Force max_width in order to avoid clipping the prompt */
	max_width = MAX(ANGBAND_TERM_STANDARD_WIDTH / 2, max_width);

	/* Actually draw the list. We pass in max_height to the format function so
	 * that all lines will be appended to the textblock. The textblock itself
	 * will handle fitting it into the region */
	monster_list_format_textblock(list, tb, max_height, max_width, NULL, NULL);

	region reg = {
		.w = max_width,
		.h = MIN(ANGBAND_TERM_STANDARD_HEIGHT, max_height)
	};

	textui_textblock_show(tb, TERM_POSITION_TOP_LEFT, reg, NULL);

	textblock_free(tb);
	monster_list_free(list);
}
