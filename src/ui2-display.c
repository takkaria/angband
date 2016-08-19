/**
 * \file ui-display.c
 * \brief Handles the setting up updating, and cleaning up of the game display.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007 Antony Sidwell
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
#include "cave.h"
#include "cmd-core.h"
#include "game-event.h"
#include "game-world.h"
#include "grafmode.h"
#include "hint.h"
#include "init.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "player.h"
#include "project.h"
#include "savefile.h"
#include "target.h"
#include "ui2-birth.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-map.h"
#include "ui2-mon-list.h"
#include "ui2-mon-lore.h"
#include "ui2-object.h"
#include "ui2-obj-list.h"
#include "ui2-output.h"
#include "ui2-player.h"
#include "ui2-prefs.h"
#include "ui2-store.h"
#include "ui2-term.h"

/* TODO UI2 #include "wizard.h" */

struct angband_term angband_cave;
struct angband_term angband_message_line;
struct angband_term angband_status_line;
struct angband_term angband_sidebar;

struct angband_terms angband_terms;

const char *atf_descr[ATF_MAX] = {
	#define ATF(a, b) b,
	#include "list-term-flags.h"
	#undef ATF
};

/**
 * There are a few functions installed to be triggered by several 
 * of the basic player events.  For convenience, these have been grouped 
 * in this list.
 */

static game_event_type player_events[] = {
	EVENT_RACE_CLASS,
	EVENT_PLAYERTITLE,
	EVENT_EXPERIENCE,
	EVENT_PLAYERLEVEL,
	EVENT_GOLD,
	EVENT_EQUIPMENT,  /* For equippy chars */
	EVENT_STATS,
	EVENT_HP,
	EVENT_MANA,
	EVENT_AC,

	EVENT_MONSTERHEALTH,

	EVENT_PLAYERSPEED,
	EVENT_DUNGEONLEVEL,
};

static game_event_type statusline_events[] = {
	EVENT_STUDYSTATUS,
	EVENT_STATUS,
	EVENT_STATE,
	EVENT_FEELING,
};

/**
 * Abbreviations of healthy stats
 */
const char *stat_names[STAT_MAX] = {
	"STR: ", "INT: ", "WIS: ", "DEX: ", "CON: "
};

/**
 * Abbreviations of damaged stats
 */
const char *stat_names_reduced[STAT_MAX] = {
	"Str: ", "Int: ", "Wis: ", "Dex: ", "Con: "
};

/**
 * Converts stat num into a six-char (right justified) string
 */
void cnv_stat(int val, char *out_val, size_t out_len)
{
	/* Stats above 18 need special treatment*/
	if (val > 18) {
		int bonus = (val - 18);

		if (bonus >= 220) {
			strnfmt(out_val, out_len, "18/***");
		} else if (bonus >= 100) {
			strnfmt(out_val, out_len, "18/%03d", bonus);
		} else {
			strnfmt(out_val, out_len, " 18/%02d", bonus);
		}
	} else {
		strnfmt(out_val, out_len, "    %2d", val);
	}
}

/**
 * ------------------------------------------------------------------------
 * Message line display functions
 * ------------------------------------------------------------------------
 */

/* "-more-" is 6 chars; 1 for preceding space */
#define MSG_MORE_LEN 7

static void message_more(int x)
{
	if (!auto_more()) {
		Term_addws(x, 0, MSG_MORE_LEN, COLOUR_L_BLUE, L"-more-");
		Term_flush_output();
		inkey_any();
	}

	Term_erase_line(0, 0);
}

/**
 * Output a message to the top line of the screen.
 *
 * Break long messages into multiple pieces
 * Allow multiple short messages to share the top line.
 * Prompt the user to make sure he has a chance to read them.
 */
static void display_message(game_event_type type, game_event_data *data, void *user)
{
	(void) type;

	if (data == NULL
			|| data->message.msg == NULL
			|| !character_generated)
	{
		return;
	}

	struct angband_term *aterm = user;

	Term_push(aterm->term);

	const int wrap = Term_width() - MSG_MORE_LEN;

	wchar_t buf[1024];
	int len = text_mbstowcs(buf, data->message.msg, sizeof(buf));

	if (aterm->offset_x > 0
			&& aterm->offset_x + len > wrap)
	{
		message_more(aterm->offset_x);
		aterm->offset_x = 0;
	}

	uint32_t color = message_type_color(data->message.type);
	wchar_t *ws = buf;

	while (len > wrap) {
		int split = wrap;

		for (int i = 0; i < wrap; i++) {
			if (ws[i] == L' ') {
				split = i;
			}
		}

		Term_addws(0, 0, split, color, ws);
		message_more(split + 1);

		ws += split;
		len -= split;
	}

	Term_addws(aterm->offset_x, 0, len, color, ws);
	Term_flush_output();

	aterm->offset_x += len + 1;

	Term_pop();
}

/*
 * Get a term from its index
 */
struct angband_term *term_by_index(int index)
{
	assert(index >= 0);

	if (index == angband_cave.index) {
		return &angband_cave;
	} else if (index == angband_message_line.index) {
		return &angband_message_line;
	} else if (index == angband_status_line.index) {
		return &angband_status_line;
	} else if (index == angband_sidebar.index) {
		return &angband_sidebar;
	} else {
		for (size_t i = 0; i < angband_terms.number; i++) {
			if (angband_terms.terms[i].index == index) {
				return &angband_terms.terms[i];
			}
		}
	}

	return NULL;
}

/*
 * Flush the contents of all terms and redraw the screen
 */
void flush_all_terms(void)
{
	struct angband_term *permanent_terms[] = {
		&angband_cave,
		&angband_message_line,
		&angband_status_line,
		&angband_sidebar
	};

	for (size_t i = 0; i < N_ELEMENTS(permanent_terms); i++) {
		Term_push(permanent_terms[i]->term);
		Term_flush_output();
		Term_pop();
	}

	for (size_t i = 0; i < angband_terms.number; i++) {
		Term_push(angband_terms.terms[i].term);
		Term_flush_output();
		Term_pop();
	}

	Term_push(angband_cave.term);
	Term_redraw_screen();
	Term_pop();
}

/**
 * Flush the output before displaying for emphasis
 */
static void bell_message(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	player->upkeep->redraw |= PR_MESSAGE;
}

/**
 * Print the "-more-" prompt
 */
static void message_flush(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct angband_term *aterm = user;

	Term_push(aterm->term);

	if (aterm->offset_x > 0) {
		message_more(aterm->offset_x);
		aterm->offset_x = 0;
	}

	Term_pop();
}

/*
 * skip next "-more-" prompt, if any
 */
void message_skip_more(void)
{
	angband_message_line.offset_x = 0;
}

/**
 * ------------------------------------------------------------------------
 * Sidebar display functions
 * ------------------------------------------------------------------------
 */

/**
 * Print character info at given row, column in a 13 char field
 */
static void prt_field(const char *info, struct loc coords)
{
	Term_erase_line(coords.x, coords.y);
	/* Dump the info itself */
	c_put_str(COLOUR_L_BLUE, info, coords);
}

/**
 * Print character stat in given row, column
 */
static void prt_stat(int stat, struct loc coords)
{
	char tmp[32];

	/* Injured or healthy stat */
	if (player->stat_cur[stat] < player->stat_max[stat]) {
		put_str(stat_names_reduced[stat], coords);
		cnv_stat(player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_YELLOW, tmp, loc(coords.x + 6, coords.y));
	} else {
		put_str(stat_names[stat], coords);
		cnv_stat(player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_L_GREEN, tmp, loc(coords.x + 6, coords.y));
	}

	/* Indicate natural maximum */
	if (player->stat_max[stat] == 18 + 100) {
		put_str("!", loc(coords.x + 3, coords.y));
	}
}

/**
 * Prints "title", including "wizard" or "winner" as needed.
 */
static void prt_title(struct loc coords)
{
	const char *p;

	/* Wizard, winner or neither */
	if (player->wizard) {
		p = "[=-WIZARD-=]";
	} else if (player->total_winner || (player->lev > PY_MAX_LEVEL)) {
		p = "***WINNER***";
	} else {
		p = player->class->title[(player->lev - 1) / 5];
	}

	prt_field(p, coords);
}

/**
 * Prints level
 */
static void prt_level(struct loc coords)
{
	char tmp[32];

	strnfmt(tmp, sizeof(tmp), "%6d", player->lev);

	if (player->lev >= player->max_lev) {
		put_str("LEVEL ", coords);
		coords.x += 6;
		c_put_str(COLOUR_L_GREEN, tmp, coords);
	} else {
		put_str("Level ", coords);
		coords.x += 6;
		c_put_str(COLOUR_YELLOW, tmp, coords);
	}
}

