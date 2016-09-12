/**
 * \file ui2-obj.h
 * \brief lists of objects and object pictures
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007-9 Andi Sidwell, Chris Carr, Ed Graham, Erik Osheim
 * Copyright (c) 2015 Nick McConnell
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

#ifndef UI2_OBJECT_H
#define UI2_OBJECT_H

#include "cmd-core.h"

/**
 * Modes for item lists in show_inven(), show_equip(), show_quiver() and
 * show_floor()
 */
enum olist_detail {
	OLIST_NONE           = 0,      /* No options */
	OLIST_WINDOW         = 1 << 0, /* Display list in a sub-term (as opposed to a menu) */
	OLIST_GOLD           = 1 << 1, /* Include gold in the list */
	OLIST_WEIGHT         = 1 << 2, /* Show item weight */
	OLIST_PRICE          = 1 << 3, /* Show item price */
	OLIST_FAIL           = 1 << 4, /* Show device failure */
	OLIST_DEATH          = 1 << 5, /* RIP screen */
	OLIST_SHOW_EMPTY     = 1 << 6, /* Show empty slots */
	OLIST_QUIVER_COMPACT = 1 << 7, /* Compact view of quiver (just missile count) */
	OLIST_QUIVER_FULL    = 1 << 8  /* Full quiver slots */
};

uint32_t object_kind_attr(const struct object_kind *kind);
wchar_t object_kind_char(const struct object_kind *kind);
uint32_t object_attr(const struct object *obj);
wchar_t object_char(const struct object *obj);
void show_inven(int mode, item_tester tester);
void show_equip(int mode, item_tester tester);
void show_quiver(int mode, item_tester tester);
void show_floor(struct object **floor_list, int floor_num,
		int mode, item_tester tester);
bool textui_get_item(struct object **choice,
		const char *prompt, const char *reject,
		cmd_code cmd, item_tester tester, int mode);
bool get_item_allow(const struct object *obj,
		unsigned char ch, cmd_code cmd, bool harmless);

void display_object_recall(struct object *obj);
void display_object_kind_recall(struct object_kind *kind);
void display_object_recall_interactive(struct object *obj);
void textui_obj_examine(void);
void textui_cmd_ignore_menu(struct object *obj);
void textui_cmd_ignore(void);
void textui_cmd_toggle_ignore(void);

#endif /* UI2_OBJECT_H */
