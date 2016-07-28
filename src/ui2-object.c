/**
 * \file ui2-object.c
 * \brief Object lists and selection, and other object-related UI functions
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007-9 Andi Sidwell, Chris Carr, Ed Graham, Erik Osheim
 * Copyright (c) 2015 Nick McConnell
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
#include "cave.h"
#include "cmd-core.h"
#include "cmds.h"
#include "effects.h"
#include "game-input.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-menu.h"
#include "ui2-object.h"
#include "ui2-options.h"
#include "ui2-output.h"
#include "ui2-prefs.h"

/**
 * ------------------------------------------------------------------------
 * Variables for object display and selection
 * ------------------------------------------------------------------------
 */
#define MAX_ITEMS 64

/**
 * Info about a particular object
 */
struct object_menu_data {
	struct {
		char str[80];
		size_t len;
		size_t size;
	} label;

	struct {
		char str[80];
		size_t len;
		size_t size;
	} equip;

	struct {
		char str[80];
		size_t len;
		size_t size;
	} name;

	struct object *object;
	char key;
};

struct object_menu_list {
	struct object_menu_data items[MAX_ITEMS];
	size_t size;
	size_t next;
	size_t max_len;
	size_t extra_fields_offset;
};

/**
 * ------------------------------------------------------------------------
 * Display of individual objects in lists or for selection
 * ------------------------------------------------------------------------
*/

/**
 * Determine if the attr and char should consider the item's flavor.
 * Identified scrolls should use their own tile.
 */
static bool use_flavor_glyph(const struct object_kind *kind)
{
	if (kind->tval == TV_SCROLL && kind->aware) {
		return false;
	} else {
		return kind->flavor;
	}
}

/**
 * Return the attr for a given item kind.  Use "lavor if available.
 * Default to user definitions.
 */
uint32_t object_kind_attr(const struct object_kind *kind)
{
	return use_flavor_glyph(kind) ?
		flavor_x_attr[kind->flavor->fidx] : kind_x_attr[kind->kidx];
}

/**
 * Return the char for a given item kind. Use flavor if available.
 * Default to user definitions.
 */
wchar_t object_kind_char(const struct object_kind *kind)
{
	return use_flavor_glyph(kind) ?
		flavor_x_char[kind->flavor->fidx] : kind_x_char[kind->kidx];
}

/**
 * Return the attr for a given item. Use flavor if available.
 * Default to user definitions.
 */
uint32_t object_attr(const struct object *obj)
{
	return object_kind_attr(obj->kind);
}

/**
 * Return the "char" for a given item.
 * Use "flavor" if available.
 * Default to user definitions.
 */
wchar_t object_char(const struct object *obj)
{
	return object_kind_char(obj->kind);
}

#define EXTRA_FIELD_WEIGHT_WIDTH 9
#define EXTRA_FIELD_PRICE_WIDTH 9
#define EXTRA_FIELD_FAIL_WIDTH 10

static void show_obj_extra(const struct object *obj,
		struct loc loc, olist_detail_t mode)
{
	char buf[80];
	const size_t bufsize = sizeof(buf);

	erase_line(loc);

	/* Price */
	if (mode & OLIST_PRICE) {
		struct store *store = store_at(cave, player->py, player->px);
		if (store) {
			int price = price_item(store, obj, true, obj->number);

			strnfmt(buf, bufsize, "%6d au", price);
			put_str_len(buf, loc, EXTRA_FIELD_PRICE_WIDTH);

			loc.x += EXTRA_FIELD_PRICE_WIDTH;
		}
	}

	/* Failure chance for magic devices and activations */
	if (mode & OLIST_FAIL && obj_can_fail(obj)) {
		int fail = (9 + get_use_device_chance(obj)) / 10;

		if (object_effect_is_known(obj)) {
			strnfmt(buf, bufsize, "%4d%% fail", fail);
		} else {
			my_strcpy(buf, "    ? fail", bufsize);
		}
		put_str_len(buf, loc, EXTRA_FIELD_FAIL_WIDTH);

		loc.x += EXTRA_FIELD_FAIL_WIDTH;
	}

	/* Weight */
	if (mode & OLIST_WEIGHT) {
		int weight = obj->weight * obj->number;

		strnfmt(buf, bufsize, "%4d.%1d lb", weight / 10, weight % 10);
		put_str_len(buf, loc, EXTRA_FIELD_WEIGHT_WIDTH);

		/* Just because (maybe stuff will be added later?) */
		loc.x += EXTRA_FIELD_WEIGHT_WIDTH;
	}
}

/**
 * Display an object.  Each object may be prefixed with a label.
 * Used by show_inven(), show_equip(), show_quiver() and show_floor().
 * Mode flags are documented in object.h
 */
static void show_obj(struct object_menu_data *data, 
		struct loc loc, size_t extra_fields_offset,
		bool cursor, olist_detail_t mode)
{
	const bool show_label =
		(mode & (OLIST_WINDOW | OLIST_DEATH)) ? true : false;

	/* Save that for later */
	struct loc saved_loc = loc;

	uint32_t attr = menu_row_style(true, cursor);

	size_t label_len = show_label ? data->label.len : 0;
	size_t equip_len = data->equip.len;
	size_t name_len  = data->name.len;

	erase_line(loc);

	if (label_len > 0) {
		c_put_str_len(attr, data->label.str, loc, label_len);
		loc.x += label_len;
	}

	c_put_str_len(attr, data->equip.str, loc, equip_len);
	loc.x += equip_len;

	/* Truncate the name if it's too long */
	if (label_len + equip_len + name_len > extra_fields_offset) {
		if (label_len + equip_len < extra_fields_offset) {
			name_len = extra_fields_offset - label_len - equip_len;
		} else {
			name_len = 0;
		}
	}

	if (name_len > 0) {
		attr = data->object ?
			(uint32_t) data->object->kind->base->attr : menu_row_style(false, false);
		c_put_str_len(attr, data->name.str, loc, name_len);
	}

	/* If we don't have an object, we can skip the extra fields */
	if (data->object) {
		saved_loc.x += extra_fields_offset;
		show_obj_extra(data->object, saved_loc, mode);
	}
}

