/**
 * \file ui2-map.c
 * \brief Writing level map info to the screen
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "grafmode.h"
#include "init.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-timed.h"
#include "trap.h"
#include "ui2-map.h"
#include "ui2-input.h"
#include "ui2-object.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-term.h"

static void hallucinatory_monster(struct term_point *point)
{
	while (true) {
		/* Select a random monster */
		struct monster_race *race = &r_info[randint0(z_info->r_max)];
		if (race->name) {
			point->fg_attr = monster_x_attr[race->ridx];
			point->fg_char = monster_x_char[race->ridx];
			return;
		}
	}
}

static void hallucinatory_object(struct term_point *point)
{
	point->fg_attr = 0;
	point->fg_char = 0;

	while (point->fg_attr == 0 || point->fg_char == 0) {
		/* Select a random object */
		struct object_kind *kind = &k_info[randint1(z_info->k_max - 1)];

		if (kind->name) {
			point->fg_attr = kind_x_attr[kind->kidx];
			point->fg_char = kind_x_char[kind->kidx];
		}
	}
}

/**
 * Get the graphics of a listed trap.
 *
 * We should probably have better handling of stacked traps, but that can
 * wait until we do, in fact, have stacked traps under normal conditions.
 */
static void grid_get_trap(const struct grid_data *g, struct term_point *point)
{
	if (!g->trap || g->hallucinate) {
		return;
	}

	/* There is a trap in this grid, we are not hallucinating
	 * and trap is visible */
	if (trf_has(g->trap->flags, TRF_VISIBLE)
			|| trf_has(g->trap->flags, TRF_RUNE))
	{
		point->fg_attr = trap_x_attr[g->lighting][g->trap->kind->tidx];
		point->fg_char = trap_x_char[g->lighting][g->trap->kind->tidx];
	}
}

static void grid_get_terrain(const struct grid_data *g, struct term_point *point)
{
	if (use_graphics == GRAPHICS_NONE) {
		/* Hybrid or block walls */
		if (feat_is_wall(g->f_idx)) {
			if (OPT(hybrid_walls)) {
				point->terrain_attr = COLOUR_SHADE;
			} else if (OPT(solid_walls)) {
				point->terrain_attr = point->fg_attr;
			} else {
				point->terrain_attr = COLOUR_DARK;
			}
		} else {
			point->terrain_attr = COLOUR_DARK;
		}
	}
}

/**
 * Apply text lighting effects
 */
static void grid_get_light(const struct grid_data *g, struct term_point *point)
{
	if (feat_is_treasure(g->f_idx)) {
		return;
	}

	struct feature *feat = &f_info[g->f_idx];

	/* Only apply lighting effects when the attr is white  and it's a floor or wall */
	if (point->fg_attr == COLOUR_WHITE
			&& (tf_has(feat->flags, TF_FLOOR) || feat_is_wall(g->f_idx)))
	{
		if (tf_has(feat->flags, TF_TORCH) && g->lighting == LIGHTING_TORCH) {
			/* If it's a floor tile list by a torch then we'll make it yellow */
			point->fg_attr = COLOUR_YELLOW;
		} else if (g->lighting == LIGHTING_DARK || g->lighting == LIGHTING_LIT) {
			/* If it's another kind of tile, only tint when unlit. */
			point->fg_attr = COLOUR_L_DARK;
		}
	} else if (feat_is_magma(g->f_idx) || feat_is_quartz(g->f_idx)) {
		if (!g->in_view) {
			point->fg_attr = COLOUR_L_DARK;
		}
	}
}

static void grid_get_object(const struct grid_data *g,
		struct term_point *point)
{
	if (g->unseen_money) {
		/* money gets an orange star */
		point->fg_attr = object_kind_attr(unknown_gold_kind);
		point->fg_char = object_kind_char(unknown_gold_kind);
	} else if (g->unseen_object) {	
		/* Everything else gets a red star */    
		point->fg_attr = object_kind_attr(unknown_item_kind);
		point->fg_char = object_kind_char(unknown_item_kind);
	} else if (g->first_kind) {
		if (g->hallucinate) {
			/* Just pick a random object to display. */
			hallucinatory_object(point);
		} else if (g->multiple_objects) {
			/* Get the "pile" feature instead */
			point->fg_attr = object_kind_attr(pile_kind);
			point->fg_char = object_kind_char(pile_kind);
		} else {
			/* Normal attr and char */
			point->fg_attr = object_kind_attr(g->first_kind);
			point->fg_char = object_kind_char(g->first_kind);
		}
	}
}

static void grid_get_monster(const struct grid_data *g,
		struct term_point *point)
{
	if (g->m_idx == 0) {
		return;
	}

