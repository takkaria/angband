/**
 * \file ui2-player.c
 * \brief character screens and dumps
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
#include "buildid.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "player.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui2-display.h"
#include "ui2-history.h"
#include "ui2-input.h"
#include "ui2-menu.h"
#include "ui2-object.h"
#include "ui2-output.h"
#include "ui2-player.h"

/**
 * ------------------------------------------------------------------------
 * Panel utilities
 * ------------------------------------------------------------------------ */

/**
 * Panel line type
 */
struct panel_line {
	uint32_t attr;
	const char *label;
	char value[20];
};

/**
 * Panel holder type
 */
struct panel {
	size_t next;
	size_t size;
	struct panel_line *lines;
};

/**
 * Allocate some panel lines
 */
static struct panel *panel_allocate(size_t size)
{
	struct panel *p = mem_zalloc(sizeof(*p));

	p->next = 0;
	p->size = size;
	p->lines = mem_zalloc(p->size * sizeof(*p->lines));

	return p;
}

/**
 * Free up panel lines
 */
static void panel_free(struct panel *p)
{
	assert(p);
	mem_free(p->lines);
	mem_free(p);
}

/**
 * Add a new line to the panel
 */
static void panel_line(struct panel *p,
		uint32_t attr, const char *label, const char *fmt, ...)
{
	/* Get the next panel line */
	assert(p);
	assert(p->next < p->size);

	struct panel_line *pl = &p->lines[p->next];

	/* Set the basics */
	pl->attr = attr;
	pl->label = label;

	/* Set the value */
	va_list vp;
	va_start(vp, fmt);
	vstrnfmt(pl->value, sizeof(pl->value), fmt, vp);
	va_end(vp);

	/* This line is used */
	p->next++;
}

/**
 * Add a spacer line in a panel
 */
static void panel_space(struct panel *p)
{
	assert(p);
	assert(p->next < p->size);
	p->next++;
}

/**
 * Returns a rating of a depending on b, and sets attr to the
 * corresponding attribute.
 */
static const char *likert(int a, int b, uint32_t *attr)
{
	if (b <= 0) {
		b = 1;
	}

	/* Negative value */
	if (a < 0) {
		*attr = COLOUR_RED;
		return "Very Bad";
	}

	/* Analyze the value */
	switch (a / b) {
		case 0: case 1:
			*attr = COLOUR_RED;
			return "Bad";
		case 2:
			*attr = COLOUR_RED;
			return "Poor";
		case 3: case 4:
			*attr = COLOUR_YELLOW;
			return "Fair";
		case 5:
			*attr = COLOUR_YELLOW;
			return "Good";
		case 6:
			*attr = COLOUR_YELLOW;
			return "Very Good";
		case 7: case 8:
			*attr = COLOUR_L_GREEN;
			return "Excellent";
		case 9: case 10: case 11: case 12: case 13:
			*attr = COLOUR_L_GREEN;
			return "Superb";
		case 14: case 15: case 16: case 17:
			*attr = COLOUR_L_GREEN;
			return "Heroic";
		default:
			*attr = COLOUR_L_GREEN;
			return "Legendary";
	}
}

/**
 * Equippy chars
 */
static void display_player_equippy(struct loc loc)
{
	for (int i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (obj) {
			uint32_t attr = object_attr(obj);
			wchar_t ch = object_char(obj);

			Term_addwc(loc.x + i, loc.y, attr, ch);
		}
	}
}

/**
 * List of resistances and abilities to display
 */
#define RES_ROWS 9

struct player_flag_record {
	const char name[7]; /* Name of resistance/ability */
	int mod;            /* Modifier */
	int flag;           /* Flag bit */
	int element;        /* Element */
	int tmd_flag;       /* corresponding timed flag */
};

