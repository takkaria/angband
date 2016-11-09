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

/**
 * You can define environment variables to make this frontend
 * display additional terms and control their sizes and positions.
 * The terms are listed in list-display-terms.h. You can get the names
 * of environment variables by appending "ANGBAND_TERM_" to term's names.
 * For example, the term that displays a list of visible/detected monsters:
 *
 * DISPLAY(MONLIST, "Monster list", 12, 3, 24, 12, INT_MAX, INT_MAX, false)
 *
 * MONLIST is the term's name; 12 and 3 are the minimum width and height.
 * So, you can define the following environment variable in your shell:
 *
 * export ANGBAND_TERM_MONLIST=0x0x12x3
 *
 * And this term will appear in the top left corner of your screen.
 * The general format of the variable is:
 *
 * ANGBAND_TERM_[NAME]=[LEFT]x[TOP]x[WIDTH]x[HEIGHT]
 *
 * Note that you can make terms bigger than the minimum size,
 * but don't make them smaller than that! They also
 * shouldn't be off screen (not even partially!)
 * It's a good idea to write a shell script that starts the game,
 * something like the following (assuming 80x24 screen):
 *
 * #!/usr/bin/env sh
 * export ANGBAND_TERM_CAVE=0x1x67x22
 * export ANGBAND_TERM_MESSAGE_LINE=0x0x80x1
 * export ANGBAND_TERM_STATUS_LINE=0x23x80x1
 * export ANGBAND_TERM_PLAYER_COMPACT=68x1x12x22
 * angband -mncurses
 *
 * Note that it's better to not have overlapping terms.
 */

#ifdef USE_NCURSES

#include "main.h"
#include <ncurses.h>
#include "ui2-display.h"
#include "ui2-output.h"

#define DONT_USE_TABS (-1)

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

	/* this term is newly pushed on the stack,
	 * and the whole stack should be redrawn */
	bool new;

	/* cursor in window */
	struct {
		int col;
		int row;
		bool visible;
	} cursor;

	/* ncurses window (includes borders) */
	WINDOW *window;

	/* area of window without borders */
	WINDOW *subwindow;

	/* offset of next tab to print; note that
	 * term_add_tab() prints them one by one*/
	int tab_offset;

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
	const char *env_name;
	int min_cols;
	int min_rows;
	int def_cols;
	int def_rows;
	int max_cols;
	int max_rows;
	bool required;
};

/* Ncurses color pairs (foreground) */
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

/* Global array of ncurses attributes (color pairs); we need
 * BASIC_COLORS * 3 colors to support hybrid and solid walls */
static int g_attrs[3][BASIC_COLORS];

#define G_ATTR_NORMAL 0
#define G_ATTR_HYBRID 1
#define G_ATTR_SOLID  2

/* Note that in text mode, background is term_points's
 * terrain_attr and not bg_attr (which is used for tiles) */
#define G_ATTR_INDEX(foreground, background) \
	((background) == (foreground) ? G_ATTR_SOLID : \
	 (background) == COLOUR_SHADE ? G_ATTR_HYBRID : G_ATTR_NORMAL)

/* We don't want to change definitions of 16 basic colors,
 * so we redefine colors only if there are enough of them */
#define MIN_EXTENDED_COLORS      (16 + BASIC_COLORS * 3)
#define MIN_EXTENDED_COLOR_PAIRS (16 + BASIC_COLORS * 3)

/* If there are not enough colors,
 * use the standard color pairs */
#define MIN_COLORS      8
#define MIN_COLOR_PAIRS 8

/* Brief module description */
const char help_ncurses[] = "Ncurses (widestring) frontend";

/* Term callbacks */
static void term_flush_events(void *user);
static void term_make_visible(void *user, bool visible);
static void term_cursor(void *user, bool visible, int col, int row);
static void term_redraw(void *user, int delay);
static void term_event(void *user, bool wait);
static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points);
static void term_delay(void *user, int msecs);
static void term_erase(void *user);
static void term_push_new(const struct term_hints *hints,
		struct term_create_info *info);
static void term_pop_new(void *user);
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
	.add_tab      = term_add_tab,
	.pop_new      = term_pop_new,
	.push_new     = term_push_new,
};