	if (g->hallucinate) {
		/* Just pick a random monster to display. */
		hallucinatory_monster(point);
	} else if (!is_mimicking(cave_monster(cave, g->m_idx)))	{
		struct monster *mon = cave_monster(cave, g->m_idx);

		/* Desired attr and char */
		uint32_t da = monster_x_attr[mon->race->ridx];
		wchar_t dc = monster_x_char[mon->race->ridx];

		/* Special handling of attrs and/or chars */
		if (da & 0x80) {
			/* Graphical attr/char codes */
			/* TODO remove */
			point->fg_attr = da;
			point->fg_char = dc;
		} else {
			if (OPT(purple_uniques)
				&& rf_has(mon->race->flags, RF_UNIQUE))
			{
				/* Turn uniques purple if desired (violet, actually) */
				point->fg_attr = COLOUR_VIOLET;
				point->fg_char = dc;
			} else if (rf_has(mon->race->flags, RF_ATTR_MULTI)
					|| rf_has(mon->race->flags, RF_ATTR_FLICKER)
					|| rf_has(mon->race->flags, RF_ATTR_RAND))
			{
				/* Multi-hued monster */
				point->fg_attr = mon->attr ? mon->attr : da;
				point->fg_char = dc;
			} else if (!flags_test(mon->race->flags, RF_SIZE,
						RF_ATTR_CLEAR, RF_CHAR_CLEAR, FLAG_END))
			{
				/* Normal monster (not "clear" in any way) */
				point->fg_attr = da;
				point->fg_char = dc;
			} else if (rf_has(mon->race->flags, RF_ATTR_CLEAR)) {
				/* Normal char, clear attr, monster */
				point->fg_char = dc;
			} else if (rf_has(mon->race->flags, RF_CHAR_CLEAR)) {
				/* Normal attr, clear char, monster */
				point->fg_attr = da;
			}

			/* Store the drawing attr so we can use it elsewhere */
			mon->attr = point->fg_attr;
		}
	}
}

static void grid_get_player(const struct grid_data *g,
		struct term_point *point)
{
	if (!g->is_player) {
		return;
	}

	struct monster_race *race = &r_info[0];

	/* Player attr and char */
	point->fg_attr = monster_x_attr[race->ridx];
	point->fg_char = monster_x_char[race->ridx];

	if (OPT(hp_changes_color) && !(point->fg_attr & 0x80)) {
		switch (player->chp * 10 / player->mhp) {
			case 10: case 9:
				point->fg_attr = COLOUR_WHITE;
				break;
			case 8: case 7:
				point->fg_attr = COLOUR_YELLOW;
				break;
			case 6: case 5:
				point->fg_attr = COLOUR_ORANGE;
				break;
			case 4: case 3:
				point->fg_attr = COLOUR_L_RED;
				break;
			case 2: case 1: case 0:
				point->fg_attr = COLOUR_RED;
				break;
			default:
				point->fg_attr = COLOUR_WHITE;
				break;
		}
	}
}

static void grid_get_creature(const struct grid_data *g,
		struct term_point *point)
{
	if (g->m_idx > 0) {
		grid_get_monster(g, point);
	} else if (g->is_player) {
		grid_get_player(g, point);
	}
}

/**
 * This function takes a pointer to a grid info struct describing the 
 * contents of a grid location (as obtained through the function map_info)
 * and fills in the term_point struct for display.
 *
 * fg_attr and fg_char are filled with the attr/char pair for the monster,
 * object, trap or floor tile that is at the top of the grid
 * (monsters covering objects, which cover traps, which cover floor,
 * assuming all are present).
 *
 * bg_attr and bg_char are filled with the attr/char pair for the floor,
 * regardless of what is on it.  This can be used by graphical displays with
 * transparency to place an object onto a floor tile, is desired.
 *
 * Any lighting effects are also applied to these pairs, clear monsters allow
 * the underlying colour or feature to show through (ATTR_CLEAR and
 * CHAR_CLEAR), multi-hued colour-changing (ATTR_MULTI) is applied, and so on.
 * Technically, the flag "CHAR_MULTI" is supposed to indicate that a monster 
 * looks strange when examined, but this flag is currently ignored.
 *
 * NOTES:
 * This is called pretty frequently, whenever a grid on the map display
 * needs updating, so don't overcomplicate it.
 *
 * The zero entry in the feature/object/monster arrays are
 * used to provide special attr/char codes, with "monster zero" being
 * used for the player attr/char, "object zero" being used for the "pile"
 * attr/char, and "feature zero" being used for the "darkness" attr/char.
 */
void grid_data_as_point(struct grid_data *g, struct term_point *point)
{
	struct feature *feat = &f_info[g->f_idx];

	if (use_graphics != GRAPHICS_NONE) {
		/* Save the background for tiles */
		point->bg_attr = feat_x_attr[g->lighting][feat->fidx];
		point->bg_char = feat_x_char[g->lighting][feat->fidx];
		/* In case the grid doesn't have anything else */
		point->fg_attr = point->bg_attr;
		point->fg_char = point->bg_char;
	} else {
		/* Text (non-tiles) mode doesn't actually use background information */
		point->bg_attr = 0;
		point->bg_char = 0;

		point->fg_attr = feat_x_attr[g->lighting][feat->fidx];
		point->fg_char = feat_x_char[g->lighting][feat->fidx];

		grid_get_light(g, point);
	}

	grid_get_trap(g, point);
	grid_get_object(g, point);
	grid_get_creature(g, point);

	grid_get_terrain(g, point);

	/* TODO UI2 set some flags */
	tpf_wipe(point->flags);
}

