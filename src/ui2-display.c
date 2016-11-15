/**
 * \file ui2-display.c
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
#include "ui2-command.h"
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
#include "ui2-wizard.h"

struct display_term {
	enum display_term_index index;

	int width;
	int height;

	term term;

	struct {
		int offset;
		bool clear;
	} messages;

	struct loc coords;

	const char *name;
	bool required;
	bool active;
};

static struct display_term *display_term_get(enum display_term_index index);
static void display_terms_check(void);

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

	/* For equippy chars */
	EVENT_EQUIPMENT,

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
void cnv_stat(const char *fmt, int val, char *out_val, size_t out_len)
{
	/* Stats above 18 need special treatment (lol) */
	if (val > 18) {
		int bonus = val - 18;
		strnfmt(out_val, out_len, fmt, 18 + bonus / 10);
	} else {
		strnfmt(out_val, out_len, fmt, val);
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
		verify_cursor(); /* -more- can come in the middle of the turn */

		Term_addws(x, 0, MSG_MORE_LEN, COLOUR_L_BLUE, L"-more-");
		Term_flush_output();
		inkey_any();
	}

	Term_erase_all();
}

/**
 * Output a message to the top line of the screen.
 *
 * Break long messages into multiple pieces
 * Allow multiple short messages to share the top line.
 * Prompt the user to make sure he has a chance to read them.
 */
static void message_print(game_event_type type, game_event_data *data, void *user)
{
	(void) type;

	struct display_term *dt = user;

	if (!dt->active || !character_generated
			|| data == NULL
			|| data->message.msg == NULL)
	{
		return;
	}

	Term_push(dt->term);

	wchar_t buf[1024];
	int len = text_mbstowcs(buf, data->message.msg, sizeof(buf));

	if (dt->messages.clear) {
		Term_erase_all();
		dt->messages.clear = false;
		dt->messages.offset = 0;
	}

	assert(dt->width > MSG_MORE_LEN);

#define MESSAGE_WRAP (dt->width - MSG_MORE_LEN)

	if (dt->messages.offset > 0
			&& dt->messages.offset + len > MESSAGE_WRAP)
	{
		message_more(dt->messages.offset);
		dt->messages.offset = 0;
	}

	uint32_t color = message_type_color(data->message.type);
	wchar_t *ws = buf;

	/* Message line can be resized while it is waiting for "-more-",
	 * so we have to recalculate message_wrap on every iteration */
	for (int wrap = MESSAGE_WRAP; len > wrap; wrap = MESSAGE_WRAP) {
		int split = wrap;

		for (int i = 0; i < wrap; i++) {
			if (ws[i] == L' ') {
				split = i;
			}
		}

		Term_addws(0, 0, split, color, ws);
		message_more(split + 1);

		if (ws[split] == L' ') {
			split++;
		}

		ws += split;
		len -= split;
	}

#undef MESSAGE_WRAP

	Term_addws(dt->messages.offset, 0, len, color, ws);
	Term_flush_output();

	dt->messages.offset += len + 1;

	Term_pop();
}

/**
 * Flush the output before displaying for emphasis
 */
static void message_bell(game_event_type type, game_event_data *data, void *user)
{
	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	message_print(type, data, user);
	player->upkeep->redraw |= PR_MESSAGE;
}

/**
 * Print the "-more-" prompt
 */
static void message_flush(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

	if (dt->messages.offset > 0) {
		message_more(dt->messages.offset);
		dt->messages.offset = 0;
	}

	Term_erase_all();
	Term_flush_output();
	Term_pop();
}

/*
 * Skip next "-more-" prompt, if any
 */
void message_skip_more(void)
{
	struct display_term *display_message_line =
		display_term_get(DISPLAY_MESSAGE_LINE);

	if (display_message_line->active) {
		display_message_line->messages.offset = 0;
		display_message_line->messages.clear = true;
	}
}

/**
 * ------------------------------------------------------------------------
 * Sidebar display functions
 * ------------------------------------------------------------------------
 */

/**
 * Print character info at given coordinates
 */
static void prt_field(const char *info, struct loc coords)
{
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
		cnv_stat("%2d", player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_YELLOW, tmp, loc(coords.x + 10, coords.y));
	} else {
		put_str(stat_names[stat], coords);
		cnv_stat("%2d", player->state.stat_use[stat], tmp, sizeof(tmp));
		c_put_str(COLOUR_L_GREEN, tmp, loc(coords.x + 10, coords.y));
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

	char *label;
	uint32_t attr;
	if (player->lev >= player->max_lev) {
		label = "LEVEL ";
		attr = COLOUR_L_GREEN;
	} else {
		label = "Level ";
		attr = COLOUR_YELLOW;
	}

	put_str(label, coords);
	coords.x += 6;
	c_put_str(attr, tmp, coords);
}

static void prt_exp_aux(char *buf, size_t buf_size, bool max_level)
{
	long long xp;

	if (max_level) {
		xp = player->exp;
	} else {
		long long next_level_xp =
			((long long) player_exp[player->lev - 1] * player->expfact / 100LL);

		xp = next_level_xp - player->exp;
	}

	strnfmt(buf, buf_size, "%8lld", xp);
}

/**
 * Display the experience
 */
