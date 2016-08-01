/**
 * \file ui2-knowledge.c
 * \brief Player knowledge functions
 *
 * Copyright (c) 2000-2007 Eytan Zweig, Andrew Doull, Pete Mack.
 * Copyright (c) 2010 Peter Denison, Chris Carr.
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
#include "game-input.h"
#include "grafmode.h"
#include "init.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-info.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-history.h"
#include "store.h"
#include "target.h"
#include "trap.h"
#include "ui2-context.h"
#include "ui2-history.h"
#include "ui2-menu.h"
#include "ui2-mon-list.h"
#include "ui2-mon-lore.h"
#include "ui2-object.h"
#include "ui2-obj-list.h"
#include "ui2-options.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-score.h"
#include "ui2-store.h"
#include "ui2-target.h"
#include "wizard.h"

/**
 * The first part of this file contains the knowledge menus. Generic display
 * routines are followed by sections which implement "subclasses" of the
 * abstract classes represented by member_funcs and group_funcs.
 *
 * After the knowledge menus are various knowledge functions - message review;
 * inventory, equipment, monster and object lists; symbol lookup; and the 
 * "locate" command which scrolls the screen around the current dungeon level.
 */

typedef struct {
	/* Name of this group */
	const char *(*name)(int group);

	/* Compares groups of two object indexes */
	int (*gcomp)(const void *a, const void *b);

	/* Returns group for an index */
	int (*group)(int index);

	/* Summary function for the "object" information. */
	void (*summary)(int group, const int *item_list, int n_items,
			int top, struct loc loc);

	/* Maximum possible item count for this class */
	int maxnum;

	/* Items don't need to be IDed to recognize membership */
	bool easy_know;

} group_funcs;

typedef struct {
	/* Displays an entry at given location, including kill-count and graphics */
	void (*display_member)(int index, bool cursor, struct loc loc, int width);

	/* Displays lore for an index */
	void (*lore)(int index);

	/* Get character attr for index (by address) */
	wchar_t *(*xchar)(int index);

	/* Get color attr for index (by address) */
	uint32_t *(*xattr)(int index);

	/* Returns optional extra prompt */
	const char *(*xtra_prompt)(int index);

	/* Handles optional extra actions */
	void (*xtra_act)(struct keypress key, int index);

	/* Does this kind have visual editing? */
	bool is_visual;

} member_funcs;

/**
 * Helper class for generating joins
 */
struct join {
	int index;
	int group;
};

/**
 * A default group-by
 */
static struct join *default_join;

#define DEFAULT_JOIN_SIZE (sizeof(struct join))

/**
 * ------------------------------------------------------------------------
 * Knowledge menu utilities
 * ------------------------------------------------------------------------
 */

static int default_item_id(int index)
{
	return default_join[index].index;
}

static int default_group_id(int index)
{
	return default_join[index].group;
}

/**
 * Return a specific ordering for the features
 */
static int feat_order(int feat)
{
	struct feature *f = &f_info[feat];

	switch (f->d_char) {
		case L'.':             return 0;
		case L'\'': case L'+': return 1;
		case L'<':  case L'>': return 2;
		case L'#':             return 3;
		case L'*':  case L'%': return 4;
		case L';':  case L':': return 5;

		default:               return 6;
	}
}

static void display_group_member(struct menu *menu, int index,
		bool cursor, struct loc loc, int width)
{
	const member_funcs *o_funcs = menu->menu_data;

	o_funcs->display_member(index, cursor, loc, width);
}

static const char *recall_prompt(int index)
{
	(void) index;

	return ", 'r' to recall";
}

#define SWAP(a, b) do { \
	void *swapspace = (a); \
	(a) = (b); \
	(b) = swapspace; \
} while (0)

static ui_event knowledge_screen_event(struct menu *active_menu)
{
	ui_event in = inkey_simple();
	ui_event out = EVENT_EMPTY;

	if (in.type == EVT_MOUSE) {
		menu_handle_mouse(active_menu, in.mouse, &out);
	} else if (in.type == EVT_KBRD) {
		menu_handle_keypress(active_menu, in.key, &out);
	}

	if (out.type != EVT_NONE) {
		in = out;
	}

	return in;
}

static void knowledge_screen_recall(member_funcs o_funcs, int index)
{
	if (index >= 0) {
		o_funcs.lore(index);
	}
}

static void knowledge_screen_prompt(member_funcs o_funcs, int index)
{
	if (o_funcs.xtra_prompt) {
		struct loc loc = {0, Term_height() - 1};

		prt(format("<dir>%s", o_funcs.xtra_prompt(index)), loc);
	}
}

static void knowledge_screen_summary(group_funcs g_funcs,
		int g_cur, int *o_list, int g_o_count, int top, region reg)
{
	if (g_funcs.summary) {
		struct loc loc = {
			.x = reg.x,
			.y = reg.y + reg.h
		};

		g_funcs.summary(g_cur, o_list, g_o_count, top, loc);
	}
}

static void knowledge_screen_draw(const char *title, const char *other_fields,
		region title_region, int g_name_max_len)
{
	struct loc loc = {0, 0};

	region_erase(title_region);

	loc.y = 2;
	prt(format("Knowledge - %s", title), loc);

	loc.y = 4;
	prt("Group", loc);

	loc.x = g_name_max_len + 3;
	prt("Name", loc);

	if (other_fields) {
		loc.x = 46;
		prt(other_fields, loc);
	}

	/* Print dividers: horizontal and vertical */
	for (int x = 0; x < 80; x++) {
		Term_addwc(x, 5, COLOUR_WHITE, '=');
	}

	for (int y = 6, z = Term_height() - 2; y < z; y++) {
		Term_addwc(g_name_max_len + 1, y, COLOUR_WHITE, '|');
	}
}

static void knowledge_screen_regions(region *title, region *group, region *object,
		int g_name_max_len, bool summary)
{
	title->x = 0;
	title->y = 0;
	title->w = 0;
	title->h = 4;

	group->x = 0;
	group->y = 6;
	group->w = g_name_max_len;
	group->h = -2;

	object->x = group->w + 3;
	object->y = 6;
	object->w = 0;
	object->h = summary ? -3 : -2;
}

static int set_g_lists(int *o_list, int o_count,
		int *g_list, int *g_offset, group_funcs g_funcs)
{
	int g_count = 0;

	for (int i = 0, prev_g = -1; i < o_count; i++) {
		int g = g_funcs.group(o_list[i]);

		if (prev_g != g) {
			g_offset[g_count] = i;
			g_list[g_count] = g;
			prev_g = g;
			g_count++;
		}
	}

	g_offset[g_count] = o_count;
	g_list[g_count] = -1;

	return g_count;
}

static int set_g_names(const int *g_list, int g_count,
		const char **g_names, group_funcs g_funcs)
{
	int group_name_len = 8;

	for (int g = 0; g < g_count; g++) {
		g_names[g] = g_funcs.name(g_list[g]);
		int len = strlen(g_names[g]);
		if (len > group_name_len) {
			group_name_len = len;
		}
	}

	return group_name_len;
}

/**
 * Interactive group by.
 * Recognises inscriptions, graphical symbols, lore
 */