/**
 * Display the experience
 */
static void prt_exp(struct loc coords)
{
	char out_val[32];
	bool lev50 = player->lev == 50;

	long xp = (long) player->exp;

	/* Calculate XP for next level */
	if (!lev50) {
		xp = (long) (player_exp[player->lev - 1] * player->expfact / 100L) -
			player->exp;
	}

	/* Format XP */
	strnfmt(out_val, sizeof(out_val), "%8d", xp);

	if (player->exp >= player->max_exp) {
		put_str((lev50 ? "EXP" : "NXT"), coords);
		coords.x += 4;
		c_put_str(COLOUR_L_GREEN, out_val, coords);
	} else {
		put_str((lev50 ? "Exp" : "Nxt"), coords);
		coords.x += 4;
		c_put_str(COLOUR_YELLOW, out_val, coords);
	}
}

/**
 * Prints current gold
 */
static void prt_gold(struct loc coords)
{
	char tmp[32];

	put_str("AU ", coords);
	coords.x += 3;
	strnfmt(tmp, sizeof(tmp), "%9d", player->au);
	c_put_str(COLOUR_L_GREEN, tmp, coords);
}

/**
 * Equippy chars (ASCII representation of gear in equipment slot order)
 */
static void prt_equippy(struct loc coords)
{

	for (int i = 0; i < player->body.count; i++, coords.x++) {
		wchar_t ch = ' ';
		uint32_t attr = COLOUR_WHITE;

		struct object *obj = slot_object(player, i);

		if (obj) {
			ch = object_char(obj);
			attr = object_attr(obj);
		}

		Term_addwc(coords.x, coords.y, attr, ch);
	}
}

/**
 * Prints current AC
 */
static void prt_ac(struct loc coords)
{
	char tmp[32];

	put_str("Cur AC ", coords);
	coords.x += 7;
	strnfmt(tmp, sizeof(tmp), "%5d", 
			player->known_state.ac + player->known_state.to_a);
	c_put_str(COLOUR_L_GREEN, tmp, coords);
}

/**
 * Prints current hitpoints
 */
static void prt_hp(struct loc coords)
{
	char cur_hp[32], max_hp[32];
	byte color = player_hp_attr(player);

	put_str("HP ", coords);
	coords.x += 3;

	strnfmt(max_hp, sizeof(max_hp), "%4d", player->mhp);
	strnfmt(cur_hp, sizeof(cur_hp), "%4d", player->chp);

	c_put_str(color, cur_hp, coords);
	coords.x += 4;

	c_put_str(COLOUR_WHITE, "/", coords);
	coords.x += 1;

	c_put_str(COLOUR_L_GREEN, max_hp, coords);
}

/**
 * Prints players max/cur spell points
 */
static void prt_sp(struct loc coords)
{
	char cur_sp[32], max_sp[32];
	byte color = player_sp_attr(player);

	/* Do not show mana unless we should have some */
	if (player_has(player, PF_NO_MANA) || 
		(player->lev < player->class->magic.spell_first))
		return;

	put_str("SP ", coords);
	coords.x += 3;

	strnfmt(max_sp, sizeof(max_sp), "%4d", player->msp);
	strnfmt(cur_sp, sizeof(cur_sp), "%4d", player->csp);

	/* Show mana */
	c_put_str(color, cur_sp, coords);
	coords.x += 4;

	c_put_str(COLOUR_WHITE, "/", coords);
	coords.x += 1;

	c_put_str(COLOUR_L_GREEN, max_sp, coords);
}

/**
 * Calculate the monster bar color separately, for ports.
 */
uint32_t monster_health_attr(const struct monster *mon)
{
	/* Default to almost dead */
	uint32_t attr = COLOUR_RED;

	if (!mon) {
		/* Not tracking */
		attr = COLOUR_DARK;
	} else if (!mflag_has(mon->mflag, MFLAG_VISIBLE)
			|| mon->hp < 0
			|| player->timed[TMD_IMAGE])
	{
		/* The monster health is unknown */
		attr = COLOUR_WHITE;
	} else {
		/* Extract the percent of health */
		int pct = 100L * mon->hp / mon->maxhp;

		if (pct >= 10) {
			/* Badly wounded */
			attr = COLOUR_L_RED;
		}
		if (pct >= 25) {
			/* Wounded */
			attr = COLOUR_ORANGE;
		}
		if (pct >= 60) {
			/* Somewhat wounded */
			attr = COLOUR_YELLOW;
		}
		if (pct >= 100) {
			/* Healthy */
			attr = COLOUR_L_GREEN;
		}
		if (mon->m_timed[MON_TMD_FEAR]) {
			/* Afraid */
			attr = COLOUR_VIOLET;
		}
		if (mon->m_timed[MON_TMD_CONF]) {
			/* Confused */
			attr = COLOUR_UMBER;
		}
		if (mon->m_timed[MON_TMD_STUN]) {
			/* Stunned */
			attr = COLOUR_L_BLUE;
		}
		if (mon->m_timed[MON_TMD_SLEEP]) {
			/* Asleep */
			attr = COLOUR_BLUE;
		}
	}
	
	return attr;
}

/**
 * Redraw the "monster health bar"
 *
 * The "monster health bar" provides visual feedback on the health
 * of the monster currently being tracked.  There are several ways
 * to track a monster, including targetting it, attacking it, and
 * affecting it (and nobody else) with a ranged attack.  When nothing
 * is being tracked, we clear the health bar.  If the monster being
 * tracked is not currently visible, a special health bar is shown.
 */
static void prt_health(struct loc coords)
{
	const struct monster *mon = player->upkeep->health_who;

	/* Not tracking */
	if (!mon) {
		/* Erase the health bar */
		Term_erase(coords.x, coords.y, 12);
		return;
	}

	uint32_t attr = monster_health_attr(mon);

	/* Tracking an unseen, hallucinatory, or dead monster */
	if (!mflag_has(mon->mflag, MFLAG_VISIBLE) /* Unseen */
			|| player->timed[TMD_IMAGE]     /* Hallucination */
			|| mon->hp < 0)                 /* Dead (?) */
	{
		/* The monster health is unknown */
		Term_adds(coords.x, coords.y, 12, attr, "[----------]");
	} else {
		/* Extract the percent of health */
		int pct = 100L * mon->hp / mon->maxhp;
		/* Convert percent into health */
		int len = (pct < 10) ? 1 : (pct < 90) ? (pct / 10 + 1) : 10;
		/* Default to unknown */
		Term_adds(coords.x, coords.y, 12, COLOUR_WHITE, "[----------]");
		/* Dump the current health (use '*' symbols) */
		Term_adds(coords.x + 1, coords.y, len, attr, "**********");
	}
}

/**
 * Prints the speed of a character.
 */
static void prt_speed(struct loc coords)
{
	int speed = player->state.speed;

	uint32_t attr = COLOUR_WHITE;
	const char *type = NULL;

	/* 110 is normal speed, and requires no display */
	if (speed > 110) {
		attr = COLOUR_L_GREEN;
		type = "Fast";
	} else if (speed < 110) {
		attr = COLOUR_L_UMBER;
		type = "Slow";
	}

	char buf[32] = {0};

	if (type) {
		strnfmt(buf, sizeof(buf), "%s (%+d)", type, (speed - 110));
	}

	c_put_str(attr, format("%-10s", buf), coords);
}

/**
 * Prints depth in stat area
 */
static void prt_depth(struct loc coords)
{
	char depths[32];

	if (!player->depth) {
		my_strcpy(depths, "Town", sizeof(depths));
	} else {
		strnfmt(depths, sizeof(depths), "%d' (L%d)",
				player->depth * 50, player->depth);
	}

	/* Right-adjust the depth, and clear old values */
	put_str(format("%-13s", depths), coords);
}

/**
 * Some simple wrapper functions
 */
static void prt_str(struct loc coords)   { prt_stat(STAT_STR, coords); }
static void prt_dex(struct loc coords)   { prt_stat(STAT_DEX, coords); }
static void prt_wis(struct loc coords)   { prt_stat(STAT_WIS, coords); }
static void prt_int(struct loc coords)   { prt_stat(STAT_INT, coords); }
static void prt_con(struct loc coords)   { prt_stat(STAT_CON, coords); }
static void prt_race(struct loc coords)  { prt_field(player->race->name, coords); } 
static void prt_class(struct loc coords) { prt_field(player->class->name, coords); }