/**
 * ------------------------------------------------------------------------
 * Display of lists of objects
 * ------------------------------------------------------------------------ */
/**
 * Returns a new object list. Non-reentrant (call it optimization, or legacy)
 */
static struct object_menu_list *get_obj_list(void)
{
	static struct object_menu_list olist;

	memset(&olist, 0, sizeof(olist));

	olist.size = N_ELEMENTS(olist.items);

	for (size_t i = 0; i < olist.size; i++) {
		olist.items[i].label.size = sizeof(olist.items[i].label.str);
		olist.items[i].equip.size = sizeof(olist.items[i].equip.str);
		olist.items[i].name.size  = sizeof(olist.items[i].name.str);
		/* Pedantry */
		olist.items[i].object = NULL;
	}

	return &olist;
}

static void free_obj_list(struct object_menu_list *olist)
{
	(void) olist;

	/* Maybe get_obj_list() will become reentrant at some point? */
}

/**
 * Set object names and get their maximum length.
 * Only makes sense after building the object list.
 */
static void set_obj_names(struct object_menu_list *olist, bool terse)
{
	int oflags = ODESC_PREFIX | ODESC_FULL;
	if (terse) {
		oflags |= ODESC_TERSE;
	}

	/* Calculate name offset and max name length */
	for (size_t i = 0; i < olist->next; i++) {
		struct object_menu_data *data = &olist->items[i];
		struct object *obj = data->object;

		if (obj == NULL) {
			data->name.len =
				my_strcpy(data->name.str, "(nothing)", data->name.size);
		} else {
			data->name.len =
				object_desc(data->name.str, data->name.size, obj, oflags);
		}

		size_t len = data->label.len + data->equip.len + data->name.len;

		olist->max_len = MAX(olist->max_len, len);
	}
}

/**
 * Build the object list.
 */
static void build_obj_list(struct object_menu_list *olist,
		struct object **objects, int last, const char *keys,
		item_tester tester, olist_detail_t mode)
{
	const bool quiver = (mode & OLIST_QUIVER_FULL) ? true : false;
	const bool sempty = (mode & OLIST_SHOW_EMPTY)  ? true : false;
	const bool window = (mode & OLIST_WINDOW)      ? true : false;
	const bool death  = (mode & OLIST_DEATH)       ? true : false;
	const bool terse  = (mode & OLIST_TERSE)       ? true : false;
	const bool gold   = (mode & OLIST_GOLD)        ? true : false;
	const bool equip  = (objects == NULL)          ? true : false;

	/* Build the object list */
	for (int i = 0; i <= last; i++) {
		assert(olist->next < olist->size);

		struct object *obj = equip ? slot_object(player, i) : objects[i];
		struct object_menu_data *data = &olist->items[olist->next];
		char key = 0;

		if (object_test(tester, obj) || (obj && gold && tval_is_money(obj))) {
			/* Acceptable items get a label */
			key = keys[i];
			assert(key != 0);

			data->label.len =
				strnfmt(data->label.str, data->label.size, "%c) ", key);
		} else if (window || (!obj && sempty)) {
			/* Unacceptable items are still sometimes shown */
			key = ' ';
			data->label.len =
				my_strcpy(data->label.str, "   ", data->label.size);
		} else {
			continue;
		}

		/* Show full slot labels for equipment (or quiver in subwindow) */
		if (equip) {
			data->equip.len = 
				strnfmt(data->equip.str, data->equip.size,
						"%-14s: ", equip_mention(player, i));
			my_strcap(data->equip.str);
		} else if (quiver && (window || death)) {
			data->equip.len =
				strnfmt(data->equip.str, data->equip.size,
						"Slot %-9d: ", i);
		}

		data->object = obj;
		data->key = key;

		olist->next++;
	}

	/* Set the names and get the max length */
	set_obj_names(olist, terse);
}

/* Returns coords of the next row (after the ones shown) */
static struct loc show_quiver_compact(int prev, struct loc loc)
{
	char buf[80];

	int stack = z_info->stack_size;
	int slots = (player->upkeep->quiver_cnt + stack - 1) / stack;
	int last_slot = slots - 1;

	uint32_t attr = menu_row_style(false, false);
	const char *fmt = "in Quiver: %d missile%s";

	/* Quiver may take multiple lines */
	for (int slot = 0; slot < slots; slot++, prev++, loc.y++) {
		char key = I2A(prev);

		if (slot == last_slot) {
			/* Last slot has less than the full number of missiles */
			stack = player->upkeep->quiver_cnt - (z_info->stack_size * last_slot);
		}

		erase_line(loc);

		/* Print the (disabled) label */
		strnfmt(buf, sizeof(buf), "%c) ", key);
		c_put_str(attr, buf, loc);
		loc.x += 3;

		/* Print the count */
		strnfmt(buf, sizeof(buf), fmt, stack, stack == 1 ? "" : "s");
		c_put_str(COLOUR_L_UMBER, buf, loc);
		loc.x -= 3;
	}

	return loc;
}

/**
 * Display a list of objects. Each object may be prefixed with a label.
 * Used by show_inven(), show_equip(), and show_floor(). Mode flags are
 * documented in object.h. Returns coordinates of the next row (after
 * the ones that were printed)
 */
