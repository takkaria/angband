/**
 * \file ui2-store.c
 * \brief Store UI
 *
 * Copyright (c) 1997 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 1998-2014 Angband developers
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
#include "game-event.h"
#include "game-input.h"
#include "hint.h"
#include "init.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-history.h"
#include "store.h"
#include "target.h"
#include "ui2-display.h"
#include "ui2-input.h"
#include "ui2-menu.h"
#include "ui2-object.h"
#include "ui2-options.h"
#include "ui2-knowledge.h"
#include "ui2-object.h"
#include "ui2-player.h"
#include "ui2-spell.h"
#include "ui2-command.h"
#include "ui2-store.h"
#include "z-debug.h"

/**
 * Shopkeeper welcome messages.
 *
 * The shopkeeper's name must come first, then the character's name.
 */
static const char *comment_welcome[] = {
	"",
	"%s nods to you.",
	"%s says hello.",
	"%s: \"See anything you like, adventurer?\"",
	"%s: \"How may I help you, %s?\"",
	"%s: \"Welcome back, %s.\"",
	"%s: \"A pleasure to see you again, %s.\"",
	"%s: \"How may I be of assistance, good %s?\"",
	"%s: \"You do honour to my humble store, noble %s.\"",
	"%s: \"I and my family are entirely at your service, %s.\""
};

static const char *comment_hint[] = {
	/*"%s tells you soberly: \"%s\".",
	"(%s) There's a saying round here, \"%s\".",
	"%s offers to tell you a secret next time you're about.",*/
	"\"%s\""
};

/**
 * Easy names for the elements of the term_loc array.
 */
enum {
	LOC_PRICE = 0,
	LOC_OWNER,
	LOC_HEADER,
	LOC_HELP_CLEAR,
	LOC_HELP_PROMPT,
	LOC_AU,
	LOC_WEIGHT,

	LOC_MAX
};

static const region store_menu_region = {
	.x =  1,
	.y =  3,
	.w = -1,
	.h = -2
};

/* State flags */
#define STORE_GOLD_CHANGE  (1 << 0)
#define STORE_FRAME_CHANGE (1 << 1)
#define STORE_SHOW_HELP    (1 << 2)

/* Compound flag for the initial display of a store */
#define STORE_INIT_CHANGE  (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE)

struct store_context {
	struct menu menu;     /* Menu instance */
	struct store *store;  /* Pointer to store */
	struct object **list; /* List of objects */
	int flags;            /* Display flags */
	bool inspect_only;    /* Only allow looking */

	struct text_out_info text_out;

	/* Places for the various things displayed onscreen */
	struct loc term_loc[LOC_MAX];
};

/* Return a random hint from the global hints list */
static const char *random_hint(void)
{
	struct hint *h = hints;

	for (int n = 2; h->next && one_in_(n); n++) {
		h = h->next;
	}

	return h->hint;
}

/**
 * The greeting a shopkeeper gives the character says a lot about his
 * general attitude.
 *
 * Taken and modified from Sangband 1.0.
 *
 * Note that each comment_hint should have exactly one %s
 */
static void prt_welcome(const struct owner *proprietor)
{
	if (one_in_(2)) {
		return;
	}

	if (one_in_(3)) {
		int index = randint0(N_ELEMENTS(comment_hint));
		msg(comment_hint[index], random_hint());
	} else if (player->lev > 5) {
		char short_name[20] = {0};
		my_strcpy(short_name, proprietor->name, sizeof(short_name));
		/* Get the first name of the store owner (truncate at the first space) */
		for (size_t i = 0; i < sizeof(short_name) && short_name[i]; i++) {
			if (short_name[i] == ' ') {
				short_name[i] = 0;
				break;
			}
		}

		/* We go from level 1 - 50  */
		size_t l = ((unsigned) player->lev - 1) / 5;
		size_t i = MIN(l, N_ELEMENTS(comment_welcome) - 1);

		const char *player_name;

		/* Get a title for the character */
		if (i % 2 && one_in_(2)) {
			player_name = player->class->title[l];
		} else if (one_in_(2)) {
			player_name = op_ptr->full_name;
		} else {
			player_name = "valued customer";
		}

		/* Balthazar says "Welcome" */
		show_prompt(format(comment_welcome[i], short_name, player_name), false);
	}
}

