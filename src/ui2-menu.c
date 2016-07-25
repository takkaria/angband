/**
 * \file ui2-menu.c
 * \brief Generic menu interaction functions
 *
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
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
#include "ui2-target.h"
#include "ui2-event.h"
#include "ui2-input.h"
#include "ui2-menu.h"

/**
 * Cursor colours
 */
static const uint32_t curs_attrs[2][2] = {
	{ COLOUR_SLATE, COLOUR_BLUE },      /* Greyed row */
	{ COLOUR_WHITE, COLOUR_L_BLUE }     /* Valid row */
};

/**
 * Some useful constants
 */
const char lower_case[] = "abcdefghijklmnopqrstuvwxyz";
const char upper_case[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char all_letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

/**
 * Forward declarations
 */
static void display_menu_row(struct menu *menu,
		int index, bool cursor, struct loc loc, int width);
static bool is_valid_row(struct menu *menu, int cursor);
static bool has_valid_row(struct menu *menu, int count);

/*
 * Helper function for accessing curs_attrs[]
 */
uint32_t menu_row_style(bool valid, bool selected)
{
	return curs_attrs[valid ? 1 : 0][selected ? 1 : 0];
}

/**
 * Display an event, with possible preference overrides
 */
static void display_action_aux(menu_action *act,
		uint32_t color, struct loc loc, int width)
{
	Term_erase(loc.x, loc.y, width);

	if (act->name) {
		Term_adds(loc.x, loc.y, width, color, act->name);
	}
}

/*
 * Helper functions for managing menu's filter list
 */
static int menu_index(const struct menu *menu, int index)
{
	assert(index > 0);

	if (menu->filter_list) {
		assert(index < menu->filter_count);
		return menu->filter_list[index];
	} else {
		assert(index < menu->count);
		return index;
	}
}

static int menu_count(const struct menu *menu)
{
	if (menu->filter_list) {
		return menu->filter_count;
	} else {
		return menu->count;
	}
}

/* ------------------------------------------------------------------------
 * MN_ACTIONS HELPER FUNCTIONS
 *
 * MN_ACTIONS is the type of menu iterator that displays a simple list of
 * menu_actions.
 * ------------------------------------------------------------------------ */

static char menu_action_tag(struct menu *menu, int index)
{
	menu_action *acts = menu_priv(menu);

	return acts[index].tag;
}

static bool menu_action_valid(struct menu *menu, int index)
{
	menu_action *acts = menu_priv(menu);

	if (acts[index].flags & MN_ACT_HIDDEN
			|| acts[index].name == NULL)
	{
		return false;
	} else {
		return true;
	}
}

static void menu_action_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	menu_action *acts = menu_priv(menu);

	bool valid = (acts[index].flags & MN_ACT_GRAYED) ? false : true;

	display_action_aux(&acts[index], menu_row_style(valid, cursor), loc, width);
}

static bool menu_action_handle(struct menu *menu, const ui_event *event, int index)
{
	menu_action *acts = menu_priv(menu);

	if (event->type != EVT_SELECT
			|| (acts[index].flags & MN_ACT_GRAYED)
			|| acts[index].action == NULL)
	{
		return false;
	}

	acts[index].action(acts[index].name, index);

	return true;
}

/**
 * Virtual function table for action_events
 */
static const menu_iter menu_iter_actions = {
	.get_tag     = menu_action_tag,
	.valid_row   = menu_action_valid,
	.display_row = menu_action_display,
	.row_handler = menu_action_handle
};

/* ------------------------------------------------------------------------
 * MN_STRINGS HELPER FUNCTIONS
 *
 * MN_STRINGS is the type of menu iterator that displays a simple list of 
 * strings - no action is associated, as selection will just return the index.
 * ------------------------------------------------------------------------ */
static void display_string(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	const char **items = menu_priv(menu);

	Term_adds(loc.x, loc.y, width, menu_row_style(true, cursor), items[index]);
}

/* Virtual function table for displaying arrays of strings */
static const menu_iter menu_iter_strings = { 
	.display_row = display_string,
};

/* ================== SKINS ============== */

/**
 * Find the position of a cursor given a subwindow address
 */
static int generic_skin_get_cursor(struct loc loc, int count, int top, region reg)
{
	int cursor = loc.y - reg.y + top;
	if (cursor >= count) {
		cursor = count - 1;
	}

	return cursor;
}

