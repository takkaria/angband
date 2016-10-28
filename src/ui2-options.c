/**
 * \file ui2-options.c
 * \brief Text UI options handling code (everything accessible from '=')
 *
 * Copyright (c) 1997-2000 Robert A. Koeneke, James E. Wilson, Ben Harrison
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
#include "angband.h"
#include "cmds.h"
#include "game-input.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "ui2-display.h"
#include "ui2-help.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-knowledge.h"
#include "ui2-menu.h"
#include "ui2-options.h"
#include "ui2-prefs.h"
#include "ui2-target.h"

/**
 * Prompt the user for a filename to save the pref file to.
 */
static bool get_pref_path(char *buf, size_t bufsize, const char *title)
{
	char prompt[ANGBAND_TERM_TEXTBLOCK_WIDTH];
	strnfmt(prompt, sizeof(prompt), "%s to a pref file: ", title);

	char filename[ANGBAND_TERM_TEXTBLOCK_WIDTH];
	/* Get the filesystem-safe name and append .prf */
	player_safe_name(filename, sizeof(filename), player->full_name, true);
	my_strcat(filename, ".prf", sizeof(filename));

	bool use_filename =
		askfor_popup(prompt, filename, sizeof(filename),
			ANGBAND_TERM_TEXTBLOCK_WIDTH, TERM_POSITION_CENTER, NULL, NULL);

	if (use_filename) {
		path_build(buf, bufsize, ANGBAND_DIR_USER, filename);
	}

	return use_filename;
}

static void dump_pref_file(void (*dump)(ang_file *), const char *title)
{
	char buf[1024];

	/* Get filename from user */
	if (get_pref_path(buf, sizeof(buf), title)) {
		/* Try to save */
		if (prefs_save(buf, dump, title)) {
			msg("Saved %s.", strstr(title, " ") + 1);
		} else {
			msg("Failed to save %s.", strstr(title, " ") + 1);
		}

		event_signal(EVENT_MESSAGE_FLUSH);
	}
}

/**
 * ------------------------------------------------------------------------
 * Options display and setting
 * ------------------------------------------------------------------------
 */

/**
 * Displays an option entry.
 */
static void option_toggle_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	uint32_t attr = menu_row_style(true, cursor);
	bool *options = menu_priv(menu);

	c_prt(attr,
			format("%-45s: %3s (%s)",
				option_desc(index),
				options[index] ? "yes" : "no ",
				option_name(index)),
			loc);
}

/**
 * Handle keypresses for an option entry.
 */
static bool option_toggle_handle(struct menu *menu,
		const ui_event *event, int index)
{
	if (event->type == EVT_SELECT) {
		/* Birth options can not be toggled after birth
		 * After birth, menu->flags has MN_NO_TAGS */
		if (!mnflag_has(menu->flags, MN_NO_TAGS)) {
			option_set(option_name(index), !player->opts.opt[index]);
		}
	} else if (event->type == EVT_KBRD) {
		if (event->key.code == 'y' || event->key.code == 'Y') {
			option_set(option_name(index), true);
		} else if (event->key.code == 'n' || event->key.code == 'N') {
			option_set(option_name(index), false);
		} else if (event->key.code == 't' || event->key.code == 'T') {
			option_set(option_name(index), !player->opts.opt[index]);
		} else if (event->key.code == '?') {
			Term_visible(false);
			show_help(format("option.txt#%s", option_name(index)));
			Term_visible(true);
		} else {
			return false;
		}
	} else {
		return false;
	}

	return true;
}

/**
 * Toggle option menu display and handling functions
 */
static const menu_iter option_toggle_iter = {
	.display_row = option_toggle_display,
	.row_handler = option_toggle_handle
};

/**
 * Interact with some options
 */
static void option_toggle_menu(const char *name, int page)
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, &option_toggle_iter);

	/* for all menus */
	menu->prompt = "Set option (y/n/t), '?' for information";
	menu->command_keys = "?YyNnTt";
	menu->selections = "abcdefghijklmopqrsuvwxz";
	mnflag_on(menu->flags, MN_DBL_TAP);

	/* We add OPT_PAGE_MAX onto the page amount to indicate we're at birth */
	if (page == OPT_PAGE_BIRTH) {
		menu->prompt =
			"You can only modify these options at character birth. " /* concat */
			"'?' for information";
		menu->command_keys = "?";
		/* Only view these options */
		mnflag_on(menu->flags, MN_NO_TAGS);
	} else if (page == OPT_PAGE_BIRTH + OPT_PAGE_MAX) {
		page -= OPT_PAGE_MAX;
	}

	/* Find the number of valid entries */
	int count = 0;
	while (count < OPT_PAGE_PER
			&& option_page[page][count] != OPT_none)
	{
		count++;
	}

	/* Set the data to the player's options */
	menu_setpriv(menu, OPT_MAX, &player->opts.opt);
	menu_set_filter(menu, option_page[page], count);

	/* Run the menu */
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, name, COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(menu);

	menu_select(menu);

	menu_free(menu);
	Term_pop();
}