static void prt_exp(struct loc coords)
{
	const bool lev50 = player->lev == 50;

	char xp[32];
	prt_exp_aux(xp, sizeof(xp), lev50);

	char *label;
	uint32_t attr;
	if (player->exp >= player->max_exp) {
		label = lev50 ? "EXP" : "NXT";
		attr = COLOUR_L_GREEN;
	} else {
		label = lev50 ? "Exp" : "Nxt";
		attr = COLOUR_YELLOW;
	}

	put_str(label, coords);
	coords.x += 4;
	c_put_str(attr, xp, coords);
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
		struct object *obj = slot_object(player, i);

		if (obj) {
			wchar_t ch = object_char(obj);
			uint32_t attr = object_attr(obj);
			Term_addwc(coords.x, coords.y, attr, ch);
		}
	}
}

/**
 * Prints current AC
 */
static void prt_ac(struct loc coords)
{
	char buf[32];

	put_str("Cur AC ", coords);
	coords.x += 7;
	strnfmt(buf, sizeof(buf),
			"%5d", player->known_state.ac + player->known_state.to_a);
	c_put_str(COLOUR_L_GREEN, buf, coords);
}

/**
 * Prints current hitpoints
 */
static void prt_hp(struct loc coords)
{
	char cur_hp[32];
	char max_hp[32];
	uint32_t attr = player_hp_attr(player);

	put_str("HP ", coords);
	coords.x += 3;

	strnfmt(cur_hp, sizeof(cur_hp), "%4d", player->chp);
	c_put_str(attr, cur_hp, coords);
	coords.x += 4;

	c_put_str(COLOUR_WHITE, "/", coords);
	coords.x += 1;

	strnfmt(max_hp, sizeof(max_hp), "%4d", player->mhp);
	c_put_str(COLOUR_L_GREEN, max_hp, coords);
}

/**
 * Prints players max/cur spell points
 */
static void prt_sp(struct loc coords)
{
	char cur_sp[32];
	char max_sp[32];
	uint32_t attr = player_sp_attr(player);

	/* Do not show mana unless we should have some */
	if (player_has(player, PF_NO_MANA)
			|| player->lev < player->class->magic.spell_first)
	{
		return;
	}

	put_str("SP ", coords);
	coords.x += 3;

	/* Show mana */
	strnfmt(cur_sp, sizeof(cur_sp), "%4d", player->csp);
	c_put_str(attr, cur_sp, coords);
	coords.x += 4;

	c_put_str(COLOUR_WHITE, "/", coords);
	coords.x += 1;

	strnfmt(max_sp, sizeof(max_sp), "%4d", player->msp);
	c_put_str(COLOUR_L_GREEN, max_sp, coords);
}