static const struct player_flag_record player_flag_table[RES_ROWS * 4] = {
	{"rAcid",  -1,              -1,               ELEM_ACID,    TMD_OPP_ACID},
	{"rElec",  -1,              -1,               ELEM_ELEC,    TMD_OPP_ELEC},
	{"rFire",  -1,              -1,               ELEM_FIRE,    TMD_OPP_FIRE},
	{"rCold",  -1,              -1,               ELEM_COLD,    TMD_OPP_COLD},
	{"rPois",  -1,              -1,               ELEM_POIS,    TMD_OPP_POIS},
	{"rLite",  -1,              -1,               ELEM_LIGHT,  -1},
	{"rDark",  -1,              -1,               ELEM_DARK,   -1},
	{"Sound",  -1,              -1,               ELEM_SOUND,  -1},
	{"Shard",  -1,              -1,               ELEM_SHARD,  -1},

	{"Nexus",  -1,              -1,               ELEM_NEXUS,  -1},
	{"Nethr",  -1,              -1,               ELEM_NETHER, -1},
	{"Chaos",  -1,              -1,               ELEM_CHAOS,  -1},
	{"Disen",  -1,              -1,               ELEM_DISEN,  -1},
	{"pFear",  -1,               OF_PROT_FEAR,   -1,            TMD_BOLD},
	{"pBlnd",  -1,               OF_PROT_BLIND,  -1,           -1},
	{"pConf",  -1,               OF_PROT_CONF,   -1,            TMD_OPP_CONF},
	{"pStun",  -1,               OF_PROT_STUN,   -1,           -1},
	{"HLife",  -1,               OF_HOLD_LIFE,   -1,           -1},

	{"Regen",  -1,               OF_REGEN,       -1,           -1},
	{"  ESP",  -1,               OF_TELEPATHY,   -1,            TMD_TELEPATHY},
	{"Invis",  -1,               OF_SEE_INVIS,   -1,            TMD_SINVIS},
	{"FrAct",  -1,               OF_FREE_ACT,    -1,           -1},
	{"Feath",  -1,               OF_FEATHER,     -1,           -1},
	{"S.Dig",  -1,               OF_SLOW_DIGEST, -1,           -1},
	{"ImpHP",  -1,               OF_IMPAIR_HP,   -1,           -1},
	{" Fear",  -1,               OF_AFRAID,      -1,            TMD_AFRAID},
	{"Aggrv",  -1,               OF_AGGRAVATE,   -1,           -1},

	{"Stea.",   OBJ_MOD_STEALTH, -1,             -1,           -1},
	{"Infra",   OBJ_MOD_INFRA,   -1,             -1,            TMD_SINFRA},
	{"Tunn.",   OBJ_MOD_TUNNEL,  -1,             -1,           -1},
	{"Speed",   OBJ_MOD_SPEED,   -1,             -1,            TMD_FAST},
	{"Blows",   OBJ_MOD_BLOWS,   -1,             -1,           -1},
	{"Shots",   OBJ_MOD_SHOTS,   -1,             -1,           -1},
	{"Might",   OBJ_MOD_MIGHT,   -1,             -1,           -1},
	{"Light",   OBJ_MOD_LIGHT,   -1,             -1,           -1},
	{"",       -1,               -1,             -1,           -1},
};

static void display_resistance_panel(const struct player_flag_record *rec,
		size_t size, struct loc loc) 
{
	const int res_cols = 6 + player->body.count + 1;

	Term_adds(loc.x, loc.y++, res_cols, COLOUR_WHITE, "      abcdefghijkl@");

	for (size_t i = 0; i < size; i++) {
		uint32_t name_attr = COLOUR_WHITE;
		Term_cursor_to_xy(loc.x + 6, loc.y);

		/* Repeated extraction of flags is inefficient but more natural */
		for (int j = 0; j <= player->body.count; j++) {
			bitflag f[OF_SIZE] = {0};

			uint32_t attr =
				j % 2 == 0 ? COLOUR_WHITE : COLOUR_L_WHITE; /* alternating colors */
			char sym = '.';

			bool res = false;
			bool imm = false;
			bool vuln = false;
			bool rune = false;
			bool timed = false;
			bool known = false;

			/* Get the object or player info */
			struct object *obj = j < player->body.count ? slot_object(player, j) : NULL;
			if (j < player->body.count && obj != NULL) {
				/* Get known properties */
				object_flags_known(obj, f);
				if (rec[i].element != -1) {
					known = object_element_is_known(obj, rec[i].element);
				} else if (rec[i].flag != -1) {
					known = object_flag_is_known(obj, rec[i].flag);
				} else {
					known = true;
				}
			} else if (j == player->body.count) {
				player_flags(player, f);
				known = true;

				/* Timed flags only in the player column */
				if (rec[i].tmd_flag >= 0) {
	 				timed = player->timed[rec[i].tmd_flag] ? true : false;
					/* There has to be one special case... */
					if (rec[i].tmd_flag == TMD_AFRAID
							&& player->timed[TMD_TERROR])
					{
						timed = true;
					}
				}
			}

			/* Set which (if any) symbol and color are used */
			if (rec[i].mod != -1) {
				if (j < player->body.count) {
					res = obj && (obj->modifiers[rec[i].mod] != 0);
				} else {
					/* Messy special cases */
					if (rec[i].mod == OBJ_MOD_INFRA) {
						res = player->race->infra > 0;
					} else if (rec[i].mod == OBJ_MOD_TUNNEL) {
						res = player->race->r_skills[SKILL_DIGGING] > 0;
					}
				}
				rune = player->obj_k->modifiers[rec[i].mod] == 1;
			} else if (rec[i].flag != -1) {
				res = of_has(f, rec[i].flag);
				rune = of_has(player->obj_k->flags, rec[i].flag);
			} else if (rec[i].element != -1) {
				if (j < player->body.count) {
					imm = obj && known &&
						obj->el_info[rec[i].element].res_level == 3;
					res = obj && known &&
						obj->el_info[rec[i].element].res_level == 1;
					vuln = obj && known &&
						obj->el_info[rec[i].element].res_level == -1;
				} else {
					imm = player->race->el_info[rec[i].element].res_level == 3;
					res = player->race->el_info[rec[i].element].res_level == 1;
					vuln = player->race->el_info[rec[i].element].res_level == -1;
				}
				rune = player->obj_k->el_info[rec[i].element].res_level == 1;
			}

			/* Set the symbols and print them */
			if (imm) {
				name_attr = COLOUR_GREEN;
			} else if (!rune) {
				name_attr = COLOUR_SLATE;
			} else if (res && name_attr != COLOUR_GREEN) {
				name_attr = COLOUR_L_BLUE;
			}

			if (vuln) {
				sym = '-';
			} else if (imm) {
				sym = '*';
			} else if (res) {
				sym = '+';
			} else if (timed) {
				sym = '!';
				attr = COLOUR_L_GREEN;
			} else if (j < player->body.count && obj && !known && !rune) {
				sym = '?';
			}

			Term_putwc(attr, sym);
		}
		Term_adds(loc.x, loc.y++, 6, name_attr, format("%5s:", rec[i].name));
	}

	Term_adds(loc.x, loc.y++, res_cols, COLOUR_WHITE, "      abcdefghijkl@");

	loc.x += 6;
	display_player_equippy(loc);
}

