/**
 * \file ui2-wiz-debug.c
 * \brief Debug mode commands
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

#include "angband.h"
#include "cave.h"
#include "cmds.h"
#include "effects.h"
#include "game-input.h"
#include "grafmode.h"
#include "init.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "target.h"
#include "trap.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-event.h"
#include "ui2-help.h"
#include "ui2-input.h"
#include "ui2-map.h"
#include "ui2-menu.h"
#include "ui2-prefs.h"
#include "ui2-target.h"
#include "ui2-term.h"
#include "ui2-wizard.h"

/**
 * This is a nice utility function; it determines if a (NULL-terminated)
 * string consists of only digits (starting with a non-zero digit).
 */
static int get_idx_from_name(char *s)
{
	char *endptr = NULL;
	long result = strtol(s, &endptr, 10);
	return *endptr == '\0' ? result : 0;
}

/**
 * Display in sequence the squares at n grids from the player,
 * as measured by the flow algorithm; n goes from 1 to max flow depth
 */
static void do_cmd_wiz_show_flow(void)
{
	int px = player->px;
	int py = player->py;

	region reg;
	get_cave_region(&reg);

	for (int i = 0; i < z_info->max_flow_depth; i++) {
		for (int y = reg.y, endy = reg.y + reg.h; y < endy; y++) {
			for (int x = reg.x, endx = reg.x + reg.w; x < endx; x++) {
				if (square_in_bounds_fully(cave, y, x)
						&& cave->squares[y][x].cost == i)
				{
					uint32_t attr =
						cave->squares[y][x].when == cave->squares[py][px].when ?
						COLOUR_YELLOW : COLOUR_RED;

					if (x == player->px && y == player->py) {
						print_rel(DISPLAY_CAVE, attr, L'@', loc(x, y));
					} else if (square_ispassable(cave, y, x)) {
						print_rel(DISPLAY_CAVE, attr, L'*', loc(x, y));
					} else {
						print_rel(DISPLAY_CAVE, attr, L'#', loc(x, y));
					}
				}
			}
		}

		char sym;
		if (!get_com(format("Depth %d: ", i), &sym)) {
			break;
		}

		print_map(DISPLAY_CAVE);
	}

	clear_prompt();
	print_map(DISPLAY_CAVE);
}

/**
 * Output part of a bitflag set in binary format.
 */
static void prt_binary(const bitflag *flags,
		int offset, int n_flags, wchar_t ch, struct loc loc)
{
	for (int flag = FLAG_START + offset;
			flag < FLAG_START + offset + n_flags;
			flag++, loc.x++)
	{
		if (of_has(flags, flag)) {
			Term_addwc(loc.x, loc.y, COLOUR_BLUE, ch);
		} else {
			Term_addwc(loc.x, loc.y, COLOUR_WHITE, L'-');
		}
	}
}

/**
 * Teleport to the requested target
 */
static void do_cmd_wiz_teleport_target(void)
{
	if (!target_set_interactive(TARGET_LOOK, loc(-1, -1))) {
		return;
	}

	int x = 0;
	int y = 0;
	target_get(&x, &y);

	if (!square_ispassable(cave, y, x)) {
		msg("The square you are aiming for is impassable.");
	} else {
		effect_simple(EF_TELEPORT_TO, "0", y, x, 0, NULL);
	}
}

/*
 * Random teleport
 */
static void do_cmd_wiz_teleport(void)
{
	effect_simple(EF_TELEPORT, "100", 0, 1, 0, NULL);
}

/**
 * Aux function for "do_cmd_wiz_change()"
 */
static void do_cmd_wiz_change_aux(void)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	/* Get stats */
	for (int s = 0; s < STAT_MAX; s++) {
		char prompt[ANGBAND_TERM_STANDARD_WIDTH];

		strnfmt(prompt, sizeof(prompt), "%s (3-118): ", stat_names[s]);
		strnfmt(buf, sizeof(buf), "%d", player->stat_max[s]);

		if (!get_string(prompt, buf, 4)) {
			return;
		}

		int stat = atoi(buf);
		stat = MAX(3, MIN(stat, 18 + 100));

		player->stat_cur[s] = player->stat_max[s] = stat;
	}

	/* Get gold */
	{
		strnfmt(buf, sizeof(buf), "%ld", (long) player->au);

		if (!get_string("Gold: ", buf, 10)) {
			return;
		}

		long gold = atol(buf);
		gold = MAX(0, gold);

		player->au = gold;
	}

	/* Get experience */
	{
		strnfmt(buf, sizeof(buf), "%ld", (long) player->exp);

		if (!get_string("Experience: ", buf, 10)) {
			return;
		}

		long exp = atol(buf);
		exp = MAX(0, exp);

		if (exp > player->exp) {
			player_exp_gain(player, exp - player->exp);
		} else {
			player_exp_lose(player, player->exp - exp, false);
		}
	}
}

/**
 * Change player stats, gold and experience.
 */
static void do_cmd_wiz_change(void)
{
	do_cmd_wiz_change_aux();
	do_cmd_redraw();
}

/**
 * Wizard routines for creating objects and modifying them
 *
 * This has been rewritten to make the whole procedure
 * of debugging objects much easier and more comfortable.
 *
 * Here are the low-level functions
 *
 * - wiz_display_item()
 *     display an item's debug-info
 * - wiz_create_itemtype()
 *     specify tval and sval (type and subtype of object)
 * - wiz_tweak_item()
 *     specify pval, +AC, +tohit, +todam
 *     Note that the wizard can leave this function anytime,
 *     thus accepting the default-values for the remaining values.
 *     pval comes first now, since it is most important.
 * - wiz_reroll_item()
 *     apply some magic to the item or turn it into an artifact.
 * - wiz_roll_item()
 *     Get some statistics about the rarity of an item:
 *     We create a lot of fake items and see if they are of the
 *     same type (tval and sval), then we compare pval and +AC.
 *     If the fake-item is better or equal it is counted.
 *     Note that cursed items that are better or equal (absolute values)
 *     are counted, too.
 *     HINT: This is *very* useful for balancing the game!
 * - wiz_quantity_item()
 *     change the quantity of an item, but be sane about it.
 *
 * And now the high-level functions
 * - do_cmd_wiz_play_item()
 *     play with an existing object
 * - wiz_create_item()
 *     create a new object
 *
 * Note - You do not have to specify "pval" and other item-properties
 * directly. Just apply magic until you are satisfied with the item.
 *
 * Note - For some items (such as wands, staffs, some rings, etc), you
 * must apply magic, or you will get "broken" or "uncharged" objects.
 *
 * Note - Redefining artifacts via do_cmd_wiz_play_item() may destroy
 * the artifact. Be careful.
 *
 * This function will allow you to create multiple artifacts.
 * This "feature" may induce crashes or other nasty effects.
 */

/**
 * Display an item's properties
 */
