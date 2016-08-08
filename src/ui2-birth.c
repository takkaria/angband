/**
 * \file ui2-birth.c
 * \brief Text-based user interface for character creation
 *
 * Copyright (c) 1987 - 2015 Angband contributors
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
#include "cmd-core.h"
#include "game-event.h"
#include "game-input.h"
#include "obj-tval.h"
#include "player.h"
#include "ui2-birth.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-help.h"
#include "ui2-input.h"
#include "ui2-menu.h"
#include "ui2-options.h"
#include "ui2-player.h"
#include "ui2-target.h"

/**
 * Overview
 * ========
 * This file implements the user interface side of the birth process
 * for the classic terminal-based UI of Angband.
 *
 * It models birth as a series of steps which must be carried out in 
 * a specified order, with the option of stepping backwards to revisit
 * past choices.
 *
 * It starts when we receive the EVENT_ENTER_BIRTH event from the game,
 * and ends when we receive the EVENT_LEAVE_BIRTH event.  In between,
 * we will repeatedly be asked to supply a game command, which change
 * the state of the character being rolled.  Once the player is happy
 * with their character, we send the CMD_ACCEPT_CHARACTER command.
 */

/**
 * A local-to-this-file global to hold the most important bit of state
 * between calls to the game proper.  Probably not strictly necessary,
 * but reduces complexity a bit.
 */
enum birth_stage {
	BIRTH_BACK = -1,
	BIRTH_RESET = 0,
	BIRTH_QUICKSTART,
	BIRTH_RACE_CHOICE,
	BIRTH_CLASS_CHOICE,
	BIRTH_ROLLER_CHOICE,
	BIRTH_POINTBASED,
	BIRTH_ROLLER,
	BIRTH_NAME_CHOICE,
	BIRTH_HISTORY_CHOICE,
	BIRTH_FINAL_CONFIRM,
	BIRTH_COMPLETE
};


enum birth_questions {
	BQ_METHOD = 0,
	BQ_RACE,
	BQ_CLASS,
	BQ_ROLLER,
	MAX_BIRTH_QUESTIONS
};

enum birth_rollers {
	BR_POINTBASED = 0,
	BR_NORMAL,
	MAX_BIRTH_ROLLERS
};

static void point_based_start(void);

static bool quickstart_allowed = false;

bool arg_force_name;

/**
 * ------------------------------------------------------------------------
 * Quickstart? screen.
 * ------------------------------------------------------------------------
 */
static enum birth_stage textui_birth_quickstart(void)
{
	const char *prompt =
		"['Y' to use this character, 'N' to start afresh, 'C' to change name or history]";
	show_prompt(prompt);

	enum birth_stage next = BIRTH_QUICKSTART;

	do {
		struct keypress key = inkey_only_key();
		
		if (key.code == 'N' || key.code == 'n') {
			cmdq_push(CMD_BIRTH_RESET);
			next = BIRTH_RACE_CHOICE;
		} else if (key.code == KTRL('X')) {
			quit(NULL);
		} else if (!arg_force_name && (key.code == 'C' || key.code == 'c')) {
			next = BIRTH_NAME_CHOICE;
		} else if (key.code == 'Y' || key.code == 'y') {
			cmdq_push(CMD_ACCEPT_CHARACTER);
			next = BIRTH_COMPLETE;
		}
	} while (next == BIRTH_QUICKSTART);

	clear_prompt();

	return next;
}

/**
 * ------------------------------------------------------------------------
 * The various menu bits of the birth process -
 * namely choice of race, class, and roller type.
 * ------------------------------------------------------------------------
 */

/**
 * The various menus
 */
static struct menu race_menu;
static struct menu class_menu;
static struct menu roller_menu;

/**
 * Locations of the menus, etc. on the screen
 */
#define HEADER_ROW          1
#define QUESTION_ROW        7
#define TABLE_ROW           9

#define QUESTION_COL        2
#define RACE_COL            2
#define RACE_AUX_COL       19
#define CLASS_COL          19
#define CLASS_AUX_COL      36
#define ROLLER_COL         36
#define HIST_INSTRUCT_ROW  18

#define MENU_ROWS \
	TABLE_ROW + 14

/**
 * upper left column and row, width, and lower column
 */
static region race_region   = {RACE_COL,   TABLE_ROW, 17, MENU_ROWS};
static region class_region  = {CLASS_COL,  TABLE_ROW, 17, MENU_ROWS};
static region roller_region = {ROLLER_COL, TABLE_ROW, 34, MENU_ROWS};

