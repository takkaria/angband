/**
 * \file ui2-term.h
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

#ifndef UI2_TERM_H
#define UI2_TERM_H

#include "h-basic.h"
#include "z-bitflag.h"
#include "ui2-event.h"

#define TERM_STACK_MAX \
	128

/* Use entire string in Term functions
 * that take length as a parameter */
#define TERM_MAX_LEN (-1)

typedef struct term *term;

enum term_point_flags {
	TPF_NONE,
	TPF_DUMMY,
	TPF_MAX
};

#define TPF_SIZE                FLAG_SIZE(TPF_MAX)
#define tpf_has(f, flag)        flag_has_dbg(f, TPF_SIZE, flag, #f, #flag)
#define tpf_next(f, flag)       flag_next(f, TPF_SIZE, flag)
#define tpf_is_empty(f)         flag_is_empty(f, TPF_SIZE)
#define tpf_is_full(f)          flag_is_full(f, TPF_SIZE)
#define tpf_is_inter(f1, f2)    flag_is_inter(f1, f2, TPF_SIZE)
#define tpf_is_subset(f1, f2)   flag_is_subset(f1, f2, TPF_SIZE)
#define tpf_is_equal(f1, f2)    flag_is_equal(f1, f2, TPF_SIZE)
#define tpf_on(f, flag)         flag_on_dbg(f, TPF_SIZE, flag, #f, #flag)
#define tpf_off(f, flag)        flag_off(f, TPF_SIZE, flag)
#define tpf_wipe(f)             flag_wipe(f, TPF_SIZE)
#define tpf_setall(f)           flag_setall(f, TPF_SIZE)
#define tpf_negate(f)           flag_negate(f, TPF_SIZE)
#define tpf_copy(f1, f2)        flag_copy(f1, f2, TPF_SIZE)
#define tpf_union(f1, f2)       flag_union(f1, f2, TPF_SIZE)
#define tpf_inter(f1, f2)       flag_inter(f1, f2, TPF_SIZE)
#define tpf_diff(f1, f2)        flag_diff(f1, f2, TPF_SIZE)

struct term_point {
	/* foreground character and attribute */
	uint32_t fg_attr;
	wchar_t fg_char;

	/* background character and attribute */
	uint32_t bg_attr;
	wchar_t bg_char;

	/* terrain attribute */
	uint32_t terrain_attr;

	/* point has at least one flag */
	bool has_flags;
	bitflag flags[TPF_SIZE];
};

/* forward declaration */
struct term_create_info;

/* on push_new_hook and pop_new_hook
 * push_new_hook is called when a new, temporary term is about to be
 * pushed on the stack (with the call Term_push_new())
 * the frontend must supply the desirable parameters for creation in term_create_info
 * (taking term_hints into account as it sees fit; the only thing guaranteed is that
 * the new term's width and height are no smaller than those specified in term_hints)
 * and must be ready to draw on this new term
 * pop_new_hook is called when the temporary term is about to be popped
 * from the stack; the frontend must perform all necessary cleanup
 *
 * note that the frontend shouldn't create or destroy temporary terms;
 * that will be done without automatically */

enum term_position {
	TERM_POSITION_NONE,
	TERM_POSITION_EXACT,
	TERM_POSITION_CENTER,
	TERM_POSITION_TOP_LEFT,
};

enum term_purpose {
	TERM_PURPOSE_NONE,
	TERM_PURPOSE_INTRO,
	TERM_PURPOSE_BIRTH,
	TERM_PURPOSE_DEATH,
	TERM_PURPOSE_TEXT,
	TERM_PURPOSE_MENU,
	TERM_PURPOSE_BIG_MAP,
};

struct term_hints {
	int x;
	int y;
	int width;
	int height;
	bool tabs;
	enum term_position position;
	enum term_purpose purpose;
};

/* NOTE THAT ALL HOOKS ARE MANDATORY */