static struct loc show_obj_list(struct object_menu_list *olist,
		olist_detail_t mode, struct loc loc)
{
	const int term_width = Term_width();

	if (mode & OLIST_WINDOW) {
		olist->max_len = 40;
		if (term_width < 40) {
			mode &= ~OLIST_WEIGHT;
		}
	}

	/* Take the quiver message into consideration */
	if ((mode & OLIST_QUIVER_COMPACT)
			&& player->upkeep->quiver[0] != NULL)
	{
		olist->max_len = MAX(olist->max_len, 24);
	}

	int extra_fields_width = 0;
	if (mode & OLIST_WEIGHT) extra_fields_width += EXTRA_FIELD_WEIGHT_WIDTH;
	if (mode & OLIST_PRICE)  extra_fields_width += EXTRA_FIELD_PRICE_WIDTH;
	if (mode & OLIST_FAIL)   extra_fields_width += EXTRA_FIELD_FAIL_WIDTH;

	/* Column offset of the first extra field */
	olist->extra_fields_offset =
		MAX(0, MIN((int) olist->max_len, term_width - extra_fields_width));

	for (size_t i = 0; i < olist->next; i++, loc.y++) {
		show_obj(&olist->items[i],
				loc, olist->extra_fields_offset, false, mode);
	}

	if (mode & OLIST_QUIVER_COMPACT) {
		loc = show_quiver_compact(olist->next, loc);
	}

	return loc;
}

/**
 * Display the inventory.  Builds a list of objects and passes them
 * off to show_obj_list() for display.  Mode flags documented in
 * object.h
 */
void show_inven(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();
	struct loc loc = {0};

	if (mode & OLIST_WINDOW) {
		/* Inven windows start with a burden header */
		char buf[80];
		int diff = weight_remaining(player);

		strnfmt(buf, sizeof(buf),
				"Burden %d.%d lb (%d.%d lb %s) ",
				player->upkeep->total_weight / 10,
				player->upkeep->total_weight % 10,
				abs(diff) / 10,
				abs(diff) % 10,
				(diff < 0 ? "overweight" : "remaining"));

		prt(buf, loc);
		loc.y++;
	}

	int last = -1;
	/* Find the last occupied inventory slot */
	for (int i = 0; i < z_info->pack_size; i++) {
		if (player->upkeep->inven[i] != NULL) {
			last = i;
		}
	}

	build_obj_list(olist, player->upkeep->inven, last,
			all_letters, tester, mode);
	show_obj_list(olist, mode, loc);
	free_obj_list(olist);
}

/**
 * Display the quiver.  Builds a list of objects and passes them
 * off to show_obj_list() for display.  Mode flags documented in
 * object.h
 */
void show_quiver(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	/* Find the last occupied quiver slot */
	int last = -1;
	for (int i = 0; i < z_info->quiver_size; i++) {
		if (player->upkeep->quiver[i] != NULL) {
			last = i;
		}
	}

	mode |= OLIST_QUIVER_FULL; 

	build_obj_list(olist, player->upkeep->quiver, last,
			all_digits, tester, mode);
	show_obj_list(olist, mode, loc(0, 0));
	free_obj_list(olist);
}

/**
 * Display the equipment.  Builds a list of objects and passes them
 * off to show_obj_list() for display.  Mode flags documented in
 * object.h
 */
void show_equip(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();
	struct loc loc = {0};

	build_obj_list(olist, NULL, player->body.count - 1,
			all_letters, tester, mode);
	loc = show_obj_list(olist, mode, loc);
	free_obj_list(olist);

	/* Show the quiver in subwindows */
	if (mode & OLIST_WINDOW) {
		prt("In quiver", loc);
		loc.y++;

		olist = get_obj_list();

		int last = -1;
		for (int i = 0; i < z_info->quiver_size; i++) {
			if (player->upkeep->quiver[i] != NULL) {
				last = i;
			}
		}

		build_obj_list(olist, player->upkeep->quiver, last,
				all_digits, tester, mode);
		show_obj_list(olist, mode, loc);
		free_obj_list(olist);
	}
}

/**
 * Display the floor.  Builds a list of objects and passes them
 * off to show_obj_list() for display.  Mode flags documented in
 * object.h
 */
void show_floor(struct object **floor_list, int floor_num,
		int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	if (floor_num > z_info->floor_size) {
		floor_num = z_info->floor_size;
	}

	build_obj_list(olist, floor_list, floor_num - 1,
			all_letters, tester, mode);
	show_obj_list(olist, mode, loc(0, 0));
	free_obj_list(olist);
}

/**
 * ------------------------------------------------------------------------
 * Variables for object selection
 * ------------------------------------------------------------------------
 */

static item_tester tester_m;
static region area = { 20, 1, -1, -2 };
static struct object *selection;
static int i1, i2;
static int e1, e2;
static int q1, q2;
static int f1, f2;
static struct object **floor_list;
static olist_detail_t olist_mode = 0;
static int item_mode;
static cmd_code item_cmd;
static bool newmenu = false;
static bool allow_all = false;

/**
 * ------------------------------------------------------------------------
 * Object selection utilities
 * ------------------------------------------------------------------------
 */

/**
 * Prevent certain choices depending on the inscriptions on the item.
 *
 * The item can be negative to mean "item on floor".
 */
bool get_item_allow(const struct object *obj,
		unsigned char ch, cmd_code cmd, bool harmless)
{
	if (ch < 0x20) {
		ch = UN_KTRL(ch);
	}

	unsigned checks = check_for_inscrip(obj, format("!%c", ch));
	if (!harmless) {
		checks += check_for_inscrip(obj, "!*");
	}

	if (checks > 0) {
		const char *verb = cmd_verb(cmd);
		if (verb == NULL) {
			verb = "do that with";
		}

		char prompt[80];
		strnfmt(prompt, sizeof(prompt), "Really %s", verb);

		while (checks > 0 && verify_object(prompt, (struct object *) obj)) {
			checks--;
		}
	}

	return checks == 0;
}

/**
 * Find the first object in the object list with the given "tag".  The object
 * list needs to be built before this function is called.
 *
 * A "tag" is a char "n" appearing as "@n" anywhere in the
 * inscription of an object.
 *
 * Also, the tag "@xn" will work as well, where "n" is a tag-char,
 * and "x" is the action that tag will work for.
 */