/**
 * Display current view of a skin
 */
static void generic_skin_display(struct menu *menu, int cursor, region reg)
{
	assert(cursor >= 0);

	int count = menu_count(menu);

	/* Keep a certain distance from the top when possible */
	if (cursor <= menu->top && menu->top > 0) {
		menu->top = cursor - 1;
	}

	/* Keep a certain distance from the bottom when possible */
	if (cursor >= menu->top + (reg.h - 1)) {
		menu->top = cursor - (reg.h - 1) + 1;
	}

	/* Limit the top to legal places */
	menu->top = MAX(0, MIN(menu->top, count - reg.h));

	/* Position of cursor relative to top */
	int rel_cursor = cursor - menu->top;

	for (int i = 0; i < reg.h; i++) {
		/* Blank all lines */
		Term_erase(reg.x, reg.y + i, reg.w);
		if (i < count) {
			/* Redraw the line if it's within the number of menu items */
			bool is_curs = (i == rel_cursor);
			display_menu_row(menu,
					menu->top + i, is_curs, loc(reg.x, reg.y + i), reg.w);
		}
	}

	if (menu->cursor >= 0) {
		Term_cursor_to_xy(reg.x + menu->cursor_x_offset, reg.y + rel_cursor);
	}
}

/*** Scrolling menu skin ***/

static ui_event scroll_skin_process_direction(struct menu *menu, int dir)
{
	ui_event out = EVENT_EMPTY;

	/* Reject diagonals */
	if (ddx[dir] && ddy[dir]) {
		return out;
	}

	if (ddx[dir]) {
		out.type = ddx[dir] < 0 ? EVT_ESCAPE : EVT_SELECT;
	} else if (ddy[dir]) {
		menu->cursor += ddy[dir];
		out.type = EVT_MOVE;
	}

	return out;
}

/**
 * Virtual function table for scrollable menu skin
 */
static const menu_skin menu_skin_scroll = {
	.get_cursor   = generic_skin_get_cursor,
	.display_list = generic_skin_display,
	.process_dir  = scroll_skin_process_direction
};

/*** Object menu skin ***/

static ui_event object_skin_process_direction(struct menu *menu, int dir)
{
	ui_event out = EVENT_EMPTY;

	/* Reject diagonals */
	if (ddx[dir] && ddy[dir]) {
		return out;
	}

	if (ddx[dir]) {
		out.type = EVT_SWITCH;
		out.key.code = ddx[dir] < 0 ? ARROW_LEFT : ARROW_RIGHT;
	} else if (ddy[dir]) {
		menu->cursor += ddy[dir];
		out.type = EVT_MOVE;
	}

	return out;
}

/**
 * Virtual function table for object menu skin
 */
static const menu_skin menu_skin_object = {
	.get_cursor   = generic_skin_get_cursor,
	.display_list = generic_skin_display,
	.process_dir  = object_skin_process_direction
};

/*** Multi-column menus ***/

static int column_skin_get_cursor(struct loc loc, int count, int top, region reg)
{
	(void) top;

	int cols = (count + reg.h - 1) / reg.h;
	int colw = 23;

	if (colw * cols > reg.w) {
		colw = reg.w / cols;
	}

	assert(colw > 0);

	int cursor = (loc.y - reg.y) + reg.h * ((loc.x - reg.x) / colw);

	if (cursor > count - 1) {
		cursor = count - 1;
	}

	assert(cursor >= 0);

	return cursor;
}

static void column_skin_display(struct menu *menu, int cursor, region reg)
{
	int count = menu_count(menu);
	int cols = (count + reg.h - 1) / reg.h;
	int colw = 23;

	if (colw * cols > reg.w) {
		colw = reg.w / cols;
	}

	assert(colw > 0);

	for (int c = 0; c < cols; c++) {
		for (int r = 0; r < reg.h; r++) {
			int index = c * reg.h + r;

			if (index < count) {
				bool is_cursor = (index == cursor);
				struct loc loc = {
					.x = reg.x + c * colw,
					.y = reg.y + r
				};

				display_menu_row(menu, index, is_cursor, loc, colw);
			}
		}
	}

	if (menu->cursor >= 0) {
		int x = reg.x + (cursor / reg.h) * colw;
		int y = reg.y + (cursor % reg.h);

		Term_cursor_to_xy(x + menu->cursor_x_offset, y);
	}
}