/**
 * Edit birth options.
 */
void do_cmd_options_birth(void)
{
	option_toggle_menu("Birth options", OPT_PAGE_BIRTH + OPT_PAGE_MAX);
}

static void do_cmd_option_toggle_menu(const char *name, int page)
{
	Term_visible(false);
	option_toggle_menu(name, page);
	Term_visible(true);
}

/**
 * ------------------------------------------------------------------------
 * Interact with keymaps
 * ------------------------------------------------------------------------ 
 */

/**
 * Current (or recent) keymap action
 */
static struct keypress keymap_buffer[KEYMAP_ACTION_MAX + 1];

/**
 * Ask for, and display, a keymap trigger.
 *
 * Returns the trigger input.
 */
static struct keypress keymap_get_trigger(void)
{
	struct keypress keys[2] = {KEYPRESS_NULL, KEYPRESS_NULL};

	event_signal(EVENT_INPUT_FLUSH);

	keys[0] = inkey_only_key();

	char text[ANGBAND_TERM_STANDARD_WIDTH];
	keypress_to_text(text, sizeof(text), keys, false);

	Term_puts(sizeof(text), COLOUR_L_BLUE, text);

	return keys[0];
}

/**
 * Keymap menu action functions
 */

static void ui_keymap_pref_append(const char *title, int index)
{
	(void) title;
	(void) index;

	dump_pref_file(keymap_dump, "Dump keymaps");
}

