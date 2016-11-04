/**
 * \file main2-ncurses.c
 * \brief Support for ncursesw systems
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

#ifdef USE_NCURSES

#include "main.h"
#include <ncurses.h>
#include "ui2-display.h"
#include "ui2-output.h"

#define MIN_COLORS      8
#define MIN_COLOR_PAIRS 8

#define HALFDELAY_PERIOD 2

/* The main datastructure that holds ncurses window,
 * which corresponds to a certain textui2 term */
struct term_data {
	/* index as defined in ui2-display.c, or some
	 * arbitrary index if it's a temporary term */
	unsigned index;

	/* this term_data is ready for use */
	bool loaded;

	/* this term_data's term is temporary, and lives
	 * on the stack of terms (see ui2-term.c) */
	bool temporary;

	/* ncurses window */
	WINDOW *window;

	/* array of wchar_t for use in term_draw() callback */
	struct {
		int len;
		wchar_t *buf;
	} fg;
};

/* The data from list-display-terms.h */
struct term_info {
	enum display_term_index index;
	const char *name;
	int min_cols;
	int min_rows;
	int def_cols;
	int def_rows;
	int max_cols;
	int max_rows;
	bool required;
};

/* Ncurses color pairs (foreground); note
 * that background is always black (so far) */
enum color_pairs {
	PAIR_WHITE,
	PAIR_RED,
	PAIR_GREEN,
	PAIR_YELLOW,
	PAIR_BLUE,
	PAIR_MAGENTA,
	PAIR_CYAN,
	PAIR_BLACK,
};

/* Global array of ncurses attributes (colors) */
static int g_attrs[BASIC_COLORS];

/* Brief module description */
const char help_ncurses[] = "Ncurses (widestring) frontend";

/* Term callbacks */
static void term_flush_events(void *user);
static void term_make_visible(void *user, bool visible);
static void term_cursor(void *user, int col, int row);
static void term_redraw(void *user, int delay);
static void term_event(void *user, bool wait);
static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points);
static void term_delay(void *user, int msecs);
static void term_erase(void *user);
static void term_create(const struct term_hints *hints,
		struct term_create_info *info);
static void term_destroy(void *user);
static void term_add_tab(void *user,
		keycode_t code, const wchar_t *label, uint32_t fg_attr, uint32_t bg_attr);
static bool term_move(void *user,
		int dst_x, int dst_y, int src_x, int src_y, int cols, int rows);

/* Term initialization info */

static const struct term_callbacks default_callbacks = {
	.flush_events = term_flush_events,
	.make_visible = term_make_visible,
	.cursor       = term_cursor,
	.redraw       = term_redraw,
	.event        = term_event,
	.draw         = term_draw,
	.move         = term_move,
	.delay        = term_delay,
	.erase        = term_erase,
	.create       = term_create,
	.destroy      = term_destroy,
	.add_tab      = term_add_tab
};

#define BLANK_CHAR    L' '
#define BLANK_ATTR    0
#define BLANK_TERRAIN BG_BLACK

static const struct term_point default_blank_point = {
	.fg_char = BLANK_CHAR,
	.fg_attr = BLANK_ATTR,
	.bg_char = BLANK_CHAR,
	.bg_attr = BLANK_ATTR,
	.terrain_attr = BLANK_TERRAIN,
	.has_flags = false
};

/* Forward declarations */

static void init_globals(void);
static void make_fg_buf(struct term_data *data);
static void redraw_terms(bool update);
static void free_data(struct term_data *data);
static struct term_data *new_stack_data(void);
static void free_stack_data(struct term_data *data);
static struct term_data *get_stack_top(void);
static struct term_data *get_perm_data(enum display_term_index i);
static const struct term_info *get_term_info(enum display_term_index i);

/* Functions */

static void term_redraw(void *user, int delay)
{
	redraw_terms(false);

	if (delay > 0) {
		napms(delay);
	}
}

