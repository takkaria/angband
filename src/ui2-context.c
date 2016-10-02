/**
 * \file ui2-context.c
 * \brief Show player and terrain context menus.
 *
 * Copyright (c) 2011 Brett Reid
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband license":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "cmds.h"
#include "game-input.h"
#include "mon-desc.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "obj-chest.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "target.h"
#include "ui2-context.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-knowledge.h"
#include "ui2-map.h"
#include "ui2-menu.h"
#include "ui2-mon-lore.h"
#include "ui2-object.h"
#include "ui2-player.h"
#include "ui2-spell.h"
#include "ui2-store.h"
#include "ui2-target.h"

#define ADD_LABEL(text, cmd, valid) do { \
	cmdkey = cmd_lookup_key_unktrl((cmd), mode); \
	menu_dynamic_add_label_valid(m, (text), cmdkey, (cmd), labels, (valid)); \
} while (0)

/**
 * Additional constants for menu item values.
 * The values must not collide with the cmd_code enum,
 * since those are the main values for these menu items.
 */
enum context_menu_value_e {
	MENU_VALUE_INSPECT = CMD_REPEAT + 1000,
	MENU_VALUE_DROP_ALL,
	MENU_VALUE_LOOK,
	MENU_VALUE_RECALL,
	MENU_VALUE_REST,
	MENU_VALUE_INVENTORY,
	MENU_VALUE_CENTER_MAP,
	MENU_VALUE_FLOOR,
	MENU_VALUE_CHARACTER,
	MENU_VALUE_OTHER,
	MENU_VALUE_KNOWLEDGE,
	MENU_VALUE_MAP,
	MENU_VALUE_MESSAGES,
	MENU_VALUE_OBJECTS,
	MENU_VALUE_MONSTERS,
	MENU_VALUE_TOGGLE_IGNORED,
	MENU_VALUE_OPTIONS,
	MENU_VALUE_HELP,
};

static struct term_hints context_term_hints(struct menu *m, struct loc mloc)
{
	region reg = menu_dynamic_calc_location(m);

	struct term_hints hints = {
		.x = mloc.x,
		.y = mloc.y,
		.width = reg.w,
		.height = reg.h,
		.position = TERM_POSITION_EXACT,
		.purpose = TERM_PURPOSE_MENU
	};

	return hints;
}

static void context_menu_player_2(struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;

	char *labels = string_make(lower_case);
	unsigned char cmdkey = 0;

	struct menu *m = menu_dynamic_new();

	m->selections = labels;

	menu_dynamic_add_label(m, "Knowledge", '~', MENU_VALUE_KNOWLEDGE, labels);
	menu_dynamic_add_label(m, "Show Map", 'M', MENU_VALUE_MAP, labels);
	menu_dynamic_add_label(m, "^Show Messages", 'P', MENU_VALUE_MESSAGES, labels);
	menu_dynamic_add_label(m, "Show Monster List", '[', MENU_VALUE_MONSTERS, labels);
	menu_dynamic_add_label(m, "Show Object List", ']', MENU_VALUE_OBJECTS, labels);

	/* Ignore toggle has different keys, but we don't have a way to look them
	 * up (see ui-game.c). */
	cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'K' : 'O';
	menu_dynamic_add_label(m, "Toggle Ignored", cmdkey, MENU_VALUE_TOGGLE_IGNORED, labels);

	ADD_LABEL("Ignore an item", CMD_IGNORE, true);

	menu_dynamic_add_label(m, "Options", '=', MENU_VALUE_OPTIONS, labels);
	menu_dynamic_add_label(m, "Commands", '?', MENU_VALUE_HELP, labels);

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	string_free(labels);
	clear_prompt();
	Term_pop();

	bool allowed = false;
	/* Check the command to see if it is allowed. */
	switch (selected) {
		case -1:
			return; /* User cancelled the menu. */

		case MENU_VALUE_KNOWLEDGE:
		case MENU_VALUE_MAP:
		case MENU_VALUE_MESSAGES:
		case MENU_VALUE_TOGGLE_IGNORED:
		case MENU_VALUE_HELP:
		case MENU_VALUE_MONSTERS:
		case MENU_VALUE_OBJECTS:
		case MENU_VALUE_OPTIONS:
			allowed = true;
			break;

		case CMD_IGNORE:
			cmdkey = cmd_lookup_key(selected, mode);
			allowed = key_confirm_command(cmdkey);
			break;

		default:
			/* Invalid command; prevent anything from happening. */
			bell("Invalid context menu command.");
			allowed = false;
			break;
	}

	if (!allowed) {
		return;
	}

	/* Perform the command. */
	switch (selected) {
		case MENU_VALUE_KNOWLEDGE:
			Term_keypress('~', 0);
			break;

		case MENU_VALUE_MAP:
			Term_keypress('M', 0);
			break;

		case MENU_VALUE_MESSAGES:
			Term_keypress(KTRL('p'), 0);
			break;

		case CMD_IGNORE:
			cmdkey = cmd_lookup_key(selected, mode);
			Term_keypress(cmdkey, 0);
			break;

		case MENU_VALUE_TOGGLE_IGNORED:
			/* Ignore toggle has different keys, but we don't have a way to
			 * look them up (see ui-game.c). */
			cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'K' : 'O';
			Term_keypress(cmdkey, 0);
			break;

		case MENU_VALUE_HELP:
			context_menu_command(mloc);
			break;

		case MENU_VALUE_MONSTERS:
			Term_keypress('[', 0);
			break;

		case MENU_VALUE_OBJECTS:
			Term_keypress(']', 0);
			break;

		case MENU_VALUE_OPTIONS:
			Term_keypress('=', 0);
			break;

		default:
			break;
	}
}

