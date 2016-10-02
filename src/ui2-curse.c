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
#include "obj-knowledge.h"
#include "object.h"
#include "ui2-menu.h"
#include "ui2-display.h"
#include "ui2-output.h"

static struct curse *selection;

/**
 * Display an entry of the item menu
 */
void get_curse_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct curse **choice = menu_priv(menu);

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf),
			"%s (power %d)", choice[index]->name, choice[index]->power);

	loc.y += index;
	c_put_str(menu_row_style(true, cursor), buf, loc);
}

/**
 * Deal with events on the get_item menu
 */
bool get_curse_action(struct menu *menu, const ui_event *event, int index)
{
	struct curse **choice = menu_priv(menu);

	if (event->type == EVT_SELECT) {
		selection = choice[index];
	}

	return false;
}

/**
 * Display list of curses to choose from
 */
struct curse *curse_menu(struct object *obj)
{
	menu_iter menu_f = {
		.display_row = get_curse_display, 
		.row_handler = get_curse_action
	};

	struct curse *curse = obj->curses;
	unsigned length;
	int count;

	/* Count and then list the curses */
	count = 0;
	while (curse) {
		count++;
		curse = curse->next;
	}
	if (count == 0) {
		return NULL;
	}

	struct curse **available = mem_zalloc(count * sizeof(*available));

	count = 0;
	length = 0;
	for (curse = obj->curses; curse != NULL; curse = curse->next) {
		available[count] = curse;
		length = MAX(length, strlen(curse->name));
		count++;
	}

	struct menu *menu = menu_new(MN_SKIN_SCROLL, &menu_f);

	/* Set up the menu */
	menu_setpriv(menu, count, available);
	menu->header = "Remove which curse?";

	/* Set up the item list variables */
	selection = NULL;

	struct term_hints hints = {
		.width = MAX(length + 1, strlen(menu->header)),
		.height = count + 1,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);

	menu_layout_term(menu);
	menu_select(menu);

	Term_pop();
	menu_free(menu);
	mem_free(available);

	return selection;
}

bool textui_get_curse(struct curse **choice, struct object *obj)
{
	struct curse *curse = curse_menu(obj);
	if (curse != NULL) {
		*choice = curse;
		return true;
	} else {
		return false;
	}
}
