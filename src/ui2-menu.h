/**
 * \file ui2-menu.h
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

#ifndef UI2_MENU_H
#define UI2_MENU_H

#include "z-type.h"
#include "z-bitflag.h"
#include "ui2-output.h"

/*** Constants ***/

/* Standard menu orderings */
extern const char lower_case[];  /* abc..z */
extern const char upper_case[];  /* ABC..Z */
extern const char all_letters[]; /* abc..zABC..Z */
extern const char all_digits[];  /* 0..9 */

/*
 * Together, these classes define the constant properties of
 * the various menu classes.  
 *
 * A menu consists of:
 *  - menu_iter, which describes how to handle the type of "list" that's
 *    being displayed as a menu
 *  - a menu_skin, which describes the layout of the menu on the screen.
 *  - various bits and bobs of other data (e.g. the actual list of entries)
 */
struct menu;

/*** Predefined menu "skins" ***/

/**
 * Types of predefined skins available.
 */
typedef enum {
	/*
	 * A simple list of actions with an associated name and id.
	 * Private data: an array of menu_action
	 */
	MN_ITER_ACTIONS = 1,

	/*
	 * A list of strings to be selected from - no associated actions.
	 * Private data: an array of const char *
	 */
	MN_ITER_STRINGS = 2
} menu_iter_id;

/**
 * Primitive menu item with bound action.
 */
typedef struct {
	uint32_t flags;
	char tag;
	const char *name;
	void (*action)(const char *title, int row);
} menu_action;

/**
 * Flags for menu_actions.
 */
enum {
	MN_ACT_GRAYED = 1, /* Row is displayed, but doesn't allow action */
};

/**
 * Underlying function set for displaying lists in a certain kind of way.
 */
typedef struct {
	/* Returns menu item tag (optional) */
	char (*get_tag)(struct menu *menu, int index);

	/* Validity checker (optional - all rows are assumed valid if not present) */
	bool (*valid_row)(struct menu *menu, int index);

	/* Displays a menu row */
	void (*display_row)(struct menu *menu,
			int index, bool cursor, struct loc loc, int width);

	/* Handle "positive" events (selections, command keys, stop keys) */
	bool (*row_handler)(struct menu *menu, const ui_event *event, int index);
} menu_iter;

/*** Menu skins ***/

/**
 * Identifiers for the kind of layout to use
 */
typedef enum {
	MN_SKIN_SCROLL  = 1, /**< Ordinary scrollable single-column list */
	MN_SKIN_OBJECT  = 2, /**< Special single-column list for object choice */
	MN_SKIN_COLUMNS = 3, /**< Multicolumn view */
} skin_id;

/**
 * Class functions for menu layout
 */
typedef struct {
	/* Determines the cursor index given a (mouse) location */
	int (*get_cursor)(struct loc loc, int count, int top, region reg);

	/* Displays the current list of visible menu items */
	void (*display_list)(struct menu *menu, int cursor, region reg);

	/* Process a direction (up, down, left, right, etc) */
	ui_event (*process_dir)(struct menu *menu, int dir);
} menu_skin;

/*** Base menu structure ***/

/**
 * Flags for menu appearance & behaviour
 */
enum {
	MN_INVALID,
	/* movement key and mouse browsing only */
	MN_NO_TAGS,
	/* tags work, but are not displayed */
	MN_PVT_TAGS,
	/* double tap (or keypress) for selection; single tap is cursor movement*/
	MN_DBL_TAP,
	/* no select events to be triggered*/
	MN_NO_ACTION,
	/* tags can be selected via an inscription*/
	MN_INSCRIP_TAGS,
	/* tag selections can be made regardless of the case of the key pressed.*/
	MN_CASELESS_TAGS,
	/* dont erase the contents of menu region before displaying anything */
	MN_DONT_CLEAR,
	/* dont display "-more-" in menu */
	MN_NO_MORE,

	MN_MAX
};

#define MNFLAG_SIZE                FLAG_SIZE(MN_MAX)

#define mnflag_has(f, flag)        flag_has_dbg(f, MNFLAG_SIZE, flag, #f, #flag)
#define mnflag_next(f, flag)       flag_next(f, MNFLAG_SIZE, flag)
#define mnflag_is_empty(f)         flag_is_empty(f, MNFLAG_SIZE)
#define mnflag_is_full(f)          flag_is_full(f, MNFLAG_SIZE)
#define mnflag_is_inter(f1, f2)    flag_is_inter(f1, f2, MNFLAG_SIZE)
#define mnflag_is_subset(f1, f2)   flag_is_subset(f1, f2, MNFLAG_SIZE)
#define mnflag_is_equal(f1, f2)    flag_is_equal(f1, f2, MNFLAG_SIZE)
#define mnflag_on(f, flag)         flag_on_dbg(f, MNFLAG_SIZE, flag, #f, #flag)
#define mnflag_off(f, flag)        flag_off(f, MNFLAG_SIZE, flag)
#define mnflag_wipe(f)             flag_wipe(f, MNFLAG_SIZE)
#define mnflag_setall(f)           flag_setall(f, MNFLAG_SIZE)
#define mnflag_negate(f)           flag_negate(f, MNFLAG_SIZE)
#define mnflag_copy(f1, f2)        flag_copy(f1, f2, MNFLAG_SIZE)
#define mnflag_union(f1, f2)       flag_union(f1, f2, MNFLAG_SIZE)
#define mnflag_inter(f1, f2)       flag_inter(f1, f2, MNFLAG_SIZE)
#define mnflag_diff(f1, f2)        flag_diff(f1, f2, MNFLAG_SIZE)