static bool get_tag(struct object **tagged_obj,
		char tag, cmd_code cmd, bool quiver_tags)
{
	int mode = OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

	/* (f)ire is handled differently from all others, due to the quiver */
	if (quiver_tags) {
		size_t i = D2I(tag);
		assert(i < z_info->quiver_size);

		if (player->upkeep->quiver[i]) {
			*tagged_obj = player->upkeep->quiver[i];
			return true;
		}
	}

	unsigned char cmdkey = cmd_lookup_key(cmd, mode);
	if (cmdkey < 0x20) {
		cmdkey = UN_KTRL(cmdkey);
	}

	/* Check every object in the object list */
	for (int i = 0; i < num_obj; i++) {
		struct object *obj = items[i].object;

		if (obj == NULL || obj->note == NULL) {
			continue;
		}

		for (const char *s = strchr(quark_str(obj->note), '@');
				s != NULL;
				s = strchr(s + 1, '@'))
		{
			if (s[1] == tag || (s[1] == cmdkey && s[2] == tag)) {
				*tagged_obj = obj;
				return true;
			}
		}
	}

	return false;
}

/**
 * ------------------------------------------------------------------------
 * Object selection menu
 * ------------------------------------------------------------------------
 */

/**
 * Make the correct header for the selection menu
 */
static void menu_header(char *header, size_t size)
{
	char tmp_buf[80];
	char out_buf[80];

	bool use_inven  = (item_mode & USE_INVEN)  ? true : false;
	bool use_equip  = (item_mode & USE_EQUIP)  ? true : false;
	bool use_quiver = (item_mode & USE_QUIVER) ? true : false;
	bool use_floor  = (f1 <= f2 || allow_all)  ? true : false;

	/* Viewing inventory */
	if (player->upkeep->command_wrk == USE_INVEN) {
		strnfmt(out_buf, sizeof(out_buf), "Inven:");

		if (i1 <= i2) {
			strnfmt(tmp_buf, sizeof(tmp_buf), " %c-%c,", I2A(i1), I2A(i2));
			my_strcat(out_buf, tmp_buf, sizeof(out_buf));
		}
		if (use_equip) {
			my_strcat(out_buf, " / for Equip,", sizeof(out_buf));
		}
		if (use_quiver) {
			my_strcat(out_buf, " | for Quiver,", sizeof(out_buf));
		}
		if (use_floor) {
			my_strcat(out_buf, " - for floor,", sizeof(out_buf));
		}
	} else if (player->upkeep->command_wrk == USE_EQUIP) {
		strnfmt(out_buf, sizeof(out_buf), "Equip:");

		if (e1 <= e2) {
			strnfmt(tmp_buf, sizeof(tmp_buf), " %c-%c,", I2A(e1), I2A(e2));
			my_strcat(out_buf, tmp_buf, sizeof(out_buf));
		}
		if (use_inven) {
			my_strcat(out_buf, " / for Inven,", sizeof(out_buf));
		}
		if (use_quiver) {
			my_strcat(out_buf, " | for Quiver,", sizeof(out_buf));
		}
		if (use_floor) {
			my_strcat(out_buf, " - for floor,", sizeof(out_buf));
		}
	} else if (player->upkeep->command_wrk == USE_QUIVER) {
		strnfmt(out_buf, sizeof(out_buf), "Quiver:");

		if (q1 <= q2) {
			strnfmt(tmp_buf, sizeof(tmp_buf), " %c-%c,", I2A(q1), I2A(q2));
			my_strcat(out_buf, tmp_buf, sizeof(out_buf));
		}
		if (use_inven) {
			my_strcat(out_buf, " / for Inven,", sizeof(out_buf));
		} else if (use_equip) {
			my_strcat(out_buf, " / for Equip,", sizeof(out_buf));
		}
		if (use_floor) {
			my_strcat(out_buf, " - for floor,", sizeof(out_buf));
		}
	} else {
		strnfmt(out_buf, sizeof(out_buf), "Floor:");

		if (f1 <= f2) {
			strnfmt(tmp_buf, sizeof(tmp_buf), " %c-%c,", I2A(f1), I2A(f2));
			my_strcat(out_buf, tmp_buf, sizeof(out_buf));
		}
		if (use_inven) {
			my_strcat(out_buf, " / for Inven,", sizeof(out_buf));
		} else if (use_equip) {
			my_strcat(out_buf, " / for Equip,", sizeof(out_buf));
		}
		if (use_quiver) {
			my_strcat(out_buf, " | for Quiver,", sizeof(out_buf));
		}
	}

	my_strcat(out_buf, " ESC", sizeof(out_buf));

	strnfmt(header, sizeof(header), "(%s)", out_buf);
}

/**
 * Get an item tag
 */
char get_item_tag(struct menu *menu, int index)
{
	struct object_menu_data *choice = menu_priv(menu);

	return choice[index].label;
}

/**
 * Determine if an item is a valid choice
 */
int get_item_validity(struct menu *menu, int index)
{
	struct object_menu_data *choice = menu_priv(menu);

	return (choice[index].object != NULL) ? 1 : 0;
}

/**
 * Display an entry on the item menu
 */
void get_item_display(struct menu *menu,
		int index, bool cursor, int row, int col, int width)
{
	show_obj(index, row - index, col, cursor, olist_mode);
}

/**
 * Deal with events on the get_item menu
 */
bool get_item_action(struct menu *menu, const ui_event *event, int index)
{
	struct object_menu_data *choice = menu_priv(menu);
	char key = event->key.code;
	bool is_harmless = item_mode & IS_HARMLESS ? true : false;
	int mode = OPT(rogue_like_commands) ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

	if (event->type == EVT_SELECT) {
		if (get_item_allow(choice[index].object, cmd_lookup_key(item_cmd, mode),
						   item_cmd, is_harmless))
			selection = choice[index].object;
	}

	if (event->type == EVT_KBRD) {
		if (key == '/') {
			/* Toggle if allowed */
			if (((item_mode & USE_INVEN) || allow_all)
				&& (player->upkeep->command_wrk != USE_INVEN)) {
				player->upkeep->command_wrk = USE_INVEN;
				newmenu = true;
			} else if (((item_mode & USE_EQUIP) || allow_all) &&
					   (player->upkeep->command_wrk != USE_EQUIP)) {
				player->upkeep->command_wrk = USE_EQUIP;
				newmenu = true;
			} else {
				bell("Cannot switch item selector!");
			}
		}

		else if (key == '|') {
			/* No toggle allowed */
			if ((q1 > q2) && !allow_all){
				bell("Cannot select quiver!");
			} else {
				/* Toggle to quiver */
				player->upkeep->command_wrk = (USE_QUIVER);
				newmenu = true;
			}
		}

		else if (key == '-') {
			/* No toggle allowed */
			if ((f1 > f2) && !allow_all) {
				bell("Cannot select floor!");
			} else {
				/* Toggle to floor */
				player->upkeep->command_wrk = (USE_FLOOR);
				newmenu = true;
			}
		}
	}

	return false;
}

