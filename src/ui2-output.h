/**
 * \file ui-output.h
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
#include "z-textblock.h"

/**
 * ------------------------------------------------------------------------
 * Regions
 * ------------------------------------------------------------------------ */
/**
 * Defines a rectangle on the screen that is bound to a Panel or subpanel
 */
typedef struct region region;

/* non-positive values of col, row, width or height
 * are relative, to, respectively,
 * left, top, right and bottom of the screen */
struct region {
	int col;
	int row;
	int width;
	int page_rows;
};

/**
 * Region that defines the full screen
 */
static const region SCREEN_REGION = {0, 0, 0, 0};

/**
 * Erase the contents of a region
 */
void region_erase(const region *loc);

/**
 * Erase the contents of a region + 1 char each way
 */
void region_erase_bordered(const region *loc);

/**
 * Given a region with relative values, make them absolute
 */
region region_calculate(region loc);

/**
 * Check whether a (mouse) event is inside a region
 */
bool region_inside(const region *loc, const ui_event *event);

/**
 * ------------------------------------------------------------------------
 * Text display
 * ------------------------------------------------------------------------ */

void textui_textblock_show(textblock *tb, region orig_area, const char *header);
void textui_textblock_place(textblock *tb, region orig_area, const char *header);

/**
 * ------------------------------------------------------------------------
 * text_out
 * ------------------------------------------------------------------------ */
int text_out_wrap;
int text_out_indent;
int text_out_pad;

void text_out(const char *fmt, ...);
void text_out_c(byte a, const char *fmt, ...);
void text_out_e(const char *fmt, ...);

/**
 * ------------------------------------------------------------------------
 * Simple text display
 * ------------------------------------------------------------------------ */
void c_put_str(uint32_t attr, const char *str, int row, int col);
void put_str(const char *str, int row, int col);
void c_prt(uint32_t attr, const char *str, int row, int col);
void prt(const char *str, int row, int col);

void show_prompt(const char *str, int row, int col);
void clear_prompt(void);

/**
 * ------------------------------------------------------------------------
 * Miscellaneous things
 * ------------------------------------------------------------------------ */
bool panel_should_modify(const struct angband_term *aterm, int y, int x);
bool modify_panel(struct angband_term *aterm, int y, int x);
bool change_panel(struct angband_term *aterm, int dir);
void verify_panel(struct angband_term *aterm);
void center_panel(struct angband_term *aterm);
void textui_get_panel(int *min_y, int *min_x, int *max_y, int *max_x);
bool textui_panel_contains(unsigned int y, unsigned int x);
bool textui_map_is_visible(void);
void window_make(int origin_x, int origin_y, int end_x, int end_y);
void clear_from(int row);

#endif /* UI2_OUTPUT_H */
