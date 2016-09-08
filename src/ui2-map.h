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

#include "ui2-term.h"
#include "ui2-display.h"
#include "z-type.h"

extern void grid_data_as_point(struct grid_data *g, struct term_point *point);
extern void move_cursor_relative(enum display_term_index i, struct loc coords);
extern void print_rel(enum display_term_index i, uint32_t attr, wchar_t ch, struct loc coords);
extern void print_map(enum display_term_index i);
extern void do_cmd_view_map(void);

#endif /* UI2_MAP_H */