/**
 * Show quiver missiles in full inventory
 */
static void item_menu_browser(int index, void *data, const region *local_area)
{
	char tmp_val[80];
	int count, j, i = num_obj;
	int quiver_slots = (player->upkeep->quiver_cnt + z_info->stack_size - 1)
		/ z_info->stack_size;

	/* Set up to output below the menu */
	text_out_wrap = 0;
	text_out_indent = local_area->col - 1;
	text_out_pad = 1;
	prt("", local_area->row + local_area->page_rows, MAX(0, local_area->col - 1));
	Term_gotoxy(local_area->col, local_area->row + local_area->page_rows);

	/* If we're printing pack slots the quiver takes up */
	if (olist_mode & OLIST_QUIVER && player->upkeep->command_wrk == USE_INVEN) {
		/* Quiver may take multiple lines */
		for (j = 0; j < quiver_slots; j++, i++) {
			const char *fmt = "in Quiver: %d missile%s\n";
			char letter = I2A(i);

			/* Number of missiles in this "slot" */
			if (j == quiver_slots - 1)
				count = player->upkeep->quiver_cnt - (z_info->stack_size *
													  (quiver_slots - 1));
			else
				count = z_info->stack_size;

			/* Print the (disabled) label */
			strnfmt(tmp_val, sizeof(tmp_val), "%c) ", letter);
			text_out_c(COLOUR_SLATE, tmp_val, local_area->row + i, local_area->col);

			/* Print the count */
			strnfmt(tmp_val, sizeof(tmp_val), fmt, count,
					count == 1 ? "" : "s");
			text_out_c(COLOUR_L_UMBER, tmp_val, local_area->row + i, local_area->col + 3);
		}
	}

	/* Always print a blank line */
	prt("", local_area->row + i, MAX(0, local_area->col - 1));

	/* Blank out whole tiles */
	while ((tile_height > 1) && ((local_area->row + i) % tile_height != 0)) {
		i++;
		prt("", local_area->row + i, MAX(0, local_area->col - 1));
	}

	text_out_pad = 0;
	text_out_indent = 0;
}

/**
 * Display list items to choose from
 */
struct object *item_menu(cmd_code cmd, int prompt_size, int mode)
{
	menu_iter iter = {
		.get_tag     = get_item_tag,
		.valid_row   = get_item_validity,
		.display_row = get_item_display,
		.row_handler = get_item_action
	};

	struct menu *m = menu_new(MN_SKIN_OBJECT, &iter);

	int row, inscrip;
	struct object *obj = NULL;

	/* Set up the menu */
	menu_setpriv(m, num_obj, items);
	if (player->upkeep->command_wrk == USE_QUIVER)
		m->selections = "0123456789";
	else
		m->selections = lower_case;
	m->switch_keys = "/|-";
	m->flags = (MN_PVT_TAGS | MN_INSCRIP_TAGS);
	m->browse_hook = item_menu_browser;

	/* Get inscriptions */
	m->inscriptions = mem_zalloc(10 * sizeof(char));
	for (inscrip = 0; inscrip < 10; inscrip++) {
		/* Look up the tag */
		if (get_tag(&obj, (char) I2D(inscrip), item_cmd, item_mode & QUIVER_TAGS)) {
			int i;
			for (i = 0; i < num_obj; i++) {
				if (items[i].object == obj) {
						break;
				}
			}

			if (i < num_obj) {
				m->inscriptions[inscrip] = get_item_tag(m, i);
			}
		}
	}

	/* Set up the item list variables */
	selection = NULL;
	set_obj_names(false);

	if (mode & OLIST_QUIVER && player->upkeep->quiver[0] != NULL)
		max_len = MAX(max_len, 24);

	if (olist_mode & OLIST_WEIGHT) {
		ex_width += 9;
		ex_offset_ctr += 9;
	}
	if (olist_mode & OLIST_PRICE) {
		ex_width += 9;
		ex_offset_ctr += 9;
	}
	if (olist_mode & OLIST_FAIL) {
		ex_width += 10;
		ex_offset_ctr += 10;
	}

	/* Set up the menu region */
	area.page_rows = m->count;
	area.row = 1;
	area.col = MIN(Term->wid - 1 - (int) max_len - ex_width, prompt_size - 2);
	if (area.col <= 3)
		area.col = 0;
	ex_offset = MIN(max_len, (size_t)(Term->wid - 1 - ex_width - area.col));
	while (strlen(header) < max_len + ex_width + ex_offset_ctr) {
		my_strcat(header, " ", sizeof(header));
		if (strlen(header) > sizeof(header) - 2) break;
	}
	area.width = MAX(max_len, strlen(header));

	for (row = area.row; row < area.row + area.page_rows; row++)
		prt("", row, MAX(0, area.col - 1));

	menu_layout(m, &area);

	/* Choose */
	ui event event = menu_select(m, 0, true);

	/* Clean up */
	mem_free(m->inscriptions);
	mem_free(m);