static void ui_keymap_query(const char *title, int index)
{
	(void) index;

	struct term_hints hints = {
		.width = 50,
		.height = 4,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	Term_add_tab(0, title, COLOUR_WHITE, COLOUR_DARK);

	struct keypress done = EVENT_EMPTY;

	do {
		struct loc loc = {1, 0};

		Term_erase_all();

		prt("Key: ", loc);

		Term_cursor_visible(true);
		Term_flush_output();
		
		/* Get a keymap trigger & mapping */
		struct keypress key = keymap_get_trigger();
		const struct keypress *act = keymap_find(KEYMAP_MODE_OPT, key);

		if (act == NULL) {
			loc.x -= 1;
			loc.y += 2;
			prt("No keymap with that trigger.", loc);
		} else {
			char tmp[1024];
			keypress_to_text(tmp, sizeof(tmp), act, false);
		
			loc.y++;
			prt("Action: ", loc);
			Term_puts(sizeof(tmp), COLOUR_L_BLUE, tmp);
		}

		loc.x = 0;
		loc.y = hints.height - 1;
		put_str_h("Press `ESC` to exit, any other key to continue.",
				loc, COLOUR_WHITE, COLOUR_L_BLUE);

		Term_cursor_visible(false);
		Term_flush_output();
		
		done = inkey_only_key();
	} while (done.code != ESCAPE);

	Term_pop();
}

static void ui_keymap_print_help(struct loc loc)
{
	put_str_h("Use `Ctrl-U` to reset.", loc, COLOUR_WHITE, COLOUR_L_GREEN);
	loc.y++;

	put_str_h("Press `$` when finished.", loc, COLOUR_WHITE, COLOUR_L_GREEN);
	loc.y++;

	c_prt(COLOUR_WHITE,
			format("(Maximum keymap length is %d keys.)", KEYMAP_ACTION_MAX),
			loc);
}

static void ui_keymap_erase_help(struct loc loc)
{
	for (int y = 0; y < 3; y++) {
		Term_erase_line(loc.x, loc.y + y);
	}
}

static void ui_keymap_edit(struct loc loc)
{
	struct loc help_loc = {0, loc.y + 2};
	ui_keymap_print_help(help_loc);

	prt("Action: ", loc);
	Term_get_cursor(&loc.x, &loc.y, NULL);

	size_t keymap_buffer_index = 0;
	bool done = false;

	while (!done) {
		char text[1024];

		int color = keymap_buffer_index < KEYMAP_ACTION_MAX ?
			COLOUR_L_BLUE: COLOUR_L_RED;

		keypress_to_text(text, sizeof(text), keymap_buffer, false);

		if (text[0] != 0) {
			c_prt(color, text, loc);
		} else {
			Term_erase_line(loc.x, loc.y);
			Term_cursor_to_xy(loc.x, loc.y);
		}

		Term_flush_output();

		struct keypress kp = inkey_only_key();

		if (kp.code == '$') {
			done = true;
		} else {
			switch (kp.code) {
				case KC_DELETE:
				case KC_BACKSPACE:
					if (keymap_buffer_index > 0) {
						keymap_buffer_index--;
						keymap_buffer[keymap_buffer_index].type = EVT_NONE;
						keymap_buffer[keymap_buffer_index].code = 0;
						keymap_buffer[keymap_buffer_index].mods = 0;
					}
					break;

				case KTRL('U'):
					memset(keymap_buffer, 0, sizeof(keymap_buffer));
					keymap_buffer_index = 0;
					break;

				default:
					if (keymap_buffer_index < KEYMAP_ACTION_MAX) {
						if (keymap_buffer_index == 0) {
							memset(keymap_buffer, 0, sizeof(keymap_buffer));
						}
						keymap_buffer[keymap_buffer_index++] = kp;
					}
					break;
			}
		}
	}

	ui_keymap_erase_help(help_loc);
}

static void ui_keymap_create(const char *title, int index)
{
	(void) index;

	struct term_hints hints = {
		.width = 50,
		.height = 6,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	Term_add_tab(0, title, COLOUR_WHITE, COLOUR_DARK);

	struct keypress done = EVENT_EMPTY;

	do {
		struct loc loc = {1, 0};

		Term_erase_all();

		prt("Key: ", loc);

		Term_cursor_visible(true);
		Term_flush_output();

		struct keypress trigger = keymap_get_trigger();

		if (trigger.code == '$') {
			loc.x = 0;
			loc.y = hints.height - 2;
			c_prt(COLOUR_L_RED, "The '$' key is reserved.", loc);
		} else if (trigger.code != 0) {
			memset(keymap_buffer, 0, sizeof(keymap_buffer));

			loc.y++;
			ui_keymap_edit(loc);

			if (keymap_buffer[0].type != EVT_NONE) {
				loc.x = 0;
				loc.y = hints.height - 1;

				prt("Save this keymap? [y/n] ", loc);
				Term_flush_output();
				loc.y--;

				struct keypress key = inkey_only_key();
				if (key.code == 'y' || key.code == 'Y' || key.code == KC_ENTER) {
					keymap_add(KEYMAP_MODE_OPT, trigger, keymap_buffer, true);
					prt("Keymap added.", loc);
				} else {
					prt("Keymap not added.", loc);
				}
			}
		}

		loc.x = 0;
		loc.y = hints.height - 1;
		put_str_h("Press `ESC` to exit, any other key to continue.",
				loc, COLOUR_WHITE, COLOUR_L_BLUE);

		Term_cursor_visible(false);
		Term_flush_output();

		done = inkey_only_key();
	} while (done.code != ESCAPE);

	Term_pop();
}

static void ui_keymap_remove(const char *title, int index)
{
	(void) index;

	struct term_hints hints = {
		.width = 50,
		.height = 5,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);
	Term_add_tab(0, title, COLOUR_WHITE, COLOUR_DARK);

	struct keypress done = EVENT_EMPTY;

	do {
		struct loc loc = {1, 0};

		Term_erase_all();

		prt("Key: ", loc);

		Term_cursor_visible(true);
		Term_flush_output();

		struct keypress trigger = keymap_get_trigger();
		const struct keypress *act = keymap_find(KEYMAP_MODE_OPT, trigger);
		
		if (act != NULL) {
			char tmp[1024];
			keypress_to_text(tmp, sizeof(tmp), act, false);
		
			loc.y++;
			prt("Action: ", loc);
			Term_puts(sizeof(tmp), COLOUR_L_BLUE, tmp);

			loc.x = 0;
			loc.y = hints.height - 1;
			prt("Remove this keymap? [y/n] ", loc);
			Term_flush_output();

			loc.y = hints.height - 2;

			struct keypress key = inkey_only_key();
			if (key.code == 'y' || key.code == 'Y' || key.code == KC_ENTER) {
				if (keymap_remove(KEYMAP_MODE_OPT, trigger)) {
					prt("Keymap removed.", loc);
				} else {
					prt("Error - can't remove keymap.", loc);
				}
			} else {
				prt("Keymap not removed.", loc);
			}
		} else {
			loc.x = 0;
			loc.y = hints.height - 2;
			prt("Keymap not found.", loc);
		}

		loc.y = hints.height - 1;
		put_str_h("Press `ESC` to exit, any other key to continue.",
				loc, COLOUR_WHITE, COLOUR_L_BLUE);

		Term_cursor_visible(false);
		Term_flush_output();

		done = inkey_only_key();
	} while (done.code != ESCAPE);

	Term_pop();
}

static struct menu *keymap_menu;

static menu_action keymap_actions[] = {
	{0, 0, "Query a keymap",       ui_keymap_query},
	{0, 0, "Create a keymap",      ui_keymap_create},
	{0, 0, "Remove a keymap",      ui_keymap_remove},
	{0, 0, "Save keymaps to file", ui_keymap_pref_append},
};

static void do_cmd_keymaps(const char *title, int index)
{
	(void) index;

	Term_visible(false);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Keymap menu", COLOUR_WHITE, COLOUR_DARK);

	if (!keymap_menu) {
		keymap_menu = menu_new_action(keymap_actions, N_ELEMENTS(keymap_actions));
		keymap_menu->selections = lower_case;
	}

	menu_layout_term(keymap_menu);
	menu_select(keymap_menu);

	Term_pop();

	Term_visible(true);
}

/**
 * ------------------------------------------------------------------------
 * Non-complex menu actions
 * ------------------------------------------------------------------------
 */

/**
 * Set base delay factor
 */
static void do_cmd_delay(const char *name, int index)
{
	(void) name;
	(void) index;

	const char *prompt =
		"New animation delay (0-255 milliseconds): ";

	char buf[4] = {0};
	strnfmt(buf, sizeof(buf), "%d", player->opts.delay_factor);

	/* Ask for a numeric value */
	if (askfor_popup(prompt, buf, sizeof(buf),
				ANGBAND_TERM_TEXTBLOCK_WIDTH, TERM_POSITION_CENTER,
				NULL, askfor_numbers))
	{
		u16b val = strtoul(buf, NULL, 0);
		player->opts.delay_factor = MIN(val, 255);
	}
}

/**
 * Set hitpoint warning level
 */
static void do_cmd_hp_warn(const char *name, int index)
{
	(void) name;
	(void) index;

	const char *prompt = 
		"New hitpoint warning (0-9): ";

	char buf[4] = {0};
	strnfmt(buf, sizeof(buf), "%d", player->opts.hitpoint_warn);

	if (askfor_popup(prompt, buf, sizeof(buf),
				ANGBAND_TERM_TEXTBLOCK_WIDTH, TERM_POSITION_CENTER,
				NULL, askfor_numbers))
	{
		byte warn = strtoul(buf, NULL, 0);
		if (warn > 9) {
			warn = 0;
		}
		player->opts.hitpoint_warn = warn;
	}
}

/**
 * Set lazy movement delay
 */
static void do_cmd_lazymove_delay(const char *name, int index)
{
	(void) name;
	(void) index;

	const char *prompt = "New input delay: ";

	char buf[4] = {0};
	strnfmt(buf, sizeof(buf), "%d", player->opts.lazymove_delay);

	if (askfor_popup(prompt, buf, sizeof(buf),
				ANGBAND_TERM_TEXTBLOCK_WIDTH, TERM_POSITION_CENTER,
				NULL, askfor_numbers))
	{
		player->opts.lazymove_delay = strtoul(buf, NULL, 0);
	}
}

/**
 * Ask for a user pref file and process it.
 */
static void do_cmd_pref_file(const char *prompt)
{
	if (prompt == NULL) {
		prompt = "File: ";
	}

	/* Default filename */
	char filename[ANGBAND_TERM_STANDARD_WIDTH];
	/* Get the filesystem-safe name and append .prf */
	player_safe_name(filename, sizeof(filename), player->full_name, true);
	my_strcat(filename, ".prf", sizeof(filename));

	/* Ask for a file (or cancel) */
	if (askfor_popup(prompt, filename, sizeof(filename),
				ANGBAND_TERM_TEXTBLOCK_WIDTH, TERM_POSITION_CENTER,
				NULL, NULL))
	{
		if (!process_pref_file(filename, false, true)) {
			msg("Failed to load '%s'!", filename);
		} else {
			msg("Loaded '%s'.", filename);
		}
	}
}
 
/**
 * Write autoinscriptions to a file.
 */
static void do_dump_autoinscrip(const char *title, int index)
{
	(void) title;
	(void) index;

	dump_pref_file(dump_autoinscriptions, "Dump autoinscriptions");
}

/**
 * Load a pref file.
 */
static void options_load_pref_file(const char *title, int index)
{
	(void) title;
	(void) index;

	do_cmd_pref_file(NULL);
}

/**
 * Load a pref line.
 */
static void options_load_pref_line(const char *title, int index)
{
	(void) title;
	(void) index;

	do_cmd_pref();
}

/**
 * ------------------------------------------------------------------------
 * Ego item ignore menu
 * ------------------------------------------------------------------------
 */

/**
 * Skip common prefixes in ego item names.
 */
static const char *strip_ego_name(const char *name)
{
	if (prefix(name, "of the ")) {
		return name + 7;
	} else if (prefix(name, "of ")) {
		return name + 3;
	} else {
		return name;
	}
}

/*
 * Find size of the prefix, stripped in strip_ego_name()
 */
static ptrdiff_t find_prefix_size(const char *str, const char *maybe_suffix)
{
	const char *suffix = strstr(str, maybe_suffix);

	if (suffix != NULL && streq(suffix, maybe_suffix)) {
		return suffix - str;
	} else {
		return 0;
	}
}

/**
 * Display an ego item type on the screen.
 */
int ego_item_name(char *buf, size_t buf_size, struct ego_desc *desc)
{
	struct ego_item *ego = &e_info[desc->e_idx];

	/* Find the ignore type */
	size_t i = 0;
	while (i < N_ELEMENTS(quality_choices) && desc->itype != i) {
		i++;
	}
	if (i == N_ELEMENTS(quality_choices)) {
		return 0;
	}

	/* Initialize the buffer */
	int end = my_strcpy(buf, "[ ] ", buf_size);
	/* Append the name */
	end += my_strcat(buf, quality_choices[i].name, buf_size);
	/* Append an extra space */
	end += my_strcat(buf, " ", buf_size);

	/* Get the length of the common prefix, if any */
	ptrdiff_t prefix_size = find_prefix_size(ego->name, desc->short_name);

	/* Found a prefix? */
	if (prefix_size > 0) {
		char prefix[64];

		/* Get a copy of the prefix */
		my_strcpy(prefix, ego->name, prefix_size);
		/* Append the prefix */
		end += my_strcat(buf, prefix, buf_size);
		/* Append an extra space */
		end += my_strcat(buf, " ", buf_size);
	}

	return end;
}

/**
 * Utility function used for sorting an array of ego-item indices by
 * ego item name.
 */
static int ego_comp_func(const void *a, const void *b)
{
	const struct ego_desc *egoa = a;
	const struct ego_desc *egob = b;

	/* Note the removal of common prefixes */
	return strcmp(egoa->short_name, egob->short_name);
}

/**
 * Display an entry on the sval menu
 */
static void ego_display(struct menu * menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct ego_desc *choice = menu->menu_data;
	bool ignored = ego_is_ignored(choice[index].e_idx, choice[index].itype);

	uint32_t attr = menu_row_style(true, cursor);
	uint32_t sq_attr = ignored ? COLOUR_L_RED : COLOUR_L_GREEN;

	/* Acquire the name of object */
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	ego_item_name(buf, sizeof(buf), &choice[index]);

	/* Print it */
	c_put_str(attr, buf, loc);

	/* Show ignore mark, if any */
	if (ignored) {
		loc.x++;
		c_put_str(COLOUR_L_RED, "*", loc);
		loc.x--;
	}

	/* Show the stripped ego item name using another colour */
	loc.x += strlen(buf);
	c_put_str(sq_attr, choice[index].short_name, loc);
}

/**
 * Deal with events on the sval menu
 */
static bool ego_action(struct menu * menu, const ui_event * event, int index)
{
	(void) event;

	struct ego_desc *choice = menu->menu_data;

	/* Toggle */
	if (event->type == EVT_SELECT) {
		ego_ignore_toggle(choice[index].e_idx, choice[index].itype);
		return true;
	} else {
		return false;
	}
}

/*
 * If 'choice' is NULL, it counts the number of ignorable egos, otherwise
 * it collects the list of egos into 'choice' as well.
 */
static int collect_ignorable_egos(struct ego_desc **choice)
{
	int max_choice = 0;

	/* Get the valid ego items */
	for (int e_idx = 0; e_idx < z_info->e_max; e_idx++) {
		struct ego_item *ego = &e_info[e_idx];

		/* Only valid known ego-items allowed */
		if (ego->name && ego->everseen) {
			/* Find appropriate ignore types */
			for (int itype = ITYPE_NONE + 1; itype < ITYPE_MAX; itype++) {
				if (ego_has_ignore_type(ego, itype)) {
					if (choice != NULL) {
						(*choice)[max_choice].e_idx = e_idx;
						(*choice)[max_choice].itype = itype;
						(*choice)[max_choice].short_name = strip_ego_name(ego->name);
					}
					max_choice++;
				}
			}
		}
	}

	return max_choice;
}

/**
 * Display list of ego items to be ignored.
 */
static void ego_menu(void)
{
	struct ego_desc *choice = mem_zalloc(z_info->e_max * ITYPE_MAX * sizeof(*choice));
	int max_choice = collect_ignorable_egos(&choice);

	if (max_choice == 0) {
		mem_free(choice);
		return;
	}

	/* Sort the array by ego item name */
	sort(choice, max_choice, sizeof(*choice), ego_comp_func);

	Term_visible(false);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Ego ignore menu", COLOUR_WHITE, COLOUR_DARK);
	Term_cursor_visible(true);

	/* Set up the menu */
	struct menu menu;
	menu_iter menu_f = {
		.display_row = ego_display,
		.row_handler = ego_action
	};
	menu_init(&menu, MN_SKIN_SCROLL, &menu_f);
	menu_setpriv(&menu, max_choice, choice);
	mnflag_on(menu.flags, MN_NO_TAGS);
	menu_set_cursor_x_offset(&menu, 1); /* Put cursor in brackets */
	menu_layout_term(&menu);

	menu_select(&menu);

	mem_free(choice);
	Term_pop();

	Term_visible(true);
}

/**
 * ------------------------------------------------------------------------
 * Quality ignore menu
 * ------------------------------------------------------------------------
 */

/**
 * Menu struct for differentiating aware from unaware ignore
 */
typedef struct {
	struct object_kind *kind;
	bool aware;
} ignore_choice;

/**
 * Ordering function for ignore choices.
 * Aware comes before unaware, and then sort alphabetically.
 */
static int cmp_ignore(const void *a, const void *b)
{
	const ignore_choice *choicea = a;
	const ignore_choice *choiceb = b;

	if (!choicea->aware && choiceb->aware) {
		return 1;
	} if (choicea->aware && !choiceb->aware) {
		return -1;
	}

	char bufa[ANGBAND_TERM_STANDARD_WIDTH];
	char bufb[ANGBAND_TERM_STANDARD_WIDTH];

	object_kind_name(bufa, sizeof(bufa), choicea->kind, choicea->aware);
	object_kind_name(bufb, sizeof(bufb), choiceb->kind, choiceb->aware);

	return strcmp(bufa, bufb);
}

/**
 * Display an entry in the menu.
 */
static void quality_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) menu;
	(void) width;

	index++;
	assert(index > ITYPE_NONE);
	assert(index < ITYPE_MAX);

	const char *level_name = quality_values[ignore_level[index]].name;
	const char *name = quality_choices[index].name;

	uint32_t attr = menu_row_style(true, cursor);
	c_put_str(attr, format("%-30s : %s", name, level_name), loc);
}

