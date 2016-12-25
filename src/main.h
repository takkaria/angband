/**
 * \file main.h
 * \brief Core game initialisation for UNIX (and other) machines
 *
 * Copyright (c) 1997 Ben Harrison, and others
 * Copyright (c) 2002 Robert Ruehlmann
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

#ifndef INCLUDED_MAIN_H
#define INCLUDED_MAIN_H

#include "angband.h"

extern errr init_x11(int argc, char **argv);
extern errr init_gcu(int argc, char **argv);
extern errr init_sdl(int argc, char **argv);
extern errr init_sdl2(int argc, char **argv);
extern errr init_test(int argc, char **argv);
extern errr init_stats(int argc, char **argv);
extern errr init_ncurses(int argc, char **argv);

extern const char help_x11[];
extern const char help_gcu[];
extern const char help_sdl[];
extern const char help_sdl2[];
extern const char help_test[];
extern const char help_stats[];
extern const char help_ncurses[];

//phantom server play
extern bool arg_force_name;


struct module
{
	const char *name;
	const char *help;
	errr (*init)(int argc, char **argv);
};

#endif /* INCLUDED_MAIN_H */