/* Tracking an unseen, hallucinatory, or dead (?) monster */
#define MONSTER_HEALTH_UNKNOWN(mon) \
	(!mflag_has((mon)->mflag, MFLAG_VISIBLE) \
	 || player->timed[TMD_IMAGE] \
	 || (mon)->hp < 0)

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
	} else if (MONSTER_HEALTH_UNKNOWN(mon)) {
		attr = COLOUR_WHITE;
	} else {
		/* Extract the percent of health */
		long long pct = 100LL * mon->hp / mon->maxhp;

		if (pct >= 100) {
			/* Healthy */
			attr = COLOUR_L_GREEN;
		} else if (pct >= 60) {
			/* Somewhat wounded */
			attr = COLOUR_YELLOW;
		} else if (pct >= 25) {
			/* Wounded */
			attr = COLOUR_ORANGE;
		} else {
			/* Badly wounded */
			attr = COLOUR_L_RED;
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
	if (mon == NULL) {
		return;
	}

	uint32_t attr = monster_health_attr(mon);

	if (MONSTER_HEALTH_UNKNOWN(mon)) {
		Term_addws(coords.x, coords.y, 12, attr, L"[----------]");
	} else {
		/* Extract the percent of health */
		long long pct = 100LL * mon->hp / mon->maxhp;
		/* Convert percent into health */
		int len = (pct < 10) ? 1 : (pct < 90) ? (pct / 10 + 1) : 10;
		/* Default to unknown */
		Term_addws(coords.x, coords.y, 12, COLOUR_WHITE, L"[----------]");
		/* Dump the current health (use '*' symbols) */
		Term_addws(coords.x + 1, coords.y, len, attr, L"**********");
	}
}

#undef MONSTER_HEALTH_UNKNOWN

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
		attr = speed_attr(speed - 110);
		type = "Fast";
	} else if (speed < 110) {
		attr = speed_attr(speed - 110);
		type = "Slow";
	}

	if (type) {
		char buf[32] = {0};
		strnfmt(buf, sizeof(buf), "%s (%+d)", type, speed - 110);
		c_put_str(attr, format("%-10s", buf), coords);
	}
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
		strnfmt(depths, sizeof(depths),
				"%d' (L%d)", player->depth * 50, player->depth);
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
	{ prt_race,    17, EVENT_RACE_CLASS },
	{ prt_title,   16, EVENT_PLAYERTITLE },
	{ prt_class,   20, EVENT_RACE_CLASS },
	{ prt_level,    8, EVENT_PLAYERLEVEL },
	{ prt_exp,     14, EVENT_EXPERIENCE },
	{ prt_gold,     9, EVENT_GOLD },
	{ prt_equippy, 15, EVENT_EQUIPMENT },
	{ prt_str,      4, EVENT_STATS },
	{ prt_int,      3, EVENT_STATS },
	{ prt_wis,      2, EVENT_STATS },
	{ prt_dex,      1, EVENT_STATS },
	{ prt_con,      0, EVENT_STATS },
	{ NULL,        13, 0 },
	{ prt_ac,       5, EVENT_AC },
	{ prt_hp,       6, EVENT_HP },
	{ prt_sp,       7, EVENT_MANA },
	{ NULL,        19, 0 },
	{ prt_health,  10, EVENT_MONSTERHEALTH },
	{ NULL,        18, 0 },
	{ prt_speed,   11, EVENT_PLAYERSPEED },  /* Slow (-NN) / Fast (+NN) */
	{ prt_depth,   12, EVENT_DUNGEONLEVEL }, /* Lev NNN / NNNN ft */
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

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

	struct loc coords = {0, 0};

	for (size_t i = 0; i < N_ELEMENTS(side_handlers) && coords.y < dt->height; i++) {
		const struct side_handler_t *handler = &side_handlers[i];
		assert(handler->priority >= 0);
		/* If this is high enough priority, display it */
		if (handler->priority < dt->height) {
			if (handler->type == type && handler->hook != NULL) {
				Term_erase_line(coords.x, coords.y);
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
 * that using this command is only for when graphics mode is off, since
 * tiles don't support that (yet)
 */
static void hp_colour_change(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	if (OPT(player, hp_changes_color) && use_graphics == GRAPHICS_NONE) {
		event_signal_point(EVENT_MAP, player->px, player->py);
	}
}

/**
 * ------------------------------------------------------------------------
 * Status line display functions
 * ------------------------------------------------------------------------
 */

struct state_info {
	int value;
	const char *str;
	size_t len;
	uint32_t attr;
};

/*
 * Note that sizeof(str) is strlen(str) + 1, which is
 * what we want (to avoid having to print space after
 * every str - see update_statusline()).
 */
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
 */
static size_t prt_state(struct loc coords)
{
	uint32_t attr = COLOUR_WHITE;
	char text[] = "Rest      ";

	/* Displayed states are resting and repeating */
	if (player_is_resting(player)) {
		int n = player_resting_count(player);

		/* Display according to length or intent of rest */
		if (n == REST_ALL_POINTS) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '*';
		} else if (n == REST_COMPLETE) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '&';
		} else if (n == REST_SOME_POINTS) {
			text[5] = text[6] = text[7] = text[8] = text[9] = '!';
		} else {
			strnfmt(text + 5, sizeof(text) - 5, "%d", n);
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
	if (!OPT(player, birth_feelings) || player->depth == 0) {
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
	assert(obj_feeling < N_ELEMENTS(obj_feeling_color));

	uint32_t obj_feeling_color_print;
	char obj_feeling_str[6];

	if (cave->feeling_squares < z_info->feeling_need) {
		obj_feeling_color_print = COLOUR_WHITE;

		my_strcpy(obj_feeling_str, "?", sizeof(obj_feeling_str));
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
	 * 0 -> ?. Monster feeling should never be 0, but we check it just in case.
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

	assert(mon_feeling < N_ELEMENTS(mon_feeling_color));

	c_put_str(mon_feeling_color[mon_feeling], mon_feeling_str, coords);
	coords.x += strlen(mon_feeling_str);

	c_put_str(COLOUR_WHITE, "-", coords);
	coords.x++;

	c_put_str(obj_feeling_color_print, obj_feeling_str, coords);
	coords.x += strlen(obj_feeling_str);

	return coords.x - oldx + 1; /* Add one to "append" a space at the end */
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
	} else {
		return 0;
	}
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

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	Term_erase_all();

	struct loc coords = {0, 0};
	for (size_t i = 0; i < N_ELEMENTS(status_handlers) && coords.x < dt->width; i++) {
		coords.x += status_handlers[i](coords);
	}

	Term_flush_output();

	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Map redraw.
 * ------------------------------------------------------------------------
 */

#ifdef MAP_DEBUG
static void trace_map_updates(game_event_type type,
		game_event_data *data, void *user)
{
	if (data->point.x == -1 && data->point.y == -1) {
		printf("Redraw whole map\n");
	} else {
		printf("Redraw (%d, %d)\n", data->point.x, data->point.y);
	}
}
#endif

/**
 * Update either a single map grid or a whole map
 */
static void update_maps(game_event_type type, game_event_data *data, void *user)
{
	(void) type;

	struct display_term *dt = user;

	Term_push(dt->term);

	if (data->point.x == -1 && data->point.y == -1) {
		/* This signals a whole-map redraw. */
		map_redraw_all(dt->index);
	} else {
		/* Single point to be redrawn */

		/* Location relative to panel */
		int relx = data->point.x - dt->coords.x;
		int rely = data->point.y - dt->coords.y;

		if (relx >= 0 && rely >= 0
				&& relx < dt->width && rely < dt->height)
		{
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

	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Animations.
 * ------------------------------------------------------------------------
 */

static byte flicker = 0;
static byte color_flicker[MAX_COLORS][3] =
{
	{COLOUR_DARK,        COLOUR_L_DARK,      COLOUR_L_RED},
	{COLOUR_WHITE,       COLOUR_L_WHITE,     COLOUR_L_BLUE},
	{COLOUR_SLATE,       COLOUR_WHITE,       COLOUR_L_DARK},
	{COLOUR_ORANGE,      COLOUR_YELLOW,      COLOUR_L_RED},
	{COLOUR_RED,         COLOUR_L_RED,       COLOUR_L_PINK},
	{COLOUR_GREEN,       COLOUR_L_GREEN,     COLOUR_L_TEAL},
	{COLOUR_BLUE,        COLOUR_L_BLUE,      COLOUR_SLATE},
	{COLOUR_UMBER,       COLOUR_L_UMBER,     COLOUR_MUSTARD},
	{COLOUR_L_DARK,      COLOUR_SLATE,       COLOUR_L_VIOLET},
	{COLOUR_WHITE,       COLOUR_SLATE,       COLOUR_L_WHITE},
	{COLOUR_L_PURPLE,    COLOUR_PURPLE,      COLOUR_L_VIOLET},
	{COLOUR_YELLOW,      COLOUR_L_YELLOW,    COLOUR_MUSTARD},
	{COLOUR_L_RED,       COLOUR_RED,         COLOUR_L_PINK},
	{COLOUR_L_GREEN,     COLOUR_L_TEAL,      COLOUR_GREEN},
	{COLOUR_L_BLUE,      COLOUR_DEEP_L_BLUE, COLOUR_BLUE_SLATE},
	{COLOUR_L_UMBER,     COLOUR_UMBER,       COLOUR_MUD},
	{COLOUR_PURPLE,      COLOUR_VIOLET,      COLOUR_MAGENTA},
	{COLOUR_VIOLET,      COLOUR_L_VIOLET,    COLOUR_MAGENTA},
	{COLOUR_TEAL,        COLOUR_L_TEAL,      COLOUR_L_GREEN},
	{COLOUR_MUD,         COLOUR_YELLOW,      COLOUR_UMBER},
	{COLOUR_L_YELLOW,    COLOUR_WHITE,       COLOUR_L_UMBER},
	{COLOUR_MAGENTA,     COLOUR_L_PINK,      COLOUR_L_RED},
	{COLOUR_L_TEAL,      COLOUR_L_WHITE,     COLOUR_TEAL},
	{COLOUR_L_VIOLET,    COLOUR_L_PURPLE,    COLOUR_VIOLET},
	{COLOUR_L_PINK,      COLOUR_L_RED,       COLOUR_L_WHITE},
	{COLOUR_MUSTARD,     COLOUR_YELLOW,      COLOUR_UMBER},
	{COLOUR_BLUE_SLATE,  COLOUR_BLUE,        COLOUR_SLATE},
	{COLOUR_DEEP_L_BLUE, COLOUR_L_BLUE,      COLOUR_BLUE},
};

static byte get_flicker(byte a)
{
	switch (flicker % 3) {
		case 1:
			return color_flicker[a][1];
		case 2:
			return color_flicker[a][2];
		default:
			return a;
	}
}

/**
 * This animates monsters as necessary.
 */
static void flicker_monsters(void)
{
	for (int i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		uint32_t attr;

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

		event_signal_point(EVENT_MAP, mon->fx, mon->fy);
	}

	player->upkeep->redraw |= PR_MONLIST;

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

	flicker_monsters();

	if (player->opts.delay_factor > 0) {
		if (player->upkeep->running) {
			if (OPT(player, show_target) || OPT(player, highlight_player)) {
				verify_cursor();
			}

			Term_redraw_screen(player->opts.delay_factor);
		} else if (cmd_get_nrepeats() > 0
				|| (player_is_resting(player) && player_resting_count(player) % 100 == 0))
		{
			/* Update the display on repeating commands
			 * (to animate resting, tunneling counters),
			 * but, if the player is resting, not too
			 * frequently, to make it go over quicker */
			Term_redraw_screen(player->opts.delay_factor);
		}
	}
}

/**
 * This is used when the user is idle to allow for simple animations.
 * Currently the only thing it really does is animate shimmering monsters.
 */
void idle_update(void)
{
	if (character_dungeon
			&& OPT(player, animate_flicker)
			&& use_graphics == GRAPHICS_NONE
			&& player->opts.delay_factor > 0)
	{
		flicker_monsters();
		redraw_stuff(player);
		Term_redraw_screen(player->opts.delay_factor);
	}
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

	struct display_term *dt = user;

	Term_push(dt->term);

	int gf_type   = data->explosion.gf_type;
	bool drawing  = data->explosion.drawing;
	int num_grids = data->explosion.num_grids;

	int *distance_to_grid  = data->explosion.distance_to_grid;
	bool *player_sees_grid = data->explosion.player_sees_grid;
	struct loc *blast_grid = data->explosion.blast_grid;

	bool drawn = false;

	/* Draw the blast from inside out */
	for (int i = 0; i < num_grids; i++) {
		/* Only do visuals if the player can see the blast */
		if (player_sees_grid[i]) {
			/* Obtain the explosion pict */
			wchar_t ch;
			uint32_t attr;
			bolt_pict(blast_grid[i], blast_grid[i], gf_type, &attr, &ch);

			/* Just display the pict, ignoring what was under it */
			print_map_relative(dt->index, attr, ch, blast_grid[i]);

			drawn = true;
		}

		if (i == num_grids - 1
				|| distance_to_grid[i + 1] > distance_to_grid[i])
		{
			/* We have all the grids at the current radius, so draw it */
			if (drawn || drawing) {
				Term_flush_output();

				if (player->opts.delay_factor > 0) {
					Term_redraw_screen(player->opts.delay_factor);
				}
			}
		}
	}

	if (drawn) {
		/* Erase the explosion drawn above */
		for (int i = 0; i < num_grids; i++) {
			/* Erase visible, valid grids */
			if (player_sees_grid[i]) {
				event_signal_point(EVENT_MAP,
						blast_grid[i].x, blast_grid[i].y);
			}
		}

		Term_flush_output();
		if (player->opts.delay_factor > 0) {
			Term_redraw_screen(player->opts.delay_factor);
		}
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

	struct display_term *dt = user;

	Term_push(dt->term);

	struct loc old = {
		.x = data->bolt.ox,
		.y = data->bolt.oy
	};
	struct loc new = {
		.x = data->bolt.x,
		.y = data->bolt.y
	};

	/* Only do visuals if the player can see the bolt */
	if (data->bolt.seen) {
		wchar_t ch;
		uint32_t attr;
		bolt_pict(old, new, data->bolt.gf_type, &attr, &ch);

		print_map_relative(dt->index, attr, ch, new);

		Term_flush_output();
		if (player->opts.delay_factor > 0) {
			Term_redraw_screen(player->opts.delay_factor);
		}

		event_signal_point(EVENT_MAP, new.x, new.y);

		/* Display "beam" grids */
		if (data->bolt.beam) {
			bolt_pict(new, new, data->bolt.gf_type, &attr, &ch);
			print_map_relative(dt->index, attr, ch, new);
		}

		Term_flush_output();

	} else if (data->bolt.drawing) {
		/* Delay for consistency */
		if (player->opts.delay_factor > 0) {
			Term_delay(player->opts.delay_factor);
		}
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

	struct display_term *dt = user;

	Term_push(dt->term);

	struct object *obj = data->missile.obj;
	struct loc coords  = {
		.x = data->missile.x,
		.y = data->missile.y
	};

	/* Only do visuals if the player can "see" the missile */
	if (data->missile.seen) {
		print_map_relative(dt->index,
				object_attr(obj), object_char(obj), coords);

		Term_flush_output();
		if (player->opts.delay_factor > 0) {
			Term_redraw_screen(player->opts.delay_factor);
		}

		event_signal_point(EVENT_MAP, coords.x, coords.y);
		Term_flush_output();
	}

	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Subwindow displays
 * ------------------------------------------------------------------------
 */

/**
 * True when we're supposed to display the equipment in the inventory
 * window, or vice-versa.
 */
static bool flip_inven_equip;

static void update_inven_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	Term_erase_all();

	if (!flip_inven_equip) {
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
	} else {
		show_equip(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_FULL, NULL);
	}

	Term_flush_output();
	Term_pop();
}

static void update_equip_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	Term_erase_all();

	if (!flip_inven_equip) {
		show_equip(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_FULL, NULL);
	} else {
		show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
	}

	Term_flush_output();
	Term_pop();
}

/**
 * Flip "inven" and "equip" in corresponding sub-windows
 */
void toggle_inven_equip(void)
{
	/* Change the actual setting */
	flip_inven_equip = !flip_inven_equip;

	struct display_term *inven = display_term_get(DISPLAY_INVEN);
	if (inven->term != NULL) {
		Term_push(inven->term);
		Term_erase_all();

		if (flip_inven_equip) {
			show_equip(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_FULL, NULL);
		} else {
			show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
		}

		Term_flush_output();
		Term_pop();
	}

	struct display_term *equip = display_term_get(DISPLAY_EQUIP);
	if (equip->term != NULL) {
		Term_push(equip->term);
		Term_erase_all();

		if (flip_inven_equip) {
			show_inven(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_COMPACT, NULL);
		} else {
			show_equip(OLIST_WINDOW | OLIST_WEIGHT | OLIST_QUIVER_FULL, NULL);
		}

		Term_flush_output();
		Term_pop();
	}
}

static void update_itemlist_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	Term_erase_all();

	object_list_show_subwindow();

	Term_flush_output();
	Term_pop();
}

static void update_monlist_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	Term_erase_all();

	monster_list_show_subwindow();

	Term_flush_output();
	Term_pop();
}

static void update_monster_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

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

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);
	
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

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

	/* Dump messages, starting from the last term line */
	int y = dt->height - 1;
	for (unsigned m = 0, num = messages_num(); m < num && y >= 0; m++) {
		uint32_t color = message_color(m);
		unsigned count = message_count(m);
		const char *str = message_str(m);

		if (count != 0) {
			const char *msg = count == 1 ? str : format("%s <%ux>", str, count);

			Term_erase_line(0, y);
			Term_adds(0, y, dt->width, color, msg);
			y--;
		}
	}

	Term_flush_output();
	Term_pop();
}

/**
 * Display player in sub-windows (basic info)
 */
static void update_player_basic_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

	display_player(PLAYER_DISPLAY_MODE_BASIC);

	Term_flush_output();
	Term_pop();
}

/**
 * Display player in sub-windows (resistances)
 */
static void update_player_extra_subwindow(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	if (!dt->active) {
		return;
	}

	Term_push(dt->term);

	display_player(PLAYER_DISPLAY_MODE_EXTRA);

	Term_flush_output();
	Term_pop();
}

static void display_term_handler(struct display_term *dt, bool enable)
{
	assert(dt->term != NULL);

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

	switch (dt->index) {
		case DISPLAY_MESSAGE_LINE:
			register_or_deregister(EVENT_MESSAGE,
					message_print, dt);
			register_or_deregister(EVENT_BELL,
					message_bell, dt);
			register_or_deregister(EVENT_MESSAGE_FLUSH,
					message_flush, dt);
			break;

		case DISPLAY_PLAYER_COMPACT:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events), update_sidebar, dt);
			break;

		case DISPLAY_STATUS_LINE:
			set_register_or_deregister(statusline_events,
					N_ELEMENTS(statusline_events), update_statusline, dt);
			break;

		case DISPLAY_INVEN:
			register_or_deregister(EVENT_INVENTORY,
					update_inven_subwindow, dt);
			break;

		case DISPLAY_EQUIP:
			register_or_deregister(EVENT_EQUIPMENT,
					update_equip_subwindow, dt);
			break;

		case DISPLAY_PLAYER_BASIC:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events),
					update_player_basic_subwindow, dt);
			break;

		case DISPLAY_PLAYER_EXTRA:
			set_register_or_deregister(player_events,
					N_ELEMENTS(player_events),
					update_player_extra_subwindow, dt);
			break;

		case DISPLAY_MESSAGES:
			register_or_deregister(EVENT_MESSAGE,
					update_messages_subwindow, dt);
			break;

		case DISPLAY_MONSTER:
			register_or_deregister(EVENT_MONSTERTARGET,
					update_monster_subwindow, dt);
			break;

		case DISPLAY_OBJECT:
			register_or_deregister(EVENT_OBJECTTARGET,
					update_object_subwindow, dt);
			break;

		case DISPLAY_MONLIST:
			register_or_deregister(EVENT_MONSTERLIST,
					update_monlist_subwindow, dt);
			break;

		case DISPLAY_ITEMLIST:
			register_or_deregister(EVENT_ITEMLIST,
					update_itemlist_subwindow, dt);
			break;

		case DISPLAY_CAVE:
			quit_fmt("Handlers for term '%s' are set automatically!", dt->name);
			break;

		default:
			quit_fmt("Unrecognized display index %u!\n", (unsigned) dt->index);
			break;
	}

	if (enable) {
		display_terms_redraw();
	}
}

