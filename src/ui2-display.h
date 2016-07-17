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
#include "cmd-core.h"
#include "ui2-term.h"

extern const char *stat_names[STAT_MAX];
extern const char *stat_names_reduced[STAT_MAX];
extern const char *window_flag_desc[32];

/* flags which determine what a term window does */
enum angband_window_flags {
	AWF_NONE,
	AWF_MESSAGE_LINE,
	AWF_STATUS_LINE,
	AWF_INVEN,
	AWF_EQUIP,
	AWF_PLAYER_BASIC,
	AWF_PLAYER_EXTRA,
	AWF_PLAYER_COMPACT,
	AWF_MAP,
	AWF_MESSAGE,
	AWF_OVERHEAD,
	AWF_MONSTER,
	AWF_OBJECT,
	AWF_MONLIST,
	AWF_ITEMLIST,
	AWF_MAX
};

#define AWF_SIZE                FLAG_SIZE(AWF_MAX)
#define awf_has(f, flag)        flag_has_dbg(f, AWF_SIZE, flag, #f, #flag)
#define awf_next(f, flag)       flag_next(f, AWF_SIZE, flag)
#define awf_is_empty(f)         flag_is_empty(f, AWF_SIZE)
#define awf_is_full(f)          flag_is_full(f, AWF_SIZE)
#define awf_is_inter(f1, f2)    flag_is_inter(f1, f2, AWF_SIZE)
#define awf_is_subset(f1, f2)   flag_is_subset(f1, f2, AWF_SIZE)
#define awf_is_equal(f1, f2)    flag_is_equal(f1, f2, AWF_SIZE)
#define awf_on(f, flag)         flag_on_dbg(f, AWF_SIZE, flag, #f, #flag)
#define awf_off(f, flag)        flag_off(f, AWF_SIZE, flag)
#define awf_wipe(f)             flag_wipe(f, AWF_SIZE)
#define awf_setall(f)           flag_setall(f, AWF_SIZE)
#define awf_negate(f)           flag_negate(f, AWF_SIZE)
#define awf_copy(f1, f2)        flag_copy(f1, f2, AWF_SIZE)
#define awf_union(f1, f2)       flag_union(f1, f2, AWF_SIZE)
#define awf_comp_union(f1, f2)  flag_comp_union(f1, f2, AWF_SIZE)
#define awf_inter(f1, f2)       flag_inter(f1, f2, AWF_SIZE)
#define awf_diff(f1, f2)        flag_diff(f1, f2, AWF_SIZE)

struct angband_term {
	term term;
	/* offset_x and offset_y are used by terms
	 * that display game map (dungeon) */
	int offset_x;
	int offset_y;

	bitflag flags[AWF_SIZE];
};

/*
 * these terms MUST be initialized at the start of the game
 * by the frontend. Recommended placement of these terms:
 * angband_term_cave in the center
 * angband_term_message_line above angband_term_cave
 * angband_term_status_line below angband_term_cave
 * (see Angband sirca 4.0.5)
 */
extern struct angband_term angband_cave;
extern struct angband_term angband_message_line;
extern struct angband_term angband_status_line;

struct angband_user_terms {
	struct angband_term *terms;
	char *names;
	size_t number;
};

/* what is displayed in these terms can be controlled by the user;
 * it is recommended to create several of those */
extern struct angband_user_terms angband_terms;

byte monster_health_attr(struct monster *mon);
void cnv_stat(int val, char *out_val, size_t out_len);
void idle_update(void);
void toggle_inven_equip(void);
void subwindows_set_flags(u32b *new_flags, size_t n_subwindows);
void init_display(void);

#endif /* UI2_DISPLAY_H */
