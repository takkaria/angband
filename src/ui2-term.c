/**
 * \file ui2-term.c
 * \brief A generic, efficient, terminal window package
 *
 * Copyright (c) 1997 Ben Harrison and others
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

#include "ui2-term.h"
#include "z-virt.h"
#include "z-util.h"

/* Local declarations */

struct term_cursor {
	int x;
	int y;
	bool visible;
};

struct term_dirty {
	int top;
	int bottom;
	struct {
		int left;
		int right;
	} *rows;
};

struct term {
	void *user;

	bool temporary;

	struct term_point *points;

	int size;
	int width;
	int height;

	struct {
		struct term_cursor new;
		struct term_cursor old;
	} cursor;
	struct term_dirty dirty;
	struct term_callbacks callbacks;
	struct term_point blank;
};

#define TERM_STACK_MAX \
	128

#define TERM_EVENT_QUEUE_MAX \
	1024

#define WIDESTRING_MAX \
	1024

#define TOP \
	(term_stack.stack[term_stack.top])

#define NOTOP \
	(-1)

#define COORDS_OK(x, y) do { \
	assert(TOP != NULL); \
	assert((x) >= 0); \
	assert((x) < TOP->width); \
	assert((y) >= 0); \
	assert((y) < TOP->height); \
} while (0)

#define STACK_OK() do { \
	assert(TOP != NULL); \
	assert(term_stack.top > NOTOP); \
	assert(term_stack.top < (int) N_ELEMENTS(term_stack.stack)); \
} while (0)

/* cursor's x position can be one point beyound the right edge of the term */
#define CURSOR_OK() do { \
	assert(TOP->cursor.new.x >= 0); \
	assert(TOP->cursor.new.x <= TOP->width); \
	assert(TOP->cursor.new.y >= 0); \
	assert(TOP->cursor.new.y < TOP->height); \
} while (0)

#define INDEX(x, y) \
	((x) + (y) * TOP->width)

#define POINT(x, y) \
	(TOP->points[INDEX(x, y)])

/* Local variables */

static struct {
	term stack[TERM_STACK_MAX];
	int top;
} term_stack = {
	.top = NOTOP
};

static struct {
	ui_event queue[TERM_EVENT_QUEUE_MAX];
	size_t head;
	size_t tail;
	size_t size;
	size_t number;
} event_queue = {
	.size = TERM_EVENT_QUEUE_MAX
};

/* Local functions */

static void term_make_dirty(term t)
{
	t->dirty.top = 0;
	t->dirty.bottom = t->height - 1;
	for (int y = t->dirty.top; y <= t->dirty.bottom; y++) {
		t->dirty.rows[y].left = 0;
		t->dirty.rows[y].right = t->width - 1;
	}

	for (int i = 0; i < t->size; i++) {
		t->points[i].dirty = true;
	}
}

static term term_alloc(int w, int h)
{
	assert(w > 0);
	assert(h > 0);

	struct term *t = mem_zalloc(sizeof(*t));

	t->width = w;
	t->height = h;
	t->size = w * h;

	t->points = mem_zalloc(t->size * sizeof(t->points[0]));
	t->dirty.rows = mem_zalloc(t->height * sizeof(t->dirty.rows[0]));

	return t;
}

static void term_free(term t)
{
	mem_free(t->points);
	mem_free(t->dirty.rows);
	mem_free(t);
}

static void term_copy(term new, term old)
{
	int min_width = MIN(new->width, old->width);
	int min_height = MIN(new->height, old->height);

	for (int y = 0; y < min_height; y++) {
		int newx = new->width * y;
		int oldx = old->width * y;
		for (int x = 0; x < min_width; x++, newx++, oldx++) {
			new->points[newx] = old->points[oldx];
		}
	}
}

static term term_new(const struct term_create_info *info)
{
	term t = term_alloc(info->width, info->height);

	t->user = info->user;
	t->blank = info->blank;
	t->callbacks = info->callbacks;

	assert(t->callbacks.flush_events != NULL);
	assert(t->callbacks.max_size != NULL);
	assert(t->callbacks.push_new != NULL);
	assert(t->callbacks.pop_new != NULL);
	assert(t->callbacks.redraw != NULL);
	assert(t->callbacks.cursor != NULL);
	assert(t->callbacks.event != NULL);
	assert(t->callbacks.delay != NULL);
	assert(t->callbacks.draw != NULL);

	term_make_dirty(t);

	t->temporary = false;
	
	return t;
}

