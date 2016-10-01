/**
 * \file ui-display.h
 * \brief Handles the setting up updating, and cleaning up of the game display.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007 Antony Sidwell
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

#ifndef UI2_DISPLAY_H
#define UI2_DISPLAY_H

#include "angband.h"
#include "z-type.h"
#include "ui2-term.h"

enum display_term_index {
	#define DISPLAY(i, name, minc, minr, defc, defr, maxc, maxr, req) \
		DISPLAY_ ##i,
	#include "list-display-terms.h"
	#undef DISPLAY
	DISPLAY_MAX
};

#define ANGBAND_TERM_STANDARD_WIDTH 80
#define ANGBAND_TERM_STANDARD_HEIGHT 24

extern const char *stat_names[STAT_MAX];
extern const char *stat_names_reduced[STAT_MAX];
extern const char *window_flag_desc[32];

void display_term_init(enum display_term_index i, term t);
void display_term_destroy(enum display_term_index i);

const char *display_term_get_name(enum display_term_index i);

/* Given absolute coords, calculate ones that are relative to display_term */
void display_term_rel_coords(enum display_term_index i, struct loc *coords);

void display_term_get_coords(enum display_term_index i, struct loc *coords);
void display_term_set_coords(enum display_term_index i, struct loc coords);

void display_term_get_area(enum display_term_index i,
		struct loc *coords, int *width, int *height);

void display_term_push(enum display_term_index i);
void display_term_pop(void);

void display_terms_redraw(void);

void init_display(void);

void message_skip_more(void);
uint32_t monster_health_attr(const struct monster *mon);
void cnv_stat(const char *fmt, int val, char *out_val, size_t out_len);
void idle_update(void);
void toggle_inven_equip(void);

#endif /* UI2_DISPLAY_H */