/**
 * We use different menu "browse functions" to display the help text
 * sometimes supplied with the menu items - currently just the list
 * of bonuses, etc, corresponding to each race and class.
 */
typedef void (*browse_f) (int index, void *data, region reg);

/**
 * We have one of these structures for each menu we display - it holds
 * the useful information for the menu - text of the menu items, help
 * text, current (or default) selection, and whether random selection
 * is allowed.
 */
struct birthmenu_data {
	const char **items;
	const char *hint;
	bool allow_random;
};

/**
 * A custom display function for our menus that simply displays the
 * text from our stored data in a different colour if it's currently
 * selected.
 */
static void birthmenu_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct birthmenu_data *data = menu->menu_data;

	uint32_t attr = menu_row_style(true, cursor);
	c_put_str(attr, data->items[index], loc);
}

/**
 * Custom menu iterator
 */
static const menu_iter birth_iter = {
	.display_row = birthmenu_display
};

static void skill_help(struct text_out_info info,
		const int *r_skills, const int *c_skills,
		int mhp, int exp, int infra)
{
	int skills[SKILL_MAX];

	for (int i = 0; i < SKILL_MAX ; i++) {
		skills[i] = (r_skills ? r_skills[i] : 0) + (c_skills ? c_skills[i] : 0);
	}

	text_out_e(info,
			"Hit/Shoot/Throw: %+d/%+d/%+d\n",
			skills[SKILL_TO_HIT_MELEE],
			skills[SKILL_TO_HIT_BOW],
			skills[SKILL_TO_HIT_THROW]);
	text_out_e(info,
			"Hit die: %2d   XP mod: %d%%\n",
			mhp, exp);
	text_out_e(info,
			"Disarm: %+3d/%+3d   Devices: %+3d\n",
			skills[SKILL_DISARM_PHYS],
			skills[SKILL_DISARM_MAGIC],
			skills[SKILL_DEVICE]);
	text_out_e(info,
			"Save:   %+3d   Stealth: %+3d\n",
			skills[SKILL_SAVE],
			skills[SKILL_STEALTH]);

	if (infra >= 0) {
		text_out_e(info, "Infravision:  %d ft\n", infra * 10);
	}

	text_out_e(info, "Digging:      %+d\n", skills[SKILL_DIGGING]);

	if (infra < 0) {
		text_out_e(info, "\n");
	}
}

static const char *get_flag_desc(bitflag flag)
{
	switch (flag) {
		case OF_SUST_STR:   return "Sustains strength";
		case OF_SUST_DEX:   return "Sustains dexterity";
		case OF_SUST_CON:   return "Sustains constitution";
		case OF_PROT_BLIND: return "Resists blindness";
		case OF_HOLD_LIFE:  return "Sustains experience";
		case OF_FREE_ACT:   return "Resists paralysis";
		case OF_REGEN:      return "Regenerates quickly";
		case OF_SEE_INVIS:  return "Sees invisible creatures";

		default: return "Undocumented flag";
	}
}

static const char *get_resist_desc(int element)
{
	switch (element) {
		case ELEM_POIS:  return "Resists poison";
		case ELEM_LIGHT: return "Resists light damage";
		case ELEM_DARK:  return "Resists darkness damage";

		default: return "Undocumented element";
	}
}

static const char *get_pflag_desc(bitflag flag)
{
	switch (flag) {
		#define PF(a, b, c) case PF_##a: return (c);
		#include "list-player-flags.h"
		#undef PF

		default:
			abort(); /* compilation consistency problem */
	}
}