/*** Display code ***/

/**
 * This function sets up screen locations based on the current term size.
 *
 * Current screen layout:
 *  line 0: shopkeeper and their purse / item buying price
 *  line 1: empty
 *  line 2: table headers
 *
 *  line 3: Start of items
 *
 * If help is turned off, then the rest of the display goes as:
 *
 *  line (height - 3): end of items
 *  line (height - 2): empty
 *  line (height - 1): Help prompt and remaining gold
 *
 * If help is turned on, then the rest of the display goes as:
 *
 *  line (height - 6): end of items
 *  line (height - 5): empty
 *  line (height - 4): gold remaining
 *  line (height - 3): command help 
 */
static void store_display_recalc(struct store_context *context)
{
	struct menu *menu = &context->menu;
	struct store *store = context->store;

	int term_width;
	int term_height;
	Term_get_size(&term_width, &term_height);

	/* Clip the width at a max of 104 (enough room for an 80-char item name) */
	term_width = MIN(term_width, 104);

	/* Clip the text_out function at one smaller than the screen width */
	context->text_out.indent = 1;
	context->text_out.pad    = 0;
	context->text_out.wrap   = term_width - 1;

	memset(context->term_loc, 0, sizeof(context->term_loc));

	/* X coords first */
	context->term_loc[LOC_AU].x     = term_width - 26;
	context->term_loc[LOC_OWNER].x  = term_width - 1;
	context->term_loc[LOC_WEIGHT].x = term_width - 9;
	context->term_loc[LOC_PRICE].x  = term_width - 10;

	if (store->sidx != STORE_HOME) {
		/* Add space for for prices */
		context->term_loc[LOC_WEIGHT].x -= 10;
	}

	/* Then Y */
	context->term_loc[LOC_OWNER].y = 0;
	context->term_loc[LOC_HEADER].y = 2;

	/* If we are displaying help, make the height smaller */
	if (context->flags & STORE_SHOW_HELP) {
		term_height -= context->inspect_only? 2 : 3;
	}

	context->term_loc[LOC_AU].y = term_height - 1;

	region reg = store_menu_region;

	/* If we're displaying the help, then put it with a line of padding */
	if (context->flags & STORE_SHOW_HELP) {
		context->term_loc[LOC_HELP_CLEAR].y  = term_height - 1;
		context->term_loc[LOC_HELP_PROMPT].y = term_height;
		reg.h = -5;
	} else {
		context->term_loc[LOC_HELP_CLEAR].y  = term_height - 2;
		context->term_loc[LOC_HELP_PROMPT].y = term_height - 1;
		reg.h = -2;
	}

	menu_layout(menu, reg);
}

/**
 * Redisplay a single store entry
 */
static void store_display_entry(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct store_context *context = menu_priv(menu);
	struct store *store = context->store;
	assert(store != NULL);

	/* Get the object */
	struct object *obj = context->list[index];

	/* Describe the object - preserving insriptions in the home */
	int desc = (store->sidx == STORE_HOME) ?
		ODESC_PREFIX | ODESC_FULL : ODESC_PREFIX | ODESC_FULL | ODESC_STORE;

	char o_name[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(o_name, sizeof(o_name), obj, desc);

	/* Display the object */
	c_put_str(obj->kind->base->attr, o_name, loc);

	/* Show weights */
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf), "%3d.%d lb", obj->weight / 10, obj->weight % 10);

	uint32_t color = menu_row_style(true, cursor);

	loc.x = context->term_loc[LOC_WEIGHT].x;
	c_put_str(color, buf, loc);

	/* Describe an object (fully) in a store */
	if (store->sidx != STORE_HOME) {
		/* Extract the "minimum" price */
		int price = price_item(store, obj, false, 1);

		/* Make sure the player can afford it */
		if (player->au < price) {
			color = menu_row_style(false, cursor);
		}

		/* Actually draw the price */
		strnfmt(buf, sizeof(buf), "%9d    ", price);

		loc.x = context->term_loc[LOC_PRICE].x;
		c_put_str(color, buf, loc);
	}
}