/**
 * Display the quality ignore subtypes.
 */
static void quality_subdisplay(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) menu;
	(void) width;

	uint32_t attr = menu_row_style(true, cursor);
	const char *name = quality_values[index].name;

	c_put_str(attr, name, loc);
}

/**
 * Handle keypresses.
 */
static bool quality_action(struct menu *m, const ui_event *e, int index)
{
	if (e->type == EVT_MOUSE) {
		return false;
	}

	index++;
	assert(index > ITYPE_NONE);
	assert(index < ITYPE_MAX);

	struct term_hints hints = {
		.x = 34,
		.y = index - m->top - 1,
		.width = 30,
		.height = IGNORE_MAX,
		.position = TERM_POSITION_EXACT,
		.purpose = TERM_PURPOSE_MENU
	};

	/* Work out how many options we have */
	int count = IGNORE_MAX;
	if (index == ITYPE_RING || index == ITYPE_AMULET) {
		count = IGNORE_BAD + 1;
		hints.height = count;
	}

	Term_push_new(&hints);

	/* Run menu */
	struct menu menu;
	menu_iter menu_f = {
		.display_row = quality_subdisplay
	};
	menu_init(&menu, MN_SKIN_SCROLL, &menu_f);
	menu_setpriv(&menu, count, quality_values);
	menu.cursor = ignore_level[index];
	mnflag_on(menu.flags, MN_NO_TAGS);
	menu_layout_term(&menu);

	ui_event event = menu_select(&menu);

	/* Set the new value appropriately */
	if (event.type == EVT_SELECT) {
		ignore_level[index] = menu.cursor;
	}

	Term_pop();

	return true;
}