	/* Deal with menu switch */
	if (event.type == EVT_SWITCH && !newmenu) {
		bool left = event.key.code == ARROW_LEFT;

		if (player->upkeep->command_wrk == USE_EQUIP) {
			if (left) {
				if (f1 <= f2)      player->upkeep->command_wrk = USE_FLOOR;
				else if (q1 <= q2) player->upkeep->command_wrk = USE_QUIVER;
				else if (i1 <= i2) player->upkeep->command_wrk = USE_INVEN;
			} else {
				if (i1 <= i2)      player->upkeep->command_wrk = USE_INVEN;
				else if (q1 <= q2) player->upkeep->command_wrk = USE_QUIVER;
				else if (f1 <= f2) player->upkeep->command_wrk = USE_FLOOR;
			}
		} else if (player->upkeep->command_wrk == USE_INVEN) {
			if (left) {
				if (e1 <= e2)      player->upkeep->command_wrk = USE_EQUIP;
				else if (f1 <= f2) player->upkeep->command_wrk = USE_FLOOR;
				else if (q1 <= q2) player->upkeep->command_wrk = USE_QUIVER;
			} else {
				if (q1 <= q2)      player->upkeep->command_wrk = USE_QUIVER;
				else if (f1 <= f2) player->upkeep->command_wrk = USE_FLOOR;
				else if (e1 <= e2) player->upkeep->command_wrk = USE_EQUIP;
			}
		} else if (player->upkeep->command_wrk == USE_QUIVER) {
			if (left) {
				if (i1 <= i2)      player->upkeep->command_wrk = USE_INVEN;
				else if (e1 <= e2) player->upkeep->command_wrk = USE_EQUIP;
				else if (f1 <= f2) player->upkeep->command_wrk = USE_FLOOR;
			} else {
				if (f1 <= f2) player->upkeep->command_wrk = USE_FLOOR;
				else if (e1 <= e2) player->upkeep->command_wrk = USE_EQUIP;
				else if (i1 <= i2) player->upkeep->command_wrk = USE_INVEN;
			}
		} else if (player->upkeep->command_wrk == USE_FLOOR) {
			if (left) {
				if (q1 <= q2)      player->upkeep->command_wrk = USE_QUIVER;
				else if (i1 <= i2) player->upkeep->command_wrk = USE_INVEN;
				else if (e1 <= e2) player->upkeep->command_wrk = USE_EQUIP;
			} else {
				if (e1 <= e2)      player->upkeep->command_wrk = USE_EQUIP;
				else if (i1 <= i2) player->upkeep->command_wrk = USE_INVEN;
				else if (q1 <= q2) player->upkeep->command_wrk = USE_QUIVER;
			}
		}

		newmenu = true;
	}

	return selection;
}

/**
 * Let the user select an object, save its address
 *
 * Return true only if an acceptable item was chosen by the user.
 *
 * The user is allowed to choose acceptable items from the equipment,
 * inventory, quiver, or floor, respectively, if the proper flag was given,
 * and there are any acceptable items in that location.
 *
 * The equipment, inventory or quiver are displayed (even if no acceptable
 * items are in that location) if the proper flag was given.
 *
 * If there are no acceptable items available anywhere, and "str" is
 * not NULL, then it will be used as the text of a warning message
 * before the function returns.
 *
 * If a legal item is selected , we save it in "choice" and return true.
 *
 * If no item is available, we do nothing to "choice", and we display a
 * warning message, using "reject" if available, and return false.
 *
 * If no item is selected, we do nothing to "choice", and return false.
 *
 * Global "player->upkeep->command_wrk" is used to choose between
 * equip/inven/quiver/floor listings.  It is equal to USE_INVEN or USE_EQUIP or
 * USE_QUIVER or USE_FLOOR, except when this function is first called, when it
 * is equal to zero, which will cause it to be set to USE_INVEN.
 *
 * We always erase the prompt when we are done, leaving a blank line,
 * or a warning message, if appropriate, if no items are available.
 *
 * Note that only "acceptable" floor objects get indexes, so between two
 * commands, the indexes of floor objects may change.  XXX XXX XXX
 */