/**
 * Display store (after clearing screen)
 */
static void store_display_frame(struct store_context *context)
{
	struct store *store = context->store;
	struct owner *proprietor = store->owner;

	Term_clear();

	if (store->sidx == STORE_HOME) {
		/* The "Home" is special */
		struct loc loc;

		loc.x = 1;
		loc.y = context->term_loc[LOC_OWNER].y;
		/* Put the owner name */
		put_str(op_ptr->full_name, loc);

		loc.x = 1;
		loc.y = context->term_loc[LOC_HEADER].y;
		/* Label the object descriptions */
		put_str("Home Inventory", loc);

		loc.x = context->term_loc[LOC_WEIGHT].x + 2;
		loc.y = context->term_loc[LOC_HEADER].y;
		/* Show weight header */
		put_str("Weight", loc);
	} else {
		/* Normal stores */
		char buf[ANGBAND_TERM_STANDARD_WIDTH];
		const char *owner_name = proprietor->name;
		struct loc loc;

		loc.x = 1;
		loc.y = context->term_loc[LOC_OWNER].y;
		/* Put the owner name */
		put_str(owner_name, loc);

		/* Show the max price in the store (above prices) */
		size_t len = strnfmt(buf, sizeof(buf), "%d", proprietor->max_cost);

		loc.x = context->term_loc[LOC_OWNER].x - len;
		loc.y = context->term_loc[LOC_OWNER].y;
		prt(buf, loc);

		loc.x = 1;
		loc.y = context->term_loc[LOC_HEADER].y;
		/* Label the object descriptions */
		put_str("Store Inventory", loc);

		loc.x = context->term_loc[LOC_WEIGHT].x + 2;
		loc.y = context->term_loc[LOC_HEADER].y;
		/* Showing weight label */
		put_str("Weight", loc);

		loc.x = context->term_loc[LOC_PRICE].x + 4;
		loc.y = context->term_loc[LOC_HEADER].y;
		/* Label the asking price (in stores) */
		put_str("Price", loc);
	}
}

/**
 * Display help.
 */
static void store_display_help(struct store_context *context)
{
	const bool home = (context->store->sidx == STORE_HOME) ? true : false;
	struct text_out_info info = context->text_out;

	/* Prepare help hooks */
	clear_from(context->term_loc[LOC_HELP_CLEAR].y);
	Term_cursor_to_xy(info.indent, context->term_loc[LOC_HELP_PROMPT].y);

	if (OPT(rogue_like_commands)) {
		text_out_c(info, COLOUR_L_GREEN, "x");
	} else {
		text_out_c(info, COLOUR_L_GREEN, "l");
	}

	text_out(info, " examines");
	if (!context->inspect_only) {
		text_out(info, " and ");
		text_out_c(info, COLOUR_L_GREEN, "p");
		text_out(info, home ? " picks up" : " purchases");
	}
	text_out(info, " the selected item. ");

	if (!context->inspect_only) {
		if (OPT(birth_no_selling)) {
			text_out_c(info, COLOUR_L_GREEN, "d");
			text_out(info,
					" gives an item to the store in return " /* concat */
					"for its identification. "               /* concat */
					"Some wands and staves will also be recharged. ");
		} else {
			text_out_c(info, COLOUR_L_GREEN, "d");
			text_out(info, home ? " drops" : " sells");
			text_out(info, " an item from your inventory. ");
		}
	} else {
		text_out_c(info, COLOUR_L_GREEN, "I");
		text_out(info, " inspects an item from your inventory.\n");
	}

	text_out_c(info, COLOUR_L_GREEN, "ESC");
	if (!context->inspect_only) {
		text_out(info, " exits the building.");
	} else {
		text_out(info, " exits this screen.");
	}
}

/**
 * Decides what parts of the store display to redraw.
 */