static void region_relative_cave(enum term_position pos, region *reg)
{
	const struct term_data *cave = get_perm_data(DISPLAY_CAVE);
	assert(cave->loaded);

	region cave_reg;
	getbegyx(cave->window, cave_reg.y, cave_reg.x);
	getmaxyx(cave->window, cave_reg.h, cave_reg.w);

	switch (pos) {
		case TERM_POSITION_TOP_LEFT:
			/* Put it in the top left corner of the map */
			reg->x = cave_reg.x;
			reg->y = cave_reg.y;
			break;

		default:
			/* All other positions just center the term in map */
			reg->x = cave_reg.x + (cave_reg.w - reg->w) / 2;
			reg->y = cave_reg.y + (cave_reg.h - reg->h) / 2;
			break;
	}

	if (reg->x < 0 || reg->y < 0) {
		/* Try to center in the screen then */
		reg->x = (COLS - reg->w) / 2;
		reg->y = (LINES - reg->h) / 2;
	}
}

static void region_relative_top(region *reg, int x, int y)
{
	const struct term_data *top = get_stack_top();
	if (top == NULL) {
		top = get_perm_data(DISPLAY_CAVE);
	}

	assert(top->loaded);

	int top_x;
	int top_y;
	getbegyx(top->window, top_y, top_x);

	reg->x = top_x + x;
	reg->y = top_y + y;
}

static region get_temp_region(const struct term_hints *hints)
{
	region reg = {
		.w = hints->width,
		.h = hints->height,
	};

	switch (hints->position) {
		case TERM_POSITION_CENTER:
		case TERM_POSITION_TOP_LEFT:
			region_relative_cave(hints->position, &reg);
			break;

		case TERM_POSITION_EXACT:
			region_relative_top(&reg, hints->x, hints->y);
			break;

		default:
			break;
	}

	return reg;
}

static void term_create(const struct term_hints *hints,
		struct term_create_info *info)
{
	struct term_data *data = new_stack_data();

	assert(data->temporary);
	assert(data->window == NULL);
	assert(!data->loaded);

	region reg = get_temp_region(hints); 
	
	data->window = newwin(reg.h, reg.w, reg.y, reg.x);
	assert(data->window != NULL);

	make_fg_buf(data);

	werase(data->window);

	data->loaded = true;

	info->user = data;
	info->blank = default_blank_point;
	info->width = reg.w,
	info->height = reg.h,
	info->callbacks = default_callbacks;
}

static void term_destroy(void *user)
{
	struct term_data *data = user;

	if (data->temporary) {
		free_stack_data(data);
		redraw_terms(true);
	} else {
		free_data(data);
	}
}

static int draw_points(struct term_data *data,
		const struct term_point *points, int n_points)
{
	uint32_t fg_attr = points[0].fg_attr;

	int draw = 0;
	while (draw < n_points && fg_attr == points[draw].fg_attr) {
		data->fg.buf[draw] = points[draw].fg_char;
		draw++;
	}

	assert(draw < data->fg.len);
	data->fg.buf[draw] = 0;

	wattrset(data->window, g_attrs[fg_attr]);
	waddnwstr(data->window, data->fg.buf, draw);

	return draw;
}

static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points)
{
	struct term_data *data = user;

	wmove(data->window, row, col);

	int drawn = 0;
	while (drawn < n_points) {
		drawn += draw_points(data, points + drawn, n_points - drawn);
	}
}

static int get_ch(bool wait)
{
	int ch;

	if (wait) {
		halfdelay(HALFDELAY_PERIOD);
		for (ch = getch(); ch == ERR; ch = getch()) {
			idle_update();
		}
		cbreak();
	} else {
		nodelay(stdscr, true);
		ch = getch();
		nodelay(stdscr, false);
	}

	return ch;
}