/**
 * ------------------------------------------------------------------------
 * Showing and updating the splash screen.
 * ------------------------------------------------------------------------
 */

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

	int width;
	int height;
	Term_get_size(&width, &height);

	const int last_line = height - 1;

	char *str = format("[%s]", data->message.msg);
	Term_erase_line(0, last_line);
	Term_adds((width - strlen(str)) / 2, last_line,
			width, COLOUR_WHITE, str);

	Term_flush_output();
	Term_redraw_screen(0);
}

static void show_splashscreen(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	char buf[1024];

	/* Verify the "news" file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "news.txt");
	if (!file_exists(buf)) {
		char why[1024];
		strnfmt(why, sizeof(why), "Cannot access the '%s' file!", buf);
		init_angband_aux(why);
	}

	Term_erase_all();

	ang_file *fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	if (fp) {
		Term_cursor_to_xy(0, 0);

		/* Dump the file to the screen */
		while (file_getl(fp, buf, sizeof(buf))) {
			char *version_marker = strstr(buf, "$VERSION");
			if (version_marker) {
				ptrdiff_t pos = version_marker - buf;
				strnfmt(version_marker, sizeof(buf) - pos, "%-8s", buildver);
			}

			struct text_out_info info = {0};
			text_out_e(info, "%s", buf);
			text_out(info, "\n");
		}

		file_close(fp);
	}

	Term_flush_output();
	Term_redraw_screen(0);
}