/**
 * Struct of sidebar handlers.
 */
static const struct side_handler_t {
	void (*hook)(struct loc coords);
	int priority;         /* 0 is most important */
	game_event_type type; /* PR_* flag this corresponds to */
} side_handlers[] = {
	{ prt_race,    19, EVENT_RACE_CLASS },
	{ prt_title,   18, EVENT_PLAYERTITLE },
	{ prt_class,   22, EVENT_RACE_CLASS },
	{ prt_level,   10, EVENT_PLAYERLEVEL },
	{ prt_exp,     16, EVENT_EXPERIENCE },
	{ prt_gold,    11, EVENT_GOLD },
	{ prt_equippy, 17, EVENT_EQUIPMENT },
	{ prt_str,      6, EVENT_STATS },
	{ prt_int,      5, EVENT_STATS },
	{ prt_wis,      4, EVENT_STATS },
	{ prt_dex,      3, EVENT_STATS },
	{ prt_con,      2, EVENT_STATS },
	{ NULL,        15, 0 },
	{ prt_ac,       7, EVENT_AC },
	{ prt_hp,       8, EVENT_HP },
	{ prt_sp,       9, EVENT_MANA },
	{ NULL,        21, 0 },
	{ prt_health,  12, EVENT_MONSTERHEALTH },
	{ NULL,        20, 0 },
	{ NULL,        22, 0 },
	{ prt_speed,   13, EVENT_PLAYERSPEED },  /* Slow (-NN) / Fast (+NN) */
	{ prt_depth,   14, EVENT_DUNGEONLEVEL }, /* Lev NNN / NNNN ft */
};

/**
 * This prints the sidebar, using a clever method which means that it will only
 * print as much as can be displayed.
 *
 * Each row is given a priority; higher numbers are less important and
 * lower numbers are more important. As the screen gets smaller, the rows start to
 * disappear in the order of lowest to highest importance.
 */
static void update_sidebar(game_event_type type,
		game_event_data *data, void *user)
{
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	int height = Term_height();
	struct loc coords = {0, 0};

	for (size_t i = 0; i < N_ELEMENTS(side_handlers) && coords.y < height; i++) {
		const struct side_handler_t *handler = &side_handlers[i];
		assert(handler->priority >= 0);
		/* If this is high enough priority, display it */
		if (handler->priority < height) {
			if (handler->type == type && handler->hook != NULL) {
				handler->hook(coords);
			}
			coords.y++;
		}
	}

	Term_flush_output();
	Term_pop();
}

/**
 * Redraw player, since the player's color indicates approximate health.  Note
 * that using this command is only for when graphics mode is off, as
 * otherwise it causes the character to be a black square.
 */
static void hp_colour_change(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	if ((OPT(hp_changes_color)) && use_graphics == GRAPHICS_NONE) {
		square_light_spot(cave, player->py, player->px);
	}
}

/**
 * ------------------------------------------------------------------------
 * Status line display functions
 * ------------------------------------------------------------------------ */

struct state_info {
	int value;
	const char *str;
	size_t len;
	uint32_t attr;
};

#define STATE_INFO(value, str, attr) \
	{ (value), (str), sizeof(str), (attr) }

/**
 * TMD_CUT descriptions
 */
static const struct state_info cut_data[] = {
	STATE_INFO(1000, "Mortal wound", COLOUR_L_RED),
	STATE_INFO( 200, "Deep gash",    COLOUR_RED),
	STATE_INFO( 100, "Severe cut",   COLOUR_RED),
	STATE_INFO(  50, "Nasty cut",    COLOUR_ORANGE),
	STATE_INFO(  25, "Bad cut",      COLOUR_ORANGE),
	STATE_INFO(  10, "Light cut",    COLOUR_YELLOW),
	STATE_INFO(   0, "Graze",        COLOUR_YELLOW)
};

/**
 * TMD_STUN descriptions
 */
static const struct state_info stun_data[] = {
	STATE_INFO(100, "Knocked out", COLOUR_RED),
	STATE_INFO( 50, "Heavy stun",  COLOUR_ORANGE),
	STATE_INFO(  0, "Stun",        COLOUR_ORANGE)
};

/**
 * player->hunger descriptions
 */
static const struct state_info hunger_data[] = {
	STATE_INFO( PY_FOOD_FAINT, "Faint",    COLOUR_RED),
	STATE_INFO( PY_FOOD_WEAK,  "Weak",     COLOUR_ORANGE),
	STATE_INFO( PY_FOOD_ALERT, "Hungry",   COLOUR_YELLOW),
	STATE_INFO( PY_FOOD_FULL,  "",         COLOUR_L_GREEN),
	STATE_INFO( PY_FOOD_MAX,   "Full",     COLOUR_L_GREEN)
};

/**
 * For the various TMD_* effects
 */
static const struct state_info effects[] = {
	STATE_INFO( TMD_BLIND,     "Blind",      COLOUR_ORANGE),
	STATE_INFO( TMD_PARALYZED, "Paralyzed!", COLOUR_RED),
	STATE_INFO( TMD_CONFUSED,  "Confused",   COLOUR_ORANGE),
	STATE_INFO( TMD_AFRAID,    "Afraid",     COLOUR_ORANGE),
	STATE_INFO( TMD_TERROR,    "Terror",     COLOUR_RED),
	STATE_INFO( TMD_IMAGE,     "Halluc",     COLOUR_ORANGE),
	STATE_INFO( TMD_POISONED,  "Poisoned",   COLOUR_ORANGE),
	STATE_INFO( TMD_PROTEVIL,  "ProtEvil",   COLOUR_L_GREEN),
	STATE_INFO( TMD_SPRINT,    "Sprint",     COLOUR_L_GREEN),
	STATE_INFO( TMD_TRAPSAFE,  "TrapSafe",   COLOUR_L_GREEN),
	STATE_INFO( TMD_TELEPATHY, "ESP",        COLOUR_L_BLUE),
	STATE_INFO( TMD_INVULN,    "Invuln",     COLOUR_L_GREEN),
	STATE_INFO( TMD_HERO,      "Hero",       COLOUR_L_GREEN),
	STATE_INFO( TMD_SHERO,     "Berserk",    COLOUR_L_GREEN),
	STATE_INFO( TMD_BOLD,      "Bold",       COLOUR_L_GREEN),
	STATE_INFO( TMD_STONESKIN, "Stone",      COLOUR_L_GREEN),
	STATE_INFO( TMD_SHIELD,    "Shield",     COLOUR_L_GREEN),
	STATE_INFO( TMD_BLESSED,   "Blssd",      COLOUR_L_GREEN),
	STATE_INFO( TMD_SINVIS,    "SInvis",     COLOUR_L_GREEN),
	STATE_INFO( TMD_SINFRA,    "Infra",      COLOUR_L_GREEN),
	STATE_INFO( TMD_OPP_ACID,  "RAcid",      COLOUR_SLATE),
	STATE_INFO( TMD_OPP_ELEC,  "RElec",      COLOUR_BLUE),
	STATE_INFO( TMD_OPP_FIRE,  "RFire",      COLOUR_RED),
	STATE_INFO( TMD_OPP_COLD,  "RCold",      COLOUR_WHITE),
	STATE_INFO( TMD_OPP_POIS,  "RPois",      COLOUR_GREEN),
	STATE_INFO( TMD_OPP_CONF,  "RConf",      COLOUR_VIOLET),
	STATE_INFO( TMD_AMNESIA,   "Amnesiac",   COLOUR_ORANGE),
	STATE_INFO( TMD_SCRAMBLE,  "Scrambled",  COLOUR_VIOLET)
};

/**
 * Print recall status.
 */
static size_t prt_recall(struct loc coords)
{
	if (player->word_recall) {
		c_put_str(COLOUR_WHITE, "Recall", coords);
		return sizeof("Recall");
	}

	return 0;
}

/**
 * Print deep descent status.
 */
static size_t prt_descent(struct loc coords)
{
	if (player->deep_descent) {
		c_put_str(COLOUR_WHITE, "Descent", coords);
		return sizeof("Descent");
	}

	return 0;
}

static size_t prt_data(struct state_info info, struct loc coords)
{
	if (info.str[0]) {
		c_put_str(info.attr, info.str, coords);
		return info.len;
	} else {
		return 0;
	}
}

/**
 * Print cut indicator.
 */
static size_t prt_cut(struct loc coords)
{
	for (size_t i = 0; i < N_ELEMENTS(cut_data); i++) {
		if (player->timed[TMD_CUT] > cut_data[i].value) {
			return prt_data(cut_data[i], coords);
		}
	}

	return 0;
}