/* Base menu type */
struct menu {
	/*** Public variables ***/

	/* New code should consider using term tabs
	 * instead of header, title and prompt */
	const char *header;
	const char *title;
	const char *prompt;

	/* Keyboard shortcuts for menu selection
	 * (shouldn't overlap with cmd_keys) */
	const char *selections; 

	/* Menu selections corresponding to inscriptions */
	const char *inscriptions; 

	/* String of characters that when pressed, menu handler should be called
	 * (shouldn't overlap with selections or some items may be unselectable) */
	const char *command_keys;

	/* String of characters that when pressed, return an EVT_SWITCH
	 * (shouldn't overlap with selections or command keys)
	 * if stop handler returns false, the menu stops (exits) */
	const char *stop_keys;

	/* Auxiliary function that is called before displaying the rest of the menu */
	void (*browse_hook)(int cursor, void *menu_data, region reg);

	/* Flags specifying the behavior of this menu */
	bitflag flags[MNFLAG_SIZE];

	/*** Private variables ***/

	/* Stored boundary, set by menu_layout() */
	region boundary;

	int filter_count;       /* number of rows in current view */
	const int *filter_list; /* optional filter (view) of menu objects */

	int count;              /* number of rows in underlying data set */
	void *menu_data;        /* the data used to access rows. */

	const menu_skin *skin;  /* menu display style functions */
	const menu_iter *iter;  /* menu skin functions */

	/* State variables */
	int cursor;             /* Currently selected row */
	int top;                /* Position in list for partial display */
	region active;          /* Subregion actually active for selection */
	int cursor_x_offset;    /* Adjustment to the default position of the cursor on a line. */
};

/*** Menu API ***/

/**
 * Allocate and return a new, initialised, menu.
 */
struct menu *menu_new(skin_id, const menu_iter *iter);
struct menu *menu_new_action(menu_action *acts, size_t count);
void menu_free(struct menu *menu);

/**
 * Initialise a menu, using the skin and iter functions specified.
 */
void menu_init(struct menu *menu, skin_id skin, const menu_iter *iter);

/**
 * Find a menu iter given its id.
 */
const menu_iter *menu_find_iter(menu_iter_id iter_id);

/**
 * Set menu private data and the number of menu items.
 *
 * Menu private data is then available from inside menu callbacks
 * using menu_priv().
 */
void menu_setpriv(struct menu *menu, int count, void *data);

/**
 * Return menu private data, set with menu_setpriv().
 */
void *menu_priv(struct menu *menu);

/*
 * Set a filter on what items a menu can display.
 *
 * Use this if your menu private data has 100 items, but you want to choose
 * which ones of those to display at any given time, e.g. in an inventory menu.
 * object_list[] should be an array of indexes to display; count should be its length.
 */
void menu_set_filter(struct menu *menu, const int object_list[], int count);

/**
 * Remove any filters set on a menu by menu_set_filer().
 */
void menu_release_filter(struct menu *menu);

/**
 * Ready a menu for display in the region specified.
 */
void menu_layout(struct menu *menu, region reg);

/**
 * Ready a menu for display using the whole term area.
 */
void menu_layout_term(struct menu *menu);

/**
 * Display a menu.
 */
void menu_refresh(struct menu *menu);

/**
 * Cursor colors for different states.
 */
uint32_t menu_row_style(bool valid, bool selected);

/**
 * Run a menu.
 *
 * Event types that can be returned:
 *   EVT_ESCAPE: no selection; go back (by default)
 *   EVT_SELECT: menu->cursor is the selected menu item (by default)
 *   EVT_MOVE:   the cursor has moved
 *   EVT_KBRD:   unhandled keyboard events
 *   EVT_MOUSE:  unhandled mouse events  
 */
ui_event menu_select(struct menu *menu);

/* Interal menu stuff that knowledge menu needs because it
 * runs its parallel menus "manually" (see ui2-knowledge.c) */
void menu_handle_mouse(struct menu *menu,
		struct mouseclick mouse, ui_event *out);
void menu_handle_keypress(struct menu *menu,
		struct keypress key, ui_event *out);

/**
 * Allow adjustment of the cursor's default x offset.
 */
void menu_set_cursor_x_offset(struct menu *menu, int offset);

/*** Dynamic menu handling ***/

struct menu *menu_dynamic_new(void);
void menu_dynamic_add(struct menu *menu,
		const char *text, int value);
void menu_dynamic_add_valid(struct menu *menu,
		const char *text, int value, bool valid);
void menu_dynamic_add_label(struct menu *menu,
		const char *text, const char label, int value, char *label_list);
void menu_dynamic_add_label_valid(struct menu *menu,
		const char *text, const char label,
		int value, char *label_list, bool valid);
size_t menu_dynamic_longest_entry(struct menu *menu);
region menu_dynamic_calc_location(struct menu *menu);
int menu_dynamic_select(struct menu *menu);
void menu_dynamic_free(struct menu *menu);

#endif /* UI2_MENU_H */