static void context_menu_player_display_floor(void)
{
	int diff = weight_remaining(player);
	struct object *obj;

	/* There is an item on the floor, select from there */
	player->upkeep->command_wrk = (USE_FLOOR);

	/* Prompt for a command */
	show_prompt(format("(Inventory) Burden %d.%d lb (%d.%d lb %s). Item for command:",
				player->upkeep->total_weight / 10,
				player->upkeep->total_weight % 10,
				abs(diff) / 10, abs(diff) % 10,
				(diff < 0 ? "overweight" : "remaining")), false);


	/* Get an item to use a context command on */
	if (get_item(&obj, NULL, NULL, CMD_NULL, NULL,
				USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | SHOW_EMPTY | IS_HARMLESS))
	{
		track_object(player->upkeep, obj);
		context_menu_object(obj);
	}
}

void context_menu_player(struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;

	char *labels = string_make(lower_case);
	unsigned char cmdkey = 0;

	struct menu *m = menu_dynamic_new();

	m->selections = labels;

	ADD_LABEL("Use", CMD_USE, true);

	/* if player can cast, add casting option */
	if (player_can_cast(player, false)) {
		ADD_LABEL("Cast", CMD_CAST, true);
	}

	/* if player is on stairs add option to use them */
	if (square_isupstairs(cave, player->py, player->px)) {
		ADD_LABEL("Go Up", CMD_GO_UP, true);
	} else if (square_isdownstairs(cave, player->py, player->px)) {
		ADD_LABEL("Go Down", CMD_GO_DOWN, true);
	}

	/* Looking has different keys, but we don't have a way to look them up
	 * (see ui-game.c). */
	cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'l' : 'x';
	menu_dynamic_add_label(m, "Look", cmdkey, MENU_VALUE_LOOK, labels);

	/* 'R' is used for resting in both keymaps. */
	menu_dynamic_add_label(m, "Rest", 'R', MENU_VALUE_REST, labels);

	/* 'i' is used for inventory in both keymaps. */
	menu_dynamic_add_label(m, "Inventory", 'i', MENU_VALUE_INVENTORY, labels);

	/* if object under player add pickup option */
	{
		struct object *obj = square_object(cave, player->py, player->px);
		if (obj && !ignore_item_ok(obj)) {
			/* 'f' isn't in rogue keymap, so we can use it here. */
			menu_dynamic_add_label(m, "Floor", 'f', MENU_VALUE_FLOOR, labels);

			bool valid = inven_carry_okay(obj) ? true : false;
			ADD_LABEL("Pick up", CMD_PICKUP, valid);
		}
	}

	/* 'C' is used for the character sheet in both keymaps. */
	menu_dynamic_add_label(m, "Character", 'C', MENU_VALUE_CHARACTER, labels);

	if (!OPT(center_player)) {
		menu_dynamic_add_label(m,
				"^Center Map", 'L', MENU_VALUE_CENTER_MAP, labels);
	}

	menu_dynamic_add_label(m, "Other", ' ', MENU_VALUE_OTHER, labels);

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	string_free(labels);
	clear_prompt();
	Term_pop();

	cmdkey = cmd_lookup_key(selected, mode);

	bool allowed = false;
	/* Check the command to see if it is allowed. */
	switch(selected) {
		case -1:
			return; /* User cancelled the menu. */

		case CMD_USE:
		case CMD_CAST:
		case CMD_GO_UP:
		case CMD_GO_DOWN:
		case CMD_PICKUP:
			/* Only check for ^ inscriptions, since we don't have an object
			 * selected (if we need one). */
			allowed = key_confirm_command(cmdkey);
			break;

		case MENU_VALUE_REST:
			allowed = key_confirm_command('R');
			break;

		case MENU_VALUE_INVENTORY:
		case MENU_VALUE_LOOK:
		case MENU_VALUE_CHARACTER:
		case MENU_VALUE_OTHER:
		case MENU_VALUE_FLOOR:
		case MENU_VALUE_CENTER_MAP:
			allowed = true;
			break;

		default:
			/* Invalid command; prevent anything from happening. */
			bell("Invalid context menu command.");
			allowed = false;
			break;
	}

	if (!allowed) {
		return;
	}

	/* Perform the command. */
	switch(selected) {
		case CMD_USE:
		case CMD_CAST:
			cmdkey = cmd_lookup_key(selected, mode);
			Term_keypress(cmdkey, 0);
			break;

		case CMD_GO_UP:
		case CMD_GO_DOWN:
		case CMD_PICKUP:
			cmdq_push(selected);
			break;

		case MENU_VALUE_REST:
			Term_keypress('R', 0);
			break;

		case MENU_VALUE_INVENTORY:
			Term_keypress('i', 0);
			break;

		case MENU_VALUE_LOOK:
			if (target_set_interactive(TARGET_LOOK,
						loc(player->px, player->py)))
			{
				msg("Target Selected.");
			}
			break;

		case MENU_VALUE_CHARACTER:
			Term_keypress('C', 0);
			break;

		case MENU_VALUE_OTHER:
			context_menu_player_2(mloc);
			break;

		case MENU_VALUE_FLOOR:
			context_menu_player_display_floor();
			break;

		case MENU_VALUE_CENTER_MAP:
			do_cmd_center_map();
			break;

		default:
			break;
	}
}

