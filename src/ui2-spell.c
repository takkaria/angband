/**
 * \file ui2-spell.c
 * \brief Spell UI handing
 *
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
#include "cmds.h"
#include "cmd-core.h"
#include "game-input.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "ui2-menu.h"
#include "ui2-output.h"

/**
 * Spell menu data struct
 */
struct spell_menu_data {
	int *spells;
	int n_spells;

	bool browse;
	bool (*is_valid)(int spell_index);

	int selected_spell;
};

#define SPELL_MENU_WIDTH 60

#define SPELL_MENU_FIELDS \
	L"    Name                          Lv Mana Fail Info"
#define SPELL_MENU_SEPARATOR \
	L"============================================================"

/**
 * Is item index valid?
 */
static bool spell_menu_valid(struct menu *menu, int index)
{
	struct spell_menu_data *data = menu_priv(menu);

	return data->is_valid(data->spells[index]);
}

/**
 * Display a row of the spell menu
 */
static void spell_menu_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;
	(void) cursor;

	struct spell_menu_data *data = menu_priv(menu);

	int spell_index = data->spells[index];
	const struct class_spell *spell = spell_by_index(spell_index);

	if (spell->slevel >= 99) {
		c_prt(COLOUR_L_DARK, "(illegible)", loc);
		return;
	}

	uint32_t attr;
	const char *comment;

	if (player->spell_flags[spell_index] & PY_SPELL_FORGOTTEN) {
		comment = " forgotten";
		attr = COLOUR_YELLOW;
	} else if (player->spell_flags[spell_index] & PY_SPELL_LEARNED) {
		if (player->spell_flags[spell_index] & PY_SPELL_WORKED) {
			/* Get extra info */
			char buf[ANGBAND_TERM_STANDARD_WIDTH / 2];
			get_spell_info(spell_index, buf, sizeof(buf));
			comment = buf;
			attr = COLOUR_WHITE;
		} else {
			comment = " untried";
			attr = COLOUR_L_GREEN;
		}
	} else if (spell->slevel <= player->lev) {
		comment = " unknown";
		attr = COLOUR_L_DARK;
	} else {
		comment = " difficult";
		attr = COLOUR_RED;
	}

	char out[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(out, sizeof(out), "%-30s%2d %4d %3d%%%s",
			spell->name, spell->slevel, spell->smana, spell_chance(spell_index), comment);
	c_prt(attr, out, loc);
}

/**
 * Show spell long description when browsing
 */
static void show_spell_description(int index, void *data, const region reg)
{
	struct spell_menu_data *d = data;

	const struct class_spell *spell = spell_by_index(d->spells[index]);

	textblock *tb = textblock_new();
	textblock_append(tb, "%s", spell->text);

	region area = {0};
	textui_textblock_show(tb, area, spell->name);

	textblock_free(tb);
}

/**
 * Handle an event on a menu row.
 */
static bool spell_menu_handler(struct menu *menu, const ui_event *event, int index)
{
	struct spell_menu_data *data = menu_priv(menu);

	if (event->type == EVT_KBRD && event->key.code == '?') {
		show_spell_description(index, menu_priv(menu), menu->active);
		return true;
	} else if (data->browse) {
		return true;
	} else if (event->type == EVT_SELECT) {
		data->selected_spell = data->spells[index];
		return false;
	} else {
		return false;
	}
}

static void spell_menu_browser_hook(int cursor, void *data, region reg)
{
	(void) cursor;
	(void) data;
	(void) reg;

	Term_addws(0, 1, wcslen(SPELL_MENU_FIELDS), COLOUR_WHITE, SPELL_MENU_FIELDS);
	Term_addws(0, 2, wcslen(SPELL_MENU_SEPARATOR), COLOUR_L_DARK, SPELL_MENU_SEPARATOR);
}

static const menu_iter spell_menu_iter = {
	.valid_row   = spell_menu_valid,
	.display_row = spell_menu_display,
	.row_handler = spell_menu_handler
};

/**
 * Initialise a spell menu, given an object and a validity hook
 */
