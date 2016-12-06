/**
 * \file ui2-curse.c
 * \brief Curse selection menu
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2016 Nick McConnell
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
#include "init.h"
#include "obj-curse.h"
#include "obj-knowledge.h"
#include "ui2-menu.h"
#include "ui2-display.h"
#include "ui2-output.h"

struct curse_menu_data {
	int index;
	int power;
};

struct curses_list {
	struct curse_menu_data *curses;
	int selection;
	int count;
};

/**
 * Display an entry of the item menu
 */
void get_curse_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	struct curses_list *list = menu_priv(menu);

	Term_adds(loc.x, loc.y, TERM_MAX_LEN,
			menu_row_style(true, cursor),
			curses[list->curses[index].index].name);

	char power[ANGBAND_TERM_STANDARD_WIDTH / 2];
	int power_len = strnfmt(power, sizeof(power),
			"(power %d)", list->curses[index].power);

	loc.x += width - power_len;
	Term_adds(loc.x, loc.y, power_len, COLOUR_WHITE, power);
}

/**
 * Deal with events on the get_item menu
 */
bool get_curse_action(struct menu *menu, const ui_event *event, int index)
{
	struct curses_list *list = menu_priv(menu);

	if (event->type == EVT_SELECT) {
		list->selection = list->curses[index].index;
	}

	return false;
}

static void curses_list_init(struct curses_list *list, const struct object *obj)
{
	assert(list != NULL);
	assert(obj != NULL);

	memset(list, 0, sizeof(*list));

	list->curses = NULL;

	for (int i = 1; i < z_info->curse_max; i++) {
		if (obj->curses[i].power > 0) {

			if (list->curses == NULL) {
				list->curses = mem_zalloc(z_info->curse_max * sizeof(list->curses));
			}

			list->curses[list->count].index = i;
			list->curses[list->count].power = obj->curses[i].power;
			list->count++;
		}
	}
}

static void curses_list_destroy(struct curses_list *list)
{
	mem_free(list->curses);
}

static void curse_menu_term_push(const struct curses_list *list)
{
	const char *tab = "Remove which curse?";

	size_t maxlen = 0;
	for (int c = 0; c < list->count; c++) {
		size_t len = strlen(curses[list->curses[c].index].name);
		maxlen = MAX(len, maxlen);
	}

	maxlen += 15; /* to append, for example " (power 99)" */

	struct term_hints hints = {
		/* add 3 to account for menu's tags */
		.width = MAX(maxlen, strlen(tab) + 1) + 3,
		.height = list->count,
		.tabs = true,
		.position = TERM_POSITION_TOP_LEFT,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, tab, COLOUR_WHITE, COLOUR_DARK);
}

static void curse_menu_term_pop(void)
{
	Term_pop();
}

/**
 * Display list of curses to choose from
 */
static int curse_menu(struct object *obj)
{
	menu_iter menu_f = {
		.display_row = get_curse_display, 
		.row_handler = get_curse_action
	};

	struct curses_list list;
	curses_list_init(&list, obj);

	if (list.count == 0) {
		curses_list_destroy(&list);
		return 0;
	}

	struct menu menu;
	menu_init(&menu, MN_SKIN_SCROLL, &menu_f);
	menu_setpriv(&menu, list.count, &list);
	menu.selections = all_letters;

	curse_menu_term_push(&list);
	menu_layout_term(&menu);

	menu_select(&menu);

	curse_menu_term_pop();
	curses_list_destroy(&list);

	return list.selection;
}

bool textui_get_curse(int *choice, struct object *obj)
{
	int curse = curse_menu(obj);

	if (curse != 0) {
		*choice = curse;
		return true;
	} else {
		return false;
	}
}