static void term_draw(int x, int y, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	assert(len > 0);

	TOP->callbacks.draw(TOP->user, x, y, len, &POINT(x, y));
}

static void term_mark_point_dirty(int x, int y)
{
	STACK_OK();
	COORDS_OK(x, y);

	POINT(x, y).dirty = true;

	if (y < TOP->dirty.top) {
		TOP->dirty.top = y;
	}
	if (y > TOP->dirty.bottom) {
		TOP->dirty.bottom = y;
	}

	if (x < TOP->dirty.rows[y].left) {
		TOP->dirty.rows[y].left = x;
	}
	if (x > TOP->dirty.rows[y].right) {
		TOP->dirty.rows[y].right = x;
	}
}

static void term_mark_line_dirty(int x, int y, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	assert(len > 0);

	int z = MIN(x + len, TOP->width);

	term_mark_point_dirty(x, y);
	while (x < z) {
		POINT(x, y).dirty = true;
		x++;
	}
	term_mark_point_dirty(z - 1, y);
}

static void term_mark_row_flushed(int y)
{
	STACK_OK();
	COORDS_OK(TOP->dirty.rows[y].left, y);
	COORDS_OK(TOP->dirty.rows[y].right, y);

	for (int x = TOP->dirty.rows[y].left; x <= TOP->dirty.rows[y].right; x++) {
		POINT(x, y).dirty = false;
	}

	TOP->dirty.rows[y].left = TOP->width;
	TOP->dirty.rows[y].right = 0;
}

static struct term_point term_make_point(uint32_t fga, wchar_t fgc,
		uint32_t bga, wchar_t bgc, bitflag *flags)
{
	struct term_point point = {
		.dirty = true,
		.fg_char = fgc,
		.fg_attr = fga,
		.bg_char = bgc,
		.bg_attr = bga
	};
	
	if (flags != NULL) {
		tpf_copy(point.flags, flags);
	}

	return point;
}

static void term_set_point(int x, int y, struct term_point point)
{
	STACK_OK();
	COORDS_OK(x, y);

	POINT(x, y) = point;
	term_mark_point_dirty(x, y);
}

static void term_set_fg(int x, int y, uint32_t fga, wchar_t fgc)
{
	STACK_OK();
	COORDS_OK(x, y);

	POINT(x, y).fg_attr = fga;
	POINT(x, y).fg_char = fgc;
	term_mark_point_dirty(x, y);
}

static bool term_put_point_at_cursor(struct term_point point)
{
	STACK_OK();
	CURSOR_OK();
	
	if (TOP->cursor.new.x < TOP->width) {
		term_set_point(TOP->cursor.new.x, TOP->cursor.new.y, point);
		TOP->cursor.new.x++;
		return true;
	} else {
		return false;
	}
}

static bool term_put_fg_at_cursor(uint32_t fga, wchar_t fgc)
{
	STACK_OK();
	CURSOR_OK();
	
	if (TOP->cursor.new.x < TOP->width) {
		term_set_fg(TOP->cursor.new.x, TOP->cursor.new.y, fga, fgc);
		TOP->cursor.new.x++;
		return true;
	} else {
		return false;
	}
}

static int term_set_ws(int x, int y, int len, uint32_t fga, const wchar_t *ws)
{
	STACK_OK();
	COORDS_OK(x, y);

	assert(*ws != 0);
	assert(len > 0);

	const wchar_t *curws = ws;

	for (int curx = x;
			curx < MIN(TOP->width, x + len) && *curws != 0;
			curx++, curws++)
	{
		POINT(curx, y).fg_attr = fga;
		POINT(curx, y).fg_char = *curws;
	}

	term_mark_line_dirty(x, y, curws - ws);

	return curws - ws;
}