static bool spell_menu_init(struct menu *menu, struct spell_menu_data *data,
		const struct object *obj, bool (*is_valid)(int spell_index))
{
	/* Collect spells from object */
	data->n_spells = spell_collect_from_book(obj, &data->spells);
	if (data->n_spells == 0 || !spell_okay_list(is_valid, data->spells, data->n_spells)) {
		mem_free(data->spells);
		return false;
	}

	/* Copy across private data */
	data->selected_spell = -1;
	data->is_valid = is_valid;
	data->browse = false;

	menu_init(menu, MN_SKIN_SCROLL, &spell_menu_iter);
	menu_setpriv(menu, data->n_spells, data);

	menu->browse_hook = spell_menu_browser_hook;
	menu->selections = lower_case;
	menu->command_keys = "?";

	mnflag_on(menu->flags, MN_CASELESS_TAGS);

	return true;
}

/**
 * Free spell menu's allocated resources
 */
static void spell_menu_destroy(struct menu *menu)
{
	struct spell_menu_data *data = menu_priv(menu);

	mem_free(data->spells);
}

/**
 * Run the spell menu to select a spell.
 */
static int spell_menu_select(struct menu *menu, const char *noun, const char *verb)
{
	struct spell_menu_data *data = menu_priv(menu);

	struct term_hints hints = {
		.width = SPELL_MENU_WIDTH,
		.height = data->n_spells + 4,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);

	region reg = {1, 3, 0, 0};

	menu_layout(menu, reg);

	/* Format, capitalise and display */
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf), "%s which %s? ('?' to read description)", verb, noun);
	my_strcap(buf);
	show_prompt(buf, false);

	menu_select(menu);

	clear_prompt();
	Term_pop();

	return data->selected_spell;
}

/**
 * Run the spell menu, without selections.
 */
static void spell_menu_browse(struct menu *menu, const char *noun)
{
	struct spell_menu_data *data = menu_priv(menu);

	struct term_hints hints = {
		.width = SPELL_MENU_WIDTH,
		.height = data->n_spells + 4,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_TOP_CENTER
	};
	Term_push_new(&hints);

	region reg = {1, 3, 0, 0};
	menu_layout(menu, reg);

	show_prompt(format("Browsing %ss. ('?' to read description)", noun), false);

	data->browse = true;
	menu_select(menu);

	clear_prompt();
	Term_pop();
}

/**
 * Browse a given book.
 */
void textui_book_browse(const struct object *obj)
{
	struct menu menu;
	struct spell_menu_data data;

	if (spell_menu_init(&menu, &data, obj, spell_okay_to_browse)) {
		const char *noun = player->class->magic.spell_realm->spell_noun;
		spell_menu_browse(&menu, noun);
		spell_menu_destroy(&menu);
	} else {
		msg("You cannot browse that.");
	}
}

/**
 * Browse the given book.
 */
void textui_spell_browse(void)
{
	struct object *obj;

	if (get_item(&obj, "Browse which book? ", "You have no books that you can read.",
				  CMD_BROWSE_SPELL, obj_can_browse, (USE_INVEN | USE_FLOOR | IS_HARMLESS)))
	{
		/* Track the object kind */
		track_object(player->upkeep, obj);
		handle_stuff(player);

		textui_book_browse(obj);
	}
}

/**
 * Get a spell from specified book.
 */
int textui_get_spell_from_book(const char *verb,
		struct object *book, const char *error, bool (*spell_filter)(int spell_index))
{
	(void) error;

	struct menu menu;
	struct spell_menu_data data;

	track_object(player->upkeep, book);
	handle_stuff(player);

	if (spell_menu_init(&menu, &data, book, spell_filter)) {
		const char *noun = player->class->magic.spell_realm->spell_noun;
		int spell_index = spell_menu_select(&menu, noun, verb);
		spell_menu_destroy(&menu);
		return spell_index;
	}

	return -1;
}

/**
 * Get a spell from the player.
 */
int textui_get_spell(const char *verb,
		item_tester book_filter, cmd_code cmd, const char *error,
		bool (*spell_filter)(int spell_index))
{
	char prompt[ANGBAND_TERM_STANDARD_WIDTH];
	struct object *book;

	/* Create prompt */
	strnfmt(prompt, sizeof(prompt), "%s which book?", verb);
	my_strcap(prompt);

	if (!get_item(&book, prompt, error, cmd, book_filter, (USE_INVEN | USE_FLOOR))) {
		return -1;
	}

	return textui_get_spell_from_book(verb, book, error, spell_filter);
}