static void display_player_flag_info(void)
{
	const int res_cols = 6 + player->body.count + 1;

	const struct loc resist_locs[] = {
		{0 * (res_cols + 1), 11},
		{1 * (res_cols + 1), 11},
		{2 * (res_cols + 1), 11},
		{3 * (res_cols + 1), 11},
	};

	for (size_t i = 0; i < N_ELEMENTS(resist_locs); i++) {
		display_resistance_panel(player_flag_table + i * RES_ROWS, RES_ROWS,
				resist_locs[i]);
	}
}

/**
 * Special display, part 2b
 */
void display_player_stat_info(void)
{
	const int col = 42;
	const int row = 2;

	struct loc loc;

	/* Print out the labels for the columns */
	loc.y = row - 1;

	loc.x = col + 5;
	c_put_str(COLOUR_WHITE, "  Self", loc);

	loc.x = col + 12;
	c_put_str(COLOUR_WHITE, " RB",    loc);

	loc.x = col + 16;
	c_put_str(COLOUR_WHITE, " CB",    loc);

	loc.x = col + 20;
	c_put_str(COLOUR_WHITE, " EB",    loc);

	loc.x = col + 24;
	c_put_str(COLOUR_WHITE, "  Best", loc);

	loc.y = row;
	/* Display the stats */
	for (int i = 0; i < STAT_MAX; i++, loc.y++) {
		loc.x = col;

		if (player->stat_cur[i] < player->stat_max[i]) {
			/* Reduced stat; use lowercase stat name */
			put_str(stat_names_reduced[i], loc);
		} else {
			/* Normal stat; use uppercase stat name */
			put_str(stat_names[i], loc);
		}

		/* Indicate natural maximum */
		if (player->stat_max[i] == 18 + 100) {
			loc.x = col + 3;
			put_str("!", loc);
		}

		char buf[80];

		/* Internal "natural" maximum value */
		loc.x = col + 5;
		cnv_stat(player->stat_max[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, loc);

		/* Race Bonus */
		loc.x = col + 12;
		strnfmt(buf, sizeof(buf), "%+3d", player->race->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, loc);

		/* Class Bonus */
		loc.x = col + 16;
		strnfmt(buf, sizeof(buf), "%+3d", player->class->c_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, loc);

		/* Equipment Bonus */
		loc.x = col + 20;
		strnfmt(buf, sizeof(buf), "%+3d", player->state.stat_add[i]);
		c_put_str(COLOUR_L_BLUE, buf, loc);

		/* Resulting "modified" maximum value */
		loc.x = col + 24;
		cnv_stat(player->state.stat_top[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, loc);

		/* Only display stat_use if there has been draining */
		if (player->stat_cur[i] < player->stat_max[i]) {
			cnv_stat(player->state.stat_use[i], buf, sizeof(buf));
			loc.x = col + 31;
			c_put_str(COLOUR_YELLOW, buf, loc);
		}
	}

	Term_flush_output();
}

/**
 * Special display, part 2c
 *
 * How to print out the modifications and sustains.
 * Positive mods with no sustain will be light green.
 * Positive mods with a sustain will be dark green.
 * Sustains (with no modification) will be a dark green 's'.
 * Negative mods (from a curse) will be red.
 * Huge mods (>9), like from MICoMorgoth, will be a '*'
 * No mod, no sustain, will be a slate '.'
 */
static void display_player_sust_info(void)
{
	const int col = 26;
	const int row = 2;

	struct loc loc;
	/* Header */
	loc.x = col;
	loc.y = row - 1;
	c_put_str(COLOUR_WHITE, "abcdefghijkl@", loc);

	bitflag f[OF_SIZE];

	/* Process equipment */
	for (int i = 0; i < player->body.count; i++, loc.x++) {
		struct object *obj = slot_object(player, i);

		if (!obj) {
			continue;
		}

		object_flags_known(obj, f);

		loc.y = row;
		for (int stat = OBJ_MOD_MIN_STAT;
				stat < OBJ_MOD_MIN_STAT + STAT_MAX;
				stat++, loc.y++)
		{
			uint32_t attr = COLOUR_SLATE;
			wchar_t ch = '.';

			if (obj->modifiers[stat] > 0) {
				attr = COLOUR_L_GREEN;
				if (obj->modifiers[stat] < 10) {
					ch = I2D(obj->modifiers[stat]);
				}
			} else if (obj->modifiers[stat] < 0) {
				attr = COLOUR_RED;
				if (obj->modifiers[stat] > -10) {
					ch = I2D(-(obj->modifiers[stat]));
				}
			}

			if (of_has(f, sustain_flag(stat))) {
				attr = COLOUR_GREEN;
				if (ch == '.') {
					ch = 's';
				}
			}

			if (ch == '.' && obj &&
					!object_flag_is_known(obj, sustain_flag(stat)))
			{
				ch = '?';
			}

			Term_addwc(loc.x, loc.y, attr, ch);
		}
	}

	/* Player flags */
	player_flags(player, f);

	/* Check stats */
	loc.y = row;
	for (int stat = 0; stat < STAT_MAX; stat++, loc.y++) {
		/* Default */
		uint32_t attr = COLOUR_SLATE;
		wchar_t ch = '.';

		if (of_has(f, sustain_flag(stat))) {
			attr = COLOUR_GREEN;
			ch = 's';
		}

		Term_addwc(loc.x, loc.y, attr, ch);
	}

	loc.x = col;

	loc.y = row + 6;
	c_put_str(COLOUR_WHITE, "abcdefghijkl@", loc);

	loc.y = row + 7;
	display_player_equippy(loc);
}

static void display_panel(const struct panel *p, bool left_adj, region reg)
{
	region_erase(reg);

	int offset = 0;

	if (left_adj) {
		for (size_t i = 0; i < p->next; i++) {
			struct panel_line *pl = &p->lines[i];
			int len = pl->label ? strlen(pl->label) : 0;

			offset = MAX(offset, len);
		}
		offset += 2;
	}

	for (size_t i = 0; i < p->next; i++, reg.y++) {
		struct panel_line *pl = &p->lines[i];

		if (pl->label) {
			Term_adds(reg.x, reg.y, strlen(pl->label), COLOUR_WHITE, pl->label);

			int len = strlen(pl->value);

			if (len > 0) {
				len = MIN(len, reg.w - offset - 1);

				if (left_adj) {
					Term_adds(reg.x + offset, reg.y, len, pl->attr, pl->value);
				} else {
					Term_adds(reg.x + reg.w - len, reg.y, len, pl->attr, pl->value);
				}
			}
		}
	}
}

static const char *show_title(void)
{
	if (player->wizard) {
		return "[=-WIZARD-=]";
	} else if (player->total_winner || player->lev > PY_MAX_LEVEL) {
		return "***WINNER***";
	} else {
		return player->class->title[(player->lev - 1) / 5];
	}
}

static const char *show_adv_exp(void)
{
	if (player->lev < PY_MAX_LEVEL) {
		static char buffer[30];
		s32b advance = (player_exp[player->lev - 1] * player->expfact / 100L);
		strnfmt(buffer, sizeof(buffer), "%d", advance);
		return buffer;
	}
	else {
		return "********";
	}
}

static const char *show_depth(void)
{
	static char buffer[13];

	if (player->max_depth == 0) {
		return "Town";
	}

	strnfmt(buffer, sizeof(buffer),
			"%d' (L%d)", player->max_depth * 50, player->max_depth);
	return buffer;
}

static const char *show_speed(void)
{
	static char buffer[10];
	int speed = player->state.speed;

	if (player->timed[TMD_FAST]) {
		speed -= 10;
	}
	if (player->timed[TMD_SLOW]) {
		speed += 10;
	}

	if (speed == 110) {
		return "Normal";
	} else {
		strnfmt(buffer, sizeof(buffer), "%d", speed - 110);
		return buffer;
	}
}

static uint32_t max_color(int val, int max)
{
	return val < max ? COLOUR_YELLOW : COLOUR_L_GREEN;
}

/**
 * Colours for table items
 */
static const uint32_t skill_colour_table[] = {
	COLOUR_RED,
	COLOUR_RED,
	COLOUR_RED,
	COLOUR_L_RED,
	COLOUR_ORANGE,
	COLOUR_YELLOW,
	COLOUR_YELLOW,
	COLOUR_GREEN,
	COLOUR_GREEN,
	COLOUR_L_GREEN,
	COLOUR_L_BLUE
};

static struct panel *get_panel_topleft(void) {
	struct panel *p = panel_allocate(6);

	panel_line(p, COLOUR_L_BLUE, "Name",  "%s", op_ptr->full_name);
	panel_line(p, COLOUR_L_BLUE, "Race",  "%s", player->race->name);
	panel_line(p, COLOUR_L_BLUE, "Class", "%s", player->class->name);
	panel_line(p, COLOUR_L_BLUE, "Title", "%s", show_title());
	panel_line(p, COLOUR_L_BLUE, "HP", "%d/%d", player->chp, player->mhp);
	panel_line(p, COLOUR_L_BLUE, "SP", "%d/%d", player->csp, player->msp);

	return p;
}

static struct panel *get_panel_midleft(void) {
	struct panel *p = panel_allocate(9);
	int diff = weight_remaining(player);
	uint32_t attr = diff < 0 ? COLOUR_L_RED : COLOUR_L_GREEN;

	panel_line(p, max_color(player->lev, player->max_lev),
			"Level", "%d", player->lev);
	panel_line(p, max_color(player->exp, player->max_exp),
			"Cur Exp", "%d", player->exp);
	panel_line(p, COLOUR_L_GREEN, "Max Exp", "%d", player->max_exp);
	panel_line(p, COLOUR_L_GREEN, "Adv Exp", "%s", show_adv_exp());
	panel_space(p);
	panel_line(p, COLOUR_L_GREEN, "Gold", "%d", player->au);
	panel_line(p, attr,
			"Burden", "%.1f lb", player->upkeep->total_weight / 10.0F);
	panel_line(p, attr, "Overweight", "%d.%d lb", -diff / 10, abs(diff) % 10);
	panel_line(p, COLOUR_L_GREEN, "Max Depth", "%s", show_depth());

	return p;
}

static struct panel *get_panel_combat(void) {
	struct panel *p = panel_allocate(9);
	int melee_dice = 1;
	int melee_sides = 1;

	/* AC */
	panel_line(p, COLOUR_L_BLUE, "Armor", "[%d,%+d]",
			player->known_state.ac, player->known_state.to_a);

	/* Melee */
	struct object *obj = equipped_item_by_slot_name(player, "weapon");
	int bth = player->state.skills[SKILL_TO_HIT_MELEE] * 10 / BTH_PLUS_ADJ;
	int dam = player->known_state.to_d + (obj ? obj->known->to_d : 0);
	int hit = player->known_state.to_h + (obj ? obj->known->to_h : 0);

	panel_space(p);

	if (obj) {
		melee_dice = obj->dd;
		melee_sides = obj->ds;
	}

	panel_line(p, COLOUR_L_BLUE, "Melee", "%dd%d,%+d", melee_dice, melee_sides, dam);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Blows", "%d.%d/turn",
			player->state.num_blows / 100, (player->state.num_blows / 10 % 10));

	/* Ranged */
	obj = equipped_item_by_slot_name(player, "shooting");
	bth = (player->state.skills[SKILL_TO_HIT_BOW] * 10) / BTH_PLUS_ADJ;
	hit = player->known_state.to_h + (obj ? obj->known->to_h : 0);
	dam = obj ? obj->known->to_d : 0;

	panel_space(p);
	panel_line(p, COLOUR_L_BLUE, "Shoot to-dam", "%+d", dam);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Shots", "%d/turn", player->state.num_shots);

	return p;
}

static struct panel *get_panel_skills(void) {
	struct panel *p = panel_allocate(8);

	int depth = cave ? cave->depth : 0;
	uint32_t attr;

#define BOUND(x, min, max) \
	MIN((max), MAX((min), (x)))

	/* Saving throw */
	int skill = BOUND(player->state.skills[SKILL_SAVE], 0, 100);
	panel_line(p, skill_colour_table[skill / 10], "Saving Throw", "%d%%", skill);

	/* Stealth */
	const char *desc = likert(player->state.skills[SKILL_STEALTH], 1, &attr);
	panel_line(p, attr, "Stealth", "%s", desc);

	/* Physical disarming: assume we're disarming a dungeon trap */
	skill = BOUND(player->state.skills[SKILL_DISARM_PHYS] - depth / 5, 2, 100);
	panel_line(p, skill_colour_table[skill / 10], "Disarm - phys.", "%d%%", skill);

	/* Magical disarming */
	skill = BOUND(player->state.skills[SKILL_DISARM_MAGIC] - depth / 5, 2, 100);
	panel_line(p, skill_colour_table[skill / 10], "Disarm - magic", "%d%%", skill);

	/* Magic devices */
	skill = player->state.skills[SKILL_DEVICE];
	panel_line(p, skill_colour_table[skill / 13], "Magic Devices", "%d", skill);

	/* Infravision */
	panel_line(p, COLOUR_L_GREEN,
			"Infravision", "%d ft", player->state.see_infra * 10);

	/* Speed */
	skill = player->state.speed;
	if (player->timed[TMD_FAST]) {
		skill -= 10;
	}
	if (player->timed[TMD_SLOW]) {
		skill += 10;
	}
	attr = skill < 110 ? COLOUR_L_UMBER : COLOUR_L_GREEN;
	panel_line(p, attr, "Speed", "%s", show_speed());

#undef BOUND

	return p;
}

static struct panel *get_panel_misc(void) {
	struct panel *p = panel_allocate(7);
	uint32_t attr = COLOUR_L_BLUE;

	panel_line(p, attr, "Age", "%d", player->age);
	panel_line(p, attr, "Height", "%d'%d\"", player->ht / 12, player->ht % 12);
	panel_line(p, attr, "Weight", "%dst %dlb", player->wt / 14, player->wt % 14);
	panel_line(p, attr, "Turns used:", "");
	panel_line(p, attr, "Game", "%d", turn);
	panel_line(p, attr, "Standard", "%d", player->total_energy / 100);
	panel_line(p, attr, "Resting", "%d", player->resting_turn);

	return p;
}

/**
 * Panels for main character screen
 */
static const struct {
	region bounds;
	bool align_left;
	struct panel *(*panel)(void);
} panels[] = {
	{{ 1, 1, 20, 6}, true,  get_panel_topleft}, /* Name, Class, ... */
	{{21, 1, 18, 7}, false, get_panel_misc},    /* Age, ht, wt, ... */
	{{ 1, 9, 24, 9}, false, get_panel_midleft}, /* Cur Exp, Max Exp, ... */
	{{29, 9, 19, 9}, false, get_panel_combat},
	{{52, 9, 20, 8}, false, get_panel_skills},
};

void display_player_basic_info(void)
{
	for (size_t i = 0; i < N_ELEMENTS(panels); i++) {
		struct panel *p = panels[i].panel();
		display_panel(p, panels[i].align_left, panels[i].bounds);
		panel_free(p);
	}

	/* Indent output by 1 character, and wrap at column 72 */
	struct text_out_info info = {
		.wrap = 72,
		.indent = 1
	};

	/* History */
	Term_cursor_to_xy(info.indent, 20);
	text_out_c(info, COLOUR_WHITE, player->history);

	Term_flush_output();
}

/**
 * Display the character on the screen (two different modes)
 * The top two lines, and the bottom line (or two) are left blank.
 */
void display_player(enum player_display_mode mode)
{
	Term_clear();

	display_player_stat_info();

	if (mode == PLAYER_DISPLAY_MODE_BASIC) {
		display_player_basic_info();
	} else if (mode == PLAYER_DISPLAY_MODE_EXTRA) {
		struct panel *p = panels[0].panel();
		display_panel(p, panels[0].align_left, panels[0].bounds);
		panel_free(p);

		display_player_sust_info();
		display_player_flag_info();
	} else {
		quit_fmt("bad player display mode %d", mode);
	}

	Term_flush_output();
}

static void dump_term_line(ang_file *file, int x, int y, int len)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH * MB_LEN_MAX];

	size_t size = 0;

	for (int endx = x + len; x < endx; x++) {
		struct term_point point;
		Term_get_point(x, y, &point);

		if (point.fg_char == 0) {
			point.fg_char = L' ';
		}

		if (size + MB_LEN_MAX < sizeof(buf)) {
			int n = wctomb(buf + size, point.fg_char);
			assert(n != -1);

			size += n;
		}
	}

	/* Back up over spaces */
	while (size > 0 && buf[size - 1] == ' ') {
		size--;
	}

	assert(size < sizeof(buf));
	buf[size] = 0;

	file_putf(file, "%s\n", buf);
}

