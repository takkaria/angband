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

/* angband_term flags which determine what a particular term does */
enum {
	#define ATF(a, b) ATF_ ##a,
	#include "list-term-flags.h"
	#undef ATF
	ATF_MAX
};

#define ATF_SIZE                FLAG_SIZE(ATF_MAX)
#define atf_has(f, flag)        flag_has_dbg(f, ATF_SIZE, flag, #f, #flag)
#define atf_next(f, flag)       flag_next(f, ATF_SIZE, flag)
#define atf_is_empty(f)         flag_is_empty(f, ATF_SIZE)
#define atf_is_full(f)          flag_is_full(f, ATF_SIZE)
#define atf_is_inter(f1, f2)    flag_is_inter(f1, f2, ATF_SIZE)
#define atf_is_subset(f1, f2)   flag_is_subset(f1, f2, ATF_SIZE)
#define atf_is_equal(f1, f2)    flag_is_equal(f1, f2, ATF_SIZE)
#define atf_on(f, flag)         flag_on_dbg(f, ATF_SIZE, flag, #f, #flag)
#define atf_off(f, flag)        flag_off(f, ATF_SIZE, flag)
#define atf_wipe(f)             flag_wipe(f, ATF_SIZE)
#define atf_setall(f)           flag_setall(f, ATF_SIZE)
#define atf_negate(f)           flag_negate(f, ATF_SIZE)
#define atf_copy(f1, f2)        flag_copy(f1, f2, ATF_SIZE)
#define atf_union(f1, f2)       flag_union(f1, f2, ATF_SIZE)
#define atf_comp_union(f1, f2)  flag_comp_union(f1, f2, ATF_SIZE)
#define atf_inter(f1, f2)       flag_inter(f1, f2, ATF_SIZE)
#define atf_diff(f1, f2)        flag_diff(f1, f2, ATF_SIZE)

extern const char *atf_descr[ATF_MAX];

struct angband_term {
	term term;
	/* offset_x and offset_y are used by terms
	 * that display game map (dungeon) */
	int offset_x;
	int offset_y;

	bitflag flags[ATF_SIZE];
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

uint32_t monster_health_attr(struct monster *mon);
void cnv_stat(int val, char *out_val, size_t out_len);
void idle_update(void);
void toggle_inven_equip(void);
void subwindows_set_flags(u32b *new_flags, size_t n_subwindows);
void init_display(void);

#endif /* UI2_DISPLAY_H */
