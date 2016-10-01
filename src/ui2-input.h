/**
 * \file ui2-input.h
 * \brief Some high-level UI functions, inkey()
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

#ifndef UI2_INPUT_H
#define UI2_INPUT_H

#include "cmd-core.h"
#include "game-event.h"
#include "ui2-keymap.h"
#include "ui2-event.h"
#include "ui2-term.h"

/* milliseconds to wait between checking for events;
 * inkey_wait() takes the number of this periods as an argument */
#define INKEY_SCAN_PERIOD 10

/**
 * Holds a generic command - if cmd is set to other than CMD_NULL 
 * it simply pushes that command to the game, otherwise the hook 
 * function will be called.
 */
struct cmd_info {
	const char *desc;
	keycode_t key[KEYMAP_MODE_MAX];
	cmd_code cmd;
	void (*hook)(void);
	bool (*prereq)(void);
};

/**
 * A categorised list of all the command lists.
 */
struct command_list {
	const char *name;
	struct cmd_info *list;
	size_t len;
};

extern struct cmd_info cmd_item[];
extern struct cmd_info cmd_action[];
extern struct cmd_info cmd_item_manage[];
extern struct cmd_info cmd_info[];
extern struct cmd_info cmd_util[];
extern struct cmd_info cmd_hidden[];
extern struct command_list cmds_all[];

void inkey_flush(game_event_type unused, game_event_data *data, void *user);
struct keypress inkey_only_key(void);
ui_event inkey_mouse_or_key(void);
ui_event inkey_simple(void);
ui_event inkey_wait(int scans);
void inkey_any(void);

bool auto_more(void);

/**
 * Type of handler function for askfor_aux()
 * (such as used in editing char name, etc)
 *
 * \param buf is the buffer to edit
 * \param buflen is the total number of bytes in buffer
 * \param curs is the cursor position in buffer
 * \param len is the position of terminating (null) byte
 * \param key is the key input by the user
 * \param firsttime indicates whether it's the first key
 */
typedef bool (*askfor_aux_handler)(char *buf, size_t buflen,
		size_t *curs, size_t *len,
		struct keypress key, bool firsttime);

/**
 * Default handler for askfor_aux()
 */
bool askfor_aux_keypress(char *buf, size_t buflen,
		size_t *curs, size_t *len,
		struct keypress keypress, bool firsttime);

bool askfor_aux(char *buf, size_t len, askfor_aux_handler handler);

extern bool (*get_file)(const char *suggested_name, char *path, size_t pathlen);

bool get_character_name(char *buf, size_t buflen);
char get_char(const char *prompt, const char *options, size_t len, char fallback);
bool get_com_ex(const char *prompt, ui_event *command);
bool key_confirm_command(unsigned char c);
bool textui_process_key(struct keypress key, unsigned char *c);
ui_event textui_get_command(int *count);

void textui_input_init(void);

#endif /* UI2_INPUT_H */