void context_menu_cave(struct chunk *c,
		struct loc loc, bool adjacent, struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;

	struct object *square_obj = square_object(c, loc.y, loc.x);

	char *labels = string_make(lower_case);
	unsigned char cmdkey = 0;

	struct menu *m = menu_dynamic_new();

	m->selections = labels;

	/* Looking has different keys, but we don't have a way to look them up
	 * (see ui-game.c). */
	cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'l' : 'x';
	menu_dynamic_add_label(m, "Look At", cmdkey, MENU_VALUE_LOOK, labels);

	if (c->squares[loc.y][loc.x].mon) {
		/* '/' is used for recall in both keymaps. */
		menu_dynamic_add_label(m, "Recall Info", '/', MENU_VALUE_RECALL, labels);
	}

	ADD_LABEL("Use Item On", CMD_USE, true);

	if (player_can_cast(player, false)) {
		ADD_LABEL("Cast On", CMD_CAST, true);
	}

	if (adjacent) {
		struct object *chest = chest_check(loc.y, loc.x, CHEST_ANY);
		ADD_LABEL(c->squares[loc.y][loc.x].mon ? "Attack" : "Alter",
				CMD_ALTER, true);

		if (chest && !ignore_item_ok(chest)) {
			if (chest->known->pval) {
				if (is_locked_chest(chest)) {
					ADD_LABEL("Disarm Chest", CMD_DISARM, true);
					ADD_LABEL("Open Chest", CMD_OPEN, true);
				} else {
					ADD_LABEL("Open Disarmed Chest", CMD_OPEN, true);
				}
			} else {
				ADD_LABEL("Open Chest", CMD_OPEN, true);
			}
		}

		if (square_isknowntrap(c, loc.y, loc.x)) {
			ADD_LABEL("Disarm", CMD_DISARM, true);
			ADD_LABEL("Jump Onto", CMD_JUMP, true);
		}

		if (square_isopendoor(c, loc.y, loc.x)) {
			ADD_LABEL("Close", CMD_CLOSE, true);
		}
		else if (square_iscloseddoor(c, loc.y, loc.x)) {
			ADD_LABEL("Open", CMD_OPEN, true);
			ADD_LABEL("Lock", CMD_DISARM, true);
		}
		else if (square_isdiggable(c, loc.y, loc.x)) {
			ADD_LABEL("Tunnel", CMD_TUNNEL, true);
		}

		ADD_LABEL("Walk Towards", CMD_WALK, true);
	} else {
		/* ',' is used for ignore in rogue keymap, so we'll just swap letters */
		cmdkey = (mode == KEYMAP_MODE_ORIG) ? ',' : '.';
		menu_dynamic_add_label(m, "Pathfind To", cmdkey, CMD_PATHFIND, labels);

		ADD_LABEL("Walk Towards", CMD_WALK, true);
		ADD_LABEL("Run Towards", CMD_RUN, true);
	}

	if (player_can_fire(player, false)) {
		ADD_LABEL("Fire On", CMD_FIRE, true);
	}

	ADD_LABEL("Throw To", CMD_THROW, true);

#define PROMPT(str) \
	("(Enter to select command, ESC to cancel) " str)

	if (player->timed[TMD_IMAGE]) {
		show_prompt(PROMPT("You see something strange:"), false);
	} else if (c->squares[loc.y][loc.x].mon) {
		char m_name[ANGBAND_TERM_STANDARD_WIDTH];
		struct monster *mon = square_monster(c, loc.y, loc.x);

		/* Get the monster name ("a kobold") */
		monster_desc(m_name, sizeof(m_name), mon, MDESC_IND_VIS);

		show_prompt(format(PROMPT("You see %s:"), m_name), false);
	} else if (square_obj && !ignore_item_ok(square_obj)) {
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];

		/* Obtain an object description */
		object_desc(o_name, sizeof(o_name), square_obj,
				ODESC_PREFIX | ODESC_FULL);

		show_prompt(format(PROMPT("You see %s:"), o_name), false);
	} else {
		/* Feature (apply mimic) */
		const char *name = square_apparent_name(c, player, loc.y, loc.x);

		if (square_isshop(cave, loc.y, loc.x)) {
			show_prompt(format(PROMPT("You see the entrance to the %s:"), name), false);
		} else {
			show_prompt(format(PROMPT("You see %s %s:"),
						(is_a_vowel(name[0]) ? "an" : "a"), name), false);
		}
	}

