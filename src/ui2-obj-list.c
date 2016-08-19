/**
 * \file ui2-obj-list.c
 * \brief Object list UI.
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

#include "angband.h"
#include "init.h"
#include "obj-list.h"
#include "obj-util.h"
#include "ui2-object.h"
#include "ui2-obj-list.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "z-textblock.h"

/**
 * Format a section of the object list: a header followed by object list entry
 * rows.
 *
 * This function will process each entry for the given section. It will display:
 * - object char;
 * - number of objects;
 * - object name (truncated, if needed to fit the line);
 * - object distance from the player (aligned to the right side of the list).
 * By passing in a NULL textblock, the maximum line width of the section can
 * be found.
 *
 * \param list is the object list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param lines_to_display are the number of entries to display (not including
 * the header).
 * \param max_width is the maximum line width.
 * \param prefix is the beginning of the header; the remainder is appended with
 * the number of objects.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void object_list_format_section(const object_list_t *list,
		textblock *tb, object_list_section_t section,
		int lines_to_display, int max_width,
		const char *prefix, bool show_others, size_t *max_width_result)
{
	const char *punctuation = lines_to_display == 0 ? "." : ":";
	const char *others = show_others ? "other " : "";

	if (list == NULL || list->entries == NULL) {
		return;
	}

	char line_buffer[200];

	if (list->total_entries[section] == 0) {
		size_t max_line_length =
			strnfmt(line_buffer, sizeof(line_buffer), "%s no objects.\n", prefix);

		if (tb != NULL) {
			textblock_append(tb, "%s", line_buffer);
		}

		/* Force a minimum width so that the prompt doesn't get cut off. */
		if (max_width_result != NULL) {
			*max_width_result = MAX(max_line_length, 40);
		}

		return;
	}

	size_t max_line_length = strnfmt(line_buffer, sizeof(line_buffer),
			"%s %d %sobject%s%s\n",
			prefix,
			list->total_entries[section],
			others,
			PLURAL(list->total_entries[section]),
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
		char location[20] = {0};
		line_buffer[0] = 0;

		if (list->entries[entry].count[section] == 0) {
			continue;
		}

		const char *n_or_s = list->entries[entry].dy <= 0 ? "N" : "S";
		const char *w_or_e = list->entries[entry].dx <= 0 ? "W" : "E";

		/* Build the location string. */
		strnfmt(location, sizeof(location),
				" %d %s %d %s",
				abs(list->entries[entry].dy), n_or_s,
				abs(list->entries[entry].dx), w_or_e);

		/* Get width available for object name: 2 for char and space;
		 * location includes padding; last -1 because textblock (in effect)
		 * adds one column of padding on the right side of any string */
		size_t full_width = max_width - 2 - utf8_strlen(location) - 1;

		/* Add the object count and clip the object name to fit. */
		object_list_format_name(&list->entries[entry], line_buffer, sizeof(line_buffer));
		utf8_clipto(line_buffer, full_width);

		/* Calculate the width of the line for dynamic sizing; use a fixed max
		 * width for location and object char. */
		max_line_length = MAX(max_line_length, utf8_strlen(line_buffer) + 12 + 2);

		/* textblock_append_pict() will add the object symbol only in text mode,
		 * despite its name (should really be renamed) */
		if (tb != NULL && !use_graphics) {
			uint32_t attr = COLOUR_RED;
			wchar_t ch = L'*';

			if (!is_unknown(list->entries[entry].object)
					&& list->entries[entry].object->kind != NULL)
			{
				attr = object_kind_attr(list->entries[entry].object->kind);
				ch = object_kind_char(list->entries[entry].object->kind);
			}

			textblock_append_pict(tb, attr, ch);
			textblock_append(tb, " ");
		}

		/* Add the left-aligned and padded object name which will align the
		 * location to the right. */
		if (tb != NULL) {
			uint32_t line_attr = object_list_entry_line_attribute(&list->entries[entry]);
			/*
			 * Because object name strings are UTF8, we have to add
			 * additional padding for any raw bytes that might be consolidated
			 * into one displayed character.
			 */
			full_width += strlen(line_buffer) - utf8_strlen(line_buffer);
			textblock_append_c(tb, line_attr,
					"%-*s%s\n", full_width, line_buffer, location);
		}

		line_count++;
	}

	/* Don't worry about the "...others" line, since it's probably shorter than
	 * what's already printed. */
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

	/* Count the remaining objects, starting where we left off in the above loop. */
	int remaining_object_total = list->distinct_entries - entry;

	if (tb != NULL) {
		textblock_append(tb, "%6s...and %d others.\n", " ", remaining_object_total);
	}
}