static void display_knowledge(const char *title, int *o_list, int o_count,
		group_funcs g_funcs, member_funcs o_funcs,
		const char *other_fields)
{
	const int max_groups = MIN(g_funcs.maxnum, o_count);
	const int omode = OPT(rogue_like_commands);

	/* Disable the roguelike commands for the duration */
	OPT(rogue_like_commands) = false;

	if (g_funcs.gcomp) {
		sort(o_list, o_count, sizeof(*o_list), g_funcs.gcomp);
	}

	/* Sort everything into group order */
	int *g_list = mem_zalloc((max_groups + 1) * sizeof(*g_list));
	int *g_offset = mem_zalloc((max_groups + 1) * sizeof(*g_offset));
	int g_count = set_g_lists(o_list, o_count, g_list, g_offset, g_funcs);

	/* The compact set of group names, in display order */
	const char **g_names = mem_zalloc(g_count * sizeof(*g_names));
	int g_name_max_len = set_g_names(g_list, g_count, g_names, g_funcs);
	g_name_max_len = MIN(g_name_max_len, 20);

	struct term_hints hints = {
		.width = 80,
		.height = 24,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};
	Term_push_new(&hints);

	region title_region;
	region group_region;
	region object_region;
	knowledge_screen_regions(&title_region, &group_region, &object_region,
			g_name_max_len, g_funcs.summary != NULL);

	struct menu group_menu;
	struct menu object_menu;

	/* Set up the two menus */
	menu_init(&group_menu, MN_SKIN_SCROLL, menu_find_iter(MN_ITER_STRINGS));
	menu_setpriv(&group_menu, g_count, g_names);
	menu_layout(&group_menu, group_region);
	mnflag_on(group_menu.flags, MN_DBL_TAP);

	menu_iter object_iter = {
		.display_row = display_group_member
	};

	menu_init(&object_menu, MN_SKIN_SCROLL, &object_iter);
	menu_setpriv(&object_menu, 0, &o_funcs);
	menu_layout(&object_menu, object_region);
	mnflag_on(object_menu.flags, MN_DBL_TAP);

	/* Currenly selected panel */
	int panel = 0;

	bool swap = false;
	bool stop = false;

	int g_old = -1;    /* old group list position */
	int g_cur = 0;     /* group list position */
	int o_cur = 0;     /* object list position */
	int g_o_count = 0; /* object count for group */

	/* Panel state */
	/* These are swapped in parallel whenever
	 * the actively browsing changes */
	int *active_cursor = &g_cur;
	int *inactive_cursor = &o_cur;
	struct menu *active_menu = &group_menu;
	struct menu *inactive_menu = &object_menu;

	knowledge_screen_draw(title, other_fields, title_region, g_name_max_len);

	while (!stop && g_count) {
		if (g_cur != g_old) {
			g_old = g_cur;
			o_cur = 0;
			g_o_count = g_offset[g_cur + 1] - g_offset[g_cur];
			menu_set_filter(&object_menu, o_list + g_offset[g_cur], g_o_count);
			group_menu.cursor = g_cur;
			object_menu.cursor = o_cur;
		}

		int index = o_list[g_offset[g_cur] + o_cur];

		if (swap) {
			SWAP(active_menu, inactive_menu);
			SWAP(active_cursor, inactive_cursor);
			panel = 1 - panel;
			swap = false;
		}

		menu_refresh(inactive_menu);
		menu_refresh(active_menu);

		knowledge_screen_summary(g_funcs,
				g_cur, o_list, g_o_count, g_offset[g_cur], object_menu.active);
		knowledge_screen_prompt(o_funcs, index);

		ui_event event = knowledge_screen_event(active_menu);

		switch (event.type) {
			case EVT_KBRD:
				if (event.key.code == 'r' || event.key.code == 'R') {
					knowledge_screen_recall(o_funcs, index);
				} else if (o_funcs.xtra_act) {
					o_funcs.xtra_act(event.key, index);
				}
				break;

			case EVT_MOUSE:
				/* Change active panels */
				if (region_inside(&inactive_menu->active, &event.mouse)) {
					SWAP(active_menu, inactive_menu);
					SWAP(active_cursor, inactive_cursor);
					panel = 1 - panel;
				}
				break;

			case EVT_ESCAPE:
				if (panel == 1) {
					swap = true;
				} else {
					stop = true;
				}
				break;

			case EVT_SELECT:
				if (panel == 0) {
					swap = true;
				} else if (panel == 1 && index >= 0 && o_cur == active_menu->cursor) {
					knowledge_screen_recall(o_funcs, index);
				}
				break;

			case EVT_MOVE:
				*active_cursor = active_menu->cursor;
				break;

			default:
				break;
		}
	}

	/* Restore roguelike option */
	OPT(rogue_like_commands) = omode;

	/* Prompt */
	if (!g_count) {
		struct loc loc = {0, 15};
		prt(format("No %s known.", title), loc);
	}

	mem_free(g_names);
	mem_free(g_offset);
	mem_free(g_list);

	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 *  MONSTERS
 * ------------------------------------------------------------------------ */

/**
 * Description of each monster group.
 */
static struct
{
	const wchar_t *chars;
	const char *name;
} monster_group[] = {
	{ (const wchar_t *) -1,   "Uniques" },
	{ L"A",        "Ainur" },
	{ L"a",        "Ants" },
	{ L"b",        "Bats" },
	{ L"B",        "Birds" },
	{ L"C",        "Canines" },
	{ L"c",        "Centipedes" },
	{ L"uU",       "Demons" },
	{ L"dD",       "Dragons" },
	{ L"vE",       "Elementals/Vortices" },
	{ L"e",        "Eyes/Beholders" },
	{ L"f",        "Felines" },
	{ L"G",        "Ghosts" },
	{ L"OP",       "Giants/Ogres" },
	{ L"g",        "Golems" },
	{ L"H",        "Harpies/Hybrids" },
	{ L"h",        "Hominids (Elves, Dwarves)" },
	{ L"M",        "Hydras" },
	{ L"i",        "Icky Things" },
	{ L"lFI",      "Insects" },
	{ L"j",        "Jellies" },
	{ L"K",        "Killer Beetles" },
	{ L"k",        "Kobolds" },
	{ L"L",        "Lichs" },
	{ L"tp",       "Men" },
	{ L".$!?=~_",  "Mimics" },
	{ L"m",        "Molds" },
	{ L",",        "Mushroom Patches" },
	{ L"n",        "Nagas" },
	{ L"o",        "Orcs" },
	{ L"q",        "Quadrupeds" },
	{ L"Q",        "Quylthulgs" },
	{ L"R",        "Reptiles/Amphibians" },
	{ L"r",        "Rodents" },
	{ L"S",        "Scorpions/Spiders" },
	{ L"s",        "Skeletons/Drujs" },
	{ L"J",        "Snakes" },
	{ L"T",        "Trolls" },
	{ L"V",        "Vampires" },
	{ L"W",        "Wights/Wraiths" },
	{ L"w",        "Worms/Worm Masses" },
	{ L"X",        "Xorns/Xarens" },
	{ L"y",        "Yeeks" },
	{ L"Y",        "Yeti" },
	{ L"Z",        "Zephyr Hounds" },
	{ L"z",        "Zombies" },
	{ NULL,        NULL }
};

/**
 * Display a monster
 */
static void display_monster(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	/* Get the race index. */
	int r_idx = default_item_id(index);

	/* Access the race */
	struct monster_race *race = &r_info[r_idx];
	struct monster_lore *lore = &l_list[r_idx];

	c_prt(menu_row_style(true, cursor), race->name, loc);

	wchar_t xc = monster_x_char[race->ridx];
	uint32_t xa = (OPT(purple_uniques) && rf_has(race->flags, RF_UNIQUE)) ?
		COLOUR_VIOLET : monster_x_attr[race->ridx];

	/* Display monster symbol */
	Term_addwc(66, loc.y, xa, xc);

	loc.x = 70;
	if (rf_has(race->flags, RF_UNIQUE)) {
		put_str(format("%s", (race->max_num == 0)?  " dead" : " alive"), loc);
	} else {
		put_str(format("%5d", lore->pkills), loc);
	}
}

static int m_cmp_race(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;
	int groupa = default_group_id(a_val);
	int groupb = default_group_id(b_val);

	int c = groupa - groupb;
	if (c != 0) {
		return c;
	}

	/* Same group */

	const struct monster_race *racea = &r_info[default_item_id(a_val)];
	const struct monster_race *raceb = &r_info[default_item_id(b_val)];

	if (groupa != 0 && racea->d_char != raceb->d_char) {
		/* Non-uniques are sorted by order they appear in the group symbols */
		return wcschr(monster_group[groupa].chars, racea->d_char) -
			wcschr(monster_group[groupa].chars, raceb->d_char);
	}

	/* Others are ordered by level and name */
	c = racea->level - raceb->level;
	if (c != 0) {
		return c;
	}

	return strcmp(racea->name, raceb->name);
}

static wchar_t *m_xchar(int index)
{
	return &monster_x_char[default_join[index].index];
}

static uint32_t *m_xattr(int index)
{
	return &monster_x_attr[default_join[index].index];
}

static const char *race_name(int group)
{
	return monster_group[group].name;
}

static void mon_lore(int index)
{
	int r_idx = default_item_id(index);

	assert(r_idx != 0);

	struct monster_race *race = &r_info[r_idx];
	const struct monster_lore *lore = get_lore(race);

	/* Update the monster recall window */
	monster_race_track(player->upkeep, race);
	handle_stuff(player);

	textblock *tb = textblock_new();
	lore_description(tb, race, lore, false);
	region reg = {0};
	textui_textblock_show(tb, reg, NULL);
	textblock_free(tb);
}

static void mon_summary(int group, const int *item_list,
		int n_items, int top, struct loc loc)
{
	int kills = 0;

	/* Access the race */
	for (int i = top, end = top + n_items; i < end; i++) {
		int index = default_join[item_list[i]].index;

		kills += l_list[index].pkills;
	}

	/* Different display for the first item if we've got uniques to show */
	if (group == 0
			&& rf_has((r_info[default_join[item_list[top]].index]).flags,
				RF_UNIQUE))
	{
		c_prt(COLOUR_L_BLUE, format("%d known uniques, %d slain.", n_items, kills), loc);
	} else {
		int tkills = 0;

		for (int i = 0; i < z_info->r_max; i++) {
			tkills += l_list[i].pkills;
		}

		c_prt(COLOUR_L_BLUE,
				format("Creatures slain: %d/%d (in group/in total)", kills, tkills),
				loc);
	}
}

static int count_known_monsters(void)
{
	int m_count = 0;

	for (int i = 0; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];
		if (!OPT(cheat_know) && !l_list[i].all_known && !l_list[i].sights) {
			continue;
		}
		if (!race->name) {
			continue;
		}

		if (rf_has(race->flags, RF_UNIQUE)) {
			m_count++;
		}

		for (size_t g = 1; g < N_ELEMENTS(monster_group) - 1; g++) {
			if (wcschr(monster_group[g].chars, race->d_char)) {
				m_count++;
			}
		}
	}

	return m_count;
}

/**
 * Display known monsters.
 */
