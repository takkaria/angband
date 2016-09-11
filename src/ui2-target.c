/**
 * \file ui2-target.c
 * \brief UI for targetting code
 *
 * Copyright (c) 1997-2016 Angband contributors
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
#include "game-input.h"
#include "init.h"
#include "mon-desc.h"
#include "mon-lore.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "project.h"
#include "target.h"
#include "trap.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-map.h"
#include "ui2-mon-lore.h"
#include "ui2-object.h"
#include "ui2-output.h"
#include "ui2-target.h"
#include "ui2-term.h"

/**
 * Extract a direction (or zero) from a character
 */
int target_dir(struct keypress key)
{
	return target_dir_allow(key, false);
}

int target_dir_allow(struct keypress key, bool allow_5)
{
	int dir = 0;

	if (isdigit((unsigned char) key.code)) {
		dir = D2I(key.code);
	} else if (isarrow(key.code)) {
		switch (key.code) {
			case ARROW_DOWN:  dir = 2; break;
			case ARROW_LEFT:  dir = 4; break;
			case ARROW_RIGHT: dir = 6; break;
			case ARROW_UP:    dir = 8; break;
		}
	} else {
		/* See if this key has a digit in the keymap we can use */
		for (const struct keypress *act = keymap_find(KEYMAP_MODE_OPT, key);
				act != NULL && act->type != EVT_NONE;
				act++)
		{
			if (isdigit((unsigned char) act->code)) {
				dir = D2I(act->code);
			}
		}
	}

	if (dir == 5 && !allow_5) {
		dir = 0;
	}

	return dir;
}

/**
 * Display targeting help at the bottom of the screen.
 */
static void target_display_help_aux(bool monster, bool free)
{
	/* Determine help location */
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = 3,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	struct text_out_info info = {
		.indent = 1
	};

	/* Display help */
	text_out_c(info, COLOUR_L_GREEN, "<dir>");
	text_out(info, " and ");
	text_out_c(info, COLOUR_L_GREEN, "<click>");
	text_out(info, " look around. ");
	text_out_c(info, COLOUR_L_GREEN, "g");
	text_out(info, " moves to the selection. ");
	text_out_c(info, COLOUR_L_GREEN, "p");
	text_out(info, " selects the player. ");
	text_out_c(info, COLOUR_L_GREEN, "q");
	text_out(info, " exits. ");
	text_out_c(info, COLOUR_L_GREEN, "r");
	text_out(info, " displays details. ");

	if (free) {
		text_out_c(info, COLOUR_L_GREEN, "m");
		text_out(info, " restricts to interesting places. ");
	}
	else {
		text_out_c(info, COLOUR_L_GREEN, "+");
		text_out(info, " and ");
		text_out_c(info, COLOUR_L_GREEN, "-");
		text_out(info, " cycle through interesting places. ");
		text_out_c(info, COLOUR_L_GREEN, "o");
		text_out(info, " allows free selection. ");
	}
	
	if (monster || free) {
		text_out(info, "");
		text_out_c(info, COLOUR_L_GREEN, "t");
		text_out(info, " targets the current selection.");
	}

	Term_flush_output();

	inkey_any();

	Term_pop();
}

static void target_display_help(struct point_set *targets,
		struct loc coords, bool restricted)
{
	bool good_target = target_able(square_monster(cave, coords.y, coords.x));
	bool free_selection =
		(restricted && point_set_size(targets) > 0) ? false : true;

	target_display_help_aux(good_target, free_selection);
}

static bool target_look(int mode)
{
	return mode & TARGET_LOOK;
}

static bool target_kill(int mode)
{
	return mode & TARGET_KILL;
}

static bool is_target_recall_event(const ui_event *event, struct loc coords)
{
	switch (event->type) {
		case EVT_MOUSE:
			if (event->mouse.button == MOUSE_BUTTON_LEFT
					&& EVENT_GRID_X(*event) == coords.x
					&& EVENT_GRID_Y(*event) == coords.y)
			{
				return true;
			}
			break;

		case EVT_KBRD:
			if (event->key.code == 'r') {
				return true;
			}
			break;

		default:
			break;
	}

	return false;
}

static bool show_target_monster_recall(const struct monster *mon,
		const ui_event *event, struct loc coords)
{
	if (is_target_recall_event(event, coords)) {
		lore_show_interactive(mon->race, get_lore(mon->race));
		return true;
	} else {
		return false;
	}
}

