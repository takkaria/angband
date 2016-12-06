/**
 * \file ui2-wiz-spoil.c
 * \brief Spoiler generation
 *
 * Copyright (c) 1997 Ben Harrison, and others
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
#include "buildid.h"
#include "cmds.h"
#include "game-world.h"
#include "init.h"
#include "mon-lore.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-info.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "ui2-input.h"
#include "ui2-knowledge.h"
#include "ui2-menu.h"
#include "ui2-mon-lore.h"
#include "ui2-wizard.h"
#include "z-file.h"

/**
 * ------------------------------------------------------------------------
 * Item Spoilers by Ben Harrison (benh@phial.com)
 * ------------------------------------------------------------------------
 */

/**
 * The spoiler file being created
 */
static ang_file *fh = NULL;

/**
 * Write out `n' of the character `c' to the spoiler file
 */
static void spoiler_out_n_chars(ang_file *fh, unsigned n, char c)
{
	while (n > 0) {
		file_writec(fh, c);
		n--;
	}
}

/**
 * Write out `n' newlines to the spoiler file
 */
static void spoiler_blanklines(ang_file *fh, unsigned n)
{
	spoiler_out_n_chars(fh, n, '\n');
}

/**
 * Write a line to the spoiler file and then "underline" it with hypens
 */
static void spoiler_underline(ang_file *fh, const char *str, char c)
{
	file_putf(fh, "%s\n", str);
	spoiler_out_n_chars(fh, strlen(str), c);
	file_writec(fh, '\n');
}

/**
 * The basic items categorized by type
 */
static const grouper group_item[] = {
	{TV_SHOT,        "Ammo"},
	{TV_ARROW,        NULL},
	{TV_BOLT,         NULL},
	{TV_BOW,         "Bows"},
	{TV_SWORD,       "Weapons"},
	{TV_POLEARM,      NULL},
	{TV_HAFTED,       NULL},
	{TV_DIGGING,      NULL},
	{TV_SOFT_ARMOR,  "Armour (Body)"},
	{TV_HARD_ARMOR,   NULL},
	{TV_DRAG_ARMOR,   NULL},
	{TV_CLOAK,       "Armour (Misc)"},
	{TV_SHIELD,       NULL},
	{TV_HELM,         NULL},
	{TV_CROWN,        NULL},
	{TV_GLOVES,       NULL},
	{TV_BOOTS,        NULL},
	{TV_AMULET,      "Amulets"},
	{TV_RING,        "Rings"},
	{TV_SCROLL,      "Scrolls"},
	{TV_POTION,      "Potions"},
	{TV_FOOD,        "Food"},
	{TV_MUSHROOM,    "Mushrooms"},
	{TV_ROD,         "Rods"},
	{TV_WAND,        "Wands"},
	{TV_STAFF,       "Staffs"},
	{TV_MAGIC_BOOK,  "Books (Mage)"},
	{TV_PRAYER_BOOK, "Books (Priest)"},
	{TV_CHEST,       "Chests"},
	{TV_LIGHT,       "Lights and fuel"},
	{TV_FLASK,        NULL},
	{0,              ""}
};

/**
 * Describe the kind
 */
static void kind_info(int k_idx,
		char *buf, size_t buf_size,
		char *dam, size_t dam_size,
		char *wgt, size_t wgt_size,
		int *lev, s32b *val)
{
	struct object_kind *kind = &k_info[k_idx];
	struct object *obj = object_new();
	struct object *known_obj = object_new();

	/* Prepare a fake item */
	object_prep(obj, kind, 0, MAXIMISE);

	/* Cancel bonuses */
	for (int i = 0; i < OBJ_MOD_MAX; i++) {
		obj->modifiers[i] = 0;
	}

	obj->to_a = 0;
	obj->to_h = 0;
	obj->to_d = 0;

	/* Level */
	(*lev) = kind->level;

	/* Make known */
	object_copy(known_obj, obj);
	obj->known = known_obj;

	/* Value */
	(*val) = object_value(obj, 1);

	/* Description (too brief) */
	if (buf && buf_size > 0) {
		object_desc(buf, buf_size, obj, ODESC_BASE | ODESC_SPOIL);
	}

	/* Weight */
	if (wgt && wgt_size > 0) {
		strnfmt(wgt, wgt_size, "%3d.%d", obj->weight / 10, obj->weight % 10);
	}

	/* Damage */
	if (dam && dam_size > 0) {
		if (tval_is_ammo(obj) || tval_is_melee_weapon(obj)) {
			strnfmt(dam, dam_size, "%dd%d", obj->dd, obj->ds);
		} else if (tval_is_armor(obj)) {
			strnfmt(dam, dam_size, "%d", obj->ac);
		}
	}

	object_delete(&known_obj);
	object_delete(&obj);
}

