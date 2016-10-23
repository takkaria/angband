/**
 * \file ui2-prefs.h
 * \brief Pref file handling code
 *
 * Copyright (c) 2003 Takeshi Mogami, Robert Ruehlmann
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
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

#ifndef UI2_PREFS_H
#define UI2_PREFS_H

#include "cave.h"
#include "parser.h"
#include "option.h"
#include "ui2-display.h"
#include "ui2-keymap.h"
#include "ui2-term.h"
#include "z-file.h"

extern char arg_name[PLAYER_NAME_LEN];
extern int use_graphics;

extern uint32_t *monster_x_attr;
extern wchar_t *monster_x_char;
extern uint32_t *kind_x_attr;
extern wchar_t *kind_x_char;
extern uint32_t *feat_x_attr[LIGHTING_MAX];
extern wchar_t *feat_x_char[LIGHTING_MAX];
extern uint32_t *trap_x_attr[LIGHTING_MAX];
extern wchar_t *trap_x_char[LIGHTING_MAX];
extern uint32_t *flavor_x_attr;
extern wchar_t *flavor_x_char;

/**
 * Private data for pref file parsing.
 */
struct prefs_data {
	bool bypass;
	struct keypress keymap_buffer[KEYMAP_ACTION_MAX];
	bool user;
};

enum parser_error parse_prefs_dummy(struct parser *p);

void dump_monsters(ang_file *file);
void dump_objects(ang_file *file);
void dump_autoinscriptions(ang_file *file);
void dump_features(ang_file *file);
void dump_flavors(ang_file *file);
void dump_colors(ang_file *file);
bool prefs_save(const char *path, void (*dump)(ang_file *), const char *title);
errr process_pref_file_command(const char *buf);
bool process_pref_file(const char *name, bool quiet, bool user);
void reset_visuals(bool load_prefs);
void textui_prefs_init(void);
void textui_prefs_free(void);
void do_cmd_pref(void);

#endif /* UI2_PREFS_H */