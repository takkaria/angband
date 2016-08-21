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
#include "z-textblock.h"
#include "ui2-input.h"
#include "ui2-output.h"

/**
 * ------------------------------------------------------------------------
 * Regions
 * ------------------------------------------------------------------------
 */

/**
 * These functions are used for manipulating regions on the screen, used 
 * mostly (but not exclusively) by the menu functions.
 */

region region_calculate(region reg)
{
	int width;
	int height;
	Term_get_size(&width, &height);

	if (reg.x < 0) {
		reg.x += width;
	}
	if (reg.y < 0) {
		reg.y += height;
	}
	if (reg.w <= 0) {
		reg.w += width - reg.x;
	}
	if (reg.h <= 0) {
		reg.h += height - reg.y;
	}

	return reg;
}

void region_erase_bordered(region reg)
{
	region calc = region_calculate(reg);

	calc.x = MAX(calc.x - 1, 0);
	calc.y = MAX(calc.y - 1, 0);
	calc.w += 2;
	calc.h += 2;

	for (int y = 0; y < calc.h; y++) {
		Term_erase(calc.x, calc.y + y, calc.w);
	}
}

void region_erase(region reg)
{
	region calc = region_calculate(reg);

	for (int y = 0; y < calc.h; y++) {
		Term_erase(calc.x, calc.y + y, calc.w);
	}
}

bool loc_in_region(struct loc loc, region reg)
{
	return loc.x >= reg.x
		&& loc.x <  reg.x + reg.w
		&& loc.y >= reg.y
		&& loc.y <  reg.y + reg.h;
}

bool mouse_in_region(struct mouseclick mouse, region reg)
{
	struct loc loc = {mouse.x, mouse.y};

	return loc_in_region(loc, reg);
}

/**
 * ------------------------------------------------------------------------
 * Text display
 * ------------------------------------------------------------------------
 */

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
		size_t n_lines, size_t cur_line, region area)
{
	assert(area.w > 0);
	assert(area.h > 0);

	for (size_t l = 0, lines = MIN(n_lines, (size_t) area.h);
			l < lines;
			l++, cur_line++)
	{
		Term_erase(area.x, area.y + l, area.w);

		for (size_t x = 0; x < line_lengths[cur_line]; x++) {
			size_t position = line_starts[cur_line] + x;

			Term_addwc(area.x + x, area.y + l,
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
			&line_starts, &line_lengths, area.w);

	if (header != NULL) {
		area.h--;
		c_prt(COLOUR_L_BLUE, header, loc(area.x, area.y));
		area.y++;
	}

	if (n_lines > (size_t) area.h) {
		n_lines = area.h;
	}

	display_area(textblock_text(tb), textblock_attrs(tb),
			line_starts, line_lengths, n_lines, 0, area);

	mem_free(line_starts);
	mem_free(line_lengths);
}

/**
 * Show a textblock interactively
 */
void textui_textblock_show(textblock *tb, region orig_area, const char *header)
{
	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;

	int term_width = orig_area.w > 0 ? orig_area.w : ANGBAND_TERM_STANDARD_WIDTH;

	size_t n_lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, term_width);

	/* Add one line for the header (if present) */
	int term_height =
		(orig_area.h > 0 ? orig_area.h : (int) n_lines) + (header ? 1 : 0);

	struct term_hints hints = {
		.width = term_width,
		.height = term_height,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	region area = region_calculate(orig_area);

	if (header != NULL) {
		c_prt(COLOUR_L_BLUE, header, loc(area.x, 0));
		area.y++;
		area.h--;
	}

	assert(area.w > 0);
	assert(area.h > 0);

	if (n_lines > (size_t) area.h) {
		show_prompt("(Up/down or ESCAPE to exit.)", false);

		int start_line = 0;
		bool done = false;

		/* Pager mode */
		while (!done) {
			display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
					line_lengths, n_lines, start_line, area);

			Term_flush_output();

			struct keypress ch = inkey_only_key();

			switch (ch.code) {
				case ARROW_UP:
					start_line--;
					break;
				case ARROW_DOWN:
					start_line++;
					break;
				case ' ':
					start_line += area.h;
					break;
				case ESCAPE: /* fallthru */
				case 'q':
					done = true;
					break;
			}

			if (start_line < 0) {
				start_line = 0;
			} else if ((size_t) (start_line + area.h) > n_lines) {
				start_line = n_lines - area.h;
			}
		}
	} else {
		show_prompt("(Press any key to continue.)", false);
		display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
				line_lengths, n_lines, 0, area);
		Term_flush_output();
		inkey_any();
	}

	clear_prompt();

	mem_free(line_starts);
	mem_free(line_lengths);

	Term_pop();
}

static void text_out_newline(struct text_out_info info, struct loc *cursor)
{
	cursor->y++;
	cursor->x = info.indent;
	Term_erase_line(cursor->x, cursor->y);
	cursor->x += info.pad;
	Term_cursor_to_xy(cursor->x, cursor->y);
}