#undef PROMPT

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	string_free(labels);
	clear_prompt();
	Term_pop();

	cmdkey = cmd_lookup_key(selected, mode);

	bool allowed = false;
	/* Check the command to see if it is allowed. */
	switch (selected) {
		case -1:
			return; /* User cancelled the menu. */

		case MENU_VALUE_LOOK:
		case MENU_VALUE_RECALL:
		case CMD_PATHFIND:
			allowed = true;
			break;

		case CMD_ALTER:
		case CMD_DISARM:
		case CMD_JUMP:
		case CMD_CLOSE:
		case CMD_OPEN:
		case CMD_TUNNEL:
		case CMD_WALK:
		case CMD_RUN:
		case CMD_CAST:
		case CMD_FIRE:
		case CMD_THROW:
		case CMD_USE:
			/* Only check for ^ inscriptions, since we don't have an object
			 * selected (if we need one). */
			allowed = key_confirm_command(cmdkey);
			break;

		default:
			/* Invalid command; prevent anything from happening. */
			bell("Invalid context menu command.");
			allowed = false;
			break;
	}

	if (!allowed) {
		return;
	}

	/* Perform the command. */
	switch (selected) {
		case MENU_VALUE_LOOK:
			/* Look at the spot */
			if (target_set_interactive(TARGET_LOOK, loc)) {
				msg("Target Selected.");
			}
			break;

		case MENU_VALUE_RECALL: {
			/* Recall monster Info */
			struct monster *mon = square_monster(c, loc.y, loc.x);
			if (mon) {
				struct monster_lore *lore = get_lore(mon->race);
				lore_show_interactive(mon->race, lore);
			}
		}
			break;

		case CMD_PATHFIND:
			cmdq_push(selected);
			cmd_set_arg_point(cmdq_peek(), "point", loc.x, loc.y);
			break;

		case CMD_ALTER:
		case CMD_DISARM:
		case CMD_JUMP:
		case CMD_CLOSE:
		case CMD_OPEN:
		case CMD_TUNNEL:
		case CMD_WALK:
		case CMD_RUN:
			cmdq_push(selected);
			cmd_set_arg_direction(cmdq_peek(),
					"direction", coords_to_dir(loc.y, loc.x));
			break;

		case CMD_CAST:
		case CMD_FIRE:
		case CMD_THROW:
		case CMD_USE:
			cmdq_push(selected);
			cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
			break;

		default:
			break;
	}
}

