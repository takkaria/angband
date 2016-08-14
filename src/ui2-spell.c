/**
 * \file ui-spell.c
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

#define SPELL_DESC_ROWS 2
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

/**
 * Is item oid valid?
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
	(void) cursor;
	(void) width;

	struct spell_menu_data *data = menu_priv(menu);
	int spell_index = data->spells[index];
	const struct class_spell *spell = spell_by_index(spell_index);

	if (spell->slevel >= 99) {
		c_prt(COLOUR_L_DARK, "(illegible)", loc);
		return;
	}

	char aux[30];
	uint32_t attr = 0;
	const char *comment = NULL;

	if (player->spell_flags[spell_index] & PY_SPELL_FORGOTTEN) {
		comment = " forgotten";
		attr = COLOUR_YELLOW;
	} else if (player->spell_flags[spell_index] & PY_SPELL_LEARNED) {
		if (player->spell_flags[spell_index] & PY_SPELL_WORKED) {
			/* Get extra info */
			get_spell_info(spell_index, aux, sizeof(aux));
			comment = aux;
			attr = COLOUR_WHITE;
		} else {
			comment = " untried";
			attr = COLOUR_L_GREEN;
		}
	} else if (spell->slevel <= player->lev) {
		comment = " unknown";
		attr = COLOUR_L_BLUE;
	} else {
		comment = " difficult";
		attr = COLOUR_RED;
	}

	/* Dump the spell --(-- */
	char out[80];
	strnfmt(out, sizeof(out), "%-30s%2d %4d %3d%%%s",
			spell->name, spell->slevel, spell->smana, spell_chance(spell_index), comment);
	c_prt(attr, out, loc);
}

/**
 * Handle an event on a menu row.
 */
static bool spell_menu_handler(struct menu *menu, const ui_event *event, int index)
{
	struct spell_menu_data *data = menu_priv(menu);

	if (data->browse) {
		return true;
	} else {
		if (event->type == EVT_SELECT) {
			data->selected_spell = data->spells[index];
		}
		return false;
	}
}

/**
 * Show spell long description when browsing
 */
static void spell_menu_browser(int index, void *data, const region reg)
{
	struct spell_menu_data *d = data;

	const struct class_spell *spell = spell_by_index(d->spells[index]);
	struct text_out_info info = {0};
	Term_cursor_to_xy(reg.x, reg.y + reg.h - SPELL_DESC_ROWS);
	text_out(info, "\n%s\n", spell->text);
}

static const menu_iter spell_menu_iter = {
	.valid_row   = spell_menu_valid,
	.display_row = spell_menu_display,
	.row_handler = spell_menu_handler
};

/**
 * Create and initialise a spell menu, given an object and a validity hook
 */
static struct menu *spell_menu_new(const struct object *obj,
		bool (*is_valid)(int spell_index))
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, &spell_menu_iter);
	struct spell_menu_data *data = mem_alloc(sizeof(*data));

	/* collect spells from object */
	data->n_spells = spell_collect_from_book(obj, &data->spells);
	if (data->n_spells == 0 || !spell_okay_list(is_valid, data->spells, data->n_spells)) {
		menu_free(menu);
		mem_free(data);
		return NULL;
	}

	/* Copy across private data */
	data->selected_spell = -1;
	data->is_valid = is_valid;
	data->browse = false;

	menu_setpriv(menu, data->n_spells, data);

	/* Set flags */
	menu->header = "Name                             Lv Mana Fail Info";
	menu->selections = lower_case;
	menu->browse_hook = spell_menu_browser;

	mnflag_on(menu->flags, MN_CASELESS_TAGS);

	return menu;
}

/**
 * Clean up a spell menu instance
 */
static void spell_menu_destroy(struct menu *menu)
{
	struct spell_menu_data *data = menu_priv(menu);
	menu_free(menu);
	mem_free(data);
}

/**
 * Run the spell menu to select a spell.
 */
static int spell_menu_select(struct menu *menu, const char *noun, const char *verb)
{
	struct spell_menu_data *data = menu_priv(menu);

	struct term_hints hints = {
		.width = 60,
		.height = data->n_spells + 1 + SPELL_DESC_ROWS,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	/* Format, capitalise and display */
	char buf[80];
	strnfmt(buf, sizeof(buf), "%s which %s?", verb, noun);
	my_strcap(buf);
	show_prompt(buf, false);

	menu_select(menu);

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
		.width = 60,
		.height = data->n_spells + 1 + SPELL_DESC_ROWS,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_TOP_CENTER
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	show_prompt(format("Browsing %ss.", noun), false);

	data->browse = true;
	menu_select(menu);

	Term_pop();
}

/**
 * Browse a given book.
 */
void textui_book_browse(const struct object *obj)
{
	struct menu *menu = spell_menu_new(obj, spell_okay_to_browse);
	if (menu) {
		const char *noun = player->class->magic.spell_realm->spell_noun;
		spell_menu_browse(menu, noun);
		spell_menu_destroy(menu);
	} else {
		msg("You cannot browse that.");
	}
}

/**
 * Browse the given book.
 */
void textui_spell_browse(void)
{
	struct object *obj = NULL;

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

	track_object(player->upkeep, book);
	handle_stuff(player);

	struct menu *menu = spell_menu_new(book, spell_filter);
	if (menu) {
		const char *noun = player->class->magic.spell_realm->spell_noun;
		int spell_index = spell_menu_select(menu, noun, verb);
		spell_menu_destroy(menu);
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
	char prompt[1024];
	struct object *book;

	/* Create prompt */
	strnfmt(prompt, sizeof prompt, "%s which book?", verb);
	my_strcap(prompt);

	if (!get_item(&book, prompt, error, cmd, book_filter, (USE_INVEN | USE_FLOOR))) {
		return -1;
	}

	return textui_get_spell_from_book(verb, book, error, spell_filter);
}