static void do_cmd_knowledge_monsters(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs r_funcs = {
		.name = race_name,
		.gcomp = m_cmp_race, 
		.group = default_group_id,
		.summary = mon_summary,
		.maxnum = N_ELEMENTS(monster_group),
		.easy_know = false
	};

	member_funcs m_funcs = {
		.display_member = display_monster,
		.lore = mon_lore,
		.xchar = m_xchar,
		.xattr = m_xattr,
		.xtra_prompt = recall_prompt,
		.xtra_act = NULL,
		.is_visual = false
	};

	int m_count = count_known_monsters();

	default_join = mem_zalloc(m_count * DEFAULT_JOIN_SIZE);
	int *monsters = mem_zalloc(m_count * sizeof(*monsters));

	m_count = 0;

	for (int i = 0; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];

		if (!OPT(cheat_know) && !l_list[i].all_known && !l_list[i].sights) {
			continue;
		}
		if (!race->name) {
			continue;
		}

		for (size_t g = 0; g < N_ELEMENTS(monster_group) - 1; g++) {

			if ((g == 0 && rf_has(race->flags, RF_UNIQUE))
					|| (g > 0 && wcschr(monster_group[g].chars, race->d_char)))
			{
				monsters[m_count] = m_count;

				default_join[m_count].index = i;
				default_join[m_count].group = g;

				m_count++;
			}
		}
	}

	display_knowledge("monsters", monsters, m_count,
			r_funcs, m_funcs,
			"                   Sym  Kills");

	mem_free(default_join);
	mem_free(monsters);
}

/**
 * ------------------------------------------------------------------------
 *  ARTIFACTS
 * ------------------------------------------------------------------------ */

/**
 * These are used for all the object sections
 */
static const grouper object_text_order[] = {
	{TV_RING,			"Ring"			},
	{TV_AMULET,			"Amulet"		},
	{TV_POTION,			"Potion"		},
	{TV_SCROLL,			"Scroll"		},
	{TV_WAND,			"Wand"			},
	{TV_STAFF,			"Staff"			},
	{TV_ROD,			"Rod"			},
 	{TV_FOOD,			"Food"			},
 	{TV_MUSHROOM,		"Mushroom"		},
	{TV_PRAYER_BOOK,	"Priest Book"	},
	{TV_MAGIC_BOOK,		"Magic Book"	},
	{TV_LIGHT,			"Light"			},
	{TV_FLASK,			"Flask"			},
	{TV_SWORD,			"Sword"			},
	{TV_POLEARM,		"Polearm"		},
	{TV_HAFTED,			"Hafted Weapon" },
	{TV_BOW,			"Bow"			},
	{TV_ARROW,			"Ammunition"	},
	{TV_BOLT,			NULL			},
	{TV_SHOT,			NULL			},
	{TV_SHIELD,			"Shield"		},
	{TV_CROWN,			"Crown"			},
	{TV_HELM,			"Helm"			},
	{TV_GLOVES,			"Gloves"		},
	{TV_BOOTS,			"Boots"			},
	{TV_CLOAK,			"Cloak"			},
	{TV_DRAG_ARMOR,		"Dragon Scale Mail" },
	{TV_HARD_ARMOR,		"Hard Armor"	},
	{TV_SOFT_ARMOR,		"Soft Armor"	},
	{TV_DIGGING,		"Digger"		},
	{TV_GOLD,			"Money"			},
	{0,					NULL			}
};

static int *obj_group_order = NULL;

static void get_artifact_display_name(char *o_name, size_t size, int a_idx)
{
	struct object body = OBJECT_NULL;
	struct object known_body = OBJECT_NULL;

	struct object *obj = &body;
	struct object *known_obj = &known_body;

	make_fake_artifact(obj, &a_info[a_idx]);
	object_wipe(known_obj);
	object_copy(known_obj, obj);
	obj->known = known_obj;
	object_desc(o_name, size, obj, ODESC_PREFIX | ODESC_BASE | ODESC_SPOIL);
	object_wipe(known_obj);
	object_wipe(obj);
}

/**
 * Display an artifact label
 */
static void display_artifact(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	char o_name[80];
	get_artifact_display_name(o_name, sizeof(o_name), index);

	uint32_t attr = menu_row_style(true, cursor);

	c_prt(attr, o_name, loc);
}

/**
 * Look for an artifact
 */
static struct object *find_artifact(struct artifact *artifact)
{
	/* Ground objects */
	for (int y = 1; y < cave->height; y++) {
		for (int x = 1; x < cave->width; x++) {
			for (struct object *obj = square_object(cave, y, x); obj; obj = obj->next) {
				if (obj->artifact == artifact) {
					return obj;
				}
			}
		}
	}

	/* Player objects */
	for (struct object *obj = player->gear; obj; obj = obj->next) {
		if (obj->artifact == artifact) {
			return obj;
		}
	}

	/* Monster objects */
	for (int i = cave_monster_max(cave) - 1; i >= 1; i--) {
		struct monster *mon = cave_monster(cave, i);

		if (mon) {
			for (struct object *obj =  mon->held_obj; obj; obj = obj->next) {
				if (obj->artifact == artifact) {
					return obj;
				}
			}
		}
	}

	/* Store objects */
	for (int i = 0; i < MAX_STORES; i++) {
		struct store *s = &stores[i];
		for (struct object *obj = s->stock; obj; obj = obj->next) {
			if (obj->artifact == artifact) {
				return obj;
			}
		}
	}

	return NULL;
}

/**
 * Show artifact lore
 */
static void desc_art_fake(int a_idx)
{
	struct object *obj = find_artifact(&a_info[a_idx]);
	struct object *known_obj = NULL;

	struct object object_body = OBJECT_NULL;
	struct object known_object_body = OBJECT_NULL;

	bool fake = false;

	/* If it's been lost, make a fake artifact for it */
	if (obj == NULL) {
		fake = true;
		obj = &object_body;
		known_obj = &known_object_body;

		make_fake_artifact(obj, &a_info[a_idx]);
		obj->known = known_obj;
		known_obj->artifact = obj->artifact;
		known_obj->kind = obj->kind;

		/* Check the history entry, to see if it was fully known
		 * before it was lost */
		if (history_is_artifact_known(obj->artifact)) {
			/* Be very careful not to influence anything but this object */
			object_copy(known_obj, obj);
		}
	}

	textblock *tb = object_info(obj, OINFO_NONE);

	char header[80];
	object_desc(header, sizeof(header), obj,
			ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL);

	if (fake) {
		object_wipe(obj);
		object_wipe(known_obj);
	}

	region area = {0, 0, 0, 0};
	textui_textblock_show(tb, area, header);
	textblock_free(tb);
}

static int a_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;

	const struct artifact *aa = &a_info[a_val];
	const struct artifact *ab = &a_info[b_val];

	/* Group by */
	int ta = obj_group_order[aa->tval];
	int tb = obj_group_order[ab->tval];
	int c = ta - tb;
	if (c != 0) {
		return c;
	}

	/* Order by */
	c = aa->sval - ab->sval;
	if (c != 0) {
		return c;
	}

	return strcmp(aa->name, ab->name);
}

static const char *kind_name(int group)
{
	return object_text_order[group].name;
}

static int art2gid(int index)
{
	return obj_group_order[a_info[index].tval];
}

/**
 * Check if the given artifact idx is something we should "Know" about
 */
static bool artifact_is_known(int a_idx)
{
	if (!a_info[a_idx].name) {
		return false;
	}

	if (player->wizard) {
		return true;
	}

	if (!a_info[a_idx].created) {
		return false;
	}

	/* Check all objects to see if it exists but hasn't been IDed */
	struct object *obj = find_artifact(&a_info[a_idx]);

	if (obj && !object_is_known_artifact(obj)) {
		return false;
	}

	return true;
}

/**
 * If 'artifacts' is NULL, it counts the number of known artifacts, otherwise
 * it collects the list of known artifacts into 'artifacts' as well.
 */
static int collect_known_artifacts(int *artifacts, size_t artifacts_len)
{
	if (artifacts) {
		assert(artifacts_len >= z_info->a_max);
	}

	int a_count = 0;

	for (int i = 0; i < z_info->a_max; i++) {
		if (a_info[i].name) {
			if (OPT(cheat_xtra) || artifact_is_known(i)) {
				if (artifacts) {
					artifacts[a_count] = i;
				}
				a_count++;
			}
		}
	}

	return a_count;
}

/**
 * Display known artifacts
 */
static void do_cmd_knowledge_artifacts(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs obj_f = {
		.name = kind_name, 
		.gcomp = a_cmp_tval, 
		.group = art2gid, 
		.summary = NULL, 
		.maxnum = TV_MAX, 
		.easy_know = false
	};

	member_funcs art_f = {
		.display_member = display_artifact,
		.lore = desc_art_fake,
		.xchar = NULL, 
		.xattr = NULL,
		.xtra_prompt = recall_prompt, 
		.xtra_act = NULL,
		.is_visual = false
	};

	int *artifacts = mem_zalloc(z_info->a_max * sizeof(int));

	/* Collect valid artifacts */
	int a_count = collect_known_artifacts(artifacts, z_info->a_max);

	display_knowledge("artifacts", artifacts, a_count, obj_f, art_f, NULL);
	mem_free(artifacts);
}

/**
 * ------------------------------------------------------------------------
 *  EGO ITEMS
 * ------------------------------------------------------------------------ */

static const char *ego_group_name(int group)
{
	return object_text_order[group].name;
}