static ui_event column_skin_process_direction(struct menu *menu, int dir)
{
	int count = menu_count(menu);
	int height = menu->active.h;

	int cols = (count + height - 1) / height;

	if (ddx[dir]) {
		menu->cursor += ddx[dir] * height;
	}
	if (ddy[dir]) {
		menu->cursor += ddy[dir];
	}

	/* Adjust to the correct locations (roughly) */
	if (menu->cursor < 0) {
		menu->cursor = (height * cols) + menu->cursor;
	}
	if (menu->cursor > count) {
		menu->cursor = menu->cursor % height;
	}

	ui_event out = {
		.type = EVT_MOVE
	};

	return out;
}

/* Virtual function table for multi-column menu skin */
static const menu_skin menu_skin_columns = {
	.get_cursor   = column_skin_get_cursor,
	.display_list = column_skin_display,
	.process_dir  = column_skin_process_direction
};

/* ================== GENERIC HELPER FUNCTIONS ============== */

static bool is_valid_row(struct menu *menu, int index)
{
	if (index < 0 || index >= menu_count(menu)) {
		return false;
	}

	if (menu->iter->valid_row) {
		return menu->iter->valid_row(menu, menu_index(menu, index));
	} else {
		return true;
	}
}

static bool has_valid_row(struct menu *menu, int count)
{
	for (int i = 0; i < count; i++) {
		if (is_valid_row(menu, i)) {
			return true;
		}
	}

	return false;
}

static char code_from_key(const struct menu *menu,
		struct keypress key, bool caseless)
{
	char code = (char) key.code;

	if (mnflag_has(menu->flags, MN_INSCRIP_TAGS)
			&& isdigit((unsigned char) key.code)
			&& menu->inscriptions[D2I(key.code)])
	{
		code = menu->inscriptions[D2I(key.code)];
	} else if (caseless) {
		code = toupper((unsigned char) key.code);
	}

	return code;
}

static bool tag_eq_code(char tag, char code, bool caseless)
{
	if (caseless) {
		tag = toupper((unsigned char) tag);
	}

	return tag != 0 && tag == code;
}

/* 
 * Return a new position in the menu based on the key
 * pressed and the flags and various handler functions.
 */
static int get_cursor_key(struct menu *menu, struct keypress key)
{
	const int count = menu_count(menu);
	const bool caseless = mnflag_has(menu->flags, MN_CASELESS_TAGS);
	const char code = code_from_key(menu, key, caseless);

	if (mnflag_has(menu->flags, MN_NO_TAGS)) {
		return -1;
	} else if (menu->selections && !mnflag_has(menu->flags, MN_PVT_TAGS)) {
		for (int i = 0; menu->selections[i]; i++) {
			if (tag_eq_code(menu->selections[i], code, caseless)) {
				return i;
			}
		}
	} else if (menu->iter->get_tag) {
		for (int i = 0; i < count; i++) {
			char tag = menu->iter->get_tag(menu, menu_index(menu, i));
			if (tag_eq_code(tag, code, caseless)) {
				return i;
			}
		}
	}

	return -1;
}

/**
 * Modal display of menu
 */
static void display_menu_row(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	index = menu_index(menu, index);

	if (menu->iter->valid_row != NULL
			&& !menu->iter->valid_row(menu, index))
	{
		return;
	}

	if (!mnflag_has(menu->flags, MN_NO_TAGS)) {
		char sel = 0;

		if (menu->selections && !mnflag_has(menu->flags, MN_PVT_TAGS)) {
			sel = menu->selections[index];
		} else if (menu->iter->get_tag) {
			sel = menu->iter->get_tag(menu, index);
		}

		if (sel != 0) {
			Term_adds(loc.x, loc.y, 3,
					menu_row_style(true, cursor), format("%c) ", sel));
			loc.x += 3;
			width -= 3;
		}
	}

	menu->iter->display_row(menu, index, cursor, loc, width);
}

void menu_refresh(struct menu *menu)
{
	if (menu->browse_hook) {
		menu->browse_hook(menu_index(menu, menu->cursor),
				menu->menu_data, menu->active);
	}

	if (menu->title) {
		Term_adds(menu->boundary.x, menu->boundary.y, menu->boundary.w,
				COLOUR_WHITE, menu->title);
	}

	if (menu->header) {
		/* Above the menu */
		Term_adds(menu->active.x, menu->active.y - 1, menu->active.w,
				COLOUR_WHITE, menu->header);
	}

	if (menu->prompt) {
		/* Below the menu */
		int y = menu->active.y + menu->active.h;
		Term_adds(menu->boundary.x, y, menu->boundary.w,
				COLOUR_WHITE, menu->prompt);
	}

	menu->skin->display_list(menu, menu->cursor, menu->active);

	Term_flush_output();
}