static void wiz_display_item(const struct object *obj, bool all)
{
	bitflag flags[OF_SIZE];
	if (all) {
		object_flags(obj, flags);
	} else {
		object_flags_known(obj, flags);
	}

	Term_clear();

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(buf, sizeof(buf), obj,
				ODESC_PREFIX | ODESC_FULL | ODESC_SPOIL);

	struct loc loc = {0, 2};
	prt(buf, loc);

	loc.y = 4;
	prt(format("combat = (%dd%d) (%+d,%+d) [%d,%+d]",
	           obj->dd, obj->ds, obj->to_h, obj->to_d, obj->ac, obj->to_a),
			loc);

	loc.y = 5;
	prt(format("kind = %-5d  tval = %-5d  sval = %-5d  wgt = %-3d     timeout = %-d",
	           obj->kind->kidx, obj->tval, obj->sval, obj->weight, obj->timeout),
			loc);

	loc.y = 6;
	prt(format("number = %-3d  pval = %-5d  name1 = %-4d  egoidx = %-4d  cost = %d",
			   obj->number, obj->pval,
			   obj->artifact ? obj->artifact->aidx : 0,
			   obj->ego ? obj->ego->eidx : 0,
			   object_value(obj, 1, false)),
			loc);

	loc.y = 16;

	prt("+------------FLAGS-------------+", loc);
	loc.y++;

	prt("SUST.PROT<-OTHER--><BAD->CUR....", loc);
	loc.y++;

	prt("     fbcssf  s  ibniiatadlhp....", loc);
	loc.y++;

	prt("siwdcelotdfrei  plommfegrccc....", loc);
	loc.y++;

	prt("tnieoannuiaesnfhcefhsrlgxuuu....", loc);
	loc.y++;

	prt("rtsxnrdfnglgpvaltsuppderprrr....", loc);
	loc.y++;

	prt_binary(flags, 0, 28, L'*', loc);
	loc.y++;

	prt_binary(obj->known->flags, 0, 28, L'+', loc);
	loc.y++;
}

/** Object creation code **/
static bool choose_artifact = false;

/**
 * Build an "artifact name" and transfer it into a buffer.
 */
static void get_art_name(char *buf, int max, int a_idx)
{
	struct object *obj, *known_obj;
	struct object_kind *kind;
	struct artifact *art = &a_info[a_idx];

	/* Get object */
	obj = object_new();

	/* Acquire the "kind" index */
	kind = lookup_kind(art->tval, art->sval);

	/* Oops */
	if (!kind)
		return;

	/* Create the base object */
	object_prep(obj, kind, 0, RANDOMISE);

	/* Mark it as an artifact */
	obj->artifact = art;

	/* Make it known to us */
	known_obj = object_new();
	obj->known = known_obj;
	object_copy(known_obj, obj);
	known_obj->notice |= OBJ_NOTICE_IMAGINED;

	/* Create the artifact description */
	object_desc(buf, max, obj, ODESC_SINGULAR | ODESC_SPOIL);

	object_delete(&known_obj);
	obj->known = NULL;
	object_delete(&obj);
}

#define WIZ_CREATE_ALL_MENU_ITEM -9999

/**
 * Create an instance of an object of a given kind.
 *
 * \param kind The base type of object to instantiate.
 * \return An object of the provided object type.
 */
static struct object *wiz_create_item_object_from_kind(struct object_kind *kind)
{
	struct object *obj;

	/* Create the item */
	if (tval_is_money_k(kind))
		obj = make_gold(player->depth, kind->name);
	else {
		/* Get object */
		obj = object_new();
		object_prep(obj, kind, player->depth, RANDOMISE);

		/* Apply magic (no messages, no artifacts) */
		apply_magic(obj, player->depth, false, false, false, false);
		apply_curse_knowledge(obj);
	}

	return obj;
}

/**
 * Create an instance of an artifact.
 *
 * \param art The artifact to instantiate.
 * \return An object that represents the artifact.
 */
static struct object *wiz_create_item_object_from_artifact(struct artifact *art)
{
	struct object_kind *kind;
	struct object *obj;

	/* Ignore "empty" artifacts */
	if (!art->name) return NULL;

	/* Acquire the "kind" index */
	kind = lookup_kind(art->tval, art->sval);
	if (!kind)
		return NULL;

	/* Get object */
	obj = object_new();

	/* Create the artifact */
	object_prep(obj, kind, art->alloc_min, RANDOMISE);
	obj->artifact = art;
	copy_artifact_data(obj, art);
	apply_curse_knowledge(obj);

	/* Mark that the artifact has been created. */
	art->created = true;

	return obj;
}

/**
 * Drop an object near the player in a manner suitable for debugging.
 *
 * \param obj The object to drop.
 */
static void wiz_create_item_drop_object(struct object *obj)
{
	if (obj == NULL)
		return;

	/* Mark as cheat, and where created */
	obj->origin = ORIGIN_CHEAT;
	obj->origin_depth = player->depth;

	/* Drop the object from heaven */
	drop_near(cave, &obj, 0, player->py, player->px, true);
}

/**
 * Drop all possible artifacts or objects by the player.
 *
 * \param create_artifacts When true, all artifacts will be created; when false,
 *		  all regular objects will be created.
 */
static void wiz_create_item_all_items(bool create_artifacts)
{
	int i;
	struct object *obj;
	struct object_kind *kind;
	struct artifact *art;

	if (create_artifacts) {
		for (i = 1; i < z_info->a_max; i++) {
			art = &a_info[i];
			obj = wiz_create_item_object_from_artifact(art);
			wiz_create_item_drop_object(obj);
		}
	}
	else {
		for (i = 1; i < z_info->k_max; i++) {
			kind = &k_info[i];

			if (kind->base == NULL || kind->base->name == NULL)
				continue;

			if (kf_has(kind->kind_flags, KF_INSTA_ART))
				continue;

			obj = wiz_create_item_object_from_kind(kind);
			wiz_create_item_drop_object(obj);
		}
	}
}

/**
 * Artifact or object kind selection
 */
static void wiz_create_item_subdisplay(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	int *choices = menu_priv(menu);
	int selected = choices[index];

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (selected == WIZ_CREATE_ALL_MENU_ITEM) {
		/* The special flag should be the last menu item, with
		 * the selected tval stored in the next element. */
		int current_tval = choices[index + 1];
		char name[ANGBAND_TERM_STANDARD_WIDTH];

		object_base_name(name, sizeof(name), current_tval, true);
		if (choose_artifact) {
			strnfmt(buf, sizeof(buf), "All artifact %s", name);
		} else {
			strnfmt(buf, sizeof(buf), "All %s", name);
		}
	} else {
		if (choose_artifact) {
			get_art_name(buf, sizeof(buf), selected);
		} else {
			object_kind_name(buf, sizeof(buf), &k_info[selected], true);
		}
	}

	c_prt(menu_row_style(true, cursor), buf, loc);
}

