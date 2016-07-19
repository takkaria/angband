/**
 * \file ui-output.c
 * \brief Putting text on the screen, screen saving and loading, panel handling
 *
 * Copyright (c) 2007 Pete Mack and others.
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
#include "cave.h"
#include "player-calcs.h"
#include "ui2-input.h"
#include "ui2-output.h"
#include "z-textblock.h"

/* scroll when player is too close to the edge of a term
 * TODO this should be an option */
#define SCROLL_DISTANCE 3

/**
 * ------------------------------------------------------------------------
 * Regions
 * ------------------------------------------------------------------------ */

/**
 * These functions are used for manipulating regions on the screen, used 
 * mostly (but not exclusively) by the menu functions.
 */

region region_calculate(region loc)
{
	int w, h;
	Term_get_size(&w, &h);

	if (loc.col < 0) {
		loc.col += w;
	}
	if (loc.row < 0) {
		loc.row += h;
	}
	if (loc.width <= 0) {
		loc.width += w - loc.col;
	}
	if (loc.page_rows <= 0) {
		loc.page_rows += h - loc.row;
	}

	return loc;
}

void region_erase_bordered(const region *loc)
{
	region calc = region_calculate(*loc);

	calc.col = MAX(calc.col - 1, 0);
	calc.row = MAX(calc.row - 1, 0);
	calc.width += 2;
	calc.page_rows += 2;

	for (int y = 0; y < calc.page_rows; y++) {
		Term_erase(calc.col, calc.row + y, calc.width);
	}
}

void region_erase(const region *loc)
{
	region calc = region_calculate(*loc);

	for (int y = 0; y < calc.page_rows; y++) {
		Term_erase(calc.col, calc.row + y, calc.width);
	}
}

bool region_inside(const region *loc, const ui_event *event)
{
	return loc->col <= event->mouse.x
		&& loc->col + loc->width > event->mouse.x
		&& loc->row <= event->mouse.y
		&& loc->row + loc->page_rows > event->mouse.y;
}

/**
 * ------------------------------------------------------------------------
 * Text display
 * ------------------------------------------------------------------------ */

/**
 * These functions are designed to display large blocks of text on the screen
 * all at once.  They are the ui-term specific layer on top of the z-textblock.c
 * functions.
 */

/**
 * Utility function
 */
static void display_area(const wchar_t *text, const byte *attrs,
		size_t *line_starts, size_t *line_lengths,
		size_t n_lines,
		const region area,
		size_t cur_line)
{
	n_lines = MIN(n_lines, (size_t) area.page_rows);

	for (size_t y = 0; y < n_lines; y++, cur_line++) {
		Term_erase(area.col, area.row + y, area.width);

		for (size_t x = 0; x < line_lengths[cur_line]; x++) {
			size_t position = line_starts[cur_line] + x;

			Term_addwc(area.col + x, area.row + y,
					attrs[position], text[position]);
		}
	}
}

/**
 * Plonk a textblock on the screen in a certain bounding box.
 */
void textui_textblock_place(textblock *tb, region orig_area, const char *header)
{
	region area = region_calculate(orig_area);

	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;

	size_t n_lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, area.width);

	if (header != NULL) {
		area.page_rows--;
		c_prt(COLOUR_L_BLUE, header, area.row, area.col);
		area.row++;
	}

	if (n_lines > (size_t) area.page_rows) {
		n_lines = area.page_rows;
	}

	display_area(textblock_text(tb), textblock_attrs(tb),
			line_starts, line_lengths, n_lines, area, 0);

	mem_free(line_starts);
	mem_free(line_lengths);
}

/**
 * Show a textblock interactively
 */
void textui_textblock_show(textblock *tb, region orig_area, const char *header)
{
	region area = region_calculate(orig_area);

	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	size_t n_lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, area.width);

	struct term_hints hints = {
		.row = area.row,
		.col = area.col,
		.width = area.width,
		.height = area.page_rows,
		.position = TERM_POSITION_EXACT,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	
	/* make room for the footer */
	area.page_rows -= 2;

	if (header != NULL) {
		area.page_rows--;
		c_prt(COLOUR_L_BLUE, header, area.row, area.col);
		area.row++;
	}

	if (n_lines > (size_t) area.page_rows) {
		int start_line = 0;

		c_prt(COLOUR_WHITE, "", area.page_rows, area.col);
		c_prt(COLOUR_L_BLUE, "(Up/down or ESCAPE to exit.)",
				area.page_rows + 1, area.col);

		/* Pager mode */
		while (true) {
			display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
					line_lengths, n_lines, area, start_line);

			struct keypress ch = inkey();

			switch (ch.code) {
				case ARROW_UP:
					start_line--;
					break;
				case ARROW_DOWN:
					start_line++;
					break;
				case ' ':
					start_line += area.page_rows;
					break;
				case ESCAPE: /* fallthru */
				case 'q':
					break;
			}

			if (start_line < 0) {
				start_line = 0;
			} else if ((size_t) (start_line + area.page_rows) > n_lines) {
				start_line = n_lines - area.page_rows;
			}
		}
	} else {
		display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
				line_lengths, n_lines, area, 0);

		c_prt(COLOUR_WHITE, "", n_lines, area.col);
		c_prt(COLOUR_L_BLUE, "(Press any key to continue.)",
				n_lines + 1, area.col);
		inkey();
	}

	mem_free(line_starts);
	mem_free(line_lengths);

	Term_pop();
}