bool textui_get_item(struct object **choice,
		const char *prompt, const char *reject,
		cmd_code cmd, item_tester tester, int mode)
{
	bool use_inven   = (mode & USE_INVEN)   ? true : false;
	bool use_equip   = (mode & USE_EQUIP)   ? true : false;
	bool use_quiver  = (mode & USE_QUIVER)  ? true : false;
	bool use_floor   = (mode & USE_FLOOR)   ? true : false;
	bool quiver_tags = (mode & QUIVER_TAGS) ? true : false;

	bool allow_inven  = false;
	bool allow_equip  = false;
	bool allow_quiver = false;
	bool allow_floor  = false;

	bool toggle = false;

	int floor_max = z_info->floor_size;

	floor_list = mem_zalloc(floor_max * sizeof(*floor_list));
	olist_mode = 0;
	item_mode  = mode;
	item_cmd   = cmd;
	tester_m   = tester;
	allow_all  = reject ? false : true;

	/* Object list display modes */
	if (mode & SHOW_FAIL) {
		olist_mode |= OLIST_FAIL;
	} else {
		olist_mode |= OLIST_WEIGHT;
	}
	if (mode & SHOW_PRICES) {
		olist_mode |= OLIST_PRICE;
	}
	if (mode & SHOW_EMPTY) {
		olist_mode |= OLIST_SEMPTY;
	}
	if (mode & SHOW_QUIVER) {
		olist_mode |= OLIST_QUIVER;
	}

	/* Full inventory */
	i1 = 0;
	i2 = z_info->pack_size - 1;

	/* Forbid inventory */
	if (!use_inven) {
		i2 = -1;
	}

	/* Restrict inventory indexes */
	while (i1 <= i2 && !object_test(tester, player->upkeep->inven[i1])) {
		i1++;
	}
	while (i1 <= i2 && !object_test(tester, player->upkeep->inven[i2])) {
		i2--;
	}

	/* Accept inventory */
	if (i1 <= i2 || allow_all) {
		allow_inven = true;
	} else if (item_mode & USE_INVEN) {
		item_mode -= USE_INVEN;
	}

	/* Full equipment */
	e1 = 0;
	e2 = player->body.count - 1;

	/* Forbid equipment */
	if (!use_equip) {
		e2 = -1;
	}

	/* Restrict equipment indexes unless starting with no command */
	if (cmd != CMD_NULL || tester != NULL) {
		while (e1 <= e2 && !object_test(tester, slot_object(player, e1))) {
			e1++;
		} while (e1 <= e2 && !object_test(tester, slot_object(player, e2))) {
			e2--;
		}
	}

	/* Accept equipment */
	if (e1 <= e2 || allow_all) {
		allow_equip = true;
	} else if (item_mode & USE_EQUIP) {
		item_mode -= USE_EQUIP;
	}

	/* Restrict quiver indexes */
	q1 = 0;
	q2 = z_info->quiver_size - 1;

	/* Forbid quiver */
	if (!use_quiver) {
		q2 = -1;
	}

	/* Restrict quiver indexes */
	while (q1 <= q2 && !object_test(tester, player->upkeep->quiver[q1])) {
		q1++;
	} while (q1 <= q2 && !object_test(tester, player->upkeep->quiver[q2])) {
		q2--;
	}

	/* Accept quiver */
	if (q1 <= q2 || allow_all) {
		allow_quiver = true;
	} else if (item_mode & USE_QUIVER) {
		item_mode -= USE_QUIVER;
	}

	/* Scan all non-gold objects in the grid */
	int floor_num = scan_floor(floor_list, floor_max,
						   OFLOOR_TEST | OFLOOR_SENSE | OFLOOR_VISIBLE, tester);

	/* Full floor */
	f1 = 0;
	f2 = floor_num - 1;

	/* Forbid floor */
	if (!use_floor) {
		f2 = -1;
	}

	/* Restrict floor indexes */
	while (f1 <= f2 && !object_test(tester, floor_list[f1])) {
		f1++;
	}
	while (f1 <= f2 && !object_test(tester, floor_list[f2])) {
		f2--;
	}

	/* Accept floor */
	if (f1 <= f2 || allow_all) {
		allow_floor = true;
	} else if (item_mode & USE_FLOOR) {
		item_mode -= USE_FLOOR;
	}

	/* Require at least one legal choice */
	if (allow_inven || allow_equip || allow_quiver || allow_floor) {
		/* Start where requested if possible */
		if ((player->upkeep->command_wrk == USE_EQUIP) && allow_equip) {
			player->upkeep->command_wrk = USE_EQUIP;
		} else if ((player->upkeep->command_wrk == USE_INVEN) && allow_inven) {
			player->upkeep->command_wrk = USE_INVEN;
		} else if ((player->upkeep->command_wrk == USE_QUIVER) && allow_quiver) {
			player->upkeep->command_wrk = USE_QUIVER;
		} else if ((player->upkeep->command_wrk == USE_FLOOR) && allow_floor) {
			player->upkeep->command_wrk = USE_FLOOR;
		} else if (quiver_tags && allow_quiver) {
			/* If we are obviously using the quiver then start on quiver */
			player->upkeep->command_wrk = USE_QUIVER;
		} else if (use_inven && allow_inven) {
			/* Otherwise choose whatever is allowed */
			player->upkeep->command_wrk = USE_INVEN;
		} else if (use_equip && allow_equip) {
			player->upkeep->command_wrk = USE_EQUIP;
		} else if (use_quiver && allow_quiver) {
			player->upkeep->command_wrk = USE_QUIVER;
		} else if (use_floor && allow_floor) {
			player->upkeep->command_wrk = USE_FLOOR;
		} else {
			/* If nothing to choose, use (empty) inventory */
			player->upkeep->command_wrk = USE_INVEN;
		}

		while (true) {
			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
			redraw_stuff(player);

			screen_save();

			wipe_obj_list();

			if (player->upkeep->command_wrk == USE_INVEN) {
				build_obj_list(i2, player->upkeep->inven, tester_m, olist_mode);
			} else if (player->upkeep->command_wrk == USE_EQUIP) {
				build_obj_list(e2, NULL, tester_m, olist_mode);
			} else if (player->upkeep->command_wrk == USE_QUIVER) {
				build_obj_list(q2, player->upkeep->quiver, tester_m,olist_mode);
			} else if (player->upkeep->command_wrk == USE_FLOOR) {
				build_obj_list(f2, floor_list, tester_m, olist_mode);
			}

			if (prompt) {
				char header[80];
				size_t len = my_strcpy(header, prompt);
				menu_header(header, sizeof(header) - len);
				show_prompt(header, 0, strlen(pmt) + 1);
			}

			newmenu = false;

			*choice = item_menu(cmd, MAX(strlen(pmt), 15), mode);

			screen_load();

			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
			redraw_stuff(player);

			clear_prompt();

			/* We have a selection, or are backing out */
			if (*choice || !newmenu) {
				break;
			}
		}
	} else {
		*choice = NULL;

		if (reject) {
			msg("%s", reject);
		}
	}

	player->upkeep->command_wrk = 0;
	mem_free(floor_list);

	return *choice != NULL;
}

/**
 * ------------------------------------------------------------------------
 * Object recall
 * ------------------------------------------------------------------------
 */

/**
 * This draws the Object Recall subwindow when displaying a particular object
 * (e.g. a helmet in the backpack, or a scroll on the ground)
 */
void display_object_recall(struct object *obj)
{
	char header[80];
	region area = {0};

	textblock *tb = object_info(obj, OINFO_NONE);
	object_desc(header, sizeof(header), obj, ODESC_PREFIX | ODESC_FULL);

	Term_clear();
	textui_textblock_place(tb, area, header);
	textblock_free(tb);
}

/**
 * This draws the Object Recall subwindow when displaying a recalled item kind
 * (e.g. a generic ring of acid or a generic blade of chaos)
 */
void display_object_kind_recall(struct object_kind *kind)
{
	struct object object = OBJECT_NULL, known_obj = OBJECT_NULL;
	object_prep(&object, kind, 0, EXTREMIFY);
	object.known = &known_obj;

	display_object_recall(&object);
}

/**
 * Display object recall modally and wait for a keypress.
 *
 * This is set up for use in look mode (see target_set_interactive_aux()).
 *
 * \param obj is the object to be described.
 */