/**
 * Write a character dump
 */
static void write_character_dump(ang_file *file)
{
	file_putf(file, "  [%s Character Dump]\n\n", buildid);

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.position = TERM_POSITION_CENTER,
		.purpose = TERM_PURPOSE_TEXT
	};

	/* What a hack */
	Term_push_new(&hints);

	display_player(PLAYER_DISPLAY_MODE_BASIC);

	for (int y = 1; y < 23; y++) {
		dump_term_line(file, 0, y, 80);
	}
	file_put(file, "\n");

	display_player(PLAYER_DISPLAY_MODE_EXTRA);

	for (int y = 12; y < 21; y++) {
		dump_term_line(file, 0, y, 40);
	}
	file_put(file, "\n");

	for (int y = 12; y < 21; y++) {
		dump_term_line(file, 40, y, 40);
	}
	file_put(file, "\n\n");

	Term_pop();

	if (player->is_dead) {
		unsigned i = messages_num();
		if (i > 15) {
			i = 15;
		}
		file_putf(file, "  [Last Messages]\n\n");
		while (i > 0) {
			i--;
			file_putf(file, "> %s\n", message_str(i));
		}
		file_putf(file, "\nKilled by %s.\n\n", player->died_from);
	}

	char o_name[ANGBAND_TERM_STANDARD_WIDTH];

	/* Dump the equipment */
	file_putf(file, "  [Character Equipment]\n\n");
	for (int i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (obj) {
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
			file_putf(file, "%c) %s\n", gear_to_label(obj), o_name);
			object_info_chardump(file, obj, 5, 72);
		}
	}
	file_put(file, "\n\n");

	/* Dump the inventory */
	file_putf(file, "  [Character Inventory]\n\n");
	for (int i = 0; i < z_info->pack_size; i++) {
		struct object *obj = player->upkeep->inven[i];
		if (obj) {
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
			file_putf(file, "%c) %s\n", gear_to_label(obj), o_name);
			object_info_chardump(file, obj, 5, 72);
		}
	}
	file_put(file, "\n\n");

	/* Dump the quiver */
	file_putf(file, "  [Character Quiver]\n\n");
	for (int i = 0; i < z_info->quiver_size; i++) {
		struct object *obj = player->upkeep->quiver[i];
		if (obj) {
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
			file_putf(file, "%c) %s\n", gear_to_label(obj), o_name);
			object_info_chardump(file, obj, 5, 72);
		}
	}
	file_put(file, "\n\n");

	/* Dump the home if anything there */
	if (stores[STORE_HOME].stock_num > 0) {
		file_putf(file, "  [Home Inventory]\n\n");

		struct object **home_list =
			mem_zalloc(sizeof(*home_list) * z_info->store_inven_max);

		store_stock_list(&stores[STORE_HOME],
				home_list, z_info->store_inven_max);

		for (int i = 0; i < stores[STORE_HOME].stock_num; i++) {
			struct object *obj = home_list[i];

			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
			file_putf(file, "%c) %s\n", I2A(i), o_name);

			object_info_chardump(file, obj, 5, 72);
		}

		file_put(file, "\n\n");

		mem_free(home_list);
	}

	/* Dump character history */
	dump_history(file);
	file_put(file, "\n\n");

	/* Dump options */
	file_putf(file, "  [Options]\n\n");

	/* Dump options */
	for (int i = 0; i < OP_MAX; i++) {
		const char *title = NULL;
		switch (i) {
			case OP_INTERFACE: title = "User interface"; break;
			case OP_BIRTH:     title = "Birth";          break;
		}

		if (title != NULL) {
			file_putf(file, "  [%s]\n\n", title);
			for (int opt = 0; opt < OPT_MAX; opt++) {
				if (option_type(opt) == i) {
					file_putf(file, "%-45s: %s (%s)\n",
							option_desc(opt),
							op_ptr->opt[opt] ? "yes" : "no ",
							option_name(opt));
				}
			}

			file_put(file, "\n");
		}
	}
}