/*** MENU RUNNING AND INPUT HANDLING CODE ***/

/**
 * Handle mouse input in a menu.
 * 
 * Mouse output is either moving, selecting, escaping, or nothing.  Returns
 * true if something changes as a result of the click.
 */
void menu_handle_mouse(struct menu *menu,
		struct mouseclick mouse, ui_event *out)
{
	if (mouse.button == MOUSE_BUTTON_RIGHT) {
		out->type = EVT_ESCAPE;
	} else if (!region_inside(&menu->active, &mouse)) {
		/* A click to the left of the active region is 'back' */
		if (mouse.x < menu->active.x) {
			out->type = EVT_ESCAPE;
		}
	} else {
		int new_cursor = menu->skin->get_cursor(loc(mouse.x, mouse.y),
				menu_count(menu), menu->top, menu->active);
	
		if (is_valid_row(menu, new_cursor)) {
			if (!mnflag_has(menu->flags, MN_DBL_TAP)
					|| new_cursor == menu->cursor)
			{
				out->type = EVT_SELECT;
			} else {
				out->type = EVT_MOVE;
			}
			menu->cursor = new_cursor;
		}
	}
}

/**
 * Handle any menu command keys
 */
static bool menu_handle_action(struct menu *menu, const ui_event *in)
{
	if (menu->iter->row_handler) {
		int index = menu_index(menu, menu->cursor);
		return menu->iter->row_handler(menu, in, index);
	}

	return false;
}
/**
 * Handle navigation keypresses.
 *
 * Returns true if they key was intelligible as navigation, regardless of
 * whether any action was taken.
 */
void menu_handle_keypress(struct menu *menu,
		struct keypress key, ui_event *out)
{
	const int count = menu_count(menu);
	if (count <= 0) {
		return;
	}

	/* Get the new cursor position from the menu item tags */
	int new_cursor = get_cursor_key(menu, key);
	if (is_valid_row(menu, new_cursor)) {
		if (!mnflag_has(menu->flags, MN_DBL_TAP)
				|| new_cursor == menu->cursor)
		{
			out->type = EVT_SELECT;
		} else {
			out->type = EVT_MOVE;
		}
		menu->cursor = new_cursor;

	} else if (key.code == ESCAPE) {
		/* Escape stops us here */
		out->type = EVT_ESCAPE;
	} else if (key.code == ' ') {
		if (menu->active.h < count) {
			/* Go to start of next page */
			menu->cursor += menu->active.h;
			if (menu->cursor >= count - 1) {
				menu->cursor = 0;
			}
			menu->top = menu->cursor;

			out->type = EVT_MOVE;
		}
	} else if (key.code == KC_ENTER) {
		out->type = EVT_SELECT;
	} else {
		/* Try directional movement */
		int dir = target_dir(key);

		if (dir != 0 && has_valid_row(menu, count)) {
			*out = menu->skin->process_dir(menu, dir);

			if (out->type == EVT_MOVE) {
				while (!is_valid_row(menu, menu->cursor)) {
					/* Loop around */
					if (menu->cursor > count - 1) {
						menu->cursor = 0;
					} else if (menu->cursor < 0) {
						menu->cursor = count - 1;
					} else {
						menu->cursor += ddy[dir];
					}
				}
			
				assert(menu->cursor >= 0);
				assert(menu->cursor < count);
			}
		}
	}
}

/**
 * Run a menu.
 *
 * If clear is true, the term is cleared before the menu is drawn
 */