static void store_redraw(struct store_context *context)
{
	if (context->flags & STORE_FRAME_CHANGE) {
		store_display_frame(context);

		if (context->flags & STORE_SHOW_HELP) {
			store_display_help(context);
		} else {
			struct loc loc = {
				.x = 1,
				.y = context->term_loc[LOC_HELP_PROMPT].y
			};
			prt("Press '?' for help.", loc);
		}

		context->flags &= ~STORE_FRAME_CHANGE;
	}

	if (context->flags & STORE_GOLD_CHANGE) {
		prt(format("Gold Remaining: %9d", (int) player->au),
				context->term_loc[LOC_AU]);
		context->flags &= ~STORE_GOLD_CHANGE;
	}
}

static bool store_get_check(const char *prompt)
{
	show_prompt(prompt, false);

	struct keypress key = inkey_only_key();

	clear_prompt();

	switch (key.code) {
		case ESCAPE: case 'N' : case 'n':
			return false;
		default:
			return true;
	}
}

/*
 * Sell an object, or drop if it we're in the home.
 */
static bool store_sell(struct store_context *context)
{
	int get_mode = USE_EQUIP | USE_INVEN | USE_FLOOR | USE_QUIVER;

	struct store *store = context->store;

	struct object *obj;
	item_tester tester = NULL;

	const char *reject = "You have nothing that I want. ";
	const char *prompt = OPT(birth_no_selling) ?
		"Give which item? " : "Sell which item? ";

	assert(store);

	/* Clear all current messages */
	clear_prompt();

	if (store->sidx == STORE_HOME) {
		prompt = "Drop which item? ";
	} else {
		tester = store_will_buy_tester;
		get_mode |= SHOW_PRICES;
	}

	/* Get an item */
	player->upkeep->command_wrk = USE_INVEN;

	if (!get_item(&obj, prompt, reject, CMD_DROP, tester, get_mode)) {
		return false;
	}

	/* Cannot remove cursed objects */
	if (object_is_equipped(player->body, obj) && !obj_can_takeoff(obj)) {
		msg("Hmmm, it seems to be stuck.");
		return false;
	}

	/* Get a quantity */
	int amt = get_quantity(NULL, obj->number);

	/* Allow user abort */
	if (amt <= 0) {
		return false;
	}

	struct object object_type_body = OBJECT_NULL;
	struct object *temp_obj = &object_type_body;

	/* Get a copy of the object representing the number being sold */
	object_copy_amt(temp_obj, obj, amt);

	if (!store_check_num(store, temp_obj)) {
		if (store->sidx == STORE_HOME) {
			msg("Your home is full.");
		} else {
			msg("I have not the room in my store to keep it.");
		}

		return false;
	}

	/* Real store */
	if (store->sidx != STORE_HOME) {
		/* Get a full description */
		char o_name[ANGBAND_TERM_STANDARD_WIDTH];
		object_desc(o_name, sizeof(o_name), temp_obj,
				ODESC_PREFIX | ODESC_FULL);

		/* Extract the value of the items */
		int price = price_item(store, temp_obj, true, amt);

		/* Confirm sale */
		char buf[ANGBAND_TERM_STANDARD_WIDTH];
		strnfmt(buf, sizeof(buf),
				"%s %s%s? [ESC, any other key to accept]",
				OPT(birth_no_selling) ? "Give" : "Sell",
				o_name,
				OPT(birth_no_selling) ? "" : format(" for %d", price));
		if (!store_get_check(buf)) {
			return false;
		}

		cmdq_push(CMD_SELL);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	} else {
		/* Player is at home */
		cmdq_push(CMD_STASH);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	}

	/* Update the display */
	context->flags |= STORE_GOLD_CHANGE;

	return true;
}

/**
 * Buy an object from a store
 */