typedef void (*push_new_hook)(const struct term_hints *hints,
		struct term_create_info *info);
typedef void (*pop_new_hook)(void *user);

/* draw_hook should draw num_points points starting at position x, y */
typedef void (*draw_hook)(void *user,
		int x, int y, int num_points, struct term_point *points);
/* erase_hook should completely clear the term */
typedef void (*erase_hook)(void *user);
/* cursor_hook should draw a cursor at position x, y */
typedef void (*cursor_hook)(void *user, int x, int y);
/* redraw_hook should make sure that all that was drawn previously
 * actually appears on the screen; the parameter delay is the time
 * (in milliseconds) of pause that should be taken after redraw */
typedef void (*redraw_hook)(void *user, int delay);
/* event_hook should call Term_keypress() or Term_mousepress()
 * if there are corresponding events; if wait is true, the frontend
 * should wait for events (as long as necessary to get at least one) */
typedef void (*event_hook)(void *user, bool wait);
/* flush_events_hook should discard all pending events */
typedef void (*flush_events_hook)(void *user);
/* delay_hook should pause for specified number of milliseconds */
typedef void (*delay_hook)(void *user, int msecs);
/* make_visible_hook should make term visible or invisible
 * (if possible), depending on the argument */
typedef void (*make_visible_hook)(void *user, bool visible);
/* add_tab_hook should add a tab to the term and handle mouse clicks on it;
 * if the tab is clicked, Term_keypress() should be called, as if the user
 * pressed a key (on keyboard) with the corresponding keycode_t "code" */
typedef void (*add_tab_hook)(void *user,
		keycode_t code, const wchar_t *label, uint32_t fg_attr, uint32_t bg_attr);
/* move hook asks the frontend to move rectangular of the term over another part
 * (see Term_move_points()); if the hook returns true, the moved points wont be
 * updated (flush) as they're assumed to be moved; if the hook returns false,
 * the moved points will be flushed on the next call to Term_flush_output() */
typedef bool (*move_hook)(void *user,
		int dst_x, int dst_y, int src_x, int src_y, int width, int height);

struct term_callbacks {
	make_visible_hook make_visible;
	flush_events_hook flush_events;
	push_new_hook     push_new;
	pop_new_hook      pop_new;
	add_tab_hook      add_tab;
	cursor_hook       cursor;
	redraw_hook       redraw;
	erase_hook        erase;
	event_hook        event;
	delay_hook        delay;
	draw_hook         draw;
	move_hook         move;
};

struct term_create_info {
	int width;
	int height;
	void *user;
	struct term_callbacks callbacks;
	struct term_point blank;
};

term Term_create(const struct term_create_info *info);
void Term_destroy(term t);

void *Term_priv(term t);
void Term_setpriv(term t, void *user);

term Term_top(void);

void Term_push(term t);
void Term_push_new(struct term_hints *hints);
void Term_pop(void);
void Term_pop_all(void);

/* inform the term code of the existence of double tile;
 * it will be redrawn when any part of it is redrawn */
void Term_double_point(int ax, int ay, int bx, int by);

/* erase len characters starting at x, y;
 * this function does not move the cursor */
void Term_erase(int x, int y, int len);
/* erase all characters starting at x, y */
void Term_erase_line(int x, int y);
/* erase the whole window */
void Term_erase_all(void);

/* notes about the cursor
 * X (horizontal) legal positions are (0 <= X <= term width)
 * Y (vertical) legal positions are (0 <= Y < term height)
 * note that the cursor can be one point beyond the right
 * edge of the term (in which case nothing will be added);
 * all other cursor positions are FATAL ERRORS */

/* the following functions move the cursor;
 * fga - foreground attr (that is, color)
 * fgc - foreground char
 * bga - background attr (that is, color)
 * bgc - background char
 * x and y must be valid */