static void race_help(int index, void *data, region reg)
{
	(void) data;
	(void) reg;

	struct player_race *race = player_id2race(index);
	if (!race) {
		return;
	}
	
	struct text_out_info info = {
		.indent = RACE_AUX_COL
	};

	Term_cursor_to_xy(RACE_AUX_COL, TABLE_ROW);

	for (int stat = 0, len = (STAT_MAX + 1) / 2; stat < len; stat++) {
		const char *name = stat_names_reduced[stat];
		int adj = race->r_adj[stat];

		text_out_e(info, "%s%+3d", name, adj);

		if (stat + len < STAT_MAX) {
			name = stat_names_reduced[stat + len];
			adj = race->r_adj[stat + len];
			text_out_e(info, "  %s%+3d", name, adj);
		}

		text_out(info, "\n");
	}
	
	text_out_e(info, "\n");
	skill_help(info, race->r_skills, NULL, race->r_mhp, race->r_exp, race->infra);
	text_out_e(info, "\n");

	int n_flags = 0;
	int flag_space = 3;

	for (int flag = 0; flag < OF_MAX && n_flags < flag_space; flag++) {
		if (of_has(race->flags, flag)) {
			text_out_e(info, "\n%s", get_flag_desc(flag));
			n_flags++;
		}
	}

	for (int flag = 0; flag < ELEM_MAX && n_flags < flag_space; flag++) {
		if (race->el_info[flag].res_level == 1) {
			text_out_e(info, "\n%s", get_resist_desc(flag));
			n_flags++;
		}
	}

	for (int flag = 0; flag < PF_MAX && n_flags < flag_space; flag++) {
		if (pf_has(race->pflags, flag)) {
			text_out_e(info, "\n%s", get_pflag_desc(flag));
			n_flags++;
		}
	}

	while (n_flags < flag_space) {
		text_out_e(info, "\n");
		n_flags++;
	}
}

static void class_help(int index, void *data, region reg)
{
	(void) data;
	(void) reg;

	struct player_class *class = player_id2class(index);
	if (!class) {
		return;
	}

	const struct player_race *race = player->race;

	struct text_out_info info = {
		.indent = CLASS_AUX_COL
	};
	Term_cursor_to_xy(CLASS_AUX_COL, TABLE_ROW);

	for (int stat = 0,len = (STAT_MAX + 1) / 2; stat < len; stat++) {  
		const char *name = stat_names_reduced[stat];
		int adj = class->c_adj[stat] + race->r_adj[stat];

		text_out_e(info, "%s%+3d", name, adj);

		if (stat + len < STAT_MAX) {
			name = stat_names_reduced[stat + len];
			adj = class->c_adj[stat + len] + race->r_adj[stat + len];
			text_out_e(info, "  %s%+3d", name, adj);
		}

		text_out(info, "\n");
	}

	text_out_e(info, "\n");
	
	skill_help(info, race->r_skills, class->c_skills,
			race->r_mhp + class->c_mhp, race->r_exp + class->c_exp, -1);

	if (class->magic.spell_realm->index != REALM_NONE)
		text_out_e(info, "\nLearns %s magic", class->magic.spell_realm->adjective);

	int n_flags = 0;
	int flag_space = 5;

	for (int flag = 0; flag < PF_MAX && n_flags < flag_space; flag++) {
		if (pf_has(class->pflags, flag)) {
			const char *s = get_pflag_desc(flag);
			text_out_e(info, "\n%s", s);
			n_flags++;
		}
	}

	while (n_flags < flag_space) {
		text_out_e(info, "\n");
		n_flags++;
	}
}

/**
 * Set up one of our menus ready to display choices for a birth question.
 * This is slightly involved.
 */
static void init_birth_menu(struct menu *menu,
		int n_choices, int initial_choice,
		region reg, bool allow_random, browse_f hook)
{
	/* Initialise a basic menu */
	menu_init(menu, MN_SKIN_SCROLL, &birth_iter);

	/* A couple of behavioural flags - we want selections letters in
	   lower case and a double tap to act as a selection. */
	menu->selections = lower_case;
	mnflag_on(menu->flags, MN_DBL_TAP);

	/* Copy across the game's suggested initial selection, etc. */
	menu->cursor = initial_choice;

	/* Allocate sufficient space for our own bits of menu information. */
	struct birthmenu_data *menu_data = mem_zalloc(sizeof(*menu_data));

	/* Allocate space for an array of menu item texts and help texts
	 * (where applicable) */
	menu_data->items = mem_zalloc(n_choices * sizeof(*menu_data->items));
	menu_data->allow_random = allow_random;

	/* Set private data */
	menu_setpriv(menu, n_choices, menu_data);

	/* Set up the browse hook to display help text (where applicable). */
	menu->browse_hook = hook;

	/* Lay out the menu appropriately */
	menu_layout(menu, reg);
}