/**
 * Create a spoiler file for items
 */
static void spoil_obj_desc(const char *fname)
{
	/* Open the file */
	char file_name[4096];
	path_build(file_name, sizeof(file_name), ANGBAND_DIR_USER, fname);
	fh = file_open(file_name, MODE_WRITE, FTYPE_TEXT);

	if (fh == NULL) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Header */
	file_putf(fh, "Spoiler File -- Basic Items (%s)\n\n\n", buildid);

	/* More Header */
	const char *format = "%-51s  %7s%6s%4s%9s\n";
	file_putf(fh, format,
			"Description", "Dam/AC", "Wgt", "Lev", "Cost");
	file_putf(fh, format,
			"----------------------------------------",
			"------", "---", "---", "----");

	u16b who[200];

	/* List the groups */
	for (int i = 0, n = 0; group_item[i].tval != 0; i++) {
		/* Write out the group title */
		if (group_item[i].name != NULL) {
			/* Bubble sort by cost and then level */
			for (int s = 0; s < n - 1; s++) {
				for (int t = 0; t < n - 1; t++) {
					int i1 = t;
					int i2 = t + 1;

					int lev1;
					int lev2;

					s32b val1;
					s32b val2;

					kind_info(who[i1], NULL, 0, NULL, 0, NULL, 0, &lev1, &val1);
					kind_info(who[i2], NULL, 0, NULL, 0, NULL, 0, &lev2, &val2);

					if (val1 > val2 || (val1 == val2 && lev1 > lev2)) {
						int tmp = who[i1];
						who[i1] = who[i2];
						who[i2] = tmp;
					}
				}
			}

			/* Spoil each item */
			for (int s = 0; s < n; s++) {
				char buf[80];
				char wgt[80];
				char dam[80];

				int lev;
				s32b val;

				/* Describe the kind */
				kind_info(who[s],
						buf, sizeof(buf),
						dam, sizeof(dam),
						wgt, sizeof(wgt),
						&lev, &val);

				file_putf(fh, "  %-51s%7s%6s%4d%9ld\n",
						buf, dam, wgt, lev, (long) val);
			}

			/* Start a new set */
			n = 0;

			/* Notice the end */
			if (group_item[i].tval != 0) {
				file_putf(fh, "\n\n%s\n\n", group_item[i].name);
			}
		}

		/* Get legal item types */
		for (int k = 1; k < z_info->k_max; k++) {
			struct object_kind *kind = &k_info[k];

			/* Skip wrong tvals and instant artefacts */
			if (kind->tval == group_item[i].tval
					&& !kf_has(kind->kind_flags, KF_INSTA_ART))
			{

				/* Save the index */
				who[n] = k;
				n++;
			}
		}
	}

	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
	} else {
		msg("Successfully created a spoiler file.");
	}
}

/**
 * ------------------------------------------------------------------------
 * Artifact Spoilers by: randy@PICARD.tamu.edu (Randy Hutson)
 *
 * (Mostly) rewritten in 2002 by Andi Sidwell and Robert Ruehlmann.
 * ------------------------------------------------------------------------
 */

/**
 * The artifacts categorized by type
 */