static bool wiz_create_item_subaction(struct menu *menu, const ui_event *event, int index)
{
	if (event->type != EVT_SELECT) {
		return true;
	}

	int *choices = menu_priv(menu);
	int selected = choices[index];

	if (selected == WIZ_CREATE_ALL_MENU_ITEM && !choose_artifact) {
		for (int cur = 0; cur < index; cur++) {
			struct object_kind *kind = &k_info[choices[cur]];
			struct object *obj = wiz_create_item_object_from_kind(kind);
			wiz_create_item_drop_object(obj);
		}
	} else if (selected == WIZ_CREATE_ALL_MENU_ITEM && choose_artifact) {
		for (int cur = 0; cur < index; cur++) {
			struct artifact *art = &a_info[choices[cur]];
			struct object *obj = wiz_create_item_object_from_artifact(art);
			wiz_create_item_drop_object(obj);
		}
	} else if (selected != WIZ_CREATE_ALL_MENU_ITEM && !choose_artifact) {
		struct object_kind *kind = &k_info[choices[index]];
		struct object *obj = wiz_create_item_object_from_kind(kind);
		wiz_create_item_drop_object(obj);
	} else if (selected != WIZ_CREATE_ALL_MENU_ITEM && choose_artifact) {
		struct artifact *art = &a_info[choices[index]];
		struct object *obj = wiz_create_item_object_from_artifact(art);
		wiz_create_item_drop_object(obj);
	}

	return false;
}

static menu_iter wiz_create_item_submenu = {
	.display_row = wiz_create_item_subdisplay,
	.row_handler = wiz_create_item_subaction
};

/**
 * Object base kind selection
 */

static void wiz_create_item_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) menu;
	(void) width;

	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	if (index == WIZ_CREATE_ALL_MENU_ITEM) {
		if (choose_artifact) {
			my_strcpy(buf, "All artifacts", sizeof(buf));
		} else {
			my_strcpy(buf, "All objects", sizeof(buf));
		}
	} else {
		object_base_name(buf, sizeof(buf), index, true);
	}

	c_prt(menu_row_style(true, cursor), buf, loc);
}

static bool wiz_create_item_action(struct menu *menu,
		const ui_event *event, int index)
{
	(void) menu;

	if (event->type != EVT_SELECT) {
		return true;
	}

	if (index == WIZ_CREATE_ALL_MENU_ITEM) {
		wiz_create_item_all_items(choose_artifact);
		return false;
	}

	int choice[60];
	int count = 0;
	/* Artifacts */
	if (choose_artifact) {
		/* We have to search the whole artifact list. */
		for (int a = 1; count < (int) N_ELEMENTS(choice) && a < z_info->a_max; a++) {
			if (a_info[a].tval == index) {
				choice[count++] = a;
			}
		}
	} else {
		/* Regular objects */
		for (int k = 1; count < (int) N_ELEMENTS(choice) && k < z_info->k_max; k++) {
			if (k_info[k].tval == index
					&& !kf_has(k_info[k].kind_flags, KF_INSTA_ART))
			{
				choice[count++] = k;
			}
		}
	}

	/* Add a flag for an "All <tval>" item to create all svals of that tval. The
	 * tval is stored (in a super hacky way) beyond the end of the valid menu
	 * items. The menu won't render it, but we can still get to it without 
	 * doing a bunch of work. */
	choice[count++] = WIZ_CREATE_ALL_MENU_ITEM;
	choice[count] = index;

	char buf[80];
	char title[80];
	object_base_name(buf, sizeof(buf), index, true);
	if (choose_artifact) {
		strnfmt(title, sizeof(title), "Which artifact %s? ", buf);
	} else {
		strnfmt(title, sizeof(title), "What kind of %s?", buf);
	}

	struct menu *new_menu = menu_new(MN_SKIN_COLUMNS, &wiz_create_item_submenu);

	menu_setpriv(new_menu, count, choice);
	new_menu->title = title;
	new_menu->selections = all_letters;

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	menu_layout_term(new_menu);

	ui_event ret = menu_select(new_menu);

	Term_pop();
	menu_free(menu);

	return ret.type == EVT_ESCAPE;
}

static const menu_iter wiz_create_item_menu = {
	.display_row = wiz_create_item_display,
	.row_handler = wiz_create_item_action
};

/**
 * Choose and create an instance of an artifact or object kind
 */
static void wiz_create_item(bool art)
{
	choose_artifact = art;

	int count = 0;
	int tvals[TV_MAX];

	/* Make a list of all tvals for the filter */
	for (int tval = 0; tval < TV_MAX; tval++) {
		/* Only real object bases */
		if (kb_info[tval].name != NULL) {
			if (art) {
				/* For artifact creation, only include tvals
				 * which have an artifact */
				for (int a = 1; a < z_info->a_max; a++) {
					if (a_info[a].tval == tval) {
						tvals[count++] = tval;
						break;
					}
				}
			} else {
				tvals[count++] = tval;
			}
		}
	}

	tvals[count] = WIZ_CREATE_ALL_MENU_ITEM;

	struct menu *menu = menu_new(MN_SKIN_COLUMNS, &wiz_create_item_menu);

	menu_setpriv(menu, TV_MAX, kb_info);
	menu_set_filter(menu, tvals, count);

	menu->selections = all_letters;
	menu->title = art ? "What kind of artifact?" : "What kind of object?";

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = count,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	menu_select(menu);

	Term_pop();
	menu_free(menu);

	do_cmd_redraw();
}

static void do_cmd_wiz_create_item(void)
{
	wiz_create_item(false);
}

static void do_cmd_wiz_create_artifact(void)
{
	wiz_create_item(true);
}

static void do_cmd_wiz_detect_everything(void)
{
	effect_simple( EF_DETECT_TRAPS,              "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_DOORS,              "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_STAIRS,             "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_GOLD,               "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_OBJECTS,            "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_VISIBLE_MONSTERS,   "22d40", 0, 0, 0, NULL);
	effect_simple( EF_DETECT_INVISIBLE_MONSTERS, "22d40", 0, 0, 0, NULL);
}

/**
 * Tweak an item - make it ego or artifact,
 * give values for modifiers, to_a, to_h or to_d
 */
static void wiz_tweak_item(struct object *obj)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	const char *prompt;
	int val;

	prompt = "Enter new ego item index: ";
	strnfmt(buf, sizeof(buf), "0");
	if (obj->ego) {
		strnfmt(buf, sizeof(buf), "%d", obj->ego->eidx);
	}
	if (!get_string(prompt, buf, 6)) {
		return;
	}
	val = atoi(buf);
	if (val) {
		obj->ego = &e_info[val];
		ego_apply_magic(obj, player->depth);
	} else {
		obj->ego = 0;
	}
	wiz_display_item(obj, true);

	prompt = "Enter new artifact index: ";
	strnfmt(buf, sizeof(buf), "0");
	if (obj->artifact) {
		strnfmt(buf, sizeof(buf), "%d", obj->artifact->aidx);
	}
	if (!get_string(prompt, buf, 6)) {
		return;
	}
	val = atoi(buf);
	if (val) {
		obj->artifact = &a_info[val];
		copy_artifact_data(obj, obj->artifact);
	} else {
		obj->artifact = 0;
	}
	wiz_display_item(obj, true);

#define WIZ_TWEAK(attribute) do { \
	prompt = "Enter new '" #attribute "' setting: "; \
	strnfmt(buf, sizeof(buf), "%d", obj->attribute); \
	if (!get_string(prompt, buf, 6)) { \
		return; \
	} \
	obj->attribute = atoi(buf); \
	wiz_display_item(obj, true); \
} while (0)

	for (int i = 0; i < OBJ_MOD_MAX; i++) {
		WIZ_TWEAK(modifiers[i]);
	}
	WIZ_TWEAK(to_a);
	WIZ_TWEAK(to_h);
	WIZ_TWEAK(to_d);