static void display_ego_item(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	/* Access the object */
	struct ego_item *ego = &e_info[default_item_id(index)];

	/* Choose a color */
	uint32_t attr = menu_row_style(ego->everseen, cursor);

	/* Display the name */
	c_prt(attr, ego->name, loc);
}

/**
 * Describe fake ego item "lore"
 */
static void desc_ego_fake(int index)
{
	int e_idx = default_item_id(index);
	struct ego_item *ego = &e_info[e_idx];


	/* List ego flags */
	textblock *tb = object_info_ego(ego);

	region area = {0, 0, 0, 0};
	textui_textblock_show(tb, area,
			format("%s %s", ego_group_name(default_group_id(index)), ego->name));
	textblock_free(tb);
}

/* TODO? Currently ego items will order by e_idx */
static int e_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;
	const struct ego_item *ea = &e_info[default_item_id(a_val)];
	const struct ego_item *eb = &e_info[default_item_id(b_val)];

	/* Order by group */
	int c = default_group_id(a_val) - default_group_id(b_val);
	if (c != 0) {
		return c;
	}

	/* Order by name */
	return strcmp(ea->name, eb->name);
}

/**
 * Display known ego_items
 */
static void do_cmd_knowledge_ego_items(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs obj_f = {
		.name = ego_group_name, 
		.gcomp = e_cmp_tval, 
		.group = default_group_id, 
		.summary = NULL, 
		.maxnum = TV_MAX, 
		.easy_know = false
	};

	member_funcs ego_f = {
		.display_member = display_ego_item, 
		.lore = desc_ego_fake, 
		.xchar = NULL,
		.xattr = NULL,
		.xtra_prompt = recall_prompt,
		.xtra_act = NULL,
		.is_visual = false
	};

	int e_count = 0;

	int max_pairs = z_info->e_max * N_ELEMENTS(object_text_order);
	int *egoitems = mem_zalloc(max_pairs * sizeof(*egoitems));
	default_join = mem_zalloc(max_pairs * DEFAULT_JOIN_SIZE);

	/* Look at all the ego items */
	for (int i = 0; i < z_info->e_max; i++)	{
		struct ego_item *ego = &e_info[i];

		if (ego->everseen || OPT(cheat_xtra)) {
			int tval[N_ELEMENTS(object_text_order)] = {0};

			/* Note the tvals which are possible for this ego */
			for (struct ego_poss_item *poss = ego->poss_items; poss; poss = poss->next) {
				struct object_kind *kind = &k_info[poss->kidx];
				tval[obj_group_order[kind->tval]]++;
			}

			/* Count and put into the list */
			for (size_t g = 0; g < TV_MAX; g++) {
				int group = obj_group_order[g];

				/* Ignore duplicates */
				if (g > 0 && e_count > 0
						&& group == default_join[e_count - 1].group
						&& i == default_join[e_count - 1].index)
				{
					continue;
				}

				if (tval[group]) {
					egoitems[e_count] = e_count;
					default_join[e_count].index = i;
					default_join[e_count].group = group;
					e_count++;
				}
			}
		}
	}

	display_knowledge("ego items", egoitems, e_count, obj_f, ego_f, NULL);

	mem_free(default_join);
	mem_free(egoitems);
}

/**
 * ------------------------------------------------------------------------
 * ORDINARY OBJECTS
 * ------------------------------------------------------------------------ */

/**
 * Looks up an artifact idx given an object_kind *that's already known
 * to be an artifact*.  Behaviour is distinctly unfriendly if passed
 * flavours which don't correspond to an artifact.
 */
static int get_artifact_from_kind(struct object_kind *kind)
{
	assert(kf_has(kind->kind_flags, KF_INSTA_ART));

	int a = 0;

	/* Look for the corresponding artifact */
	while (a < z_info->a_max
			&& (kind->tval != a_info[a].tval || kind->sval != a_info[a].sval))
	{
		a++;
	}

	assert(a < z_info->a_max);

	return a;
}

/**
 * Display the objects in a group.
 */
static void display_object(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct object_kind *kind = &k_info[index];
	const char *inscrip = get_autoinscription(kind, kind->aware);

	/* Choose a color */
	bool aware = (!kind->flavor || kind->aware);
	uint32_t attr = menu_row_style(aware, cursor);

	char o_name[80];
	/* Display known artifacts differently */
	if (kf_has(kind->kind_flags, KF_INSTA_ART)
			&& artifact_is_known(get_artifact_from_kind(kind)))
	{
		get_artifact_display_name(o_name, sizeof(o_name), get_artifact_from_kind(kind));
	} else {
 		object_kind_name(o_name, sizeof(o_name), kind, OPT(cheat_xtra));
	}

	/* If the type is "tried", display that */
	if (kind->tried && !aware) {
		my_strcat(o_name, " {tried}", sizeof(o_name));
	}

	/* Display the name */
	c_prt(attr, o_name, loc);

	/* Show ignore status */
	if ((aware && kind_is_ignored_aware(kind))
			|| (!aware && kind_is_ignored_unaware(kind)))
	{
		loc.x = 46;
		c_put_str(attr, "Yes", loc);
	}

	/* Show autoinscription if around */
	if (inscrip) {
		loc.x = 55;
		c_put_str(COLOUR_YELLOW, inscrip, loc);
	}

	/* Graphics versions of the object_char and object_attr defines */
	Term_addwc(76, loc.y, object_kind_attr(kind), object_kind_char(kind));
}

/**
 * Describe fake object
 */
static void desc_obj_fake(int k_idx)
{
	struct object_kind *kind = &k_info[k_idx];
	struct object_kind *old_kind = player->upkeep->object_kind;
	struct object *old_obj = player->upkeep->object;
	struct object *obj = object_new();
	struct object *known_obj = object_new();

	/* Check for known artifacts, display them as artifacts */
	if (kf_has(kind->kind_flags, KF_INSTA_ART)
			&& artifact_is_known(get_artifact_from_kind(kind)))
	{
		desc_art_fake(get_artifact_from_kind(kind));
		return;
	}

	/* Update the object recall window */
	track_object_kind(player->upkeep, kind);
	handle_stuff(player);

	/* Create the artifact */
	object_prep(obj, kind, 0, EXTREMIFY);

	/* It's fully known */
	if (kind->aware || !kind->flavor) {
		object_copy(known_obj, obj);
	}
	obj->known = known_obj;

	char header[80];
	object_desc(header, sizeof(header), obj, ODESC_PREFIX | ODESC_CAPITAL);

	textblock *tb = object_info(obj, OINFO_FAKE);
	region area = {0, 0, 0, 0};
	textui_textblock_show(tb, area, header);
	object_delete(&known_obj);
	object_delete(&obj);
	textblock_free(tb);

	/* Restore the old trackee */
	if (old_kind) {
		track_object_kind(player->upkeep, old_kind);
	} else if (old_obj) {
		track_object(player->upkeep, old_obj);
	} else {
		track_object_cancel(player->upkeep);
	}
}

static int o_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;
	const struct object_kind *ka = &k_info[a_val];
	const struct object_kind *kb = &k_info[b_val];

	/* Group by */
	int ta = obj_group_order[ka->tval];
	int tb = obj_group_order[kb->tval];
	int c = ta - tb;
	if (c!= 0) {
		return c;
	}

	/* Order by */
	c = ka->aware - kb->aware;
	if (c != 0) {
		return -c; /* aware has low sort weight */
	}

	switch (ka->tval) {
		case TV_LIGHT:
		case TV_MAGIC_BOOK:
		case TV_PRAYER_BOOK:
		case TV_DRAG_ARMOR:
			/* leave sorted by sval */
			return ka->sval - kb->sval;

		default:
			if (ka->aware) {
				return strcmp(ka->name, kb->name);
			}

			/* Then in tried order */
			c = ka->tried - kb->tried;
			if (c != 0) {
				return -c;
			}

			return strcmp(ka->flavor->text, kb->flavor->text);
	}
}

static int obj2gid(int index)
{
	return obj_group_order[k_info[index].tval];
}

static wchar_t *o_xchar(int index)
{
	struct object_kind *kind = objkind_byid(index);

	if (!kind->flavor || kind->aware) {
		return &kind_x_char[kind->kidx];
	} else {
		return &flavor_x_char[kind->flavor->fidx];
	}
}

static uint32_t *o_xattr(int index)
{
	struct object_kind *kind = objkind_byid(index);

	if (!kind->flavor || kind->aware) {
		return &kind_x_attr[kind->kidx];
	} else {
		return &flavor_x_attr[kind->flavor->fidx];
	}
}

/**
 * Display special prompt for object inscription.
 */
static const char *o_xtra_prompt(int index)
{
	struct object_kind *kind = objkind_byid(index);

	const char *no_insc = ", 's' to toggle ignore, 'r'ecall, '{'";
	const char *with_insc = ", 's' to toggle ignore, 'r'ecall, '{', '}'";

	/* Appropriate prompt */
	if (kind->aware) {
		return kind->note_aware ? with_insc : no_insc;
	} else {
		return kind->note_unaware ? with_insc : no_insc;
	}
}

/**
 * Special key actions for object inscription.
 */