ui_event menu_select(struct menu *menu)
{
	assert(menu->active.w != 0);
	assert(menu->active.h != 0);

	const bool action_ok = mnflag_has(menu->flags, MN_NO_ACTION) ? false : true;
	const int stop_flags = (EVT_SELECT | EVT_ESCAPE | EVT_SWITCH);

	ui_event in = EVENT_EMPTY;
	/* Stop on first unhandled event */
	while ((in.type & stop_flags) == 0) {
		ui_event out = EVENT_EMPTY;

		menu_refresh(menu);
		in = inkey_simple();

		/* Handle mouse and keyboard commands */
		if (in.type == EVT_MOUSE) {
			if (action_ok && menu_handle_action(menu, &in)) {
				continue;
			}

			menu_handle_mouse(menu, in.mouse, &out);
		} else if (in.type == EVT_KBRD) {
			if (action_ok) {
				if (menu->command_keys
						&& strchr(menu->command_keys, (char) in.key.code))
				{
					/* Command key */
					if (menu_handle_action(menu, &in)) {
						continue;
					}
				}
				if (menu->stop_keys
						&& strchr(menu->stop_keys, (char) in.key.code))
				{
					/* Stop key */
					if (menu_handle_action(menu, &in)) {
						continue;
					} else {
						break;
					}
				}
			}

			menu_handle_keypress(menu, in.key, &out);
		}

		/* If we've selected something, then try to handle that */
		if (out.type == EVT_SELECT
				&& action_ok
				&& menu_handle_action(menu, &out))
		{
			continue;
		}

		/* Notify about the outgoing type */
		in = out;
	}

	return in;
}

/* ================== MENU ACCESSORS ================ */

/**
 * Return the menu iter struct for a given iter ID.
 */
const menu_iter *menu_find_iter(menu_iter_id id)
{
	switch (id) {
		case MN_ITER_ACTIONS:
			return &menu_iter_actions;

		case MN_ITER_STRINGS:
			return &menu_iter_strings;

		default:
			return NULL;
	}
}

/*
 * Return the skin behaviour struct for a given skin ID.
 */
static const menu_skin *menu_find_skin(skin_id id)
{
	switch (id) {
		case MN_SKIN_SCROLL:
			return &menu_skin_scroll;

		case MN_SKIN_OBJECT:
			return &menu_skin_object;

		case MN_SKIN_COLUMNS:
			return &menu_skin_columns;

		default:
			return NULL;
	}
}

void menu_set_filter(struct menu *menu, const int *filter_list, int count)
{
	menu->filter_list = filter_list;
	menu->filter_count = count;

	menu_ensure_cursor_valid(menu);
}

void menu_release_filter(struct menu *menu)
{
	menu->filter_list = NULL;
	menu->filter_count = 0;

	menu_ensure_cursor_valid(menu);
}

void menu_ensure_cursor_valid(struct menu *menu)
{
	int count = menu_count(menu);

	for (int row = menu->cursor; row < count; row++) {
		if (is_valid_row(menu, row)) {
			menu->cursor = row;
			return;
		}
	}

	/* If we've run off the end, without finding a valid row, put cursor
	 * on the last row */
	menu->cursor = count - 1;
}

/* ======================== MENU INITIALIZATION ==================== */

void menu_layout(struct menu *menu, region reg)
{
	menu->boundary = region_calculate(reg);
	menu->active = menu->boundary;

	if (menu->title) {
		/* Shrink menu, move it down and to the right */
		menu->active.y += 2;
		menu->active.h -= 2;
		menu->active.x += 4;
	}

	if (menu->header) {
		/* Header is right above menu */
		menu->active.y++;
		menu->active.h--;
	}

	if (menu->prompt) {
		/* Prompt is normally right below the menu */
		if (menu->active.h > 1) {
			menu->active.h--;
		} else {
			int offset = strlen(menu->prompt) + 1;
			menu->active.x += offset;
			menu->active.w -= offset;
		}
	}

	assert(menu->active.w > 0);
	assert(menu->active.h > 0);
}

void menu_layout_term(struct menu *menu)
{
	region full = {0};

	/* Make menu as big as the whole term */
	menu_layout(menu, full);
}

void menu_setpriv(struct menu *menu, int count, void *data)
{
	menu->count = count;
	menu->menu_data = data;

	menu_ensure_cursor_valid(menu);
}

void *menu_priv(struct menu *menu)
{
	return menu->menu_data;
}

void menu_init(struct menu *menu, skin_id skin_id, const menu_iter *iter)
{
	const menu_skin *skin = menu_find_skin(skin_id);
	assert(skin != NULL);
	assert(iter != NULL);

	/* Wipe the struct */
	memset(menu, 0, sizeof(*menu));

	/* Pedantry */
	menu->header       = NULL;
	menu->title        = NULL;
	menu->prompt       = NULL;
	menu->selections   = NULL;
	menu->inscriptions = NULL;
	menu->command_keys = NULL;
	menu->stop_keys    = NULL;
	menu->browse_hook  = NULL;
	menu->filter_list  = NULL;

	/* Menu-specific initialisation */
	menu->skin = skin;
	menu->iter = iter;
}