/**
 * Hack -- Where to wrap the text when using text_out().  Use the default
 * value (for example the screen width) when 'text_out_wrap' is 0.
 */
int text_out_wrap = 0;

/**
 * Hack -- Indentation for the text when using text_out().
 */
int text_out_indent = 0;

/**
 * Hack -- Padding after wrapping
 */
int text_out_pad = 0;

static void text_out_newline(int *col, int *row)
{
	int x = *col;
	int y = *row;

	x = text_out_indent;
	y++;
	Term_erase(x, y, Term_width());
	x += text_out_pad;
	Term_cursor_to_xy(x, y);

	*col = x;
	*row = y;
}

/**
 * Print some (colored) text to the screen at the current cursor position,
 * automatically "wrapping" existing text (at spaces) when necessary to
 * avoid placing any text into the last column, and clearing every line
 * before placing any text in that line.  Also, allow "newline" to force
 * a "wrap" to the next line.  Advance the cursor as needed so sequential
 * calls to this function will work correctly.
 *
 * Once this function has been called, the cursor should not be moved
 * until all the related "text_out()" calls to the window are complete.
 */
static void text_out_to_screen(uint32_t attr, const char *str)
{
	const int width = Term_width();
	const int wrap = text_out_wrap > 0 && text_out_wrap < width ?
		text_out_wrap - 1 : width - 1;

	int x;
	int y;
	Term_get_cursor(&x, &y, NULL, NULL);

	wchar_t buf[1024];
	text_mbstowcs(buf, str, sizeof(buf));
	
	for (const wchar_t *ws = buf; *ws; ws++) {
		if (*ws == L'\n') {
			text_out_newline(&x, &y);
			continue;
		}

		wchar_t ch = iswprint(*ws) ? *ws : L' ';

		/* Wrap words as needed */
		if (x >= wrap) {
			if (ch == L' ') {
				text_out_newline(&x, &y);
				continue;
			}

			struct term_point points[wrap];

			int space = 0;
			for (int w = wrap - 1; w >= 0; w--) {
				Term_get_point(w, y, &points[w]);

				if (points[w].fg_char == L' ') {
					break;
				} else {
					space = w;
				}
			}

			if (space != 0) {
				Term_erase(space, y, width);
				text_out_newline(&x, &y);

				while (space < wrap) {
					Term_putwc(points[space].fg_attr, points[space].fg_char);
					space++;
					x++;
				}
			} else {
				text_out_newline(&x, &y);
			}
		}

		Term_putwc(attr, ch);
		x++;
	}
}

/**
 * Output text to the screen
 */
void text_out(const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	text_out_to_screen(COLOUR_WHITE, buf);
}

/**
 * Output text to the screen (in color)
 */
void text_out_c(byte a, const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	text_out_to_screen(a, buf);
}

/**
 * Given a "formatted" chunk of text (i.e. one including tags like {red}{/})
 * in 'source', with starting point 'init', this finds the next section of
 * text and any tag that goes with it, return true if it finds something to 
 * print.
 * 
 * If it returns true, then it also fills 'text' with a pointer to the start
 * of the next printable section of text, and 'len' with the length of that 
 * text, and 'end' with a pointer to the start of the next section.  This
 * may differ from "text + len" because of the presence of tags.  If a tag
 * applies to the section of text, it returns a pointer to the start of that
 * tag in 'tag' and the length in 'taglen'.  Otherwise, 'tag' is filled with
 * NULL.
 *
 * See text_out_e for an example of its use.
 */