static void o_xtra_act(struct keypress key, int index)
{
	struct object_kind *kind = objkind_byid(index);

	/* Toggle ignore */
	if (ignore_tval(kind->tval) && (key.code == 's' || key.code == 'S')) {
		if (kind->aware) {
			if (kind_is_ignored_aware(kind)) {
				kind_ignore_clear(kind);
			} else {
				kind_ignore_when_aware(kind);
			}
		} else {
			if (kind_is_ignored_unaware(kind)) {
				kind_ignore_clear(kind);
			} else {
				kind_ignore_when_unaware(kind);
			}
		}

		return;
	}

	/* Uninscribe */
	if (key.code == '}') {
		remove_autoinscription(index);
	} else if (key.code == '{') {
		char buf[80] = {0};

		show_prompt("Inscribe with: ");

		/* Default note */
		if (kind->note_aware || kind->note_unaware) {
			strnfmt(buf, sizeof(buf), "%s", get_autoinscription(kind, kind->aware));
		}

		/* Get an inscription */
		if (askfor_aux(buf, sizeof(buf), NULL)) {
			/* Remove old inscription if existent */
			if (kind->note_aware || kind->note_unaware) {
				remove_autoinscription(index);
			}

			/* Add the autoinscription */
			add_autoinscription(index, buf, kind->aware);
			cmdq_push(CMD_AUTOINSCRIBE);

			/* Redraw gear */
			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
		}

		clear_prompt();
	}
}

/**
 * Display known objects
 */
void textui_browse_object_knowledge(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs kind_f = {
		.name = kind_name,
		.gcomp = o_cmp_tval,
		.group = obj2gid,
		.summary = NULL,
		.maxnum = TV_MAX,
		.easy_know = false
	};

	member_funcs obj_f = {
		.display_member = display_object,
		.lore = desc_obj_fake,
		.xchar = o_xchar,
		.xattr = o_xattr,
		.xtra_prompt = o_xtra_prompt,
		.xtra_act = o_xtra_act,
		.is_visual = false
	};

	int o_count = 0;

	int *objects = mem_zalloc(z_info->k_max * sizeof(*objects));

	for (int i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];
		/* It's in the list if we've ever seen it, or it has a flavour,
		 * and either it's not one of the special artifacts, or if it is,
		 * we're not aware of it yet. This way the flavour appears in the list
		 * until it is found. */
		if ((kind->everseen || kind->flavor || OPT(cheat_xtra))
				&& (!kf_has(kind->kind_flags, KF_INSTA_ART)
					|| !artifact_is_known(get_artifact_from_kind(kind))))
		{
			int c = obj_group_order[k_info[i].tval];
			if (c >= 0) {
				objects[o_count] = i;
				o_count++;
			}
		}
	}

	display_knowledge("known objects", objects, o_count, kind_f, obj_f,
			"Ignore  Inscribed          Sym");

	mem_free(objects);
}

/**
 * ------------------------------------------------------------------------
 * OBJECT RUNES
 * ------------------------------------------------------------------------ */

/**
 * Description of each rune group.
 */
static const char *rune_group_text[] = {
	"Combat",
	"Modifiers",
	"Resists",
	"Brands",
	"Slays",
	"Other",
	NULL
};

/**
 * Display the runes in a group.
 */
static void display_rune(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	uint32_t attr = menu_row_style(true, cursor);
	const char *inscrip = quark_str(rune_note(index));

	c_prt(attr, rune_name(index), loc);

	/* Show autoinscription if around */
	if (inscrip) {
		loc.x = 47;
		c_put_str(COLOUR_YELLOW, inscrip, loc);
	}
}

static const char *rune_var_name(int group)
{
	return rune_group_text[group];
}

static int rune_var(int index)
{
	return rune_variety(index);
}

static void rune_lore(int index)
{
	textblock *tb = textblock_new();
	char *title = string_make(rune_name(index));

	my_strcap(title);
	textblock_append_c(tb, COLOUR_L_BLUE, title);
	textblock_append(tb, "\n");
	textblock_append(tb, rune_desc(index));
	textblock_append(tb, "\n");
	region reg = {0, 0, 0, 0};
	textui_textblock_show(tb, reg, NULL);

	textblock_free(tb);
	string_free(title);
}

/**
 * Display special prompt for rune inscription.
 */
static const char *rune_xtra_prompt(int index)
{
	const char *no_insc = ", 'r'ecall, '{'";
	const char *with_insc = ", 'r'ecall, '{', '}'";

	/* Appropriate prompt */
	return rune_note(index) ? with_insc : no_insc;
}

/**
 * Special key actions for rune inscription.
 */
static void rune_xtra_act(struct keypress key, int index)
{
	/* Uninscribe */
	if (key.code == '}') {
		rune_set_note(index, NULL);
	} else if (key.code == '{') {
		/* Inscribe */
		char note_text[80] = "";

		show_prompt("Inscribe with: ");

		/* Default note */
		if (rune_note(index)) {
			strnfmt(note_text, sizeof(note_text), "%s", quark_str(rune_note(index)));
		}

		/* Get an inscription */
		if (askfor_aux(note_text, sizeof(note_text), NULL)) {
			/* Remove old inscription if existent */
			if (rune_note(index)) {
				rune_set_note(index, NULL);
			}

			/* Add the autoinscription */
			rune_set_note(index, note_text);
			rune_autoinscribe(index);

			/* Redraw gear */
			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
		}

		clear_prompt();
	}
}

/**
 * Display rune knowledge.
 */
static void do_cmd_knowledge_runes(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs rune_var_f = {
		.name = rune_var_name, 
		.gcomp = NULL,
		.group = rune_var,
		.summary = NULL,
		.maxnum = N_ELEMENTS(rune_group_text),
		.easy_know = false
	};

	member_funcs rune_f = {
		.display_member = display_rune,
		.lore = rune_lore,
		.xchar = NULL,
		.xattr = NULL,
		.xtra_prompt = rune_xtra_prompt,
		.xtra_act = rune_xtra_act,
		.is_visual = false
	};

	int rune_count = 0;

	int rune_max = max_runes();
	int *runes = mem_zalloc(rune_max * sizeof(*runes));

	for (int i = 0; i < rune_max; i++) {
		/* Ignore unknown runes */
		if (player_knows_rune(player, i)) {
			runes[rune_count] = i;
			rune_count++;
		}
	}

	display_knowledge("runes", runes, rune_count, rune_var_f, rune_f, "Inscribed");
	mem_free(runes);
}

/**
 * ------------------------------------------------------------------------
 * TERRAIN FEATURES
 * ------------------------------------------------------------------------ */

/**
 * Description of each feature group.
 */
static const char *feature_group_text[] = {
	"Floors",
	"Doors",
	"Stairs",
	"Walls",
	"Streamers",
	"Obstructions",
	"Stores",
	"Other",
	NULL
};

/**
 * Display the features in a group.
 */
static void display_feature(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct feature *feat = &f_info[index];
	uint32_t attr = menu_row_style(true, cursor);

	c_prt(attr, feat->name, loc);

	/* Display symbols */
	int x = 65;
	int y = loc.y;
	Term_addwc(x, y,
	   feat_x_attr[LIGHTING_DARK][feat->fidx],
	   feat_x_char[LIGHTING_DARK][feat->fidx]);
	x++;

	Term_addwc(x, y,
	   feat_x_attr[LIGHTING_LIT][feat->fidx],
	   feat_x_char[LIGHTING_LIT][feat->fidx]);
	x++;

	Term_addwc(x, y,
	   feat_x_attr[LIGHTING_TORCH][feat->fidx],
	   feat_x_char[LIGHTING_TORCH][feat->fidx]);
	x++;

	Term_addwc(x, y,
			feat_x_attr[LIGHTING_LOS][feat->fidx],
			feat_x_char[LIGHTING_LOS][feat->fidx]);
}

static int f_cmp_fkind(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;
	const struct feature *fa = &f_info[a_val];
	const struct feature *fb = &f_info[b_val];

	int c = feat_order(a_val) - feat_order(b_val);
	if (c != 0) {
		return c;
	}

	return strcmp(fa->name, fb->name);
}

static const char *fkind_name(int group)
{
	return feature_group_text[group];
}

/**
 * Disgusting hack to allow 4 in 1 editing of terrain visuals
 */
static enum grid_light_level f_uik_lighting = LIGHTING_LIT;

static uint32_t *f_xattr(int index)
{
	return &feat_x_attr[f_uik_lighting][index];
}

static wchar_t *f_xchar(int index)
{
	return &feat_x_char[f_uik_lighting][index];
}

static void feat_lore(int index)
{
	struct feature *feat = &f_info[index];
	textblock *tb = textblock_new();
	char *title = string_make(feat->name);

	if (feat->desc) {
		my_strcap(title);
		textblock_append_c(tb, COLOUR_L_BLUE, title);
		textblock_append(tb, "\n");
		textblock_append(tb, feat->desc);
		textblock_append(tb, "\n");
		region reg = {0, 0, 0, 0};
		textui_textblock_show(tb, reg, NULL);
		textblock_free(tb);
	}

	string_free(title);
}

static const char *feat_prompt(int index)
{
	(void) index;

	return ", 'l' to cycle lighting";
}

/**
 * Special key actions for cycling lighting
 */
