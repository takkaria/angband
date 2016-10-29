/**
 * \file ui2-command.c
 * \brief Deal with UI only command processing.
 *
 * Copyright (c) 1997-2014 Angband developers
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
#include "buildid.h"
#include "cave.h"
#include "game-input.h"
#include "game-world.h"
#include "player-calcs.h"
#include "player-util.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-event.h"
#include "ui2-input.h"
#include "ui2-map.h"
#include "ui2-menu.h"
#include "ui2-options.h"
#include "ui2-output.h"
#include "ui2-wizard.h"

/**
 * Display the options and redraw afterward.
 */
void do_cmd_options_and_redraw(void)
{
	do_cmd_options();
	display_terms_redraw();
}

/**
 * Invoked when the command isn't recognised.
 */
void do_cmd_unknown(void)
{
	show_prompt("Type '?' for help.", false);
}

/**
 * Verify use of "debug" mode
 */
void textui_cmd_debug(void)
{
	/* Ask first time */
	if (!(player->noscore & NOSCORE_DEBUG)) {
		/* Mention effects */
		msg("You are about to use the dangerous, unsupported, debug commands!");
		event_signal(EVENT_MESSAGE_FLUSH);
		msg("Your machine may crash, and your savefile may become corrupted!");
		event_signal(EVENT_MESSAGE_FLUSH);

		/* Verify request */
		if (!get_check("Are you sure you want to use the debug commands? ")) {
			return;
		}

		/* Mark savefile */
		player->noscore |= NOSCORE_DEBUG;
	}

	get_debug_command();
}

/**
 * Verify the suicide command
 */
void textui_cmd_suicide(void)
{
	/* Flush input */
	event_signal(EVENT_INPUT_FLUSH);

	/* Verify */
	if (player->total_winner) {
		if (!get_check("Do you want to retire? ")) {
			return;
		}
	} else {
		if (!get_check("Do you really want to commit suicide? ")) {
			return;
		}

		event_signal(EVENT_INPUT_FLUSH);

		/* Special verification for suicide */
		const char *prompt =
			"Please verify SUICIDE by typing the `@` sign: ";

		struct term_hints hints = {
			.width = strlen(prompt) - 2 + 1,
			.height = 1,
			.position = TERM_POSITION_CENTER,
			.purpose = TERM_PURPOSE_TEXT
		};

		Term_push_new(&hints);
		Term_cursor_visible(true);
		put_str_h(prompt, loc(0, 0), COLOUR_WHITE, COLOUR_RED);
		Term_flush_output();

		struct keypress key = inkey_only_key();

		Term_pop();

		if (key.code != L'@') {
			return;
		}
	}

	cmdq_push(CMD_SUICIDE);
}

/**
 * Get input for the rest command
 */

#define REST_MENU_DONT_REST 0

struct rest_menu_entry {
	int value;
	const char *name;
};

static void rest_menu_display(struct menu *menu,
			int index, bool cursor, struct loc loc, int width)
{
	struct rest_menu_entry *entries = menu_priv(menu);

	Term_adds(loc.x, loc.y, width,
			menu_row_style(true, cursor), entries[index].name);
}

static bool rest_menu_handle(struct menu *menu,
		const ui_event *event, int index)
{
	if (event->type == EVT_SELECT) {
		struct rest_menu_entry *entries = menu_priv(menu);

		if (entries[index].value == REST_MENU_DONT_REST) {
			char buf[sizeof("1234")] = "";
			const wchar_t *prompt = L"Rest (0-9999) ";
			int prompt_len = wcslen(prompt);

			struct term_hints hints = {
				.x = 0,
				.y = index,
				.width = prompt_len + sizeof(buf) - 1,
				.height = 1,
				.position = TERM_POSITION_EXACT,
				.purpose = TERM_PURPOSE_TEXT
			};

			Term_push_new(&hints);
			Term_addws(0, 0, prompt_len, COLOUR_WHITE, prompt);
			Term_cursor_visible(true);
			Term_flush_output();

			/* Ask for duration */
			if (askfor_simple(buf, sizeof(buf), askfor_numbers)) {
				int value = atoi(buf);
				if (value > 0) {
					entries[index].value = value;
				}
			}

			Term_pop();

			/* If the user cancelled, keep running this menu */
			return entries[index].value == REST_MENU_DONT_REST;
		}
	}

	return false;
}

void textui_cmd_rest(void)
{
	menu_iter rest_menu_iter = {
		.display_row = rest_menu_display,
		.row_handler = rest_menu_handle
	};

	assert(REST_COMPLETE != REST_MENU_DONT_REST
			&& REST_ALL_POINTS != REST_MENU_DONT_REST
			&& REST_SOME_POINTS != REST_MENU_DONT_REST);

	struct rest_menu_entry entries[] = {
		{REST_ALL_POINTS,     "For HP and MP"},
		{REST_SOME_POINTS,    "For HP or MP"},
		{REST_COMPLETE,       "As needed"},
		{REST_MENU_DONT_REST, "Specify"},
	};

	struct menu menu;
	menu_init(&menu, MN_SKIN_SCROLL, &rest_menu_iter);
	menu_setpriv(&menu, N_ELEMENTS(entries), entries);
	menu.selections = lower_case;

	int max_len = 0;
	for (size_t i = 0; i < N_ELEMENTS(entries); i++) {
		int len = strlen(entries[i].name);
		if (len > max_len) {
			max_len = len;
		}
	}

	struct term_hints hints = {
		.width = max_len + 3,
		.height = N_ELEMENTS(entries),
		.tabs = true,
		.position = TERM_POSITION_TOP_LEFT,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Rest", COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(&menu);
	ui_event event = menu_select(&menu);

	assert(menu.cursor >= 0);
	assert(menu.cursor < (int) N_ELEMENTS(entries));

	int value = entries[menu.cursor].value;

	if (event.type == EVT_SELECT && value != REST_MENU_DONT_REST) {
		cmdq_push(CMD_REST);
		cmd_set_arg_choice(cmdq_peek(), "choice", value);
	}

	Term_pop();
}

/**
 * Quit the game.
 */
void textui_quit(void)
{
	player->upkeep->playing = false;
}