#undef WIZ_TWEAK
}

/**
 * Apply magic to an item or turn it into an artifact. -Bernd-
 * Actually just regenerate it optionally with the good or great flag set - NRM
 */
static void wiz_reroll_item(struct object *obj)
{
	if (obj->artifact) {
		return;
	}


	bool changed = false;

	/* Get new copy, hack off slays and brands */
	struct object *new = mem_zalloc(sizeof(*new));
	object_copy(new, obj);
	new->slays = NULL;
	new->brands = NULL;

	while (true) {
		wiz_display_item(new, true);

		/* Ask wizard what to do. */
		char ch;
		if (!get_com("[a]ccept, [n]ormal, [g]ood, [e]xcellent? ", &ch)) {
			break;
		}

		if (ch == 'A' || ch == 'a') {
			break;
		} else if (ch == 'n' || ch == 'N') {
			/* Apply normal magic, but first clear object */
			changed = true;
			object_wipe(new, true);
			object_prep(new, obj->kind, player->depth, RANDOMISE);
			apply_magic(new, player->depth, false, false, false, false);
		} else if (ch == 'g' || ch == 'G') {
			/* Apply good magic, but first clear object */
			changed = true;
			object_wipe(new, true);
			object_prep(new, obj->kind, player->depth, RANDOMISE);
			apply_magic(new, player->depth, false, true, false, false);
		} else if (ch == 'e' || ch == 'E') {
			/* Apply great magic, but first clear object */
			changed = true;
			object_wipe(new, true);
			object_prep(new, obj->kind, player->depth, RANDOMISE);
			apply_magic(new, player->depth, false, true, true, false);
		}
	}

	if (changed) {
		/* Record the old pile info */
		struct object *prev = obj->prev;
		struct object *next = obj->next;
		struct object *known_obj = obj->known;

		/* Free slays and brands on the old object by hand */
		free_slay(obj->slays);
		free_brand(obj->brands);

		/* Copy over - slays and brands OK, pile info needs restoring */
		object_copy(obj, new);
		apply_curse_knowledge(obj);
		obj->prev = prev;
		obj->next = next;
		obj->known = known_obj;

		obj->origin = ORIGIN_CHEAT;

		player->upkeep->update |= (PU_BONUS | PU_INVEN);
		player->upkeep->notice |= (PN_COMBINE);
		player->upkeep->redraw |= (PR_INVEN | PR_EQUIP );
	}

	mem_free(new);
}

/*
 * Maximum number of rolls
 */
#define TEST_ROLL 100000

/**
 * Try to create an item again. Output some statistics.
 *
 * The statistics are correct now. We acquire a clean grid, and then
 * repeatedly place an object in this grid, copying it into an item
 * holder, and then deleting the object. We fiddle with the artifact
 * counter flags to prevent weirdness. We use the items to collect
 * statistics on item creation relative to the initial item.
 */
static void wiz_statistics(struct object *obj, int level)
{
	const char *statistics =
		"Rolls: %ld, Matches: %ld, Better: %ld, Worse: %ld, Other: %ld";

	/* Allow multiple artifacts, because breaking the game is fine here */
	if (obj->artifact) {
		obj->artifact->created = false;
	}

	while (true) {
		const char *pmt = "Roll for [n]ormal, [g]ood, or [e]xcellent treasure? ";

		wiz_display_item(obj, true);

		char ch;
		if (!get_com(pmt, &ch)) {
			break;
		}

		bool good = false;
		bool great = false;
		const char *quality = NULL;

		if (ch == 'n' || ch == 'N') {
			good = false;
			great = false;
			quality = "normal";
		} else if (ch == 'g' || ch == 'G') {
			good = true;
			great = false;
			quality = "good";
		} else if (ch == 'e' || ch == 'E') {
			good = true;
			great = true;
			quality = "excellent";
		} else {
			break;
		}

		msg("Creating a lot of %s items. Base level = %d.",
				quality, player->depth);
		event_signal(EVENT_MESSAGE_FLUSH);

		long matches = 0;
		long better = 0;
		long worse = 0;
		long other = 0;

		int roll;
		for (roll = 0; roll < TEST_ROLL; roll++) {
			/* Output every few rolls */
			if (roll < 100 || roll % 100 == 0) {
				ui_event event = inkey_wait(0);
				if (event.type != EVT_NONE) {
					event_signal(EVENT_INPUT_FLUSH);
					break;
				}

				struct loc loc = {0, 0};
				prt(format(statistics,
							roll, matches, better, worse, other),
						loc);
				Term_flush_output();
				Term_redraw_screen();
			}

			struct object *test_obj =
				make_object(cave, level, good, great, false, NULL, 0);

			/* Allow multiple artifacts, because breaking the game is OK here */
			if (obj->artifact) {
				obj->artifact->created = false;
			}

			if (obj->tval != test_obj->tval
					|| obj->sval != test_obj->sval)
			{
				continue;
			}

			bool ismatch = true;
			for (int j = 0; j < OBJ_MOD_MAX; j++) {
				if (test_obj->modifiers[j] != obj->modifiers[j]) {
					ismatch = false;
				}
			}

			bool isbetter = true;
			for (int j = 0; j < OBJ_MOD_MAX; j++) {
				if (test_obj->modifiers[j] < obj->modifiers[j]) {
					isbetter = false;
				}
			}

			bool isworse = true;
			for (int j = 0; j < OBJ_MOD_MAX; j++) {
				if (test_obj->modifiers[j] > obj->modifiers[j]) {
					isworse = false;
				}
			}

			if (ismatch
					&& test_obj->to_a == obj->to_a
					&& test_obj->to_h == obj->to_h
					&& test_obj->to_d == obj->to_d)
			{
				matches++;
			} else if (isbetter
					&& test_obj->to_a >= obj->to_a
					&& test_obj->to_h >= obj->to_h
					&& test_obj->to_d >= obj->to_d)
			{
					better++;
			} else if (isworse
					&& test_obj->to_a <= obj->to_a
					&& test_obj->to_h <= obj->to_h
					&& test_obj->to_d <= obj->to_d)
			{
				worse++;
			} else {
				other++;
			}

			object_delete(&test_obj);
		}

		prt(format(statistics, roll, matches, better, worse, other), loc(0, 0));
		Term_flush_output();
		Term_pop();
	}

	if (obj->artifact) {
		obj->artifact->created = true;
	}
}