static bool next_section(const char *source, size_t init,
		const char **text, size_t *len,
		const char **tag, size_t *taglen,
		const char **end)
{
	const char *next;	

	*tag = NULL;
	*text = source + init;
	if (*text[0] == '\0') {
		return false;
	}

	next = strchr(*text, '{');
	while (next) {
		const char *s = next + 1;

		while (*s && (isalpha((unsigned char) *s)
					|| isspace((unsigned char) *s)))
		{
			s++;
		}

		/* valid opening tag */
		if (*s == '}') {
			const char *close = strstr(s, "{/}");

			/* There's a closing thing, so it's valid. */
			if (close) {
				/* If this tag is at the start of the fragment */
				if (next == *text) {
					*tag = *text + 1;
					*taglen = s - *text - 1;
					*text = s + 1;
					*len = close - *text;
					*end = close + 3;
					return true;
				} else {
					/* Otherwise return the chunk up to this */
					*len = next - *text;
					*end = *text + *len;
					return true;
				}
			} else {
				/* No closing thing, therefore all one lump of text. */
				*len = strlen(*text);
				*end = *text + *len;
				return true;
			}
		} else if (*s == '\0') {
			/* End of the string, that's fine. */
			*len = strlen(*text);
			*end = *text + *len;
			return true;
		} else {
			/* An invalid tag, skip it. */
			next = next + 1;
		}

		next = strchr(next, '{');
	}

	/* Default to the rest of the string */
	*len = strlen(*text);
	*end = *text + *len;

	return true;
}

/**
 * Output text to the screen or to a file depending on the
 * selected hook.  Takes strings with "embedded formatting",
 * such that something within {red}{/} will be printed in red.
 *
 * Note that such formatting will be treated as a "breakpoint"
 * for the printing, so if used within words may lead to part of the
 * word being moved to the next line.
 */
void text_out_e(const char *fmt, ...)
{
	char buf[1024];
	char smallbuf[1024];
	va_list vp;

	const char *start, *next, *text, *tag;
	size_t textlen, taglen = 0;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	start = buf;
	while (next_section(start, 0, &text, &textlen, &tag, &taglen, &next)) {
		int a = -1;

		memcpy(smallbuf, text, textlen);
		smallbuf[textlen] = 0;

		if (tag) {
			char tagbuffer[16];

			/* Colour names are less than 16 characters long. */
			assert(taglen < 16);

			memcpy(tagbuffer, tag, taglen);
			tagbuffer[taglen] = '\0';

			a = color_text_to_attr(tagbuffer);
		}
		
		if (a == -1) {
			a = COLOUR_WHITE;
		}

		/* Output now */
		text_out_to_screen(a, smallbuf);

		start = next;
	}
}

/**
 * ------------------------------------------------------------------------
 * Simple text display
 * ------------------------------------------------------------------------ */

/**
 * Display a string on the screen using an attribute.
 *
 * At the given location, using the given attribute, if allowed,
 * add the given string.  Do not clear the line. Ignore strings
 * with invalid coordinates.
 */
void c_put_str(uint32_t attr, const char *str, int row, int col) {
	/* Position cursor, Dump the attr/text */
	if (Term_point_ok(col, row)) {
		Term_adds(col, row, Term_width(), attr, str);
	}
}

/**
 * As above, but in white
 */
void put_str(const char *str, int row, int col) {
	c_put_str(COLOUR_WHITE, str, row, col);
}

/**
 * Display a string on the screen using an attribute, and clear to the
 * end of the line. Ignore strings with invalid coordinates.
 */
void c_prt(uint32_t attr, const char *str, int row, int col) {
	if (Term_point_ok(col, row)) {
		int width = Term_width();

		Term_erase(col, row, width);
		Term_adds(col, row, width, attr, str);
	}
}

/**
 * As above, but in white
 */
void prt(const char *str, int row, int col) {
	c_prt(COLOUR_WHITE, str, row, col);
}

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------ */

/**
 * A Hengband-like 'window' function, that draws a surround box in ASCII art.
 */
void window_make(int origin_x, int origin_y, int end_x, int end_y)
{
	region to_clear = {
		.col = origin_x,
		.row = origin_y,
		.width = end_x - origin_x,
		.page_rows = end_y - origin_y
	};

	region_erase(&to_clear);

	Term_addwc(origin_x, origin_y, COLOUR_WHITE, L'+');
	Term_addwc(end_x, origin_y, COLOUR_WHITE, L'+');
	Term_addwc(origin_x, end_y, COLOUR_WHITE, L'+');
	Term_addwc(end_x, end_y, COLOUR_WHITE, L'+');

	for (int n = 1; n < end_x - origin_x; n++) {
		Term_addwc(origin_x + n, origin_y, COLOUR_WHITE, L'-');
		Term_addwc(origin_x + n, end_y, COLOUR_WHITE, L'-');
	}

	for (int n = 1; n < end_y - origin_y; n++) {
		Term_addwc(origin_x, origin_y + n, COLOUR_WHITE, L'|');
		Term_addwc(end_x, origin_y + n, COLOUR_WHITE, L'|');
	}
}