static bool term_put_ws_at_cursor(int len, uint32_t fga, const wchar_t *ws)
{
	STACK_OK();
	CURSOR_OK();

	TOP->cursor.new.x +=
		term_set_ws(TOP->cursor.new.x, TOP->cursor.new.y, len, fga, ws);

	CURSOR_OK();

	return TOP->cursor.new.x < TOP->width;
}

static void term_wipe_line(int x, int y, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	assert(len > 0);

	for (int curx = x, z = MIN(x + len, TOP->width); curx < z; curx++) {
		POINT(curx, y) = TOP->blank;
	}
	term_mark_line_dirty(x, y, len);
}

static void term_move_cursor(int x, int y)
{
	STACK_OK();
	CURSOR_OK();

	TOP->cursor.new.x = x;
	TOP->cursor.new.y = y;

	CURSOR_OK();
}

static bool term_check_event(ui_event *event)
{
	if (event_queue.number == 0) {
		return false;
	}

	if (event != NULL) {
		*event = event_queue.queue[event_queue.tail];
	}

	return true;
}

static bool term_take_event(ui_event *event)
{
	if (!term_check_event(event)) {
		return false;
	}

	assert(event_queue.number > 0);

	event_queue.number--;
	event_queue.tail++;

	if (event_queue.tail == event_queue.size) {
		event_queue.tail = 0;
	}

	return true;
}

static bool term_prepend_events(const ui_event *events, size_t num_events)
{
	if (event_queue.size - event_queue.number < num_events) {
		return false;
	}

	for (size_t i = num_events; i > 0; i--) {
		if (event_queue.tail == 0) {
			event_queue.tail = event_queue.size;
		}
		event_queue.tail--;

		event_queue.queue[event_queue.tail] = events[i - 1];
		event_queue.number++;
	}

	return true;
}

static bool term_append_events(const ui_event *events, size_t num_events)
{
	if (event_queue.size - event_queue.number < num_events) {
		return false;
	}

	for (size_t i = 0; i < num_events; i++) {
		event_queue.queue[event_queue.head] = events[i];
		event_queue.number++;

		event_queue.head++;
		if (event_queue.head == event_queue.size) {
			event_queue.head = 0;
		}
	}

	return true;
}

static void term_mbstowcs(wchar_t *ws, const char *mbs, size_t ws_max_len)
{
	size_t len = text_mbstowcs(ws, mbs, ws_max_len);

	if (len == (size_t) -1) {
		quit_fmt("can't convert the string '%s'", mbs);
	} else if (len == ws_max_len) {
		ws[ws_max_len - 1] = 0;
	}
}

static void term_erase_cursor(struct term_cursor cursor)
{
	STACK_OK();

	if (cursor.visible && cursor.x < TOP->width) {
		term_draw(cursor.x, cursor.y, 1);
	}
}

static void term_draw_cursor(struct term_cursor cursor)
{
	STACK_OK();

	if (cursor.visible && cursor.x < TOP->width) {
		TOP->callbacks.cursor(TOP->user, cursor.x, cursor.y);
	}
}

static void term_flush_row(int y)
{
	STACK_OK();
	COORDS_OK(TOP->dirty.rows[y].left, y);
	COORDS_OK(TOP->dirty.rows[y].right, y);

	int len = 0;
	int drawx = 0;

	for (int x = TOP->dirty.rows[y].left; x <= TOP->dirty.rows[y].right; x++) {
		if (POINT(x, y).dirty) {
			if (len == 0) {
				drawx = x;
			}
			len++;
		} else if (len > 0) {
			term_draw(drawx, y, len);
			len = 0;
		}
	}

	if (len > 0) {
		term_draw(drawx, y, len);
	}

	term_mark_row_flushed(y);
}

static void term_flush_out(void)
{
	for (int y = TOP->dirty.top; y <= TOP->dirty.bottom; y++) {
		term_flush_row(y);
	}

	TOP->dirty.top = TOP->height;
	TOP->dirty.bottom = 0;
}

/* External functions */

term Term_create(const struct term_create_info *info)
{
	return term_new(info);
}

void Term_destroy(term *t)
{
	assert((*t)->temporary == false);

	term_free(*t);
	*t = NULL;
}

void Term_setpriv(term t, void *user)
{
	t->user = user;
}