/**
 * Display quality ignore menu.
 */
static void quality_menu(void)
{
	Term_visible(false);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Quality ignore menu", COLOUR_WHITE, COLOUR_DARK);

	/* Set up the menu */
	struct menu menu;
	menu_iter menu_f = {
		.display_row = quality_display,
		.row_handler = quality_action
	};
	menu_init(&menu, MN_SKIN_SCROLL, &menu_f);
	/* Take into account ITYPE_NONE - we don't want to display that */
	menu_setpriv(&menu, ITYPE_MAX - 1, quality_values);
	mnflag_on(menu.flags, MN_NO_TAGS);
	menu_layout_term(&menu);

	menu_select(&menu);

	Term_pop();

	Term_visible(true);
}

/**
 * ------------------------------------------------------------------------
 * Sval ignore menu
 * ------------------------------------------------------------------------
 */

/**
 * Structure to describe tval/description pairings.
 */
typedef struct {
	int tval;
	const char *desc;
} tval_desc;

/**
 * Categories for sval-dependent ignore.
 */
static tval_desc sval_dependent[] = {
	{TV_STAFF,       "Staffs"},
	{TV_WAND,        "Wands"},
	{TV_ROD,         "Rods"},
	{TV_SCROLL,      "Scrolls"},
	{TV_POTION,      "Potions"},
	{TV_RING,        "Rings"},
	{TV_AMULET,      "Amulets"},
	{TV_FOOD,        "Food"},
	{TV_MUSHROOM,    "Mushrooms"},
	{TV_MAGIC_BOOK,  "Magic books"},
	{TV_PRAYER_BOOK, "Prayer books"},
	{TV_LIGHT,       "Lights"},
	{TV_FLASK,       "Flasks of oil"},
	{TV_GOLD,        "Money"},
};