void move_cursor_relative(enum display_term_index index, struct loc coords)
{
	/* Calculate relative coordinates */
	display_term_rel_coords(index, &coords);

	display_term_push(index);
	Term_cursor_to_xy(coords.x, coords.y);
	display_term_pop();
}

void print_rel(enum display_term_index index,
		uint32_t attr, wchar_t ch, struct loc coords)
{
	/* Calculate relative coordinates */
	display_term_rel_coords(index, &coords);

	display_term_push(index);

	if (Term_point_ok(coords.x, coords.y)) {
		struct term_point point;
		Term_get_point(coords.x, coords.y, &point);

		point.fg_attr = attr;
		point.fg_char = ch;

		Term_set_point(coords.x, coords.y, point);
	}

	display_term_pop();
}

void print_map(enum display_term_index index)
{
	int width;
	int height;
	struct loc offset;

	display_term_get_area(index, &offset, &width, &height);

	display_term_push(index);

	/* Dump the map */
	for (int rely = 0, absy = offset.y; rely < height; rely++, absy++) {
		for (int relx = 0, absx = offset.x; relx < width; relx++, absx++) {
			if (square_in_bounds(cave, absy, absx)) {
				struct grid_data g;
				map_info(absy, absx, &g);

				struct term_point point;
				grid_data_as_point(&g, &point);

				Term_set_point(relx, rely, point);
			}
		}
	}

	display_term_pop();
}

static byte **make_priority_grid(void)
{
	byte **priority_grid =
		mem_zalloc(cave->height * sizeof(*priority_grid));

	for (int y = 0; y < cave->height; y++) {
		priority_grid[y] =
			mem_zalloc(cave->width * sizeof(**priority_grid));
	}

	return priority_grid;
}

static void free_priority_grid(byte **priority_grid)
{
	for (int y = 0; y < cave->height; y++) {
		mem_free(priority_grid[y]);
	}
	mem_free(priority_grid);
}

/**
 * Display a map of the dungeon in the active term.
 * The map may be scaled if the term is too small for the whole dungeon.
 * If "py" and "px" are not NULL, then returns the screen location at which
 * the player was displayed, so the cursor can be moved to that location,
 */
static void view_map_aux(int *px, int *py)
{
	/* Desired map height */
	int width;
	int height;
	Term_get_size(&width, &height);

	const int cave_width = cave->width;
	const int cave_height = cave->height;
	const int term_width = MIN(width, cave_width);
	const int term_height = MIN(height, cave_height);

	byte **priority_grid = NULL;
	if (term_width != cave_width || term_height != cave_height) {
		priority_grid = make_priority_grid();
	}

	for (int y = 0; y < cave_height; y++) {
		int row = y * term_height / cave_height;

		for (int x = 0; x < cave_width; x++) {
			int col = x * term_width / cave_width;

			struct grid_data g;
			map_info(y, x, &g);

			struct term_point point;
			grid_data_as_point(&g, &point);

			if (priority_grid == NULL) {
				Term_set_point(col, row, point);
			} else {
				byte priority = f_info[g.f_idx].priority;

				/* Stuff on top of terrain gets higher priority */
				if (point.fg_attr != point.bg_attr
						|| point.fg_char != point.bg_char)
				{
					priority = 20;
				}

				if (priority_grid[row][col] < priority) {
					Term_set_point(col, row, point);
					priority_grid[row][col] = priority;
				}
			}
		}
	}

	/* Player location */
	int col = player->px * term_width / cave_width;
	int row = player->py * term_height / cave_height;

	struct term_point point;
	Term_get_point(col, row, &point);

	struct term_point other = {
		.fg_attr = monster_x_attr[r_info[0].ridx],
		.fg_char = monster_x_char[r_info[0].ridx],
		.bg_attr = point.bg_attr,
		.bg_char = point.bg_char,
		.terrain_attr = BG_BLACK
	};

	/* Draw the player */
	Term_set_point(col, row, other);

	if (px != NULL) {
		*px = col;
	}
	if (py != NULL) {
		*py = row;
	}

	if (priority_grid != NULL) {
		free_priority_grid(priority_grid);
	}
}

/*
 * Display a map of the dungeon, possibly scaled
 */
void do_cmd_view_map(void)
{
	struct term_hints hints = {
		.width = cave->width,
		.height = cave->height,
		.purpose = TERM_PURPOSE_BIG_MAP
	};
	Term_push_new(&hints);

	int px;
	int py;
	view_map_aux(&px, &py);

	Term_cursor_to_xy(px, py);
	Term_cursor_visible(true);
	Term_flush_output();

	inkey_any();

	Term_pop();
}