static void repeated_command_display(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	message_skip_more();
}

/**
 * Housekeeping on arriving on a new level
 */
static void new_level_display_update(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	/* force invalid offsets so that
	 * they will be updated later */
	dt->coords.x = z_info->dungeon_wid;
	dt->coords.y = z_info->dungeon_hgt;

	if (player->upkeep->autosave) {
		save_game();
		player->upkeep->autosave = false;
	}

	Term_push(dt->term);
	Term_erase_all();

	verify_panel(dt->index);

	player->upkeep->only_partial = true;

	/* Update stuff */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS);
	/* Calculate torch radius */
	player->upkeep->update |= (PU_TORCH);
	/* Fully update the visuals (and monster distances) */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_DISTANCE);
	/* Fully update the flow */
	player->upkeep->update |= (PU_FORGET_FLOW | PU_UPDATE_FLOW);
	/* Redraw dungeon */
	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP);
	/* Redraw "statusy" things */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MONLIST | PR_ITEMLIST);

	update_stuff(player);
	redraw_stuff(player);

	player->upkeep->only_partial = false;

	Term_flush_output();
	Term_pop();
}

/**
 * ------------------------------------------------------------------------
 * Temporary (hopefully) hackish solutions.
 * ------------------------------------------------------------------------
 */

static void cheat_death(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	msg("You invoke wizard mode and cheat death.");
	event_signal(EVENT_MESSAGE_FLUSH);

	wiz_cheat_death();
}