/**
 * Allow the standard list formatted to be bypassed for special cases.
 *
 * Returning true will bypass any other formatteding in
 * object_list_format_textblock().
 *
 * \param list is the object list to format.
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
static bool object_list_format_special(const object_list_t *list,
		textblock *tb, int max_lines, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	(void) list;
	(void) tb;
	(void) max_lines;
	(void) max_width;
	(void) max_height_result;
	(void) max_width_result;

	return false;
}

/**
 * Format the entire object list with the given parameters.
 *
 * This function can be used to calculate the preferred dimensions for the list
 * by passing in a
 * NULL textblock. This function calls object_list_format_special() first; if
 * that function
 * returns true, it will bypass normal list formatting.
 *
 * \param list is the object list to format.
 * \param tb is the textblock to produce or NULL if only the dimensions need to
 * be calculated.
 * \param max_lines is the maximum number of lines that can be displayed.
 * \param max_width is the maximum line width that can be displayed.
 * \param max_height_result is returned with the number of lines needed to
 * format the list without truncation.
 * \param max_width_result is returned with the width needed to format the list
 * without truncation.
 */
static void object_list_format_textblock(const object_list_t *list,
		textblock *tb, int max_lines, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	if (list == NULL || list->entries == NULL) {
		return;
	}

	if (object_list_format_special(list,
				tb, max_lines, max_width, max_height_result, max_width_result))
	{
		return;
	}

	int los_lines_to_display = list->total_entries[OBJECT_LIST_SECTION_LOS];
	int no_los_lines_to_display = list->total_entries[OBJECT_LIST_SECTION_NO_LOS];
                                                          
	int header_lines = 1;

	if (list->total_entries[OBJECT_LIST_SECTION_NO_LOS] > 0) {
		header_lines += 2;
	}

 	if (max_height_result != NULL) {
		*max_height_result =
			header_lines + los_lines_to_display + no_los_lines_to_display;
	}

	int lines_remaining =
		max_lines - header_lines - list->total_entries[OBJECT_LIST_SECTION_LOS];

	/* Remove non-los lines as needed */
	if (lines_remaining < list->total_entries[OBJECT_LIST_SECTION_NO_LOS]) {
		no_los_lines_to_display = MAX(lines_remaining - 1, 0);
	}

	/* If we don't even have enough room for the NO_LOS header, start removing
	 * LOS lines, leaving one for the "...others". */
	if (lines_remaining < 0) {
		los_lines_to_display =
			list->total_entries[OBJECT_LIST_SECTION_LOS] - abs(lines_remaining) - 1;
	}

	/* Display only headers if we don't have enough space. */
	if (header_lines >= max_lines) {
		los_lines_to_display = 0;
		no_los_lines_to_display = 0;
	}
        
	size_t max_los_line = 0;
	size_t max_no_los_line = 0;

	object_list_format_section(list, tb,
			OBJECT_LIST_SECTION_LOS, los_lines_to_display, max_width,
			"You can see", false, &max_los_line);

	if (list->total_entries[OBJECT_LIST_SECTION_NO_LOS] > 0) {
         bool show_others = list->total_objects[OBJECT_LIST_SECTION_LOS] > 0;

         if (tb != NULL) {
			 textblock_append(tb, "\n");
		 }

         object_list_format_section(list, tb,
				 OBJECT_LIST_SECTION_NO_LOS, no_los_lines_to_display, max_width,
				 "You are aware of", show_others, &max_no_los_line);
	}

	if (max_width_result != NULL) {
		*max_width_result = MAX(max_los_line, max_no_los_line);
	}
}

/**
 * Display the object list statically. Contents will be adjusted accordingly.
 *
 * In order to be more efficient, this function uses a shared list object so
 * that it's not constantly allocating and freeing the list.
 */
void object_list_show_subwindow(void)
{
	int width;
	int height;
	Term_get_size(&width, &height);

	textblock *tb = textblock_new();
	object_list_t *list = object_list_shared_instance();

	object_list_reset(list);
	object_list_collect(list);
	object_list_sort(list, object_list_standard_compare);

	/* Draw the list to exactly fit the subwindow. */
	object_list_format_textblock(list, tb, height, width, NULL, NULL);

	region reg = {0, 0, 0, 0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}

/**
 * Display the object list interactively. This will dynamically size the list
 * for the best appearance. This should only be used in the main term.
 *
 * \param height is the height limit for the list.
 * \param width is the width limit for the list.
 */
void object_list_show_interactive(void)
{
	textblock *tb = textblock_new();
	object_list_t *list = object_list_new();

	object_list_collect(list);
	object_list_sort(list, object_list_standard_compare);

	/*
	 * Large numbers are passed as the height and width limit so that we can
	 * calculate the maximum number of rows and columns to display the list nicely.
	 */
	size_t max_width = 0;
	size_t max_height = 0;
	object_list_format_textblock(list, NULL, 1000, 1000, &max_height, &max_width);

	/*
	 * Actually draw the list. We pass in max_height to the format function so
	 * that all lines will be appended to the textblock. The textblock itself
	 * will handle fitting it into the region.
	 */
	object_list_format_textblock(list, tb, max_height, max_width, NULL, NULL);

	region reg = {
		.w = max_width,
		.h = max_height
	};

	textui_textblock_show(tb, reg, NULL);

	textblock_free(tb);
	object_list_free(list);
}