#define BLANK_CHAR    L' '
#define BLANK_ATTR    COLOUR_SLATE
#define BLANK_TERRAIN COLOUR_DARK

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
static void redraw_terms();
static void free_term_data(struct term_data *data);
static struct term_data *new_stack_data(void);
static void free_stack_data(struct term_data *data);
static struct term_data *get_top();
static struct term_data *get_stack_top(void);
static struct term_data *get_perm_data(enum display_term_index i);
static const struct term_info *get_term_info(enum display_term_index i);
static void calc_default_term_regions(void);
static void read_env_term_regions(void);
static const region *get_term_region(enum display_term_index i);
static void get_out(const char *fmt, ...);

/* Functions */

static void redraw_win(const struct term_data *data, int n_data)
{
	for (int n = 0; n < n_data; n++) {
		if (data[n].loaded) {
			wnoutrefresh(data[n].window);
			wnoutrefresh(data[n].subwindow);
		}
	}
}

static void touch_win(const struct term_data *data, int n_data)
{
	for (int n = 0; n < n_data; n++) {
		if (data[n].loaded) {
			touchwin(data[n].window);
		}
	}
}

static void term_redraw(void *user, int delay)
{
	(void) user;

	redraw_terms();

	if (delay > 0) {
		napms(delay);
	}
}

static bool region_in_region(const region *small, const region *big)
{
	return small->x >= big->x
		&& small->x + small->w <= big->x + big->w
		&& small->y >= big->y
		&& small->y + small->h <= big->y + big->h;
}

static void load_term_data(struct term_data *data,
		const region *win, const region *sub)
{
	assert(!data->loaded);

	assert(data->window == NULL);
	assert(data->fg.buf == NULL);

	data->window = newwin(win->h, win->w, win->y, win->x);
	assert(data->window != NULL);

	keypad(data->window, true);
	werase(data->window);

	if (sub != NULL
			&& sub->x > 0
			&& sub->y > 0
			&& sub->w > 0
			&& sub->h > 0)
	{
		data->subwindow =
			derwin(data->window,
				sub->h, sub->w, sub->y, sub->x);

		wattrset(data->window, A_DIM);
		box(data->window, 0, 0);

		data->tab_offset = sub->x + 1;
	} else {
		data->subwindow =
			derwin(data->window, win->h, win->w, 0, 0);

		data->tab_offset = DONT_USE_TABS;
	}

	make_fg_buf(data);

	data->loaded = true;
}

static void region_adjust(region *win, region *sub)
{
	if (win->x + win->w + 1 > COLS) {
		win->x = COLS - win->w - 1;
	}
	if (win->x < 0) {
		win->x = 0;
	}
	if (win->y + win->h + 1 > LINES) {
		win->y = LINES - win->h - 1;
	}
	if (win->y < 0) {
		win->y = 0;
	}

	if (win->x > 0 && win->y > 0
			&& win->x + win->w + 1 <= COLS
			&& win->y + win->h + 1 <= LINES)
	{
		sub->x = 1;
		sub->y = 1;
		sub->w = win->w;
		sub->h = win->h;

		win->x -= 1;
		win->w += 2;

		win->y -= 1;
		win->h += 2;
	} else {
		sub->x = 0;
		sub->y = 0;
		sub->w = win->w;
		sub->h = win->h;

		win->x = 0;
		win->y = 0;
	}
}

static void region_corner_map(region *win, region *sub)
{
	const struct term_data *map = get_perm_data(DISPLAY_CAVE);
	assert(map->loaded);

	int mapx;
	int mapy;
	getbegyx(map->window, mapy, mapx);

	win->x = mapx + 1;
	win->y = mapy + 1;

	region_adjust(win, sub);
}

static void region_exact_top(region *win, region *sub, int x, int y)
{
	const struct term_data *top = get_top();

	int topx;
	int topy;
	getbegyx(top->window, topy, topx);

	win->x = x + topx + 1;
	win->y = y + topy + 1;

	region_adjust(win, sub);
}

static void region_center_top(region *win, region *sub)
{
	const struct term_data *top = get_stack_top();

	region topwin;
	if (top == NULL) {
		getbegyx(stdscr, topwin.y, topwin.x);
		getmaxyx(stdscr, topwin.h, topwin.w);
	} else {
		getbegyx(top->window, topwin.y, topwin.x);
		getmaxyx(top->window, topwin.h, topwin.w);
	}

	win->x = topwin.x + (topwin.w - win->w) / 2;
	win->y = topwin.y + (topwin.h - win->h) / 2;

	region_adjust(win, sub);
}