static const grouper group_artifact[] = {
	{TV_SWORD,         "Edged Weapons"},
	{TV_POLEARM,       "Polearms"},
	{TV_HAFTED,        "Hafted Weapons"},
	{TV_BOW,           "Bows"},
	{TV_DIGGING,       "Diggers"},
	{TV_SOFT_ARMOR,    "Body Armor"},
	{TV_HARD_ARMOR,     NULL},
	{TV_DRAG_ARMOR,     NULL},
	{TV_CLOAK,         "Cloaks"},
	{TV_SHIELD,        "Shields"},
	{TV_HELM,          "Helms/Crowns"},
	{TV_CROWN,          NULL},
	{TV_GLOVES,        "Gloves"},
	{TV_BOOTS,         "Boots"},
	{TV_LIGHT,         "Light Sources"},
	{TV_AMULET,        "Amulets"},
	{TV_RING,          "Rings"},
	{0, NULL }
};

/**
 * Create a spoiler file for artifacts
 */
static void spoil_artifact(const char *fname)
{
	/* Build the filename */
	char file_name[4096];
	path_build(file_name, sizeof(file_name), ANGBAND_DIR_USER, fname);
	fh = file_open(file_name, MODE_WRITE, FTYPE_TEXT);

	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump the header */
	spoiler_underline(fh, format("Artifact Spoilers for %s", buildid), '=');

	file_putf(fh, "\nRandart seed is %u\n", seed_randart);

	/* List the artifacts by tval */
	for (int i = 0; group_artifact[i].tval; i++) {
		/* Write out the group title */
		if (group_artifact[i].name) {
			spoiler_blanklines(fh, 2);
			spoiler_underline(fh, group_artifact[i].name, '=');
			spoiler_blanklines(fh, 1);
		}

		/* Now search through all of the artifacts */
		for (int a = 1; a < z_info->a_max; a++) {
			struct artifact *art = &a_info[a];

			/* We only want objects in the current group */
			if (art->tval != group_artifact[i].tval) {
				continue;
			}

			/* Get local object */
			struct object *obj = object_new();
			struct object *known_obj = object_new();

			/* Attempt to "forge" the artifact */
			if (!make_fake_artifact(obj, art)) {
				object_delete(&known_obj);
				object_delete(&obj);
				continue;
			}

			/* Grab artifact name */
			object_copy(known_obj, obj);
			obj->known = known_obj;

			char buf[80];
			object_desc(buf, sizeof(buf), obj, ODESC_PREFIX |
				ODESC_COMBAT | ODESC_EXTRA | ODESC_SPOIL);

			/* Print name and underline */
			spoiler_underline(fh, buf, '-');

			/* Temporarily blank the artifact flavour text - spoilers
			   spoil the mechanics, not the atmosphere. */
			char *temp = obj->artifact->text;
			obj->artifact->text = NULL;

			/* Write out the artifact description to the spoiler file */
			object_info_spoil(fh, obj, 80);

			/* Put back the flavour */
			obj->artifact->text = temp;

			/*
			 * Determine the minimum and maximum depths an
			 * artifact can appear, its rarity, its weight,
			 * and its power rating.
			 */
			file_putf(fh,
					"\nMin Level %u, Max Level %u, Generation chance %u, Power %d, %d.%d lbs\n",
					art->alloc_min,
					art->alloc_max,
					art->alloc_prob,
					object_power(obj, false, NULL),
					art->weight / 10,
					art->weight % 10);

			if (OPT(player, birth_randarts)) {
				file_putf(fh, "%s.\n", art->text);
			}

			/* Terminate the entry */
			spoiler_blanklines(fh, 2);
			object_delete(&known_obj);
			object_delete(&obj);
		}
	}

	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
	} else {
		msg("Successfully created a spoiler file.");
	}
}

/**
 * ------------------------------------------------------------------------
 * Brief monster spoilers
 * ------------------------------------------------------------------------
 */


static int cmp_mexp(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (r_info[ia].mexp < r_info[ib].mexp) {
		return -1;
	}

	if (r_info[ia].mexp > r_info[ib].mexp) {
		return 1;
	}

	return
		a < b ? -1 :
		a > b ?  1 : 0;
}