static void context_menu_object_create(struct menu *m, struct object *obj)
{
/* 1 for menu padding on both sides */
#define CONTEXT_MENU_OBJECT_WIDTH \
	(menu_reg.w + 1 + 1)

	region menu_reg = menu_dynamic_calc_location(m);
	textblock *tb = object_info(obj, OINFO_NONE);

	int textblock_width = ANGBAND_TERM_STANDARD_WIDTH - CONTEXT_MENU_OBJECT_WIDTH;
	assert(textblock_width > 0);

	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	size_t lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, textblock_width);
	mem_free(line_starts);
	mem_free(line_lengths);

	int term_height;
	if ((int) lines == menu_reg.h + 1) {
		term_height = menu_reg.h + 2;
	} else {
		term_height = MAX(menu_reg.h, (int) lines);
	}

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = term_height,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);

	region textblock_reg = {0, 0, textblock_width, 0};
	textui_textblock_place(tb, textblock_reg, NULL);

	menu_reg.x = hints.width - menu_reg.w - 1;
	if (term_height > menu_reg.h + 1) {
		menu_reg.y = 1;
	}
	menu_layout(m, menu_reg);

#undef CONTEXT_MENU_OBJECT_WIDTH
}

static void context_menu_object_destroy(struct menu *m)
{
	menu_dynamic_free(m);
	Term_pop();
}

/**
 * Pick the context menu options appropiate for the item.
 * Returns true when user selected a command that must be done.
 */