static void region_big_map(region *win, region *sub)
{
	win->w = COLS;
	win->h = LINES;
	sub->w = COLS;
	sub->h = LINES;
}

static void calc_temp_window(const struct term_hints *hints,
		region *win, region *sub)
{
	memset(win, 0, sizeof(*win));
	memset(sub, 0, sizeof(*sub));

	if (hints->purpose == TERM_PURPOSE_BIG_MAP) {
		region_big_map(win, sub);
	} else {
		win->w = hints->width;
		win->h = hints->height;
		switch (hints->position) {
			case TERM_POSITION_TOP_LEFT:
				region_corner_map(win, sub);
				break;

			case TERM_POSITION_EXACT:
				region_exact_top(win, sub, hints->x, hints->y);
				break;

			default:
				region_center_top(win, sub);
				break;
		}
	}
}

static void term_push_new(const struct term_hints *hints,
		struct term_create_info *info)
{
	region win;
	region sub;
	calc_temp_window(hints, &win, &sub); 

	assert(sub.w > 0);
	assert(sub.h > 0);

	struct term_data *data = new_stack_data();
	assert(data->temporary);
	
	load_term_data(data, &win, &sub);

	info->user = data;
	info->blank = default_blank_point;
	info->width = sub.w,
	info->height = sub.h,
	info->callbacks = default_callbacks;

	curs_set(0);
}

static void term_pop_new(void *user)
{
	struct term_data *data = user;

	free_stack_data(data);
}

static int draw_points(struct term_data *data,
		const struct term_point *points, int n_points)
{
	uint32_t fg_attr = points[0].fg_attr;
	uint32_t terrain_attr = points[0].terrain_attr;

	int draw = 0;
	while (draw < n_points
			&& fg_attr == points[draw].fg_attr
			&& terrain_attr == points[draw].terrain_attr)
	{
		data->fg.buf[draw] = points[draw].fg_char;
		draw++;
	}

	assert(draw < data->fg.len);
	data->fg.buf[draw] = 0;

	wattrset(data->subwindow,
			g_attrs[G_ATTR_INDEX(fg_attr, terrain_attr)][fg_attr]);
	waddnwstr(data->subwindow, data->fg.buf, draw);

	return draw;
}

static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points)
{
	struct term_data *data = user;

	wmove(data->subwindow, row, col);

	int drawn = 0;
	while (drawn < n_points) {
		drawn += draw_points(data, points + drawn, n_points - drawn);
	}
}

static struct term_data *get_top(void)
{
	struct term_data *top = get_stack_top();

	if (top == NULL) {
		/* if there are no terms on the stack,
		 * we assume that map term is the top */
		top = get_perm_data(DISPLAY_CAVE);
	}

	assert(top != NULL);
	assert(top->loaded);

	return top;
}

static int get_ch(struct term_data *data, bool wait)
{
	int ch;

	if (wait) {
		halfdelay(HALFDELAY_PERIOD);
		for (ch = wgetch(data->window);
				ch == ERR;
				ch = wgetch(data->window))
		{
			if (data->index == DISPLAY_CAVE) {
				idle_update();
			}
		}
		cbreak();
	} else {
		nodelay(data->window, true);
		ch = wgetch(data->window);
		nodelay(data->window, false);
	}

	return ch;
}
static void define_keys(void)
{
	/* Stolen from Dungeon Crawl's source...
	 * It appears that ncurses redefines some of these
	 * control strings when keypad() is called on a window;
	 * so we use these definitions as a fallback for ncurses,
	 * in case it doesn't have suitable strings in its database.
	 * Note that some keypad keys are mapped to KC_HOME, KC_END, etc;
	 * Angband defines keymaps that translate those to movement keys. */

    /* keypad 0 - 9 */
    define_key("\033Op",  KC_INSERT);
    define_key("\033Oq",  KC_END);
    define_key("\033Or", '2');
    define_key("\033Os",  KC_PGDOWN);
    define_key("\033Ot", '4');
    define_key("\033Ou", '5');
    define_key("\033Ov", '6');
    define_key("\033Ow",  KC_HOME);
    define_key("\033Ox", '8');
    define_key("\033Oy",  KC_PGUP);

    /* non-arrow keypad keys */
    define_key("\033OM", KC_ENTER);
    define_key("\033OQ", '/');
    define_key("\033OR", '*');
    define_key("\033OS", '-');
    define_key("\033Oj", '*');
    define_key("\033Ok", '+');
    define_key("\033Ol", '+');
    define_key("\033Om", '.');
    define_key("\033On", '.');
    define_key("\033Oo", '-');

    define_key("\033[1~", KC_HOME);
    define_key("\033[4~", KC_END);
    define_key("\033[E",  '5');
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
		case 033:           key = ESCAPE;       break;

		case KEY_HOME:      key = KC_HOME;      break;
		case KEY_END:       key = KC_END;       break;
		case KEY_PPAGE:     key = KC_PGUP;      break;
		case KEY_NPAGE:     key = KC_PGDOWN;    break;

		/* Keypad keys */
		case KEY_C1:        key = '1';          break;
		case KEY_C3:        key = '3';          break;
		case KEY_B2:        key = '5';          break;
		case KEY_A1:        key = '7';          break;
		case KEY_A3:        key = '9';          break;

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
	struct term_data *data = user;

	int ch = get_ch(data, wait);

	if (ch != ERR && ch != EOF) {
		Term_keypress(ch_to_code(ch), 0);
	}
}

