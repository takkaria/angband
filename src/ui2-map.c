/**
 * \file ui-map.c
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

static void hallucinatory_monster(uint32_t *attr, wchar_t *ch)
{
	while (true) {
		/* Select a random monster */
		struct monster_race *race = &r_info[randint0(z_info->r_max)];
		if (!race->name) {
			continue;
		}
		
		*attr = monster_x_attr[race->ridx];
		*ch = monster_x_char[race->ridx];

		return;
	}
}

static void hallucinatory_object(uint32_t *attr, wchar_t *ch)
{
	*attr = 0;
	*ch = 0;

	while (*attr == 0 || *ch == 0) {
		/* Select a random object */
		struct object_kind *kind = &k_info[randint1(z_info->k_max - 1)];

		if (kind->name) {
			*attr = kind_x_attr[kind->kidx];
			*ch = kind_x_char[kind->kidx];
		}
	}
}

/**
 * Get the graphics of a listed trap.
 *
 * We should probably have better handling of stacked traps, but that can
 * wait until we do, in fact, have stacked traps under normal conditions.
 */
static void grid_get_trap(struct grid_data *g, uint32_t *attr, wchar_t *ch)
{
	if (!g->trap || g->hallucinate) {
		return;
	}

	/* There is a trap in this grid, we are not hallucinating
	 * and trap is visible */
	if (trf_has(g->trap->flags, TRF_VISIBLE)
			|| trf_has(g->trap->flags, TRF_RUNE))
	{
		*attr = trap_x_attr[g->lighting][g->trap->kind->tidx];
		*ch = trap_x_char[g->lighting][g->trap->kind->tidx];
	}
}

static void grid_get_light(const struct grid_data *g,
		struct feature *feat, uint32_t *attr)
{
	enum grid_light_level light = g->lighting;

	if (tf_has(feat->flags, TF_TORCH)) {
		/* If it's a floor tile then we'll tint based on lighting. */
		if (light == LIGHTING_TORCH) {
			*attr = COLOUR_YELLOW;
		} else if (light == LIGHTING_DARK || light == LIGHTING_LIT) {
			*attr = COLOUR_L_DARK;
		}
	} else if (light == LIGHTING_DARK || light == LIGHTING_LIT) {
		/* If it's another kind of tile, only tint when unlit. */
		*attr = COLOUR_L_DARK;
	}
}

static void grid_get_terrain(const struct grid_data *g, uint32_t *attr)
{
	if (use_graphics == GRAPHICS_NONE) {
		/* Hybrid or block walls */
		if (feat_is_wall(g->f_idx)) {
			if (OPT(hybrid_walls)) {
				*attr = BG_DARK;
			} else if (OPT(solid_walls)) {
				*attr = BG_SAME;
			} else {
				*attr = BG_BLACK;
			}
		} else {
			*attr = BG_BLACK;
		}
	} else {
		*attr = 0;
	}
}

/**
 * Apply text lighting effects
 */
static void grid_get_attr(const struct grid_data *g, uint32_t *attr)
{
	struct feature *feat = &f_info[g->f_idx];

	if (!feat_is_treasure(g->f_idx)) {
		/* Only apply lighting effects when the attr is white
		 * and it's a floor or wall */
		if (*attr == COLOUR_WHITE
				&& (tf_has(feat->flags, TF_FLOOR) || feat_is_wall(g->f_idx)))
		{
			grid_get_light(g, feat, attr);

		} else if (feat_is_magma(g->f_idx) || feat_is_quartz(g->f_idx)) {
			if (!g->in_view) {
				*attr = COLOUR_L_DARK;
			}
		}
	}
}

static void grid_get_object(struct grid_data *g, uint32_t *attr, wchar_t *ch)
{
	if (g->unseen_money) {
		/* money gets an orange star */
		*attr = object_kind_attr(unknown_gold_kind);
		*ch = object_kind_char(unknown_gold_kind);
	} else if (g->unseen_object) {	
		/* Everything else gets a red star */    
		*attr = object_kind_attr(unknown_item_kind);
		*ch = object_kind_char(unknown_item_kind);
	} else if (g->first_kind) {
		if (g->hallucinate) {
			/* Just pick a random object to display. */
			hallucinatory_object(attr, ch);
		} else if (g->multiple_objects) {
			/* Get the "pile" feature instead */
			*attr = object_kind_attr(pile_kind);
			*ch = object_kind_char(pile_kind);
		} else {
			/* Normal attr and char */
			*attr = object_kind_attr(g->first_kind);
			*ch = object_kind_char(g->first_kind);
		}
	}
}