static void setup_menus(void)
{
	/* Count the races */
	int n_races = 0;
	for (struct player_race *race = races; race; race = race->next) {
		n_races++;
	}

	/* Race menu. */
	init_birth_menu(&race_menu,
			n_races, player->race ? player->race->ridx : 0,
			race_region, true, race_help);

	struct birthmenu_data *race_data = menu_priv(&race_menu);

	for (struct player_race *race = races; race; race = race->next) {
		race_data->items[race->ridx] = race->name;
	}

	race_data->hint =
		"Race affects stats and skills, and may confer resistances and abilities.";

	/* Count the classes */
	int n_classes = 0;
	for (struct player_class *class = classes; class; class = class->next) {
		n_classes++;
	}

	/* Class menu similar to race. */
	init_birth_menu(&class_menu,
			n_classes, player->class ? player->class->cidx : 0,
			class_region, true, class_help);

	struct birthmenu_data *class_data = menu_priv(&class_menu);

	for (struct player_class *class = classes; class; class = class->next) {
		class_data->items[class->cidx] = class->name;
	}

	class_data->hint =
		"Class affects stats, skills, and other character traits.";
		
	/* Roller menu straightforward */
	const char *roller_choices[MAX_BIRTH_ROLLERS] = { 
		"Point-based", 
		"Standard roller" 
	};

	init_birth_menu(&roller_menu, MAX_BIRTH_ROLLERS, 0, roller_region, false, NULL);

	struct birthmenu_data *roller_data = menu_priv(&roller_menu);

	for (int i = 0; i < MAX_BIRTH_ROLLERS; i++) {
		roller_data->items[i] = roller_choices[i];
	}

	roller_data->hint =
		"Choose how to generate your intrinsic stats. Point-based is recommended.";
}

/**
 * Cleans up our stored menu info when we've finished with it.
 */
static void free_birth_menu(struct menu *menu)
{
	struct birthmenu_data *data = menu_priv(menu);

	if (data) {
		mem_free(data->items);
		mem_free(data);
	}
}

static void free_birth_menus(void)
{
	/* We don't need these any more. */
	free_birth_menu(&race_menu);
	free_birth_menu(&class_menu);
	free_birth_menu(&roller_menu);
}

/**
 * Clear the previous question
 */
static void clear_question(void)
{
	for (int y = QUESTION_ROW; y < TABLE_ROW; y++) {
		Term_erase_line(0, y);
	}
}

#define BIRTH_MENU_HELPTEXT \
	"{light blue}Please select your character traits from the menus below:{/}\n\n" \
	"Use the {light green}movement keys{/} to scroll the menu, " \
	"{light green}Enter{/} to select the current menu item, '{light green}*{/}' " \
	"for a random menu item, '{light green}ESC{/}' to step back through the " \
	"birth process, '{light green}={/}' for the birth options, '{light green}?{/}' " \
	"for help, or '{light green}Ctrl-X{/}' to quit."

/**
 * Show the birth instructions on an otherwise blank screen
 */	
static void print_menu_instructions(void)
{
	Term_clear();
	Term_cursor_to_xy(QUESTION_COL, HEADER_ROW);
	
	struct text_out_info info  = {
		.indent = QUESTION_COL
	};
	text_out_e(info, BIRTH_MENU_HELPTEXT);
}

/**
 * Allow the user to select from the current menu, and return the 
 * corresponding command to the game.  Some actions are handled entirely
 * by the UI (displaying help text, for instance).
 */
static enum birth_stage menu_question(enum birth_stage current,
		struct menu *current_menu, cmd_code choice_command)
{
	struct birthmenu_data *menu_data = menu_priv(current_menu);

	enum birth_stage next = BIRTH_RESET;
	
	/* Print the question currently being asked. */
	clear_question();
	Term_adds(QUESTION_COL, QUESTION_ROW, Term_width(),
			COLOUR_YELLOW, menu_data->hint);

	current_menu->stop_keys = "?=*\x18"; /* ?, =, *, Ctrl-X */

	while (next == BIRTH_RESET) {
		/* Display the menu, wait for a selection of some sort to be made. */
		ui_event event = menu_select(current_menu);

		/* As all the menus are displayed in "hierarchical" style, we allow
		   use of "back" (left arrow key or equivalent) to step back in 
		   the proces as well as "escape". */
		if (event.type == EVT_ESCAPE) {
			next = BIRTH_BACK;
		} else if (event.type == EVT_SELECT) {
			if (current == BIRTH_ROLLER_CHOICE) {
				if (current_menu->cursor == BR_POINTBASED) {
					/* 
					 * Make sure we've got a point-based char to play with. 
					 * We call point_based_start here to make sure we get
					 * an update on the points totals before trying to
					 * display the screen. The call to CMD_RESET_STATS
					 * forces a rebuying of the stats to give us up-to-date
					 * totals. This is, it should go without saying, a hack.
					 */
					point_based_start();
					cmdq_push(CMD_RESET_STATS);
					cmd_set_arg_choice(cmdq_peek(), "choice", true);
					/* go to BIRTH_POINTBASED */
					next = current + 1;
				} else {
					/* Do a first roll of the stats */
					cmdq_push(CMD_ROLL_STATS);
					/* go to BIRTH_ROLLER */
					next = current + 2;
				}
			} else {
				cmdq_push(choice_command);
				cmd_set_arg_choice(cmdq_peek(), "choice", current_menu->cursor);
				next = current + 1;
			}
		} else if (event.type == EVT_KBRD) {
			/* '*' chooses an option at random from those the game's provided */
			if (event.key.code == '*' && menu_data->allow_random) {
				current_menu->cursor = randint0(current_menu->count);
				cmdq_push(choice_command);
				cmd_set_arg_choice(cmdq_peek(), "choice", current_menu->cursor);

				menu_refresh(current_menu);
				next = current + 1;
			} else if (event.key.code == '=') {
				do_cmd_options_birth();
				next = current;
			} else if (event.key.code == KTRL('X')) {
				quit(NULL);
			} else if (event.key.code == '?') {
				do_cmd_help();
			}
		}
	}
	