static void text_out_backtrack(struct text_out_info info, int wrap, struct loc *cursor)
{
	assert(wrap > 0);
	struct term_point points[wrap];

	int split = 0;
	for (int w = wrap - 1; w > info.indent + info.pad; w--) {
		Term_get_point(w, cursor->y, &points[w]);

		if (points[w].fg_char == L' ') {
			split = w + 1;
			break;
		}
	}

	/* split == 0 when there are no spaces in this line
	 * split == wrap when the last char in this line is space */
	if (split > 0 && split < wrap) {
		Term_erase_line(split, cursor->y);
		text_out_newline(info, cursor);

		while (split < wrap) {
			Term_putwc(points[split].fg_attr, points[split].fg_char);
			split++;
			cursor->x++;
		}
	} else {
		text_out_newline(info, cursor);
	}
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
static void text_out_to_screen(struct text_out_info info, uint32_t attr, const char *str)
{
	const int width = Term_width();
	const int wrap = info.wrap > 0 && info.wrap < width ?
		info.wrap - 1 : width - 1;

	assert(info.indent + info.pad < wrap);

	struct loc cursor;
	Term_get_cursor(&cursor.x, &cursor.y, NULL, NULL);

	wchar_t buf[1024];
	text_mbstowcs(buf, str, sizeof(buf));
	
	for (const wchar_t *ws = buf; *ws; ws++) {
		if (*ws == L'\n') {
			text_out_newline(info, &cursor);
			continue;
		}

		wchar_t ch = iswprint(*ws) ? *ws : L' ';

		/* Wrap words as needed */
		if (cursor.x >= wrap) {
			if (ch == L' ') {
				text_out_newline(info, &cursor);
				continue;
			} else {
				text_out_backtrack(info, wrap, &cursor);
			}
		}

		Term_putwc(attr, ch);
		cursor.x++;
	}
}

/**
 * Output text to the screen
 */
void text_out(struct text_out_info info, const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	text_out_to_screen(info, COLOUR_WHITE, buf);
}

/**
 * Output text to the screen (in color)
 */
void text_out_c(struct text_out_info info, uint32_t attr, const char *fmt, ...)
{
	char buf[1024];
	va_list vp;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	text_out_to_screen(info, attr, buf);
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
void text_out_e(struct text_out_info info, const char *fmt, ...)
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
		text_out_to_screen(info, a, smallbuf);

		start = next;
	}
}

/**
 * ------------------------------------------------------------------------
 * Simple text display
 * ------------------------------------------------------------------------
 */

void clear_prompt(void)
{
	Term_push(angband_message_line.term);
	Term_clear();
	Term_cursor_visible(false);
	Term_flush_output();
	Term_pop();

	/* Reset the term state, so that messages
	 * won't print "-more-" over prompt */
	message_skip_more();
}

/**
 * Display a simple prompt on the screen
 */
void show_prompt(const char *str, bool cursor)
{
	event_signal(EVENT_MESSAGE_FLUSH);

	Term_push(angband_message_line.term);
	Term_clear();
	Term_cursor_visible(cursor);
	Term_adds(0, 0, Term_width(), COLOUR_WHITE, str);
	Term_flush_output();
	Term_pop();

	/* Reset the term state, so that messages
	 * won't print "-more-" over prompt */
	message_skip_more();
}

/**
 * Display a string on the screen using an attribute.
 *
 * At the given location, using the given attribute, if allowed,
 * add the given string.  Do not clear the line.
 */

void c_put_str_len(uint32_t attr, const char *str, struct loc at, int len) {
	assert(len >= 0);

	Term_adds(at.x, at.y, len, attr, str);
}

/**
 * As above, but in white
 */
void put_str_len(const char *str, struct loc at, int len) {
	assert(len >= 0);

	c_put_str_len(COLOUR_WHITE, str, at, len);
}

/**
 * Display a string on the screen using an attribute, and clear to the
 * end of the line.
 */
void c_prt_len(uint32_t attr, const char *str, struct loc at, int len) {
	assert(len >= 0);

	Term_erase_line(at.x, at.y);
	Term_adds(at.x, at.y, len, attr, str);
}

/**
 * As above, but in white
 */
void prt_len(const char *str, struct loc at, int len) {
	c_prt_len(COLOUR_WHITE, str, at, len);
}

/*
 * Simplified interfaces to the above
 */

void c_put_str(uint32_t attr, const char *str, struct loc at) {
	c_put_str_len(attr, str, at, Term_width());
}

void put_str(const char *str, struct loc at) {
	c_put_str_len(COLOUR_WHITE, str, at, Term_width());
}

void c_prt(uint32_t attr, const char *str, struct loc at) {
	c_prt_len(attr, str, at, Term_width());
}

