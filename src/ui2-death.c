/**
 * \file ui2-death.c
 * \brief Handle the UI bits that happen after the character dies.
 *
 * Copyright (c) 1987 - 2007 Angband contributors
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
#include "cmds.h"
#include "game-input.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-info.h"
#include "savefile.h"
#include "store.h"
#include "ui2-death.h"
#include "ui2-history.h"
#include "ui2-input.h"
#include "ui2-knowledge.h"
#include "ui2-menu.h"
#include "ui2-object.h"
#include "ui2-player.h"
#include "ui2-score.h"
#include "ui2-wizard.h"

struct death_info_tab {
	keycode_t code;
	char *name;
	bool valid;
	void (*handler)(void);
};

static void death_info_term_push(keycode_t active,
		const struct death_info_tab *tabs, size_t n_tabs)
{
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	for (size_t t = 0; t < n_tabs; t++) {
		if (tabs[t].valid) {
			Term_add_tab(tabs[t].code, tabs[t].name,
					tabs[t].code == active ? COLOUR_WHITE : COLOUR_L_DARK,
					COLOUR_DARK);
		}
	}
}

static void death_info_term_pop(void)
{
	Term_pop();
}

static void death_info_player(void)
{
	display_player(PLAYER_DISPLAY_MODE_DEATH);
}

static void death_info_equip(void)
{
	show_equip(OLIST_WEIGHT | OLIST_SHOW_EMPTY | OLIST_DEATH, NULL);
}

static void death_info_inven(void)
{
	show_inven(OLIST_WEIGHT | OLIST_DEATH, NULL);
}

static void death_info_quiver(void)
{
	show_quiver(OLIST_WEIGHT | OLIST_DEATH, NULL);
}

static void death_info_home(void)
{
	struct store *home = &stores[STORE_HOME];

	struct object **list = mem_zalloc(sizeof(*list) * z_info->store_inven_max);
	store_stock_list(home, list, z_info->store_inven_max);

	for (int line = 0, height = Term_height();
			line < home->stock_num && line < height;
			line++)
	{
		assert(list[line] != NULL);

		char tag[8];
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];

		strnfmt(tag, sizeof(tag), "%c) ", I2A(line));
		prt(tag, loc(0, line));

		object_desc(o_name, sizeof(o_name), list[line], ODESC_PREFIX | ODESC_FULL);

		uint32_t attr = list[line]->kind->base->attr;

		c_put_str(attr, o_name, loc(3, line));
	}

	Term_flush_output();
	mem_free(list);
}

static keycode_t death_info_move(keycode_t direction,
		size_t prev, const struct death_info_tab *tabs, size_t n_tabs)
{
	assert(prev < n_tabs);

	if (direction == ARROW_LEFT) {
		for (size_t n = 0, t = prev; n < n_tabs; n++) {
			if (t == 0) {
				t = n_tabs - 1;
			} else {
				t--;
			}
			if (tabs[t].valid) {
				return tabs[t].code;
			}
		}
	} else if (direction == ARROW_RIGHT) {
		for (size_t n = 0, t = prev; n < n_tabs; n++) {
			if (t == n_tabs - 1) {
				t = 0;
			} else {
				t++;
			}
			if (tabs[t].valid) {
				return tabs[t].code;
			}
		}
	}

	return ESCAPE;
}

/**
 * Menu command: view character dump and inventory.
 */
static void death_info(const char *title, int index)
{
	(void) title;
	(void) index;

	struct death_info_tab tabs[] = {
		{'1', "Character", true,                             death_info_player},
		{'2', "Inventory", player->upkeep->inven_cnt > 0,    death_info_inven},
		{'3', "Equipment", player->upkeep->equip_cnt > 0,    death_info_equip},
		{'4', " Quiver ",  player->upkeep->quiver_cnt > 0,   death_info_quiver},
		{'5', "  Home  ",  stores[STORE_HOME].stock != NULL, death_info_home},
	};
	const size_t n_tabs = N_ELEMENTS(tabs);

	size_t prev = 0;
	death_info_term_push(tabs[prev].code, tabs, n_tabs);
	tabs[prev].handler();

	struct keypress key = inkey_only_key();

	while (key.code != ESCAPE) {
		if (key.code == ARROW_LEFT || key.code == ARROW_RIGHT) {
			key.code = death_info_move(key.code, prev, tabs, n_tabs);
			assert(key.code != ARROW_LEFT && key.code != ARROW_RIGHT);
		} else {
			for (size_t t = 0; t < n_tabs; t++) {
				if (tabs[t].code == key.code && tabs[t].valid) {
					death_info_term_pop();
					death_info_term_push(tabs[t].code, tabs, n_tabs);
					tabs[t].handler();
					prev = t;
					break;
				}
			}

			key = inkey_only_key();
		}
	}

	death_info_term_pop();
}