static void f_xtra_act(struct keypress key, int index)
{
	(void) index;

	if (key.code == 'l') {
		switch (f_uik_lighting) {
				case LIGHTING_LIT:   f_uik_lighting = LIGHTING_TORCH; break;
                case LIGHTING_TORCH: f_uik_lighting = LIGHTING_LOS;   break;
				case LIGHTING_LOS:   f_uik_lighting = LIGHTING_DARK;  break;
				default:             f_uik_lighting = LIGHTING_LIT;   break;
		}		
	} else if (key.code == 'L') {
		switch (f_uik_lighting) {
				case LIGHTING_DARK: f_uik_lighting = LIGHTING_LOS;   break;
                case LIGHTING_LOS:  f_uik_lighting = LIGHTING_TORCH; break;
				case LIGHTING_LIT:  f_uik_lighting = LIGHTING_DARK;  break;
				default:            f_uik_lighting = LIGHTING_LIT;   break;
		}
	}
}

/**
 * Interact with feature visuals.
 */
static void do_cmd_knowledge_features(const char *name, int row)
{
	(void) name;
	(void) row;

	group_funcs fkind_f = {
		.name = fkind_name,
		.gcomp = f_cmp_fkind,
		.group = feat_order,
		.summary = NULL,
		.maxnum = N_ELEMENTS(feature_group_text),
		.easy_know = false
	};

	member_funcs feat_f = {
		.display_member = display_feature,
		.lore = feat_lore,
		.xchar = f_xchar,
		.xattr = f_xattr,
		.xtra_prompt = feat_prompt,
		.xtra_act = f_xtra_act,
		.is_visual = false
	};

	int f_count = 0;

	int *features = mem_zalloc(z_info->f_max * sizeof(*features));

	for (int i = 0; i < z_info->f_max; i++) {
		/* Ignore non-features and mimics */
		if (f_info[i].name == 0 || f_info[i].mimic != i) {
			continue;
		}

		/* Currently no filter for features */
		features[f_count] = i;
		f_count++;
	}

	display_knowledge("features", features, f_count, fkind_f, feat_f,
			"                    Sym");
	mem_free(features);
}

/**
 * ------------------------------------------------------------------------
 * TRAPS
 * ------------------------------------------------------------------------ */

/**
 * Description of each feature group.
 */
static const char *trap_group_text[] = {
	"Runes",
	"Locks",
	"Traps",
	"Other",
	NULL
};

/**
 * Display the features in a group.
 */
static void display_trap(int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct trap_kind *trap = &trap_info[index];
	uint32_t attr = menu_row_style(true, cursor);

	c_prt(attr, trap->desc, loc);

	/* Display symbols */
	int x = 65;
	int y = loc.y;
	Term_addwc(x, y,
			trap_x_attr[LIGHTING_DARK][trap->tidx],
			trap_x_char[LIGHTING_DARK][trap->tidx]);
	x++;

	Term_addwc(x, y,
			trap_x_attr[LIGHTING_LIT][trap->tidx],
			trap_x_char[LIGHTING_LIT][trap->tidx]);
	x++;

	Term_addwc(x, y,
			trap_x_attr[LIGHTING_TORCH][trap->tidx],
			trap_x_char[LIGHTING_TORCH][trap->tidx]);
	x++;

	Term_addwc(x, y,
			trap_x_attr[LIGHTING_LOS][trap->tidx],
			trap_x_char[LIGHTING_LOS][trap->tidx]);
}

static int trap_order(int trap)
{
	const struct trap_kind *t = &trap_info[trap];

	if (trf_has(t->flags, TRF_RUNE)) {
		return 0;
	} else if (trf_has(t->flags, TRF_LOCK)) {
		return 1;
	} else if (trf_has(t->flags, TRF_TRAP)) {
		return 2;
	} else {
		return 3;
	}
}

static int t_cmp_tkind(const void *a, const void *b)
{
	const int a_val = *(const int *) a;
	const int b_val = *(const int *) b;
	const struct trap_kind *ta = &trap_info[a_val];
	const struct trap_kind *tb = &trap_info[b_val];

	/* Group by */
	int c = trap_order(a_val) - trap_order(b_val);
	if (c != 0) {
		return c;
	}

	/* Order by name */
	if (ta->name) {
		if (tb->name) {
			return strcmp(ta->name, tb->name);
		} else {
			return 1;
		}
	} else if (tb->name) {
		return -1;
	}

	return 0;
}

static const char *tkind_name(int group)
{
	return trap_group_text[group];
}

/**
 * Disgusting hack to allow 4 in 1 editing of trap visuals
 */
static enum grid_light_level t_uik_lighting = LIGHTING_LIT;

static uint32_t *t_xattr(int index)
{
	return &trap_x_attr[t_uik_lighting][index];
}

static wchar_t *t_xchar(int index)
{
	return &trap_x_char[t_uik_lighting][index];
}

static void trap_lore(int index)
{
	struct trap_kind *trap = &trap_info[index];
	textblock *tb = textblock_new();
	char *title = string_make(trap->desc);

	if (trap->text) {
		my_strcap(title);
		textblock_append_c(tb, COLOUR_L_BLUE, title);
		textblock_append(tb, "\n");
		textblock_append(tb, trap->text);
		textblock_append(tb, "\n");
		region reg = {0, 0, 0, 0};
		textui_textblock_show(tb, reg, NULL);
	}

	textblock_free(tb);
	string_free(title);
}

static const char *trap_prompt(int index)
{
	(void) index;

	return ", 'l' to cycle lighting";
}

/**
 * Special key actions for cycling lighting
 */
static void t_xtra_act(struct keypress key, int index)
{
	(void) index;

	if (key.code == 'l') {
		switch (t_uik_lighting) {
				case LIGHTING_LIT:   t_uik_lighting = LIGHTING_TORCH; break;
                case LIGHTING_TORCH: t_uik_lighting = LIGHTING_LOS;   break;
				case LIGHTING_LOS:   t_uik_lighting = LIGHTING_DARK;  break;
				default:             t_uik_lighting = LIGHTING_LIT;   break;
		}		
	} else if (key.code == 'L') {
		switch (t_uik_lighting) {
				case LIGHTING_DARK: t_uik_lighting = LIGHTING_LOS;   break;
                case LIGHTING_LOS:  t_uik_lighting = LIGHTING_TORCH; break;
				case LIGHTING_LIT:  t_uik_lighting = LIGHTING_DARK;  break;
				default:            t_uik_lighting = LIGHTING_LIT;   break;
		}
	}
}

/**
 * Interact with trap visuals.
 */
static void do_cmd_knowledge_traps(const char *name, int row)
{
	(void) name;
	(void) row;
	group_funcs tkind_f = {
		.name = tkind_name,
		.gcomp = t_cmp_tkind,
		.group = trap_order,
		.summary = NULL,
		.maxnum = N_ELEMENTS(trap_group_text),
		.easy_know = false
	};

	member_funcs trap_f = {
		.display_member = display_trap,
		.lore = trap_lore,
		.xchar = t_xchar,
		.xattr = t_xattr,
		.xtra_prompt = trap_prompt,
		.xtra_act = t_xtra_act,
		.is_visual = false
	};

	int t_count = 0;

	int *traps = mem_zalloc(z_info->trap_max * sizeof(*traps));

	for (int i = 0; i < z_info->trap_max; i++) {
		if (trap_info[i].name) {
			traps[t_count] = i;
			t_count++;
		}
	}

	display_knowledge("traps", traps, t_count, tkind_f, trap_f,
			"                    Sym");
	mem_free(traps);
}

/**
 * ------------------------------------------------------------------------
 * Main knowledge menus
 * ------------------------------------------------------------------------ */

/* The first row of the knowledge_actions menu which does store knowledge */
#define STORE_KNOWLEDGE_ROW 7

static void do_cmd_knowledge_store(const char *name, int row)
{
	textui_store_knowledge(row - STORE_KNOWLEDGE_ROW);
}

static void do_cmd_knowledge_scores(const char *name, int row)
{
	show_scores();
}

static void do_cmd_knowledge_history(const char *name, int row)
{
	history_display();
}

/**
 * Definition of the "player knowledge" menu.
 */
static menu_action knowledge_actions[] = {
	{ 0, 0, "Display object knowledge",   	     textui_browse_object_knowledge },
	{ 0, 0, "Display rune knowledge",   	     do_cmd_knowledge_runes },
	{ 0, 0, "Display artifact knowledge", 	     do_cmd_knowledge_artifacts },
	{ 0, 0, "Display ego item knowledge", 	     do_cmd_knowledge_ego_items },
	{ 0, 0, "Display monster knowledge",  	     do_cmd_knowledge_monsters  },
	{ 0, 0, "Display feature knowledge",  	     do_cmd_knowledge_features  },
	{ 0, 0, "Display trap knowledge",            do_cmd_knowledge_traps  },
	{ 0, 0, "Display contents of general store", do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of armourer",      do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of weaponsmith",   do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of temple",        do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of alchemist",     do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of magic shop",    do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of black market",  do_cmd_knowledge_store     },
	{ 0, 0, "Display contents of home",   	     do_cmd_knowledge_store     },
	{ 0, 0, "Display hall of fame",       	     do_cmd_knowledge_scores    },
	{ 0, 0, "Display character history",  	     do_cmd_knowledge_history   },
};

static struct menu knowledge_menu;

/**
 * Keep macro counts happy.
 */
static void cleanup_cmds(void)
{
	mem_free(obj_group_order);
}