static void check_viewport(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct display_term *dt = user;

	verify_panel(dt->index);
}

static void see_floor_items(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	const bool blind = player->timed[TMD_BLIND] || no_light();
	const int floor_max = z_info->floor_size;

	struct object **floor_list = mem_zalloc(floor_max * sizeof(*floor_list));

	/* Scan all visible, sensed objects in the grid */
	int floor_num = scan_floor(floor_list, floor_max,
			OFLOOR_SENSE | OFLOOR_VISIBLE, NULL);
	if (floor_num == 0) {
		mem_free(floor_list);
		return;
	}

	/* Can we pick any up? */
	bool can_pickup = false;
	for (int i = 0; i < floor_num; i++) {
	    if (inven_carry_okay(floor_list[i])) {
			can_pickup = true;
			break;
		}
	}

	/* One object */
	if (floor_num == 1) {
		/* Get the object */
		struct object *obj = floor_list[0];
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];

		const char *p = "see";
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
		/* Display objects on the floor */
		struct term_hints hints = {
			.width = ANGBAND_TERM_STANDARD_WIDTH,
			.height = floor_num,
			.tabs = true,
			.purpose = TERM_PURPOSE_TEXT,
			.position = TERM_POSITION_TOP_LEFT
		};
		Term_push_new(&hints);
		Term_add_tab(0, "Floor", COLOUR_WHITE, COLOUR_DARK);

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
 * ------------------------------------------------------------------------
 */

/**
 * Process the user pref files relevant to a newly loaded character
 */
static void process_character_pref_files(void)
{
	char buf[1024];

	/* Process the "user.prf" file */
	process_pref_file("user.prf", true, true);

	/* Get the filesystem-safe name and append .prf */
	player_safe_name(buf, sizeof(buf), player->full_name, true);
	my_strcat(buf, ".prf", sizeof(buf));

	/* Try pref file using savefile name if we fail using character name */
	if (!process_pref_file(buf, true, true)) {
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
	Term_redraw_screen(0);
	Term_pop();
}

static void ui_enter_world(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	struct display_term *display_cave = display_term_get(DISPLAY_CAVE);

	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP | PR_MONSTER | PR_MESSAGE);
	redraw_stuff(player);

	/* Player HP can optionally change the colour of the '@' now. */
	event_add_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date */
	event_add_handler(EVENT_MAP, update_maps, display_cave);
#ifdef MAP_DEBUG
	event_add_handler(EVENT_MAP, trace_map_updates, display_cave);
#endif

	/* Check if the panel should shift when the player's moved */
	event_add_handler(EVENT_PLAYERMOVED, check_viewport, display_cave);

	/* Take note of what's on the floor */
	event_add_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Enter a store */
	event_add_handler(EVENT_ENTER_STORE, enter_store, NULL);

	/* Display an explosion */
	event_add_handler(EVENT_EXPLOSION, display_explosion, display_cave);

	/* Display a bolt spell */
	event_add_handler(EVENT_BOLT, display_bolt, display_cave);

	/* Display a physical missile */
	event_add_handler(EVENT_MISSILE, display_missile, display_cave);

	/* Check to see if the player has tried to cancel game processing */
	event_add_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, display_cave);

	/* Do the visual updates required on a new dungeon level */
	event_add_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, display_cave);

	/* Automatically clear messages while the game is repeating commands */
	event_add_handler(EVENT_COMMAND_REPEAT, repeated_command_display, NULL);

	/* Do animations (e.g. monster colour changes) */
	event_add_handler(EVENT_ANIMATE, animate, NULL);

	/* Allow the player to cheat death, if appropriate */
	event_add_handler(EVENT_CHEAT_DEATH, cheat_death, NULL);
}

