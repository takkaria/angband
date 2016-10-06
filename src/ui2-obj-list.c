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
#include "obj-desc.h"
#include "obj-list.h"
#include "obj-util.h"
#include "ui2-object.h"
#include "ui2-obj-list.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "z-textblock.h"

/**
 * This uses the default logic of object_desc() in order
 * to handle flavors, artifacts, vowels and so on.
 *
 * \param entry is the object list entry that has a name to be formatted.
 * \param section is section of the entry (LOS or NO_LOS)
 * \param line_buffer is the buffer to format into.
 * \param size is the size of line_buffer.
 */
static size_t object_list_entry_name(const object_list_entry_t *entry,
		object_list_section_t section, char *buf, size_t bufsize)
{
	assert(entry != NULL);
	assert(entry->object != NULL);
	assert(entry->object->kind != NULL);

	char name[ANGBAND_TERM_STANDARD_WIDTH];

	/* Because each entry points to a specific object and not something more
	 * general, the number of similar objects we counted has to be swapped in.
	 * This isn't an ideal way to do this, but it's the easiest way until
	 * object_desc() is more flexible. */
	int old_number = entry->object->number;

	entry->object->number = entry->count[section];
	object_desc(name, sizeof(name),
			cave->objects[entry->object->oidx], ODESC_PREFIX | ODESC_FULL);
	entry->object->number = old_number;

	my_strcpy(buf, " ", sizeof(buf));

	return my_strcat(buf, name, sizeof(name));
}

/**
 * As an optimization, we don't want to do anything with the
 * buffer if there is no textblock (since the buffer will not
 * be appended to it; see object_list_show_interactive()).
 */
static void maybe_clipto(char *buf, size_t clip, const textblock *tb)
{
	if (tb != NULL) {
		utf8_clipto(buf, clip);
	}
}

/**
 * This function is called from object_list_format_section()
 *
 * \param entry is the object list entry to process
 * \param section is the section of the entry (LOS or NO_LOS)
 * \param tb is the textblock to add text to or NULL if only the dimensions
 * need to be calculated
 * \param max_width is the maximum line width that can be displayed
 * \param max_line_length is updated with the length of the string to display
 */
static void object_list_process_entry(const object_list_entry_t *entry,
		textblock *tb, object_list_section_t section,
		size_t max_width, size_t *max_line_length)
{
	char name[ANGBAND_TERM_STANDARD_WIDTH] = "";
	char coords[ANGBAND_TERM_STANDARD_WIDTH / 2] = "";

	/* object tile */
	size_t pict_w = 1;

	/* object coordinates */
	const char *n_or_s = entry->dy <= 0 ? "N" : "S";
	const char *w_or_e = entry->dx <= 0 ? "W" : "E";
	size_t coords_w = strnfmt(coords, sizeof(coords),
			" %d %s %d %s",
			abs(entry->dy), n_or_s,
			abs(entry->dx), w_or_e);

	/* object name */
	size_t name_w =
		object_list_entry_name(entry, section, name, sizeof(name));

	if (pict_w + name_w + coords_w <= max_width) {
		/* there is enough space for everything */;
	} else if (pict_w + coords_w <= max_width) {
		name_w = max_width - pict_w - coords_w;
		maybe_clipto(name, name_w, tb);
	} else {
		assert(max_width >= pict_w);

		name_w = 0;
		maybe_clipto(name, name_w, tb);

		coords_w = max_width - pict_w;
		maybe_clipto(coords, coords_w, tb);
	}

	*max_line_length =
		MAX(*max_line_length, pict_w + name_w + coords_w);

	if (tb != NULL) {
		uint32_t attr = COLOUR_RED;
		wchar_t ch = L'*';
		if (!is_unknown(entry->object) && entry->object->kind != NULL) {
			attr = object_kind_attr(entry->object->kind);
			ch = object_kind_char(entry->object->kind);
		}
		textblock_append_pict(tb, attr, ch);

		textblock_append_c(tb,
				object_list_entry_line_attribute(entry), "%s", name); 

		assert(max_width >= pict_w + name_w + coords_w);

		/* Hack - because object_name strings are UTF8, we have to add some padding
		 * for any raw bytes that might be consolidated into one displayed character */
		int coords_width = max_width - pict_w - name_w +
			(strlen(name) - utf8_strlen(name));
		textblock_append(tb, "%*s\n", coords_width, coords);
	}
}

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
		textblock *tb,
		object_list_section_t section,
		int lines_to_display, int max_width,
		const char *prefix, bool show_others,
		size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);
	assert(max_width > 0);

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (list->total_entries[section] == 0) {
		size_t len = strnfmt(buf, sizeof(buf), "%s no objects.\n", prefix);

		if (tb != NULL) {
			textblock_append(tb, "%s", buf);
		}

		if (max_width_result != NULL) {
			*max_width_result = len;
		}

		return;
	}

	size_t max_line_length = strnfmt(buf, sizeof(buf),
			"%s %d %sobject%s%s\n",
			prefix,
			list->total_entries[section],
			show_others ? "other " : "",
			PLURAL(list->total_entries[section]),
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
		const object_list_entry_t *entry = &list->entries[entry_count];

		if (entry->count[section] > 0) {
			object_list_process_entry(entry, tb, section,
					max_width, &max_line_length);
			line_count++;
		}
	}

	/* Don't worry about the "...others" line, since it's probably shorter than
	 * what's already printed, and if not, it will be split into several lines
	 * by the textblock display functions (textui_textblock_show/place()) */
	if (max_width_result != NULL) {
		*max_width_result = max_line_length;
	}

	if (lines_to_display > 0
			&& lines_to_display < list->total_entries[section])
	{
		/* Count the remaining objects, starting
		 * where we left off in the above loop. */
		int remaining_object_total =
			list->distinct_entries - entry_count;

		if (tb != NULL) {
			textblock_append(tb, "  ...and %d others.\n",
					remaining_object_total);
		}
	}
}