static keycode_t ch_to_code(int ch)
{
	keycode_t key;

	switch (ch) {
		case KEY_UP:        key = ARROW_UP;     break;
		case KEY_DOWN:      key = ARROW_DOWN;   break;
		case KEY_LEFT:      key = ARROW_LEFT;   break;
		case KEY_RIGHT:     key = ARROW_RIGHT;  break;
		case KEY_DC:        key = KC_DELETE;    break;
		case KEY_BACKSPACE: key = KC_BACKSPACE; break;
		case KEY_ENTER:     key = KC_ENTER;     break;
		case '\r':          key = KC_ENTER;     break;
		case '\t':          key = KC_TAB;       break;
		case 0x1B:          key = ESCAPE;       break;

		/* Keypad keys */
		case 0xFC:          key = '0';          break;
		case 0xFD:          key = '.';          break;
		case 0xDF:          key = '1';          break;
		case 0xF5:          key = '3';          break;
		case 0xE9:          key = '5';          break;
		case 0xC1:          key = '7';          break;
		case 0xF4:          key = '9';          break;

		/* F1 - F12 keys */
		case KEY_F(1):      key = KC_F1;        break;
		case KEY_F(2):      key = KC_F2;        break;
		case KEY_F(3):      key = KC_F3;        break;
		case KEY_F(4):      key = KC_F4;        break;
		case KEY_F(5):      key = KC_F5;        break;
		case KEY_F(6):      key = KC_F6;        break;
		case KEY_F(7):      key = KC_F7;        break;
		case KEY_F(8):      key = KC_F8;        break;
		case KEY_F(9):      key = KC_F9;        break;
		case KEY_F(10):     key = KC_F10;       break;
		case KEY_F(11):     key = KC_F11;       break;
		case KEY_F(12):     key = KC_F12;       break;

		default:            key = ch;           break;
	}

	return key;
}

static void term_event(void *user, bool wait)
{
	(void) user;

	int ch = get_ch(wait);

	if (ch != ERR && ch != EOF) {
		Term_keypress(ch_to_code(ch), 0);
	}
}

static void term_flush_events(void *user)
{
	(void) user;

	nodelay(stdscr, true);
	for (int ch = getch(); ch != ERR && ch != EOF; ch = getch()) {
		;
	}
	nodelay(stdscr, false);
}

static void term_make_visible(void *user, bool visible)
{
	/* We don't make windows visible or invisible */
	
	(void) user;
	(void) visible;
}

static void term_cursor(void *user, int col, int row)
{
	struct term_data *data = user;

	wmove(data->window, row, col);
}

static void term_delay(void *user, int msecs)
{
	(void) user;

	napms(msecs);
}

static void term_erase(void *user)
{
	struct term_data *data = user;

	wclear(data->window);
}

static void term_add_tab(void *user,
		keycode_t code, const wchar_t *label, uint32_t fg_attr, uint32_t bg_attr)
{
	/* Don't draw any tabs (for now) */

	(void) user;
	(void) code;
	(void) label;
	(void) fg_attr;
	(void) bg_attr;
}

static bool term_move(void *user,
		int dst_x, int dst_y, int src_x, int src_y, int cols, int rows)
{
	/* We don't do any optimizations (besides what ncurses provides) */

	(void) user;
	(void) dst_x;
	(void) dst_y;
	(void) src_x;
	(void) src_y;
	(void) cols;
	(void) rows;

	return false;
}

static region get_perm_region(enum display_term_index index)
{
	region reg = {0};

	switch (index) {
		case DISPLAY_CAVE:
			reg.w = COLS;
			reg.h = LINES - 1;
			reg.x = 0;
			reg.y = 1;
			break;

		case DISPLAY_MESSAGE_LINE:
			reg.w = COLS;
			reg.h = 1;
			reg.x = 0;
			reg.y = 0;
			break;
		
		default:
			break;
	}

	const struct term_info *info = get_term_info(index);
	if (reg.w < info->min_cols || reg.h < info->min_rows) {
		quit_fmt("Screen size for term '%s'" /* concat */
				" is too small (need %dx%d, got %dx%d)",
				info->name, info->min_cols, info->min_rows, reg.w, reg.h);
	}

	return reg;
}

static void make_fg_buf(struct term_data *data)
{
	assert(data->window != NULL);

	assert(data->fg.len == 0);
	assert(data->fg.buf == NULL);

	int w;
	int h;
	getmaxyx(data->window, h, w);

	(void) h; /* height is not needed */

	data->fg.len = w + 1; /* add 1 for the null terminator */
	data->fg.buf = mem_alloc(data->fg.len * sizeof(data->fg.buf[0]));
}

