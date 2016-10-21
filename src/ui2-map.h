/**
 * \file ui2-map.h
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

#ifndef UI2_MAP_H
#define UI2_MAP_H

#include "ui2-display.h"
#include "ui2-event.h"
#include "ui2-output.h"
#include "ui2-term.h"
#include "z-type.h"

void grid_data_as_point(struct grid_data *g, struct term_point *point);
void move_cursor_relative(enum display_term_index i, struct loc coords, bool flush);
void move_map(enum display_term_index i, struct loc diff, region panel);
void map_redraw_all(enum display_term_index i);
void print_map_relative(enum display_term_index i,
		uint32_t attr, wchar_t ch, struct loc coords);

void do_cmd_view_map(void);
void verify_cursor(void);

/*
 * Convert relative coordinates on the map to absolute ones
 */
int map_grid_x(int x);
int map_grid_y(int y);

#endif /* UI2_MAP_H */