static void term_flush_events(void *user)
{
	(void) user;

	flushinp();
}

static void term_make_visible(void *user, bool visible)
{
	/* We don't make windows visible or invisible */
	
	(void) user;
	(void) visible;
}

static void term_cursor(void *user, bool visible, int col, int row)
{
	struct term_data *data = user;

	if (visible && !data->cursor.visible) {
		curs_set(1);
		data->cursor.visible = true;
	} else if (!visible && data->cursor.visible) {
		curs_set(0);
		data->cursor.visible = false;
	}

	data->cursor.col = col;
	data->cursor.row = row;
}

static void term_delay(void *user, int msecs)
{
	(void) user;

	napms(msecs);
}

static void term_erase(void *user)
{
	struct term_data *data = user;

	werase(data->subwindow);
}

/* Strip label of initial spaces, and return
 * length without spaces at the end (if any) */
static int wihout_spaces(const wchar_t **label)
{
	const wchar_t *l = *label;

	while (*l == L' ') {
		l++;
	}
	*label = l;

	int len = 0;
	while (*l) {
		l++;
		len++;
	}

	for (l--; len > 0 && *l == L' '; l--) {
		len--;
	}

	return len;
}

static void term_add_tab(void *user,
		keycode_t code, const wchar_t *label, uint32_t fg_attr, uint32_t bg_attr)
{
	(void) code;

	struct term_data *data = user;

	if (data->tab_offset != DONT_USE_TABS) {
		const int len = wihout_spaces(&label);

		wattrset(data->window, g_attrs[G_ATTR_NORMAL][fg_attr]);
		mvwaddnwstr(data->window, 0, data->tab_offset, label, len);

		data->tab_offset += len + 1;
	}
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

static void calc_perm_window(enum display_term_index index, region *win)
{
	const region *reg = get_term_region(index);

	assert(reg->x >= 0);
	assert(reg->y >= 0);

	const struct term_info *info = get_term_info(index);

	if (reg->w < info->min_cols || reg->h < info->min_rows) {
		get_out("Screen size for term '%s'" /* concat */
				" is too small (need %dx%d, got %dx%d)",
				info->name, info->min_cols, info->min_rows, reg->w, reg->h);
	}

	*win = *reg;
}

static void handle_cursor(bool update)
{
	const struct term_data *top = get_top();

	if (update) {
		curs_set(top->cursor.visible ? 1 : 0);
	}

	if (top->cursor.visible) {
		wmove(top->subwindow, top->cursor.row, top->cursor.col);
		wnoutrefresh(top->subwindow);
	}
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
	region win;
	calc_perm_window(index, &win);

	struct term_data *data = get_perm_data(index);

	load_term_data(data, &win, NULL);

	struct term_create_info info = {
		.user      = data,
		.width     = win.w,
		.height    = win.h,
		.callbacks = default_callbacks,
		.blank     = default_blank_point,
	};

	display_term_create(index, &info);
}

static void load_perm_terms(void)
{
	for (unsigned i = 0; i < DISPLAY_MAX; i++) {
		const region *reg = get_term_region(i);
		if (reg->w > 0 && reg->h > 0) {
			load_term(i);
		}
	}
}

static void wipe_term_data(struct term_data *data)
{
	bool temporary = data->temporary;
	unsigned index = data->index;

	memset(data, 0, sizeof(*data));

	data->temporary = temporary;
	data->index = index;

	data->subwindow = NULL;
	data->window = NULL;

	data->fg.buf = NULL;
}

static void free_term_data(struct term_data *data)
{
	assert(data->loaded);

	assert(data->fg.buf != NULL);
	assert(data->fg.len != 0);
	mem_free(data->fg.buf);

	assert(data->subwindow != NULL);
	delwin(data->subwindow);

	assert(data->window != NULL);
	delwin(data->window);

	wipe_term_data(data);
}

static void free_terms(void)
{
	for (unsigned i = 0; i < DISPLAY_MAX; i++) {
		struct term_data *data = get_perm_data(i);

		if (data->loaded) {
			display_term_destroy(data->index);
			free_term_data(data);
		}
	}
}

static void init_max_colors(void)
{
	assert(COLORS      >= MIN_EXTENDED_COLORS);
	assert(COLOR_PAIRS >= MIN_EXTENDED_COLOR_PAIRS);

	assert(N_ELEMENTS(g_attrs[G_ATTR_NORMAL]) == BASIC_COLORS);

#define SCALE_COLOR(color, channel) \
	((long) angband_color_table[(color)][(channel)] * 1000L / 255L)

	int pair = 0;

	/* color pair zero is special (to ncurses);
	 * we don't want to call init_pair() on it */
	init_color(COLORS - 1,
			SCALE_COLOR(0, 1), SCALE_COLOR(0, 2), SCALE_COLOR(0, 3));
	g_attrs[G_ATTR_NORMAL][pair] = COLOR_PAIR(pair);
	pair++;

	/* init colors downwards, so as not to clobber
	 * existing terminal colors, because ncurses
	 * seems to be incapable of restoring them */
	for (int c = 1; c < BASIC_COLORS; c++, pair++) {
		init_color(COLORS - 1 - c,
				SCALE_COLOR(c, 1), SCALE_COLOR(c, 2), SCALE_COLOR(c, 3));
		init_pair(pair, COLORS - 1 - c, -1);
		g_attrs[G_ATTR_NORMAL][c] = COLOR_PAIR(c);
	}

#undef SCALE_COLOR

	for (int c = 0, shade = COLORS - 1 - COLOUR_SHADE;
			c < BASIC_COLORS;
			c++, pair++)
	{
		init_pair(pair, COLORS - 1 - c, shade);
		g_attrs[G_ATTR_HYBRID][c] = COLOR_PAIR(pair);
	}

	for (int c = 0; c < BASIC_COLORS; c++, pair++) {
		init_pair(pair, COLORS - 1 - c, COLORS - 1 - c);
		g_attrs[G_ATTR_SOLID][c] = COLOR_PAIR(pair);
	}
}

static void init_min_colors(void)
{
	assert(COLORS      >= MIN_COLORS);
	assert(COLOR_PAIRS >= MIN_COLOR_PAIRS);

	/* In some terminals the cursor has the same color as the grid
	 * under it; if there are problems with cursor, notice that
	 * PAIR_BLACK has black background and foreground, and
	 * solid walls have an attribute A_INVIS */

	init_pair(PAIR_RED,     COLOR_RED,     COLOR_BLACK);
	init_pair(PAIR_GREEN,   COLOR_GREEN,   COLOR_BLACK);
	init_pair(PAIR_YELLOW,  COLOR_YELLOW,  COLOR_BLACK);
	init_pair(PAIR_BLUE,    COLOR_BLUE,    COLOR_BLACK);
	init_pair(PAIR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
	init_pair(PAIR_CYAN,    COLOR_CYAN,    COLOR_BLACK);
	init_pair(PAIR_BLACK,   COLOR_BLACK,   COLOR_BLACK);

	g_attrs[G_ATTR_NORMAL][COLOUR_DARK]        = COLOR_PAIR(PAIR_BLACK);
	g_attrs[G_ATTR_NORMAL][COLOUR_WHITE]       = COLOR_PAIR(PAIR_WHITE)   | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_SLATE]       = COLOR_PAIR(PAIR_WHITE);
	g_attrs[G_ATTR_NORMAL][COLOUR_ORANGE]      = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_RED]         = COLOR_PAIR(PAIR_RED);
	g_attrs[G_ATTR_NORMAL][COLOUR_GREEN]       = COLOR_PAIR(PAIR_GREEN);
	g_attrs[G_ATTR_NORMAL][COLOUR_BLUE]        = COLOR_PAIR(PAIR_BLUE);
	g_attrs[G_ATTR_NORMAL][COLOUR_UMBER]       = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[G_ATTR_NORMAL][COLOUR_L_DARK]      = COLOR_PAIR(PAIR_BLACK)   | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_WHITE]     = COLOR_PAIR(PAIR_WHITE);
	g_attrs[G_ATTR_NORMAL][COLOUR_L_PURPLE]    = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[G_ATTR_NORMAL][COLOUR_YELLOW]      = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_RED]       = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_GREEN]     = COLOR_PAIR(PAIR_GREEN)   | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_BLUE]      = COLOR_PAIR(PAIR_BLUE)    | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_UMBER]     = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[G_ATTR_NORMAL][COLOUR_PURPLE]      = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[G_ATTR_NORMAL][COLOUR_VIOLET]      = COLOR_PAIR(PAIR_MAGENTA);
	g_attrs[G_ATTR_NORMAL][COLOUR_TEAL]        = COLOR_PAIR(PAIR_CYAN);
	g_attrs[G_ATTR_NORMAL][COLOUR_MUD]         = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[G_ATTR_NORMAL][COLOUR_L_YELLOW]    = COLOR_PAIR(PAIR_YELLOW)  | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_MAGENTA]     = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_TEAL]      = COLOR_PAIR(PAIR_CYAN)    | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_VIOLET]    = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_L_PINK]      = COLOR_PAIR(PAIR_MAGENTA) | A_BOLD;
	g_attrs[G_ATTR_NORMAL][COLOUR_MUSTARD]     = COLOR_PAIR(PAIR_YELLOW);
	g_attrs[G_ATTR_NORMAL][COLOUR_BLUE_SLATE]  = COLOR_PAIR(PAIR_BLUE);
	g_attrs[G_ATTR_NORMAL][COLOUR_DEEP_L_BLUE] = COLOR_PAIR(PAIR_BLUE);

	for (size_t i = 0; i < N_ELEMENTS(g_attrs[G_ATTR_NORMAL]); i++) {
		g_attrs[G_ATTR_HYBRID][i] = g_attrs[G_ATTR_NORMAL][i] | A_REVERSE;
		g_attrs[G_ATTR_SOLID][i] = g_attrs[G_ATTR_NORMAL][i] | A_REVERSE | A_INVIS;
	}
}