/**
 * Change the quantity of an item
 */
static void wiz_quantity_item(struct object *obj, bool carried)
{
	/* Never duplicate artifacts */
	if (obj->artifact) {
		return;
	}

	char buf[3];
	strnfmt(buf, sizeof(buf), "%d", obj->number);

	if (get_string("Quantity: ", buf, 3)) {
		int val = atoi(buf);
		val = MAX(1, MIN(val, 99));

		/* Adjust total weight being carried */
		if (carried) {
			/* Remove the weight of the old number of objects */
			player->upkeep->total_weight -= obj->number * obj->weight;

			/* Add the weight of the new number of objects */
			player->upkeep->total_weight += val * obj->weight;
		}

		/* Adjust charges/timeouts for devices */
		if (tval_can_have_charges(obj)) {
			obj->pval = obj->pval * val / obj->number;
		}
		if (tval_can_have_timeout(obj)) {
			obj->timeout = obj->timeout * val / obj->number;
		}

		obj->number = val;
	}
}

/**
 * Tweak the cursed status of an object.
 *
 * \param obj is the object to curse or decurse
 */
static void wiz_tweak_curse(struct object *obj)
{
	(void) obj;
	// DO SOMETHING - NRM
}

/**
 * Play with an item. Options include:
 *   - Output statistics (via wiz_roll_item)
 *   - Reroll item (via wiz_reroll_item)
 *   - Change properties (via wiz_tweak_item)
 *   - Change the number of items (via wiz_quantity_item)
 */
static void do_cmd_wiz_play_item(void)
{
	const char *prompt = "Play with which object? ";
	const char *reject = "You have nothing to play with.";

	struct object *obj;
	if (!get_item(&obj, prompt, reject, 0, NULL,
				(USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR)))
	{
		return;
	}

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	bool all = true;
	bool changed = false;

	while (true) {
		wiz_display_item(obj, all);

		char ch;
		if (!get_com("[a]ccept [s]tatistics [r]eroll [t]weak [c]urse [q]uantity [k]nown? ", &ch))
			break;

		if (ch == 'A' || ch == 'a') {
			changed = true;
			break;
		} else if (ch == 'c' || ch == 'C') {
			wiz_tweak_curse(obj);
		} else if (ch == 's' || ch == 'S') {
			wiz_statistics(obj, player->depth);
		} else if (ch == 'r' || ch == 'R') {
			wiz_reroll_item(obj);
		} else if (ch == 't' || ch == 'T') {
			wiz_tweak_item(obj);
		} else if (ch == 'k' || ch == 'K') {
			all = !all;
		} else if (ch == 'q' || ch == 'Q') {
			wiz_quantity_item(obj, object_is_carried(player, obj));
		}
	}

	/* Accept change */
	if (changed) {
		msg("Changes accepted.");
		player->upkeep->update |= (PU_INVEN | PU_BONUS);
		player->upkeep->notice |= (PN_COMBINE);
		player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
	} else {
		msg("Changes ignored.");
	}

	Term_pop();
}

/**
 * What happens when you cheat death.  Tsk, tsk.
 */
void wiz_cheat_death(void)
{
	/* Mark social class, reset age, if needed */
	player->age = 1;
	player->noscore |= NOSCORE_WIZARD;

	player->is_dead = false;

	/* Restore hit and spell points */
	player->chp = player->mhp;
	player->chp_frac = 0;
	player->csp = player->msp;
	player->csp_frac = 0;

	/* Healing */
	player_clear_timed(player, TMD_BLIND,     true);
	player_clear_timed(player, TMD_CONFUSED,  true);
	player_clear_timed(player, TMD_POISONED,  true);
	player_clear_timed(player, TMD_AFRAID,    true);
	player_clear_timed(player, TMD_PARALYZED, true);
	player_clear_timed(player, TMD_IMAGE,     true);
	player_clear_timed(player, TMD_STUN,      true);
	player_clear_timed(player, TMD_CUT,       true);

	/* Prevent starvation */
	player_set_food(player, PY_FOOD_MAX - 1);

	/* Cancel recall */
	if (player->word_recall) {
		msg("A tension leaves the air around you...");
		event_signal(EVENT_MESSAGE_FLUSH);

		/* Prevent recall */
		player->word_recall = 0;
	}

	/* Cancel deep descent */
	if (player->deep_descent) {
		msg("The air around you stops swirling...");
		event_signal(EVENT_MESSAGE_FLUSH);

		/* Prevent recall */
		player->deep_descent = 0;
	}

	/* Note cause of death */
	my_strcpy(player->died_from, "Cheating death", sizeof(player->died_from));

	/* Back to the town */
	dungeon_change_level(0);
}

/**
 * Cure everything instantly
 */
static void do_cmd_wiz_cure_all(void)
{
	/* Remove curses */
	for (int i = 0; i < player->body.count; i++) {
		if (player->body.slots[i].obj) {
			free_curse(player->body.slots[i].obj->curses, true);
			player->body.slots[i].obj->curses = NULL;
		}
	}

	/* Restore stats */
	effect_simple(EF_RESTORE_STAT, "0", STAT_STR, 0, 0, NULL);
	effect_simple(EF_RESTORE_STAT, "0", STAT_INT, 0, 0, NULL);
	effect_simple(EF_RESTORE_STAT, "0", STAT_WIS, 0, 0, NULL);
	effect_simple(EF_RESTORE_STAT, "0", STAT_DEX, 0, 0, NULL);
	effect_simple(EF_RESTORE_STAT, "0", STAT_CON, 0, 0, NULL);

	/* Restore the level */
	effect_simple(EF_RESTORE_EXP, "0", 1, 0, 0, NULL);

	/* Heal the player */
	player->chp = player->mhp;
	player->chp_frac = 0;

	/* Restore mana */
	player->csp = player->msp;
	player->csp_frac = 0;

	/* Cure stuff */
	player_clear_timed(player, TMD_BLIND,     true);
	player_clear_timed(player, TMD_CONFUSED,  true);
	player_clear_timed(player, TMD_POISONED,  true);
	player_clear_timed(player, TMD_AFRAID,    true);
	player_clear_timed(player, TMD_PARALYZED, true);
	player_clear_timed(player, TMD_IMAGE,     true);
	player_clear_timed(player, TMD_STUN,      true);
	player_clear_timed(player, TMD_CUT,       true);
	player_clear_timed(player, TMD_SLOW,      true);
	player_clear_timed(player, TMD_AMNESIA,   true);

	/* No longer hungry */
	player_set_food(player, PY_FOOD_MAX - 1);

	do_cmd_redraw();

	msg("You feel *much* better!");
}

/**
 * Go to any level. optionally choosing level generation algorithm
 */
