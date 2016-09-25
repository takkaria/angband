/**
 * \file ui2-keymap.h
 * \brief Keymap handling
 *
 * Copyright (c) 2011 Andi Sidwell
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

#ifndef UI2_KEYMAP_H
#define UI2_KEYMAP_H

#include "z-file.h"

/**
 * Maximum number of keypresses a trigger can map to.
 */
#define KEYMAP_ACTION_MAX 20

#define KEYMAP_MODE_OPT \
	(OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG)

/**
 * Keymap modes.
 */
enum {
	KEYMAP_MODE_ORIG = 0,
	KEYMAP_MODE_ROGUE,

	KEYMAP_MODE_MAX
};

/**
 * Given a keymap mode and a keypress, return any attached action.
 */
const struct keypress *keymap_find(int keymap, struct keypress key);

/**
 * Given a keymap mode, a trigger, and an action, store it in the keymap list.
 */
void keymap_add(int keymap, struct keypress trigger,
		const struct keypress *actions, bool user);

/**
 * Given a keypress, remove any keymap that would trigger on that key.
 */
bool keymap_remove(int keymap, struct keypress trigger);

/**
 * Free all keymaps.
 */
void keymap_free(void);

/**
 * Save keymaps to the specified file.
 */
void keymap_dump(ang_file *fff);

#endif /* UI2_KEYMAP_H */