/**
 * Save the character dump to a file in the user directory.
 *
 * \param path is the path to the filename
 * \returns true on success, false otherwise.
 */
bool dump_save(const char *path)
{
	if (text_lines_to_file(path, write_character_dump)) {
		msg("Failed to create file %s.new", path);
		return false;
	}

	return true;
}

static const struct {
	keycode_t code;
	const char *label;
	enum player_display_mode mode;
} player_display_tabs[] = {
	{'1', "Basic", PLAYER_DISPLAY_MODE_BASIC},
	{'2', "Extra", PLAYER_DISPLAY_MODE_EXTRA},
};

static void change_player_display_mode(keycode_t code,
		enum player_display_mode *mode, bool *change)
{
	if (code == ARROW_LEFT || code == ARROW_RIGHT) {
		/* If the user pressed left or right arrow key,
		 * choose previous or next player info screen */
		for (size_t i = 0; i < N_ELEMENTS(player_display_tabs); i++) {
			if (*mode == player_display_tabs[i].mode) {

				size_t tab = 0;
				if (code == ARROW_LEFT) {
					tab = i > 0 ? i - 1 : N_ELEMENTS(player_display_tabs) - 1;
				} else if (code == ARROW_RIGHT) {
					tab = (i + 1) % N_ELEMENTS(player_display_tabs);
				}

				*mode = player_display_tabs[tab].mode;
				*change = true;

				return;
			}
		}
	} else {
		/* If the user pressed some other key, it was probably
		 * a key associated with a term tab; try to find it */
		for (size_t i = 0; i < N_ELEMENTS(player_display_tabs); i++) {
			if (code == player_display_tabs[i].code) {
				if (*mode != player_display_tabs[i].mode) {
					*mode = player_display_tabs[i].mode;
					*change = true;
				}

				return;
			}
		}
	}
}