/**
 * Write formatted string fmt on line loc.y,
 * centred between points loc.x and loc.x + width
 */
static void put_str_centred(struct loc loc, int width, const char *fmt, ...)
{
	va_list vp;

	/* Format into the (growable) tmp */
	va_start(vp, fmt);
	const char *tmp = vformat(fmt, vp);
	va_end(vp);

	/* Center the string */
	loc.x += width / 2 - (int) strlen(tmp) / 2;

	put_str(tmp, loc);
}

/**
 * Display the tombstone
 */
static void print_tomb(void)
{
	Term_clear();

	time_t death_time = time(NULL);

	/* Open the death file */
	char buf[1024];
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "dead.txt");
	ang_file *fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	if (fp) {
		struct loc loc = {0, 0};

		while (file_getl(fp, buf, sizeof(buf))) {
			if (*buf) {
				put_str(buf, loc);
			}
			loc.y++;
		}

		file_close(fp);
	}

	const int width = 33;
	struct loc loc = {7, 7};

	put_str_centred(loc, width, "%s", op_ptr->full_name);
	loc.y++;

	put_str_centred(loc, width, "the");
	loc.y++;

	if (player->total_winner) {
		put_str_centred(loc, width, "Magnificent");
		loc.y++;
	} else {
		put_str_centred(loc, width, "%s", player->class->title[(player->lev - 1) / 5]);
		loc.y++;
	}

	loc.y++;

	put_str_centred(loc, width, "%s", player->class->name);
	loc.y++;

	put_str_centred(loc, width, "Level: %d", (int) player->lev);
	loc.y++;

	put_str_centred(loc, width, "Exp: %d", (int) player->exp);
	loc.y++;

	put_str_centred(loc, width, "AU: %d", (int) player->au);
	loc.y++;

	put_str_centred(loc, width, "Killed on Level %d", player->depth);
	loc.y++;

	put_str_centred(loc, width, "by %s.", player->died_from);
	loc.y++;

	loc.y++;

	put_str_centred(loc, width, "on %-.24s", ctime(&death_time));
}

/**
 * Display the winner crown
 */
static void display_winner(void)
{
	const int term_width = Term_width();

	char buf[1024];

	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "crown.txt");
	ang_file *fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	Term_clear();

	struct loc loc;

	if (fp) {
		/* Get us the first line of file, which tells us
		 * how long the longest line is */
		int line_width = 0;
		file_getl(fp, buf, sizeof(buf));
		if (sscanf(buf, "%d", &line_width) != 1 || line_width == 0) {
			line_width = 25;
		}

		loc.x = term_width / 2 - line_width / 2;
		loc.y = 2;

		while (file_getl(fp, buf, sizeof(buf))) {
			if (*buf) {
				put_str(buf, loc);
			}
			loc.y++;
		}

		file_close(fp);
	}

	loc.x = 1;

	put_str_centred(loc, term_width, "All Hail the Mighty Champion!");

	Term_flush_output();
	event_signal(EVENT_INPUT_FLUSH);

	show_prompt("(Press any key to continue)", false);
	inkey_any();
	clear_prompt();
}

/**
 * Menu command: dump character dump to file.
 */
static void death_file(const char *title, int index)
{
	(void) title;
	(void) index;

	char buf[1024];
	char ftmp[ANGBAND_TERM_STANDARD_WIDTH];

	strnfmt(ftmp, sizeof(ftmp), "%s.txt", player_safe_name(player, false));

	if (get_file(ftmp, buf, sizeof(buf))) {
		if (dump_save(buf)) {
			msg("Character dump successful.");
		} else {
			msg("Character dump failed!");
		}

		event_signal(EVENT_MESSAGE_FLUSH);
	}
}