static void panel_fix_coords(const struct angband_term *aterm, int *y, int *x)
{
	Term_push(aterm->term);

	int width;
	int height;
	Term_get_size(&width, &height);
	Term_pop();

	int curx = *x;
	int cury = *y;
	int maxx = cave->width - width;
	int maxy = cave->height - height;

	*x = MAX(0, MIN(curx, maxx));
	*y = MAX(0, MIN(cury, maxy));
}

bool panel_should_modify(const struct angband_term *aterm, int y, int x)
{
	panel_fix_coords(aterm, &y, &x);

	return aterm->offset_y != y || aterm->offset_x != x;
}

/**
 * Modify the current panel to the given coordinates, adjusting only to
 * ensure the coordinates are legal, and return true if anything done.
 *
 * The town should never be scrolled around.
 *
 * Note that monsters are no longer affected in any way by panel changes.
 *
 * As a total hack, whenever the current panel changes, we assume that
 * the "overhead view" window should be updated.
 */
bool modify_panel(struct angband_term *aterm, int y, int x)
{
	panel_fix_coords(aterm, &y, &x);

	if (aterm->offset_x != x || aterm->offset_y != y) {
		aterm->offset_x = x;
		aterm->offset_y = y;

		player->upkeep->redraw |= PR_MAP;

		return true;
	} else {
		return false;
	}
}

static void verify_panel_int(struct angband_term *aterm, bool centered)
{
	Term_push(aterm->term);

	int term_width;
	int term_height;
	Term_get_size(&term_width, &term_height);

	Term_pop();

	int offset_x = aterm->offset_x;
	int offset_y = aterm->offset_y;

	int player_x = player->px;
	int player_y = player->py;

	/* center screen horizontally
	 * when off-center and not running
	 * OR
	 * when SCROLL_DISTANCE grids from top/bottom edge */
	if ((centered && !player->upkeep->running
				&& player_x != offset_x + term_width / 2)
			|| (player_x < offset_x + SCROLL_DISTANCE
				|| player_x >= offset_x + term_width - SCROLL_DISTANCE))
	{
		offset_x = player_x - term_width / 2;
	}

	/* center screen vertically
	 * when off-center and not running
	 * OR
	 * when SCROLL_DISTANCE grids from top/bottom edge */
	if ((centered && !player->upkeep->running
				&& player_y != offset_y + term_height / 2)
			|| (player_y < offset_y + SCROLL_DISTANCE
				|| player_y >= offset_y + term_height - SCROLL_DISTANCE))
	{
		offset_y = player_y - term_height / 2;
	}

	modify_panel(aterm, offset_y, offset_x);
}

/**
 * Change the panels to the panel lying in the given direction.
 * Return true if any panel was changed.
 */
bool change_panel(struct angband_term *aterm, int dir)
{
	Term_push(aterm->term);

	int term_width;
	int term_height;
	Term_get_size(&term_width, &term_height);

	Term_pop();

	/* Shift by half a panel */
	int offset_x = aterm->offset_x + ddx[dir] * term_width / 2;
	int offset_y = aterm->offset_y + ddy[dir] * term_height / 2;

	if (modify_panel(aterm, offset_y, offset_x)) {
		return true;
	} else {
		return false;
	}
}

/**
 * Verify the current panel (relative to the player location).
 *
 * By default, when the player gets "too close" to the edge of the current
 * panel, the map scrolls one panel in that direction so that the player
 * is no longer so close to the edge.
 *
 * The "OPT(center_player)" option allows the current panel to always be
 * centered around the player, which is very expensive, and also has some
 * interesting gameplay ramifications.
 */
void verify_panel(struct angband_term *aterm)
{
	verify_panel_int(aterm, OPT(center_player));
}

void center_panel(struct angband_term *aterm)
{
	verify_panel_int(aterm, true);
}

void textui_get_panel(int *min_y, int *min_x, int *max_y, int *max_x)
{
	struct angband_term *aterm = &angband_cave;

	Term_push(aterm->term);

	int width;
	int height;
	Term_get_size(&width, &height);

	Term_pop();

	*min_y = aterm->offset_y;
	*min_x = aterm->offset_x;
	*max_y = aterm->offset_y + width;
	*max_x = aterm->offset_x + height;
}

bool textui_panel_contains(unsigned int y, unsigned int x)
{
	struct angband_term *aterm = &angband_cave;

	Term_push(aterm->term);

	int width;
	int height;
	Term_get_size(&width, &height);

	Term_pop();

	return (int) x >= aterm->offset_x
		&& (int) x <  aterm->offset_x + width
		&& (int) y >= aterm->offset_y
		&& (int) y <  aterm->offset_y + height;
}

/**
 * Clear the bottom part of the screen
 */
void clear_from(int row)
{
	int width;
	int height;
	Term_get_size(&width, &height);

	for (int y = row; y < height; y++) {
		Term_erase(0, y, width);
	}
}