static void ui_leave_world(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	struct display_term *display_cave = display_term_get(DISPLAY_CAVE);

	/* Player HP can optionally change the colour of the '@' now. */
	event_remove_handler(EVENT_HP, hp_colour_change, NULL);

	/* Simplest way to keep the map up to date */
	event_remove_handler(EVENT_MAP, update_maps, display_cave);
#ifdef MAP_DEBUG
	event_remove_handler(EVENT_MAP, trace_map_updates, display_cave);
#endif

	/* Check if the panel should shift when the player's moved */
	event_remove_handler(EVENT_PLAYERMOVED, check_viewport, display_cave);

	/* Take note of what's on the floor */
	event_remove_handler(EVENT_SEEFLOOR, see_floor_items, NULL);

	/* Display an explosion */
	event_remove_handler(EVENT_EXPLOSION, display_explosion, display_cave);

	/* Display a bolt spell */
	event_remove_handler(EVENT_BOLT, display_bolt, display_cave);

	/* Display a physical missile */
	event_remove_handler(EVENT_MISSILE, display_missile, display_cave);

	/* Check to see if the player has tried to cancel game processing */
	event_remove_handler(EVENT_CHECK_INTERRUPT, check_for_player_interrupt, display_cave);

	/* Do the visual updates required on a new dungeon level */
	event_remove_handler(EVENT_NEW_LEVEL_DISPLAY, new_level_display_update, display_cave);

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

	/* Tell the UI to ignore all pending input */
	event_add_handler(EVENT_INPUT_FLUSH, inkey_flush, NULL);
}

static void ui_leave_game(game_event_type type,
		game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Tell the UI to ignore all pending input */
	event_remove_handler(EVENT_INPUT_FLUSH, inkey_flush, NULL);
}