static int cmp_level(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (r_info[ia].level < r_info[ib].level) {
		return -1;
	}

	if (r_info[ia].level > r_info[ib].level) {
		return 1;
	}

	return cmp_mexp(a, b);
}

static int cmp_monsters(const void *a, const void *b)
{
	return cmp_level(a, b);
}

/**
 * Create a brief spoiler file for monsters
 */
static void spoil_mon_desc(const char *fname)
{
	/* Build the filename */
	char file_name[1024];
	path_build(file_name, sizeof(file_name), ANGBAND_DIR_USER, fname);
	fh = file_open(file_name, MODE_WRITE, FTYPE_TEXT);

	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump the header */
	file_putf(fh, "Monster Spoilers for %s\n", buildid);
	file_putf(fh, "------------------------------------------\n\n");

	/* Dump the header */
	file_putf(fh, "%-40.40s%4s%4s%6s%8s%4s  %11.11s\n",
	        "Name", "Lev", "Rar", "Spd", "Hp", "Ac", "Visual Info");
	file_putf(fh, "%-40.40s%4s%4s%6s%8s%4s  %11.11s\n",
	        "----", "---", "---", "---", "--", "--", "-----------");

	/* Allocate the "who" array */
	u16b *who = mem_zalloc(z_info->r_max * sizeof(*who));

	/* Scan the monsters (except the ghost) */
	int n_who = 0;
	for (int i = 1; i < z_info->r_max - 1; i++) {
		struct monster_race *race = &r_info[i];
		if (race->name) {
			who[n_who] = i;
			n_who++;
		}
	}

	/* Sort the array by dungeon depth of monsters */
	sort(who, n_who, sizeof(*who), cmp_monsters);

	/* Scan again */
	for (int i = 0; i < n_who; i++) {
		struct monster_race *race = &r_info[who[i]];
		const char *name = race->name;

		/* Get the "name" */
		char nam[80];
		if (rf_has(race->flags, RF_QUESTOR)) {
			strnfmt(nam, sizeof(nam), "[Q] %s", name);
		} else if (rf_has(race->flags, RF_UNIQUE)) {
			strnfmt(nam, sizeof(nam), "[U] %s", name);
		} else {
			strnfmt(nam, sizeof(nam), "The %s", name);
		}

		/* Level */
		char lev[80];
		strnfmt(lev, sizeof(lev), "%d", race->level);

		/* Rarity */
		char rar[80];
		strnfmt(rar, sizeof(rar), "%d", race->rarity);

		/* Speed */
		char spd[80];
		if (race->speed >= 110) {
			strnfmt(spd, sizeof(spd), "+%d", (race->speed - 110));
		} else {
			strnfmt(spd, sizeof(spd), "-%d", (110 - race->speed));
		}

		/* Armor Class */
		char ac[80];
		strnfmt(ac, sizeof(ac), "%d", race->ac);

		/* Hitpoints */
		char hp[80];
		strnfmt(hp, sizeof(hp), "%d", race->avg_hp);

		/* Experience */
		char exp[80];
		strnfmt(exp, sizeof(exp), "%ld", (long) race->mexp);

		/* Dump the info */
		file_putf(fh, "%-40.40s%4s%4s%6s%8s%4s  %11.11s\n",
		        nam, lev, rar, spd, hp, ac, exp);
	}

	file_putf(fh, "\n");

	mem_free(who);

	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
	} else {
		msg("Successfully created a spoiler file.");
	}
}

/**
 * ------------------------------------------------------------------------
 * Monster spoilers originally by: smchorse@ringer.cs.utsa.edu (Shawn McHorse)
 * ------------------------------------------------------------------------
 */

/**
 * Create a spoiler file for monsters (-SHAWN-)
 */