void *Term_getpriv(term t)
{
	return t->user;
}

term Term_top(void)
{
	if (term_stack.top > NOTOP) {
		return TOP;
	} else {
		return NULL;
	}
}

void Term_push(term t)
{
	assert(t != NULL);
	assert(!t->temporary);

	term_stack.top++;
	TOP = t;

	STACK_OK();
}

void Term_push_new(struct term_hints *hints)
{
	assert(hints->width > 0);
	assert(hints->height > 0);

	struct term_create_info info = {0};

	TOP->callbacks.push_new(hints, &info);

	if (info.width == 0) {
		info.width = hints->width;
	}
	if (info.height == 0) {
		info.height = hints->height;
	}

	term_stack.top++;
	TOP = term_new(&info);
	TOP->temporary = true;

	STACK_OK();
}

void Term_pop(void)
{
	STACK_OK();

	if (TOP->temporary) {
		TOP->callbacks.pop_new(TOP->user);
		term_free(TOP);
	}

	TOP = NULL;
	term_stack.top--;
}

bool Term_putwchar(uint32_t fga, wchar_t fgc,
		uint32_t bga, wchar_t bgc, bitflag *flags)
{
	return term_put_point_at_cursor(term_make_point(fga, fgc, bga, bgc, flags));
}

bool Term_putwc(uint32_t fga, wchar_t fgc)
{
	return term_put_fg_at_cursor(fga, fgc);
}

bool Term_putws(int len, uint32_t fga, const wchar_t *fgc)
{
	assert(len >= 0);

	return term_put_ws_at_cursor(len, fga, fgc);
}

bool Term_puts(int len, uint32_t fga, const char *fgc)
{
	assert(len >= 0);

	wchar_t ws[WIDESTRING_MAX];
	term_mbstowcs(ws, fgc, WIDESTRING_MAX);

	return term_put_ws_at_cursor(len, fga, ws);
}

bool Term_addwchar(int x, int y,
		uint32_t fga, wchar_t fgc, uint32_t bga, wchar_t bgc, bitflag *flags)
{
	term_move_cursor(x, y);

	return term_put_point_at_cursor(term_make_point(fga, fgc, bga, bgc, flags));
}

bool Term_addwc(int x, int y, uint32_t fga, wchar_t fgc)
{
	term_move_cursor(x, y);

	return term_put_fg_at_cursor(fga, fgc);
}

bool Term_addws(int x, int y, int len, uint32_t fga, const wchar_t *fgc)
{
	assert(len >= 0);

	term_move_cursor(x, y);

	return term_put_ws_at_cursor(len, fga, fgc);
}

bool Term_adds(int x, int y, int len, uint32_t fga, const char *fgc)
{
	assert(len >= 0);

	term_move_cursor(x, y);

	wchar_t ws[WIDESTRING_MAX];
	term_mbstowcs(ws, fgc, WIDESTRING_MAX);

	return term_put_ws_at_cursor(len, fga, ws);
}

void Term_erase(int x, int y, int len)
{
	assert(len > 0);

	term_wipe_line(x, y, len);
}

void Term_erase_line(int x, int y)
{
	STACK_OK();

	term_wipe_line(x, y, TOP->width);
}

void Term_clear(void)
{
	for (int y = 0; y < TOP->height; y++) {
		term_wipe_line(0, y, TOP->width);
	}
}

void Term_dirty_point(int x, int y)
{
	term_mark_point_dirty(x, y);
}

void Term_dirty_region(int left, int top, int right, int bottom)
{
	COORDS_OK(left, top);
	COORDS_OK(right, bottom);

	assert(left <= right);
	assert(top <= bottom);

	int len = right - left + 1;
	for (int y = top; y <= bottom; y++) {
		term_mark_line_dirty(left, y, len);
	}
}

void Term_dirty_all(void)
{
	for (int y = 0; y < TOP->height; y++) {
		term_mark_line_dirty(0, y, TOP->width);
	}
}