static void player_display_term_push(enum player_display_mode mode)
{
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_TEXT,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);

	for (size_t i = 0; i < N_ELEMENTS(player_display_tabs); i++) {
		Term_add_tab(player_display_tabs[i].code,
				player_display_tabs[i].label,
				player_display_tabs[i].mode == mode ? COLOUR_WHITE : COLOUR_L_DARK,
				COLOUR_DARK);
	}
}

static void player_display_term_pop()
{
	Term_pop();
}

/**
 * View character and (potentially) change name
 */
void do_cmd_change_name(void)
{
	const char *prompt = "['c' to change name, 'f' to file, 'h' to change mode, or ESC]";

	show_prompt(prompt, false);

	enum player_display_mode mode = PLAYER_DISPLAY_MODE_BASIC;
	bool done = false;
	bool change = false;

	player_display_term_push(mode);
	display_player(mode);

	while (!done) {

		if (change) {
			player_display_term_pop();
			player_display_term_push(mode);
			display_player(mode);
			change = false;
		}

		ui_event event = inkey_simple();

		if (event.type == EVT_KBRD) {
			switch (event.key.code) {
				case ESCAPE:
					done = true;
					break;

				case 'c': {
					char namebuf[32] = "";
					if (get_character_name(namebuf, sizeof(namebuf))) {
						my_strcpy(op_ptr->full_name, namebuf, sizeof(op_ptr->full_name));
						change = true;
					}
					break;
				}

				case 'f': {
					char buf[1024];
					char fname[80];

					strnfmt(fname, sizeof(fname),
							"%s.txt", player_safe_name(player, false));

					if (get_file(fname, buf, sizeof(buf))) {
						if (dump_save(buf)) {
							msg("Character dump successful.");
						} else {
							msg("Character dump failed!");
						}
					}
					break;
				}
				
				case 'h': case ARROW_LEFT:
					change_player_display_mode(ARROW_LEFT, &mode, &change);
					break;

				case 'l': case ARROW_RIGHT: case ' ':
					change_player_display_mode(ARROW_RIGHT, &mode, &change);
					break;

				default:
					change_player_display_mode(event.key.code, &mode, &change);
					break;
			}
		} else if (event.type == EVT_MOUSE) {
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				change_player_display_mode(ARROW_RIGHT, &mode, &change);
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				done = true;
			}
		}
	}

	clear_prompt();
	player_display_term_pop();
}
