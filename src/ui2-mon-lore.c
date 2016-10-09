/**
 * \file ui2-mon-lore.c
 * \brief Monster memory UI
 *
 * Copyright (c) 1997-2007 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "mon-lore.h"
#include "ui2-mon-lore.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-term.h"
#include "z-textblock.h"

/**
 * Place a monster recall title into a textblock.
 *
 * If graphics are turned on, this appends the title with the
 * appropriate tile.
 *
 * \param tb is the textblock we are placing the title into.
 * \param race is the monster race we are describing.
 */
static void lore_title(textblock *tb, const struct monster_race *race)
{
	assert(tb != NULL);
	assert(race != NULL);

	wchar_t standard_char = race->d_char;
	uint32_t standard_attr = race->d_attr;

	const bool unique = rf_has(race->flags, RF_UNIQUE);

	if (unique && OPT(purple_uniques)) {
		standard_attr = COLOUR_VIOLET;
	}
	if (!unique) {
		textblock_append(tb, "The ");
	}

	textblock_append(tb, race->name);

	textblock_append(tb, " ('");
	textblock_append_pict(tb, standard_attr, standard_char);
	textblock_append(tb, "')");

	/* Tile info is in monster_x_attr[] and monster_x_char[] */
	if (standard_attr != monster_x_attr[race->ridx]
			|| standard_char != monster_x_char[race->ridx])
	{
		textblock_append(tb, " ('");
		textblock_append_pict(tb,
				monster_x_attr[race->ridx], monster_x_char[race->ridx]);
		textblock_append(tb, "')");
	}
}

/**
 * Place a full monster recall description (with title) into a textblock, with
 * or without spoilers.
 *
 * \param tb is the textblock we are placing the description into.
 * \param race is the monster race we are describing.
 * \param original_lore is the known information about the monster race.
 * \param spoilers indicates what information is used; "true" will display full
 *        information without subjective information and monster flavor,
 *        while "false" only shows what the player knows.
 */
void lore_description(textblock *tb, const struct monster_race *race,
		const struct monster_lore *original_lore, bool spoilers)
{
	assert(tb != NULL);
	assert(race != NULL);
	assert(original_lore != NULL);

	/* Create a copy of the monster memory that we can modify */
	struct monster_lore mutable_lore;
	struct monster_lore *lore = &mutable_lore;
	memcpy(lore, original_lore, sizeof(*lore));

	/* Determine the special attack colors */
	int melee_colors[RBE_MAX];
	int spell_colors[RSF_MAX];
	get_attack_colors(melee_colors, spell_colors);

	/* Now get the known monster flags */
	bitflag known_flags[RF_SIZE];
	monster_flags_known(race, lore, known_flags);

	if (OPT(cheat_know) || spoilers) {
		cheat_monster_lore(race, lore);
	}

	if (!spoilers) {
		/* Show monster name and char (and tile) */
		lore_title(tb, race);
		textblock_append(tb, "\n");

		/* Show kills of monster vs. player(s) */
		lore_append_kills(tb, race, lore, known_flags);
	}

	/* If we are generating spoilers, we want to output as UTF-8. As of 3.5,
	 * the values in race->name and race->text remain unconverted from the
	 * UTF-8 edit files. */
	lore_append_flavor(tb, race, spoilers);

	/* Describe the monster type, speed, life, and armor */
	lore_append_movement(tb, race, lore, known_flags);

	if (!spoilers) {
		/* Describe the monster AC, HP, and hit chance */
		lore_append_toughness(tb, race, lore, known_flags);
		/* Describe the experience awarded for killing it */
		lore_append_exp(tb, race, lore, known_flags);
	}

	lore_append_drop(tb, race, lore, known_flags);

	/* Describe the special properties of the monster */
	lore_append_abilities(tb, race, lore, known_flags);
	lore_append_awareness(tb, race, lore, known_flags);
	lore_append_friends(tb, race, lore, known_flags);

	/* Describe the spells, spell-like abilities and melee attacks */
	lore_append_spells(tb, race, lore, known_flags, spell_colors);
	lore_append_attack(tb, race, lore, known_flags, melee_colors);
	
	if (lore_is_fully_known(race)) {
		textblock_append(tb, "You know everything about this monster.");
	}
	
	if (rf_has(race->flags, RF_QUESTOR)) {
		/* Notice quest monsters (e.g., Sauron and Morgoth) */
		textblock_append(tb, "\nYou feel an intense desire to kill this monster...");
	}
}

/**
 * Display monster recall modally and wait for a keypress.
 *
 * \param race is the monster race we are describing.
 * \param lore is the known information about the monster race.
 */
void lore_show_interactive(const struct monster_race *race,
		const struct monster_lore *lore)
{
	assert(race != NULL);
	assert(lore != NULL);

	textblock *tb = textblock_new();
	lore_description(tb, race, lore, false);

	region reg = {0, 0, 0, 0};
	textui_textblock_show(tb, TERM_POSITION_TOP_LEFT, reg, NULL);

	textblock_free(tb);
}

/**
 * Display monster recall statically.
 *
 * This is intended to be called in a subwindow, since it clears the entire
 * window before drawing, and has no interactivity.
 *
 * \param race is the monster race we are describing.
 * \param lore is the known information about the monster race.
 */
void lore_show_subwindow(const struct monster_race *race,
		const struct monster_lore *lore)
{
	assert(race != NULL);
	assert(lore != NULL);

	Term_erase_all();

	textblock *tb = textblock_new();
	lore_description(tb, race, lore, false);

	region reg = {0, 0, 0, 0};
	textui_textblock_place(tb, reg, NULL);

	textblock_free(tb);
}