/**
 * Determines whether a tval is eligible for sval-ignore.
 */
bool ignore_tval(int tval)
{
	/* Only ignore if the tval's allowed */
	for (size_t i = 0; i < N_ELEMENTS(sval_dependent); i++) {
		if (tval == sval_dependent[i].tval) {
			return true;
		}
	}

	return false;
}

/**
 * Display an entry on the sval menu
 */
static void ignore_sval_menu_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	const ignore_choice *choice = menu_priv(menu);

	struct object_kind *kind = choice[index].kind;
	bool aware = choice[index].aware;

	uint32_t attr = menu_row_style(aware, cursor);

	/* Acquire the name of object */
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	object_kind_name(buf, sizeof(buf), kind, aware);

	c_put_str(attr, format("[ ] %s", buf), loc);
	if ((aware && (kind->ignore & IGNORE_IF_AWARE))
			|| (!aware && (kind->ignore & IGNORE_IF_UNAWARE)))
	{
		loc.x++;
		c_put_str(COLOUR_L_RED, "*", loc);
	}
}

/**
 * Deal with events on the sval menu
 */
static bool ignore_sval_menu_action(struct menu *menu,
		const ui_event *event, int index)
{
	const ignore_choice *choice = menu_priv(menu);

	if (event->type == EVT_SELECT
			|| (event->type == EVT_KBRD && tolower(event->key.code) == 't'))
	{
		struct object_kind *kind = choice[index].kind;

		/* Toggle the appropriate flag */
		if (choice[index].aware) {
			kind->ignore ^= IGNORE_IF_AWARE;
		} else {
			kind->ignore ^= IGNORE_IF_UNAWARE;
		}

		player->upkeep->notice |= PN_IGNORE;

		return true;
	}

	return false;
}

