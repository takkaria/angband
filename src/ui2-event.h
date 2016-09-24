/**
 * \file ui2-event.h
 * \brief Utility functions relating to UI events
 *
 * Copyright (c) 2011 Andi Sidwell
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
#ifndef INCLUDED_UI2_EVENT_H
#define INCLUDED_UI2_EVENT_H

/**
 * The various UI events that can occur.
 */
typedef enum {
	EVT_NONE = 0,

	/* Basic events */
	EVT_KBRD,   /* Keypress */
	EVT_MOUSE,  /* Mousepress */
	EVT_RESIZE, /* Display resize */

	/* 'Abstract' events */
	EVT_ESCAPE, /* Get out of this menu */
	EVT_MOVE,   /* Menu movement */
	EVT_SELECT, /* Menu selection */
	EVT_SWITCH, /* Menu switch */
} ui_event_type;

/**
 * Key modifiers.
 */
#define KC_MOD_CONTROL  0x01
#define KC_MOD_SHIFT    0x02
#define KC_MOD_ALT      0x04
#define KC_MOD_META     0x08
#define KC_MOD_KEYPAD   0x10

/**
 * The game assumes that in certain cases, the effect of a modifer key will
 * be encoded in the keycode itself (e.g. 'A' is shift-'a').  In these cases
 * (specified below), a keypress' 'mods' value should not encode them also.
 *
 * If the character has come from the keypad:
 *   Include all mods
 * Else if the character is in the range 0x01-0x1F, and the keypress was
 * from a key that without modifiers would be in the range 0x40-0x5F:
 *   CONTROL is encoded in the keycode, and should not be in mods
 * Else if the character is in the range 0x21-0x2F, 0x3A-0x60 or 0x7B-0x7E:
 *   SHIFT is often used to produce these should not be encoded in mods
 *
 * (All ranges are inclusive.)
 *
 * You can use these macros for part of the above conditions.
 */
#define MODS_INCLUDE_CONTROL(v) \
	(((v) >= 0x01 && (v) <= 0x1F) ? false : true)

#define MODS_INCLUDE_SHIFT(v) \
	((((v) >= 0x21 && (v) <= 0x2F) || \
			((v) >= 0x3A && (v) <= 0x60) || \
			((v) >= 0x7B && (v) <= 0x7E)) ? false : true)

/**
 * If keycode you're trying to apply control to is between 0x40-0x5F
 * inclusive, then you should take 0x40 from the keycode and leave
 * KC_MOD_CONTROL unset.  Otherwise, leave the keycode alone and set
 * KC_MOD_CONTROL in mods.
 *
 * This macro returns true in the former case and false in the latter.
 */
#define ENCODE_KTRL(v) \
	(((v) >= 0x40 && (v) <= 0x5F) ? true : false)

/**
 * Given a character X, turn it into a control character.
 */
#define KTRL(X) \
	((X) & 0x1F)

/**
 * Given a control character X, turn it into its uppercase ASCII equivalent.
 */
#define UN_KTRL(X) \
	((X) + 64)

/**
 * Mouse button definitions
 */

enum {
	/* Backwards compat */
	MOUSE_BUTTON_LEFT = 1,
	MOUSE_BUTTON_RIGHT = 2,
	MOUSE_BUTTON_MIDDLE = 3
};

/**
 * Convert a mouse event into a location (x coordinate)
 */
#define EVENT_GRID_X(e) \
	(event_grid_x((e).mouse.x))

/**
 * Convert a mouse event into a location (y coordinate)
 */
#define EVENT_GRID_Y(e) \
  (event_grid_y((e).mouse.y))

/**
 * Keyset mappings for various keys.
 */
#define ARROW_DOWN    0x80
#define ARROW_LEFT    0x81
#define ARROW_RIGHT   0x82
#define ARROW_UP      0x83

#define KC_F1         0x84
#define KC_F2         0x85
#define KC_F3         0x86
#define KC_F4         0x87
#define KC_F5         0x88
#define KC_F6         0x89
#define KC_F7         0x8A
#define KC_F8         0x8B
#define KC_F9         0x8C
#define KC_F10        0x8D
#define KC_F11        0x8E
#define KC_F12        0x8F
#define KC_F13        0x90
#define KC_F14        0x91
#define KC_F15        0x92

#define KC_HELP       0x93
#define KC_HOME       0x94
#define KC_PGUP       0x95
#define KC_END        0x96
#define KC_PGDOWN     0x97
#define KC_INSERT     0x98
#define KC_PAUSE      0x99
#define KC_BREAK      0x9a
#define KC_BEGIN      0x9b
#define KC_ENTER      0x9c /* ASCII \r */
#define KC_TAB        0x9d /* ASCII \t */
#define KC_DELETE     0x9e
#define KC_BACKSPACE  0x9f /* ASCII \h */
#define ESCAPE        0xE000

/* we have up until 0x9F before we start edging into displayable Unicode */
/* then we could move into private use area 1, 0xE000 onwards */

/**
 * Analogous to isdigit() etc in ctypes
 */
#define isarrow(c)  ((c >= ARROW_DOWN) && (c <= ARROW_UP))

/**
 * Type capable of holding any input key we might want to use.
 */
typedef u32b keycode_t;

/**
 * Struct holding all relevant info for keypresses.
 */
struct keypress {
	ui_event_type type;
	keycode_t code;
	byte mods;
};

/**
 * Null keypress constant, for safe initializtion.
 */
static struct keypress const KEYPRESS_NULL = {
	.type = EVT_NONE,
	.code = 0,
	.mods = 0
};

/**
 * Struct holding all relevant info for mouse clicks.
 */
struct mouseclick {
	ui_event_type type;
	byte x;
	byte y;
	byte button;
	byte mods;

	/* Non-negative index is an index of a term that presumably was clicked.
	 * Negative index is an index of a temporary term (just make it -1) */
	int index;
};

/**
 * Union type to hold information about any given event.
 */
typedef union {
	ui_event_type type;
	struct mouseclick mouse;
	struct keypress key;
} ui_event;

/**
 * Easy way to initialise a ui_event without seeing the gory bits.
 */
#define EVENT_EMPTY {0}

/*** Functions ***/

/**
 * Given a string (and that string's length), return the corresponding keycode 
 */
keycode_t keycode_find_code(const char *str, size_t len);

/**
 * Given a keycode, return its description
 */
const char *keycode_find_desc(keycode_t kc);

/**
 * Convert a string of keypresses into their textual representation
 */
void keypress_to_text(char *buf, size_t len,
		const struct keypress *src, bool expand_backslash);

/**
 * Convert a textual representation of keypresses into actual keypresses
 */
void keypress_from_text(struct keypress *buf, size_t len, const char *str);

/**
 * Convert a keypress into something the user can read
 */
void keypress_to_readable(char *buf, size_t len, struct keypress src);

bool char_matches_key(wchar_t c, keycode_t key);

/*
 * Convert relative coordinates on the grid to absolute ones
 */
int event_grid_x(int x);
int event_grid_y(int y);

#endif /* UI2_EVENT_H */