static void do_cmd_wiz_jump(void)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	char prompt[ANGBAND_TERM_STANDARD_WIDTH];

	strnfmt(prompt, sizeof(prompt), "Jump to level (0-%d): ", z_info->max_depth - 1);

	strnfmt(buf, sizeof(buf), "%d", player->depth);

	if (get_string(prompt, buf, 11)) {
		int depth = atoi(buf);
		depth = MIN(z_info->max_depth - 1, MAX(0, depth));

		strnfmt(prompt, sizeof(prompt), "Choose cave_profile?");

		if (get_check(prompt)) {
			player->noscore |= NOSCORE_JUMPING;
		}

		msg("You jump to dungeon level %d.", depth);
		dungeon_change_level(depth);
		cmdq_push(CMD_HOLD);
	}
}

/**
 * Become aware of all object flavors
 */
static void do_cmd_wiz_learn()
{
	int level = 100;

	for (int i = 1; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		if (kind != NULL && kind->name != NULL) {
			if (kind->level <= level) {
				kind->aware = true;
			}
		}
	}
	
	msg("You now know about many items!");
}

/*
 * Magic Mapping
 */
static void do_cmd_wiz_magic_map(void)
{
	effect_simple(EF_MAP_AREA, "22d40", 0, 0, 0, NULL);
}

/*
 * Wizard Light the Level
 */
static void do_cmd_wiz_light(void)
{
	wiz_light(cave, true);
}

/*
 * Cast phase door
 */
static void do_cmd_wiz_phase_door(void)
{
	effect_simple(EF_TELEPORT, "10", 0, 1, 0, NULL);
}

/**
 * Rerate hitpoints
 */
static void do_cmd_wiz_rerate(void)
{
	int min_value = (PY_MAX_LEVEL * 3 * (player->hitdie - 1)) / 8 + PY_MAX_LEVEL;
	int max_value = (PY_MAX_LEVEL * 5 * (player->hitdie - 1)) / 8 + PY_MAX_LEVEL;

	player->player_hp[0] = player->hitdie;

	do {
		for (int i = 1; i < PY_MAX_LEVEL; i++) {
			player->player_hp[i] = randint1(player->hitdie);
			player->player_hp[i] += player->player_hp[i - 1];
		}
	} while (player->player_hp[PY_MAX_LEVEL - 1] >= min_value
			&& player->player_hp[PY_MAX_LEVEL - 1] <= max_value);

	int percent = ((long) player->player_hp[PY_MAX_LEVEL - 1] * 200L) /
			(player->hitdie + ((PY_MAX_LEVEL - 1) * player->hitdie));

	do_cmd_redraw();

	msg("Current Life Rating is %d/100.", percent);
}

/**
 * Hit all monsters in LOS
 */
static void do_cmd_wiz_hit_monsters(void)
{
	effect_simple(EF_PROJECT_LOS, "10000", GF_DISP_ALL, 0, 0, NULL);
}

/**
 * Summon some creatures
 */
static void do_cmd_wiz_summon(int num)
{
	for (int i = 0; i < num; i++) {
		effect_simple(EF_SUMMON, "1", 0, 0, 0, NULL);
	}
}

/**
 * Summon a creature of the specified type
 */
static void do_cmd_wiz_named_monster(struct monster_race *race, bool sleep)
{
	assert(race != NULL);

	/* Try 10 times */
	for (int i = 0; i < 10; i++) {
		int x;
		int y;

		/* Pick a location */
		scatter(cave, &y, &x, player->py, player->px, 1, true);

		/* Require empty grids */
		if (square_isempty(cave, y, x)) {
			/* Place it (allow groups) */
			if (place_new_monster(cave, y, x,
						race, sleep, true, ORIGIN_DROP_WIZARD))
			{
				return;
			}
		}
	}
}

/*
 * Summon a named monster
 */
static void do_cmd_wiz_summon_monster(void)
{
	char name[ANGBAND_TERM_STANDARD_WIDTH] = {0};
	struct monster_race *race = NULL;

	show_prompt("Summon which monster? ", false);

	if (askfor_aux(name, sizeof(name), NULL)) {
		int r_idx = get_idx_from_name(name);
		if (r_idx != 0) {
			race = &r_info[r_idx];
		} else {
			race = lookup_monster(name); 
		}
			
		player->upkeep->redraw |= (PR_MAP | PR_MONLIST);
	}

	if (race) {
		do_cmd_wiz_named_monster(race, true);
	} else {
		msg("No monster found.");
	}
}

/*
 * Summon random_monsters
 */
static void do_cmd_wiz_summon_monsters(void)
{
	int quantity = get_quantity("How many monsters? ", 40);

	if (quantity > 0) {
		do_cmd_wiz_summon(quantity);
	}
}

/*
 * Un-hide all monsters
 */
static void do_cmd_wiz_reveal_monsters(void)
{
	effect_simple(EF_DETECT_VISIBLE_MONSTERS, "500d500", 0, 0, 0, NULL);
	effect_simple(EF_DETECT_INVISIBLE_MONSTERS, "500d500", 0, 0, 0, NULL);
}

/**
 * Delete all nearby monsters
 */
static void do_cmd_wiz_monsters_delete(int dist)
{
	/* Banish everyone nearby */
	for (int i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		if (mon->race != NULL && mon->cdis <= dist) {
			delete_monster_idx(i);
		}
	}

	player->upkeep->redraw |= PR_MONLIST;
}

static void wiz_show_feature(struct loc coords)
{
	uint32_t attr = square_ispassable(cave, coords.y, coords.x) ?
		COLOUR_YELLOW : COLOUR_RED;

	if (coords.x == player->px && coords.y == player->py) {
		print_rel(DISPLAY_CAVE, attr, L'@', coords);
	} else if (square_ispassable(cave, coords.y, coords.x)) {
		print_rel(DISPLAY_CAVE, attr, L'*', coords);
	} else {
		print_rel(DISPLAY_CAVE, attr, L'#', coords);
	}
}

/*
 * Delete (banish) some monsters
 */
static void do_cmd_wiz_banish(void)
{
	int distance = get_quantity("Zap within what distance? ", z_info->max_sight);

	if (distance > 0) {
		do_cmd_wiz_monsters_delete(distance);
	}
}

/**
 * Query square flags - needs alteration if list-square-flags.h changes
 */