static void grid_get_monster(struct grid_data *g, uint32_t *attr, wchar_t *ch)
{
	if (g->m_idx == 0) {
		return;
	}

	if (g->hallucinate) {
		/* Just pick a random monster to display. */
		hallucinatory_monster(attr, ch);
	} else if (!is_mimicking(cave_monster(cave, g->m_idx)))	{
		struct monster *mon = cave_monster(cave, g->m_idx);

		/* Desired attr and char */
		uint32_t da = monster_x_attr[mon->race->ridx];
		wchar_t dc = monster_x_char[mon->race->ridx];

		/* Special handling of attrs and/or chars */
		if (da & 0x80) {
			/* Special attr/char codes */
			/* TODO remove */
			*attr = da;
			*ch = dc;
		} else if (OPT(purple_uniques)
				&& rf_has(mon->race->flags, RF_UNIQUE))
		{
			/* Turn uniques purple if desired (violet, actually) */
			*attr = COLOUR_VIOLET;
			*ch = dc;
		} else if (rf_has(mon->race->flags, RF_ATTR_MULTI)
				|| rf_has(mon->race->flags, RF_ATTR_FLICKER)
				|| rf_has(mon->race->flags, RF_ATTR_RAND))
		{
			/* Multi-hued monster */
			*attr = mon->attr ? mon->attr : da;
			*ch = dc;
		} else if (!flags_test(mon->race->flags, RF_SIZE,
					RF_ATTR_CLEAR, RF_CHAR_CLEAR, FLAG_END))
		{
			/* Normal monster (not "clear" in any way) */
			*attr = da;
			*ch = dc;
		} else if (rf_has(mon->race->flags, RF_ATTR_CLEAR)) {
			/* Normal char, Clear attr, monster */
			*ch = dc;
		} else if (rf_has(mon->race->flags, RF_CHAR_CLEAR)) {
			/* Normal attr, Clear char, monster */
			*attr = da;
		}

		/* Store the drawing attr so we can use it elsewhere */
		mon->attr = *attr;
	}
}

static void grid_get_player(struct grid_data *g, uint32_t *attr, wchar_t *ch)
{
	if (!g->is_player) {
		return;
	}

	struct monster_race *race = &r_info[0];

	/* Player attr and char */
	*attr = monster_x_attr[race->ridx];
	*ch = monster_x_char[race->ridx];

	if ((OPT(hp_changes_color)) && !(*attr & 0x80)) {
		switch (player->chp * 10 / player->mhp) {
			case 10: case 9:
				*attr = COLOUR_WHITE;
				break;
			case 8: case 7:
				*attr = COLOUR_YELLOW;
				break;
			case 6: case 5:
				*attr = COLOUR_ORANGE;
				break;
			case 4: case 3:
				*attr = COLOUR_L_RED;
				break;
			case 2: case 1: case 0:
				*attr = COLOUR_RED;
				break;
			default:
				*attr = COLOUR_WHITE;
				break;
		}
	}
}

static void grid_get_creature(struct grid_data *g, uint32_t *attr, wchar_t *ch)
{
	if (g->m_idx > 0) {
		grid_get_monster(g, attr, ch);
	} else if (g->is_player) {
		grid_get_player(g, attr, ch);
	}
}

/**
 * This function takes a pointer to a grid info struct describing the 
 * contents of a grid location (as obtained through the function map_info)
 * and fills in the character and attr pairs for display.
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
 * The "zero" entry in the feature/object/monster arrays are
 * used to provide "special" attr/char codes, with "monster zero" being
 * used for the player attr/char, "object zero" being used for the "pile"
 * attr/char, and "feature zero" being used for the "darkness" attr/char.
 */
void grid_data_as_point(struct grid_data *g, struct term_point *point)
{
	struct feature *feat = &f_info[g->f_idx];

	uint32_t attr = feat_x_attr[g->lighting][feat->fidx];
	wchar_t ch = feat_x_char[g->lighting][feat->fidx];

	/* Get the colour for ASCII */
	if (use_graphics == GRAPHICS_NONE) {
		grid_get_attr(g, &attr);
	}

	/* Save the terrain info for the transparency effects */
	point->bg_attr = attr;
	point->bg_char = ch;

	grid_get_terrain(g, &point->terrain_attr);

	grid_get_trap(g, &attr, &ch);
	grid_get_object(g, &attr, &ch);
	grid_get_creature(g, &attr, &ch);

	point->fg_attr = attr;
	point->fg_char = ch;

	/* TODO UI2 set some flags */
	tpf_wipe(point->flags);
}

void move_cursor_relative(struct angband_term *aterm, struct loc coords)
{
	Term_push(aterm->term);

	int relx = coords.x - aterm->offset_x;
	int rely = coords.y - aterm->offset_y;

	Term_cursor_to_xy(relx, rely);

	Term_pop();
}

void print_rel(struct angband_term *aterm,
		uint32_t attr, wchar_t ch, struct loc coords)
{
	Term_push(aterm->term);

	int relx = coords.x - aterm->offset_x;
	int rely = coords.y - aterm->offset_y;

	if (Term_point_ok(relx, rely)) {
		struct term_point point;
		Term_get_point(relx, rely, &point);

		/* Use different point to quickly wipe all flags */
		struct term_point other = {
			.fg_attr = attr,
			.fg_char = ch,
			.bg_attr = point.bg_attr,
			.bg_char = point.bg_char
		};

		Term_set_point(relx, rely, other);
	}

	Term_pop();
}

void print_map(struct angband_term *aterm)
{
	Term_push(aterm->term);

	int width;
	int height;
	Term_get_size(&width, &height);

	/* Dump the map */
	for (int rely = 0, absy = aterm->offset_y; rely < height; rely++, absy++) {
		for (int relx = 0, absx = aterm->offset_x; relx < width; relx++, absx++) {
			if (square_in_bounds(cave, absy, absx)) {
				struct grid_data g;
				map_info(absy, absx, &g);

				struct term_point point;
				grid_data_as_point(&g, &point);

				Term_set_point(relx, rely, point);
			}
		}
	}

	Term_pop();
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

	if (term_width < 1 || term_height < 1) {
		return;
	}

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
		.fg_char = monster_x_attr[r_info[0].ridx],
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
	Term_flush_output();

	inkey_any();

	Term_pop();
}