struct menu *menu_new(skin_id skin_id, const menu_iter *iter)
{
	struct menu *menu = mem_alloc(sizeof(*menu));
	menu_init(menu, skin_id, iter);

	return menu;
}

struct menu *menu_new_action(menu_action *acts, size_t count)
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, menu_find_iter(MN_ITER_ACTIONS));
	menu_setpriv(menu, count, acts);

	return menu;
}

void menu_free(struct menu *menu)
{
	mem_free(menu);
}

void menu_set_cursor_x_offset(struct menu *menu, int offset)
{
	/* This value is used in the menu skin's display_list() function. */
	menu->cursor_x_offset = offset;
}

/*** Dynamic menu handling ***/
struct menu_entry {
	char *text;
	int value;
	bool valid;

	struct menu_entry *next;
};

static bool dynamic_valid(struct menu *menu, int index)
{
	struct menu_entry *entry;

	for (entry = menu_priv(menu); index; index--) {
		entry = entry->next;
		assert(entry);
	}

	return entry->valid;
}

static void dynamic_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	struct menu_entry *entry;
	uint32_t color = menu_row_style(true, cursor);

	for (entry = menu_priv(menu); index; index--) {
		entry = entry->next;
		assert(entry);
	}

	Term_adds(loc.x, loc.y, width, color, entry->text);
}

static const menu_iter dynamic_iter = {
	.valid_row = dynamic_valid,
	.display_row = dynamic_display
};

struct menu *menu_dynamic_new(void)
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, &dynamic_iter);
	menu_setpriv(menu, 0, NULL);

	return menu;
}

void menu_dynamic_add_valid(struct menu *menu,
		const char *text, int value, bool valid)
{
	struct menu_entry *head = menu_priv(menu);
	struct menu_entry *new = mem_zalloc(sizeof(*new));

	assert(menu->iter == &dynamic_iter);

	new->text = string_make(text);
	new->value = value;
	new->valid = valid;

	if (head) {
		struct menu_entry *tail = head;
		while (true) {
			if (tail->next) {
				tail = tail->next;
			} else {
				break;
			}
		}

		tail->next = new;
		menu_setpriv(menu, menu->count + 1, head);
	} else {
		menu_setpriv(menu, menu->count + 1, new);
	}
}

void menu_dynamic_add(struct menu *menu, const char *text, int value)
{
	menu_dynamic_add_valid(menu, text, value, true);
}

void menu_dynamic_add_label_valid(struct menu *menu,
		const char *text, const char label, int value, char *label_list, bool valid)
{
	if (label && menu->selections && (menu->selections == label_list)) {
		label_list[menu->count] = label;
	}
	menu_dynamic_add_valid(menu, text, value, valid);
}

void menu_dynamic_add_label(struct menu *menu,
		const char *text, const char label, int value, char *label_list)
{
	menu_dynamic_add_label_valid(menu, text, label, value, label_list, true);
}

size_t menu_dynamic_longest_entry(struct menu *menu)
{
	size_t biggest = 0;

	for (struct menu_entry *entry = menu_priv(menu);
			entry != NULL;
			entry = entry->next)
	{
		size_t current = strlen(entry->text);
		if (current > biggest) {
			biggest = current;
		}
	}

	return biggest;
}

region menu_dynamic_calc_location(struct menu *menu)
{
	region reg = {
		.x = 0,
		.y = 0,
		.w = menu_dynamic_longest_entry(menu) + 3, /* 3 for tag */
		.h = menu->count
	};

	return region;
}

int menu_dynamic_select(struct menu *menu)
{
	ui_event e = menu_select(menu);

	if (e.type == EVT_ESCAPE) {
		return -1;
	}

	int cursor = menu->cursor;
	struct menu_entry *entry;

	for (entry = menu_priv(menu); cursor; cursor--) {
		entry = entry->next;
		assert(entry);
	}	

	return entry->value;
}

void menu_dynamic_free(struct menu *menu)
{
	struct menu_entry *entry = menu_priv(menu);
	while (entry) {
		struct menu_entry *next = entry->next;
		string_free(entry->text);
		mem_free(entry);
		entry = next;
	}
	mem_free(menu);
}
