/**
 * \file ui2-keymap.c
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
#include "angband.h"
#include "ui2-keymap.h"
#include "ui2-term.h"

/**
 * Keymap implementation.
 *
 * Keymaps are defined in pref files and map onto the internal game keyset,
 * which is roughly what you get if you have roguelike keys turned off.
 *
 * We store keymaps by pairing triggers with actions; the trigger is a single
 * keypress and the action is stored as a string of keypresses, terminated
 * with a keypress with type == EVT_NONE.
 *
 * XXX We should note when we read in keymaps that are "official game" keymaps
 * and ones which are user-defined.  Then we can avoid writing out official
 * game ones and messing up everyone's pref files with a load of junk.
 */

/**
 * Struct for a keymap.
 */
struct keymap {
	struct keypress key;
	struct keypress *actions;

	/* User-defined keymap */
	bool user;

	struct keymap *next;
};

/**
 * List of keymaps.
 */
static struct keymap *keymaps[KEYMAP_MODE_MAX];

/**
 * Find a keymap, given a keypress.
 */
const struct keypress *keymap_find(int mode, struct keypress key)
{
	assert(mode >= 0);
	assert(mode < KEYMAP_MODE_MAX);

	for (struct keymap *k = keymaps[mode]; k; k = k->next) {
		if (k->key.code == key.code && k->key.mods == key.mods) {
			return k->actions;
		}
	}

	return NULL;
}

/**
 * Duplicate a given keypress string and return the duplicate.
 */
static struct keypress *keymap_make(const struct keypress *actions)
{
	size_t n = 0;
	while (actions[n].type != EVT_NONE) {
		n++;
	}

	/* Make room for the terminator */
	n++;

	struct keypress *new = mem_zalloc(n * sizeof(*new));
	memcpy(new, actions, n * sizeof(*new));

	return new;
}

/**
 * Add a keymap to the mappings table.
 */
void keymap_add(int mode,
		struct keypress trigger, struct keypress *actions, bool user)
{
	assert(mode >= 0);
	assert(mode < KEYMAP_MODE_MAX);

	keymap_remove(mode, trigger);

	struct keymap *k = mem_zalloc(sizeof(*k));

	k->key = trigger;
	k->actions = keymap_make(actions);
	k->user = user;

	k->next = keymaps[mode];
	keymaps[mode] = k;
}

/**
 * Remove a keymap.  Return true if one was removed.
 */
bool keymap_remove(int mode, struct keypress trigger)
{
	assert(mode >= 0);
	assert(mode < KEYMAP_MODE_MAX);

	for (struct keymap *k = keymaps[mode], *prev = NULL; k; prev = k, k = k->next) {
		if (k->key.code == trigger.code && k->key.mods == trigger.mods) {
			if (prev) {
				prev->next = k->next;
			} else {
				keymaps[mode] = k->next;
			}

			mem_free(k->actions);
			mem_free(k);

			return true;
		}
	}

	return false;
}

/**
 * Forget and free all keymaps.
 */
void keymap_free(void)
{
	for (size_t i = 0; i < N_ELEMENTS(keymaps); i++) {
		struct keymap *k = keymaps[i];
		while (k) {
			struct keymap *next = k->next;
			mem_free(k->actions);
			mem_free(k);
			k = next;
		}
	}
}

/**
 * Append active keymaps to a given file.
 */
void keymap_dump(ang_file *file)
{
	const int mode = KEYMAP_MODE_OPT;

	for (struct keymap *k = keymaps[mode]; k; k = k->next) {
		if (k->user) {
			char buf[1024];

			/* Encode the action */
			keypress_to_text(buf, sizeof(buf), k->actions, false);
			file_putf(file, "keymap-act:%s\n", buf);

			/* Convert the key into a string */
			struct keypress key[2] = {k->key, KEYPRESS_NULL};
			keypress_to_text(buf, sizeof(buf), key, true);
			file_putf(file, "keymap-input:%d:%s\n", mode, buf);

			file_putf(file, "\n");
		}
	}
}