/**
 * Print stun indicator.
 */
static size_t prt_stun(struct loc coords)
{
	for (size_t i = 0; i < N_ELEMENTS(stun_data); i++) {
		if (player->timed[TMD_STUN] > stun_data[i].value) {
			return prt_data(stun_data[i], coords);
		}
	}

	return 0;
}

/**
 * Prints status of hunger
 */
static size_t prt_hunger(struct loc coords)
{
	for (size_t i = 0; i <= N_ELEMENTS(hunger_data); i++) {
		if (player->food <= hunger_data[i].value) {
			return prt_data(hunger_data[i], coords);
		}
	}

	return 0;
}

/**
 * Prints Resting, or 'count' status
 * Display is always exactly 10 characters wide (see below)
 *
 * This function was a major bottleneck when resting, so a lot of
 * the text formatting code was optimized in place below.
 */
static size_t prt_state(struct loc coords)
{
	uint32_t attr = COLOUR_WHITE;
	char text[16] = "Rest      ";

	/* Displayed states are resting and repeating */
	if (player_is_resting(player)) {
		int i;
		int n = player_resting_count(player);

		/* Display according to length or intent of rest */
		if (n >= 1000) {
			i = n / 100;
			text[9] = '0';
			text[8] = '0';
			text[7] = I2D(i % 10);
			if (i >= 10) {
				i = i / 10;
				text[6] = I2D(i % 10);
				if (i >= 10)
					text[5] = I2D(i / 10);
			}
		} else if (n >= 100) {
			i = n;
			text[9] = I2D(i % 10);
			i = i / 10;
			text[8] = I2D(i % 10);
			text[7] = I2D(i / 10);
		} else if (n >= 10) {
			i = n;
			text[9] = I2D(i % 10);
			text[8] = I2D(i / 10);
		} else if (n > 0) {
			i = n;
			text[9] = I2D(i);
		} else if (n == REST_ALL_POINTS) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '*';
		} else if (n == REST_COMPLETE) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '&';
		} else if (n == REST_SOME_POINTS) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '!';
		}
		c_put_str(attr, text, coords);

		return strlen(text) + 1;
	} else if (cmd_get_nrepeats()) {
		int nrepeats = cmd_get_nrepeats();

		if (nrepeats > 999) {
			strnfmt(text, sizeof(text), "Rep. %3d00", nrepeats / 100);
		} else {
			strnfmt(text, sizeof(text), "Repeat %3d", nrepeats);
		}
		c_put_str(attr, text, coords);

		return strlen(text) + 1;
	}

	return 0;
}

/* Colors used to display each obj feeling 	*/
static const uint32_t obj_feeling_color[] = {
	COLOUR_WHITE,    /* "Looks like any other level." */
	COLOUR_L_PURPLE, /* "You sense an item of wondrous power!" */
	COLOUR_L_RED,    /* "There are superb treasures here." */
	COLOUR_ORANGE,   /* "There are excellent treasures here." */
	COLOUR_YELLOW,   /* "There are very good treasures here." */
	COLOUR_YELLOW,   /* "There are good treasures here." */
	COLOUR_L_GREEN,  /* "There may be something worthwhile here." */
	COLOUR_L_GREEN,  /* "There may not be much interesting here." */
	COLOUR_L_GREEN,  /* "There aren't many treasures here." */
	COLOUR_L_BLUE,   /* "There are only scraps of junk here." */
	COLOUR_L_BLUE    /* "There are naught but cobwebs here." */
};

/* Colors used to display each monster feeling */
static const uint32_t mon_feeling_color[] = {
	COLOUR_WHITE,  /* "You are still uncertain about this place" */
	COLOUR_RED,    /* "Omens of death haunt this place" */
	COLOUR_ORANGE, /* "This place seems murderous" */
	COLOUR_ORANGE, /* "This place seems terribly dangerous" */
	COLOUR_YELLOW, /* "You feel anxious about this place" */
	COLOUR_YELLOW, /* "You feel nervous about this place" */
	COLOUR_GREEN,  /* "This place does not seem too risky" */
	COLOUR_GREEN,  /* "This place seems reasonably safe" */
	COLOUR_BLUE,   /* "This seems a tame, sheltered place" */
	COLOUR_BLUE,   /* "This seems a quiet, peaceful place" */
};

/**
 * Prints level feelings at status if they are enabled.
 */
static size_t prt_level_feeling(struct loc coords)
{
	/* Don't show feelings for cold-hearted characters
	 * no useful feelings in town */
	if (!OPT(birth_feelings) || player->depth == 0) {
		return 0;
	}

	/*
	 * Convert object feeling to a symbol easier to parse for a human.
	 * 0 -> * "Looks like any other level."
	 * 1 -> $ "you sense an item of wondrous power!" (special feeling)
	 * 2 to 10 are feelings from 2 meaning superb feeling to 10
	 * meaning naught but cowebs.
	 *
	 * It is easier for the player to have poor feelings as a
	 * low number and superb feelings as a higher one. So for
	 * display we reverse this numbers and substract 1.
	 * Thus (2-10) becomes (1-9 reversed)
	 *
	 * But before that check if the player has explored enough
	 * to get a feeling. If not display as '?'
	 */

	u16b obj_feeling = cave->feeling / 10;
	uint32_t obj_feeling_color_print;
	char obj_feeling_str[6];

	if (cave->feeling_squares < z_info->feeling_need) {
		my_strcpy(obj_feeling_str, "?", sizeof(obj_feeling_str));
		obj_feeling_color_print = COLOUR_WHITE;
	} else {
		obj_feeling_color_print = obj_feeling_color[obj_feeling];

		if (obj_feeling == 0) {
			my_strcpy(obj_feeling_str, "*", sizeof(obj_feeling_str));
		} else if (obj_feeling == 1) {
			my_strcpy(obj_feeling_str, "$", sizeof(obj_feeling_str));
		} else {
			strnfmt(obj_feeling_str, sizeof(obj_feeling_str),
					"%d", 11 - obj_feeling);
		}
	}

	/** 
	 * Convert monster feeling to a symbol easier to parse for a human.
	 * 0 -> ? . Monster feeling should never be 0, but we check it just in case.
	 * 1 to 9 are feelings from omens of death to quiet, paceful.
	 * We also reverse this so that what we show is a danger feeling.
	 */
	u16b mon_feeling = cave->feeling - (10 * obj_feeling);
	char mon_feeling_str[6];

	if (mon_feeling == 0) {
		my_strcpy(mon_feeling_str, "?", sizeof(mon_feeling_str));
	} else {
		strnfmt(mon_feeling_str, sizeof(mon_feeling_str),
				"%d", 10 - mon_feeling);
	}

	/* Display it */

	const int oldx = coords.x;

	c_put_str(COLOUR_WHITE, "LF:", coords);
	coords.x += 3;

	c_put_str(mon_feeling_color[mon_feeling], mon_feeling_str, coords);
	coords.x += strlen(mon_feeling_str);

	c_put_str(COLOUR_WHITE, "-", coords);
	coords.x++;

	c_put_str(obj_feeling_color_print, obj_feeling_str, coords);
	coords.x += strlen(obj_feeling_str) + 1;

	return coords.x - oldx;
}

/**
 * Print how many spells the player can study.
 */
static size_t prt_study(struct loc coords)
{
	if (player->upkeep->new_spells == 0) {
		return 0;
	}

	uint32_t attr = player_book_has_unlearned_spells(player) ? 
		COLOUR_WHITE : COLOUR_L_DARK;
	char *str = format("Study (%d)", player->upkeep->new_spells);

	c_put_str(attr, str, coords);

	return strlen(str) + 1;
}

/**
 * Print all timed effects.
 */
static size_t prt_tmd(struct loc coords)
{
	const int oldx = coords.x;

	for (size_t i = 0; i < N_ELEMENTS(effects); i++)
		if (player->timed[effects[i].value]) {
			c_put_str(effects[i].attr, effects[i].str, coords);
			coords.x += effects[i].len;
		}

	return coords.x - oldx;
}

/**
 * Print "unignoring" status
 */
static size_t prt_unignore(struct loc coords)
{
	if (player->unignoring) {
		put_str("Unignoring", coords);
		return sizeof("Unignoring");
	}

	return 0;
}

/**
 * Descriptive typedef for status handlers
 */
typedef size_t status_f(struct loc coords);