bool context_menu_object(struct object *obj)
{
	const int mode = KEYMAP_MODE_OPT;

	char *labels = string_make(lower_case);
	unsigned char cmdkey = 0;

	struct menu *m = menu_dynamic_new();

	m->selections = labels;

	if (obj_can_browse(obj)) {
		if (obj_can_cast_from(obj) && player_can_cast(player, false)) {
			ADD_LABEL("Cast", CMD_CAST, true);
		}
		if (obj_can_study(obj) && player_can_study(player, false)) {
			ADD_LABEL("Study", CMD_STUDY, true);
		}
		if (player_can_read(player, false)) {
			ADD_LABEL("Browse", CMD_BROWSE_SPELL, true);
		}
	} else if (obj_is_useable(obj)) {
		if (tval_is_wand(obj)) {
			bool valid = obj_has_charges(obj);
			ADD_LABEL("Aim", CMD_USE_WAND, valid);
		} else if (tval_is_rod(obj)) {
			bool valid = obj_can_zap(obj);
			ADD_LABEL("Zap", CMD_USE_ROD, valid);
		} else if (tval_is_staff(obj)) {
			bool valid = obj_has_charges(obj);
			ADD_LABEL("Use", CMD_USE_STAFF, valid);
		} else if (tval_is_scroll(obj)) {
			bool valid = player_can_read(player, false);
			ADD_LABEL("Read", CMD_READ_SCROLL, valid);
		} else if (tval_is_potion(obj)) {
			ADD_LABEL("Quaff", CMD_QUAFF, true);
		} else if (tval_is_edible(obj)) {
			ADD_LABEL("Eat", CMD_EAT, true);
		} else if (obj_is_activatable(obj)) {
			bool valid =
				object_is_equipped(player->body, obj)
				&& obj_can_activate(obj);
			ADD_LABEL("Activate", CMD_ACTIVATE, valid);
		} else if (obj_can_fire(obj)) {
			ADD_LABEL("Fire", CMD_FIRE, true);
		} else {
			ADD_LABEL("Use", CMD_USE, true);
		}
	}

	if (obj_can_refill(obj)) {
		ADD_LABEL("Refill", CMD_REFILL, true);
	}

	if (object_is_equipped(player->body, obj) && obj_can_takeoff(obj)) {
		ADD_LABEL("Take off", CMD_TAKEOFF, true);
	} else if (!object_is_equipped(player->body, obj) && obj_can_wear(obj)) {
		ADD_LABEL("Equip", CMD_WIELD, true);
	}

	if (object_is_carried(player, obj)) {
		if (!square_isshop(cave, player->py, player->px)) {
			ADD_LABEL("Drop", CMD_DROP, true);

			if (obj->number > 1) {
				/* 'D' is used for ignore in rogue keymap, so swap letters. */
				cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'D' : 'k';
				menu_dynamic_add_label(m,
						"Drop All", cmdkey, MENU_VALUE_DROP_ALL, labels);
			}
		} else if (square_shopnum(cave, player->py, player->px) == STORE_HOME) {
			ADD_LABEL("Drop", CMD_DROP, true);

			if (obj->number > 1) {
				/* 'D' is used for ignore in rogue keymap, so swap letters. */
				cmdkey = (mode == KEYMAP_MODE_ORIG) ? 'D' : 'k';
				menu_dynamic_add_label(m,
						"Drop All", cmdkey, MENU_VALUE_DROP_ALL, labels);
			}
		} else if (store_will_buy_tester(obj)) {
			ADD_LABEL("Sell", CMD_DROP, true);
		}
	} else {
		bool valid = inven_carry_okay(obj);
		ADD_LABEL("Pick up", CMD_PICKUP, valid);
	}

	ADD_LABEL("Throw", CMD_THROW, true);
	ADD_LABEL("Inscribe", CMD_INSCRIBE, true);

	if (obj_has_inscrip(obj)) {
		ADD_LABEL("Uninscribe", CMD_UNINSCRIBE, true);
	}

	ADD_LABEL((object_is_ignored(obj) ? "Unignore" : "Ignore"), CMD_IGNORE, true);

	char header[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(header, sizeof(header), obj, ODESC_PREFIX | ODESC_BASE);

	show_prompt(format("(Enter to select, ESC) Command for %s:", header), false);

	context_menu_object_create(m, obj);

	int selected = menu_dynamic_select(m);

	context_menu_object_destroy(m);
	string_free(labels);
	clear_prompt();

	cmdkey = cmd_lookup_key(selected, mode);

	bool allowed = false;

	switch (selected) {
		case -1:
			return false; /* User cancelled the menu. */

		case MENU_VALUE_DROP_ALL:
			/* Drop entire stack with confirmation. */
			if (get_check(format("Drop %s? ", header))) {
				if (square_isshop(cave, player->py, player->px)) {
					cmdq_push(CMD_STASH);
				} else {
					cmdq_push(CMD_DROP);
				}
				cmd_set_arg_item(cmdq_peek(), "item", obj);
				cmd_set_arg_number(cmdq_peek(), "quantity", obj->number);
			}
			return true;

		case CMD_BROWSE_SPELL:
		case CMD_STUDY:
		case CMD_CAST:
		case CMD_IGNORE:
		case CMD_WIELD:
		case CMD_TAKEOFF:
		case CMD_INSCRIBE:
		case CMD_UNINSCRIBE:
		case CMD_PICKUP:
		case CMD_DROP:
		case CMD_REFILL:
		case CMD_THROW:
		case CMD_USE_WAND:
		case CMD_USE_ROD:
		case CMD_USE_STAFF:
		case CMD_READ_SCROLL:
		case CMD_QUAFF:
		case CMD_EAT:
		case CMD_ACTIVATE:
		case CMD_FIRE:
		case CMD_USE:
			/* Check for inscriptions that trigger confirmation. */
			allowed = key_confirm_command(cmdkey)
				&& get_item_allow(obj, selected, false);
			break;
		default:
			/* Invalid command; prevent anything from happening. */
			bell("Invalid context menu command.");
			allowed = false;
			break;
	}

	if (!allowed) {
		return false;
	}

	if (selected == CMD_IGNORE) {
		/* ignore or unignore the item */
		textui_cmd_ignore_menu(obj);
	} else if (selected == CMD_BROWSE_SPELL) {
		/* browse a spellbook */
		/* copied from textui_spell_browse */
		textui_book_browse(obj);
		return false;
	} else if (selected == CMD_STUDY) {
		cmdq_push(CMD_STUDY);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
	} else if (selected == CMD_CAST) {
		if (obj_can_cast_from(obj)) {
			cmdq_push(CMD_CAST);
			cmd_set_arg_item(cmdq_peek(), "book", obj);
		}
	} else {
		cmdq_push(selected);
		cmd_set_arg_item(cmdq_peek(), "item", obj);

		/* If we're in a store, change the "drop" command to "stash". */
		if (selected == CMD_DROP
				&& square_isshop(cave, player->py, player->px))
		{
			struct command *gc = cmdq_peek();
			if (square_shopnum(cave, player->py, player->px) == STORE_HOME) {
				gc->code = CMD_STASH;
			} else {
				gc->code = CMD_SELL;
			}
		}
	}

	return true;
}

static void show_command_list(struct cmd_info *cmd_list,
		int size, struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;

	struct menu *m = menu_dynamic_new();

	m->selections = lower_case;

	for (int i = 0; i < size; i++) {
		char cmd_name[ANGBAND_TERM_STANDARD_WIDTH];
		char key[3];

		if (KTRL(cmd_list[i].key[mode]) == cmd_list[i].key[mode]) {
			key[0] = '^';
			key[1] = UN_KTRL(cmd_list[i].key[mode]);
			key[2] = 0;
		} else {
			key[0] = cmd_list[i].key[mode];
			key[1] = 0;
		}

		strnfmt(cmd_name, sizeof(cmd_name), "%s (%s)",  cmd_list[i].desc, key);
		menu_dynamic_add(m, cmd_name, i + 1);
	}

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	clear_prompt();
	Term_pop();

	if (selected > 0 && selected < size + 1) {
		/* execute the command */
		Term_keypress(cmd_list[selected - 1].key[mode], 0);
	}
}

void context_menu_command(struct loc mloc)
{
	struct menu *m = menu_dynamic_new();

	m->selections = lower_case;
	menu_dynamic_add(m, "Item", 1);
	menu_dynamic_add(m, "Action", 2);
	menu_dynamic_add(m, "Item Management", 3);
	menu_dynamic_add(m, "Info", 4);
	menu_dynamic_add(m, "Util", 5);
	menu_dynamic_add(m, "Misc", 6);

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	clear_prompt();
	Term_pop();

	if (selected > 0) {
		selected--;
		show_command_list(cmds_all[selected].list, cmds_all[selected].len, mloc);
	}
}

/**
 * Handle a textui mouseclick.
 */
void textui_process_click(ui_event event)
{
	if (!OPT(mouse_movement)) {
		return;
	}

	int x = map_grid_x(event.mouse.x);
	int y = map_grid_y(event.mouse.y);

	/* Check for a valid location */
	if (!square_in_bounds_fully(cave, y, x)) {
		return;
	}

	if (player->px == x && player->py == y) {
		if (event.mouse.mods & KC_MOD_SHIFT) {
			/* shift-click - cast magic */
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				cmdq_push(CMD_CAST);
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				Term_keypress('i', 0);
			}
		} else if (event.mouse.mods & KC_MOD_CONTROL) {
			/* ctrl-click - use feature / use inventory item
			 * switch with default */
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				if (square_isupstairs(cave, player->py, player->px)) {
					cmdq_push(CMD_GO_UP);
				} else if (square_isdownstairs(cave, player->py, player->px)) {
					cmdq_push(CMD_GO_DOWN);
				}
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				cmdq_push(CMD_USE);
			}
		} else if (event.mouse.mods & KC_MOD_ALT) {
			/* alt-click - show char screen */
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				Term_keypress('C', 0);
			}
		} else {
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				if (square_object(cave, y, x)) {
					cmdq_push(CMD_PICKUP);
				} else {
					cmdq_push(CMD_HOLD);
				}
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				/* Show a context menu */
				context_menu_player(loc(event.mouse.x, event.mouse.y));
			}
		}
	} else if (event.mouse.button == MOUSE_BUTTON_LEFT) {
		if (player->timed[TMD_CONFUSED]) {
			cmdq_push(CMD_WALK);
		} else {
			if (event.mouse.mods & KC_MOD_SHIFT) {
				/* shift-click - run */
				cmdq_push(CMD_RUN);
				cmd_set_arg_direction(cmdq_peek(),
						"direction", coords_to_dir(y, x));
			} else if (event.mouse.mods & KC_MOD_CONTROL) {
				/* control-click - alter */
				cmdq_push(CMD_ALTER);
				cmd_set_arg_direction(cmdq_peek(),
						"direction", coords_to_dir(y, x));
			} else if (event.mouse.mods & KC_MOD_ALT) {
				/* alt-click - look */
				if (target_set_interactive(TARGET_LOOK, loc(x, y))) {
					msg("Target Selected.");
				}
			} else {
				/* Pathfind does not work well on trap detection borders,
				 * so if the click is next to the player, force a walk step */
				if ((x - player->px >= -1) && (x - player->px <= 1)
						&& (y - player->py >= -1) && (y - player->py <= 1))
				{
					cmdq_push(CMD_WALK);
					cmd_set_arg_direction(cmdq_peek(),
							"direction", coords_to_dir(y, x));
				} else {
					cmdq_push(CMD_PATHFIND);
					cmd_set_arg_point(cmdq_peek(), "point", y, x);
				}
			}
		}
	} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
		struct monster *mon = square_monster(cave, y, x);
		if (mon && target_able(mon)) {
			/* Set up target information */
			monster_race_track(player->upkeep, mon->race);
			health_track(player->upkeep, mon);
			target_set_monster(mon);
		} else {
			target_set_location(y, x);
		}

		if (event.mouse.mods & KC_MOD_SHIFT) {
			/* shift-click - cast spell at target */
			cmdq_push(CMD_CAST);
			cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
		} else if (event.mouse.mods & KC_MOD_CONTROL) {
			/* control-click - fire at target */
			cmdq_push(CMD_USE);
			cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
		} else if (event.mouse.mods & KC_MOD_ALT) {
			/* alt-click - throw at target */
			cmdq_push(CMD_THROW);
			cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
		} else {
			/* see if the click was adjacent to the player */
			if ((x - player->px >= -1) && (x - player->px <= 1)
					&& (y - player->py >= -1) && (y - player->py <= 1))
			{
				context_menu_cave(cave,
						loc(x, y), true, loc(event.mouse.x, event.mouse.y));
			} else {
				context_menu_cave(cave,
						loc(x, y), false, loc(event.mouse.x, event.mouse.y));
			}
		}
	}
}

