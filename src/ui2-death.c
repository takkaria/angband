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

/* TODO UI2 #include "wizard.h" */

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

	time_t death_time = 0;
	time(&death_time);

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

	const int width = 31;
	struct loc loc = {8, 7};

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

	put_str_centred(loc, width, "Level: %d", (int)player->lev);
	loc.y++;

	put_str_centred(loc, width, "Exp: %d", (int)player->exp);
	loc.y++;

	put_str_centred(loc, width, "AU: %d", (int)player->au);
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
	char buf[1024];

	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "crown.txt");
	ang_file *fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	Term_clear();

	int term_width;
	int term_height;
	Term_get_size(&term_width, &term_height);

	struct loc loc = {
		.x = 0,
		.y = 2
	};

	if (fp) {
		/* Get us the first line of file, which tells us
		 * how long the longest line is */
		int line_width = 0;
		file_getl(fp, buf, sizeof(buf));
		if (sscanf(buf, "%d", &line_width) != 1 || line_width == 0) {
			line_width = 25;
		}

		loc.x = term_width / 2 - line_width / 2;

		while (file_getl(fp, buf, sizeof(buf))) {
			if (*buf) {
				put_str(buf, loc);
			}
			loc.y++;
		}

		file_close(fp);
	}

	put_str_centred(loc, term_width, "All Hail the Mighty Champion!");

	event_signal(EVENT_INPUT_FLUSH);

	pause_line();
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
 * Menu command: view character dump and inventory.
 */
static void death_info(const char *title, int index)
{
	(void) title;
	(void) index;

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	display_player(0);

	show_prompt("Hit any key to see more information:", false);

	inkey_any();

	/* Equipment - if any */
	if (player->upkeep->equip_cnt) {
		Term_clear();
		show_equip(OLIST_WEIGHT | OLIST_SHOW_EMPTY | OLIST_DEATH, NULL);
		show_prompt("You are using: -more-", false);
		inkey_any();
	}

	/* Inventory - if any */
	if (player->upkeep->inven_cnt) {
		Term_clear();
		show_inven(OLIST_WEIGHT | OLIST_DEATH, NULL);
		show_prompt("You are carrying: -more-", false);
		inkey_any();
	}

	/* Quiver - if any */
	if (player->upkeep->quiver_cnt) {
		Term_clear();
		show_quiver(OLIST_WEIGHT | OLIST_DEATH, NULL);
		show_prompt("Your quiver holds: -more-", false);
		inkey_any();
	}

	const struct store *home = &stores[STORE_HOME];

	/* Home - if anything there */
	if (home->stock) {
		struct object *obj = home->stock;

		/* Display contents of the home */
		for (int page = 1; obj; page++) {
			Term_clear();

			struct loc loc = {
				.x = 4,
				.y = 2
			};

			/* Show 12 items */
			for (int line = 0; obj && line < 12; obj = obj->next, line++) {
				char tag[8];
				char o_name[ANGBAND_TERM_STANDARD_WIDTH];

				strnfmt(tag, sizeof(tag), "%c) ", I2A(line));
				prt(tag, loc);

				object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);

				uint32_t attr = obj->kind->base->attr;

				loc.x += 3;
				c_put_str(attr, o_name, loc);
				loc.x -= 3;
				loc.y++;
			}

			show_prompt(format("Your home contains (page %d): -more-", page), false);

			inkey_any();
		}
	}

	clear_prompt();
	Term_pop();
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

	while (get_item(&obj, prompt, reject, 0, NULL,
				USE_INVEN | USE_QUIVER | USE_EQUIP | IS_HARMLESS))
	{
		char header[120];

		textblock *tb = object_info(obj, OINFO_NONE);
		object_desc(header, sizeof(header),
				obj, ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL);

		region area = {0, 0, 0, 0};
		textui_textblock_show(tb, area, header);
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

	/* TODO UI2 do_cmd_spoilers(); */
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
		.w = 0,
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