/* add a char at current cursor position */
void Term_putwc(uint32_t fga, wchar_t fgc);
/* as above, but with a null-terminated string of wchar_t */
void Term_putws(int len, uint32_t fga, const wchar_t *fgc);
/* as above, but with a (utf-8 encoded) string of bytes; len is length in codepoints
 * only the part of the string that fits in the term window will be used */
void Term_puts(int len, uint32_t fga, const char *fgc);
 /* Term_add* are like Term_put* functions, but allow to specify coordinates */
void Term_addwc(int x, int y, uint32_t fga, wchar_t fgc);
void Term_addws(int x, int y, int len, uint32_t fga, const wchar_t *fgc);
void Term_adds(int x, int y, int len, uint32_t fga, const char *fgc);

/* determine the position and visibility of the cursor
 * pass NULL for any argument that doesn't interest you */
void Term_get_cursor(int *x, int *y, bool *visible);
/* set the position of the cursor */
void Term_cursor_to_xy(int x, int y);
/* set visibility of the cursor (Term_put functions will
 * still move it as usual whether it is visible or not) */
void Term_cursor_visible(bool visible);

/* At a given location, determine the current point */
void Term_get_point(int x, int y, struct term_point *point);
/* set a point at x, y, and don't move the cursor;
 * this is the most efficient way to add a point to a term */
void Term_set_point(int x, int y, struct term_point point);

/* Get a point suitable for initializing other points */
struct term_point Term_get_blank(void);

/* valid coordinates for point in term */
bool Term_point_ok(int x, int y);

/* determine the size of the top term */
void Term_get_size(int *w, int *h);
/* simplified interface to the above */
int Term_width(void);
int Term_height(void);
/* make the top term bigger or smaller */
void Term_resize(int w, int h);

/* this function is essentially memmove() for terms;
 * it operates on rectangular regions of points, described by
 * their top left corners; both regions have the same width and height;
 * regions can overlap; both regions must contain only valid coordinates;
 * note that term must be flushed before attempting to move any points! */
void Term_move_points(int dst_x, int dst_y, int src_x, int src_y,
		int width, int height);

/* flush the output (it won't necessarily appear on the screen;
 * that depends on how th frontend does it; what will happen is
 * that draw_hook will be called) */
void Term_flush_output(void);

/* redraw the display; note that most monitors (at the time of writing this)
 * can only be redrawn 60 times a second; trying to redraw more can cause
 * flickering, tearing of frames, slowdowns and other undesirable things.
 * the parameter delay is time (in milliseconds) of pause after redraw */
void Term_redraw_screen(int delay);

/* append a keypress to ui_event queue */
void Term_keypress(keycode_t key, byte mods);
/* append a mousepress to ui_event queue; see
 * list-display_terms.h, which has indexes of all terms */
void Term_mousepress(int x, int y, int button, byte mods, int index);

/* get the first event from the event queue
 * returns false if there are no events */
bool Term_take_event(ui_event *event);
/* as above, but waits as long as necessary for an event */
bool Term_wait_event(ui_event *event);
/* check if there is at least one event in the queue;
 * the event will be copied to the first argument, but not removed from
 * the queue; if you don't want the event, supply NULL as the argument */
bool Term_check_event(ui_event *event);
/* forget all queued events (make the queue empty */
void Term_flush_events(void);
/* prepend an event to the front of the event_queue,
 * or append to the end of the queue;
 * not enough space in the queue is a FATAL ERROR */
void Term_prepend_events(const ui_event *events, size_t num_events);
void Term_append_events(const ui_event *events, size_t num_events);

/* make a term visible or invisible */
void Term_visible(bool visible);

/* Add a tab to the term on top of the stack; clicking
 * on this tab is equivalent to pressing key "code" */
void Term_add_tab(keycode_t code,
		const char *label, uint32_t fg_attr, uint32_t bg_attr);

/* pause for some time (in milliseconds) */
void Term_delay(int msecs);

#endif /* UI2_TERM_H */