/**
 * ------------------------------------------------------------------------
 * Menu functions
 * ------------------------------------------------------------------------ */

static char cmd_sub_tag(struct menu *menu, int index)
{
	struct cmd_info *commands = menu_priv(menu);

	return commands[index].key[KEYMAP_MODE_OPT];
}

/**
 * Display an entry on a command menu
 */
static void cmd_sub_entry(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	const int mode = KEYMAP_MODE_OPT;

	struct cmd_info *commands = menu_priv(menu);

	uint32_t attr = menu_row_style(true, cursor);

	Term_adds(loc.x, loc.y, width, attr, commands[index].desc);

	/* Include keypress */
	Term_putwc(attr, L' ');
	Term_putwc(attr, L'(');

	struct keypress key = {
		.type = EVT_KBRD,
		.code = commands[index].key[mode],
		.mods = 0
	};
	/* Get readable version */
	char buf[16] = {0};
	keypress_to_readable(buf, sizeof(buf), key);
	Term_puts(sizeof(buf), attr, buf);

	Term_putwc(attr, L')');
}

/**
 * Display a list of commands.
 */
static bool cmd_menu(struct command_list *list, void *selection_p)
{
	struct menu menu;

	menu_iter commands_menu = {
		.get_tag     = cmd_sub_tag,
		.display_row = cmd_sub_entry
	};

	struct cmd_info **selection = selection_p;

	/* Set up the menu */
	menu_init(&menu, MN_SKIN_SCROLL, &commands_menu);
	menu_setpriv(&menu, list->len, list->list);
	mnflag_on(menu.flags, MN_NO_DISPLAY_TAGS);

	int maxlen = 0;
	for (size_t i = 0; i < list->len; i++) {
		int len = strlen(list->list[i].desc);
		if (len > maxlen) {
			maxlen = len;
		}
	}

	struct term_hints hints = {
		.width = maxlen + 8, /* 8 should be enough for additional chars */
		.height = list->len,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	menu_layout_term(&menu);

	ui_event event = menu_select(&menu);

	Term_pop();

	if (event.type == EVT_SELECT) {
		*selection = &list->list[menu.cursor];
		return false;
	} else {
		return true;
	}
}

static bool cmd_list_action(struct menu *m, const ui_event *event, int index)
{
	if (event->type == EVT_SELECT) {
		return cmd_menu(&cmds_all[index], menu_priv(m));
	} else {
		return false;
	}
}

static void cmd_list_entry(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) menu;

	Term_adds(loc.x, loc.y, width,
			menu_row_style(true, cursor), cmds_all[index].name);
}

static menu_iter command_menu_iter = {
	.display_row = cmd_list_entry,
	.row_handler = cmd_list_action
};

/**
 * Display a list of command types, allowing the user to select one.
 */
struct cmd_info *textui_action_menu_choose(void)
{
	struct menu command_menu;
	struct cmd_info *chosen_command = NULL;

	int count = 0;
	int maxlen = 0;
	while (cmds_all[count].list != NULL) {
		if (cmds_all[count].len) {
			int len = strlen(cmds_all[count].name);
			if (len > maxlen) {
				maxlen = len;
			}
			count++;
		}
	};

	menu_init(&command_menu, MN_SKIN_SCROLL, &command_menu_iter);
	menu_setpriv(&command_menu, count, &chosen_command);

	command_menu.selections = lower_case;

	const char *menu_tab = "Command groups";
	const int tablen = strlen(menu_tab);

	struct term_hints hints = {
		/* add 3 to maxlen to account for menu tags,
		 * add 1 to tablen to make it look better */
		.width = MAX(maxlen + 3, tablen + 1),
		.height = count,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, menu_tab, COLOUR_WHITE, COLOUR_DARK);
	menu_layout_term(&command_menu);

	menu_select(&command_menu);

	Term_pop();

	return chosen_command;
}
