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
#include "init.h"
#include "obj-desc.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "ui2-game.h"
#include "ui2-keymap.h"
#include "ui2-menu.h"
#include "ui2-output.h"

/**
 * Spell menu data structs
 */

struct spell_menu_element {
	int sidx;
	char desc[ANGBAND_TERM_STANDARD_WIDTH];
	size_t desc_len;
	uint32_t attr;
};

struct spell_menu_data {
	struct spell_menu_element *spells;
	size_t n_spells;

	char book_name[ANGBAND_TERM_STANDARD_WIDTH];

	bool browse;
	bool (*is_valid)(int spell_index);

	int selected;
};

/**
 * Spell book data structs
 */

struct book_menu_element {
	char desc[ANGBAND_TERM_STANDARD_WIDTH];
	size_t desc_len;
	uint32_t attr;
	const struct object *book;
};

struct book_menu_data {
	struct book_menu_element *books;
	size_t n_books;

	const struct object *selected;
};

#define MAX_BOOK_MENU_ELEMENTS 32
#define MAX_SPELL_MENU_ELEMENTS 32

#define SPELL_MENU_FIELDS \
	L"                                 Lv Mana Fail Info"

/**
 * Is item index valid?
 */
static bool spell_menu_valid(struct menu *menu, int index)
{
	struct spell_menu_data *data = menu_priv(menu);

	return data->is_valid(data->spells[index].sidx);
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

	c_prt(data->spells[index].attr, data->spells[index].desc, loc);
}

/**
 * Show spell long description when browsing
 */
static void show_spell_description(int index, struct spell_menu_data *data, region reg)
{
	const struct class_spell *spell = spell_by_index(data->spells[index].sidx);

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
		data->selected = data->spells[index].sidx;
		return false;
	} else {
		return false;
	}
}

static void make_spell_menu_element(int spell_index, struct spell_menu_element *elem)
{
	const struct class_spell *spell = spell_by_index(spell_index);

	if (spell->slevel >= 99) {
		elem->sidx = spell_index;
		elem->attr = COLOUR_L_DARK;
		elem->desc_len = my_strcpy(elem->desc, "(illegible)", sizeof(elem->desc));
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

	elem->sidx = spell_index;
	elem->attr = attr;
	elem->desc_len = strnfmt(elem->desc, sizeof(elem->desc),
			"%-30s%2d %4d %3d%%%s",
			spell->name,
			spell->slevel,
			spell->smana,
			spell_chance(spell_index),
			comment);
}

static void spell_menu_browser_hook(int cursor, void *data, region reg)
{
	(void) cursor;
	(void) data;
	(void) reg;

	Term_addws(0, 0, wcslen(SPELL_MENU_FIELDS), COLOUR_WHITE, SPELL_MENU_FIELDS);
}

static const menu_iter spell_menu_iter = {
	.valid_row   = spell_menu_valid,
	.display_row = spell_menu_display,
	.row_handler = spell_menu_handler
};

/**
 * Note, this function is not reentrant (as an optimization)
 */
static struct spell_menu_element *get_spell_menu_elements(size_t number)
{
	static struct spell_menu_element elems[MAX_SPELL_MENU_ELEMENTS];

	assert(number <= N_ELEMENTS(elems));
	memset(elems, 0, sizeof(elems));
	return elems;
}

static void free_spell_menu_elements(struct spell_menu_element *elems)
{
	(void) elems;
}

static size_t get_spells(const struct object *book,
		struct spell_menu_element *spell_elems, int n_elems,
		bool (*is_valid_spell)(int spell_index))
{
	int *spells;
	int n_spells = spell_collect_from_book(book, &spells);

	if (n_spells > 0 && spell_okay_list(is_valid_spell, spells, n_spells)) {
		assert(n_elems >= n_spells);
		
		for (int i = 0; i < n_spells; i++) {
			make_spell_menu_element(spells[i], &spell_elems[i]);
		}
	} else {
		n_spells = 0;
	}

	mem_free(spells);

	return n_spells;
}

/**
 * Initialise a spell menu, given an object and a validity hook
 */
static bool spell_menu_init(struct menu *menu, struct spell_menu_data *data,
		const struct object *book, bool (*is_valid_spell)(int spell_index))
{
	struct spell_menu_element *elems =
		get_spell_menu_elements(MAX_SPELL_MENU_ELEMENTS);

	int n_elems =
		get_spells(book, elems, MAX_SPELL_MENU_ELEMENTS, is_valid_spell);

	if (n_elems > 0) {
		my_strcpy(data->book_name, book->kind->name, sizeof(data->book_name));

		/* Copy across private data */
		data->spells   = elems;
		data->n_spells = n_elems;
		data->selected = -1;
		data->is_valid = is_valid_spell;
		data->browse   = false;

		menu_init(menu, MN_SKIN_SCROLL, &spell_menu_iter);
		menu_setpriv(menu, n_elems, data);

		menu->browse_hook = spell_menu_browser_hook;
		menu->selections = lower_case;
		menu->command_keys = "?";

		mnflag_on(menu->flags, MN_CASELESS_TAGS);

		return true;
	} else {
		free_spell_menu_elements(elems);

		return false;
	}
}

/**
 * Free spell menu's allocated resources
 */
static void spell_menu_destroy(struct menu *menu)
{
	struct spell_menu_data *data = menu_priv(menu);

	free_spell_menu_elements(data->spells);
}

static void spell_menu_term_push(const struct spell_menu_data *data)
{
	size_t max_len = 0;
	for (size_t i = 0; i < data->n_spells; i++) {
		if (data->spells[i].desc_len > max_len) {
			max_len = data->spells[i].desc_len;
		}
	}

	struct term_hints hints = {
		.width = MAX(wcslen(SPELL_MENU_FIELDS), max_len + 3),
		.height = data->n_spells + 1,
		.tabs = true,
		.position = TERM_POSITION_TOP_LEFT,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, data->book_name, COLOUR_WHITE, COLOUR_DARK);
}

static void spell_menu_term_pop(void)
{
	Term_pop();
}

/**
 * Run the spell menu to select a spell.
 */
static int spell_menu_select(struct menu *menu, const char *noun, const char *verb)
{
	struct spell_menu_data *data = menu_priv(menu);

	spell_menu_term_push(data);

	region reg = {0, 1, 0, 0};
	menu_layout(menu, reg);

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf), "%s which %s? ('?' to read description)", verb, noun);
	my_strcap(buf);
	show_prompt(buf, false);

	menu_select(menu);

	clear_prompt();
	spell_menu_term_pop();

	return data->selected;
}

