/**
 * \file ui2-output.h
 * \brief Putting text on the screen, screen saving and loading, panel handling
 *
 * Copyright (c) 2007 Pete Mack and others.
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

#ifndef UI2_OUTPUT_H
#define UI2_OUTPUT_H

#include "ui2-event.h"
#include "ui2-display.h"
#include "z-type.h"
#include "z-textblock.h"

/* scroll when player is too close to the edge of a term
 * TODO this should be an option */
#define SCROLL_DISTANCE 3

/**
 * ------------------------------------------------------------------------
 * Regions
 * ------------------------------------------------------------------------ */
/**
 * Defines a rectangle on the screen that is bound to a Panel or subpanel
 */
typedef struct region region;

/* non-positive values of x, y, width or height
 * are relative, to, respectively,
 * left, top, right and bottom of the subwindow */
struct region {
	int x;
	int y;
	int w;
	int h;
};

/**
 * Erase the contents of a region
 */
void region_erase(region reg);

/**
 * Given a region with relative values, make them absolute
 */
region region_calculate(region reg);

/**
 * Check whether a mouse event is inside a region
 */
bool mouse_in_region(struct mouseclick mouse, region reg);

/*
 * Check where localtion (x, y) is inside a region
 */
bool loc_in_region(struct loc loc, region reg);

/**
 * ------------------------------------------------------------------------
 * Text display
 * ------------------------------------------------------------------------ */

void textui_textblock_show(textblock *tb,
		enum term_position position, region orig_area, const char *header);
void textui_textblock_place(textblock *tb, region orig_area, const char *header);

/**
 * ------------------------------------------------------------------------
 * text_out
 * ------------------------------------------------------------------------ */
struct text_out_info {
	int wrap;
	int indent;
	int pad;
};

void text_out(struct text_out_info info, const char *fmt, ...);
void text_out_c(struct text_out_info info, uint32_t attr, const char *fmt, ...);
void text_out_e(struct text_out_info info, const char *fmt, ...);

/**
 * ------------------------------------------------------------------------
 * Simple text display
 * ------------------------------------------------------------------------ */
void erase_line(struct loc at);

void c_put_str_len(uint32_t attr, const char *str, struct loc at, int len);
void put_str_len(const char *str, struct loc at, int len);
void c_prt_len(uint32_t attr, const char *str, struct loc at, int len);
void prt_len(const char *str, struct loc at, int len);

void c_put_str(uint32_t attr, const char *str, struct loc at);
void put_str(const char *str, struct loc at);
void c_prt(uint32_t attr, const char *str, struct loc at);
void prt(const char *str, struct loc at);

void show_prompt(const char *str, bool cursor);
void clear_prompt(void);

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------ */
bool panel_should_modify(enum display_term_index i, struct loc new_coords);
bool modify_panel(enum display_term_index i, struct loc new_coords);
bool adjust_panel(enum display_term_index i, struct loc new_coords);
bool change_panel(enum display_term_index i, int dir);
void verify_panel(enum display_term_index i);
void center_panel(enum display_term_index i);

void clear_from(int row);

bool textui_map_is_visible(void);

void get_cave_region(region *reg);
void textui_get_panel(int *min_y, int *min_x, int *max_y, int *max_x);
bool textui_panel_contains(unsigned int y, unsigned int x);

#endif /* UI2_OUTPUT_H */