static void do_cmd_wiz_square_flag(void)
{
	char cmd;
	if (!get_com("Debug command query: ", &cmd)) {
		return;
	}

	int flag = 0;
	switch (cmd) {
		case 'g': flag = (SQUARE_GLOW);         break;
		case 'r': flag = (SQUARE_ROOM);         break;
		case 'a': flag = (SQUARE_VAULT);        break;
		case 's': flag = (SQUARE_SEEN);         break;
		case 'v': flag = (SQUARE_VIEW);         break;
		case 'w': flag = (SQUARE_WASSEEN);      break;
		case 'f': flag = (SQUARE_FEEL);         break;
		case 't': flag = (SQUARE_TRAP);         break;
		case 'n': flag = (SQUARE_INVIS);        break;
		case 'i': flag = (SQUARE_WALL_INNER);   break;
		case 'o': flag = (SQUARE_WALL_OUTER);   break;
		case 'l': flag = (SQUARE_WALL_SOLID);   break;
		case 'x': flag = (SQUARE_MON_RESTRICT); break;
	}

	region reg;
	get_cave_region(&reg);

	/* Scan map */
	for (int y = reg.y, endy = reg.y + reg.h; y < endy; y++) {
		for (int x = reg.x, endx = reg.x + reg.w; x < endx; x++) {
			if (square_in_bounds_fully(cave, y, x)) {
				/* Given flag, show only those grids;
				 * given no flag, show known grids */
				if ((flag != 0 && sqinfo_has(cave->squares[y][x].info, flag))
						|| (flag == 0 && square_isknown(cave, y, x)))
				{
					wiz_show_feature(loc(x, y));
				}
			}
		}
	}

	Term_flush_output();

	show_prompt("Press any key.", false);
	inkey_any();
	clear_prompt();

	print_map(DISPLAY_CAVE);
}

/*
 * Create a trap
 */
static void do_cmd_wiz_place_trap(void)
{
	if (player->depth == 0) {
		msg("You can't place a trap in the town!");
		return;
	}
	if (!square_isfloor(cave, player->py, player->px)) {
		msg("You can't place a trap there!");
		return;
	}

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	if (get_string("Create which trap? ", buf, sizeof(buf))) {
		struct trap_kind *trap = lookup_trap(buf);
		if (trap != NULL) {
			place_trap(cave, player->py, player->px, trap->tidx, 0);
		} else {
			msg("Trap not found.");
		}
	}
}

/**
 * Query terrain - needs alteration if terrain types change
 */
static void do_cmd_wiz_features(void)
{
	int *feat = NULL;
	int featf[] = {FEAT_FLOOR};
	int feato[] = {FEAT_OPEN};
	int featb[] = {FEAT_BROKEN};
	int featu[] = {FEAT_LESS};
	int featz[] = {FEAT_MORE};
	int featt[] = {FEAT_LESS, FEAT_MORE};
	int featc[] = {FEAT_CLOSED};
	int featd[] = {FEAT_CLOSED, FEAT_OPEN, FEAT_BROKEN, FEAT_SECRET};
	int feath[] = {FEAT_SECRET};
	int featm[] = {FEAT_MAGMA, FEAT_MAGMA_K};
	int featq[] = {FEAT_QUARTZ, FEAT_QUARTZ_K};
	int featg[] = {FEAT_GRANITE};
	int featp[] = {FEAT_PERM};
	int featr[] = {FEAT_RUBBLE};
	int length = 0;

	char cmd;
	if (!get_com("Debug Command Feature Query: ", &cmd)) {
		return;
	}

	switch (cmd) {
		/* Floors */
		case 'f': feat = featf; length = 1; break;
		/* Open doors */
		case 'o': feat = feato; length = 1; break;
		/* Broken doors */
		case 'b': feat = featb; length = 1; break;
		/* Upstairs */
		case 'u': feat = featu; length = 1; break;
		/* Downstairs */
		case 'z': feat = featz; length = 1; break;
		/* Stairs */
		case 't': feat = featt; length = 2; break;
		/* Closed doors */
		case 'c': feat = featc; length = 8; break;
		/* Doors */
		case 'd': feat = featd; length = 11; break;
		/* Secret doors */
		case 'h': feat = feath; length = 1; break;
		/* Magma */
		case 'm': feat = featm; length = 3; break;
		/* Quartz */
		case 'q': feat = featq; length = 3; break;
		/* Granite */
		case 'g': feat = featg; length = 1; break;
		/* Permanent wall */
		case 'p': feat = featp; length = 1; break;
		/* Rubble */
		case 'r': feat = featr; length = 1; break;
	}

	region reg;
	get_cave_region(&reg);

	/* Scan map */
	for (int y = reg.y, endy = reg.y + reg.h; y < endy; y++) {
		for (int x = reg.x, endx = reg.x + reg.w; x < endx; x++) {
			if (square_in_bounds_fully(cave, y, x)) {
				for (int i = 0; i < length; i++) {
					if (cave->squares[y][x].feat == feat[i]) {
						wiz_show_feature(loc(x, y));
						break;
					}
				}
			}
		}
	}

	Term_flush_output();

	show_prompt("Press any key.", false);
	inkey_any();
	clear_prompt();

	print_map(DISPLAY_CAVE);
}

/*
 * Wipe recall for a monster
 */
static void do_cmd_wiz_wipe_recall(void)
{
	const char *prompt =
		"Wipe recall for [a]ll monsters or [s]pecific monster? ";

	char sym;
	if (!get_com(prompt, &sym)) {
		return;
	}

	if (sym == 'a' || sym == 'A') {
		for (int i = 0; i < z_info->r_max; i++) {
			wipe_monster_lore(&r_info[i], &l_list[i]);
		}
		msg("Done.");
	} else if (sym == 's' || sym == 'S') {
		char name[ANGBAND_TERM_STANDARD_WIDTH] = "";
		const struct monster_race *race = NULL;

		show_prompt("Which monster? ", false);

		if (askfor_aux(name, sizeof(name), NULL)) {
			int r_idx = get_idx_from_name(name);
			if (r_idx != 0) {
				race = &r_info[r_idx];
			} else {
				race = lookup_monster(name); 
			}
		}

		/* Did we find a valid monster? */
		if (race != NULL) {
			wipe_monster_lore(race, get_lore(race));
		} else {
			msg("No monster found.");
		}
	}
}

/*
 * Get full recall for a monster
 */
static void do_cmd_wiz_monster_recall(void)
{
	const char *prompt =
		"Full recall for [a]ll monsters or [s]pecific monster? ";

	char sym;
	if (!get_com(prompt, &sym)) {
		return;
	}

	if (sym == 'a' || sym == 'A') {
		for (int i = 0; i < z_info->r_max; i++) {
			cheat_monster_lore(&r_info[i], &l_list[i]);
		}
		msg("Done.");
	} else if (sym == 's' || sym == 'S') {
		char name[ANGBAND_TERM_STANDARD_WIDTH] = "";
		const struct monster_race *race = NULL;
			
		show_prompt("Which monster? ", false);

		if (askfor_aux(name, sizeof(name), NULL)) {
			int r_idx = get_idx_from_name(name);
			if (r_idx != 0) {
				race = &r_info[r_idx];
			} else {
				race = lookup_monster(name); 
			}
		}

		if (race != NULL) {
			cheat_monster_lore(race, get_lore(race));
			msg("Done.");
		} else {
			msg("No monster found.");
		}
	}
}

/**
 * Create lots of items.
 */