static const menu_iter ignore_sval_menu = {
	.display_row = ignore_sval_menu_display,
	.row_handler = ignore_sval_menu_action,
};

/**
 * Collect all tvals in the big ignore_choice array
 */
static int ignore_collect_kind(int tval, ignore_choice **c)
{
	int n_choices = 0;

	/* Create the array, with entries both for aware and unaware ignore */
	ignore_choice *choice = mem_zalloc(2 * z_info->k_max * sizeof(*choice));

	for (int i = 1; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		/* Skip empty objects, unseen objects, and incorrect tvals */
		if (!kind->name || kind->tval != tval) {
			continue;
		}

		if (!kind->aware) {
			/* can unaware ignore anything */
			choice[n_choices].kind = kind;
			choice[n_choices].aware = false;
			n_choices++;
		}

		if ((kind->everseen && !kf_has(kind->kind_flags, KF_INSTA_ART))
				|| tval_is_money_k(kind))
		{
			/* Do not display the artifact base kinds in this list 
			 * aware ignore requires everseen 
			 * do not require awareness for aware ignore, so people can set 
			 * at game start */
			choice[n_choices].kind = kind;
			choice[n_choices].aware = true;
			n_choices++;
		}
	}

	if (n_choices == 0) {
		mem_free(choice);
	} else {
		*c = choice;
	}

	return n_choices;
}

/**
 * Display list of svals to be ignored.
 */
static bool sval_menu(int tval, const char *desc)
{
	ignore_choice *choices;
	int n_choices = ignore_collect_kind(tval, &choices);
	if (n_choices == 0) {
		return false;
	}

	/* Sort by name in ignore menus except for categories of items that are
	 * aware from the start */
	switch (tval) {
		case TV_LIGHT:
		case TV_MAGIC_BOOK:
		case TV_PRAYER_BOOK:
		case TV_DRAG_ARMOR:
		case TV_GOLD:
			/* leave sorted by sval */
			break;

		default:
			/* sort by awareness and name */
			sort(choices, n_choices, sizeof(*choices), cmp_ignore);
			break;
	}

	Term_visible(false);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_cursor_visible(true);

	char title[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(title, sizeof(title), "Ignore the following %s", desc);
	Term_add_tab(0, title, COLOUR_WHITE, COLOUR_DARK);

	/* Run menu */
	struct menu *menu = menu_new(MN_SKIN_COLUMNS, &ignore_sval_menu);
	menu_setpriv(menu, n_choices, choices);
	menu->command_keys = "Tt";
	menu_set_cursor_x_offset(menu, 1); /* Place cursor in brackets. */
	mnflag_on(menu->flags, MN_NO_TAGS);
	menu_layout_term(menu);

	menu_select(menu);

	mem_free(choices);
	menu_free(menu);
	Term_pop();

	Term_visible(true);

	return true;
}

/**
 * Returns true if there's anything to display a menu of
 */
static bool seen_tval(int tval)
{
	if (tval == TV_GOLD) {
		return true;
	}

	for (int i = 1; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		/* Skip empty objects, unseen objects, and incorrect tvals */
		if (kind->name && kind->everseen && kind->tval == tval) {
			return true;
		}
	}

	return false;
}

/**
 * Extra options on the "item options" menu
 */
static struct {
	char tag;
	const char *name;
	void (*action)(void);
} extra_item_options[] = {
	{'Q', "Quality ignoring options", quality_menu},
	{'E', "Ego ignoring options", ego_menu},
};

static char tag_options_item(struct menu *menu, int index)
{
	(void) menu;

	size_t line = index;

	if (line < N_ELEMENTS(sval_dependent)) {
		return I2A(index);
	}

	/* Separator - blank line. */
	if (line == N_ELEMENTS(sval_dependent)) {
		return 0;
	}

	line -= N_ELEMENTS(sval_dependent) + 1;

	if (line < N_ELEMENTS(extra_item_options)) {
		return extra_item_options[line].tag;
	}

	return 0;
}

static bool valid_options_item(struct menu *menu, int index)
{
	(void) menu;

	size_t line = index;

	if (line < N_ELEMENTS(sval_dependent)) {
		return true;
	}

	/* Separator - blank line. */
	if (line == N_ELEMENTS(sval_dependent)) {
		return false;
	}

	line -= N_ELEMENTS(sval_dependent) + 1;

	if (line < N_ELEMENTS(extra_item_options)) {
		return true;
	}

	return false;
}

static void display_options_item(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) menu;
	(void) width;

	size_t line = index;

	/* Most of the menu is svals, with a small "extra options" section below */
	if (line < N_ELEMENTS(sval_dependent)) {
		bool known = seen_tval(sval_dependent[line].tval);
		uint32_t attr = menu_row_style(known, cursor);

		c_prt(attr, sval_dependent[line].desc, loc);
	} else {
		line -= N_ELEMENTS(sval_dependent) + 1;
		if (line < N_ELEMENTS(extra_item_options)) {
			uint32_t attr;
			if (extra_item_options[line].tag == 'E') {
				attr = menu_row_style(collect_ignorable_egos(NULL) != 0, cursor);
			} else {
				attr = menu_row_style(true, cursor);
			}

			c_prt(attr, extra_item_options[line].name, loc);
		}
	}
}

