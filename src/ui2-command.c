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
#include "ui2-options.h"
#include "ui2-output.h"

/* TODO UI2 #include "wizard.h" */

/**
 * Redraw the screen
 *
 * This command performs various low level updates, clears all the extra
 * windows and does a total redraw of the main window.
 */
void do_cmd_redraw(void)
{
	if (!character_dungeon) {
		return;
	}

	Term_push(angband_cave.term);
	Term_clear();
	Term_pop();

	/* Reset "inkey()" */
	event_signal(EVENT_INPUT_FLUSH);

	player->upkeep->notice |= (PN_COMBINE);

	player->upkeep->update |= (PU_TORCH | PU_INVEN | PU_BONUS | PU_HP |
			PU_SPELLS | PU_UPDATE_VIEW | PU_MONSTERS);

	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP | PR_INVEN |
			PR_EQUIP | PR_MESSAGE | PR_MONSTER | PR_OBJECT | PR_MONLIST | PR_ITEMLIST);

	handle_stuff(player);
	move_cursor_relative(&angband_cave, loc(player->px, player->py));
	verify_panel(&angband_cave);

	flush_all_terms();
}

/**
 * Display the options and redraw afterward.
 */
void do_cmd_options_and_redraw(void)
{
	do_cmd_options();
	do_cmd_redraw();
}

/**
 * Invoked when the command isn't recognised.
 */
void do_cmd_unknown(void)
{
	show_prompt("Type '?' for help.", false);
}

/**
 * Print the version and copyright notice.
 */
void do_cmd_version(void)
{
	char header_buf[80];

	textblock *tb = textblock_new();
	region reg = {0, 0, 0, 0};

	my_strcpy(header_buf,
			format("You are playing %s.  Type '?' for more info.", buildver),
			sizeof(header_buf));
	textblock_append(tb, "\n");
	textblock_append(tb, copyright);
	textui_textblock_show(tb, reg, header_buf);
	textblock_free(tb);
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
		msg("Your machine may crash, and your savefile may become corrupted!");
		event_signal(EVENT_MESSAGE_FLUSH);

		/* Verify request */
		if (!get_check("Are you sure you want to use the debug commands? ")) {
			return;
		}

		/* Mark savefile */
		player->noscore |= NOSCORE_DEBUG;
	}

	/* TODO UI2 get_debug_command(); */
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
		if (!get_check("Do you really want to commit suicide? [y/n]")) {
			return;
		}

		event_signal(EVENT_INPUT_FLUSH);

		/* Special Verification for suicide */
		show_prompt("Please verify SUICIDE by typing the '@' sign: ", true);
		struct keypress key = inkey_only_key();
		clear_prompt();

		if (key.code != '@') {
			return;
		}
	}

	cmdq_push(CMD_SUICIDE);
}

/**
 * Get input for the rest command
 */
void textui_cmd_rest(void)
{
	const char *prompt =
		"Rest (0-9999, '!' for HP or SP, '*' for HP and SP, '&' as needed): ";

	char buf[5] = "&";

	/* Ask for duration */
	if (!get_string(prompt, buf, sizeof(buf))) {
		return;
	}

	/* Rest... */
	if (buf[0] == '&') {
		/* ...until done */
		cmdq_push(CMD_REST);
		cmd_set_arg_choice(cmdq_peek(), "choice", REST_COMPLETE);
	} else if (buf[0] == '*') {
		/* ...a lot */
		cmdq_push(CMD_REST);
		cmd_set_arg_choice(cmdq_peek(), "choice", REST_ALL_POINTS);
	} else if (buf[0] == '!') {
		/* ...until HP or SP filled */
		cmdq_push(CMD_REST);
		cmd_set_arg_choice(cmdq_peek(), "choice", REST_SOME_POINTS);
	} else {
		/* ...some */
		int turns = atoi(buf);
		if (turns > 0) {
			turns = MIN(turn, 9999);
			cmdq_push(CMD_REST);
			cmd_set_arg_choice(cmdq_peek(), "choice", turns);
		}
	}
}

/**
 * Quit the game.
 */
void textui_quit(void)
{
	player->upkeep->playing = false;
}