static void wiz_test_kind(int tval)
{
	for (int sval = 0; sval < 255; sval++) {
		/* This spams failure messages, but that's the downside of wizardry */
		struct object_kind *kind = lookup_kind(tval, sval);

		if (kind != NULL) {
			struct object *obj;
			struct object *known_obj;

			/* Create the item */
			if (tval == TV_GOLD) {
				obj = make_gold(player->depth, kind->name);
			} else {
				obj = object_new();
				object_prep(obj, kind, player->depth, RANDOMISE);

				/* Apply magic (no messages, no artifacts) */
				apply_magic(obj, player->depth, false, false, false, false);
				apply_curse_knowledge(obj);

				/* Mark as cheat, and where created */
				obj->origin = ORIGIN_CHEAT;
				obj->origin_depth = player->depth;
			}

			/* Make a known object */
			known_obj = object_new();
			obj->known = known_obj;

			/* Drop the object from heaven */
			drop_near(cave, &obj, 0, player->py, player->px, true);
		}
	}

	msg("Done.");
}

/**
 * Create some good objects
 */
static void do_cmd_wiz_good_objects(void)
{
	int quantity = get_quantity("How many good objects? ", 40);

	if (quantity > 0) {
		acquirement(player->py, player->px, player->depth, quantity, false);
	}
}

/**
 * Create some exceptional objects
 */
static void do_cmd_wiz_very_good_objects(void)
{
	int quantity = get_quantity("How many great objects? ", 40);

	if (quantity > 0) {
		acquirement(player->py, player->px, player->depth, quantity, true);
	}
}

/**
 * Create lots of objects
 */
static void do_cmd_wiz_lots_objects(void)
{
	int tval = get_quantity("Create all items of what tval? ", 255);

	if (tval > 0) {
		wiz_test_kind(tval);
	}
}

/**
 * Display the debug commands help file.
 */
static void do_cmd_wiz_help(void) 
{
	struct term_hints hints = {
		.width    = ANGBAND_TERM_STANDARD_WIDTH,
		.height   = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_CENTER,
		.purpose  = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	show_file("debug.txt");

	Term_pop();
}

/**
 * Advance the player to level 50 with max stats and other bonuses.
 */
static void do_cmd_wiz_level_50(void)
{
	/* Max stats */
	for (int i = 0; i < STAT_MAX; i++) {
		player->stat_cur[i] = player->stat_max[i] = 118;
	}

	/* Lots of money */
	player->au = 1000000L;

	/* Level 50 */
	player_exp_gain(player, PY_MAX_EXP);

	/* Heal the player */
	player->chp = player->mhp;
	player->chp_frac = 0;

	/* Restore mana */
	player->csp = player->msp;
	player->csp_frac = 0;

	do_cmd_redraw();
}

/*
 * Increase player's experience
 */
static void do_cmd_wiz_gain_exp(void)
{
	int quantity = get_quantity("Gain how much experience? ", 9999);

	if (quantity > 0) {
		player_exp_gain(player, quantity);
	}
}

/**
 * Prompt for an effect and perform it.
 */
void do_cmd_wiz_effect(void)
{
	char name[ANGBAND_TERM_STANDARD_WIDTH] = "";
	char dice[ANGBAND_TERM_STANDARD_WIDTH] = "0";
	char param[ANGBAND_TERM_STANDARD_WIDTH] = "0";

	int index = -1;
	show_prompt("Do which effect? ", false);
	if (askfor_aux(name, sizeof(name), NULL)) {
		/* See if an effect index was entered */
		index = get_idx_from_name(name);

		/* If not, find the effect with that name */
		if (index <= EF_NONE || index >= EF_MAX) {
			index = effect_lookup(name);
		}
	}

	show_prompt("Enter damage dice (eg 1+2d6M2): ", false);
	if (!askfor_aux(dice, sizeof(dice), NULL)) {
		my_strcpy(dice, "0", sizeof(dice));
	}

	int p1 = 0;
	show_prompt("Enter name or number for first parameter: ", false);
	if (askfor_aux(param, sizeof(param), NULL)) {
		/* See if an effect parameter was entered */
		p1 = effect_param(index, param);
		if (p1 < 0) {
			p1 = 0;
		}
	}

	int p2 = get_quantity("Enter second parameter: ", 100);
	int p3 = get_quantity("Enter third parameter: ", 100);

	clear_prompt();

	if (index > EF_NONE && index < EF_MAX) {
		bool ident = false;
		effect_simple(index, dice, p1, p2, p3, &ident);
		if (ident) {
			msg("Identified!");
		}
	} else {
		msg("No effect found.");
	}
}

/*
 * Quit the game without saving
 */
static void do_cmd_wiz_quit(void)
{
	if (get_check("Really quit without saving? "))
		quit("user choice");
}

/**
 * Main switch for processing debug commands.
 * This is a step back in time to how all commands used to be processed.
 * (Except everything used to be inlined for some reason).
 */
void get_debug_command(void)
{
	char wiz_command;

	if (!get_com("Debug Command: ", &wiz_command)) {
		return;
	}

	switch (wiz_command) {
		case '"': do_cmd_spoilers();              break;
		case '?': do_cmd_wiz_help();              break;
		case 'a': do_cmd_wiz_cure_all();          break;
		case 'A': do_cmd_wiz_level_50();          break;
		case 'b': do_cmd_wiz_teleport_target();   break;
		case 'c': do_cmd_wiz_create_item();       break;
		case 'C': do_cmd_wiz_create_artifact();   break;
		case 'd': do_cmd_wiz_detect_everything(); break;
		case 'D': disconnect_stats();             break;
		case 'e': do_cmd_wiz_change();            break;
		case 'E': do_cmd_wiz_effect();            break;
		case 'f': stats_collect();                break;
		case 'F': do_cmd_wiz_features();          break;
		case 'g': do_cmd_wiz_good_objects();      break;
		case 'h': do_cmd_wiz_rerate();            break;
		case 'H': do_cmd_wiz_hit_monsters();      break;
		case 'j': do_cmd_wiz_jump();              break;
		case 'l': do_cmd_wiz_learn();             break;
		case 'm': do_cmd_wiz_magic_map();         break;
		case 'n': do_cmd_wiz_summon_monster();    break;
		case 'o': do_cmd_wiz_play_item();         break;
		case 'p': do_cmd_wiz_phase_door();        break;
		case 'P': pit_stats();                    break;
		case 'q': do_cmd_wiz_square_flag();       break;
		case 'r': do_cmd_wiz_monster_recall();    break;
		case 's': do_cmd_wiz_summon_monsters();   break;
		case 'S': stats_collect();                break;
		case 't': do_cmd_wiz_teleport();          break;
		case 'T': do_cmd_wiz_place_trap();        break;
		case 'u': do_cmd_wiz_reveal_monsters();   break;
		case 'v': do_cmd_wiz_very_good_objects(); break;
		case 'V': do_cmd_wiz_lots_objects();      break;
		case 'w': do_cmd_wiz_light();             break;
		case 'W': do_cmd_wiz_wipe_recall();       break;
		case 'x': do_cmd_wiz_gain_exp();          break;
		case 'X': do_cmd_wiz_quit();              break;
		case 'z': do_cmd_wiz_banish();            break;
		case '_': do_cmd_wiz_show_flow();         break;

		default:
			break;
	}
}