static bool handle_options_item(struct menu *menu,
		const ui_event *event, int index)
{
	(void) menu;

	if (event->type == EVT_SELECT) {
		if ((size_t) index < N_ELEMENTS(sval_dependent)) {
			sval_menu(sval_dependent[index].tval, sval_dependent[index].desc);
		} else {
			index -= N_ELEMENTS(sval_dependent) + 1;
			assert((size_t) index < N_ELEMENTS(extra_item_options));
			extra_item_options[index].action();
		}

		return true;
	}

	return false;
}

static const menu_iter options_item_iter = {
	.get_tag     = tag_options_item,
	.valid_row   = valid_options_item,
	.display_row = display_options_item,
	.row_handler = handle_options_item,
};

/**
 * Display and handle the main ignoring menu.
 */
void do_cmd_options_item(const char *title, int index)
{
	(void) index;

	int count = N_ELEMENTS(sval_dependent) + N_ELEMENTS(extra_item_options) + 1;

	Term_visible(false);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, title, COLOUR_WHITE, COLOUR_DARK);

	struct menu menu;
	menu_init(&menu, MN_SKIN_SCROLL, &options_item_iter);
	menu_setpriv(&menu, count, NULL);
	menu_layout_term(&menu);

	menu_select(&menu);

	Term_pop();

	Term_visible(true);

	player->upkeep->notice |= PN_IGNORE;
}

/**
 * ------------------------------------------------------------------------
 * Main menu definitions and display
 * ------------------------------------------------------------------------
 */

static struct menu *option_menu;

static menu_action option_actions[] = {
	{0, 'a', "User interface options",              do_cmd_option_toggle_menu},
	{0, 'b', "Birth (difficulty) options",          do_cmd_option_toggle_menu},
	{0, 'x', "Cheat options",                       do_cmd_option_toggle_menu},
	{0, 'i', "Item ignoring setup",                 do_cmd_options_item},
	{0,  0,   NULL,                                 NULL},
	{0, 'd', "Set animation delay"  ,               do_cmd_delay},
	{0, 'h', "Set hitpoint warning",                do_cmd_hp_warn},
	{0, 'm', "Set input delay",                     do_cmd_lazymove_delay},
	{0,  0,   NULL,                                 NULL},
	{0, 's', "Auto-inscriptions setup",             do_cmd_knowledge_objects},
	{0, 't', "Save auto-inscriptions to pref file", do_dump_autoinscrip},
	{0,  0,   NULL,                                 NULL},
	{0, 'k', "Edit keymaps",                        do_cmd_keymaps},
	{0,  0,   NULL,                                 NULL},
	{0, 'f', "Load a user pref file",               options_load_pref_file},
	{0, 'l', "Load a single pref line",             options_load_pref_line},
};

/**
 * Display the options main menu.
 */
void do_cmd_options(void)
{
	if (!option_menu) {
		option_menu = menu_new_action(option_actions, N_ELEMENTS(option_actions));
		mnflag_on(option_menu->flags, MN_CASELESS_TAGS);
	}

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	Term_add_tab(0, "Options", COLOUR_WHITE, COLOUR_DARK);

	menu_layout_term(option_menu);
	menu_select(option_menu);

	Term_pop();
}

void cleanup_options(void)
{
	if (keymap_menu) menu_free(keymap_menu);
	if (option_menu) menu_free(option_menu);
}
