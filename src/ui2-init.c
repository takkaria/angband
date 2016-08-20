/**
 * \file ui-init.c
 * \brief UI initialistion
 *
 * Copyright (c) 2015 Nick McConnell
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
 *
 * This file is used to initialize various variables and arrays for the
 * Angband game.
 *
 * Several of the arrays for Angband are built from data files in the
 * "lib/gamedata" directory.
 */


#include "angband.h"
#include "game-input.h"
#include "game-event.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-knowledge.h"
#include "ui2-options.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-term.h"

/**
 * Initialise the UI
 */
void textui_init(void)
{
	event_signal_message(EVENT_INITSTATUS, MSG_GENERIC, "Loading basic pref file...");

	/* Initialize graphics info and basic pref data */
	process_pref_file("pref.prf", false, false);

	/* Sneakily init command list */
	cmd_init();

	/* Initialize knowledge things */
	textui_knowledge_init();

	/* Initialize input hooks */
	textui_input_init();

	/* Initialize visual prefs */
	textui_prefs_init();

	event_signal_message(EVENT_INITSTATUS, MSG_GENERIC, "Initialization complete");
}

/**
 * Clean up UI
 */
void textui_cleanup(void)
{
	/* Cleanup any options menus */
	cleanup_options();

	keymap_free();
	textui_prefs_free();
}
