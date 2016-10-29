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

/**
 * Additional constants for menu item values.
 * The values must not collide with the cmd_code enum,
 * since those are the main values for these menu items.
 */
enum context_menu_value_e {
	MENU_VALUE_INSPECT = CMD_REPEAT + 1000,
	MENU_VALUE_DROP_ALL,
	MENU_VALUE_LOOK,
	MENU_VALUE_TARGET,
	MENU_VALUE_RECALL,
	MENU_VALUE_REST,
	MENU_VALUE_REST_HP_AND_SP,
	MENU_VALUE_REST_HP_OR_SP,
	MENU_VALUE_REST_AS_NEEDED,
	MENU_VALUE_REST_KEYPRESS,
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
	MENU_VALUE_COMMANDS,
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

static void context_menu_player_other(struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;
	struct menu *m = menu_dynamic_new();
	
	mnflag_on(m->flags, MN_NO_TAGS);

	menu_dynamic_add(m, "Knowledge", MENU_VALUE_KNOWLEDGE);
	menu_dynamic_add(m, "Show Map", MENU_VALUE_MAP);
	menu_dynamic_add(m, "Show Messages", MENU_VALUE_MESSAGES);
	menu_dynamic_add(m, "Show Monster List", MENU_VALUE_MONSTERS);
	menu_dynamic_add(m, "Show Object List", MENU_VALUE_OBJECTS);
	menu_dynamic_add(m, "Toggle Ignored", MENU_VALUE_TOGGLE_IGNORED);
	menu_dynamic_add(m, "Ignore an item", CMD_IGNORE);
	menu_dynamic_add(m, "Options", MENU_VALUE_OPTIONS);
	menu_dynamic_add(m, "Commands", MENU_VALUE_COMMANDS);

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	clear_prompt();
	Term_pop();

	bool allowed = false;

	/* Check the command to see if it is allowed. */
	switch (selected) {
		case -1:
			break; /* User cancelled the menu. */

		case MENU_VALUE_KNOWLEDGE:
		case MENU_VALUE_MAP:
		case MENU_VALUE_MESSAGES:
		case MENU_VALUE_TOGGLE_IGNORED:
		case MENU_VALUE_COMMANDS:
		case MENU_VALUE_MONSTERS:
		case MENU_VALUE_OBJECTS:
		case MENU_VALUE_OPTIONS:
			allowed = true;
			break;

		case CMD_IGNORE:
			allowed = key_confirm_command(cmd_lookup_key(selected, mode));
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
			Term_keypress(cmd_lookup_key(selected, mode), 0);
			break;

		case MENU_VALUE_TOGGLE_IGNORED:
			Term_keypress(mode == KEYMAP_MODE_ORIG ? 'K' : 'O', 0);
			break;

		case MENU_VALUE_COMMANDS:
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

static void context_rest(int choice)
{
	cmdq_push(CMD_REST);
	cmd_set_arg_choice(cmdq_peek(), "choice", choice);
}

static void context_menu_player_rest(struct loc mloc)
{
	struct menu *m = menu_dynamic_new();
	
	mnflag_on(m->flags, MN_NO_TAGS);

	menu_dynamic_add(m, "For HP and SP", MENU_VALUE_REST_HP_AND_SP);
	menu_dynamic_add(m, "For HP or SP",  MENU_VALUE_REST_HP_OR_SP);
	menu_dynamic_add(m, "As needed",     MENU_VALUE_REST_AS_NEEDED);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	Term_pop();

	switch (selected) {
		case MENU_VALUE_REST_AS_NEEDED: context_rest(REST_COMPLETE);    break;
		case MENU_VALUE_REST_HP_AND_SP: context_rest(REST_ALL_POINTS);  break;
		case MENU_VALUE_REST_HP_OR_SP:  context_rest(REST_SOME_POINTS); break;
	}
}

static void context_menu_player_display_floor(void)
{
	int diff = weight_remaining(player);

	show_prompt(format("(Inventory) Burden %d.%d lb (%d.%d lb %s). Item for command:",
				player->upkeep->total_weight / 10,
				player->upkeep->total_weight % 10,
				abs(diff) / 10, abs(diff) % 10,
				(diff < 0 ? "overweight" : "remaining")), false);


	struct object *obj;
	player->upkeep->command_wrk = USE_FLOOR;

	if (get_item(&obj, NULL, NULL, CMD_NULL, NULL,
				USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | SHOW_EMPTY | IS_HARMLESS))
	{
		track_object(player->upkeep, obj);
		context_menu_object(obj);
	}
}

static void context_menu_player_entries(struct menu *m)
{
	menu_dynamic_add(m, "Use", CMD_USE);

	/* if player can cast, add casting option */
	if (player_can_cast(player, false)) {
		menu_dynamic_add(m, "Cast", CMD_CAST);
	}

	/* if player is on stairs add option to use them */
	if (square_isupstairs(cave, player->py, player->px)) {
		menu_dynamic_add(m, "Go Up", CMD_GO_UP);
	} else if (square_isdownstairs(cave, player->py, player->px)) {
		menu_dynamic_add(m, "Go Down", CMD_GO_DOWN);
	}

	menu_dynamic_add(m, "Look", MENU_VALUE_LOOK);
	menu_dynamic_add(m, "Rest", MENU_VALUE_REST);
	menu_dynamic_add(m, "Inventory", MENU_VALUE_INVENTORY);

	{
		/* if object under player add pickup option */
		struct object *obj = square_object(cave, player->py, player->px);
		if (obj && !ignore_item_ok(obj)) {
			menu_dynamic_add(m, "Floor", MENU_VALUE_FLOOR);
			menu_dynamic_add(m, "Pick up", CMD_PICKUP);
		}
	}

	menu_dynamic_add(m, "Character", MENU_VALUE_CHARACTER);

	if (!OPT(player, center_player)) {
		menu_dynamic_add(m, "Center Map", MENU_VALUE_CENTER_MAP);
	}

	menu_dynamic_add(m, "Other", MENU_VALUE_OTHER);
}

void context_menu_player(struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;
	struct menu *m = menu_dynamic_new();

	mnflag_on(m->flags, MN_NO_TAGS);
	context_menu_player_entries(m);

	show_prompt("(Enter to select, ESC) Command:", false);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	clear_prompt();
	Term_pop();

	bool allowed = false;

	/* Check the command to see if it is allowed. */
	switch(selected) {
		case -1:
			break; /* User cancelled the menu. */

		case CMD_USE:
		case CMD_CAST:
		case CMD_GO_UP:
		case CMD_GO_DOWN:
		case CMD_PICKUP:
			allowed = key_confirm_command(cmd_lookup_key(selected, mode));
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
			Term_keypress(cmd_lookup_key(selected, mode), 0);
			break;

		case CMD_GO_UP:
		case CMD_GO_DOWN:
		case CMD_PICKUP:
			cmdq_push(selected);
			break;

		case MENU_VALUE_REST:
			context_menu_player_rest(mloc);
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
			context_menu_player_other(mloc);
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

static void context_menu_cave_entries(struct menu *m,
		struct chunk *c, struct loc loc, bool adjacent)
{
	menu_dynamic_add(m, "Target", MENU_VALUE_TARGET);
	menu_dynamic_add(m, "Look At", MENU_VALUE_LOOK);

	if (c->squares[loc.y][loc.x].mon) {
		/* '/' is used for recall in both keymaps. */
		menu_dynamic_add(m, "Recall Info", MENU_VALUE_RECALL);
	}

	menu_dynamic_add(m, "Use Item On", CMD_USE);

	if (player_can_cast(player, false)) {
		menu_dynamic_add(m, "Cast On", CMD_CAST);
	}

	if (adjacent) {
		menu_dynamic_add(m,
				c->squares[loc.y][loc.x].mon ? "Attack" : "Alter",
				CMD_ALTER);

		struct object *chest = chest_check(loc.y, loc.x, CHEST_ANY);

		if (chest && !ignore_item_ok(chest)) {
			if (chest->known->pval) {
				if (is_locked_chest(chest)) {
					menu_dynamic_add(m, "Disarm Chest", CMD_DISARM);
					menu_dynamic_add(m, "Open Chest", CMD_OPEN);
				} else {
					menu_dynamic_add(m, "Open Disarmed Chest", CMD_OPEN);
				}
			} else {
				menu_dynamic_add(m, "Open Chest", CMD_OPEN);
			}
		}

		if (square_isknowntrap(c, loc.y, loc.x)) {
			menu_dynamic_add(m, "Disarm", CMD_DISARM);
			menu_dynamic_add(m, "Jump Onto", CMD_JUMP);
		}

		if (square_isopendoor(c, loc.y, loc.x)) {
			menu_dynamic_add(m, "Close", CMD_CLOSE);
		} else if (square_iscloseddoor(c, loc.y, loc.x)) {
			menu_dynamic_add(m, "Open", CMD_OPEN);
			menu_dynamic_add(m, "Lock", CMD_DISARM);
		} else if (square_isdiggable(c, loc.y, loc.x)) {
			menu_dynamic_add(m, "Tunnel", CMD_TUNNEL);
		}

		menu_dynamic_add(m, "Walk Towards", CMD_WALK);
	} else {
		menu_dynamic_add(m, "Pathfind To", CMD_PATHFIND);
		menu_dynamic_add(m, "Walk Towards", CMD_WALK);
		menu_dynamic_add(m, "Run Towards", CMD_RUN);
	}

	if (player_can_fire(player, false)) {
		menu_dynamic_add(m, "Fire On", CMD_FIRE);
	}

	menu_dynamic_add(m, "Throw To", CMD_THROW);

#define PROMPT(str) \
	("(Enter to select command, ESC to cancel) " str)

	struct object *square_obj = square_object(c, loc.y, loc.x);

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
}

static void context_menu_recall(struct chunk *c, struct loc loc)
{
	struct monster *mon = square_monster(c, loc.y, loc.x);

	if (mon != NULL) {
		struct monster_lore *lore = get_lore(mon->race);
		lore_show_interactive(mon->race, lore);
	}
}

static void context_menu_target(struct chunk *c, struct loc loc)
{
	struct monster *mon = square_monster(c, loc.y, loc.x);

	if (mon && target_able(mon)) {
		monster_race_track(player->upkeep, mon->race);
		health_track(player->upkeep, mon);
		target_set_monster(mon);
	} else {
		target_set_location(loc.y, loc.x);
	}
}

void context_menu_cave(struct chunk *c,
		struct loc loc, bool adjacent, struct loc mloc)
{
	const int mode = KEYMAP_MODE_OPT;
	struct menu *m = menu_dynamic_new();

	mnflag_on(m->flags, MN_NO_TAGS);
	context_menu_cave_entries(m, c, loc, adjacent);

	struct term_hints hints = context_term_hints(m, mloc);
	Term_push_new(&hints);
	menu_layout_term(m);

	int selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	clear_prompt();
	Term_pop();

	bool allowed = false;

	/* Check the command to see if it is allowed. */
	switch (selected) {
		case -1:
			break; /* User cancelled the menu. */

		case MENU_VALUE_LOOK:
		case MENU_VALUE_TARGET:
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
			allowed = key_confirm_command(cmd_lookup_key(selected, mode));
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
		case MENU_VALUE_TARGET:
			context_menu_target(c, loc);
			break;

		case MENU_VALUE_LOOK:
			/* Look at the spot */
			if (target_set_interactive(TARGET_LOOK, loc)) {
				msg("Target Selected.");
			}
			break;

		case MENU_VALUE_RECALL:
			context_menu_recall(c, loc);
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
					"direction", coords_to_dir(player, loc.y, loc.x));
			break;

		case CMD_CAST:
		case CMD_FIRE:
		case CMD_THROW:
		case CMD_USE:
			context_menu_target(c, loc);
			cmdq_push(selected);
			cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
			break;

		default:
			break;
	}
}

static void context_menu_object_create(struct menu *m, struct object *obj)
{
/* 2 for menu padding on the right side */
#define CONTEXT_MENU_OBJECT_WIDTH \
	(menu_reg.w + 2)

	region menu_reg = menu_dynamic_calc_location(m);
	textblock *tb = object_info(obj, OINFO_NONE);

	int textblock_width =
		ANGBAND_TERM_STANDARD_WIDTH - CONTEXT_MENU_OBJECT_WIDTH;
	assert(textblock_width > 0);

	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	size_t lines = textblock_calculate_lines(tb,
			&line_starts, &line_lengths, textblock_width);

	/* Remove empty lines from the end of textblock */
	while (lines > 0 && line_lengths[lines - 1] == 0) {
		lines--;
	}

	mem_free(line_starts);
	mem_free(line_lengths);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = MAX(menu_reg.h, (int) lines),
		.tabs = true,
		.position = TERM_POSITION_TOP_LEFT,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);

	char tab[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(tab, sizeof(tab), obj, ODESC_PREFIX | ODESC_BASE);

	Term_add_tab(0, tab, COLOUR_WHITE, COLOUR_DARK);

	region textblock_reg = {
		.x = CONTEXT_MENU_OBJECT_WIDTH,
		.y = 0,
		.w = textblock_width,
		.h = 0
	};
	textui_textblock_place(tb, textblock_reg, NULL);

	menu_layout(m, menu_reg);

#undef CONTEXT_MENU_OBJECT_WIDTH
}

static void context_menu_object_destroy(struct menu *m)
{
	menu_dynamic_free(m);
	Term_pop();
}

static void context_menu_object_entry(struct menu *m, char *labels,
		bool valid, const char *text, cmd_code cmd, int mode)
{
	(void) labels;
	(void) mode;

	menu_dynamic_add_valid(m, text, cmd, valid);
}

static void context_menu_object_entries(struct menu *m, char *labels,
		struct object *obj, int mode)
{
	if (obj_can_browse(obj)) {
		if (obj_can_cast_from(obj) && player_can_cast(player, false)) {
			context_menu_object_entry(m, labels,
					true, "Cast", CMD_CAST, mode);
		}

		if (obj_can_study(obj) && player_can_study(player, false)) {
			context_menu_object_entry(m, labels,
					true, "Study", CMD_STUDY, mode);
		}

		if (player_can_read(player, false)) {
			context_menu_object_entry(m, labels,
					true, "Browse", CMD_BROWSE_SPELL, mode);
		}
	} else if (obj_is_useable(obj)) {
		if (tval_is_wand(obj)) {
			context_menu_object_entry(m, labels,
					obj_has_charges(obj), "Aim", CMD_USE_WAND, mode);
		} else if (tval_is_rod(obj)) {
			context_menu_object_entry(m, labels,
					obj_can_zap(obj), "Zap", CMD_USE_ROD, mode);
		} else if (tval_is_staff(obj)) {
			context_menu_object_entry(m, labels,
					obj_has_charges(obj), "Use", CMD_USE_STAFF, mode);
		} else if (tval_is_scroll(obj)) {
			context_menu_object_entry(m, labels,
					player_can_read(player, false), "Read", CMD_READ_SCROLL, mode);
		} else if (tval_is_potion(obj)) {
			context_menu_object_entry(m, labels,
					true, "Quaff", CMD_QUAFF, mode);
		} else if (tval_is_edible(obj)) {
			context_menu_object_entry(m, labels,
					true, "Eat", CMD_EAT, mode);
		} else if (obj_is_activatable(obj)) {
			context_menu_object_entry(m, labels,
					object_is_equipped(player->body, obj) && obj_can_activate(obj),
					"Activate", CMD_ACTIVATE, mode);
		} else if (obj_can_fire(obj)) {
			context_menu_object_entry(m, labels,
					true, "Fire", CMD_FIRE, mode);
		} else {
			context_menu_object_entry(m, labels,
					true, "Use", CMD_USE, mode);
		}
	}

	if (obj_can_refill(obj)) {
		context_menu_object_entry(m, labels,
				true, "Refill", CMD_REFILL, mode);
	}

	if (object_is_equipped(player->body, obj)
			&& obj_can_takeoff(obj))
	{
		context_menu_object_entry(m, labels,
				true, "Take off", CMD_TAKEOFF, mode);
	} else if (!object_is_equipped(player->body, obj)
			&& obj_can_wear(obj))
	{
		context_menu_object_entry(m, labels,
				true, "Equip", CMD_WIELD, mode);
	}

	if (object_is_carried(player, obj)) {
		if (!square_isshop(cave, player->py, player->px)) {
			context_menu_object_entry(m, labels,
					true, "Drop", CMD_DROP, mode);
			if (obj->number > 1) {
				context_menu_object_entry(m, labels,
						true, "Drop All", MENU_VALUE_DROP_ALL, mode);
			}
		} else if (square_shopnum(cave, player->py, player->px) == STORE_HOME) {
			context_menu_object_entry(m, labels,
					true, "Drop", CMD_DROP, mode);
			if (obj->number > 1) {
				context_menu_object_entry(m, labels,
						true, "Drop All", MENU_VALUE_DROP_ALL, mode);
			}
		} else if (store_will_buy_tester(obj)) {
			context_menu_object_entry(m, labels, true, "Sell", CMD_DROP, mode);
		}
	} else {
		context_menu_object_entry(m, labels,
				inven_carry_okay(obj), "Pick up", CMD_PICKUP, mode);
	}

	context_menu_object_entry(m, labels, true, "Throw", CMD_THROW, mode);
	context_menu_object_entry(m, labels, true, "Inscribe", CMD_INSCRIBE, mode);

	if (obj_has_inscrip(obj)) {
		context_menu_object_entry(m, labels,
				true, "Uninscribe", CMD_UNINSCRIBE, mode);
	}

	context_menu_object_entry(m, labels,
			true,
			object_is_ignored(obj) ? "Unignore" : "Ignore",
			CMD_IGNORE, mode);
}

/**
 * Pick the context menu options appropiate for the item.
 * Returns true when user selected a command that must be done.
 */
bool context_menu_object(struct object *obj)
{
	const int mode = KEYMAP_MODE_OPT;
	struct menu *m = menu_dynamic_new();

	char labels[32];
	my_strcpy(labels, lower_case, sizeof(labels));

	m->selections = labels;
	context_menu_object_entries(m, labels, obj, mode);

	show_prompt_h(format("Item commands: (`%c`-`%c`, ESC)",
				labels[0], labels[MAX(0, m->count - 1)]),
			false, COLOUR_WHITE, COLOUR_TEAL);
	context_menu_object_create(m, obj);

	int selected = menu_dynamic_select(m);

	context_menu_object_destroy(m);
	clear_prompt();

	bool allowed = false;

	switch (selected) {
		case -1:
			return false; /* User cancelled the menu. */

		case MENU_VALUE_DROP_ALL:
			if (square_isshop(cave, player->py, player->px)) {
				cmdq_push(CMD_STASH);
			} else {
				cmdq_push(CMD_DROP);
			}
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", obj->number);
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
			allowed = key_confirm_command(cmd_lookup_key(selected, mode))
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
		textui_cmd_ignore_menu(obj);
	} else if (selected == CMD_BROWSE_SPELL) {
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

		/* If we're in a store, change the "drop" command to "stash" or "sell". */
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

	mnflag_on(m->flags, MN_NO_TAGS);

	for (int i = 0; i < size; i++) {
		menu_dynamic_add(m, cmd_list[i].desc, i + 1);
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

	mnflag_on(m->flags, MN_NO_TAGS);

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

static bool is_adjacent_to_player(struct loc coords)
{
	int relx = coords.x - player->px;
	int rely = coords.y - player->py;

	return relx >= -1 && relx <= 1
		&& rely >= -1 && rely <= 1;
}

static void textui_left_click(struct mouseclick mouse, struct loc coords)
{
	if (player->timed[TMD_CONFUSED]) {
		cmdq_push(CMD_WALK);
	} else {
		if (mouse.mods & KC_MOD_SHIFT) {
			/* shift-click - run */
			cmdq_push(CMD_RUN);
			cmd_set_arg_direction(cmdq_peek(),
					"direction", coords_to_dir(player, coords.y, coords.x));
		} else if (mouse.mods & KC_MOD_CONTROL) {
			/* control-click - alter */
			cmdq_push(CMD_ALTER);
			cmd_set_arg_direction(cmdq_peek(),
					"direction", coords_to_dir(player, coords.y, coords.x));
		} else if (mouse.mods & KC_MOD_ALT) {
			/* alt-click - look */
			if (target_set_interactive(TARGET_LOOK, coords)) {
				msg("Target Selected.");
			}
		} else {
			 /* normal click - take a step or travel */
			if (is_adjacent_to_player(coords)) {
				cmdq_push(CMD_WALK);
				cmd_set_arg_direction(cmdq_peek(),
						"direction", coords_to_dir(player, coords.y, coords.x));
			} else {
				cmdq_push(CMD_PATHFIND);
				cmd_set_arg_point(cmdq_peek(), "point", coords.y, coords.x);
			}
		}
	}
}

static void textui_right_click(struct mouseclick mouse, struct loc coords)
{
	if (mouse.mods & KC_MOD_SHIFT) {
		/* shift-click - cast spell at target */
		cmdq_push(CMD_CAST);
		cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
	} else if (mouse.mods & KC_MOD_CONTROL) {
		/* control-click - fire at target */
		cmdq_push(CMD_USE);
		cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
	} else if (mouse.mods & KC_MOD_ALT) {
		/* alt-click - throw at target */
		cmdq_push(CMD_THROW);
		cmd_set_arg_target(cmdq_peek(), "target", DIR_TARGET);
	} else {
		struct loc click = {mouse.x, mouse.y};
		/* normal click - open a menu */
		if (is_adjacent_to_player(coords)) {
			context_menu_cave(cave, coords, true, click);
		} else {
			context_menu_cave(cave, coords, false, click);
		}
	}
}

static void textui_player_click(struct mouseclick mouse, struct loc coords)
{
	if (mouse.mods & KC_MOD_SHIFT) {
		/* shift-click - cast magic or view inventory */
		if (mouse.button == MOUSE_BUTTON_LEFT) {
			cmdq_push(CMD_CAST);
		} else if (mouse.button == MOUSE_BUTTON_RIGHT) {
			Term_keypress('i', 0);
		}
	} else if (mouse.mods & KC_MOD_CONTROL) {
		/* ctrl-click - use stairs or use inventory item */
		if (mouse.button == MOUSE_BUTTON_LEFT) {
			if (square_isupstairs(cave, coords.y, coords.x)) {
				cmdq_push(CMD_GO_UP);
			} else if (square_isdownstairs(cave, coords.y, coords.x)) {
				cmdq_push(CMD_GO_DOWN);
			}
		} else if (mouse.button == MOUSE_BUTTON_RIGHT) {
			cmdq_push(CMD_USE);
		}
	} else if (mouse.mods & KC_MOD_ALT) {
		/* alt-click - show char screen */
		if (mouse.button == MOUSE_BUTTON_LEFT) {
			Term_keypress('C', 0);
		}
	} else {
		/* normal click - pickup item, spend a turn or open a menu */
		if (mouse.button == MOUSE_BUTTON_LEFT) {
			if (square_object(cave, coords.y, coords.x)) {
				cmdq_push(CMD_PICKUP);
			} else {
				cmdq_push(CMD_HOLD);
			}
		} else if (mouse.button == MOUSE_BUTTON_RIGHT) {
			struct loc click = {mouse.x, mouse.y};
			context_menu_player(click);
		}
	}
}

/**
 * Handle a mouseclick.
 */
void textui_process_click(ui_event event)
{
	assert(event.type == EVT_MOUSE);

	if (OPT(player, mouse_movement)) {
		struct mouseclick mouse = event.mouse;
		struct loc coords = {
			.x = map_grid_x(mouse.x),
			.y = map_grid_y(mouse.y)
		};

		if (square_in_bounds_fully(cave, coords.y, coords.x)) {
			if (player->px == coords.x && player->py == coords.y) {
				textui_player_click(mouse, coords);
			} else if (mouse.button == MOUSE_BUTTON_LEFT) {
				textui_left_click(mouse, coords);
			} else if (mouse.button == MOUSE_BUTTON_RIGHT) {
				textui_right_click(mouse, coords);
			}
		}
	}
}

/**
 * ------------------------------------------------------------------------
 * Menu functions
 * ------------------------------------------------------------------------
 */

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
	struct cmd_info *commands = menu_priv(menu);

	uint32_t attr = menu_row_style(true, cursor);

	Term_adds(loc.x, loc.y, width, attr, commands[index].desc);

	struct keypress key = {
		.type = EVT_KBRD,
		.code = commands[index].key[KEYMAP_MODE_OPT],
		.mods = 0
	};
	/* Get readable version */
	char buf[16] = {0};
	keypress_to_readable(buf, sizeof(buf), key);

	Term_cursor_to_xy(loc.x + width - (int) strlen(buf) - 2, loc.y);

	Term_putwc(COLOUR_L_DARK, L'(');
	Term_puts(sizeof(buf), attr, buf);
	Term_putwc(COLOUR_L_DARK, L')');
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
	mnflag_on(menu.flags, MN_PVT_TAGS);

	int maxlen = 0;
	for (size_t i = 0; i < list->len; i++) {
		int len = strlen(list->list[i].desc);
		if (len > maxlen) {
			maxlen = len;
		}
	}

	const char *menu_tab = list->name;
	const int tablen = strlen(menu_tab);

	Term_visible(false);

	struct term_hints hints = {
		/* add 8 to maxlen to make room for command keys
		 * add 1 to tablen to make it look better */
		.width = MAX(maxlen + 8, tablen + 1),
		.height = list->len,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_TOP_LEFT
	};
	Term_push_new(&hints);
	Term_add_tab(0, menu_tab, COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(&menu);

	ui_event event = menu_select(&menu);

	Term_pop();
	Term_visible(true);

	if (event.type == EVT_SELECT) {
		*selection = &list->list[menu.cursor];
		return false;
	} else {
		return true;
	}
}

static bool cmd_list_action(struct menu *menu, const ui_event *event, int index)
{
	if (event->type == EVT_SELECT) {
		return cmd_menu(&cmds_all[index], menu_priv(menu));
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
		.position = TERM_POSITION_TOP_LEFT
	};
	Term_push_new(&hints);
	Term_add_tab(0, menu_tab, COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(&command_menu);
	menu_select(&command_menu);

	Term_pop();

	return chosen_command;
}