static status_f *status_handlers[] = {
	prt_level_feeling,
	prt_unignore,
	prt_recall,
	prt_descent,
	prt_state,
	prt_cut,
	prt_stun,
	prt_hunger,
	prt_study,
	prt_tmd
};

/**
 * Print the status line.
 */
static void update_statusline(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	Term_erase_line(0, 0);

	struct loc coords = {0, 0};
	for (size_t i = 0; i < N_ELEMENTS(status_handlers); i++) {
		coords.x += status_handlers[i](coords);
	}

	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Map redraw.
 * ------------------------------------------------------------------------ */

#ifdef MAP_DEBUG
static void trace_map_updates(game_event_type type, game_event_data *data,
							  void *user)
{
	if (data->point.x == -1 && data->point.y == -1) {
		printf("Redraw whole map\n");
	} else {
		printf("Redraw (%i, %i)\n", data->point.x, data->point.y);
	}
}
#endif

/**
 * Update either a single map grid or a whole map
 */
static void update_maps(game_event_type type, game_event_data *data, void *user)
{
	(void) type;

	Term_push(ANGBAND_TERM(user)->term);

	if (data->point.x == -1 && data->point.y == -1) {
		/* This signals a whole-map redraw. */
		print_map(ANGBAND_TERM(user));
	} else {
		/* Single point to be redrawn */

		/* Location relative to panel */
		int relx = data->point.x - ANGBAND_TERM(user)->offset_x;
		int rely = data->point.y - ANGBAND_TERM(user)->offset_y;

		if (Term_point_ok(relx, rely)) {
			struct grid_data g;
			map_info(data->point.y, data->point.x, &g);

			struct term_point point;
			grid_data_as_point(&g, &point);
			Term_set_point(relx, rely, point);
#ifdef MAP_DEBUG
			/* Plot 'spot' updates in light green to make them visible */
			point.fg_attr = COLOUR_L_GREEN;
			Term_set_point(relx, rely, point);
#endif
		}
	}

	if (player->upkeep->update & PU_PANEL && OPT(center_player)) {
		struct loc coords = {
			.x = player->px - Term_width() / 2,
			.y = player->py - Term_height() / 2
		};
		if (panel_should_modify(ANGBAND_TERM(user), coords)) {
			Term_pop();
			return;
		}
	}

	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Animations.
 * ------------------------------------------------------------------------ */

static byte flicker = 0;
static byte color_flicker[MAX_COLORS][3] = 
{
	{COLOUR_DARK, COLOUR_L_DARK, COLOUR_L_RED},
	{COLOUR_WHITE, COLOUR_L_WHITE, COLOUR_L_BLUE},
	{COLOUR_SLATE, COLOUR_WHITE, COLOUR_L_DARK},
	{COLOUR_ORANGE, COLOUR_YELLOW, COLOUR_L_RED},
	{COLOUR_RED, COLOUR_L_RED, COLOUR_L_PINK},
	{COLOUR_GREEN, COLOUR_L_GREEN, COLOUR_L_TEAL},
	{COLOUR_BLUE, COLOUR_L_BLUE, COLOUR_SLATE},
	{COLOUR_UMBER, COLOUR_L_UMBER, COLOUR_MUSTARD},
	{COLOUR_L_DARK, COLOUR_SLATE, COLOUR_L_VIOLET},
	{COLOUR_WHITE, COLOUR_SLATE, COLOUR_L_WHITE},
	{COLOUR_L_PURPLE, COLOUR_PURPLE, COLOUR_L_VIOLET},
	{COLOUR_YELLOW, COLOUR_L_YELLOW, COLOUR_MUSTARD},
	{COLOUR_L_RED, COLOUR_RED, COLOUR_L_PINK},
	{COLOUR_L_GREEN, COLOUR_L_TEAL, COLOUR_GREEN},
	{COLOUR_L_BLUE, COLOUR_DEEP_L_BLUE, COLOUR_BLUE_SLATE},
	{COLOUR_L_UMBER, COLOUR_UMBER, COLOUR_MUD},
	{COLOUR_PURPLE, COLOUR_VIOLET, COLOUR_MAGENTA},
	{COLOUR_VIOLET, COLOUR_L_VIOLET, COLOUR_MAGENTA},
	{COLOUR_TEAL, COLOUR_L_TEAL, COLOUR_L_GREEN},
	{COLOUR_MUD, COLOUR_YELLOW, COLOUR_UMBER},
	{COLOUR_L_YELLOW, COLOUR_WHITE, COLOUR_L_UMBER},
	{COLOUR_MAGENTA, COLOUR_L_PINK, COLOUR_L_RED},
	{COLOUR_L_TEAL, COLOUR_L_WHITE, COLOUR_TEAL},
	{COLOUR_L_VIOLET, COLOUR_L_PURPLE, COLOUR_VIOLET},
	{COLOUR_L_PINK, COLOUR_L_RED, COLOUR_L_WHITE},
	{COLOUR_MUSTARD, COLOUR_YELLOW, COLOUR_UMBER},
	{COLOUR_BLUE_SLATE, COLOUR_BLUE, COLOUR_SLATE},
	{COLOUR_DEEP_L_BLUE, COLOUR_L_BLUE, COLOUR_BLUE},
};

static byte get_flicker(byte a)
{
	switch(flicker % 3) {
		case 1:
			return color_flicker[a][1];
		case 2:
			return color_flicker[a][2];
	}

	return a;
}

/**
 * This animates monsters and/or items as necessary.
 */
static void do_animation(void)
{
	for (int i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		byte attr;

		if (!mon || !mon->race || !mflag_has(mon->mflag, MFLAG_VISIBLE)) {
			continue;
		} else if (rf_has(mon->race->flags, RF_ATTR_MULTI)) {
			attr = randint1(BASIC_COLORS - 1);
		} else if (rf_has(mon->race->flags, RF_ATTR_FLICKER)) {
			attr = get_flicker(monster_x_attr[mon->race->ridx]);
		} else {
			continue;
		}

		mon->attr = attr;
		player->upkeep->redraw |= (PR_MAP | PR_MONLIST);
	}

	flicker++;
}

/**
 * Update animations on request
 */
static void animate(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	if (!player_is_resting(player)) {
		do_animation();

		Term_push(angband_cave.term);
		Term_flush_output();
		Term_redraw_screen();
		Term_pop();
	}
}

static void redraw(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	Term_redraw_screen();
}

/**
 * This is used when the user is idle to allow for simple animations.
 * Currently the only thing it really does is animate shimmering monsters.
 */
void idle_update(void)
{
	if (!character_dungeon
			|| !OPT(animate_flicker)
			|| use_graphics != GRAPHICS_NONE)
	{
		return;
	}

	do_animation();
	redraw_stuff(player);

	Term_flush_output();
	Term_redraw_screen();
}

/**
 * Find the attr/char pair to use for a spell effect
 * It is moving (or has moved) from old to new; if the distance is not
 * one, we (may) return "*".
 */
static void bolt_pict(struct loc old, struct loc new,
		int typ, uint32_t *attr, wchar_t *ch)
{
	int motion;

	/* Convert co-ordinates into motion */
	if (new.y == old.y && new.x == old.x) {
		motion = BOLT_NO_MOTION;
	} else if (new.x == old.x) {
		motion = BOLT_0;
	} else if (new.y - old.y == old.x - new.x) {
		motion = BOLT_45;
	} else if (new.y == old.y) {
		motion = BOLT_90;
	} else if (new.y - old.y == new.x - old.x) {
		motion = BOLT_135;
	} else {
		motion = BOLT_NO_MOTION;
	}

	/* Decide on output char */
	if (use_graphics == GRAPHICS_NONE) {
		const wchar_t chars[] = L"*|/-\\";

		*attr = gf_color(typ);
		*ch = chars[motion];
	} else {
		*attr = gf_to_attr[typ][motion];
		*ch = gf_to_char[typ][motion];
	}
}

/**
 * Draw an explosion
 */
static void display_explosion(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;

	Term_push(ANGBAND_TERM(user)->term);

	bool new_radius = false;
	bool drawn = false;

	int gf_type = data->explosion.gf_type;
	int num_grids = data->explosion.num_grids;
	int *distance_to_grid = data->explosion.distance_to_grid;
	bool drawing = data->explosion.drawing;
	bool *player_sees_grid = data->explosion.player_sees_grid;
	struct loc *blast_grid = data->explosion.blast_grid;

	/* Draw the blast from inside out */
	for (int i = 0; i < num_grids; i++) {
		/* Extract the location */
		struct loc coords = {
			.x = blast_grid[i].x,
			.y = blast_grid[i].y
		};

		/* Only do visuals if the player can see the blast */
		if (player_sees_grid[i]) {
			uint32_t attr;
			wchar_t ch;

			drawn = true;

			/* Obtain the explosion pict */
			bolt_pict(coords, coords, gf_type, &attr, &ch);

			/* Just display the pict, ignoring what was under it */
			print_rel(ANGBAND_TERM(user), attr, ch, coords);
		}

		/* Check for new radius, taking care not to overrun array */
		if (i == num_grids - 1
				|| distance_to_grid[i + 1] > distance_to_grid[i])
		{
			new_radius = true;
		}

		/* We have all the grids at the current radius, so draw it */
		if (new_radius) {
			/* Flush all the grids at this radius */
			Term_flush_output();
			/* Delay to show this radius appearing */
			if (drawn || drawing) {
				Term_redraw_screen();
				Term_delay(op_ptr->delay_factor);
			}

			new_radius = false;
		}
	}

	if (drawn) {
		/* Erase the explosion drawn above */
		for (int i = 0; i < num_grids; i++) {
			int x = blast_grid[i].x;
			int y = blast_grid[i].y;

			/* Erase visible, valid grids */
			if (player_sees_grid[i]) {
				event_signal_point(EVENT_MAP, x, y);
			}
		}

		/* Flush the explosion */
		Term_flush_output();
		Term_redraw_screen();
	}

	Term_pop();
}

/**
 * Draw a moving spell effect (bolt or beam)
 */
static void display_bolt(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;

	Term_push(ANGBAND_TERM(user)->term);

	int gf_type = data->bolt.gf_type;
	bool drawing = data->bolt.drawing;
	bool seen = data->bolt.seen;
	bool beam = data->bolt.beam;
	struct loc old = {
		.x = data->bolt.ox,
		.y = data->bolt.oy
	};
	struct loc new = {
		.x = data->bolt.x,
		.y = data->bolt.y
	};

	/* Only do visuals if the player can "see" the bolt */
	if (seen) {
		uint32_t attr;
		wchar_t ch;
		bolt_pict(old, new, gf_type, &attr, &ch);

		print_rel(ANGBAND_TERM(user), attr, ch, new);

		Term_flush_output();
		Term_redraw_screen();
		Term_delay(op_ptr->delay_factor);

		event_signal_point(EVENT_MAP, new.x, new.y);

		Term_flush_output();
		Term_redraw_screen();

		/* Display "beam" grids */
		if (beam) {
			bolt_pict(new, new, gf_type, &attr, &ch);
			print_rel(ANGBAND_TERM(user), attr, ch, new);
		}
	} else if (drawing) {
		/* Delay for consistency */
		Term_delay(op_ptr->delay_factor);
	}

	Term_pop();
}

/**
 * Draw a moving missile
 */
static void display_missile(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) user;

	Term_push(ANGBAND_TERM(user)->term);

	struct object *obj = data->missile.obj;
	bool seen = data->missile.seen;
	struct loc coords = {
		.x = data->missile.x,
		.y = data->missile.y
	};

	/* Only do visuals if the player can "see" the missile */
	if (seen) {
		print_rel(ANGBAND_TERM(user), object_attr(obj), object_char(obj), coords);

		Term_flush_output();
		Term_redraw_screen();
		Term_delay(op_ptr->delay_factor);

		event_signal_point(EVENT_MAP, coords.x, coords.y);

		Term_flush_output();
		Term_redraw_screen();
	}

	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Subwindow displays
 * ------------------------------------------------------------------------ */

/**
 * true when we're supposed to display the equipment in the inventory 
 * window, or vice-versa.
 */
static bool flip_inven;

static void update_inven_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	if (!flip_inven) {
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
	} else {
		show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
	}

	Term_flush_output();
	Term_pop();
}

static void update_equip_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	if (!flip_inven) {
		show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
	} else {
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
	}

	Term_flush_output();
	Term_pop();
}

