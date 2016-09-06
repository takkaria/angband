/**
 * \file ui2-wizard.h
 * \brief Debug mode commands, stats collection, spoiler generation
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

#ifndef UI2_WIZARD_H
#define UI2_WIZARD_H

/* ui2-wiz-debug.c */
void wiz_cheat_death(void);
void get_debug_command(void);

/* ui2-wiz-stats.c */
void stats_collect(void);
void disconnect_stats(void);
void pit_stats(void);

/* ui2-wiz-spoil.c */
void do_cmd_spoilers(void);

#endif /* UI2_WIZARD_H */