	return next;
}

/**
 * ------------------------------------------------------------------------
 * The rolling bit of the roller.
 * ------------------------------------------------------------------------
 */
static enum birth_stage roller_command(bool first_call)
{
	char prompt[80] = {0};
	size_t promptlen = 0;

	enum birth_stage next = BIRTH_ROLLER;

	/* Used to keep track of whether we've rolled a character before or not. */
	static bool prev_roll = false;

	/* Display the player - a bit cheaty, but never mind. */
	display_player(0);

	if (first_call) {
		prev_roll = false;
	}

	/* Prepare a prompt (must squeeze everything in) */
	strnfcat(prompt, sizeof (prompt), &promptlen, "['r' to reroll");
	if (prev_roll) {
		strnfcat(prompt, sizeof(prompt), &promptlen, ", 'p' for previous roll");
	}
	strnfcat(prompt, sizeof (prompt), &promptlen, " or 'Enter' to accept]");

	show_prompt(prompt);
	
	struct keypress key = inkey_only_key();

	if (key.code == ESCAPE) {
		next = BIRTH_BACK;
	} else if (key.code == KC_ENTER) {
		/* 'Enter' accepts the roll */
		next = BIRTH_NAME_CHOICE;
	} else if (key.code == ' ' || key.code == 'r') {
		/* Reroll this character */
		cmdq_push(CMD_ROLL_STATS);
		prev_roll = true;
	} else if (prev_roll && key.code == 'p') {
		/* Previous character */
		cmdq_push(CMD_PREV_STATS);
	} else if (key.code == KTRL('X')) {
		quit(NULL);
	} else if (key.code == '?') {
		do_cmd_help();
	} else {
		/* Nothing handled directly here */
		bell("Illegal roller command!");
	}

	return next;
}

/**
 * ------------------------------------------------------------------------
 * Point-based stat allocation.
 * ------------------------------------------------------------------------
 */

/* The locations of the "costs" area on the birth screen. */
#define COSTS_ROW  2
#define COSTS_COL (42 + 32)
#define TOTAL_COL (42 + 19)

/**
 * This is called whenever a stat changes.  We take the easy road, and just
 * redisplay them all using the standard function.
 */
static void point_based_stats(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	display_player_stat_info();
}

/**
 * This is called whenever any of the other miscellaneous stat-dependent things
 * changed.  We are hooked into changes in the amount of gold in this case,
 * but redisplay everything because it's easier.
 */
static void point_based_misc(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	display_player_xtra_info();
}


/**
 * This is called whenever the points totals are changed (in birth.c), so
 * that we can update our display of how many points have been spent and
 * are available.
 */
static void point_based_points(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) user;

	int *stats = data->birthstats.stats;

	struct loc loc = {COSTS_COL, COSTS_ROW};
	/* Display the costs header above COSTS_ROW */
	loc.y--;
	put_str("Cost", loc);
	loc.y++;
	
	int sum = 0;
	/* Display the costs */
	for (int i = 0; i < STAT_MAX; i++, loc.y++) {
		/* Display cost */
		put_str(format("%4d", stats[i]), loc);
		sum += stats[i];
	}
	
	loc.x = TOTAL_COL;
	put_str(format("Total Cost: %2d/%2d", sum, data->birthstats.remaining + sum), loc);
}

