/**
 * \file ui2-target.h
 * \brief UI for targeting code
 *
 * Copyright (c) 1997-2014 Angband contributors
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

#ifndef UI2_TARGET_H
#define UI2_TARGET_H

#include "z-type.h"
#include "ui2-event.h"

/*
 * Time to pause (in milliseconds) after targeting a monster with "'" command
 */
#define TARGET_CLOSEST_DELAY 150

int target_dir(struct keypress key);
int target_dir_allow(struct keypress key, bool allow_5);
void textui_target(void);
void textui_target_closest(void);
bool target_set_interactive(int mode, struct loc coords);

#endif /* UI2_TARGET_H */