/**
 * Format the entire object list with the given parameters.
 *
 * This function can be used to calculate the preferred dimensions for the list
 * by passing in a NULL textblock.
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
		textblock *tb, int max_height, int max_width,
		size_t *max_height_result, size_t *max_width_result)
{
	assert(list != NULL);
	assert(list->entries != NULL);

	const int los_entries = list->total_entries[OBJECT_LIST_SECTION_LOS];
	const int no_los_entries = list->total_entries[OBJECT_LIST_SECTION_NO_LOS];

	int header_lines = 1 + (no_los_entries > 0 ? 2 : 0);

 	if (max_height_result != NULL) {
		*max_height_result = header_lines + los_entries + no_los_entries;
	}

	int los_lines_to_display;
	int no_los_lines_to_display;

	if (header_lines < max_height) {
		int lines_remaining = max_height - header_lines;

		if (los_entries + no_los_entries <= lines_remaining) {
			los_lines_to_display = los_entries;
			no_los_lines_to_display = no_los_entries;
		} else if (los_entries <= lines_remaining) {
			/* Remove some NO_LOS lines, leaving room for "...others" */
			los_lines_to_display = los_entries;
			no_los_lines_to_display = MAX(lines_remaining - los_entries - 1, 0);
		} else {
			/* Remove some LOS lines, leaving room for "...others" */
			los_lines_to_display = MAX(lines_remaining - 1, 0);
			no_los_lines_to_display = 0;
		}
	} else {
		assert(max_height == header_lines);

		los_lines_to_display = 0;
		no_los_lines_to_display = 0;
	}

	size_t max_los_line = 0;
	size_t max_no_los_line = 0;

	object_list_format_section(list, tb,
			OBJECT_LIST_SECTION_LOS,
			los_lines_to_display, max_width,
			"You can see", false,
			&max_los_line);

	if (no_los_entries > 0) {
         if (tb != NULL) {
			 textblock_append(tb, "\n");
		 }

         object_list_format_section(list, tb,
				 OBJECT_LIST_SECTION_NO_LOS,
				 no_los_lines_to_display, max_width,
				 "You are aware of", los_entries > 0,
				 &max_no_los_line);
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

	region reg = {0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}

/**
 * Display the object list interactively.
 * This will dynamically size the list for the best appearance.
 */
void object_list_show_interactive(void)
{
	textblock *tb = textblock_new();
	object_list_t *list = object_list_new();

	object_list_collect(list);
	object_list_sort(list, object_list_standard_compare);

	/* Sufficiently large numbers are passed as the height and width limit so that
	 * we can calculate the number of rows and columns to display the list nicely */
	size_t max_width = ANGBAND_TERM_TEXTBLOCK_WIDTH;
	size_t max_height =
		list->total_entries[OBJECT_LIST_SECTION_LOS] +
		list->total_entries[OBJECT_LIST_SECTION_NO_LOS] + 3;

	object_list_format_textblock(list, NULL,
			max_height, max_width, &max_height, &max_width);

	/* Force max_width in order to avoid clipping the prompt */
	max_width = MAX(ANGBAND_TERM_STANDARD_WIDTH / 2, max_width);

	/* Actually draw the list. We pass in max_height to the format function so
	 * that all lines will be appended to the textblock. The textblock itself
	 * will handle fitting it into the region */
	object_list_format_textblock(list, tb, max_height, max_width, NULL, NULL);

	region reg = {
		.w = max_width,
		.h = MIN(ANGBAND_TERM_STANDARD_HEIGHT, max_height)
	};

	textui_textblock_show(tb, TERM_POSITION_TOP_LEFT, reg, NULL);

	textblock_free(tb);
	object_list_free(list);
}