static void point_based_start(void)
{
	const char *prompt =
		"[up/down to move, left/right to modify, 'r' to reset, 'Enter' to accept]";

	Term_clear();

	display_player_xtra_info();
	display_player_stat_info();

	show_prompt(prompt);

	/* Register handlers for various events - cheat a bit because we redraw
	   the lot at once rather than each bit at a time. */
	event_add_handler(EVENT_BIRTHPOINTS, point_based_points, NULL);	
	event_add_handler(EVENT_STATS, point_based_stats, NULL);	
	event_add_handler(EVENT_GOLD, point_based_misc, NULL);	
}

static void point_based_stop(void)
{
	event_remove_handler(EVENT_BIRTHPOINTS, point_based_points, NULL);	
	event_remove_handler(EVENT_STATS, point_based_stats, NULL);	
	event_remove_handler(EVENT_GOLD, point_based_misc, NULL);	
}

static enum birth_stage point_based_command(void)
{
	static int stat = 0;

	enum birth_stage next = BIRTH_POINTBASED;

	/* Place cursor just after cost of current stat */
	Term_cursor_to_xy(COSTS_COL + 4, COSTS_ROW + stat);

	/* Get key */
	struct keypress key = inkey_only_key();
	
	if (key.code == KTRL('X')) {
		quit(NULL);
	} else if (key.code == ESCAPE) {
		/* Go back a step, or back to the start of this step */
		next = BIRTH_BACK;
	} else if (key.code == 'r' || key.code == 'R') {
		cmdq_push(CMD_RESET_STATS);
		cmd_set_arg_choice(cmdq_peek(), "choice", false);
	} else if (key.code == KC_ENTER) {
		/* Done */
		next = BIRTH_NAME_CHOICE;
	} else {
		int dir = target_dir(key);

		/* Prev stat, looping round to the bottom when going off the top */
		if (dir == 8) {
			stat = (stat + STAT_MAX - 1) % STAT_MAX;
		}
		
		/* Next stat, looping round to the top when going off the bottom */
		if (dir == 2) {
			stat = (stat + 1) % STAT_MAX;
		}
		
		/* Decrease stat (if possible) */
		if (dir == 4) {
			cmdq_push(CMD_SELL_STAT);
			cmd_set_arg_choice(cmdq_peek(), "choice", stat);
		}
		
		/* Increase stat (if possible) */
		if (dir == 6) {
			cmdq_push(CMD_BUY_STAT);
			cmd_set_arg_choice(cmdq_peek(), "choice", stat);
		}
	}

	return next;
}
	
/**
 * ------------------------------------------------------------------------
 * Asking for the player's chosen name.
 * ------------------------------------------------------------------------
 */
static enum birth_stage get_name_command(void)
{
	enum birth_stage next;
	char name[32];
	
	if (arg_force_name) {
		next = BIRTH_HISTORY_CHOICE;
	} else if (get_character_name(name, sizeof(name))) {
		cmdq_push(CMD_NAME_CHOICE);
		cmd_set_arg_string(cmdq_peek(), "name", name);
		next = BIRTH_HISTORY_CHOICE;
	} else {
		next = BIRTH_BACK;
	}
	
	return next;
}

void get_screen_loc(size_t cursor, int *x, int *y,
		size_t n_lines, size_t *line_starts, size_t *line_lengths)
{
	for (size_t i = 0, lengths_so_far = 0; i < n_lines; i++) {
		if (cursor >= line_starts[i]) {
			if (cursor <= line_starts[i] + line_lengths[i]) {
				*x = cursor - lengths_so_far;
				*y = i;
				break;
			}
		}
		/* +1 for the space */
		lengths_so_far += line_lengths[i] + 1;
	}
}