void Term_get_cursor(int *x, int *y, bool *visible, bool *usable)
{
	STACK_OK();
	CURSOR_OK();

	if (x != NULL) {
		*x = TOP->cursor.new.x;
	}
	if (y != NULL) {
		*y = TOP->cursor.new.y;
	}
	if (visible != NULL) {
		*visible = TOP->cursor.new.visible;
	}
	if (usable != NULL) {
		*usable = TOP->cursor.new.x < TOP->width;
	}
}

void Term_cursor_to_xy(int x, int y)
{
	term_move_cursor(x, y);
}

void Term_cursor_visible(bool visible)
{
	STACK_OK();

	TOP->cursor.new.visible = visible;
}

void Term_get_size(int *w, int *h)
{
	STACK_OK();

	*w = TOP->width;
	*h = TOP->height;
}

int Term_width(void)
{
	STACK_OK();

	return TOP->width;
}

int Term_height(void)
{
	STACK_OK();

	return TOP->height;
}

void Term_get_point(int x, int y, struct term_point *point)
{
	STACK_OK();
	COORDS_OK(x, y);

	*point = POINT(x, y);
}

void Term_set_point(int x, int y, struct term_point point)
{
	term_set_point(x, y, point);
}

void Term_add_point(int x, int y, struct term_point point)
{
	term_move_cursor(x, y);

	term_put_point_at_cursor(point);
}

bool Term_point_ok(int x, int y)
{
	STACK_OK();

	return x >= 0
		&& y >= 0
		&& x < TOP->width
		&& y < TOP->width;
}

void Term_resize(int w, int h)
{
	STACK_OK();
	assert(w > 0);
	assert(h > 0);

	struct term old = *TOP;

	TOP->width = w;
	TOP->height = h;
	TOP->size = w * h;

	TOP->points = mem_zalloc(TOP->size * sizeof(TOP->points[0]));
	TOP->dirty.rows = mem_zalloc(TOP->height * sizeof(TOP->dirty.rows[0]));

	for (int y = 0; y < TOP->height; y++) {
		term_wipe_line(0, y, TOP->width);
	}

	term_copy(TOP, &old);

	mem_free(old.points);
	mem_free(old.dirty.rows);
}

void Term_flush_output(void)
{
	STACK_OK();

	term_erase_cursor(TOP->cursor.old);
	term_flush_out();
	term_draw_cursor(TOP->cursor.new);

	TOP->cursor.old = TOP->cursor.new;
}

void Term_redraw_screen(void)
{
	STACK_OK();

	TOP->callbacks.redraw(TOP->user);
}

bool Term_keypress(keycode_t key, byte mods)
{
	STACK_OK();

	ui_event event = {
		.key = {
			.type = EVT_KBRD,
			.code = key,
			.mods = mods
		}
	};

	return term_append_events(&event, 1);
}

bool Term_mousepress(int x, int y, int button, byte mods, int index)
{
	STACK_OK();

	ui_event event = {
		.mouse = {
			.type = EVT_MOUSE,
			.x = x,
			.y = y,
			.button = button,
			.mods = mods,
			.index = index
		}
	};

	return term_append_events(&event, 1);
}

bool Term_take_event(ui_event *event)
{
	STACK_OK();

	if (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, false);
	}

	return term_take_event(event);
}

bool Term_wait_event(ui_event *event)
{
	STACK_OK();

	assert(event != NULL);

	while (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, true);
	}

	return term_take_event(event);
}

bool Term_check_event(ui_event *event)
{
	STACK_OK();

	if (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, false);
	}

	return term_check_event(event);
}

bool Term_prepend_events(const ui_event *events, size_t num_events)
{
	return term_prepend_events(events, num_events);
}

bool Term_append_events(const ui_event *events, size_t num_events)
{
	return term_append_events(events, num_events);
}

void Term_flush_events(void)
{
	STACK_OK();

	TOP->callbacks.flush_events(TOP->user);

	event_queue.head = 0;
	event_queue.tail = 0;
	event_queue.number = 0;
}

void Term_delay(int msecs)
{
	STACK_OK();

	assert(msecs > 0);

	TOP->callbacks.delay(TOP->user, msecs);
}

void Term_max_size(int *w, int *h)
{
	STACK_OK();

	TOP->callbacks.max_size(TOP->user, w, h);

	assert(*w > 0);
	assert(*h > 0);
}