void textui_knowledge_init(void)
{
	/* Initialize the menus */
	struct menu *menu = &knowledge_menu;
	menu_init(menu, MN_SKIN_SCROLL, menu_find_iter(MN_ITER_ACTIONS));
	menu_setpriv(menu, N_ELEMENTS(knowledge_actions), knowledge_actions);

	menu->title = "Display current knowledge";
	menu->selections = lower_case;

	/* initialize other static variables */
	if (!obj_group_order) {
		int i;
		int group = -1;

		obj_group_order = mem_zalloc((TV_MAX + 1) * sizeof(int));
		atexit(cleanup_cmds);

		/* Allow for missing values */
		for (i = 0; i < TV_MAX; i++)
			obj_group_order[i] = -1;

		for (i = 0; 0 != object_text_order[i].tval; i++) {
			if (object_text_order[i].name) group = i;
			obj_group_order[object_text_order[i].tval] = group;
		}
	}
}

/**
 * Display the "player knowledge" menu, greying out items that won't display
 * anything.
 */
void textui_browse_knowledge(void)
{
	int i, rune_max = max_runes();
	region knowledge_region = { 0, 0, -1, 19 };

	/* Runes */
	knowledge_actions[1].flags = MN_ACT_GRAYED;
	for (i = 0; i < rune_max; i++) {
		if (player_knows_rune(player, i) || OPT(cheat_xtra)) {
			knowledge_actions[1].flags = 0;
		    break;
		}
	}
		
	/* Artifacts */
	if (collect_known_artifacts(NULL, 0) > 0)
		knowledge_actions[2].flags = 0;
	else
		knowledge_actions[2].flags = MN_ACT_GRAYED;

	/* Ego items */
	knowledge_actions[3].flags = MN_ACT_GRAYED;
	for (i = 0; i < z_info->e_max; i++) {
		if (e_info[i].everseen || OPT(cheat_xtra)) {
			knowledge_actions[3].flags = 0;
			break;
		}
	}

	/* Monsters */
	if (count_known_monsters() > 0)
		knowledge_actions[4].flags = 0;
	else
		knowledge_actions[4].flags = MN_ACT_GRAYED;

	screen_save();
	menu_layout(&knowledge_menu, &knowledge_region);

	clear_from(0);
	menu_select(&knowledge_menu, 0, false);

	screen_load();
}

/**
 * ------------------------------------------------------------------------
 * Other knowledge functions
 * ------------------------------------------------------------------------ */

/**
 * Recall the most recent message
 */
void do_cmd_message_one(void)
{
	/* Recall one message XXX XXX XXX */
	c_prt(message_color(0), format( "> %s", message_str(0)), 0, 0);
}

/**
 * Show previous messages to the user
 *
 * The screen format uses line 0 and 23 for headers and prompts,
 * skips line 1 and 22, and uses line 2 thru 21 for old messages.
 *
 * This command shows you which commands you are viewing, and allows
 * you to "search" for strings in the recall.
 *
 * Note that messages may be longer than 80 characters, but they are
 * displayed using "infinite" length, with a special sub-command to
 * "slide" the virtual display to the left or right.
 *
 * Attempt to only highlight the matching portions of the string.
 */
void do_cmd_messages(void)
{
	ui_event event;

	bool more = true;

	int i, j, n, q;
	int wid, hgt;

	char shower[80] = "";

	/* Total messages */
	n = messages_num();

	/* Start on first message */
	i = 0;

	/* Start at leftmost edge */
	q = 0;

	/* Get size */
	Term_get_size(&wid, &hgt);

	/* Save screen */
	screen_save();

	/* Process requests until done */
	while (more) {
		/* Clear screen */
		Term_clear();

		/* Dump messages */
		for (j = 0; (j < hgt - 4) && (i + j < n); j++) {
			const char *msg;
			const char *str = message_str(i + j);
			byte attr = message_color(i + j);
			u16b count = message_count(i + j);

			if (count == 1)
				msg = str;
			else
				msg = format("%s <%dx>", str, count);

			/* Apply horizontal scroll */
			msg = ((int)strlen(msg) >= q) ? (msg + q) : "";

			/* Dump the messages, bottom to top */
			Term_putstr(0, hgt - 3 - j, -1, attr, msg);

			/* Highlight "shower" */
			if (shower[0]) {
				str = msg;

				/* Display matches */
				while ((str = my_stristr(str, shower)) != NULL) {
					int len = strlen(shower);

					/* Display the match */
					Term_putstr(str-msg, hgt - 3 - j, len, COLOUR_YELLOW, str);

					/* Advance */
					str += len;
				}
			}
		}

		/* Display header */
		prt(format("Message recall (%d-%d of %d), offset %d",
				   i, i + j - 1, n, q), 0, 0);

		/* Display prompt (not very informative) */
		if (shower[0])
			prt("[Movement keys to navigate, '-' for next, '=' to find]",
				hgt - 1, 0);
		else
			prt("[Movement keys to navigate, '=' to find, or ESCAPE to exit]",
				hgt - 1, 0);
			
		/* Get a command */
		event = inkey_ex();

		/* Scroll forwards or backwards using mouse clicks */
		if (event.type == EVT_MOUSE) {
			if (event.mouse.button == 1) {
				if (event.mouse.y <= hgt / 2) {
					/* Go older if legal */
					if (i + 20 < n)
						i += 20;
				} else {
					/* Go newer */
					i = (i >= 20) ? (i - 20) : 0;
				}
			} else if (event.mouse.button == 2) {
				more = false;
			}
		} else if (event.type == EVT_KBRD) {
			switch (event.key.code) {
				case ESCAPE:
					more = false;
					break;

				case '=':
					/* Get the string to find */
					prt("Find: ", hgt - 1, 0);
					if (!askfor_aux(shower, sizeof shower, NULL)) continue;
		
					/* Set to find */
					event.key.code = '-';
					break;

				case ARROW_LEFT:
				case '4':
					q = (q >= wid / 2) ? (q - wid / 2) : 0;
					break;

				case ARROW_RIGHT:
				case '6':
					q = q + wid / 2;
					break;

				case ARROW_UP:
				case '8':
					if (i + 1 < n) i += 1;
					break;

				case ARROW_DOWN:
				case '2':
				case KC_ENTER:
					i = (i >= 1) ? (i - 1) : 0;
					break;

				case KC_PGUP:
				case 'p':
				case ' ':
					if (i + 20 < n) i += 20;
					break;

				case KC_PGDOWN:
				case 'n':
					i = (i >= 20) ? (i - 20) : 0;
					break;
			}
		}

		/* Find the next item */
		if (event.key.code == '-' && shower[0]) {
			s16b z;

			/* Scan messages */
			for (z = i + 1; z < n; z++) {
				/* Search for it */
				if (my_stristr(message_str(z), shower)) {
					/* New location */
					i = z;

					/* Done */
					break;
				}
			}
		}
	}

	/* Load screen */
	screen_load();
}

#define GET_ITEM_PARAMS \
 	(USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | SHOW_QUIVER | SHOW_EMPTY | IS_HARMLESS)
 
/**
 * Display inventory
 */