static bool show_target_object_recall(const struct object *obj,
		const ui_event *event, struct loc coords)
{
	if (is_target_recall_event(event, coords)) {
		display_object_recall_interactive(cave->objects[obj->oidx]);
		return true;
	} else {
		return false;
	}
}

static bool is_target_stop_event(const ui_event *event, int mode)
{
	switch (event->type) {
		case EVT_MOUSE:
			if (event->mouse.button == MOUSE_BUTTON_RIGHT
					|| !target_look(mode))
			{
				return true;
			}
			break;

		case EVT_KBRD:
			if ((event->key.code != KC_ENTER && event->key.code != ' ')
					|| (event->key.code == ' ' && !target_look(mode)))
			{
				return true;
			}
			break;

		default:
			break;
	}

	return false;
}

struct desc {
	const char *a;
	const char *b;
	const char *c;
	char coords[20];
};

/**
 * Display the object name of the selected object and allow for full object
 * recall. Returns an event that occurred display.
 *
 * This will only work for a single object on the ground and not a pile. This
 * loop is similar to the monster recall loop in target_set_interactive_aux().
 *
 * \param obj is the object to describe.
 * \param coords is the cave (column, row) of the object.
 * \param buf is the string that holds the name of the object
 * \param size is the number of bytes in buf
 * \param desc is part of output string
 */