/**
 * Run the spell menu, without selections.
 */
static void spell_menu_browse(struct menu *menu, const char *noun)
{
	struct spell_menu_data *data = menu_priv(menu);

	spell_menu_term_push(data);

	region reg = {0, 1, 0, 0};
	menu_layout(menu, reg);

	show_prompt(format("Browsing %ss. ('?' to read description)", noun), false);

	data->browse = true;
	menu_select(menu);

	clear_prompt();
	spell_menu_term_pop();
}

static size_t get_spell_books(struct book_menu_element *elems, int n_elems,
		item_tester book_tester)
{
	assert(n_elems >= player->class->magic.num_books);

	size_t found_books = 0;

	for (size_t i = 0; i < z_info->pack_size; i++) {
		const struct object *obj = player->upkeep->inven[i];

		if (object_test(book_tester, obj)) {
			for (int book = 0; book < player->class->magic.num_books; book++) {
				if (obj->tval == player->class->magic.books[book].tval
						&& obj->sval == player->class->magic.books[book].sval)
				{
					elems[book].attr = obj->kind->base->attr;
					elems[book].book = obj;
					elems[book].desc_len =
						object_desc(elems[book].desc, sizeof(elems[book].desc),
							obj, ODESC_PREFIX | ODESC_FULL);

					found_books++;
					break;
				}
			}
		}
	}

	return found_books;
}

char spell_book_tag(struct menu *menu, int index)
{
	(void) menu;

	return I2A(index);
}

void spell_book_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct book_menu_data *data = menu_priv(menu);

	if (data->books[index].book != NULL) {
		c_prt(data->books[index].attr, data->books[index].desc, loc);
	}
}

bool spell_book_handle(struct menu *menu, const ui_event *event, int index)
{
	struct book_menu_data *data = menu_priv(menu);

	if (event->type == EVT_SELECT) {
		data->selected = data->books[index].book;
		return false;
	} else {
		return true;
	}
}