void do_cmd_inven(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (player->upkeep->inven[0] == NULL) {
		msg("You have nothing in your inventory.");
		return;
	}

	/* Start in "inventory" mode */
	player->upkeep->command_wrk = (USE_INVEN);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the inventory) */
		if (get_item(&obj, "Select Item:", NULL, CMD_NULL, NULL,
					 GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				while ((ret = context_menu_object(obj)) == 2);
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}

/**
 * Display equipment
 */
void do_cmd_equip(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (!player->upkeep->equip_cnt) {
		msg("You are not wielding or wearing anything.");
		return;
	}

	/* Start in "equipment" mode */
	player->upkeep->command_wrk = (USE_EQUIP);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the equipment) */
		if (get_item(&obj, "Select Item:", NULL, CMD_NULL, NULL,
					 GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				while ((ret = context_menu_object(obj)) == 2);

				/* Stay in "equipment" mode */
				player->upkeep->command_wrk = (USE_EQUIP);
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}

/**
 * Display equipment
 */
void do_cmd_quiver(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (player->upkeep->quiver_cnt == 0) {
		msg("You have nothing in your quiver.");
		return;
	}

	/* Start in "quiver" mode */
	player->upkeep->command_wrk = (USE_QUIVER);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the quiver) */
		if (get_item(&obj, "Select Item:", NULL, CMD_NULL, NULL,
					 GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				while ((ret = context_menu_object(obj)) == 2);

				/* Stay in "quiver" mode */
				player->upkeep->command_wrk = (USE_QUIVER);
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}

/**
 * Look command
 */
void do_cmd_look(void)
{
	/* Look around */
	if (target_set_interactive(TARGET_LOOK, -1, -1)) {
		msg("Target Selected.");
	}
}

/**
 * Number of basic grids per panel, vertically and horizontally
 */
#define PANEL_SIZE 11

/**
 * Allow the player to examine other sectors on the map
 */
void do_cmd_locate(void)
{
	int y1, x1;

	/* Start at current panel */
	y1 = Term->offset_y;
	x1 = Term->offset_x;

	/* Show panels until done */
	while (1) {
		char tmp_val[80];
		char out_val[160];

		/* Assume no direction */
		int dir = 0;

		/* Get the current panel */
		int y2 = Term->offset_y;
		int x2 = Term->offset_x;
		
		/* Adjust for tiles */
		int panel_hgt = (int)(PANEL_SIZE / tile_height);
		int panel_wid = (int)(PANEL_SIZE / tile_width);

		/* Describe the location */
		if ((y2 == y1) && (x2 == x1)) {
			tmp_val[0] = '\0';
		} else {
			strnfmt(tmp_val, sizeof(tmp_val), "%s%s of",
			        ((y2 < y1) ? " north" : (y2 > y1) ? " south" : ""),
			        ((x2 < x1) ? " west" : (x2 > x1) ? " east" : ""));
		}

		/* Prepare to ask which way to look */
		strnfmt(out_val, sizeof(out_val),
		        "Map sector [%d,%d], which is%s your sector.  Direction?",
		        (y2 / panel_hgt), (x2 / panel_wid), tmp_val);

		/* More detail */
		if (OPT(center_player)) {
			strnfmt(out_val, sizeof(out_val),
		        	"Map sector [%d(%02d),%d(%02d)], which is%s your sector.  Direction?",
					(y2 / panel_hgt), (y2 % panel_hgt),
					(x2 / panel_wid), (x2 % panel_wid), tmp_val);
		}

		/* Get a direction */
		while (!dir) {
			struct keypress command = KEYPRESS_NULL;

			/* Get a command (or Cancel) */
			if (!get_com(out_val, (char *)&command.code)) break;

			/* Extract direction */
			dir = target_dir(command);

			/* Error */
			if (!dir) bell("Illegal direction for locate!");
		}

		/* No direction */
		if (!dir) break;

		/* Apply the motion */
		change_panel(dir);

		/* Handle stuff */
		handle_stuff(player);
	}

	/* Verify panel */
	verify_panel();
}

static int cmp_mexp(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (r_info[ia].mexp < r_info[ib].mexp)
		return -1;
	if (r_info[ia].mexp > r_info[ib].mexp)
		return 1;
	return (a < b ? -1 : (a > b ? 1 : 0));
}

static int cmp_level(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (r_info[ia].level < r_info[ib].level)
		return -1;
	if (r_info[ia].level > r_info[ib].level)
		return 1;
	return cmp_mexp(a, b);
}

static int cmp_tkill(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (l_list[ia].tkills < l_list[ib].tkills)
		return -1;
	if (l_list[ia].tkills > l_list[ib].tkills)
		return 1;
	return cmp_level(a, b);
}

static int cmp_pkill(const void *a, const void *b)
{
	u16b ia = *(const u16b *) a;
	u16b ib = *(const u16b *) b;

	if (l_list[ia].pkills < l_list[ib].pkills)
		return -1;
	if (l_list[ia].pkills > l_list[ib].pkills)
		return 1;
	return cmp_tkill(a, b);
}

int cmp_monsters(const void *a, const void *b)
{
	return cmp_level(a, b);
}

/**
 * Search the monster, item, and feature types to find the
 * meaning for the given symbol.
 *
 * Note: We currently search items first, then features, then
 * monsters, and we return the first hit for a symbol.
 * This is to prevent mimics and lurkers from matching
 * a symbol instead of the item or feature it is mimicking.
 *
 * Todo: concatenate all matches into buf. This will be much
 * easier once we can loop through item tvals instead of items
 * (see note below.)
 *
 * Todo: Should this take the user's pref files into account?
 */
static void lookup_symbol(char sym, char *buf, size_t max)
{
	int i;
	struct monster_base *race;

	/* Look through items */
	/* Note: We currently look through all items, and grab the tval when we
	 * find a match.
	 * It would make more sense to loop through tvals, but then we need to
	 * associate a display character with each tval. */
	for (i = 1; i < z_info->k_max; i++) {
		if (char_matches_key(k_info[i].d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, tval_find_name(k_info[i].tval));
			return;
		}
	}

	/* Look through features */
	/* Note: We need a better way of doing this. Currently '#' matches secret
	 * door, and '^' matches trap door (instead of the more generic "trap"). */
	for (i = 1; i < z_info->f_max; i++) {
		if (char_matches_key(f_info[i].d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, f_info[i].name);
			return;
		}
	}
	
	/* Look through monster templates */
	for (race = rb_info; race; race = race->next) {
		/* Slight hack - P appears twice */
		if (streq(race->name, "Morgoth")) continue;
		if (char_matches_key(race->d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, race->text);
			return;
		}
	}

	/* No matches */
	if (isprint(sym)) {
		strnfmt(buf, max, "%c - Unknown Symbol.", sym);
	} else {
		strnfmt(buf, max, "? - Unknown Symbol.");
	}

	return;
}

/**
 * Identify a character, allow recall of monsters
 *
 * Several "special" responses recall "multiple" monsters:
 *   ^A (all monsters)
 *   ^U (all unique monsters)
 *   ^N (all non-unique monsters)
 *
 * The responses may be sorted in several ways, see below.
 *
 * Note that the player ghosts are ignored, since they do not exist.
 */
void do_cmd_query_symbol(void)
{
	int i, n;
	char buf[128];

	char sym;
	struct keypress query;

	bool all = false;
	bool uniq = false;
	bool norm = false;

	bool recall = false;

	u16b *who;

	/* Get a character, or abort */
	if (!get_com("Enter character to be identified, or control+[ANU]: ", &sym))
		return;

	/* Describe */
	if (sym == KTRL('A')) {
		all = true;
		my_strcpy(buf, "Full monster list.", sizeof(buf));
	} else if (sym == KTRL('U')) {
		all = uniq = true;
		my_strcpy(buf, "Unique monster list.", sizeof(buf));
	} else if (sym == KTRL('N')) {
		all = norm = true;
		my_strcpy(buf, "Non-unique monster list.", sizeof(buf));
	} else {
		lookup_symbol(sym, buf, sizeof(buf));
	}

	/* Display the result */
	prt(buf, 0, 0);

	/* Allocate the "who" array */
	who = mem_zalloc(z_info->r_max * sizeof(u16b));

	/* Collect matching monsters */
	for (n = 0, i = 1; i < z_info->r_max - 1; i++) {
		struct monster_race *race = &r_info[i];
		struct monster_lore *lore = &l_list[i];

		/* Nothing to recall */
		if (!OPT(cheat_know) && !lore->all_known && !lore->sights) continue;

		/* Require non-unique monsters if needed */
		if (norm && rf_has(race->flags, RF_UNIQUE)) continue;

		/* Require unique monsters if needed */
		if (uniq && !rf_has(race->flags, RF_UNIQUE)) continue;

		/* Collect "appropriate" monsters */
		if (all || char_matches_key(race->d_char, sym)) who[n++] = i;
	}

	/* Nothing to recall */
	if (!n) {
		/* XXX XXX Free the "who" array */
		mem_free(who);

		return;
	}

	/* Prompt */
	put_str("Recall details? (y/k/n): ", 0, 40);

	/* Query */
	query = inkey();

	/* Restore */
	prt(buf, 0, 0);

	/* Interpret the response */
	if (query.code == 'k') {
		/* Sort by kills (and level) */
		sort(who, n, sizeof(*who), cmp_pkill);
	} else if (query.code == 'y' || query.code == 'p') {
		/* Sort by level; accept 'p' as legacy */
		sort(who, n, sizeof(*who), cmp_level);
	} else {
		/* Any unsupported response is "nope, no history please" */
		/* XXX XXX Free the "who" array */
		mem_free(who);
		return;
	}

	/* Start at the end */
	i = n - 1;

	/* Scan the monster memory */
	while (1) {
		textblock *tb;

		/* Extract a race */
		int r_idx = who[i];
		struct monster_race *race = &r_info[r_idx];
		struct monster_lore *lore = &l_list[r_idx];

		/* Hack -- Auto-recall */
		monster_race_track(player->upkeep, race);

		/* Hack -- Handle stuff */
		handle_stuff(player);

		tb = textblock_new();
		lore_title(tb, race);

		textblock_append(tb, " [(r)ecall, ESC]");
		textui_textblock_place(tb, SCREEN_REGION, NULL);
		textblock_free(tb);

		/* Interact */
		while (1) {
			/* Ignore keys during recall presentation, otherwise, the 'r' key
			 * acts like a toggle and instead of a one-off command */
			if (recall)
				lore_show_interactive(race, lore);
			else
				query = inkey();

			/* Normal commands */
			if (query.code != 'r') break;

			/* Toggle recall */
			recall = !recall;
		}

		/* Stop scanning */
		if (query.code == ESCAPE) break;

		/* Move to "prev" or "next" monster */
		if (query.code == '-') {
			if (++i == n)
				i = 0;
		} else {
			if (i-- == 0)
				i = n - 1;
		}
	}

	/* Re-display the identity */
	prt(buf, 0, 0);

	/* Free the "who" array */
	mem_free(who);
}

/**
 * Centers the map on the player
 */
void do_cmd_center_map(void)
{
	center_panel();
}

/**
 * Display the main-screen monster list.
 */
void do_cmd_monlist(void)
{
	screen_save();
    monster_list_show_interactive(Term->hgt, Term->wid);
	screen_load();
}

/**
 * Display the main-screen item list.
 */
void do_cmd_itemlist(void)
{
	screen_save();
    object_list_show_interactive(Term->hgt, Term->wid);
	screen_load();
}