static ui_event target_recall_loop_object(struct object *obj,
		char *buf, size_t size,
		struct loc coords, const struct desc *desc)
{
	char o_name[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(o_name, sizeof(o_name),
			cave->objects[obj->oidx], ODESC_PREFIX | ODESC_FULL);

	if (player->wizard) {
		strnfmt(buf, size,
				"%s%s%s%s, %s (%d:%d, cost = %d, when = %d).",
				desc->a, desc->b, desc->c, o_name,
				desc->coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);
	} else {
		strnfmt(buf, size,
				"%s%s%s%s, %s.",
				desc->a, desc->b, desc->c, o_name, desc->coords);
	}

	show_prompt(buf, false);

	ui_event event;

	do {
		event = inkey_mouse_or_key();
	} while (show_target_object_recall(obj, &event, coords));

	return event;
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_halluc(ui_event *event,
		struct loc coords, const struct desc *desc)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	const char *strange = "something strange";

	/* Display a message */
	if (player->wizard) {
		strnfmt(buf, sizeof(buf),
				"%s%s%s%s, %s (%d:%d, cost = %d, when = %d).",
				desc->a, desc->b, desc->c, strange,
				desc->coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);
	} else {
		strnfmt(buf, sizeof(buf), "%s%s%s%s, %s.",
				desc->a, desc->b, desc->c, strange, desc->coords);
	}

	show_prompt(buf, false);

	event->key = inkey_only_key();

	/* Stop on everything but "enter" */
	return event->key.code != KC_ENTER;
}

static void target_desc_monster(char *buf, size_t size,
		const struct monster *mon, struct loc coords, const struct desc *desc)
{
	char mon_name[ANGBAND_TERM_STANDARD_WIDTH];
	monster_desc(mon_name, sizeof(mon_name), mon, MDESC_IND_VIS);

	char mon_health[ANGBAND_TERM_STANDARD_WIDTH];
	look_mon_desc(mon_health, sizeof(mon_health), cave->squares[coords.y][coords.x].mon);

	if (player->wizard) {
		strnfmt(buf, size,
				"%s%s%s%s (%s), %s (%d:%d, cost = %d, when = %d).",
				desc->a, desc->b, desc->c, mon_name, mon_health,
				desc->coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);
	} else {
		strnfmt(buf, size,
				"%s%s%s%s (%s), %s.",
				desc->a, desc->b, desc->c, mon_name, mon_health, desc->coords);
	}
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_monster_objects(ui_event *event,
		const struct monster *mon,
		char *buf, size_t size,
		struct loc coords, const struct desc *desc, int mode)
{
	struct desc copy = *desc;

	copy.b = "carrying ";

	for (const struct object *obj = mon->held_obj; obj; obj = obj->next) {
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];
		object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
		strnfmt(buf, size,
				"%s%s%s%s, %s (%d:%d, cost = %d, when = %d).",
				copy.a, copy.b, copy.c, o_name,
				copy.coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);

		show_prompt(buf, false);
		*event = inkey_mouse_or_key();

		if (is_target_stop_event(event, mode)) {
			return true;
		}

		copy.b = "also carrying ";
	}

	return false;
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_monster(ui_event *event,
		struct loc coords, struct desc *desc, int mode, bool *boring)
{
	if (cave->squares[coords.y][coords.x].mon <= 0) {
		return false;
	}

	struct monster *mon = square_monster(cave, coords.y, coords.x);

	if (!mflag_has(mon->mflag, MFLAG_VISIBLE)
			|| mflag_has(mon->mflag, MFLAG_UNAWARE))
	{
		return false;
	}

	*boring = false;

	monster_race_track(player->upkeep, mon->race);
	health_track(player->upkeep, mon);
	handle_stuff(player);

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	target_desc_monster(buf, sizeof(buf), mon, coords, desc); 

	show_prompt(buf, false);

	do {
		*event = inkey_mouse_or_key();
	} while (show_target_monster_recall(mon, event, coords));

	if (is_target_stop_event(event, mode)) {
		return true;
	}

	if (rf_has(mon->race->flags, RF_FEMALE)) {
		desc->a = "She is ";
	} else if (rf_has(mon->race->flags, RF_MALE)) {
		desc->a = "He is ";
	} else {
		desc->a = "It is ";
	}
	desc->b = "on ";
	desc->c = "";

	if (player->wizard) {
		return target_interactive_aux_monster_objects(event,
				mon, buf, sizeof(buf), coords, desc, mode);
	}

	return false;
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_trap(ui_event *event,
		struct loc coords, struct desc *desc, int mode, bool *boring)
{
	if (!square_isvisibletrap(cave, coords.y, coords.x)) {
		return false;
	}

	*boring = false;

	const struct trap *trap = cave->squares[coords.y][coords.x].trap;

	if (cave->squares[coords.y][coords.x].mon < 0) {
		desc->a = "You are ";
		desc->b = "on ";
	} else {
		desc->a = "You see ";
		desc->b = "";
	}
	desc->c = is_a_vowel(trap->kind->desc[0]) ? "an " : "a ";

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (player->wizard) {
		strnfmt(buf, sizeof(buf),
				"%s%s%s%s, %s (%d:%d, cost = %d, when = %d).",
				desc->a, desc->b, desc->c, trap->kind->name,
				desc->coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);
	} else {
		strnfmt(buf, sizeof(buf), "%s%s%s%s, %s.", 
				desc->a, desc->b, desc->c, trap->kind->desc, desc->coords);
	}

	show_prompt(buf, false);

	do {
		*event = inkey_mouse_or_key();
	} while (!is_target_stop_event(event, mode));

	return true;
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_objects(ui_event *event,
		struct object **floor_list, int floor_num,
		struct loc coords, struct desc *desc, int mode, bool *boring)
{
	/* if no objects
	 * or
	 * if character is blind and object is not directly under his feet */
	if (floor_num <= 0
			|| (player->timed[TMD_BLIND]
				&& (coords.x != player->px || coords.y != player->py)))
	{
		return false;
	}

	*boring = false;

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (floor_num > 1) {
		if (player->wizard) {
			strnfmt(buf, sizeof(buf),
					"%s%s%sa pile of %d objects, %s (%d:%d, cost = %d, when = %d).",
					desc->a, desc->b, desc->c, floor_num,
					desc->coords, coords.x, coords.y,
					(int) cave->squares[coords.y][coords.x].cost,
					(int) cave->squares[coords.y][coords.x].when);
		} else {
			strnfmt(buf, sizeof(buf),
					"%s%s%sa pile of %d objects, %s.",
					desc->a, desc->b, desc->c, floor_num, desc->coords);
		}

		show_prompt(buf, false);

		struct term_hints hints = {
			.width = ANGBAND_TERM_STANDARD_WIDTH,
			.height = floor_num,
			.purpose = TERM_PURPOSE_TEXT,
			.position = TERM_POSITION_TOP_CENTER
		};
		Term_push_new(&hints);
		show_floor(floor_list, floor_num,
				   (OLIST_WEIGHT | OLIST_GOLD), NULL);
		*event = inkey_mouse_or_key();
		Term_pop();
	} else {
		struct object *obj = floor_list[0];

		track_object(player->upkeep, obj);
		handle_stuff(player);

		*event = target_recall_loop_object(obj, buf, sizeof(buf), coords, desc);

		if (is_target_stop_event(event, mode)) {
			return true;
		}

		desc->a = VERB_AGREEMENT(obj->number, "It is ", "They are ");
		desc->b = "on ";
		desc->c = "";
	}

	return false;
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_interactive_aux_square(ui_event *event,
		struct loc coords, struct desc *desc, int mode, bool boring)
{
	if (!boring && !square_isinteresting(cave, coords.y, coords.x)) {
		return false;
	}

	const char *name = square_apparent_name(cave, player, coords.y, coords.x);

	if (square_isdoor(cave, coords.y, coords.x)) {
		desc->b = "in ";
	}
	desc->c = is_a_vowel(name[0]) ? "an " : "a ";

	if (square_isshop(cave, coords.y, coords.x)) {
		desc->c = "the entrance to the ";
	}

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (player->wizard) {
		strnfmt(buf, sizeof(buf),
				"%s%s%s%s, %s (%d:%d, cost = %d, when = %d).", 
				desc->a, desc->b, desc->c, name,
				desc->coords, coords.x, coords.y,
				(int) cave->squares[coords.y][coords.x].cost,
				(int) cave->squares[coords.y][coords.x].when);
	} else {
		strnfmt(buf, sizeof(buf),
				"%s%s%s%s, %s.", desc->a, desc->b, desc->c, name, desc->coords);
	}

	show_prompt(buf, false);
	*event = inkey_mouse_or_key();

	return is_target_stop_event(event, mode);
}

/* returns true when target_interactive_aux() needs to stop */
static bool target_aux_loop_stop(ui_event event)
{
	switch (event.type) {
		case EVT_MOUSE:
			if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				return true;
			}
			break;

		case EVT_KBRD:
			if (event.key.code != KC_ENTER) {
				return true;
			}
			break;

		default:
			break;
	}

	return false;
}

static void desc_init(struct desc *desc, struct loc coords)
{
	coords_desc(desc->coords, sizeof(desc->coords), coords.y, coords.x);

	if (cave->squares[coords.y][coords.x].mon < 0) {
		/* The player */
		desc->a = "You are ";
		desc->b = "on ";
		desc->c = "";
	} else {
		/* Something else */
		desc->a = "You see ";
		desc->b = "";
		desc->c = "";
	}

}

/**
 * Examine a grid, return a keypress.
 *
 * The mode argument contains the "TARGET_LOOK" bit flag, which
 * indicates that the space key should scan through the contents
 * of the grid, instead of simply returning immediately.  This lets
 * the look command get complete information, without making the
 * target command annoying.
 *
 * Note that if a monster is in the grid, we update both the monster
 * recall info and the health bar info to track that monster.
 *
 * This function correctly handles multiple objects per grid, and objects
 * and terrain features in the same grid, though the latter never happens.
 *
 * This function must handle blindness/hallucination.
 */
static ui_event target_set_interactive_aux(struct loc coords, int mode)
{
	move_cursor_relative(DISPLAY_CAVE, coords, true);

	struct object **floor_list =
		mem_zalloc(z_info->floor_size * sizeof(*floor_list));
	int floor_num =
		scan_distant_floor(floor_list, z_info->floor_size, coords.y, coords.x);

	ui_event event;

	do {
		struct desc desc = {0};
		bool boring = true;

		desc_init(&desc, coords);

		if (player->timed[TMD_IMAGE]) {
			if (target_interactive_aux_halluc(&event, coords, &desc)) {
				break;
			} else {
				continue;
			}
		}

		if (target_interactive_aux_monster(&event, coords, &desc, mode, &boring)) {
			break;
		}
		if (target_interactive_aux_trap(&event, coords, &desc, mode, &boring)) {
			break;
		}
		if (target_interactive_aux_objects(&event, floor_list, floor_num,
					coords, &desc, mode, &boring)) {
			break;
		}
		if (target_interactive_aux_square(&event, coords, &desc, mode, boring)) {
			break;
		}

	} while (!target_aux_loop_stop(event));

	mem_free(floor_list);

	return event;
}

/**
 * Target command
 */
void textui_target(void)
{
	if (target_set_interactive(TARGET_KILL, loc(-1, -1))) {
		msg("Target Selected.");
	} else {
		msg("Target Aborted.");
	}
}

/**
 * Target closest monster.
 */
void textui_target_closest(void)
{
	if (target_set_closest(TARGET_KILL)) {
		display_term_push(DISPLAY_CAVE);

		int x, y;
		target_get(&x, &y);

		bool visible;
		Term_get_cursor(NULL, NULL, &visible, NULL);
		Term_cursor_visible(true);

		move_cursor_relative(DISPLAY_CAVE, loc(x, y), true);

		Term_redraw_screen();
		Term_delay(TARGET_CLOSEST_DELAY);

		Term_cursor_visible(visible);
		Term_flush_output();

		display_term_pop();
	}
}

static uint32_t draw_path_get_color(struct loc loc)
{
	uint32_t color = COLOUR_WHITE;

	struct monster *mon = square_monster(cave, loc.y, loc.x);
	struct object *obj = square_object(player->cave, loc.y, loc.x);

	if (mon && mflag_has(mon->mflag, MFLAG_VISIBLE)) {
		if (rf_has(mon->race->flags, RF_UNAWARE)) {
			/* Mimics act as objects */
			color = COLOUR_YELLOW;
		} else {
			/* Visible monsters are red. */
			color = COLOUR_L_RED;
		}
	} else if (obj) {
		/* Known objects are yellow. */
		color = COLOUR_YELLOW;
	} else if (!square_isprojectable(cave, loc.y, loc.x)
			&& (square_isknown(cave, loc.y, loc.x) || square_isseen(cave, loc.y, loc.x)))
	{
		/* Known walls are blue. */
		color = COLOUR_BLUE;
	} else if (!square_isknown(cave, loc.y, loc.x) && !square_isseen(cave, loc.y, loc.x)) {
		/* Unknown squares are grey. */
		color = COLOUR_L_DARK;
	} else {
		/* Unoccupied squares are white. */
		color = COLOUR_WHITE;
	}

	return color;
}

/**
 * Draw a visible path over the squares between (x1,y1) and (x2,y2).
 *
 * The path consists of "*", which are white except where there is a
 * monster, object or feature in the grid.
 *
 * This routine has (at least) three weaknesses:
 * - remembered objects/walls which are no longer present are not shown,
 * - squares which (e.g.) the player has walked through in the dark are
 *   treated as unknown space.
 * - walls which appear strange due to hallucination aren't treated correctly.
 *
 * The first two result from information being lost from the dungeon arrays,
 * which requires changes elsewhere
 */
static size_t draw_path(struct loc *path_points, size_t path_number,
		struct term_point *term_points, struct loc coords)
{
	if (path_number == 0) {
		return 0;
	}

	region cave_reg;
	get_cave_region(&cave_reg);

	/* 
	 * The starting square is never drawn, but notice if it is being
     * displayed. It could be the last such square.
     */
	bool on_screen = loc_in_region(coords, cave_reg);

	size_t i;
	for (i = 0; i < path_number; i++) {
		/*
		 * If the square being drawn is visible, this is part of it.
		 * If some of it has been drawn, finish now as there are no
		 * more visible squares to draw.
		 */
		 if (loc_in_region(path_points[i], cave_reg)) {
			 int relx = path_points[i].x - cave_reg.x;
			 int rely = path_points[i].y - cave_reg.y;

			 Term_get_point(relx, rely, &term_points[i]);

			 struct term_point point = {
				 .fg_char = L'*',
				 .fg_attr = draw_path_get_color(path_points[i]),
				 .bg_char = term_points[i].bg_char,
				 .bg_attr = term_points[i].bg_attr,
				 .terrain_attr = term_points[i].terrain_attr
			 };
			 Term_set_point(relx, rely, point);

			 on_screen = true;
		 } else if (on_screen) {
			 break;
		 }
	}

	Term_flush_output();

	return i;
}

/**
 * Load the points along path which were saved in draw_path().
 */
static void load_path(struct loc *path_points, size_t path_number,
		struct term_point *term_points)
{
	region cave_reg;
	get_cave_region(&cave_reg);

	for (size_t i = 0; i < path_number; i++) {
		if (loc_in_region(path_points[i], cave_reg)) {
			int relx = path_points[i].x - cave_reg.x;
			int rely = path_points[i].y - cave_reg.y;
			Term_set_point(relx, rely, term_points[i]);
		}
	}

	Term_flush_output();
}

static void target_restricted_handle_mouse(ui_event event,
		struct loc *coords,
		struct point_set *targets,
		int *square, bool *restricted, bool *done)
{
	assert(event.type == EVT_MOUSE);

	if (event.mouse.button == MOUSE_BUTTON_MIDDLE) {
		event.mouse.button = MOUSE_BUTTON_RIGHT;
		event.mouse.mods = KC_MOD_CONTROL;
	}

	int x = EVENT_GRID_X(event);
	int y = EVENT_GRID_Y(event);

	coords->x = x;
	coords->y = y;

	if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
		if (event.mouse.mods & KC_MOD_CONTROL) {
			struct monster *mon = square_monster(cave, y, x);

			if (target_able(mon)) {
				monster_race_track(player->upkeep, mon->race);
				health_track(player->upkeep, mon);
				target_set_monster(mon);
				*done = true;
			} else {
				bell("Illegal target!");
			}
		} else if (event.mouse.mods & KC_MOD_ALT) {
			cmdq_push(CMD_PATHFIND);
			cmd_set_arg_point(cmdq_peek(), "point", y, x);
			*done = true;
		} else {
			*done = true;
		}
	} else if (event.mouse.button == MOUSE_BUTTON_LEFT) {
		*restricted = false;
		if (square_monster(cave, y, x) || square_object(cave, y, x)) {
			for (int i = 0; i < point_set_size(targets); i++) {
				if (x == targets->pts[i].x && y == targets->pts[i].y) {
					*square = i;
					*restricted = true;
					break;
				}
			}
		}
	}
}

static void target_restricted_dir(int dir,
		struct point_set **targets, int *square, int mode)
{
	int oldx = (*targets)->pts[*square].x;
	int oldy = (*targets)->pts[*square].y;

	int pick = target_pick(oldy, oldx, ddy[dir], ddx[dir], *targets);

	if (pick < 0) {
		struct loc old_offsets;
		display_term_get_coords(DISPLAY_CAVE, &old_offsets);

		if (change_panel(DISPLAY_CAVE, dir)) {
			point_set_dispose(*targets);
			*targets = target_get_monsters(mode);

			pick = target_pick(oldy, oldx, ddy[dir], ddx[dir], *targets);

			if (pick < 0) {
				modify_panel(DISPLAY_CAVE, old_offsets);
				point_set_dispose(*targets);
				*targets = target_get_monsters(mode);
			}

			handle_stuff(player);
		}
	}

	if (pick >= 0) {
		*square = pick;
	}
}

static void target_restricted_handle_key(ui_event event,
		struct loc *coords, struct point_set **targets,
		int *square, bool *restricted, bool *done, int mode)
{
	assert(event.type == EVT_KBRD);

	int dir = 0;
	struct monster *mon = NULL;

	switch (event.key.code) {
		case ESCAPE: case 'q':
			*done = true;
			break;

		case ' ': case '*': case '+':
			if (*square >= point_set_size(*targets) - 1) {
				*square = 0;
			} else {
				(*square)++;
			}
			break;

		case '-':
			if (*square <= 0) {
				*square = point_set_size(*targets) - 1;
			} else {
				(*square)--;
			}
			break;

		case 'p':
			verify_panel(DISPLAY_CAVE);
			handle_stuff(player);
			coords->x = player->px;
			coords->y = player->py;
			*restricted = false;
			break;

		case 'o':
			*restricted = false;
			break;

		case 'm':
			break;

		case 't': case '5': case '0': case '.':
			mon = square_monster(cave, coords->y, coords->x);

			if (target_able(mon)) {
				health_track(player->upkeep, mon);
				target_set_monster(mon);
				*done = true;
			} else {
				bell("Illegal target!");
			}
			break;

		case 'g':
			cmdq_push(CMD_PATHFIND);
			cmd_set_arg_point(cmdq_peek(), "point", coords->y, coords->x);
			*done = true;
			break;
	
		case '?':
			target_display_help(*targets, *coords, *restricted);
			break;

		default:
			dir = target_dir(event.key);

			if (dir != 0) {
				target_restricted_dir(dir, targets, square, mode);
			} else {
				bell("Illegal command for target mode!");
			}
			break;
	}
}

static void target_restricted(struct loc *coords, struct point_set **targets,
		int *square, bool *restricted, bool *done, int mode)
{
	coords->x = (*targets)->pts[*square].x;
	coords->y = (*targets)->pts[*square].y;

	if (adjust_panel(DISPLAY_CAVE, *coords)) {
		handle_stuff(player);
	}

	struct loc path_points[z_info->max_range];
	struct term_point term_points[z_info->max_range];

	size_t path_number = project_path(path_points, z_info->max_range,
			player->py, player->px, coords->y, coords->x, PROJECT_THRU);

	size_t path_drawn = 0;
	if (target_kill(mode)) {
		path_drawn = draw_path(path_points, path_number,
				term_points, loc(player->px, player->py));
	}

	ui_event event = target_set_interactive_aux(*coords, mode);

	if (path_drawn > 0) {
		load_path(path_points, path_number, term_points);
	}

	if (event.type == EVT_MOUSE) {
		target_restricted_handle_mouse(event, coords,
				*targets, square, restricted, done);
	} else if (event.type == EVT_KBRD) {
		target_restricted_handle_key(event, coords,
				targets, square, restricted, done, mode);
	}
}

static void target_free_select_dir(int dir,
		struct point_set **targets, struct loc *coords, int mode)
{
	const int movx = coords->x + ddx[dir];
	const int movy = coords->y + ddy[dir];
	const int maxx = cave->width - 1;
	const int maxy = cave->height - 1;

	coords->x = MAX(0, MIN(movx, maxx));
	coords->y = MAX(0, MIN(movy, maxy));

	if (adjust_panel(DISPLAY_CAVE, *coords)) {
		handle_stuff(player);
		point_set_dispose(*targets);
		*targets = target_get_monsters(mode);
	}
}

static void target_free_select_handle_key(ui_event event,
		struct loc *coords,
		struct point_set **targets,
		int *square, bool *restricted, bool *done, int mode)
{
	assert(event.type == EVT_KBRD);

	int best_dist = INT_MAX;
	int dir = 0;

	switch (event.key.code) {
		case ESCAPE:
		case 'q':
			*done = true;
			break;

		case ' ': case '*': case '+': case '-':
			break;

		case 'p':
			verify_panel(DISPLAY_CAVE);
			handle_stuff(player);
			coords->x = player->px;
			coords->y = player->py;
			break;

		case 'o':
			break;

		case 'm':
			*restricted = true;
			*square = 0;

			for (int i = 0; i < point_set_size(*targets); i++) {
				int dist = distance(coords->y, coords->x,
						(*targets)->pts[i].y, (*targets)->pts[i].x);
				if (dist < best_dist) {
					*square = i;
					best_dist = dist;
				}
			}
			if (best_dist == INT_MAX) {
				*restricted = false;
			}
			break;

		case 't': case '5': case '0': case '.':
			target_set_location(coords->y, coords->x);
			*done = true;
			break;

		case 'g':
			cmdq_push(CMD_PATHFIND);
			cmd_set_arg_point(cmdq_peek(), "point", coords->y, coords->x);
			*done = true;
			break;

		case '?':
			target_display_help(*targets, *coords, *restricted);
			break;

		default:
			dir = target_dir(event.key);
			if (dir != 0) {
				target_free_select_dir(dir, targets, coords, mode);
			} else {
				bell("Illegal command for target mode!");
			}
			break;
	}
}

static void target_free_select_handle_mouse(ui_event event,
		struct loc *coords,
		struct point_set **targets,
		int *square, bool *restricted, bool *done, int mode)
{
	assert(event.type == EVT_MOUSE);

	if (event.mouse.button == MOUSE_BUTTON_MIDDLE) {
		event.mouse.button = MOUSE_BUTTON_RIGHT;
		event.mouse.mods = KC_MOD_CONTROL;
	}

	int x = EVENT_GRID_X(event);
	int y = EVENT_GRID_Y(event);

	if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
		*done = true;

		if ((target_kill(mode))
				&& (coords->x == x && coords->y == y))
		{
			target_set_location(y, x);
		} else if (event.mouse.mods & KC_MOD_CONTROL) {
			target_set_location(y, x);
		} else if (event.mouse.mods & KC_MOD_ALT) {
			cmdq_push(CMD_PATHFIND);
			cmd_set_arg_point(cmdq_peek(), "point", y, x);
		}
	} else if (event.mouse.button == MOUSE_BUTTON_LEFT) {
		int term_width;
		int term_height;
		struct loc offsets;

		display_term_get_area(DISPLAY_CAVE,
				&offsets, &term_width, &term_height);

		/* scroll the map to, resp., west, east, north or south */
		if (event.mouse.x < SCROLL_DISTANCE) {
			x = offsets.x - 1;
		} else if (event.mouse.x >= term_width - SCROLL_DISTANCE) {
			x = offsets.x + term_width;
		} else if (event.mouse.y < SCROLL_DISTANCE) {
			y = offsets.y - 1;
		} else if (event.mouse.y >= term_height - SCROLL_DISTANCE) {
			y = offsets.y + term_height;
		}

		coords->x = MAX(0, MIN(x, cave->width - 1));
		coords->y = MAX(0, MIN(y, cave->height - 1));

		if (adjust_panel(DISPLAY_CAVE, *coords)) {
			handle_stuff(player);
			point_set_dispose(*targets);
			*targets = target_get_monsters(mode);
		}

		if (square_monster(cave, coords->y, coords->x)
				|| square_object(cave, coords->y, coords->x))
		{
			for (int i = 0; i < point_set_size(*targets); i++) {
				if (coords->x == (*targets)->pts[i].x
						&& coords->y == (*targets)->pts[i].y)
				{
					*square = i;
					*restricted = true;
					break;
				}
			}
		} else {
			*restricted = false;
		}
	}
}

static void target_free_select(struct loc *coords, struct point_set **targets,
		int *square, bool *restricted, bool *done, int mode)
{
	struct loc path_points[z_info->max_range];
	struct term_point term_points[z_info->max_range];

	size_t path_number = project_path(path_points, z_info->max_range,
			player->py, player->px, coords->y, coords->x, PROJECT_THRU);

	size_t path_drawn = 0;
	if (target_kill(mode)) {
		path_drawn = draw_path(path_points, path_number,
				term_points, loc(player->px, player->py));
	}

	ui_event event = target_set_interactive_aux(*coords, mode | TARGET_LOOK);

	if (path_drawn > 0) {
		load_path(path_points, path_number, term_points);
	}

	if (event.type == EVT_MOUSE) {
		target_free_select_handle_mouse(event, coords,
				targets, square, restricted, done, mode);
	} else if (event.type == EVT_KBRD) {
		target_free_select_handle_key(event, coords,
				targets, square, restricted, done, mode);
	}

}
/**
 * Handle target and look.
 *
 * Note that this code can be called from "get_aim_dir()".
 *
 * Currently, when "restricted" mode is true, that is, when
 * interesting grids are being used, and a directional key is used, we
 * only scroll by a single panel, in the direction requested, and check
 * for any interesting grids on that panel.  The "correct" solution would
 * actually involve scanning a larger set of grids, including ones in
 * panels which are adjacent to the one currently scanned, but this is
 * overkill for this function.
 *
 * The player can use the direction keys to move among "interesting"
 * grids in a heuristic manner, or the "space", "+", and "-" keys to
 * move through the "interesting" grids in a sequential manner, or
 * can enter location mode, and use the direction keys to move one
 * grid at a time in any direction.  The "t" (set target) command will
 * only target a monster (as opposed to a location) if the monster is
 * target_able and the "interesting" mode is being used.
 *
 * The current grid is described using the "look" method above, and
 * a new command may be entered at any time, but note that if the
 * "TARGET_LOOK" bit flag is set (or if we are in "location" mode,
 * where space has no obvious meaning) then space will scan
 * through the description of the current grid until done, instead
 * of immediately jumping to the next "interesting" grid.  This
 * allows the target command to retain its old semantics.
 *
 * The "*", "+", and "-" keys may always be used to jump immediately
 * to the next (or previous) interesting grid, in the proper mode.
 *
 * The "return" key may always be used to scan through a complete
 * grid description (forever).
 *
 * This command will cancel any old target, even if used from
 * inside the look command.
 *
 * 'mode' is one of TARGET_LOOK or TARGET_KILL.
 * coords is the initial position of the target to be highlighted,
 * both x and y should be -1 if no location is specified.
 * Returns true if a target has been successfully set, false otherwise.
 */
bool target_set_interactive(int mode, struct loc coords)
{
	display_term_push(DISPLAY_CAVE);

	bool saved_cursor;
	Term_get_cursor(NULL, NULL, &saved_cursor, NULL);
	Term_cursor_visible(true);

	target_set_monster(NULL);
	struct point_set *targets = target_get_monsters(mode);

	bool restricted;
	if (coords.x == -1 || coords.y == -1) {
		coords.x = player->px;
		coords.y = player->py;
		restricted = true;
	} else {
		restricted = false;
	}

	int square = 0;
	bool done = false;

	while (!done) {
		if (restricted && point_set_size(targets) > 0) {
			target_restricted(&coords, &targets,
					&square, &restricted, &done, mode);
		} else {
			target_free_select(&coords, &targets,
					&square, &restricted, &done, mode);
		}
	}

	Term_cursor_visible(saved_cursor);
	Term_flush_output();

	point_set_dispose(targets);
	verify_panel(DISPLAY_CAVE);
	handle_stuff(player);
	clear_prompt();

	display_term_pop();

	return target_is_set();
}
