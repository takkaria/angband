/**
 * \file ui2-command.h
 * \brief Deal with UI only command processing.
 *
 * Copyright (c) 1997-2014 Angband developers
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

#ifndef UI2_COMMAND_H
#define UI2_COMMAND_H

void do_cmd_redraw(void);
void do_cmd_options_and_redraw(void);
void do_cmd_unknown(void);
void do_cmd_version(void);
void textui_cmd_suicide(void);
void textui_cmd_debug(void);
void textui_cmd_rest(void);
void textui_quit(void);

#endif /* UI2_COMMAND_H */