static void load_term(enum display_term_index index)
{
	struct term_data *data = get_perm_data(index);

	assert(data->window == NULL);
	assert(!data->loaded);

	region reg = get_perm_region(index);
	assert(reg.w > 0 && reg.h > 0);

	data->window = newwin(reg.h, reg.w, reg.y, reg.x);
	assert(data->window != NULL);

	make_fg_buf(data);

	werase(data->window);

	struct term_create_info info = {
		.user      = data,
		.width     = reg.w,
		.height    = reg.h,
		.callbacks = default_callbacks,
		.blank     = default_blank_point,
	};

	display_term_create(index, &info);
	data->loaded = true;
}

static void load_terms(void)
{
	load_term(DISPLAY_CAVE);
	load_term(DISPLAY_MESSAGE_LINE);
}

static void free_data(struct term_data *data)
{
	assert(data->loaded);

	assert(data->fg.buf != NULL);
	assert(data->fg.len != 0);

	mem_free(data->fg.buf);
	data->fg.buf = NULL;
	data->fg.len = 0;

	assert(data->window != NULL);

	delwin(data->window);
	data->window = NULL;

	data->loaded = false;
}

static void free_terms(void)
{
	for (unsigned i = 0; i < DISPLAY_MAX; i++) {
		struct term_data *data = get_perm_data(i);

		if (data->loaded) {
			display_term_destroy(data->index);
		}
	}
}