/**
 * Flip "inven" and "equip" in any sub-windows
 */
void toggle_inven_equip(void)
{
	/* Change the actual setting */
	flip_inven = !flip_inven;

	/* Redraw any subwindows showing the inventory/equipment lists */
	for (size_t i = 0; i < angband_terms.number; i++) {
		struct angband_term *aterm = &angband_terms.terms[i];

		if (atf_has(aterm->flags, ATF_INVEN)) {
			Term_push(aterm->term);

			if (!flip_inven) {
				show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
			} else {
				show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
			}

			Term_flush_output();
			Term_pop();
		} else if (atf_has(aterm->flags, ATF_EQUIP)) {
			Term_push(aterm->term);

			if (!flip_inven) {
				show_equip(OLIST_WINDOW | OLIST_WEIGHT, NULL);
			} else {
				show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
			}

			Term_flush_output();
			Term_pop();
		}
	}
}

static void update_itemlist_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);
	Term_clear();

	object_list_show_subwindow();

	Term_flush_output();
	Term_pop();
}

static void update_monlist_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);
	Term_clear();

	monster_list_show_subwindow();

	Term_flush_output();
	Term_pop();
}

static void update_monster_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	/* Display monster race info */
	if (player->upkeep->monster_race) {
		lore_show_subwindow(player->upkeep->monster_race, 
							get_lore(player->upkeep->monster_race));
	}

	Term_flush_output();
	Term_pop();
}

static void update_object_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);
	
	if (player->upkeep->object != NULL) {
		display_object_recall(player->upkeep->object);
	} else if (player->upkeep->object_kind) {
		display_object_kind_recall(player->upkeep->object_kind);
	}

	Term_flush_output();
	Term_pop();
}

static void update_messages_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	int width;
	int height;
	Term_get_size(&width, &height);

	/* Dump messages */
	for (int i = 0; i < height; i++) {
		uint32_t color = message_color(i);
		u16b count = message_count(i);
		const char *str = message_str(i);
		const char *msg;

		if (count == 1) {
			msg = str;
		} else if (count == 0) {
			msg = " "; /* TODO ??? */
		} else {
			msg = format("%s <%dx>", str, count);
		}

		Term_erase_line(0, height - 1 - i);
		Term_adds(0, height - 1 - i, width, color, msg);
	}

	Term_flush_output();
	Term_pop();
}

/**
 * Display player in sub-windows (mode 0)
 */
static void update_player_basic_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	display_player(0);

	Term_flush_output();
	Term_pop();
}

/**
 * Display player in sub-windows (mode 1)
 */
static void update_player_extra_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	display_player(1);

	Term_flush_output();
	Term_pop();
}

/**
 * Display the left-hand-side of the main term, in more compact fashion.
 */
static void update_player_compact_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	struct loc coords = {0, 0};

	/* Race and Class */
	prt_field(player->race->name, coords);
	coords.y++;
	prt_field(player->class->name, coords);
	coords.y++;
	/* Title */
	prt_title(coords);
	coords.y++;
	/* Level/Experience */
	prt_level(coords);
	coords.y++;
	prt_exp(coords);
	coords.y++;
	/* Gold */
	prt_gold(coords);
	coords.y++;
	/* Equippy chars */
	prt_equippy(coords);
	coords.y++;
	/* All Stats */
	for (int i = 0; i < STAT_MAX; i++) {
		prt_stat(i, coords);
		coords.y++;
	}
	/* Empty row */
	coords.y++;
	/* Armor */
	prt_ac(coords);
	coords.y++;
	/* Hitpoints */
	prt_hp(coords);
	coords.y++;
	/* Spellpoints */
	prt_sp(coords);
	coords.y++;
	/* Monster health */
	prt_health(coords);

	Term_flush_output();
	Term_pop();
}

static void flush_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);
	Term_flush_output();
	Term_pop();
}

/**
 * Certain "screens" always use the main screen, including News, Birth,
 * Dungeon, Tomb-stone, High-scores, Macros, Colors, Visuals, Options.
 *
 * Later, special flags may allow sub-windows to "steal" stuff from the
 * main window, including File dump (help), File dump (artifacts, uniques),
 * Character screen, Small scale map, Previous Messages, Store screen, etc.
 */
