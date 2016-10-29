/**
 * \file ui2-output.c
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
#include "ui2-map.h"
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
 * These functions are designed to display large blocks of text
 * on the screen all at once. They are the ui-term specific layer
 * on top of the z-textblock.c functions.
 */

/**
 * This function displays only as many lines of text as region allows.
 */
static void display_area(const wchar_t *text, const byte *attrs,
		size_t *line_starts, size_t *line_lengths,
		size_t n_lines, size_t cur_line, region area)
{
	assert(area.w > 0);
	assert(area.h > 0);

	for (size_t y = 0, lines = MIN(n_lines, (size_t) area.h);
			y < lines;
			y++, cur_line++)
	{
		Term_erase(area.x, area.y + y, area.w);

		for (size_t x = 0, length = MIN(line_lengths[cur_line], (size_t) area.w);
				x < length;
				x++)
		{
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

static void textblock_term_push(textblock *tb,
		enum term_position position, region orig_area, const char *header,
		size_t **line_starts, size_t **line_lengths, int *lines, region *area)
{
	int width = orig_area.w > 0 ?
		orig_area.w : ANGBAND_TERM_TEXTBLOCK_WIDTH;

	size_t n_lines =
		textblock_calculate_lines(tb, line_starts, line_lengths, width);

	/* Remove empty lines from the end of textblock */
	while (n_lines > 0 && (*line_lengths)[n_lines - 1] == 0) {
		n_lines--;
	}

	/* Add 2 lines for term's instructions ("press any key to continue") */
	int height = (orig_area.h > 0 ? orig_area.h : (int) n_lines) + 2;

	assert(orig_area.x >= 0);
	assert(orig_area.y >= 0);

	struct term_hints hints = {
		.x = orig_area.x,
		.y = orig_area.y,
		.width = width,
		.height = height,
		.tabs = header ? true : false,
		.position = position,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	if (header) {
		Term_add_tab(0, header, COLOUR_WHITE, COLOUR_SHADE);
	}

	*lines = n_lines;

	area->x = 0;
	area->y = 0;
	area->w = width;
	area->h = height - 2;

	if (*lines > area->h) { 
		Term_addws(0, hints.height - 1,
				TERM_MAX_LEN, COLOUR_WHITE, L"(up/down to scroll or ESC to exit)");
	} else {
		Term_addws(0, hints.height - 1,
				TERM_MAX_LEN, COLOUR_WHITE, L"(press any key to continue)");
	}
}

static void textblock_term_pop()
{
	Term_pop();
}

/**
 * Show a textblock interactively
 */
void textui_textblock_show(textblock *tb,
		enum term_position position, region orig_area, const char *header)
{
	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	int lines = 0;
	region area = {0};

	textblock_term_push(tb, position, orig_area, header,
			&line_starts, &line_lengths, &lines, &area);

	if (lines > area.h) {
		int start_line = 0;
		bool done = false;

		/* Pager mode */
		while (!done) {
			display_area(textblock_text(tb), textblock_attrs(tb), line_starts,
					line_lengths, lines, start_line, area);

			Term_flush_output();

			struct keypress key = inkey_only_key();

			switch (key.code) {
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
			} else if (start_line + area.h > lines) {
				start_line = lines - area.h;
			}
		}
	} else {
		display_area(textblock_text(tb), textblock_attrs(tb),
				line_starts, line_lengths, lines, 0, area);
		Term_flush_output();
		inkey_any();
	}

	mem_free(line_starts);
	mem_free(line_lengths);

	textblock_term_pop();
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

	int next = 0;
	for (int w = wrap - 1; w > info.indent + info.pad; w--) {
		Term_get_point(w, cursor->y, &points[w]);

		if (points[w].fg_char == L' ') {
			next = w + 1;
			break;
		}
	}

	/* next == 0 when there are no spaces in this line
	 * next == wrap when the last char in this line is space */
	if (next > 0 && next < wrap) {
		Term_erase_line(next, cursor->y);
		text_out_newline(info, cursor);

		while (next < wrap) {
			Term_putwc(points[next].fg_attr, points[next].fg_char);
			next++;
			cursor->x++;
		}
	} else {
		text_out_newline(info, cursor);
	}
}

/**
 * Print some (colored) text to the screen at the current cursor position,
 * automatically wrapping existing text (at spaces) when necessary and
 * clearing every line before placing any text in that line.
 * Also, allow "newline" to force a "wrap" to the next line.
 * Advance the cursor as needed so sequential calls to this function
 * will work correctly.
 *
 * Once this function has been called, the cursor should not be moved
 * until all the related "text_out()" calls to the window are complete.
 */
static void text_out_to_screen(struct text_out_info info, uint32_t attr, const char *str)
{
	const int width = Term_width();
	const int wrap = info.wrap > 0 && info.wrap < width ?
		info.wrap : width;

	assert(info.indent + info.pad < wrap);

	struct loc cursor;
	Term_get_cursor(&cursor.x, &cursor.y, NULL);

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
 * text and any tag that goes with it.
 *
 * It return true if it finds something to print.
 * 
 * If it returns true, then it also fills 'text' with a pointer to the start
 * of the next printable section of text, and 'textlen' with the length of
 * that text, and 'end' with a pointer to the start of the next section.
 * This may differ from "text + textlen" because of the presence of tags.
 * If a tag applies to the section of text, it returns a pointer to the
 * start of that tag in 'tag' and the length in 'taglen'.
 * Otherwise, 'tag' is filled with NULL.
 *
 * See text_out_e for an example of its use.
 */
static bool next_section(const char *source, size_t init,
		const char **text, size_t *textlen,
		const char **tag, size_t *taglen,
		const char **end)
{
	*tag = NULL;
	*text = source + init;
	if (*text[0] == '\0') {
		return false;
	}

	const char *next = strchr(*text, '{');

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
					*textlen = close - *text;
					*end = close + 3;
					return true;
				} else {
					/* Otherwise return the chunk up to this */
					*textlen = next - *text;
					*end = *text + *textlen;
					return true;
				}
			} else {
				/* No closing thing, therefore all one lump of text. */
				*textlen = strlen(*text);
				*end = *text + *textlen;
				return true;
			}
		} else if (*s == '\0') {
			/* End of the string, that's fine. */
			*textlen = strlen(*text);
			*end = *text + *textlen;
			return true;
		} else {
			/* An invalid tag, skip it. */
			next = next + 1;
		}

		next = strchr(next, '{');
	}

	/* Default to the rest of the string */
	*textlen = strlen(*text);
	*end = *text + *textlen;

	return true;
}

/**
 * Output text to the screen or to a file depending on the
 * selected hook. Takes strings with "embedded formatting",
 * such that something within {red}{/} will be printed in red.
 *
 * Note that such formatting will be treated as a "breakpoint"
 * for the printing, so if used within words may lead to part
 * of the word being moved to the next line.
 */
void text_out_e(struct text_out_info info, const char *fmt, ...)
{
	char buf[1024];
	char smallbuf[1024];
	va_list vp;

	const char *start;
	const char *next;
	const char *text;
	const char *tag;

	size_t textlen;
	size_t taglen = 0;

	va_start(vp, fmt);
	(void) vstrnfmt(buf, sizeof(buf), fmt, vp);
	va_end(vp);

	start = buf;
	while (next_section(start, 0, &text, &textlen, &tag, &taglen, &next)) {

		assert(textlen < sizeof(smallbuf));

		memcpy(smallbuf, text, textlen);
		smallbuf[textlen] = 0;

		uint32_t attr;

		if (tag) {
			char tagbuffer[16];

			/* Colour names are less than 16 characters long. */
			assert(taglen < 16);

			memcpy(tagbuffer, tag, taglen);
			tagbuffer[taglen] = '\0';

			attr = color_text_to_attr(tagbuffer);
		} else {
			attr = COLOUR_WHITE;
		}

		/* Output now */
		text_out_to_screen(info, attr, smallbuf);

		start = next;
	}
}

/**
 * ------------------------------------------------------------------------
 * Simple text display
 * ------------------------------------------------------------------------
 */

#define PUT_STR_H_TEXT_COLOR \
	COLOUR_WHITE
#define PUT_STR_H_HIGHLIGHT_COLOR \
	COLOUR_L_GREEN

/**
 * Print a colorized string on the screen.
 */
void put_str_h(const char *str, struct loc loc,
		uint32_t color, uint32_t highlight)
{
	/* We don't print backticks; imagine that
	 * every char in the string is surrounded by
	 * two backticks ("`a``b``c`"...).
	 * The function must work correctly even then,
	 * up to ANGBAND_TERM_STANDARD_WIDTH chars. */
	wchar_t ws[ANGBAND_TERM_STANDARD_WIDTH * 3];

	size_t text_length = text_mbstowcs(ws, str, N_ELEMENTS(ws));
	assert(text_length != (size_t) -1);

	ws[N_ELEMENTS(ws) - 1] = 0;

	int w;
	int h;
	Term_get_size(&w, &h);

	assert(loc.x >= 0);
	assert(loc.x < w);
	assert(loc.y >= 0);
	assert(loc.y < h);

	Term_cursor_to_xy(loc.x, loc.y);

	uint32_t attr = color;

	for (size_t i = 0; i < text_length && loc.x < w; i++) {
		if (ws[i] == PUT_STR_H_MARK_WCHAR) {
			attr = attr == color ? highlight : color;
		} else {
			Term_putwc(attr, ws[i]);
			loc.x++;
		}
	}

	Term_flush_output();
}

void put_str_h_center(const char *str, int y,
		uint32_t color, uint32_t highlight)
{
	int len = 0; /* length without mark chars */
	for (int i = 0; str[i] != 0; i++) {
		if (str[i] != PUT_STR_H_MARK_CHAR) {
			len++;
		}
	}

	struct loc loc = {
		.x = (Term_width() - len) / 2,
		.y = y
	};

	put_str_h(str, loc, color, highlight);
}

void put_str_h_simple(const char *str, struct loc loc)
{
	put_str_h(str, loc, PUT_STR_H_TEXT_COLOR, PUT_STR_H_HIGHLIGHT_COLOR);
}

void put_str_h_center_simple(const char *str, int y)
{
	put_str_h_center(str, y, PUT_STR_H_TEXT_COLOR, PUT_STR_H_HIGHLIGHT_COLOR);
}

void clear_prompt(void)
{
	display_term_push(DISPLAY_MESSAGE_LINE);
	Term_erase_all();
	Term_flush_output();
	display_term_pop();

	/* Reset the term state, so that messages
	 * won't print "-more-" over empty message line */
	message_skip_more();
}

/**
 * Display a colorized prompt on the screen.
 */
void show_prompt(const char *str)
{
	event_signal(EVENT_MESSAGE_FLUSH);
	display_term_push(DISPLAY_MESSAGE_LINE);
	Term_erase_all();

	struct loc loc = {0, 0};
	put_str_h(str, loc, PUT_STR_H_TEXT_COLOR, PUT_STR_H_HIGHLIGHT_COLOR);

	Term_flush_output();
	display_term_pop();

	/* Reset the term state, so that messages
	 * won't print "-more-" over prompt string */
	message_skip_more();
}

/**
 * Display a string on the screen using an attribute.
 *
 * At the given location, using the given attribute, if allowed,
 * add the given string. Do not clear the line.
 */

void c_put_str_len(uint32_t attr, const char *str, struct loc at, int len) {
	Term_adds(at.x, at.y, len, attr, str);
}

/**
 * As above, but in white
 */
void put_str_len(const char *str, struct loc at, int len) {
	c_put_str_len(COLOUR_WHITE, str, at, len);
}

/**
 * Display a string on the screen using an attribute, and clear to the
 * end of the line.
 */
void c_prt_len(uint32_t attr, const char *str, struct loc at, int len) {
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
	c_put_str_len(attr, str, at, TERM_MAX_LEN);
}

void put_str(const char *str, struct loc at) {
	c_put_str_len(COLOUR_WHITE, str, at, TERM_MAX_LEN);
}

void c_prt(uint32_t attr, const char *str, struct loc at) {
	c_prt_len(attr, str, at, TERM_MAX_LEN);
}

void prt(const char *str, struct loc at) {
	c_prt_len(COLOUR_WHITE, str, at, TERM_MAX_LEN);
}

/*
 * Wipe line at some point
 */
void erase_line(struct loc at)
{
	Term_erase_line(at.x, at.y);
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

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------
 */

/*
 * Get term's offsets, width and height as a region
 */
static void get_term_region(enum display_term_index index, region *reg)
{
	int width;
	int height;
	struct loc coords;

	display_term_get_area(index, &coords, &width, &height);

	reg->x = coords.x;
	reg->y = coords.y;
	reg->w = width;
	reg->h = height;
}

/*
 * Get a panel; a viewport into Angband's dungeon
 */
void get_cave_region(region *reg)
{
	get_term_region(DISPLAY_CAVE, reg);
}

/*
 * Ensure that coords are valid coordinates of Angband's dungeon
 */
static void panel_fix_coords(struct loc *coords, region panel)
{
	const int curx = coords->x;
	const int cury = coords->y;
	const int maxx = cave->width - panel.w;
	const int maxy = cave->height - panel.h;

	coords->x = MAX(0, MIN(curx, maxx));
	coords->y = MAX(0, MIN(cury, maxy));
}

bool panel_should_modify(enum display_term_index index, struct loc new_coords)
{
	region panel;
	get_term_region(index, &panel);

	panel_fix_coords(&new_coords, panel);

	return panel.x != new_coords.x || panel.y != new_coords.y;
}

/*
 * This is the function that actually modifies
 * x and y offsets of a term and updates the map.
 * Parameter "panel" must be valid;
 * x and y must be coords of a display_term,
 * w and h must be term's width and height.
 */
static bool modify_panel_int(enum display_term_index index,
		struct loc new_coords, region panel)
{
	panel_fix_coords(&new_coords, panel);

	if (panel.x != new_coords.x || panel.y != new_coords.y) {
		display_term_set_coords(index, new_coords);

		struct loc diff = {
			.x = panel.x - new_coords.x,
			.y = panel.y - new_coords.y
		};
		region new_panel = {
			.x = new_coords.x,
			.y = new_coords.y,
			.w = panel.w,
			.h = panel.h
		};
		map_move(index, diff, new_panel);

		return true;
	} else {
		return false;
	}
}

/**
 * Modify the current panel to the given coordinates, adjusting only to
 * ensure the coordinates are legal, and return true if anything done.
 *
 * The town should never be scrolled around.
 */
bool modify_panel(enum display_term_index index, struct loc new_coords)
{
	region panel;
	get_term_region(index, &panel);

	return modify_panel_int(index, new_coords, panel);
}

static void verify_panel_int(enum display_term_index index, bool centered)
{
	/* Term region ("panel") */
	region t;
	get_term_region(index, &t);

	/* scroll the panel if @ is closer than this to the edge */
	int scroll = MAX(SCROLL_MIN_DISTANCE, MIN(t.w / 4, t.h / 4));

	/* New coords (about to be modified) */
	struct loc n = {t.x, t.y};

	/* Player coords (just to save some keystrokes) */
	struct loc p = {player->px, player->py};

	if ((centered && p.x != t.x + t.w / 2)
			|| (p.x < t.x + scroll || p.x >= t.x + t.w - scroll))
	{
		n.x = p.x - t.w / 2;
	}

	if ((centered && p.y != t.y + t.h / 2)
			|| (p.y < t.y + scroll || p.y >= t.y + t.h - scroll))
	{
		n.y = p.y - t.h / 2;
	}

	modify_panel_int(index, n, t);
}

/**
 * Change the panels to the panel lying in the given direction.
 * Return true if any panel was changed.
 */
bool change_panel(enum display_term_index index, int dir)
{
	region panel;
	get_term_region(index, &panel);

	/* Shift by half a panel */
	struct loc new_coords = {
		.x = panel.x + ddx[dir] * panel.w / 2,
		.y = panel.y + ddy[dir] * panel.h / 2
	};

	return modify_panel_int(index, new_coords, panel);
}

/**
 * Verify the current panel (relative to the player location).
 *
 * By default, when the player gets too close to the edge of the current
 * panel, the map scrolls one panel in that direction so that the player
 * is no longer so close to the edge.
 *
 * The "OPT(player, center_player)" option allows the current panel
 * to always be centered around the player
 */
void verify_panel(enum display_term_index index)
{
	verify_panel_int(index, OPT(player, center_player));
}

void center_panel(enum display_term_index index)
{
	verify_panel_int(index, true);
}

/**
 * Perform the minimum whole panel adjustment to ensure that the given
 * location is contained inside the current panel, and return true if any
 * such adjustment was performed.
 */
bool adjust_panel(enum display_term_index index, struct loc coords)
{
	region panel;
	get_term_region(index, &panel);

	/* New panel's offsets */
	struct loc n = {panel.x, panel.y};

	while (n.x + panel.w <= coords.x) {
		n.x += panel.w / 2;
	}
	while (n.x > coords.x) {
		n.x -= panel.w / 2;
	}
	while (n.y + panel.h <= coords.y) {
		n.y += panel.h / 2;
	}
	while (n.y > coords.y) {
		n.y -= panel.h / 2;
	}

	return modify_panel_int(index, n, panel);
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
	struct loc loc = {x, y};

	region reg;
	get_cave_region(&reg);

	return loc_in_region(loc, reg);
}

bool textui_map_is_visible(void)
{
	return true;
}