static void find_book_inscriptions(struct menu *menu,
		char *inscriptions, size_t n_inscriptions, cmd_code cmd)
{
	struct book_menu_data *data = menu_priv(menu);

	unsigned char cmdkey = cmd_lookup_key(cmd, KEYMAP_MODE_OPT);
	if (cmdkey < 0x20) {
		cmdkey = UN_KTRL(cmdkey);
	}

	memset(inscriptions, 0, n_inscriptions * sizeof(*inscriptions));

	/* Check every book in the book list */
	for (size_t i = 0; i < data->n_books; i++) {
		const struct object *book = data->books[i].book;

		if (book != NULL && book->note != 0) {
			for (const char *s = strchr(quark_str(book->note), '@');
					s != NULL;
					s = strchr(s + 1, '@'))
			{
				for (const char *digit = all_digits; *digit != 0; digit++) {
					if (s[1] == *digit || (s[1] == (char) cmdkey && s[2] == *digit)) {
						size_t d = D2I(*digit);
						assert(d < n_inscriptions);

						inscriptions[d] = all_letters[i];
					}
				}
			}
		}
	}

	for (size_t i = 0; i < n_inscriptions; i++) {
		if (inscriptions[i] != 0) {
			menu->inscriptions = inscriptions;
			mnflag_on(menu->flags, MN_INSCRIP_TAGS);
			break;
		}
	}
}

static const struct object *select_spell_book(struct book_menu_element *elems,
		size_t n_elems, cmd_code cmd)
{
	menu_iter book_iter = {
		.get_tag     = spell_book_tag,
		.display_row = spell_book_display,
		.row_handler = spell_book_handle,
	};

	struct menu menu;
	menu_init(&menu, MN_SKIN_SCROLL, &book_iter);

	struct book_menu_data data = {
		.books = elems,
		.n_books = n_elems,
		.selected = NULL
	};

	menu_setpriv(&menu, 1, &data);

	int filter_count = 0;
	int filter_list[n_elems];

	for (size_t i = 0; i < data.n_books; i++) {
		if (data.books[i].book != NULL) {
			filter_list[filter_count++] = i;
		}
	}

	menu_set_filter(&menu, filter_list, filter_count);

	char inscriptions[10];
	find_book_inscriptions(&menu, inscriptions, sizeof(inscriptions), cmd);

	size_t max_len = 0;
	for (size_t i = 0; i < data.n_books; i++) {
		if (data.books[i].desc_len > max_len) {
			max_len = data.books[i].desc_len;
		}
	}

	struct term_hints hints = {
		.width = max_len + 3,
		.height = filter_count,
		.tabs = true,
		.position = TERM_POSITION_TOP_LEFT,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Books", COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(&menu);
	menu_select(&menu);

	Term_pop();

	return data.selected;
}

/**
 * Note, this function is not reentrant (as an optimization)
 */
static struct book_menu_element *get_book_menu_elements(size_t number)
{
	static struct book_menu_element elems[MAX_BOOK_MENU_ELEMENTS];

	assert(number <= N_ELEMENTS(elems));
	memset(elems, 0, sizeof(elems));

	for (size_t i = 0; i < N_ELEMENTS(elems); i++) {
		elems[i].book = NULL;
	}

	return elems;
}

static void free_book_menu_elements(struct book_menu_element *elems)
{
	(void) elems;
}

static bool get_spell_book(const struct object **book,
		const char *prompt, const char *reject,
		item_tester tester, cmd_code cmd)
{
	show_prompt(prompt, false);

	*book = NULL;

	struct book_menu_element *elems =
		get_book_menu_elements(player->class->magic.num_books);

	if (get_spell_books(elems, player->class->magic.num_books, tester) > 0) {
		*book = select_spell_book(elems, player->class->magic.num_books, cmd);
	} else if (reject) {
		msg("%s", reject);
	}

	free_book_menu_elements(elems);

	return *book != NULL;
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
	const struct object *book;

	if (get_spell_book(&book,
				"Browse which book?",
				"You have no books that you can read.",
				obj_can_browse, CMD_BROWSE_SPELL))
	{
		textui_book_browse(book);
	}
}

/**
 * Get a spell from specified book.
 */
int textui_get_spell_from_book(const char *verb,
		const struct object *book,
		const char *error, bool (*spell_filter)(int spell_index))
{
	(void) error;

	struct menu menu;
	struct spell_menu_data data;

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

	/* Create prompt */
	strnfmt(prompt, sizeof(prompt), "%s which book?", verb);
	my_strcap(prompt);

	const struct object *book;

	if (get_spell_book(&book, prompt, error, book_filter, cmd)) {
		return textui_get_spell_from_book(verb, book, error, spell_filter);
	} else {
		return -1;
	}
}