int edit_text(char *buffer, size_t buflen) {
	const region area = {1, HIST_INSTRUCT_ROW + 1, 71, 5};

	size_t length = strlen(buffer);
	size_t cursor = 0;

	bool done = false;

	while (!done) {
		textblock *tb = textblock_new();

		/* Display on screen */
		clear_from(HIST_INSTRUCT_ROW);
		textblock_append(tb, buffer);
		textui_textblock_place(tb, area, NULL);

		size_t *line_starts = NULL;
		size_t *line_lengths = NULL;

		size_t n_lines = textblock_calculate_lines(tb,
				&line_starts, &line_lengths, area.w);

		/* Set cursor to current editing position */
		int x;
		int y;
		get_screen_loc(cursor, &x, &y, n_lines, line_starts, line_lengths);
		Term_cursor_to_xy(1 + x, 19 + y);

		struct keypress key = inkey_only_key();

		switch (key.code) {
			case ESCAPE:
				return -1;

			case KC_ENTER:
				done = true;
				break;

			case ARROW_LEFT:
				if (cursor > 0) {
					cursor--;
				}
				break;

			case ARROW_RIGHT:
				if (cursor < length) {
					cursor++;
				}
				break;

			case ARROW_DOWN: {
				size_t add = line_lengths[y] + 1;
				if (cursor + add < length) {
					cursor += add;
				}
				break;
			}

			case ARROW_UP:
				if (y > 0) {
					size_t up = line_lengths[y - 1] + 1;
					if (cursor >= up) {
						cursor -= up;
					}
				}
				break;

			case KC_END:
				cursor = MAX(0, length);
				break;

			case KC_HOME:
				cursor = 0;
				break;

			case KC_BACKSPACE:
			case KC_DELETE: {
				/* Refuse to backspace into oblivion */
				if ((key.code == KC_BACKSPACE && cursor == 0)
						|| (key.code == KC_DELETE && cursor >= length))
				{
					break;
				}

				/* Move the string from k to nul along to the left by 1 */
				if (key.code == KC_BACKSPACE) {
					memmove(&buffer[cursor - 1], &buffer[cursor], length - cursor);
					cursor--;
				} else {
					memmove(&buffer[cursor], &buffer[cursor + 1], length - cursor - 1);
				}
				length--;
				buffer[length] = 0;
				break;
			}
			
			default:
				if (!isprint(key.code)) {
					break;
				}
				if (buffer[cursor] == 0) {
					/* Make sure we have enough room for a new character */
					if (cursor + 1 >= buflen) break;
				} else {
					/* Make sure we have enough room to add a new character */
					if (length + 1 >= buflen) break;

					/* Move the rest of the buffer along to make room */
					memmove(&buffer[cursor + 1], &buffer[cursor], length - cursor);
				}

				/* Insert the character */
				buffer[cursor] = (char) key.code;
				cursor++;
				length++;
				buffer[length] = 0;

				break;
		}

		textblock_free(tb);
	}

	return 0;
}

/**
 * ------------------------------------------------------------------------
 * Allowing the player to choose their history.
 * ------------------------------------------------------------------------
 */
static enum birth_stage get_history_command(void)
{
	enum birth_stage next = 0;
	char old_history[240];

	/* Save the original history */
	my_strcpy(old_history, player->history, sizeof(old_history));

	show_prompt("Accept character history? [y/n]");

	struct keypress key = inkey_only_key();

	/* Quit, go back, change history, or accept */
	if (key.code == KTRL('X')) {
		quit(NULL);
	} else if (key.code == ESCAPE) {
		next = BIRTH_BACK;
	} else if (key.code == 'N' || key.code == 'n') {
		char history[240];
		my_strcpy(history, player->history, sizeof(history));

		switch (edit_text(history, sizeof(history))) {
			case -1:
				next = BIRTH_BACK; /* XXX fallthru? */

			case 0:
				cmdq_push(CMD_HISTORY_CHOICE);
				cmd_set_arg_string(cmdq_peek(), "history", history);
				next = BIRTH_HISTORY_CHOICE;
				break;
		}
	} else {
		next = BIRTH_FINAL_CONFIRM;
	}

	return next;
}

/**
 * ------------------------------------------------------------------------
 * Final confirmation of character.
 * ------------------------------------------------------------------------
 */
static enum birth_stage get_confirm_command(void)
{
	const char *prompt =
		"['ESC' to step back, 'S' to start over, or any other key to continue]";

	enum birth_stage next = BIRTH_RESET;

	show_prompt(prompt);

	struct keypress key = inkey_only_key();
	
	if (key.code == 'S' || key.code == 's') {
		next = BIRTH_RESET;
	} else if (key.code == KTRL('X')) {
		quit(NULL);
	} else if (key.code == ESCAPE) {
		next = BIRTH_BACK;
	} else {
		cmdq_push(CMD_ACCEPT_CHARACTER);
		next = BIRTH_COMPLETE;
	}

	clear_prompt();

	return next;
}

/**
 * ------------------------------------------------------------------------
 * Things that relate to the world outside this file: receiving game events
 * and being asked for game commands.
 * ------------------------------------------------------------------------
 */

/**
 * This is called when we receive a request for a command in the birth 
 * process.

 * The birth process continues until we send a final character confirmation
 * command (or quit), so this is effectively called in a loop by the main
 * game.
 *
 * We're imposing a step-based system onto the main game here, so we need
 * to keep track of where we're up to, where each step moves on to, etc.
 */