void prt(const char *str, struct loc at) {
	c_prt_len(COLOUR_WHITE, str, at, Term_width());
}

/*
 * Wipe line at some point
 */
void erase_line(struct loc at)
{
	Term_erase_line(at.x, at.y);
}

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------
 */

/**
 * A Hengband-like 'window' function, that draws a surround box in ASCII art.
 */
void window_make(struct loc start, struct loc end)
{
	region to_clear = {
		.x = start.x,
		.y = start.y,
		.w = end.x - start.x,
		.h = end.y - start.y
	};

	region_erase(to_clear);

	Term_addwc(start.x, start.y, COLOUR_WHITE, L'+');
	Term_addwc(end.x, start.y, COLOUR_WHITE, L'+');
	Term_addwc(start.x, end.y, COLOUR_WHITE, L'+');
	Term_addwc(end.x, end.y, COLOUR_WHITE, L'+');

	for (int x = start.x + 1; x < end.x; x++) {
		Term_addwc(x, start.y, COLOUR_WHITE, L'-');
		Term_addwc(x, end.y, COLOUR_WHITE, L'-');
	}

	for (int y = start.y + 1; y < end.y; y++) {
		Term_addwc(start.x, y, COLOUR_WHITE, L'|');
		Term_addwc(end.x, y, COLOUR_WHITE, L'|');
	}
}

static void panel_fix_coords(const struct angband_term *aterm, struct loc *coords)
{
	Term_push(aterm->term);

	int width;
	int height;
	Term_get_size(&width, &height);
	Term_pop();

	const int curx = coords->x;
	const int cury = coords->y;
	const int maxx = cave->width - width;
	const int maxy = cave->height - height;

	coords->x = MAX(0, MIN(curx, maxx));
	coords->y = MAX(0, MIN(cury, maxy));
}

bool panel_should_modify(const struct angband_term *aterm, struct loc coords)
{
	panel_fix_coords(aterm, &coords);

	return aterm->offset_x != coords.x || aterm->offset_y != coords.y;
}

/**
 * Modify the current panel to the given coordinates, adjusting only to
 * ensure the coordinates are legal, and return true if anything done.
 *
 * The town should never be scrolled around.
 */
bool modify_panel(struct angband_term *aterm, struct loc coords)
{
	panel_fix_coords(aterm, &coords);

	if (aterm->offset_x != coords.x || aterm->offset_y != coords.y) {
		aterm->offset_x = coords.x;
		aterm->offset_y = coords.y;

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

	modify_panel(aterm, loc(offset_x, offset_y));
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

	return modify_panel(aterm, loc(offset_x, offset_y));
}

/**
 * Verify the current panel (relative to the player location).
 *
 * By default, when the player gets "too close" to the edge of the current
 * panel, the map scrolls one panel in that direction so that the player
 * is no longer so close to the edge.
 *
 * The "OPT(center_player)" option allows the current panel to always be
 * centered around the player
 */
void verify_panel(struct angband_term *aterm)
{
	verify_panel_int(aterm, OPT(center_player));
}

void center_panel(struct angband_term *aterm)
{
	verify_panel_int(aterm, true);
}

/**
 * Perform the minimum whole panel adjustment to ensure that the given
 * location is contained inside the current panel, and return true if any
 * such adjustment was performed.
 */
bool adjust_panel(struct angband_term *aterm, struct loc coords)
{
	Term_push(aterm->term);
	int term_width;
	int term_height;
	Term_get_size(&term_width, &term_height);
	Term_pop();

	int ox = aterm->offset_x;
	int oy = aterm->offset_y;

	while (ox + term_width <= coords.x) {
		ox += term_width / 2;
	}
	while (ox > coords.x) {
		ox -= term_width / 2;
	}
	while (oy + term_height <= coords.y) {
		oy += term_height / 2;
	}
	while (oy > coords.y) {
		oy -= term_height / 2;
	}

	return modify_panel(aterm, loc(ox, oy));
}

void get_cave_region(region *reg)
{
	reg->x = angband_cave.offset_x;
	reg->y = angband_cave.offset_y;

	Term_push(angband_cave.term);
	Term_get_size(&reg->w, &reg->h);
	Term_pop();
}

void textui_get_panel(int *min_y, int *min_x, int *max_y, int *max_x)
{
	region reg;
	get_cave_region(&reg);

	*min_x = reg.x;
	*min_y = reg.y;
	*max_x = reg.x + reg.w;
	*max_y = reg.y + reg.h;
}

bool textui_panel_contains(unsigned int y, unsigned int x)
{
	region reg;
	get_cave_region(&reg);

	struct loc loc = {x, y};

	return loc_in_region(loc, reg);
}

bool textui_map_is_visible(void)
{
	return true;
}

/**
 * Clear the bottom part of the screen
 */
void clear_from(int row)
{
	int height = Term_height();

	for (int y = row; y < height; y++) {
		Term_erase_line(0, y);
	}
}