static bool store_purchase(struct store_context *context, int item, bool single)
{
	struct store *store = context->store;
	struct object *obj = context->list[item];

	/* Get an amount if we weren't given one */
	int amt = 0;
	if (single) {
		amt = 1;
		/* Check if the player can afford any at all */
		if (store->sidx != STORE_HOME
				&& player->au < price_item(store, obj, false, 1))
		{
			msg("You do not have enough gold for this item.");
			return false;
		}
	} else {
		if (store->sidx == STORE_HOME) {
			amt = obj->number;
		} else {
			/* Price of one */
			int price_one = price_item(store, obj, false, 1);

			/* Check if the player can afford any at all */
			if (player->au < price_one) {
				msg("You do not have enough gold for this item.");
				return false;
			}

			/* Work out how many the player can afford */
			amt = price_one > 0 ? player->au / price_one : obj->number;
			amt = MIN(amt, obj->number);

			/* Double check for wands/staves */
			if (amt < obj->number
					&& player->au >= price_item(store, obj, false, amt + 1))
			{
				amt++;
			}
		}

		/* Limit to the number that can be carried */
		int carry_num = inven_carry_num(obj, false);
		amt = MIN(amt, carry_num);

		/* XXX ? */
		bool aware = object_flavor_is_aware(obj);

		/* Fail if there is no room */
		if (amt <= 0 || (!aware && pack_is_full())) {
			msg("You cannot carry that many items.");
			return false;
		}

		/* Find the number of this item in the inventory */
		int num = aware ? find_inven(obj) : 0;

		char buf[ANGBAND_TERM_STANDARD_WIDTH];
		strnfmt(buf, sizeof(buf),
				"%s how many%s? (max %d) ",
				store->sidx == STORE_HOME ? "Take" : "Buy",
				num > 0 ? format(" (you have %d)", num) : "",
				amt);

		/* Get a quantity */
		amt = get_quantity(buf, amt);

		/* Allow user abort */
		if (amt <= 0) {
			return false;
		}
	}

	/* Get desired object */
	struct object *dummy = object_new();
	object_copy_amt(dummy, obj, amt);

	/* Ensure we have room */
	if (!inven_carry_okay(dummy)) {
		msg("You cannot carry that many items.");
		object_delete(&dummy);
		return false;
	}

	/* Describe the object (fully) */
	char o_name[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(o_name, sizeof(o_name), dummy,
			ODESC_PREFIX | ODESC_FULL | ODESC_STORE);

	/* Attempt to buy it */
	if (store->sidx != STORE_HOME) {
		/* Extract the price for the entire stack */
		int price = price_item(store, dummy, false, dummy->number);

		/* Confirm purchase */
		bool response =
			store_get_check(format("Buy %s for %d? [ESC, any other key to accept]",
						o_name, price));

		/* Negative response, so give up */
		if (!response) {
			return false;
		}

		cmdq_push(CMD_BUY);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	} else {
		cmdq_push(CMD_RETRIEVE);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	}

	/* Update the display */
	context->flags |= STORE_GOLD_CHANGE;

	object_delete(&dummy);

	return true;
}

/**
 * Examine an item in a store
 */
static void store_examine(struct store_context *context, int item)
{
	if (item < 0) {
		return;
	}

	/* Get the actual object */
	struct object *obj = context->list[item];

	/* Show full info in most stores, but normal info in player home */
	textblock *tb = object_info(obj, OINFO_NONE);

	char header[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(header, sizeof(header), obj,
			ODESC_PREFIX | ODESC_FULL | ODESC_STORE);

	region reg = {0};
	textui_textblock_show(tb, reg, header);
	textblock_free(tb);

	/* Browse book, then prompt for a command */
	if (obj_can_browse(obj)) {
		textui_book_browse(obj);
	}
}

static void store_menu_set_selections(struct menu *menu, bool knowledge_menu)
{
	/* Note that command_keys and selection can't intersect! */
	if (knowledge_menu) {
		if (OPT(rogue_like_commands)) {
			menu->command_keys = "?|Ieilx";
			menu->selections = "abcdfghjkmnopqrstuvwyz134567";
		} else {
			menu->command_keys = "?|Ieil";
			menu->selections = "abcdfghjkmnopqrstuvwxyz13456";
		}
	} else {
		if (OPT(rogue_like_commands)) {
			/* \x04 = ^D, \x05 = ^E, \x10 = ^p */
			menu->command_keys = "\x04\x05\x10?={|}~CEIPTdegilpswx";
			menu->selections = "abcfmnoqrtuvyz13456790ABDFGH";
		} else {
			/* \x05 = ^E, \x10 = ^p */
			menu->command_keys = "\x05\x010?={|}~CEIbdegiklpstwx";
			menu->selections = "acfhjmnoqruvyz13456790ABDFGH";
		}
	}
}

static void store_menu_recalc(struct menu *menu)
{
	struct store_context *context = menu_priv(menu);

	menu_setpriv(menu, context->store->stock_num, context);
}

/**
 * Process a command in a store
 *
 * Note that we must allow the use of a few "special" commands in the stores
 * which are not allowed in the dungeon, and we must disable some commands
 * which are allowed in the dungeon but not in the stores, to prevent chaos.
 */
static bool store_process_command_key(struct keypress kp)
{
	cmd_code cmd = CMD_NULL;

	/* Process the keycode */
	switch (kp.code) {
		/* roguelike */
		case 'T': case 't':
			cmd = CMD_TAKEOFF;
			break;
		/* roguelike */
		case KTRL('D'): case 'k':
			textui_cmd_ignore();
			break;
		/* roguelike */
		case 'P': case 'b':
			textui_spell_browse();
			break;
		case '~':
			textui_browse_knowledge();
			break;
		case 'I':
			textui_obj_examine();
			break;
		case 'w':
			cmd = CMD_WIELD;
			break;
		case '{':
			cmd = CMD_INSCRIBE;
			break;
		case '}':
			cmd = CMD_UNINSCRIBE;
			break;
		case 'e':
			do_cmd_equip();
			break;
		case 'i':
			do_cmd_inven();
			break;
		case '|':
			do_cmd_quiver();
			break;
		case KTRL('E'):
			toggle_inven_equip();
			break;
		case 'C':
			do_cmd_change_name();
			break;
		case KTRL('P'):
			do_cmd_messages();
			break;
		default:
			return false;
	}

	if (cmd != CMD_NULL) {
		cmdq_push_repeat(cmd, 0);
	}

	return true;
}

/**
 * Select an item from the store's stock, and return the stock index
 */
static int store_get_stock(struct menu *menu, int index)
{
	bool mn_no_action = mnflag_has(menu->flags, MN_NO_ACTION);

	/* Set a flag to make sure that we get the selection or escape
	 * without running the menu handler */
	mnflag_on(menu->flags, MN_NO_ACTION);

	ui_event event = menu_select(menu);

	if (!mn_no_action) {
		mnflag_off(menu->flags, MN_NO_ACTION);
	}

	if (event.type == EVT_SELECT) {
		return menu->cursor;
	} else if (event.type == EVT_ESCAPE) {
		return -1;
	}

	/* if we do not have a new selection, just return the original item */
	return index;
}

/** Enum for context menu entries */
enum {
	ACT_INSPECT_INVEN,
	ACT_SELL,
	ACT_EXAMINE,
	ACT_BUY,
	ACT_BUY_ONE,
};

/* Pick the context menu options appropiate for a store */
static int context_menu_store(struct store_context *context,
		const int index, struct loc mloc)
{
	(void) index;

	struct store *store = context->store;
	bool home = (store->sidx == STORE_HOME) ? true : false;

	struct menu *menu = menu_dynamic_new();

	char *labels = string_make(lower_case);
	menu->selections = labels;

	menu_dynamic_add_label(menu, "Inspect inventory", 'I', ACT_INSPECT_INVEN, labels);
	menu_dynamic_add_label(menu, home ? "Stash" : "Sell", 'd', ACT_SELL, labels);

	region reg = menu_dynamic_calc_location(menu);
	struct term_hints hints = {
		.x = mloc.x,
		.y = mloc.y,
		.width = reg.w,
		.height = reg.h,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_EXACT
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	show_prompt("(Enter to select, ESC) Command:", false);

	int selected = menu_dynamic_select(menu);

	menu_dynamic_free(menu);
	string_free(labels);
	clear_prompt();
	Term_pop();

	switch (selected) {
		case ACT_SELL:
			store_sell(context);
			return true;
		case ACT_INSPECT_INVEN:
			textui_obj_examine();
			return true;
		default:
			return false;
	}
}

/* pick the context menu options appropiate for an item available in a store */
static void context_menu_store_item(struct store_context *context,
		const int index, struct loc mloc)
{
	struct store *store = context->store;
	bool home = (store->sidx == STORE_HOME) ? true : false;

	struct menu *menu = menu_dynamic_new();
	struct object *obj = context->list[index];

	char header[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(header, sizeof(header), obj, ODESC_PREFIX | ODESC_BASE);

	char *labels = string_make(lower_case);
	menu->selections = labels;

	menu_dynamic_add_label(menu, "Examine", 'x', ACT_EXAMINE, labels);
	menu_dynamic_add_label(menu, home ? "Take" : "Buy", 'd', ACT_BUY, labels);
	if (obj->number > 1) {
		menu_dynamic_add_label(menu, home ? "Take one" : "Buy one", 'o', ACT_BUY_ONE, labels);
	}

	region reg = menu_dynamic_calc_location(menu);
	struct term_hints hints = {
		.x = mloc.x,
		.y = mloc.y,
		.width = reg.w,
		.height = reg.h,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_EXACT
	};
	Term_push_new(&hints);
	menu_layout_term(menu);

	show_prompt(format("(Enter to select, ESC) Command for %s:", header), false);

	int selected = menu_dynamic_select(menu);

	menu_dynamic_free(menu);
	string_free(labels);
	clear_prompt();
	Term_pop();

	switch (selected) {
		case ACT_EXAMINE:
			store_examine(context, index);
			break;
		case ACT_BUY:
			store_purchase(context, index, false);
			break;
		case ACT_BUY_ONE:
			store_purchase(context, index, true);
			break;
	}
}

/**
 * Handle store menu input
 */
static bool store_menu_handle(struct menu *menu,
		const ui_event *event, int index)
{
	struct store_context *context = menu_priv(menu);
	struct store *store = context->store;

	bool processed = true;
	
	if (event->type == EVT_SELECT) {
		/* Nothing for now, except "handle" the event */
		return true;
		/* In future, maybe we want a display a list of what you can do. */
	} else if (event->type == EVT_MOUSE) {
		if (event->mouse.button == MOUSE_BUTTON_RIGHT) {
			/* Exit the store? What already does this? menu_handle_mouse(),
			 * so exit this so that menu_handle_mouse() will be called */
			return false;
		} else if (event->mouse.button == MOUSE_BUTTON_LEFT) {
			bool action = false;

			if (event->mouse.y == 0 || event->mouse.y == 1) {
				/* show the store context menu */
				if (!context_menu_store(context, index,
							loc(event->mouse.x, event->mouse.y)))
				{
					return false;
				}

				action = true;
			} else if (event->mouse.y == index + context->term_loc[LOC_HEADER].y + 1) {
				/* if press is on a list item, so store item context */
				context_menu_store_item(context, index,
						loc(event->mouse.x, event->mouse.y));
				action = true;
			}

			if (action) {
				context->flags |= (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE);

				/* Let the game handle any core commands (equipping, etc) */
				cmdq_pop(CMD_STORE);

				/* Notice and handle stuff */
				notice_stuff(player);
				handle_stuff(player);

				/* Display the store */
				store_display_recalc(context);
				store_menu_recalc(menu);
				store_redraw(context);

				return true;
			}
		}
	} else if (event->type == EVT_KBRD) {
		switch (event->key.code) {
			case 's': case 'd':
				store_sell(context);
				break;

			case 'p': case 'g':
				/* use the old way of purchasing items */
				if (store->sidx != STORE_HOME) {
					show_prompt("Purchase which item? (ESC to cancel, Enter to select)", false);
				} else {
					show_prompt("Get which item? (Esc to cancel, Enter to select)", false);
				}

				index = store_get_stock(menu, index);

				clear_prompt();

				if (index >= 0) {
					store_purchase(context, index, false);
				}
				break;

			case 'l': case 'x':
				/* use the old way of examining items */
				show_prompt("Examine which item? (ESC to cancel, Enter to select)", false);

				index = store_get_stock(menu, index);

				clear_prompt();

				if (index >= 0) {
					store_examine(context, index);
				}
				break;

			case '?':
				/* Toggle help */
				if (context->flags & STORE_SHOW_HELP) {
					context->flags &= ~STORE_SHOW_HELP;
				} else {
					context->flags |= STORE_SHOW_HELP;
				}

				/* Redisplay */
				context->flags |= STORE_INIT_CHANGE;

				store_display_recalc(context);
				store_redraw(context);
				break;

			case '=':
				do_cmd_options();
				store_menu_set_selections(menu, false);
				break;

			default:
				processed = store_process_command_key(event->key);
				break;
		}

		/* Let the game handle any core commands (equipping, etc) */
		cmdq_pop(CMD_STORE);

		if (processed) {
			event_signal(EVENT_INVENTORY);
			event_signal(EVENT_EQUIPMENT);
		}

		/* Notice and handle stuff */
		notice_stuff(player);
		handle_stuff(player);

		return processed;
	}

	return false;
}

static const menu_iter store_menu = {
	.display_row = store_display_entry,
	.row_handler = store_menu_handle
};

/**
 * Init the store menu
 */
static void store_menu_init(struct store_context *context,
		struct store *store, bool inspect_only)
{
	struct menu *menu = &context->menu;

	context->store = store;
	context->flags = STORE_INIT_CHANGE;
	context->inspect_only = inspect_only;
	context->list = mem_zalloc(sizeof(*context->list) * z_info->store_inven_max);

	store_stock_list(context->store, context->list, z_info->store_inven_max);

	/* Init the menu structure */
	menu_init(menu, MN_SKIN_SCROLL, &store_menu);
	menu_setpriv(menu, 0, context);

	/* Calculate the positions of things and draw */
	store_menu_set_selections(menu, inspect_only);
	store_display_recalc(context);
	store_menu_recalc(menu);
	store_redraw(context);
}

/**
 * Display contents of a store from knowledge menu
 *
 * The only allowed actions are 'I' to inspect an item
 */
void textui_store_knowledge(int store)
{
	struct store_context context;

	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, stores[store].name, COLOUR_WHITE, COLOUR_DARK);

	store_menu_init(&context, &stores[store], true);
	menu_select(&context.menu);

	Term_pop();

	mem_free(context.list);
}

/**
 * Handle stock change.
 */
static void refresh_stock(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;

	struct store_context *context = user;
	struct menu *menu = &context->menu;

	store_stock_list(context->store, context->list, z_info->store_inven_max);

	/* Display the store */
	store_display_recalc(context);
	store_menu_recalc(menu);
	store_redraw(context);
}

/**
 * Enter a store.
 */
void enter_store(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Check that we're on a store */
	if (!square_isshop(cave, player->py, player->px)) {
		msg("You see no store here.");
		return;
	}

	/* Shut down the normal game view */
	event_signal(EVENT_LEAVE_WORLD);
}

/**
 * Interact with a store.
 */
void use_store(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	struct store *store = store_at(cave, player->py, player->px);

	assert(store != NULL);

	/*** Display ***/
	struct term_hints hints = {
		.width = ANGBAND_TERM_STANDARD_WIDTH,
		.height = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_CENTER
	};
	Term_push_new(&hints);
	Term_add_tab(0, store->name, COLOUR_WHITE, COLOUR_DARK);

	/* Get a array version of the store stock, register handler for changes */
	struct store_context context;
	event_add_handler(EVENT_STORECHANGED, refresh_stock, &context);
	store_menu_init(&context, store, false);

	/* Say a friendly hello. */
	if (store->sidx != STORE_HOME) {
		prt_welcome(store->owner);
	}

	/* Shopping */
	menu_select(&context.menu);

	/* Shopping's done */
	event_remove_handler(EVENT_STORECHANGED, refresh_stock, &context);
	mem_free(context.list);

	/* Take a turn */
	player->upkeep->energy_use = z_info->move_energy;

	Term_pop();
}

void leave_store(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	/* Disable repeats */
	cmd_disable_repeat();

	/* Switch back to the normal game view. */
	event_signal(EVENT_ENTER_WORLD);
}