int textui_do_birth(void)
{
	enum birth_stage prev    = BIRTH_BACK;
	enum birth_stage current = BIRTH_RESET;
	enum birth_stage next    = BIRTH_RESET;
	enum birth_stage roller  = BIRTH_RESET;

	struct menu *menu;
	cmd_code command;

	bool done = false;

	cmdq_push(CMD_BIRTH_INIT);
	cmdq_execute(CMD_BIRTH);

	while (!done) {
		switch (current) {

			case BIRTH_RESET:
				cmdq_push(CMD_BIRTH_RESET);
				roller = BIRTH_RESET;
				if (quickstart_allowed) {
					next = BIRTH_QUICKSTART;
				} else {
					next = BIRTH_RACE_CHOICE;
				}
				break;

			case BIRTH_QUICKSTART:
				display_player(0);
				next = textui_birth_quickstart();
				if (next == BIRTH_COMPLETE) {
					done = true;
				}
				break;

			case BIRTH_RACE_CHOICE:
			case BIRTH_CLASS_CHOICE:
			case BIRTH_ROLLER_CHOICE:
				menu = &race_menu;
				command = CMD_CHOOSE_RACE;

				Term_clear();
				print_menu_instructions();

				if (current > BIRTH_RACE_CHOICE) {
					menu_refresh(&race_menu);
					menu = &class_menu;
					command = CMD_CHOOSE_CLASS;
				}
				if (current > BIRTH_CLASS_CHOICE) {
					menu_refresh(&class_menu);
					menu = &roller_menu;
				}
				next = menu_question(current, menu, command);
				if (next == BIRTH_BACK) {
					next = current - 1;
				}
				/* Make sure the character gets reset before quickstarting */
				if (next == BIRTH_QUICKSTART) {
					next = BIRTH_RESET;
				}
				break;

			case BIRTH_POINTBASED:
				roller = BIRTH_POINTBASED;
				if (prev > BIRTH_POINTBASED) {
					point_based_start();
				}
				next = point_based_command();
				if (next == BIRTH_BACK) {
					next = BIRTH_ROLLER_CHOICE;
				}
				if (next != BIRTH_POINTBASED) {
					point_based_stop();
				}
				break;

			case BIRTH_ROLLER:
				roller = BIRTH_ROLLER;
				next = roller_command(prev < BIRTH_ROLLER);
				if (next == BIRTH_BACK) {
					next = BIRTH_ROLLER_CHOICE;
				}
				break;

			case BIRTH_NAME_CHOICE:
				if (prev < BIRTH_NAME_CHOICE) {
					display_player(0);
				}
				next = get_name_command();
				if (next == BIRTH_BACK) {
					next = roller;
				}

				break;

			case BIRTH_HISTORY_CHOICE:
				if (prev < BIRTH_HISTORY_CHOICE) {
					display_player(0);
				}
				next = get_history_command();
				if (next == BIRTH_BACK) {
					next = BIRTH_NAME_CHOICE;
				}

				break;

			case BIRTH_FINAL_CONFIRM:
				if (prev < BIRTH_FINAL_CONFIRM) {
					display_player(0);
				}
				next = get_confirm_command();
				if (next == BIRTH_BACK) {
					next = BIRTH_HISTORY_CHOICE;
				}
				if (next == BIRTH_COMPLETE) {
					done = true;
				}
				break;

			default:
				break;
		}

		prev = current;
		current = next;

		/* Execute whatever commands have been sent */
		cmdq_execute(CMD_BIRTH);
	}

	return 0;
}

/**
 * Called when we enter the birth mode - so we set up handlers, command hooks,
 * etc, here.
 */
static void ui_enter_birthscreen(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) user;

	/* Set the ugly static global that tells us if quickstart's available. */
	quickstart_allowed = data->flag;

	struct term_hints hints = {
		.width = 80,
		.height = 24,
		.purpose = TERM_PURPOSE_BIRTH
	};
	Term_push_new(&hints);

	setup_menus();
}

static void ui_leave_birthscreen(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Set the savefile name if it's not already set */
	if (!savefile[0]) {
		savefile_set_name(player_safe_name(player, true));
	}

	free_birth_menus();

	Term_pop();
}


void ui_init_birthstate_handlers(void)
{
	event_add_handler(EVENT_ENTER_BIRTH, ui_enter_birthscreen, NULL);
	event_add_handler(EVENT_LEAVE_BIRTH, ui_leave_birthscreen, NULL);
}