void display_object_recall_interactive(struct object *obj)
{
	char header[80];
	region area = {0};

	textblock *tb = object_info(obj, OINFO_NONE);
	object_desc(header, sizeof(header), obj, ODESC_PREFIX | ODESC_FULL);
	textui_textblock_show(tb, area, header);
	textblock_free(tb);
}

/**
 * Examine an object
 */
void textui_obj_examine(void)
{
	struct object *obj;

	if (get_item(&obj, "Examine which item? ", "You have nothing to examine.",
			CMD_NULL, NULL, (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | IS_HARMLESS)))
	{
		char header[80];
		region area = {0};

		/* Track object for object recall */
		track_object(player->upkeep, obj);

		textblock *tb = object_info(obj, OINFO_NONE);
		object_desc(header, sizeof(header), obj,
				ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL);

		textui_textblock_show(tb, area, header);
		textblock_free(tb);
	}
}

/**
 * ------------------------------------------------------------------------
 * Object ignore interface
 * ------------------------------------------------------------------------
 */

enum {
	IGNORE_THIS_ITEM,
	UNIGNORE_THIS_ITEM,
	IGNORE_THIS_FLAVOR,
	UNIGNORE_THIS_FLAVOR,
	IGNORE_THIS_EGO,
	UNIGNORE_THIS_EGO,
	IGNORE_THIS_QUALITY
};

void textui_cmd_ignore_menu(struct object *obj)
{
	if (!obj) {
		return;
	}

	char out_val[160];

	struct menu *m;
	region r;
	int selected;
	uint32_t value;
	int type;

	m = menu_dynamic_new();
	m->selections = lower_case;

	/* Basic ignore option */
	if (!(obj->known->notice & OBJ_NOTICE_IGNORE)) {
		menu_dynamic_add(m, "This item only", IGNORE_THIS_ITEM);
	} else {
		menu_dynamic_add(m, "Unignore this item", UNIGNORE_THIS_ITEM);
	}

	/* Flavour-aware ignore */
	if (ignore_tval(obj->tval) &&
			(!obj->artifact || !object_flavor_is_aware(obj))) {
		bool ignored = kind_is_ignored_aware(obj->kind) ||
				kind_is_ignored_unaware(obj->kind);

		char tmp[70];
		object_desc(tmp, sizeof(tmp), obj,
					ODESC_NOEGO | ODESC_BASE | ODESC_PLURAL);
		if (!ignored) {
			strnfmt(out_val, sizeof out_val, "All %s", tmp);
			menu_dynamic_add(m, out_val, IGNORE_THIS_FLAVOR);
		} else {
			strnfmt(out_val, sizeof out_val, "Unignore all %s", tmp);
			menu_dynamic_add(m, out_val, UNIGNORE_THIS_FLAVOR);
		}
	}

	/* Ego ignoring */
	if (obj->known->ego) {
		struct ego_desc choice;
		struct ego_item *ego = obj->ego;
		char tmp[80] = "";

		choice.e_idx = ego->eidx;
		choice.itype = ignore_type_of(obj);
		choice.short_name = "";
		(void) ego_item_name(tmp, sizeof(tmp), &choice);
		if (!ego_is_ignored(choice.e_idx, choice.itype)) {
			strnfmt(out_val, sizeof out_val, "All %s", tmp + 4);
			menu_dynamic_add(m, out_val, IGNORE_THIS_EGO);
		} else {
			strnfmt(out_val, sizeof out_val, "Unignore all %s", tmp + 4);
			menu_dynamic_add(m, out_val, UNIGNORE_THIS_EGO);
		}
	}

	/* Quality ignoring */
	value = ignore_level_of(obj);
	type = ignore_type_of(obj);

	if (tval_is_jewelry(obj) &&	ignore_level_of(obj) != IGNORE_BAD)
		value = IGNORE_MAX;

	if (value != IGNORE_MAX && type != ITYPE_MAX) {
		strnfmt(out_val, sizeof out_val, "All %s %s",
				quality_values[value].name, ignore_name_for_type(type));

		menu_dynamic_add(m, out_val, IGNORE_THIS_QUALITY);
	}

	/* Work out display region */
	r.width = menu_dynamic_longest_entry(m) + 3 + 2; /* +3 for tag, 2 for pad */
	r.col = 80 - r.width;
	r.row = 1;
	r.page_rows = m->count;

	screen_save();
	menu_layout(m, &r);
	region_erase_bordered(&r);

	prt("(Enter to select, ESC) Ignore:", 0, 0);
	selected = menu_dynamic_select(m);

	screen_load();

	if (selected == IGNORE_THIS_ITEM) {
		obj->known->notice |= OBJ_NOTICE_IGNORE;
	} else if (selected == UNIGNORE_THIS_ITEM) {
		obj->known->notice &= ~(OBJ_NOTICE_IGNORE);
	} else if (selected == IGNORE_THIS_FLAVOR) {
		object_ignore_flavor_of(obj);
	} else if (selected == UNIGNORE_THIS_FLAVOR) {
		kind_ignore_clear(obj->kind);
	} else if (selected == IGNORE_THIS_EGO) {
		ego_ignore(obj);
	} else if (selected == UNIGNORE_THIS_EGO) {
		ego_ignore_clear(obj);
	} else if (selected == IGNORE_THIS_QUALITY) {
		byte ignore_value = ignore_level_of(obj);
		int ignore_type = ignore_type_of(obj);

		ignore_level[ignore_type] = ignore_value;
	}

	player->upkeep->notice |= PN_IGNORE;

	menu_dynamic_free(m);
}

void textui_cmd_ignore(void)
{
	struct object *obj;

	if (get_item(&obj, "Ignore which item? ", "You have nothing to ignore.",
				CMD_IGNORE, NULL,
				USE_INVEN | USE_QUIVER | USE_EQUIP | USE_FLOOR))
	{
		textui_cmd_ignore_menu(obj);
	}
}

void textui_cmd_toggle_ignore(void)
{
	player->unignoring = !player->unignoring;
	player->upkeep->notice |= PN_IGNORE;
	do_cmd_redraw();
}