static void init_ncurses_colors(void)
{
	if (start_color() == ERR) {
		get_out("Can't initialize color");
	}

	if (!has_colors()) {
		get_out("Can't start without color");
	}

	use_default_colors();

	if (!can_change_color()
			|| COLORS < MIN_EXTENDED_COLORS
			|| COLOR_PAIRS < MIN_EXTENDED_COLOR_PAIRS)
	{
		init_min_colors();
	} else {
		init_max_colors();
	}
}

static void handle_quit(void)
{
	Term_pop_all();
	free_terms();
	endwin();
}

static void get_out(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	char *msg = vformat(fmt, ap);
	va_end(ap);

	handle_quit();
	quit_aux = NULL;
	quit(msg);
}

static void quit_hook(const char *s)
{
	(void) s;

	handle_quit();
}

int init_ncurses(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	setenv("ESCDELAY", "20", 0);

	if (initscr() == NULL) {
		return 1;
	}

	cbreak();
	noecho();
	nonl();

	define_keys();

	curs_set(0);

	init_globals();
	load_perm_terms();

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

/* All terms must be redrawn completely */
static int g_update;

/* Information about terms (description, size, etc) */
static struct term_info g_term_info[] = {
	#define DISPLAY(i, desc, minc, minr, defc, defr, maxc, maxr, req) \
		{ \
			.index    = DISPLAY_ ##i, \
			.name     = (desc), \
			.env_name = "ANGBAND_TERM_" #i, \
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

	calc_default_term_regions();
	read_env_term_regions();
	init_ncurses_colors();
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
	g_update = true;

	return &g_temp_data.stack[t];
}

static void free_stack_data(struct term_data *data)
{
	assert(data->loaded);
	assert(data->temporary);

	assert(g_temp_data.top > 0);
	assert(&g_temp_data.stack[g_temp_data.top - 1] == data);

	free_term_data(data);

	g_temp_data.top--;
	g_update = true;
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

static void redraw_terms(void)
{
	if (g_update) {
		erase();
		refresh();
		touch_win(g_perm_data, N_ELEMENTS(g_perm_data));
		touch_win(g_temp_data.stack, g_temp_data.top);
	}

	redraw_win(g_perm_data, N_ELEMENTS(g_perm_data));
	redraw_win(g_temp_data.stack, g_temp_data.top);

	handle_cursor(g_update);
	doupdate();

	g_update = false;
}

/* Areas of permanent terms on the screen;
 * initialized by calc_default_term_regions()
 * and environment variables defined by the user (not implemented yet) */
static region g_term_regions[DISPLAY_MAX];

static const region *get_term_region(enum display_term_index i)
{
	assert(i >= 0);
	assert(i < DISPLAY_MAX);

	return &g_term_regions[i];
}

static void calc_default_term_regions(void)
{
	if (COLS < ANGBAND_TERM_STANDARD_WIDTH
			|| LINES < ANGBAND_TERM_STANDARD_HEIGHT)
	{
		get_out("Angband needs at least %dx%d screen (you have %dx%d)",
				ANGBAND_TERM_STANDARD_WIDTH, ANGBAND_TERM_STANDARD_HEIGHT,
				COLS, LINES);
	}

	const struct term_info *sidebar      = get_term_info(DISPLAY_PLAYER_COMPACT);
	const struct term_info *message_line = get_term_info(DISPLAY_MESSAGE_LINE);
	const struct term_info *status_line  = get_term_info(DISPLAY_STATUS_LINE);

	g_term_regions[DISPLAY_CAVE].x = sidebar->def_cols + 1;
	g_term_regions[DISPLAY_CAVE].y = message_line->def_rows;
	g_term_regions[DISPLAY_CAVE].w = COLS - sidebar->def_cols - 1;
	g_term_regions[DISPLAY_CAVE].h =
		LINES - message_line->def_rows - status_line->def_rows;

	g_term_regions[DISPLAY_PLAYER_COMPACT].x = 0;
	g_term_regions[DISPLAY_PLAYER_COMPACT].y = message_line->def_rows;
	g_term_regions[DISPLAY_PLAYER_COMPACT].w = sidebar->def_cols + 1;
	g_term_regions[DISPLAY_PLAYER_COMPACT].h =
		LINES - message_line->def_rows - status_line->def_rows;

	g_term_regions[DISPLAY_MESSAGE_LINE].x = 0;
	g_term_regions[DISPLAY_MESSAGE_LINE].y = 0;
	g_term_regions[DISPLAY_MESSAGE_LINE].w = COLS;
	g_term_regions[DISPLAY_MESSAGE_LINE].h = message_line->def_rows;

	g_term_regions[DISPLAY_STATUS_LINE].x = 0;
	g_term_regions[DISPLAY_STATUS_LINE].y = LINES - status_line->def_rows;
	g_term_regions[DISPLAY_STATUS_LINE].w = COLS;
	g_term_regions[DISPLAY_STATUS_LINE].h = status_line->def_rows;
}

static void env_error(const char *error, const char *hint,
		const struct term_info *info)
{
	get_out("Error %s: %s\n(term \"%s\", variable %s)",
			error, hint, info->name, info->env_name);
}

static void read_env_term_regions(void)
{
	const region scr = {0, 0, COLS, LINES};

	for (unsigned i = 0; i < DISPLAY_MAX; i++) {

		const struct term_info *info = get_term_info(i);
		const char *size = getenv(info->env_name);

		if (size != NULL) {
			region reg = {0};

			if (sscanf(size, "%dx%dx%dx%d", &reg.x, &reg.y, &reg.w, &reg.h) != 4) {
				env_error("reading variable", size, info);
			}

			if (reg.w > COLS || reg.h > LINES) {
				env_error("in size", "term is too big", info);
			}

			if (reg.w < info->min_cols || reg.h < info->min_rows) {
				env_error("in size", "term is too small", info);
			}

			if (!region_in_region(&reg, &scr)) {
				env_error("in size or coordinates", "term is offscreen", info);
			}

			g_term_regions[info->index] = reg;
		}
	}
}

#endif /* USE_NCURSES */