/**
 * Menu command: peruse pre-death messages.
 */
static void death_messages(const char *title, int index)
{
	(void) title;
	(void) index;

	do_cmd_messages();
}

/**
 * Menu command: see top twenty scores.
 */
static void death_scores(const char *title, int index)
{
	(void) title;
	(void) index;

	show_scores();
}

/**
 * Menu command: examine items in the inventory.
 */
static void death_examine(const char *title, int index)
{
	(void) title;
	(void) index;

	const char *prompt = "Examine which item? ";
	const char *reject = "You have nothing to examine.";

	struct object *obj;

	while (get_item(&obj, prompt, reject, CMD_NULL, NULL,
				USE_INVEN | USE_QUIVER | USE_EQUIP | IS_HARMLESS))
	{
		char header[ANGBAND_TERM_STANDARD_WIDTH];

		textblock *tb = object_info(obj, OINFO_NONE);
		object_desc(header, sizeof(header),
				obj, ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL);

		region reg = {
			.x = (ANGBAND_TERM_STANDARD_WIDTH - ANGBAND_TERM_TEXTBLOCK_WIDTH) / 2,
			.y = index,
			.w = ANGBAND_TERM_TEXTBLOCK_WIDTH,
			.h = 0
		};
		textui_textblock_show(tb, TERM_POSITION_EXACT, reg, header);
		textblock_free(tb);
	}
}

/**
 * Menu command: view character history.
 */
static void death_history(const char *title, int index)
{
	(void) title;
	(void) index;

	history_display();
}

/**
 * Menu command: allow spoiler generation (mainly for randarts).
 */
static void death_spoilers(const char *title, int index)
{
	(void) title;
	(void) index;

	do_cmd_spoilers();
}

/* Menu command: toggle birth_keep_randarts option. */
static void death_randarts(const char *title, int index)
{
	(void) title;
	(void) index;

	if (OPT(birth_randarts)) {
		option_set(option_name(OPT_birth_keep_randarts),
				get_check("Keep randarts for next game? "));
	} else {
		msg("You are not playing with randarts!");
	}
}


/**
 * Menu structures for the death menu. Note that Quit must always be the
 * last option, due to a hard-coded check in death_screen
 */
static menu_action death_actions[] = {
	{0, 'i', "Information",   death_info     },
	{0, 'm', "Messages",      death_messages },
	{0, 'f', "File dump",     death_file     },
	{0, 'v', "View scores",   death_scores   },
	{0, 'x', "Examine items", death_examine  },
	{0, 'h', "History",       death_history  },
	{0, 's', "Spoilers",      death_spoilers },
	{0, 'r', "Keep randarts", death_randarts },
	{0, 'q', "Quit",          NULL           },
};

/**
 * Handle character death
 */
void death_screen(void)
{
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_DEATH
	};
	Term_push_new(&hints);

	if (player->total_winner) {
		display_winner();
	}

	print_tomb();

	/* Flush all input and output */
	event_signal(EVENT_INPUT_FLUSH);
	event_signal(EVENT_MESSAGE_FLUSH);
	clear_prompt();

	/* Display and use the death menu */
	struct menu *death_menu =
		menu_new_action(death_actions, N_ELEMENTS(death_actions));
	death_menu->stop_keys = "\x18"; /* Ctrl-X */
	mnflag_on(death_menu->flags, MN_CASELESS_TAGS);

	const region area = {
		.x = 51,
		.y = 2,
		.w = hints.width - 51,
		.h = N_ELEMENTS(death_actions)
	};
	menu_layout(death_menu, area);

	while (true) {
		ui_event event = menu_select(death_menu);

		if (event.type == EVT_KBRD) {
			if (event.key.code == KTRL('X')) {
				break;
			}
		} else if (event.type == EVT_SELECT) {
			if (get_check("Do you want to quit? ")) {
				break;
			}
		}
	}

	menu_free(death_menu);
	Term_pop();
}
