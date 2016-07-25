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
 * ------------------------------------------------------------------------ */

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
		Term_erase(calc.x, calc.x + y, calc.w);
	}
}

void region_erase(region reg)
{
	region calc = region_calculate(reg);

	for (int y = 0; y < calc.h; y++) {
		Term_erase(calc.x, calc.y + y, calc.w);
	}
}

bool region_inside(const region *reg, const struct mouseclick *mouse)
{
	return reg->x <= mouse->x
		&& reg->x + reg->w > mouse->x
		&& reg->y <= mouse->y
		&& reg->y + reg->h > mouse->y;
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
		size_t n_lines, size_t cur_line, region area)
{
	n_lines = MIN(n_lines, (size_t) area.h);

	for (size_t y = 0; y < n_lines; y++, cur_line++) {
		Term_erase(area.x, area.y + y, area.w);

		for (size_t x = 0; x < line_lengths[cur_line]; x++) {
			size_t position = line_starts[cur_line] + x;

			Term_addwc(area.x + x, area.y + y,
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
	region area = region_calculate(orig_area);

	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	size_t n_lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, area.w);

	struct term_hints hints = {
		.x = area.x,
		.y = area.y,
		.width = area.w,
		.height = area.h,
		.position = TERM_POSITION_EXACT,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	
	/* Make room for the footer */
	area.h -= 2;

	if (header != NULL) {
		area.h--;
		c_prt(COLOUR_L_BLUE, header, loc(area.x, area.y));
		area.y++;
	}

	if (n_lines > (size_t) area.h) {
		int start_line = 0;

		c_prt(COLOUR_WHITE, "", loc(area.x, area.h));
		c_prt(COLOUR_L_BLUE, "(Up/down or ESCAPE to exit.)",
				loc(area.x, area.h + 1));

		/* Pager mode */
		while (true) {
			display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
					line_lengths, n_lines, start_line, area);

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
					break;
			}

			if (start_line < 0) {
				start_line = 0;
			} else if ((size_t) (start_line + area.h) > n_lines) {
				start_line = n_lines - area.h;
			}
		}
	} else {
		display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
				line_lengths, n_lines, 0, area);

		c_prt(COLOUR_WHITE, "", loc(area.x, n_lines));
		c_prt(COLOUR_L_BLUE, "(Press any key to continue.)",
				loc(area.x, n_lines + 1));
		inkey_any();
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

static void text_out_newline(struct loc *cursor)
{
	cursor->y++;
	cursor->x = text_out_indent;
	Term_erase_line(cursor->x, cursor->y);
	cursor->x += text_out_pad;
	Term_cursor_to_xy(cursor->x, cursor->y);
}

static void text_out_backtrack(int wrap, struct loc *cursor)
{
	assert(wrap > 0);
	struct term_point points[wrap];

	int split = 0;
	for (int w = wrap - 1; w > text_out_indent + text_out_pad; w--) {
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
		text_out_newline(cursor);

		while (split < wrap) {
			Term_putwc(points[split].fg_attr, points[split].fg_char);
			split++;
			cursor->x++;
		}
	} else {
		text_out_newline(cursor);
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
static void text_out_to_screen(uint32_t attr, const char *str)
{
	const int width = Term_width();
	const int wrap = text_out_wrap > 0 && text_out_wrap < width ?
		text_out_wrap - 1 : width - 1;

	assert(text_out_indent + text_out_pad < wrap);

	struct loc cursor;
	Term_get_cursor(&cursor.x, &cursor.y, NULL, NULL);

	wchar_t buf[1024];
	text_mbstowcs(buf, str, sizeof(buf));
	
	for (const wchar_t *ws = buf; *ws; ws++) {
		if (*ws == L'\n') {
			text_out_newline(&cursor);
			continue;
		}

		wchar_t ch = iswprint(*ws) ? *ws : L' ';

		/* Wrap words as needed */
		if (cursor.x >= wrap) {
			if (ch == L' ') {
				text_out_newline(&cursor);
				continue;
			} else {
				text_out_backtrack(wrap, &cursor);
			}
		}

		Term_putwc(attr, ch);
		cursor.x++;
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
 * Display a simple prompt on the screen
 */
void show_prompt(const char *str)
{
	event_signal(EVENT_MESSAGE_FLUSH);

	Term_push(angband_message_line.term);
	Term_adds(0, 0, Term_width(), COLOUR_WHITE, str);
	Term_flush_output();
	Term_pop();
}

void clear_prompt(void)
{
	Term_push(angband_message_line.term);
	Term_clear();
	Term_flush_output();
	Term_pop();
}

/**
 * Display a string on the screen using an attribute.
 *
 * At the given location, using the given attribute, if allowed,
 * add the given string.  Do not clear the line. Ignore strings
 * with invalid coordinates.
 */
void c_put_str(uint32_t attr, const char *str, struct loc at) {
	if (Term_point_ok(at.x, at.y)) {
		Term_adds(at.x, at.y, Term_width(), attr, str);
	}
}

/**
 * As above, but in white
 */
void put_str(const char *str, struct loc at) {
	c_put_str(COLOUR_WHITE, str, at);
}

/**
 * Display a string on the screen using an attribute, and clear to the
 * end of the line. Ignore strings with invalid coordinates.
 */
void c_prt(uint32_t attr, const char *str, struct loc at) {
	if (Term_point_ok(at.x, at.y)) {
		Term_erase_line(at.x, at.y);
		Term_adds(at.x, at.y, Term_width(), attr, str);
	}
}

/**
 * As above, but in white
 */
void prt(const char *str, struct loc at) {
	c_prt(COLOUR_WHITE, str, at);
}

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------ */

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

/* TODO use struct loc instead */
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

/* TODO use struct loc instead */
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