static void subwindow_flag_changed(struct angband_term *aterm, int flag, bool enable)
{
	assert(flag > ATF_NONE);
	assert(flag < ATF_MAX);

	void (*register_or_deregister)(game_event_type type,
			game_event_handler *fn, void *user);
	void (*set_register_or_deregister)(game_event_type *type,
			size_t n_events, game_event_handler *fn, void *user);

	/* Decide whether to register or deregister an event handler */
	if (enable) {
		register_or_deregister = event_add_handler;
		set_register_or_deregister = event_add_handler_set;
	} else {
		register_or_deregister = event_remove_handler;
		set_register_or_deregister = event_remove_handler_set;
	}

	switch (flag) {
		case ATF_INVEN:
			register_or_deregister(EVENT_INVENTORY,
					update_inven_subwindow, aterm);
			break;

		case ATF_EQUIP:
			register_or_deregister(EVENT_EQUIPMENT,
					update_equip_subwindow, aterm);
			break;

		case ATF_PLAYER_BASIC:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events),
					update_player_basic_subwindow, aterm);
			break;

		case ATF_PLAYER_EXTRA:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events),
					update_player_extra_subwindow, aterm);
			break;

		case ATF_PLAYER_COMPACT:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events),
					update_player_compact_subwindow, aterm);
			break;

		case ATF_MAP:
			register_or_deregister(EVENT_MAP,
					update_maps, aterm);

			register_or_deregister(EVENT_END,
					flush_subwindow, aterm);
			break;

		case ATF_MESSAGE:
			register_or_deregister(EVENT_MESSAGE,
					update_messages_subwindow, aterm);
			break;

		case ATF_MONSTER:
			register_or_deregister(EVENT_MONSTERTARGET,
					update_monster_subwindow, aterm);
			break;

		case ATF_OBJECT:
			register_or_deregister(EVENT_OBJECTTARGET,
					update_object_subwindow, aterm);
			break;

		case ATF_MONLIST:
			register_or_deregister(EVENT_MONSTERLIST,
					update_monlist_subwindow, aterm);
			break;

		case ATF_ITEMLIST:
			register_or_deregister(EVENT_ITEMLIST,
					update_itemlist_subwindow, aterm);
			break;

		default:
			break;
	}
}

/**
 * Set the flags for one term, calling subwindow_flag_changed() with each flag
 * that has changed setting so that it can do any housekeeping to do with 
 * displaying the new thing or no longer displaying the old one.
 */
void subwindow_set_flags(struct angband_term *aterm, bitflag *flags, size_t size)
{
	assert(size == ATF_SIZE);

	/* Deal with the changed flags by seeing what's changed */
	for (int flag = ATF_NONE + 1; flag < ATF_MAX; flag++) {
		bool old = atf_has(aterm->flags, flag);
		bool new = atf_has(flags, flag);
		if (new != old) {
			subwindow_flag_changed(aterm, flag, new);
		}
	}

	atf_copy(aterm->flags, flags);

	Term_push(aterm->term);
	Term_clear();
	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Showing and updating the splash screen.
 * ------------------------------------------------------------------------ */
/**
 * Explain a broken "lib" folder and quit (see below).
 */
static void init_angband_aux(const char *why)
{
	quit_fmt("%s\n\n%s", why,
	         "The 'lib' directory is probably missing or broken.\n"
	         "Perhaps the archive was not extracted correctly.\n"
	         "See the 'readme.txt' file for more information.");
}

/*
 * Take notes on last line of splash screen
 */
static void splashscreen_note(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) user;

	if (data->message.type == MSG_BIRTH) {
		static struct loc coords = {0, 2};

		prt(data->message.msg, coords);
		pause_line();

		/* Advance one line (wrap if needed) */
		coords.y++;
		if (coords.y >= ANGBAND_TERM_STANDARD_HEIGHT) {
			coords.y = 2;
		}
	} else {
		int width;
		int height;
		Term_get_size(&width, &height);

		const int last_line = ANGBAND_TERM_STANDARD_HEIGHT - 1;
		assert(height >= last_line);

		char *s = format("[%s]", data->message.msg);
		Term_erase_line(0, (height - last_line) / 5 + last_line);
		Term_adds((width - strlen(s)) / 2, (height - last_line) / 5 + last_line,
				width, COLOUR_WHITE, s);
	}

	Term_flush_output();
	Term_redraw_screen();
}

static void show_splashscreen(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	ang_file *fp;

	char buf[1024];

	/* Verify the "news" file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "news.txt");
	if (!file_exists(buf)) {
		char why[1024];
		strnfmt(why, sizeof(why), "Cannot access the '%s' file!", buf);
		init_angband_aux(why);
	}

	Term_clear();

	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "news.txt");
	fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	if (fp) {
		/* Centre the splashscreen - assume news.txt has width 80, height 24 */
		struct text_out_info info = {
			.indent = (Term_width() - ANGBAND_TERM_STANDARD_WIDTH) / 2
		};

		Term_cursor_to_xy(0, (Term_height() - (ANGBAND_TERM_STANDARD_HEIGHT - 1)) / 5);

		/* Dump the file to the screen */
		while (file_getl(fp, buf, sizeof(buf))) {
			char *version_marker = strstr(buf, "$VERSION");
			if (version_marker) {
				ptrdiff_t pos = version_marker - buf;
				strnfmt(version_marker, sizeof(buf) - pos, "%-8s", buildver);
			}

			text_out_e(info, "%s", buf);
			text_out(info, "\n");
		}

		file_close(fp);
	}

	Term_flush_output();
	Term_redraw_screen();
}

/**
 * ------------------------------------------------------------------------
 * Visual updates between player turns.
 * ------------------------------------------------------------------------
 */
#if 0
static void refresh(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	Term_push(ANGBAND_TERM(user)->term);

	/* Place cursor on player/target */
	if (OPT(show_target) && target_sighted()) {
		struct loc coords;
		target_get(&coords.x, &coords.y);
		move_cursor_relative(ANGBAND_TERM(user), coords);
	}

	Term_flush_output();
	Term_pop();
}
#endif

static void repeated_command_display(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* TODO UI2 something */
}

/**
 * Housekeeping on arriving on a new level
 */
static void new_level_display_update(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	/* force invalid offsets so that they will be updated later */
	ANGBAND_TERM(user)->offset_x = z_info->dungeon_wid;
	ANGBAND_TERM(user)->offset_y = z_info->dungeon_hgt;

	if (player->upkeep->autosave) {
		save_game();
		player->upkeep->autosave = false;
	}

	verify_panel(ANGBAND_TERM(user));

	Term_push(ANGBAND_TERM(user)->term);
	Term_clear();

	player->upkeep->only_partial = true;
	/* Update stuff */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS);
	/* Calculate torch radius */
	player->upkeep->update |= (PU_TORCH);
	/* Update stuff */
	update_stuff(player);
	/* Fully update the visuals (and monster distances) */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_DISTANCE);
	/* Fully update the flow */
	player->upkeep->update |= (PU_FORGET_FLOW | PU_UPDATE_FLOW);
	/* Redraw dungeon */
	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP);
	/* Redraw "statusy" things */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MONLIST | PR_ITEMLIST);
	/* Because changing levels doesn't take a turn and PR_MONLIST might not be
	 * set for a few game turns, manually force an update on level change. */
	monster_list_force_subwindow_update();
	/* Update stuff */
	update_stuff(player);
	/* Redraw stuff */
	redraw_stuff(player);
	player->upkeep->only_partial = false;

	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Temporary (hopefully) hackish solutions.
 * ------------------------------------------------------------------------ */
static void cheat_death(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	msg("You invoke wizard mode and cheat death.");
	event_signal(EVENT_MESSAGE_FLUSH);

	/* TODO UI2 wiz_cheat_death(); */
}

static void check_panel(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	verify_panel(ANGBAND_TERM(user));
}

static void see_floor_items(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	int floor_max = z_info->floor_size;
	struct object **floor_list = mem_zalloc(floor_max * sizeof(*floor_list));
	int floor_num = 0;
	bool blind = (player->timed[TMD_BLIND] || no_light());

	const char *p = "see";
	bool can_pickup = false;

	/* Scan all visible, sensed objects in the grid */
	floor_num = scan_floor(floor_list, floor_max,
			OFLOOR_SENSE | OFLOOR_VISIBLE, NULL);
	if (floor_num == 0) {
		mem_free(floor_list);
		return;
	}

	/* Can we pick any up? */
	for (int i = 0; i < floor_num; i++) {
	    if (inven_carry_okay(floor_list[i])) {
			can_pickup = true;
		}
	}

	/* One object */
	if (floor_num == 1) {
		/* Get the object */
		struct object *obj = floor_list[0];
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];

		if (!can_pickup) {
			p = "have no room for";
		} else if (blind) {
			p = "feel";
		}

		/* Describe the object. Less detail if blind. */
		if (blind) {
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_BASE);
		} else {
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL);
		}

		event_signal(EVENT_MESSAGE_FLUSH);
		msg("You %s %s.", p, o_name);
	} else {
		if (!can_pickup) {
			p = "have no room for the following objects";
		} else if (blind) {
			p = "feel something on the floor";
		}
		show_prompt(format("You %s:", p), false);

		/* Display objects on the floor */
		struct term_hints hints = {
			.width = ANGBAND_TERM_STANDARD_WIDTH,
			.height = floor_num,
			.purpose = TERM_PURPOSE_TEXT,
			.position = TERM_POSITION_TOP_CENTER
		};
		Term_push_new(&hints);

		show_floor(floor_list, floor_num, OLIST_WEIGHT, NULL);

		ui_event event = inkey_simple();
		Term_prepend_events(&event, 1);

		Term_pop();
	}

	mem_free(floor_list);
}