static void init_ncurses_colors(void)
{
	if (start_color() == ERR) {
		quit_fmt("Can't initialize color");
	}

	if (!has_colors()) {
		quit_fmt("Can't start without color");
	}

	assert(COLORS      >= MIN_COLORS);
	assert(COLOR_PAIRS >= MIN_COLOR_PAIRS);

	/* Initialize ncurses color pairs */
	init_pair(PAIR_RED,     COLOR_RED,     COLOR_BLACK);
	init_pair(PAIR_GREEN,   COLOR_GREEN,   COLOR_BLACK);
	init_pair(PAIR_YELLOW,  COLOR_YELLOW,  COLOR_BLACK);
	init_pair(PAIR_BLUE,    COLOR_BLUE,    COLOR_BLACK);
	init_pair(PAIR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(PAIR_CYAN,    COLOR_CYAN,    COLOR_BLACK);
	init_pair(PAIR_BLACK,   COLOR_BLACK,   COLOR_BLACK);

	/* Initialize array of ncurses attributes */
	g_attrs[COLOUR_DARK]        = COLOR_PAIR(PAIR_BLACK);
	g_attrs[COLOUR_WHITE]       = COLOR_PAIR(PAIR_WHITE)   | A_BOLD;
	g_attrs[COLOUR_SLATE]       = COLOR_PAIR(PAIR_WHITE);
	g_attrs[COLOUR_ORANGE]      = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[COLOUR_RED]         = COLOR_PAIR(PAIR_RED);
	g_attrs[COLOUR_GREEN]       = COLOR_PAIR(PAIR_GREEN);
	g_attrs[COLOUR_BLUE]        = COLOR_PAIR(PAIR_BLUE);
	g_attrs[COLOUR_UMBER]       = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[COLOUR_L_DARK]      = COLOR_PAIR(PAIR_BLACK)   | A_BOLD;
	g_attrs[COLOUR_L_WHITE]     = COLOR_PAIR(PAIR_WHITE);
	g_attrs[COLOUR_L_PURPLE]    = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[COLOUR_YELLOW]      = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[COLOUR_L_RED]       = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[COLOUR_L_GREEN]     = COLOR_PAIR(PAIR_GREEN)   | A_BOLD;
	g_attrs[COLOUR_L_BLUE]      = COLOR_PAIR(PAIR_BLUE)    | A_BOLD;
	g_attrs[COLOUR_L_UMBER]     = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[COLOUR_PURPLE]      = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[COLOUR_VIOLET]      = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[COLOUR_TEAL]        = COLOR_PAIR(PAIR_CYAN);
	g_attrs[COLOUR_MUD]         = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[COLOUR_L_YELLOW]    = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[COLOUR_MAGENTA]     = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[COLOUR_L_TEAL]      = COLOR_PAIR(PAIR_CYAN)    | A_BOLD;
	g_attrs[COLOUR_L_VIOLET]    = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[COLOUR_L_PINK]      = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[COLOUR_MUSTARD]     = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[COLOUR_BLUE_SLATE]  = COLOR_PAIR(PAIR_BLUE);
	g_attrs[COLOUR_DEEP_L_BLUE] = COLOR_PAIR(PAIR_BLUE);
}

static void quit_hook(const char *s)
{
	(void) s;

	Term_pop_all();
	free_terms();
	endwin();
}

int init_ncurses(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (initscr() == NULL) {
		return 1;
	}

	init_ncurses_colors();

	cbreak();
	noecho();
	nonl();

	nodelay(stdscr, false);
	keypad(stdscr, true);

	init_globals();
	load_terms();

	quit_aux = quit_hook;

	return 0;
}

/* Global datastructures and functions that operate on them */

/* Permanent terms (managed by ui2-display.c) */
static struct term_data g_perm_data[DISPLAY_MAX];

/* Temporary terms (managed by ui2-term.c) */
static struct {
	struct term_data stack[TERM_STACK_MAX];
	size_t top;
} g_temp_data;

/* Information about terms (description, size, etc) */
static struct term_info g_term_info[] = {
	#define DISPLAY(i, desc, minc, minr, defc, defr, maxc, maxr, req) \
		{ \
			.index    = DISPLAY_ ##i, \
			.name     = (desc), \
			.min_cols = (minc), \
			.min_rows = (minr), \
			.def_cols = (defc), \
			.def_rows = (defr), \
			.max_cols = (maxc), \
			.max_rows = (maxr), \
			.required = (req), \
		},
	#include "list-display-terms.h"
	#undef DISPLAY
};

static void init_globals(void)
{
	assert(N_ELEMENTS(g_perm_data) == N_ELEMENTS(g_term_info));

	for (size_t i = 0; i < N_ELEMENTS(g_perm_data); i++) {
		g_perm_data[i].index = g_term_info[i].index;
	}

	for (size_t i = 0; i < N_ELEMENTS(g_temp_data.stack); i++) {
		g_temp_data.stack[i].index = DISPLAY_MAX + i;
		g_temp_data.stack[i].temporary = true;
	}
}

static const struct term_info *get_term_info(enum display_term_index i)
{
	assert(i >= 0);
	assert(i < N_ELEMENTS(g_term_info));
	assert(g_term_info[i].index == i);

	return &g_term_info[i];
}

static struct term_data *new_stack_data(void)
{
	size_t t = g_temp_data.top;
	assert(t < N_ELEMENTS(g_temp_data.stack));

	g_temp_data.top++;

	return &g_temp_data.stack[t];
}

static void free_stack_data(struct term_data *data)
{
	assert(data->loaded);
	assert(data->temporary);

	assert(g_temp_data.top > 0);
	assert(&g_temp_data.stack[g_temp_data.top - 1] == data);

	free_data(data);

	g_temp_data.top--;
}

static struct term_data *get_stack_top(void)
{
	struct term_data *data;

	if (g_temp_data.top > 0) {
		data = &g_temp_data.stack[g_temp_data.top - 1];
	} else {
		data = NULL;
	}

	return data;
}

static struct term_data *get_perm_data(enum display_term_index i)
{
	assert(i >= 0);
	assert(i < N_ELEMENTS(g_perm_data));
	assert(g_perm_data[i].index == i);

	return &g_perm_data[i];
}

static void redraw_terms(bool update)
{
	for (size_t i = 0; i < N_ELEMENTS(g_perm_data); i++) {
		struct term_data *data = &g_perm_data[i];

		if (data->loaded) {
			if (update) {
				touchwin(data->window);
			}
			wnoutrefresh(data->window);
		}
	}

	for (size_t t = 0; t < g_temp_data.top; t++) {
		struct term_data *data = &g_temp_data.stack[t];
		assert(data->loaded);

		if (update) {
			touchwin(data->window);
		}
		wnoutrefresh(data->window);
	}

	doupdate();
}

#endif /* USE_NCURSES */