void display_term_create(enum display_term_index index,
		const struct term_create_info *info)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term == NULL);

	dt->term = Term_create(info);

	dt->width = info->width;
	dt->height = info->height;

	if (index != DISPLAY_CAVE) {
		display_term_handler(dt, true);
	}

	dt->active = true;
}

void display_term_destroy(enum display_term_index index)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term != NULL);

	if (index != DISPLAY_CAVE) {
		display_term_handler(dt, false);
	}

	Term_destroy(dt->term);
	dt->term = NULL;

	memset(&dt->coords, 0, sizeof(dt->coords));
	memset(&dt->messages, 0, sizeof(dt->messages));

	dt->active = false;
}

void display_term_resize(enum display_term_index index,
		int cols, int rows)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term != NULL);

	Term_push(dt->term);
	Term_resize(cols, rows);
	Term_pop();

	dt->width = cols;
	dt->height = rows;

	dt->messages.clear = true;
}

void display_term_get_coords(enum display_term_index index, struct loc *coords)
{
	struct display_term *dt = display_term_get(index);

	*coords = dt->coords;
}

void display_term_rel_coords(enum display_term_index index, struct loc *coords)
{
	struct display_term *dt = display_term_get(index);

	coords->x -= dt->coords.x;
	coords->y -= dt->coords.y;
}

void display_term_set_coords(enum display_term_index index, struct loc coords)
{
	struct display_term *dt = display_term_get(index);

	dt->coords = coords;
}

void display_term_get_area(enum display_term_index index,
		struct loc *coords, int *width, int *height)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term != NULL);

	coords->x = dt->coords.x;
	coords->y = dt->coords.y;

	*width  = dt->width;
	*height = dt->height;
}

void display_term_push(enum display_term_index index)
{
	struct display_term *dt = display_term_get(index);

	Term_push(dt->term);
}

void display_term_pop(void)
{
	Term_pop();
}

bool display_term_active(enum display_term_index index)
{
	struct display_term *dt = display_term_get(index);

	return dt->active;
}

void display_term_off(enum display_term_index index)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term != NULL);

	assert(dt->active);
	dt->active = false;
}

void display_term_on(enum display_term_index index)
{
	struct display_term *dt = display_term_get(index);
	assert(dt->term != NULL);

	assert(!dt->active);
	dt->active = true;

	Term_push(dt->term);
	Term_erase_all();
	Term_flush_output();
	Term_pop();

	display_terms_redraw();
}

void init_terms(void)
{
	display_terms_check();

	/**
	 * This term is always on the stack;
	 * this is necessary because the rest of textui depends
	 * on the fact that term callbacks can always be invoked
	 */
	Term_push(display_term_get(DISPLAY_CAVE)->term);
}

void init_display(void)
{
	init_terms();

	event_add_handler(EVENT_ENTER_INIT, ui_enter_init, NULL);
	event_add_handler(EVENT_LEAVE_INIT, ui_leave_init, NULL);

	event_add_handler(EVENT_ENTER_GAME, ui_enter_game, NULL);
	event_add_handler(EVENT_LEAVE_GAME, ui_leave_game, NULL);

	event_add_handler(EVENT_ENTER_WORLD, ui_enter_world, NULL);
	event_add_handler(EVENT_LEAVE_WORLD, ui_leave_world, NULL);

	ui_init_birthstate_handlers();
}

/* Array of display terms */

static struct display_term display_terms[] = {
	#define DISPLAY(i, desc, minc, minr, defc, defr, maxc, maxr, req) \
		{ \
			.index = DISPLAY_ ##i, \
			.term = NULL, \
			.messages = {0, false}, \
			.coords = {0}, \
			.name = (desc), \
			.required = (req), \
			.active = false \
		},
	#include "list-display-terms.h"
	#undef DISPLAY
};

static struct display_term *display_term_get(enum display_term_index index)
{
	assert(index >= 0);
	assert(index < N_ELEMENTS(display_terms));
	assert(display_terms[index].index == index);

	return &display_terms[index];
}

static void display_terms_check(void)
{
	for (size_t i = 0; i < N_ELEMENTS(display_terms); i++) {
		assert(display_terms[i].index == i);

		if (display_terms[i].required && display_terms[i].term == NULL) {
			quit_fmt("Display '%s' is not initialized!", display_terms[i].name);
		}
	}
}

/**
 * This function performs various low level updates
 * and does a total redraw of all display terms.
 */
void display_terms_redraw(void)
{
	if (!character_dungeon) {
		return;
	}

	player->upkeep->notice |= (PN_COMBINE);

	player->upkeep->update |=
		(PU_TORCH
		 | PU_INVEN
		 | PU_BONUS
		 | PU_HP
		 | PU_SPELLS
		 | PU_UPDATE_VIEW
		 | PU_MONSTERS);

	player->upkeep->redraw |=
		(PR_BASIC
		 | PR_EXTRA
		 | PR_MAP
		 | PR_INVEN
		 | PR_EQUIP
		 | PR_MESSAGE
		 | PR_MONSTER
		 | PR_OBJECT
		 | PR_MONLIST
		 | PR_ITEMLIST);

	verify_panel(DISPLAY_CAVE);
	verify_cursor();
	handle_stuff(player);
}