/**
 * ------------------------------------------------------------------------
 * Initialising
 * ------------------------------------------------------------------------ */
/**
 * Process the user pref files relevant to a newly loaded character
 */
static void process_character_pref_files(void)
{
	bool found;
	char buf[1024];

	/* Process the "window.prf" file */
	process_pref_file("window.prf", true, true);

	/* Process the "user.prf" file */
	process_pref_file("user.prf", true, true);

	/* Process the pref file based on the character name */
	strnfmt(buf, sizeof(buf), "%s.prf", player_safe_name(player, true));
	found = process_pref_file(buf, true, true);

	/* Try pref file using savefile name if we fail using character name */
	if (!found) {
		int filename_index = path_filename_index(savefile);
		char filename[128];

		my_strcpy(filename, &savefile[filename_index], sizeof(filename));
		strnfmt(buf, sizeof(buf), "%s.prf", filename);
		process_pref_file(buf, true, true);
	}
}

static void ui_enter_init(game_event_type type,
		game_event_data *data, void *user)
{
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.purpose = TERM_PURPOSE_INTRO
	};
	Term_push_new(&hints);

	show_splashscreen(type, data, user);

	event_add_handler(EVENT_INITSTATUS, splashscreen_note, NULL);
}

static void ui_leave_init(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Reset visuals, then load prefs */
	reset_visuals(true);
	process_character_pref_files();

	/* Remove our splashscreen handlers */
	event_remove_handler(EVENT_INITSTATUS, splashscreen_note, NULL);

	/* Flash a message */
	prt("Please wait...", loc(0, 0));

	/* Flush the message */
	Term_flush_output();
	Term_redraw_screen();
	Term_pop();
}

static void ui_enter_world(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MESSAGE);
	redraw_stuff(player);

	/* Because of the "flexible" sidebar, all these things trigger
	   the same function. */
	event_add_handler_set(player_events,
			N_ELEMENTS(player_events), update_sidebar, &angband_sidebar);

	/* The flexible statusbar has similar requirements, so is
	   also trigger by a large set of events. */
	event_add_handler_set(statusline_events,
			N_ELEMENTS(statusline_events), update_statusline, &angband_status_line);

	/* Player HP can optionally change the colour of the '@' now. */
	event_add_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date */
	event_add_handler(EVENT_MAP, update_maps, &angband_cave);
#ifdef MAP_DEBUG
	event_add_handler(EVENT_MAP, trace_map_updates, &angband_cave);
#endif

	/* Check if the panel should shift when the player's moved */
	event_add_handler(EVENT_PLAYERMOVED, check_panel, &angband_cave);

	/* Take note of what's on the floor */
	event_add_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Enter a store */
	event_add_handler(EVENT_ENTER_STORE, enter_store, NULL);

	/* Display an explosion */
	event_add_handler(EVENT_EXPLOSION, display_explosion, &angband_cave);

	/* Display a bolt spell */
	event_add_handler(EVENT_BOLT, display_bolt, &angband_cave);

	/* Display a physical missile */
	event_add_handler(EVENT_MISSILE, display_missile, &angband_cave);

	/* Check to see if the player has tried to cancel game processing */
	event_add_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, &angband_cave);

#if 0
	/* Refresh the screen and put the cursor in the appropriate place */
	event_add_handler(EVENT_REFRESH, refresh, &angband_cave);
#endif

	/* Do the visual updates required on a new dungeon level */
	event_add_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, &angband_cave);

	/* Automatically clear messages while the game is repeating commands */
	event_add_handler(EVENT_COMMAND_REPEAT, repeated_command_display, NULL);

	/* Do animations (e.g. monster colour changes) */
	event_add_handler(EVENT_ANIMATE, animate, NULL);

	/* Allow the player to cheat death, if appropriate */
	event_add_handler(EVENT_CHEAT_DEATH, cheat_death, NULL);

	event_add_handler(EVENT_END, redraw, NULL);
}

static void ui_leave_world(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Because of the "flexible" sidebar, all these things trigger
	   the same function. */
	event_remove_handler_set(player_events,
			N_ELEMENTS(player_events), update_sidebar, &angband_sidebar);

	/* The flexible statusbar has similar requirements, so is
	   also trigger by a large set of events. */
	event_remove_handler_set(statusline_events,
			N_ELEMENTS(statusline_events), update_statusline, &angband_status_line);

	/* Player HP can optionally change the colour of the '@' now. */
	event_remove_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date */
	event_remove_handler(EVENT_MAP, update_maps, &angband_cave);
#ifdef MAP_DEBUG
	event_remove_handler(EVENT_MAP, trace_map_updates, &angband_cave);
#endif

	/* Check if the panel should shift when the player's moved */
	event_remove_handler(EVENT_PLAYERMOVED, check_panel, NULL);

	/* Take note of what's on the floor */
	event_remove_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Display an explosion */
	event_remove_handler(EVENT_EXPLOSION, display_explosion, &angband_cave);

	/* Display a bolt spell */
	event_remove_handler(EVENT_BOLT, display_bolt, &angband_cave);

	/* Display a physical missile */
	event_remove_handler(EVENT_MISSILE, display_missile, &angband_cave);

	/* Check to see if the player has tried to cancel game processing */
	event_remove_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, &angband_cave);

#if 0
	/* Refresh the screen and put the cursor in the appropriate place */
	event_remove_handler(EVENT_REFRESH, refresh, &angband_cave);
#endif

	/* Do the visual updates required on a new dungeon level */
	event_remove_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, &angband_cave);

	/* Automatically clear messages while the game is repeating commands */
	event_remove_handler(EVENT_COMMAND_REPEAT, repeated_command_display, NULL);

	/* Do animations (e.g. monster colour changes) */
	event_remove_handler(EVENT_ANIMATE, animate, NULL);

	/* Allow the player to cheat death, if appropriate */
	event_remove_handler(EVENT_CHEAT_DEATH, cheat_death, NULL);

	/* Prepare to interact with a store */
	event_add_handler(EVENT_USE_STORE, use_store, NULL);

	/* If we've gone into a store, we need to know how to leave */
	event_add_handler(EVENT_LEAVE_STORE, leave_store, NULL);
}

static void ui_enter_game(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Display a message to the player */
	event_add_handler(EVENT_MESSAGE, display_message, &angband_message_line);

	/* Display a message and make a noise to the player */
	event_add_handler(EVENT_BELL, bell_message, NULL);

	/* Tell the UI to ignore all pending input */
	event_add_handler(EVENT_INPUT_FLUSH, inkey_flush, NULL);

	/* Print all waiting messages */
	event_add_handler(EVENT_MESSAGE_FLUSH, message_flush, &angband_message_line);
}

static void ui_leave_game(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Display a message to the player */
	event_remove_handler(EVENT_MESSAGE, display_message, NULL);

	/* Display a message and make a noise to the player */
	event_remove_handler(EVENT_BELL, bell_message, NULL);

	/* Tell the UI to ignore all pending input */
	event_remove_handler(EVENT_INPUT_FLUSH, inkey_flush, NULL);

	/* Print all waiting messages */
	event_remove_handler(EVENT_MESSAGE_FLUSH, message_flush, NULL);
}

void init_display(void)
{
	/* and never pop it */
	Term_push(angband_cave.term);

	event_add_handler(EVENT_ENTER_INIT, ui_enter_init, NULL);
	event_add_handler(EVENT_LEAVE_INIT, ui_leave_init, NULL);

	event_add_handler(EVENT_ENTER_GAME, ui_enter_game, NULL);
	event_add_handler(EVENT_LEAVE_GAME, ui_leave_game, NULL);

	event_add_handler(EVENT_ENTER_WORLD, ui_enter_world, NULL);
	event_add_handler(EVENT_LEAVE_WORLD, ui_leave_world, NULL);

	ui_init_birthstate_handlers();
}