static void spoil_mon_info(const char *fname)
{

	/* Open the file */
	char file_name[4096];
	path_build(file_name, sizeof(file_name), ANGBAND_DIR_USER, fname);
	fh = file_open(file_name, MODE_WRITE, FTYPE_TEXT);

	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump the header */
	textblock *tb = textblock_new();
	textblock_append(tb, "Monster Spoilers for %s\n", buildid);
	textblock_append(tb, "------------------------------------------\n\n");
	textblock_to_file(tb, fh, 0, 75);
	textblock_free(tb);
	tb = NULL;

	/* Allocate the "who" array */
	u16b *who = mem_zalloc(z_info->r_max * sizeof(*who));

	/* Scan the monsters */
	int n_who = 0;
	for (int i = 1; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];
		if (race->name) {
			who[n_who] = i;
			n_who++;
		}
	}

	sort(who, n_who, sizeof(*who), cmp_monsters);

	/* List all monsters in order. */
	for (int i = 0; i < n_who; i++) {
		int r_idx = who[i];
		const struct monster_race *race = &r_info[r_idx];
		const struct monster_lore *lore = &l_list[r_idx];
		textblock *tb = textblock_new();

		/* Line 1: prefix, name, color, and symbol */
		if (rf_has(race->flags, RF_QUESTOR)) {
			textblock_append(tb, "[Q] ");
		} else if (rf_has(race->flags, RF_UNIQUE)) {
			textblock_append(tb, "[U] ");
		} else {
			textblock_append(tb, "The ");
		}

		/* As of 3.5, race->name and race->text are stored as UTF-8 strings;
		 * there is no conversion from the source edit files. */
		textblock_append_utf8(tb, race->name);
		textblock_append(tb, "  (");	/* ---)--- */
		textblock_append(tb, attr_to_text(race->d_attr));
		textblock_append(tb, " '%c')\n", race->d_char);

		/* Line 2: number, level, rarity, speed, HP, AC, exp */
		textblock_append(tb, "=== ");
		textblock_append(tb, "Num:%d  ", r_idx);
		textblock_append(tb, "Lev:%d  ", race->level);
		textblock_append(tb, "Rar:%d  ", race->rarity);

		if (race->speed >= 110) {
			textblock_append(tb, "Spd:+%d  ", (race->speed - 110));
		} else {
			textblock_append(tb, "Spd:-%d  ", (110 - race->speed));
		}

		textblock_append(tb, "Hp:%d  ", race->avg_hp);
		textblock_append(tb, "Ac:%d  ", race->ac);
		textblock_append(tb, "Exp:%ld\n", (long)(race->mexp));

		/* Normal description (with automatic line breaks) */
		lore_description(tb, race, lore, true);
		textblock_append(tb, "\n");

		textblock_to_file(tb, fh, 0, 75);
		textblock_free(tb);
		tb = NULL;
	}

	mem_free(who);

	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
	} else {
		msg("Successfully created a spoiler file.");
	}
}

static void spoiler_menu_act(const char *title, int index)
{
	(void) title;

	switch (index) {
		case 0: spoil_obj_desc("obj-desc.spo"); break;
		case 1: spoil_artifact("artifact.spo"); break;
		case 2: spoil_mon_desc("mon-desc.spo"); break;
		case 3: spoil_mon_info("mon-info.spo"); break;

		default:
			 break;
	}

	event_signal(EVENT_MESSAGE_FLUSH);
}

static menu_action spoil_actions[] = {
	{0, 0, "Brief Object Info (obj-desc.spo)",   spoiler_menu_act},
	{0, 0, "Brief Artifact Info (artifact.spo)", spoiler_menu_act},
	{0, 0, "Brief Monster Info (mon-desc.spo)",  spoiler_menu_act},
	{0, 0, "Full Monster Info (mon-info.spo)",   spoiler_menu_act},
};

/**
 * Create spoiler files
 */
void do_cmd_spoilers(void)
{
	struct menu *menu = menu_new_action(spoil_actions, N_ELEMENTS(spoil_actions));

	menu->selections = lower_case;
	menu->title = "Create spoilers";

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = 2 + N_ELEMENTS(spoil_actions),
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	menu_select(menu);

	Term_pop();
	menu_free(menu);
}
