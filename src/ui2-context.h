/**
 * \file ui2-context.h
 * \brief Show player and terrain context menus.
 *
 * Copyright (c) 2011 Brett Reid
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband license":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef UI2_CONTEXT_H
#define UI2_CONTEXT_H

#include "cave.h"
#include "z-type.h"
#include "ui2-input.h"

void context_menu_player(struct loc mloc);
void context_menu_cave(struct chunk *cave,
		struct loc loc, bool adjacent, struct loc mloc);
void context_menu_object(struct object *obj);
void context_menu_command(struct loc mloc);
void textui_process_click(ui_event event);
struct cmd_info *textui_action_menu_choose(void);

#endif /* UI2_CONTEXT_H */
