/**
 * \file main-sdl2.c
 * \brief Angband SDL2 port
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

#ifdef USE_SDL2

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"

#include "angband.h"
#include "buildid.h"
#include "game-world.h"
#include "grafmode.h"
#include "init.h"
#include "parser.h"
#include "player-calcs.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-map.h"
#include "ui2-output.h"
#include "ui2-prefs.h"
#include "ui2-term.h"

#define WINDOW_MAIN 0

#define MAX_WINDOWS 4
#define MAX_BUTTONS 32
#define MAX_FONTS   128

#define DEFAULT_DISPLAY 0

#define INIT_SDL_FLAGS \
	(SDL_INIT_VIDEO)
#define INIT_IMG_FLAGS \
	(IMG_INIT_PNG)

#define DEFAULT_CONFIG_FILE "sdl2init.txt"
#define DEFAULT_CONFIG_FILE_DIR \
	ANGBAND_DIR_USER

#define DEFAULT_ALPHA_FULL 0xFF
#define ALPHA_PERCENT(p) \
	(DEFAULT_ALPHA_FULL * (p) / 100)
#define DEFAULT_ALPHA_LOW \
	ALPHA_PERCENT(80)
/* for "Alpha" button; in percents */
#define DEFAULT_ALPHA_STEP  10
#define DEFAULT_ALPHA_LOWEST 0

#define DEFAULT_WALLPAPER "att-128.png"
#define DEFAULT_WALLPAPER_DIR \
	ANGBAND_DIR_ICONS
#define DEFAULT_WINDOW_ICON "att-32.png"
#define DEFAULT_WINDOW_ICON_DIR \
	ANGBAND_DIR_ICONS
#define DEFAULT_ABOUT_ICON "att-128.png"
#define DEFAULT_ABOUT_ICON_DIR \
	ANGBAND_DIR_ICONS

#define DEFAULT_FONT_HINTING \
	TTF_HINTING_LIGHT
/* border of subwindows, in pixels */
#define DEFAULT_BORDER 8
#define DEFAULT_XTRA_BORDER \
	(DEFAULT_BORDER * 2)
#define DEFAULT_VISIBLE_BORDER 2

/* XXX hack: the widest character present in a font
 * for determining font advance (width) */
#define GLYPH_FOR_ADVANCE 'W'
#define GLYPH_PADDING 1
#define DEFAULT_VECTOR_FONT_SIZE 12

#define DEFAULT_FONT        "10x20x.fon"
#define DEFAULT_SYSTEM_FONT "8x13x.fon"
#define DEFAULT_GAME_FONT \
	DEFAULT_FONT

#define MAX_VECTOR_FONT_SIZE 32
#define MIN_VECTOR_FONT_SIZE 4

#define DEFAULT_BUTTON_BORDER 8
#define DEFAULT_LINE_HEIGHT(h)      ((h) * 150 / 100)
#define DEFAULT_MENU_LINE_HEIGHT(h) ((h) * 200 / 100)
#define DEFAULT_MENU_LINE_WIDTH(w) \
	((w) + DEFAULT_BUTTON_BORDER + DEFAULT_XTRA_BORDER)

/* update period in window delays (160 milliseconds, assuming 60 fps) */
#define DEFAULT_IDLE_UPDATE_PERIOD 10

#define DEFAULT_WINDOW_BG_COLOR \
	COLOUR_L_DARK
#define DEFAULT_SUBWINDOW_BG_COLOR \
	COLOUR_DARK
#define DEFAULT_SUBWINDOW_CURSOR_COLOR \
	COLOUR_YELLOW
#define DEFAULT_STATUS_BAR_BG_COLOR \
	COLOUR_DARK
#define DEFAULT_SHADE_COLOR \
	COLOUR_SHADE
#define DEFAULT_SUBWINDOW_BORDER_COLOR \
	COLOUR_SHADE
#define DEFAULT_STATUS_BAR_BUTTON_ACTIVE_COLOR \
	COLOUR_WHITE
#define DEFAULT_STATUS_BAR_BUTTON_INACTIVE_COLOR \
	COLOUR_L_DARK

#define DEFAULT_MENU_SIMPLE_FG_ACTIVE_COLOR \
	COLOUR_WHITE
#define DEFAULT_MENU_SIMPLE_FG_INACTIVE_COLOR \
	COLOUR_WHITE
#define DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR \
	COLOUR_WHITE
#define DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR \
	COLOUR_L_DARK

#define DEFAULT_MENU_BG_ACTIVE_COLOR \
	COLOUR_SHADE
#define DEFAULT_MENU_BG_INACTIVE_COLOR \
	COLOUR_DARK

#define DEFAULT_MENU_PANEL_OUTLINE_COLOR \
	COLOUR_SHADE

#define DEFAULT_ERROR_COLOR \
	COLOUR_RED

#define DEFAULT_ABOUT_FG_COLOR \
	COLOUR_WHITE
#define DEFAULT_ABOUT_BG_COLOR \
	COLOUR_SHADE
#define DEFAULT_ABOUT_BORDER_OUTER_COLOR \
	COLOUR_L_DARK
#define DEFAULT_ABOUT_BORDER_INNER_COLOR \
	COLOUR_WHITE

#define DEFAULT_TOOLTIP_FG_COLOR \
	COLOUR_WHITE
#define DEFAULT_TOOLTIP_BG_COLOR \
	COLOUR_DARK
#define DEFAULT_TOOLTIP_OUTLINE_COLOR \
	COLOUR_SHADE

#define SUBWINDOW_WIDTH(cols, col_width) \
	((cols) * (col_width) + DEFAULT_BORDER * 2)

#define SUBWINDOW_HEIGHT(rows, row_height) \
	((rows) * (row_height) + DEFAULT_BORDER * 2)

/* shockbolt's tiles are 64x64; dungeon is 198 tiles long;
 * 64 * 198 is 12672 which is bigger than any possible texture! */
#define REASONABLE_MAP_TILE_WIDTH 16
#define REASONABLE_MAP_TILE_HEIGHT 16

#define MIN_COLS_TEMPORARY 1
#define MIN_ROWS_TEMPORARY 1

/* some random numbers */
#define DEFAULT_WINDOW_MINIMUM_W 198
#define DEFAULT_WINDOW_MINIMUM_H 66

/* See try_snap(); range in pixels */
#define DEFAULT_SNAP_RANGE 4

#define CHECK_BUTTON_GROUP_TYPE(button, button_group, data_type) \
	do { \
		assert((button)->info.group == (button_group)); \
		assert((button)->info.type  == (data_type));  \
	} while (0)

#define SUBWINDOW_PERMANENT_MAX \
	DISPLAY_MAX
#define SUBWINDOW_TEMPORARY_MAX \
	TERM_STACK_MAX

enum wallpaper_mode {
	/* so that we won't forget to actually set wallpaper */
	WALLPAPER_INVALID = 0,
	WALLPAPER_DONT_SHOW,
	WALLPAPER_TILED,
	WALLPAPER_CENTERED,
	WALLPAPER_SCALED
};

enum button_data_type {
	BUTTON_DATA_INVALID = 0,
	BUTTON_DATA_NONE,
	BUTTON_DATA_IVAL,
	BUTTON_DATA_UVAL,
	BUTTON_DATA_WINVAL,
	BUTTON_DATA_SUBVAL,
	BUTTON_DATA_FONTVAL,
	BUTTON_DATA_ALPHAVAL
};

enum button_group {
	BUTTON_GROUP_INVALID = 0,
	BUTTON_GROUP_NONE,
	BUTTON_GROUP_MOVESIZE,
	BUTTON_GROUP_SUBWINDOWS,
	BUTTON_GROUP_MENU
};

enum button_movesize {
	BUTTON_MOVESIZE_INVALID = 0,
	BUTTON_MOVESIZE_MOVING,
	BUTTON_MOVESIZE_SIZING
};

enum button_caption_position {
	CAPTION_POSITION_INVALID = 0,
	CAPTION_POSITION_CENTER,
	CAPTION_POSITION_LEFT,
	CAPTION_POSITION_RIGHT
};

enum font_type {
	FONT_TYPE_INVALID = 0,
	FONT_TYPE_RASTER,
	FONT_TYPE_VECTOR
};

struct ttf {
	TTF_Font *handle;
	/* this is not a real rect for glyphs;
	 * x and y are offsets to add to coords of where glyph/string should be rendered;
	 * f and h are glyph metrics but include glyph padding
	 * (for calculating rows/cols, mostly) */
	SDL_Rect glyph;
};

/* the array of ascii chars was generated by this perl script:
 * my $ascii = join '', map /\p{Print}/ ? $_ : ' ', map chr, 0 .. 127; 
 * for (my $i = 0; $i < length($ascii); $i += 40) {
 *     my $s = substr $ascii, $i, 40;
 *     $s =~ s#\\#\\\\#g;
 *     $s =~ s#"#\\"#g;
 *     print qq(\t"$s"\n);
 * }
 */
static const char g_ascii_codepoints_for_cache[] =
	"                                 !\"#$%&'"
	"()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNO"
	"PQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvw"
	"xyz{|}~ ";
/* Simple font cache. Only for ascii (which is like 99.99% (?) of what the game
 * displays, anyway) */
#define ASCII_CACHE_SIZE \
		(N_ELEMENTS(g_ascii_codepoints_for_cache) - 1)
struct font_cache {
	SDL_Texture *texture;
	/* it wastes some space... so what? */
	SDL_Rect rects[ASCII_CACHE_SIZE];
};
/* 0 is also a valid codepoint, of course... that's just for finding bugs */
#define IS_CACHED_ASCII_CODEPOINT(c) \
		((c) > 0 && (c) < ASCII_CACHE_SIZE)

struct font {
	struct ttf ttf;
	char *name;
	char *path;
	int size;
	/* index of font in g_font_info array */
	unsigned index;

	struct font_cache cache;
};

struct subwindow_border {
	bool visible;
	bool error;
	int width;
	SDL_Color color;
};

struct subwindow_config {
	char *font_name;
	int font_size;
};

struct window_config {
	int renderer_flags;
	int renderer_index;
	int window_flags;

	char *wallpaper_path;

	char *system_font_name;
	int system_font_size;

	char *game_font_name;
	int game_font_size;
};

/* struct subwindow is representation of angband's term */
struct subwindow {
	bool inited;
	bool loaded;
	bool linked;
	bool visible;

	/* this is a subwindow for temporary term */
	bool is_temporary;

	/* this is the "overview map" subwindow (invoked by 'M' command) */
	bool big_map;
	
	/* subwindow can use graphics */
	bool use_graphics;

	struct subwindow_config *config;

	/* top in z-order */
	bool is_top;
	bool always_top;

	/* index into g_term_info array */
	unsigned index;

	int rows;
	int cols;

	/* either font size, or tile size */
	int cell_width;
	int cell_height;

	/* coordinates of full rect are relative to coordinates of window
	 * (basically, full rect is texture) */
	SDL_Rect full_rect;
	/* coordinates of inner rect are relative to that of full rect */
	SDL_Rect inner_rect;
	/* for use when resizing term and for scaling big map */
	SDL_Rect sizing_rect;
	/* a one pixel texture, for displaying something when
	 * the player is resizing term */
	SDL_Texture *aux_texture;

	/* background color */
	SDL_Color color;

	struct subwindow_border borders;

	SDL_Texture *texture;

	struct font *font;

	struct window *window;

	term term;
};

struct button;

struct button_bank {
	struct button *buttons;
	size_t size;
	size_t number;
};

struct menu_panel {
	SDL_Rect rect;
	struct button_bank button_bank;
	struct menu_panel *next;
};

typedef bool (*button_click)(struct window *window,
		struct button *button);
typedef void (*button_render)(const struct window *window,
		struct button *button);
typedef bool (*button_event)(struct window *window,
		struct button *button, const SDL_Event *event);
typedef void (*button_menu)(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel);

struct fontval {
	/* this a font for permanent subwindows;
	 * the field window must be NULL then */
	struct subwindow *subwindow;

	/* this a font for temporary subwindows (game_font);
	 * the field subwindow must be NULL then */
	struct window *window;

	/* index of font in g_font_info array */
	unsigned index;
	bool size_ok;
};

/* only one of subwindow or window in fontval must be present;
 * the other field must be NULL */
#define SUBWINDOW_XOR_WINDOW(fontval) \
	(((fontval).window != NULL && (fontval).subwindow == NULL) \
	 || ((fontval).window == NULL && (fontval).subwindow != NULL))

#define CHECK_FONTVAL(fontval) do { \
	assert(SUBWINDOW_XOR_WINDOW(fontval)); \
	assert(fontval.index < N_ELEMENTS(g_font_info)); \
} while (0)

struct alphaval {
	struct subwindow *subwindow;
	int real_value;
	int show_value;
};

struct button_info {
	enum button_data_type type;
	union {
		int ival;
		unsigned uval;
		struct subwindow *subval;
		struct window *winval;
		struct fontval fontval;
		struct alphaval alphaval;
	} data;

	enum button_group group;
};

struct menu_elem {
	const char *caption;
	struct button_info info;
	button_render on_render;
	button_menu on_menu;
};

struct button_callbacks {
	/* this function should render the button;
	 * otherwise, the button will be invisible */
	button_render on_render;
	/* optional custom event handler */
	button_event on_event;
	/* if there is no custon handler,
	 * this function should do something when button is clicked
	 * otherwise, the button will be useless */
	button_click on_click;
	/* for use only with menu; only custom events */
	button_menu on_menu;
};

struct button {
	/* selected means the user pointed at button and pressed
	 * mouse button (but not released yet) */
	bool selected;
	/* highlighted means the user pointed at button but
	 * not clicking yet */
	bool highlighted;

	/* this string will be freed with the button,
	 * so use string_make() to create it */
	char *caption;

	SDL_Rect full_rect;
	SDL_Rect inner_rect;

	struct button_info info;
	struct button_callbacks callbacks;
};

struct status_bar {
	struct font *font;

	struct button_bank button_bank;
	struct menu_panel *menu_panel;

	struct window *window;

	SDL_Rect full_rect;
	SDL_Rect inner_rect;
	SDL_Color color;
	SDL_Texture *texture;

	bool is_in_menu;
};

struct graphics {
	SDL_Texture *texture;
	int id;
	int tile_pixel_w;
	int tile_pixel_h;

	int overdraw_row;
	int overdraw_max;
};

/* thats for dragging terms */
struct move_state {
	bool active;
	bool moving;

	int originx;
	int originy;

	struct subwindow *subwindow;
};

/* thats for resizing terms */
struct size_state {
	bool active;
	bool sizing;

	int originx;
	int originy;

	bool left;
	bool top;

	struct subwindow *subwindow;
};

struct wallpaper {
	int width;
	int height;
	SDL_Texture *texture;
	enum wallpaper_mode mode;
};

/* struct window is a real window on screen, it has one or more
 * subwindows (terms) in it */
struct window {
	bool inited;
	bool loaded;

	/* id is SDL's id, for use with events */
	Uint32 id;
	/* and this is our id, mostly for debugging */
	unsigned index;

	struct window_config *config;

	/* does window have mouse focus? */
	bool focus;

	/* from display mode; typically 16 milliseconds */
	int delay;

	/* as reported by SDL_GetWindowFlags() */
	Uint32 flags;

	/* position and size of window as it is on display */
	SDL_Rect full_rect;
	/* position and size of the part of window without status bar, basically */
	SDL_Rect inner_rect;

	SDL_Color color;
	/* for making terms transparent while moving or sizing them */
	Uint8 alpha;

	SDL_Window *window;
	SDL_Renderer *renderer;

	int pixelformat;

	struct wallpaper wallpaper;
	struct move_state move_state;
	struct size_state size_state;
	struct status_bar status_bar;
	struct graphics graphics;

	/* used for menus and other temporary terms */
	struct font *game_font;

	struct {
		size_t number;
		struct subwindow *subwindows[SUBWINDOW_PERMANENT_MAX];
	} permanent;

	struct {
		size_t number;
		struct subwindow *subwindows[SUBWINDOW_TEMPORARY_MAX];
	} temporary;
};

struct font_info {
	char *name;
	char *path;
	int size;
	size_t index;
	enum font_type type;
	bool loaded;
};

/* there are also global arrays of subwindows and windows
 * those are at the end of the file */

const char help_sdl2[] = "SDL2 frontend";
static SDL_Color g_colors[MAX_COLORS];
static struct font_info g_font_info[MAX_FONTS];
struct {
	enum display_term_index index;
	const char *name;
	int min_cols;
	int min_rows;
	int def_cols;
	int def_rows;
	int max_cols;
	int max_rows;
	bool required;
} g_term_info[] = {
	#define DISPLAY(i, desc, minc, minr, defc, defr, maxc, maxr, req) \
		{ \
			.index    = DISPLAY_ ##i, \
			.name     = (desc), \
			.min_cols = (minc), \
			.min_rows = (minr), \
			.def_cols = (defc), \
			.def_rows = (defr), \
			.max_cols = (maxc), \
			.max_rows = (maxr), \
			.required = (req), \
		},
	#include "list-display-terms.h"
	#undef DISPLAY
};

/* Forward declarations */

static void init_globals(void);
static void free_globals(void);
static bool read_config_file(void);
static void dump_config_file(void);
static void init_colors(void);
static void start_windows(void);
static void start_window(struct window *window);
static void load_font(struct font *font);
static bool reload_font(struct subwindow *subwindow,
		const struct font_info *info);
static struct font *make_font(const struct window *window,
		const char *name, int size);
static void free_font(struct font *font);
static const struct font_info *find_font_info(const char *name);
static void get_string_metrics(struct font *font, const char *str, int *w, int *h);
static struct window *get_new_window(unsigned index);
static void wipe_window(struct window *window, int display);
/* create default config for spawning a window via gui */
static void wipe_window_aux_config(struct window *window);
static void adjust_window_geometry(struct window *window);
static void free_window(struct window *window);
static struct window *get_window_by_id(Uint32 id);
static struct window *get_window_direct(unsigned index);
static struct window *get_loaded_window(unsigned index);
static bool has_visible_subwindow(const struct window *window, unsigned index);
static void resize_window(struct window *window, int w, int h);
static void attach_subwindow_to_window(struct window *window,
		struct subwindow *subwindow);
static void detach_subwindow_from_window(struct window *window,
		struct subwindow *subwindow);
static void fit_subwindow_in_window(const struct window *window,
		struct subwindow *subwindow);
static struct subwindow *get_new_subwindow(unsigned index);
static struct subwindow *get_new_temporary_subwindow(void);
static void free_subwindow(struct subwindow *subwindow);
static void free_temporary_subwindow(struct subwindow *subwindow);
static void load_subwindow(struct window *window, struct subwindow *subwindow);
static bool is_subwindow_loaded(unsigned index);
static struct subwindow *transfer_subwindow(struct window *window, unsigned index);
static struct subwindow *get_subwindow_by_xy(const struct window *window,
		int x, int y, bool temporary);
static struct subwindow *get_subwindow_by_index(const struct window *window,
		unsigned index, bool visible);
static struct subwindow *get_subwindow_direct(unsigned index);
/* this function loads new subwindow if it's not already loaded */
static struct subwindow *make_subwindow(struct window *window, unsigned index);
static void sort_to_top(struct window *window);
static void bring_to_top(struct window *window, struct subwindow *subwindow);
static void render_borders(struct subwindow *subwindow);
static SDL_Texture *load_image(const struct window *window, const char *path);
static void reload_graphics(struct window *window, graphics_mode *mode);
static void free_graphics(struct graphics *graphics);
static void load_terms(void);
static void load_term(struct subwindow *subwindow);
static bool adjust_subwindow_geometry(const struct window *window,
		struct subwindow *subwindow);
static void position_temporary_subwindow(struct subwindow *subwindow,
		const struct term_hints *hints);
static int get_min_cols(const struct subwindow *subwindow);
static int get_min_rows(const struct subwindow *subwindow);
static bool is_ok_col_row(const struct subwindow *subwindow,
		const SDL_Rect *rect, int cell_w, int cell_h);
static void resize_rect(SDL_Rect *rect,
		int left, int top, int right, int bottom);
static bool is_point_in_rect(int x, int y, const SDL_Rect *rect);
static bool is_close_to(int a, int b, unsigned range);
static bool is_over_status_bar(const struct status_bar *status_bar, int x, int y);
static void make_button_bank(struct button_bank *bank);
static void free_button_bank(struct button_bank *button_bank);
static void free_menu_panel(struct menu_panel *menu_panel);
static struct menu_panel *get_menu_panel_by_xy(struct menu_panel *menu_panel,
		int x, int y);
static void refresh_display_terms(void);
static void handle_quit(void);
static void wait_anykey(void);

/* Term callbacks */

static void term_flush_events(void *user);
static void term_cursor(void *user, int col, int row);
static void term_redraw(void *user);
static void term_event(void *user, bool wait);
static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points);
static void term_delay(void *user, int msecs);
static void term_push_new(const struct term_hints *hints,
		struct term_create_info *info);
static void term_pop_new(void *user);

/* Defaults for term creations */

static const struct term_callbacks default_callbacks = {
	.flush_events = term_flush_events,
	.cursor       = term_cursor,
	.redraw       = term_redraw,
	.event        = term_event,
	.draw         = term_draw,
	.delay        = term_delay,
	.push_new     = term_push_new,
	.pop_new      = term_pop_new
};

#define BLANK_CHAR    0
#define BLANK_ATTR    MAX_COLORS
#define BLANK_TERRAIN BG_BLACK

#define IS_BLANK_POINT_FG(point_ptr) \
	((point_ptr)->fg_char == BLANK_CHAR && (point_ptr)->fg_attr == BLANK_ATTR)

#define IS_BLANK_POINT_BG(point_ptr) \
	((point_ptr)->bg_char == BLANK_CHAR && (point_ptr)->bg_attr == BLANK_ATTR)

#define IS_BLANK_POINT_TERRAIN(point_ptr) \
	((point_ptr)->terrain_attr == BLANK_TERRAIN)

#define IS_BLANK_POINT(point_ptr) \
	(IS_BLANK_POINT_FG(point_ptr) \
	 && IS_BLANK_POINT_BG(point_ptr) \
	 && IS_BLANK_POINT_TERRAIN(point_ptr))

static const struct term_point default_blank_point = {
	.fg_char = BLANK_CHAR,
	.fg_attr = BLANK_ATTR,
	.bg_char = BLANK_CHAR,
	.bg_attr = BLANK_ATTR,
	.terrain_attr = BLANK_TERRAIN
};

/* Functions */

static void render_clear(const struct window *window,
		SDL_Texture *texture, const SDL_Color *color)
{
	SDL_SetRenderTarget(window->renderer, texture);
	SDL_SetRenderDrawColor(window->renderer,
			color->r, color->g, color->b, color->a);
	SDL_RenderClear(window->renderer);
}

static void render_wallpaper_tiled(const struct window *window)
{
	SDL_SetRenderTarget(window->renderer, NULL);

	SDL_Rect rect = {0, 0, window->wallpaper.width, window->wallpaper.height};
	for (rect.y = window->inner_rect.y;
			rect.y < window->inner_rect.h;
			rect.y += rect.h)
	{
		for (rect.x = window->inner_rect.x;
				rect.x < window->inner_rect.w;
				rect.x += rect.w)
		{
			SDL_RenderCopy(window->renderer, window->wallpaper.texture, NULL, &rect);
		}
	}
}

static void render_wallpaper_scaled(const struct window *window)
{
	SDL_SetRenderTarget(window->renderer, NULL);
	SDL_RenderCopy(window->renderer, window->wallpaper.texture, NULL, NULL);
}

static void render_wallpaper_centered(const struct window *window)
{
	SDL_Rect rect;

	rect.w = window->wallpaper.width;
	rect.h = window->wallpaper.height;

	rect.x = window->inner_rect.x + (window->inner_rect.w - rect.w) / 2;
	rect.y = window->inner_rect.y + (window->inner_rect.h - rect.h) / 2;

	SDL_SetRenderTarget(window->renderer, NULL);
	SDL_RenderCopy(window->renderer, window->wallpaper.texture, NULL, &rect);
}

static void render_background(const struct window *window)
{
	render_clear(window, NULL, &window->color);

	switch (window->wallpaper.mode) {
		case WALLPAPER_DONT_SHOW:
			return;
		case WALLPAPER_TILED:
			render_wallpaper_tiled(window);
			return;
		case WALLPAPER_CENTERED:
			render_wallpaper_centered(window);
			return;
		case WALLPAPER_SCALED:
			render_wallpaper_scaled(window);
			return;
		default:
			quit_fmt("bad wallpaper mode %d in window %u",
					window->wallpaper.mode, window->index);
			break;
	}
}

static void render_subwindows(const struct window *window,
		struct subwindow *const *subwindows, size_t number)
{
	for (size_t i = 0; i < number; i++) {
		const struct subwindow *subwindow = subwindows[i];
		if (subwindow->visible) {
			SDL_RenderCopy(window->renderer,
					subwindow->texture, NULL, &subwindow->full_rect);
		}
	}
}

static void render_all(const struct window *window)
{
	render_background(window);

	SDL_RenderCopy(window->renderer,
			window->status_bar.texture, NULL, &window->status_bar.full_rect);

	render_subwindows(window, window->permanent.subwindows, window->permanent.number);
	render_subwindows(window, window->temporary.subwindows, window->temporary.number);
}

static void render_big_map(const struct window *window)
{
	assert(window->temporary.number > 0);
	assert(window->temporary.subwindows[window->temporary.number - 1]->big_map);

	render_background(window);

	SDL_RenderCopy(window->renderer,
			window->status_bar.texture, NULL, &window->status_bar.full_rect);

	render_subwindows(window, window->permanent.subwindows, window->permanent.number);
	render_subwindows(window, window->temporary.subwindows, window->temporary.number - 1);

	const struct subwindow *big_map =
		window->temporary.subwindows[window->temporary.number - 1];

	SDL_RenderCopy(window->renderer, big_map->texture, NULL, &big_map->sizing_rect);
}

static void render_status_bar(const struct window *window)
{
	render_clear(window, window->status_bar.texture, &window->status_bar.color);

	for (size_t i = 0; i < window->status_bar.button_bank.number; i++) {
		struct button *button = &window->status_bar.button_bank.buttons[i];

		assert(button->callbacks.on_render != NULL);
		button->callbacks.on_render(window, button);
	}
}

static void render_outline_rect(const struct window *window,
		SDL_Texture *texture, const SDL_Rect *rect, const SDL_Color *color)
{
	SDL_SetRenderTarget(window->renderer, texture);
	SDL_SetRenderDrawColor(window->renderer,
			color->r, color->g, color->b, color->a);
	SDL_RenderDrawRect(window->renderer, rect);
}

static void render_outline_rect_width(const struct window *window,
		SDL_Texture *texture, const SDL_Rect *rect, const SDL_Color *color, int width)
{
	SDL_Rect dst = *rect;

	for (int i = 0; i < width; i++) {
		render_outline_rect(window, texture, &dst, color);
		resize_rect(&dst, 1, 1, -1, -1);
	}
}

static void render_fill_rect(const struct window *window,
		SDL_Texture *texture, const SDL_Rect *rect, const SDL_Color *color)
{
	SDL_SetRenderTarget(window->renderer, texture);
	SDL_SetRenderDrawColor(window->renderer,
			color->r, color->g, color->b, color->a);
	SDL_RenderFillRect(window->renderer, rect);
}

static void render_all_in_menu(const struct window *window)
{
	render_background(window);

	SDL_SetRenderTarget(window->renderer, NULL);

	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];
		if (subwindow->visible) {
			if (subwindow->sizing_rect.w > 0 && subwindow->sizing_rect.h > 0) {
				SDL_SetRenderTarget(window->renderer, subwindow->aux_texture);
				/* in case subwindow's color changed (as in when it becomes transparent) */
				render_fill_rect(window,
						subwindow->aux_texture, NULL, &subwindow->color);

				SDL_SetRenderTarget(window->renderer, NULL);
				SDL_RenderCopy(window->renderer,
						subwindow->aux_texture, NULL, &subwindow->sizing_rect);
			}

			SDL_RenderCopy(window->renderer,
					subwindow->texture,
					NULL, &subwindow->full_rect);
		}
	}

	render_subwindows(window, window->temporary.subwindows, window->temporary.number);

	/* render it last to allow the menu to draw over subwindows */
	render_status_bar(window);
	SDL_SetRenderTarget(window->renderer, NULL);
	SDL_RenderCopy(window->renderer,
			window->status_bar.texture, NULL, &window->status_bar.full_rect);
}

static void set_subwindow_alpha(struct subwindow *subwindow, int alpha)
{
	SDL_SetTextureAlphaMod(subwindow->texture, alpha);
	SDL_SetTextureAlphaMod(subwindow->aux_texture, alpha);
}

static void set_subwindows_alpha(const struct window *window, int alpha)
{
	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];

		set_subwindow_alpha(subwindow, alpha);
	}
}

static void redraw_window(struct window *window)
{
	if (window->status_bar.is_in_menu) {
		set_subwindows_alpha(window, window->alpha);
		render_all_in_menu(window);
	} else {
		render_all(window);
	}

	SDL_RenderPresent(window->renderer);
}

static void redraw_big_map(struct window *window)
{
	render_big_map(window);

	SDL_RenderPresent(window->renderer);
}

static void redraw_all_windows(void)
{
	for (unsigned i = 0; i < MAX_WINDOWS; i++) {
		struct window *window = get_loaded_window(i);
		if (window != NULL) {
			render_status_bar(window);
			redraw_window(window);
		}
	}
}

static void render_utf8_string(const struct window *window,
		const struct font *font, SDL_Texture *dst_texture,
		SDL_Color fg, SDL_Rect rect, const char *utf8_string)
{
	SDL_Surface *surface = TTF_RenderUTF8_Blended(font->ttf.handle, utf8_string, fg);
	SDL_Texture *src_texture = SDL_CreateTextureFromSurface(window->renderer, surface);
	SDL_FreeSurface(surface);

	rect.x += font->ttf.glyph.x;
	rect.y += font->ttf.glyph.y;

	SDL_SetRenderTarget(window->renderer, dst_texture);
	SDL_RenderCopy(window->renderer, src_texture, NULL, &rect);

	SDL_DestroyTexture(src_texture);
}

/* this function is typically called in a loop, so for efficiency it doesnt
 * SetRenderTarget; caller must do it (but it does SetTextureColorMod) */
static void render_glyph_mono(const struct window *window, const struct font *font,
		int x, int y, SDL_Color fg, uint32_t codepoint)
{
	SDL_Rect dst = {
		.x = x + font->ttf.glyph.x,
		.y = y + font->ttf.glyph.y,
	};

	if (IS_CACHED_ASCII_CODEPOINT(codepoint)) {
		dst.w = font->cache.rects[codepoint].w;
		dst.h = font->cache.rects[codepoint].h;

		SDL_SetTextureColorMod(font->cache.texture, fg.r, fg.g, fg.b);

		SDL_RenderCopy(window->renderer,
				font->cache.texture, &font->cache.rects[codepoint], &dst);
	} else {
		SDL_Surface *surface =
			TTF_RenderGlyph_Blended(font->ttf.handle, (Uint16) codepoint, fg);

		if (surface == NULL) {
			return;
		}

		SDL_Rect src = {
			.x = 0,
			.y = 0,
			.w = MIN(surface->w, font->ttf.glyph.w - font->ttf.glyph.x),
			.h = MIN(surface->h, font->ttf.glyph.h - font->ttf.glyph.y)
		};

		dst.w = src.w;
		dst.h = src.h;

		SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
		assert(texture != NULL);

		SDL_RenderCopy(window->renderer, texture, &src, &dst);

		SDL_FreeSurface(surface);
		SDL_DestroyTexture(texture);
	}
}

static void render_cursor(struct subwindow *subwindow,
		int col, int row)
{
	SDL_Color color = g_colors[DEFAULT_SUBWINDOW_CURSOR_COLOR];

	SDL_Rect rect = {
		subwindow->inner_rect.x + subwindow->cell_width * col,
		subwindow->inner_rect.y + subwindow->cell_height * row,
		subwindow->cell_width,
		subwindow->cell_height
	};

	render_outline_rect(subwindow->window,
			subwindow->texture, &rect, &color);
}

static void render_big_map_cursor(struct subwindow *subwindow,
		int col, int row)
{
	SDL_Color color = g_colors[DEFAULT_SUBWINDOW_CURSOR_COLOR];

	SDL_Rect rect = {
		subwindow->inner_rect.x + subwindow->cell_width * col,
		subwindow->inner_rect.y + subwindow->cell_height * row,
		subwindow->cell_width,
		subwindow->cell_height
	};

	/* XXX some arbitrary values that look ok at the moment */
	int width = MIN(MIN(subwindow->cell_width / 4, subwindow->cell_height / 4),
			DEFAULT_VISIBLE_BORDER);

	/* render cursor around player */
	render_outline_rect_width(subwindow->window,
			subwindow->texture, &rect, &color, width);
}

static void render_tile(const struct subwindow *subwindow,
		const struct graphics *graphics,
		int src_col, int src_row, int dst_col, int dst_row,
		SDL_Rect dst)
{
	SDL_Rect src = {
		graphics->tile_pixel_w * src_col, 
		graphics->tile_pixel_h * src_row,
		graphics->tile_pixel_w,
		graphics->tile_pixel_h
	};

	SDL_SetRenderTarget(subwindow->window->renderer, subwindow->texture);

	if (graphics->overdraw_row != 0
			&& dst_row >= 1
			&& src_row >= graphics->overdraw_row
			&& src_row <= graphics->overdraw_max)
	{
		src.y -= src.h;
		src.h *= 2;

		dst.y -= dst.h;
		dst.h *= 2;

		SDL_RenderCopy(subwindow->window->renderer,
				graphics->texture, &src, &dst);

		Term_dirty_point(dst_col, dst_row - 1);
		Term_dirty_point(dst_col, dst_row);
	} else {
		SDL_RenderCopy(subwindow->window->renderer,
				graphics->texture, &src, &dst);
	}
}

static void clear_all_borders(struct window *window)
{
	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];

		subwindow->borders.error = false;
		render_borders(subwindow);
	}
}

static void render_borders(struct subwindow *subwindow)
{
	SDL_Rect rect = {0};
	SDL_QueryTexture(subwindow->texture, NULL, NULL, &rect.w, &rect.h);

	SDL_Color *color;
	if (subwindow->borders.error) {
		color = &g_colors[DEFAULT_ERROR_COLOR];
	} else if (subwindow->borders.visible) {
		color = &subwindow->borders.color;
	} else {
		color = &subwindow->color;
	}

	render_outline_rect_width(subwindow->window,
			subwindow->texture, &rect, color,
			subwindow->borders.width);
}

static SDL_Texture *make_subwindow_texture(const struct window *window, int w, int h)
{
	SDL_Texture *texture = SDL_CreateTexture(window->renderer,
			window->pixelformat, SDL_TEXTUREACCESS_TARGET, w, h);
	if (texture == NULL) {
		quit_fmt("cant create texture for subwindow in window %u: %s",
				window->index, SDL_GetError());
	}

	if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0) {
		SDL_DestroyTexture(texture);
		quit_fmt("cant set blend mode for texture in window %u: %s",
				window->index, SDL_GetError());
	}

	return texture;
}

static void render_menu_panel(const struct window *window, struct menu_panel *menu_panel)
{
	if (menu_panel == NULL) {
		return;
	}

	for (size_t i = 0; i < menu_panel->button_bank.number; i++) {
		struct button *button = &menu_panel->button_bank.buttons[i];

		assert(button->callbacks.on_render != NULL);
		button->callbacks.on_render(window, button);
	}
	render_outline_rect(window,
			NULL, &menu_panel->rect, &g_colors[DEFAULT_MENU_PANEL_OUTLINE_COLOR]);

	/* recurse */
	render_menu_panel(window, menu_panel->next);
}

static SDL_Rect get_button_caption_rect(const struct button *button)
{
	SDL_Rect rect = {
		button->full_rect.x + button->inner_rect.x,
		button->full_rect.y + button->inner_rect.y,
		button->inner_rect.w,
		button->inner_rect.h
	};

	return rect;
}

static void render_button_menu(const struct window *window,
		struct button *button, const SDL_Color *fg, const SDL_Color *bg)
{
	SDL_Rect rect = get_button_caption_rect(button);

	render_fill_rect(window,
			NULL, &button->full_rect, bg);
	render_utf8_string(window, window->status_bar.font, NULL, 
			*fg, rect, button->caption);
}

static void render_button_menu_toggle(const struct window *window,
		struct button *button, bool active)
{
	SDL_Color *bg;
	SDL_Color *fg;

	if (active) {
		fg = &g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else {
		fg = &g_colors[DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR];
	}
	if (button->highlighted) {
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	render_button_menu(window, button, fg, bg);
}

static void render_button_menu_simple(const struct window *window, struct button *button)
{
	SDL_Color *fg;
	SDL_Color *bg;

	if (button->highlighted) {
		fg = &g_colors[DEFAULT_MENU_SIMPLE_FG_ACTIVE_COLOR];
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		fg = &g_colors[DEFAULT_MENU_SIMPLE_FG_INACTIVE_COLOR];
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	render_button_menu(window, button, fg, bg);
}

static void render_button_menu_terms(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	struct subwindow *subwindow = button->info.data.subval;

	if (button->highlighted && subwindow->visible) {
		/* draw a border around subwindow, so that it would be easy to see
		 * which subwindow corresponds to that button */
		int outline_width = (subwindow->full_rect.w - subwindow->inner_rect.w) / 2
				- subwindow->borders.width;
		SDL_Rect outline_rect = subwindow->full_rect;
		render_outline_rect_width(window,
				NULL,
				&outline_rect,
				&g_colors[DEFAULT_SUBWINDOW_BORDER_COLOR],
				outline_width);
	}

	render_button_menu_simple(window, button);
}

static void render_button_menu_borders(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	struct subwindow *subwindow = button->info.data.subval;

	render_button_menu_toggle(window, button, subwindow->borders.visible);
}

static void render_button_menu_alpha(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_ALPHAVAL);

	struct subwindow *subwindow = button->info.data.alphaval.subwindow;
	int alpha = button->info.data.alphaval.real_value;

	SDL_Color fg;
	SDL_Color *bg;

	if (is_close_to(alpha, subwindow->color.a, DEFAULT_ALPHA_STEP / 2)) {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR];
	}
	if (button->highlighted) {
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_fill_rect(window,
			NULL, &button->full_rect, bg);
	render_utf8_string(window, window->status_bar.font, NULL, 
			fg, rect, format(button->caption, button->info.data.alphaval.show_value));
}

static void render_button_menu_top(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	struct subwindow *subwindow = button->info.data.subval;

	render_button_menu_toggle(window, button, subwindow->always_top);
}

static void render_button_menu_tile_set(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_IVAL);

	render_button_menu_toggle(window,
			button, button->info.data.ival == current_graphics_mode->grafID);
}

static void render_button_menu_font_size(const struct window *window,
		struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_FONTVAL);

	SDL_Color fg;
	SDL_Color *bg;

	struct fontval fontval = button->info.data.fontval;
	CHECK_FONTVAL(fontval);

	int size = fontval.window != NULL ?
		fontval.window->game_font->size : fontval.subwindow->font->size;

	if (!button->info.data.fontval.size_ok) {
		fg = g_colors[DEFAULT_ERROR_COLOR];
	} else if (g_font_info[fontval.index].type == FONT_TYPE_VECTOR) {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR];
	}

	if (button->highlighted) {
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_fill_rect(window,
			NULL, &button->full_rect, bg);
	render_utf8_string(window, window->status_bar.font, NULL,
			fg, rect, format(button->caption, size));
}

static void render_button_menu_font_name(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_FONTVAL);

	SDL_Color fg;
	SDL_Color *bg;

	CHECK_FONTVAL(button->info.data.fontval);

	struct window *winval = button->info.data.fontval.window;
	struct subwindow *subval = button->info.data.fontval.subwindow;

	size_t index = button->info.data.fontval.index;

	if (!button->info.data.fontval.size_ok) {
		fg = g_colors[DEFAULT_ERROR_COLOR];
	} else if (subval != NULL && subval->font->index == index) {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else if (winval != NULL && winval->game_font->index == index) {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR];
	}

	if (button->highlighted) {
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_fill_rect(window,
			NULL, &button->full_rect, bg);
	render_utf8_string(window, window->status_bar.font, NULL, 
			fg, rect, button->caption);
}

static void render_button_menu_window(const struct window *window,
		struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_UVAL);

	struct window *w = get_loaded_window(button->info.data.uval);

	SDL_Color fg;
	SDL_Color *bg;

	if (w != NULL) {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_ACTIVE_COLOR];
	} else {
		fg = g_colors[DEFAULT_MENU_TOGGLE_FG_INACTIVE_COLOR];
	}
	if (button->highlighted) {
		bg = &g_colors[DEFAULT_MENU_BG_ACTIVE_COLOR];
	} else {
		bg = &g_colors[DEFAULT_MENU_BG_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_fill_rect(window, NULL, &button->full_rect, bg);
	render_utf8_string(window, window->status_bar.font, NULL, 
			fg, rect, format(button->caption, button->info.data.uval));
}

static void render_button_menu_fullscreen(const struct window *window,
		struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	render_button_menu_toggle(window, button,
			window->flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

/* the menu proper is rendered via this callback, used by the "Menu" button */
static void render_menu_button(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	SDL_Color color;
	if (button->highlighted) {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_ACTIVE_COLOR];
	} else {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_utf8_string(window, window->status_bar.font, window->status_bar.texture, 
			color, rect, button->caption);

	if (button->highlighted) {
		render_menu_panel(window, window->status_bar.menu_panel);
	}
}

static void render_button_subwindows(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_SUBWINDOWS, BUTTON_DATA_UVAL);

	SDL_Color color;
	if (has_visible_subwindow(window, button->info.data.uval)
			|| button->highlighted) {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_ACTIVE_COLOR];
	} else {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_utf8_string(window, window->status_bar.font, window->status_bar.texture, 
			color, rect, button->caption);

	if (button->highlighted) {
		/* Display term name as a tooltip */

		assert(button->info.data.uval < N_ELEMENTS(g_term_info));
		const char *tip =
			format("\"%s\" subwindow", g_term_info[button->info.data.uval].name);

		SDL_Rect text_rect = {
			.x = rect.x,
			.y = window->status_bar.full_rect.y + window->status_bar.full_rect.h +
				DEFAULT_XTRA_BORDER
		};
		get_string_metrics(window->status_bar.font, tip, &text_rect.w, &text_rect.h);

		SDL_Rect background_rect = text_rect;

		resize_rect(&background_rect,
				-DEFAULT_XTRA_BORDER, -DEFAULT_XTRA_BORDER,
				DEFAULT_XTRA_BORDER, DEFAULT_XTRA_BORDER);

		render_fill_rect(window, NULL, &background_rect,
				&g_colors[DEFAULT_TOOLTIP_BG_COLOR]);

		render_outline_rect_width(window, NULL, &background_rect,
				&g_colors[DEFAULT_TOOLTIP_OUTLINE_COLOR], DEFAULT_VISIBLE_BORDER);

		render_utf8_string(window, window->status_bar.font, NULL,
				g_colors[DEFAULT_TOOLTIP_FG_COLOR], text_rect, tip);
	}
}

static void render_button_movesize(const struct window *window, struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MOVESIZE, BUTTON_DATA_IVAL);

	bool active = false;
	switch (button->info.data.ival) {
		case BUTTON_MOVESIZE_MOVING:
			active = window->move_state.active;
			break;
		case BUTTON_MOVESIZE_SIZING:
			active = window->size_state.active;
			break;
		default:
			quit_fmt("button '%s' has wrong ival %d",
					button->caption, button->info.data.ival);
			break;
	}

	SDL_Color color;
	if (active || button->highlighted) {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_ACTIVE_COLOR];
	} else {
		color = g_colors[DEFAULT_STATUS_BAR_BUTTON_INACTIVE_COLOR];
	}

	SDL_Rect rect = get_button_caption_rect(button);

	render_utf8_string(window, window->status_bar.font, window->status_bar.texture, 
			color, rect, button->caption);
}

static void show_about(const struct window *window)
{
	const char *about_text[] = {
		buildid,
		"See http://www.rephial.org",
		"Visit our forum at http://angband.oook.cz/forum"
	};

	struct { SDL_Rect rect; const char *text; } elems[N_ELEMENTS(about_text)];
	for (size_t i = 0; i < N_ELEMENTS(elems); i++) {
		elems[i].text = about_text[i];
	}

	char path[4096];
	path_build(path, sizeof(path), DEFAULT_ABOUT_ICON_DIR, DEFAULT_ABOUT_ICON);

	SDL_Texture *texture = load_image(window, path);
	SDL_Rect texture_rect = {0};
	SDL_QueryTexture(texture, NULL, NULL, &texture_rect.w, &texture_rect.h);

	SDL_Rect total = {
		0, 0,
		2 * DEFAULT_XTRA_BORDER + texture_rect.w,
		/* the default icon just looks better without bottom border */
		DEFAULT_XTRA_BORDER + texture_rect.h
	};

	for (size_t i = 0; i < N_ELEMENTS(elems); i++) {
		int w;
		int h;
		get_string_metrics(window->status_bar.font,
				elems[i].text, &w, &h);

		elems[i].rect.h = h;
		elems[i].rect.w = w;
		elems[i].rect.y = total.h + (DEFAULT_LINE_HEIGHT(h) - h) / 2;

		total.w = MAX(w + 2 * DEFAULT_XTRA_BORDER, total.w);
		total.h += DEFAULT_LINE_HEIGHT(h);
	}
	total.h += DEFAULT_XTRA_BORDER;

	total.x = window->full_rect.w / 2 - total.w / 2;
	total.y = window->full_rect.h / 2 - total.h / 2;

	render_all_in_menu(window);

	render_fill_rect(window, NULL, &total, &g_colors[DEFAULT_ABOUT_BG_COLOR]);
	render_outline_rect_width(window, NULL, &total,
			&g_colors[DEFAULT_ABOUT_BORDER_OUTER_COLOR], DEFAULT_VISIBLE_BORDER);
	resize_rect(&total,
			DEFAULT_VISIBLE_BORDER, DEFAULT_VISIBLE_BORDER,
			-DEFAULT_VISIBLE_BORDER, -DEFAULT_VISIBLE_BORDER);
	render_outline_rect_width(window, NULL, &total,
			&g_colors[DEFAULT_ABOUT_BORDER_INNER_COLOR], DEFAULT_VISIBLE_BORDER);

	for (size_t i = 0; i < N_ELEMENTS(elems); i++) {
		/* center the string in total rect */
		elems[i].rect.x = total.x + (total.w - elems[i].rect.w) / 2;
		/* make the y coord of string absolute (was relative to total rect) */
		elems[i].rect.y += total.y;

		render_utf8_string(window, window->status_bar.font,
			NULL, g_colors[DEFAULT_ABOUT_FG_COLOR],
			elems[i].rect, elems[i].text);
	}

	texture_rect.x = total.x + (total.w - texture_rect.w) / 2;
	texture_rect.y = total.y + DEFAULT_XTRA_BORDER;

	SDL_SetRenderTarget(window->renderer, NULL);
	SDL_RenderCopy(window->renderer, texture, NULL, &texture_rect);
	SDL_RenderPresent(window->renderer);

	wait_anykey();

	SDL_DestroyTexture(texture);
}

static void signal_move_state(struct window *window)
{
	assert(!window->size_state.active);

	bool was_active = window->move_state.active;

	if (was_active) {
		window->move_state.active = false;
		window->move_state.moving = false;
		window->move_state.subwindow = NULL;
	} else {
		window->move_state.active = true;
	}

	SDL_SetWindowGrab(window->window,
			was_active ? SDL_FALSE : SDL_TRUE);
	window->alpha = was_active ? DEFAULT_ALPHA_FULL : DEFAULT_ALPHA_LOW;
}

static void signal_size_state(struct window *window)
{
	assert(!window->move_state.active);

	bool was_active = window->size_state.active;

	if (was_active) {
		window->size_state.active = false;
		window->size_state.sizing = false;
		if (window->size_state.subwindow != NULL) {
			memset(&window->size_state.subwindow->sizing_rect,
					0, sizeof(window->size_state.subwindow->sizing_rect));
			window->size_state.subwindow = NULL;
		}
	} else {
		window->size_state.active = true;
	}

	SDL_SetWindowGrab(window->window,
			was_active ? SDL_FALSE : SDL_TRUE);
	window->alpha = was_active ? DEFAULT_ALPHA_FULL : DEFAULT_ALPHA_LOW;
}

static bool do_button_movesize(struct window *window,
		struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MOVESIZE, BUTTON_DATA_IVAL);

	switch (button->info.data.ival) {
		case BUTTON_MOVESIZE_MOVING:
			if (window->size_state.active) {
				/* toggle size button */
				signal_size_state(window);
			}
			signal_move_state(window);
			break;
		case BUTTON_MOVESIZE_SIZING:
			if (window->move_state.active) {
				/* toggle move button */
				signal_move_state(window);
			}
			signal_size_state(window);
			break;
	}

	return true;
}

static void push_button(struct button_bank *bank, struct font *font,
		const char *caption, struct button_info info, struct button_callbacks callbacks,
		const SDL_Rect *rect, enum button_caption_position position)
{
	assert(bank->number < bank->size);

	struct button *button = &bank->buttons[bank->number];

	int w;
	int h;
	get_string_metrics(font, caption, &w, &h);

	int x = 0;
	switch (position) {
		case CAPTION_POSITION_CENTER:
			x = (rect->w - w) / 2;
			break;
		case CAPTION_POSITION_LEFT:
			x = DEFAULT_BUTTON_BORDER;
			break;
		case CAPTION_POSITION_RIGHT:
			x = rect->w - DEFAULT_BUTTON_BORDER - w;
			break;
		default:
			quit_fmt("bad caption position %d in button '%s'",
					position, button->caption);
			break;
	}

	button->inner_rect.x = x;
	button->inner_rect.y = (rect->h - h) / 2;
	button->inner_rect.w = w;
	button->inner_rect.h = h;

	button->full_rect = *rect;

	assert(button->full_rect.w >= button->inner_rect.w
			&& button->full_rect.h >= button->inner_rect.h);

	button->caption = string_make(caption);

	button->callbacks = callbacks;
	button->info = info;
	button->highlighted = false;
	button->selected = false;

	bank->number++;
}

static struct menu_panel *new_menu_panel(void)
{
	struct menu_panel *menu = mem_zalloc(sizeof(*menu));

	make_button_bank(&menu->button_bank);
	menu->next = NULL;

	return menu;
}

/* if caption of some button is NULL, the button is not included in menu (skipped) */
static struct menu_panel *make_menu_panel(const struct button *origin,
		struct font *font, size_t n_elems, struct menu_elem *elems)
{
	int maxlen = 0;

	for (size_t i = 0; i < n_elems; i++) {
		if (elems[i].caption == NULL) {
			continue;
		}
		int w;
		get_string_metrics(font, elems[i].caption, &w, NULL);
		maxlen = MAX(maxlen, w);
	}

	struct menu_panel *menu_panel = new_menu_panel();
	if (menu_panel == NULL) {
		return NULL;
	}

	SDL_Rect rect = {
		origin->full_rect.x + origin->full_rect.w,
		origin->full_rect.y,
		DEFAULT_MENU_LINE_WIDTH(maxlen),
		DEFAULT_MENU_LINE_HEIGHT(font->ttf.glyph.h)
	};

	menu_panel->rect = rect;
	menu_panel->rect.h = 0;

	for (size_t i = 0; i < n_elems; i++) {
		if (elems[i].caption == NULL) {
			continue;
		}

		struct button_callbacks callbacks = {
			elems[i].on_render, NULL, NULL, elems[i].on_menu
		};
		push_button(&menu_panel->button_bank,
				font,
				elems[i].caption,
				elems[i].info,
				callbacks,
				&rect,
				CAPTION_POSITION_LEFT);

		rect.y += rect.h;
		menu_panel->rect.h += rect.h;
	}

	return menu_panel;
}

static void load_next_menu_panel(const struct window *window,
		struct menu_panel *menu_panel, const struct button *origin,
		size_t n_elems, struct menu_elem *elems)
{
	assert(menu_panel->next == NULL);
	menu_panel->next = make_menu_panel(origin,
			window->status_bar.font, n_elems, elems);
}

static void do_menu_cleanup(struct button *button,
		struct menu_panel *menu_panel, const SDL_Event *event)
{
	switch (event->type) {
		case SDL_MOUSEMOTION:     /* fallthru */
		case SDL_MOUSEBUTTONDOWN: /* fallthru */
		case SDL_MOUSEBUTTONUP:
			if (menu_panel->next != NULL) {
				free_menu_panel(menu_panel->next);
				menu_panel->next = NULL;
			}
			break;
		default:
			quit_fmt("non mouse event %d for button '%s'",
					event->type, button->caption);
			break;
	}
}

static bool select_menu_button(struct button *button,
		struct menu_panel *menu_panel, const SDL_Event *event)
{
	if (button->selected) {
		return false;
	} else {
		do_menu_cleanup(button, menu_panel, event);
		button->selected = true;

		return true;
	}
}

static bool click_menu_button(struct button *button,
		struct menu_panel *menu_panel, const SDL_Event *event)
{
	/* any event on that button removes the existing panel submenus
	 * (and clickable buttons should not have their own submenus) */
	do_menu_cleanup(button, menu_panel, event);

	switch (event->type) {
		case SDL_MOUSEBUTTONDOWN:
			button->selected = true;
			return false;
		case SDL_MOUSEBUTTONUP:
			if (button->selected) {
				button->selected = false;
				/* click the button */
				return true;
			} else {
				return false;
			}
		default:
			return false;
	}
}

static void handle_menu_window(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_UVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	struct window *other = get_loaded_window(button->info.data.uval);
	if (other == NULL) {
		other = get_new_window(button->info.data.uval);
		assert(other != NULL);
		wipe_window_aux_config(other);
		start_window(other);
	}
}

static void handle_menu_windows(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct menu_elem elems[MAX_WINDOWS];

	for (unsigned i = 0; i < MAX_WINDOWS; i++) {
		elems[i].caption = "Window-%u";
		elems[i].info.type = BUTTON_DATA_UVAL;
		elems[i].info.data.uval = i;
		elems[i].info.group = BUTTON_GROUP_MENU;
		elems[i].on_render = render_button_menu_window;
		elems[i].on_menu = handle_menu_window;
	}

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_fullscreen(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	if (window->flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		SDL_SetWindowFullscreen(window->window, 0);
		SDL_SetWindowMinimumSize(window->window,
				DEFAULT_WINDOW_MINIMUM_W, DEFAULT_WINDOW_MINIMUM_H);
	} else {
		SDL_SetWindowFullscreen(window->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	}

	window->flags = SDL_GetWindowFlags(window->window);
}

static void handle_menu_about(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	show_about(window);
}

static void handle_menu_quit(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	handle_quit();
}

static void handle_menu_tile_set(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_IVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}
	
	graphics_mode *mode = get_graphics_mode(button->info.data.ival);
	assert(mode != NULL);

	reload_graphics(window, mode);

	refresh_display_terms();
}

static void handle_menu_tile_sets(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	size_t num_elems = 0;

	graphics_mode *mode = graphics_modes;
	while (mode != NULL) {
		num_elems++;
		mode = mode->pNext;
	}

	struct menu_elem elems[num_elems];

	mode = graphics_modes;
	for (size_t i = 0; i < num_elems; i++) {
		elems[i].caption = mode->menuname;
		elems[i].info.type = BUTTON_DATA_IVAL;
		elems[i].info.data.ival = mode->grafID;
		elems[i].info.group = BUTTON_GROUP_MENU;
		elems[i].on_render = render_button_menu_tile_set;
		elems[i].on_menu = handle_menu_tile_set;

		mode = mode->pNext;
	}

	load_next_menu_panel(window, menu_panel, button, num_elems, elems);
}

static void handle_menu_tiles(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct button_info info = {
		BUTTON_DATA_SUBVAL, {.subval = button->info.data.subval}, BUTTON_GROUP_MENU
	};

	struct menu_elem elems[] = {
		{"Set", info, render_button_menu_simple, handle_menu_tile_sets},
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_font_name(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_FONTVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	assert(button->info.data.fontval.index < N_ELEMENTS(g_font_info));
	CHECK_FONTVAL(button->info.data.fontval);

	struct window *winval = button->info.data.fontval.window;
	struct subwindow *subval = button->info.data.fontval.subwindow;

	unsigned index = button->info.data.fontval.index;

	const struct font_info *font_info = &g_font_info[index];
	assert(font_info->loaded);

	if (subval && subval->font->index != index) {
		if (reload_font(subval, font_info)) {
			button->info.data.fontval.size_ok = true;
		} else {
			button->info.data.fontval.size_ok = false;
		}
	} else if (winval && winval->game_font->index != index) {
		free_font(winval->game_font);
		winval->game_font = make_font(winval, font_info->name, font_info->size);
	}
}

static void handle_menu_font_size(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_FONTVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}
	if (!button->info.data.fontval.size_ok) {
		return;
	}

	CHECK_FONTVAL(button->info.data.fontval);

	unsigned index = button->info.data.fontval.index;
	struct font_info *info = &g_font_info[index];

	if (info->type == FONT_TYPE_RASTER) {
		return;
	}

	struct window *winval = button->info.data.fontval.window;
	struct subwindow *subval = button->info.data.fontval.subwindow;

	int size = winval != NULL ?
		winval->game_font->size : subval->font->size;

	int increment =
		(event->button.x - button->full_rect.x < button->full_rect.w / 2) ? -1 : +1;

	for (size_t i = 0; i < MAX_VECTOR_FONT_SIZE - MIN_VECTOR_FONT_SIZE; i++) {
		size += increment;
		if (size > MAX_VECTOR_FONT_SIZE) {
			size = MIN_VECTOR_FONT_SIZE;
		} else if (size < MIN_VECTOR_FONT_SIZE) {
			size = MAX_VECTOR_FONT_SIZE;
		}
		info->size = size;

		if (winval != NULL) {
			free_font(winval->game_font);
			winval->game_font = make_font(winval, info->name, size);
			return;
		} else if (subval != NULL && reload_font(subval, info)) {
			return;
		}
	}

	button->info.data.fontval.size_ok = false;
}

static void load_next_menu_panel_font_sizes(const struct window *window,
		struct menu_panel *menu_panel, const struct button *button,
		struct window *winval, struct subwindow *subval, unsigned index)
{
	struct button_info info = {
		BUTTON_DATA_FONTVAL,
		{
			.fontval = {
				.subwindow = subval,
				.index = index,
				.size_ok = true,
				.window = winval
			}
		},
		BUTTON_GROUP_MENU
	};

	CHECK_FONTVAL(info.data.fontval);

	struct menu_elem elems[] = {
		{"< %2d points >", info, render_button_menu_font_size, handle_menu_font_size}
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_font_sizes_subwindow(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subval = button->info.data.subval;
	unsigned index = subval->font->index;

	load_next_menu_panel_font_sizes(window, menu_panel, button,
			NULL, subval, index);
}

static void handle_menu_font_sizes_window(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_WINVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct window *winval = button->info.data.winval;
	unsigned index = winval->game_font->index;

	load_next_menu_panel_font_sizes(window, menu_panel, button,
			winval, NULL, index);
}

static void load_next_menu_panel_font_names(const struct window *window,
		struct menu_panel *menu_panel, const struct button *button,
		struct window *winval, struct subwindow *subval)
{
	struct menu_elem elems[N_ELEMENTS(g_font_info)];

	size_t num_elems = 0;
	for (size_t i = 0; i < N_ELEMENTS(g_font_info); i++) {
		if (g_font_info[i].loaded) {
			elems[num_elems].caption = g_font_info[i].name;
			elems[num_elems].info.type = BUTTON_DATA_FONTVAL;

			elems[num_elems].info.data.fontval.subwindow = subval;
			elems[num_elems].info.data.fontval.size_ok = true;
			elems[num_elems].info.data.fontval.index = i;
			elems[num_elems].info.data.fontval.window = winval;

			elems[num_elems].info.group = BUTTON_GROUP_MENU;
			elems[num_elems].on_render = render_button_menu_font_name;
			elems[num_elems].on_menu = handle_menu_font_name;

			CHECK_FONTVAL(elems[num_elems].info.data.fontval);

			num_elems++;
		}
	}

	load_next_menu_panel(window, menu_panel, button, num_elems, elems);
}

static void handle_menu_font_names_subwindow(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	load_next_menu_panel_font_names(window, menu_panel, button,
			NULL, button->info.data.subval);
}

static void handle_menu_font_names_window(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_WINVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	load_next_menu_panel_font_names(window, menu_panel, button,
			button->info.data.winval, NULL);
}

static void handle_menu_font_window(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_WINVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct button_info info = {
		BUTTON_DATA_WINVAL, {.winval = button->info.data.winval}, BUTTON_GROUP_MENU
	};

	struct menu_elem elems[] = {
		{"Name", info, render_button_menu_simple, handle_menu_font_names_window},
		{"Size", info, render_button_menu_simple, handle_menu_font_sizes_window}
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_font_other(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct button_info info = {
		BUTTON_DATA_SUBVAL, {.subval = button->info.data.subval}, BUTTON_GROUP_MENU
	};

	struct menu_elem elems[] = {
		{"Name", info, render_button_menu_simple, handle_menu_font_names_subwindow},
		{"Size", info, render_button_menu_simple, handle_menu_font_sizes_subwindow}
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_font_cave(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.subval;

	struct button_info info_perm = {
		BUTTON_DATA_SUBVAL, {.subval = subwindow}, BUTTON_GROUP_MENU
	};

	struct button_info info_temp = {
		BUTTON_DATA_WINVAL, {.winval = subwindow->window}, BUTTON_GROUP_MENU
	};

	const char *caption_map = "Map";
	const char *caption_other = subwindow->window->temporary.number > 0 ?
		NULL : "Other";

	struct menu_elem elems[] = {
		{caption_map, info_perm, render_button_menu_simple, handle_menu_font_other},
		{caption_other, info_temp, render_button_menu_simple, handle_menu_font_window}
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_borders(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.subval;

	subwindow->borders.visible = !subwindow->borders.visible;
	render_borders(subwindow);
}

static void handle_menu_subwindow_alpha(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_ALPHAVAL);

	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.alphaval.subwindow;
	subwindow->color.a = button->info.data.alphaval.real_value;

	render_clear(subwindow->window, subwindow->texture, &subwindow->color);
	render_borders(subwindow);

	refresh_display_terms();
}

static void handle_menu_alpha(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.subval;

	struct menu_elem elems[(100 - DEFAULT_ALPHA_LOWEST) / DEFAULT_ALPHA_STEP +
		1 + ((100 - DEFAULT_ALPHA_LOWEST) % DEFAULT_ALPHA_STEP == 0 ? 0 : 1)];

	for (size_t i = 0; i < N_ELEMENTS(elems); i++) {
		int alpha = ALPHA_PERCENT(DEFAULT_ALPHA_LOWEST + i * DEFAULT_ALPHA_STEP);

		elems[i].caption = " %3d%% ";
		elems[i].info.type = BUTTON_DATA_ALPHAVAL;
		elems[i].info.data.alphaval.subwindow = subwindow;
		elems[i].info.data.alphaval.real_value = alpha;
		elems[i].info.data.alphaval.show_value = i * DEFAULT_ALPHA_STEP;
		elems[i].info.group = BUTTON_GROUP_MENU;
		elems[i].on_render = render_button_menu_alpha;
		elems[i].on_menu = handle_menu_subwindow_alpha;
	}
	elems[N_ELEMENTS(elems) - 1].info.data.alphaval.real_value =
		DEFAULT_ALPHA_FULL;

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void handle_menu_top(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	(void) window;

	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);
	
	if (!click_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.subval;

	subwindow->always_top = !subwindow->always_top;

	sort_to_top(subwindow->window);
}

static void handle_menu_terms(struct window *window,
		struct button *button, const SDL_Event *event,
		struct menu_panel *menu_panel)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_SUBVAL);

	if (!select_menu_button(button, menu_panel, event)) {
		return;
	}

	struct subwindow *subwindow = button->info.data.subval;

	struct button_info info = {BUTTON_DATA_SUBVAL, {.subval = subwindow}, BUTTON_GROUP_MENU};

	struct menu_elem elems[] = {
		{
			"Font", info, render_button_menu_simple, 
			subwindow->index == DISPLAY_CAVE ?
				handle_menu_font_cave : handle_menu_font_other
		},
		{
			subwindow->index == DISPLAY_CAVE ? "Tiles" : NULL,
			info, render_button_menu_simple, handle_menu_tiles
		},
		{
			subwindow->index == DISPLAY_CAVE ? NULL : "Alpha",
			info, render_button_menu_simple, handle_menu_alpha
		},
		{
			"Borders", info, render_button_menu_borders, handle_menu_borders
		},
		{
			"Top", info, render_button_menu_top, handle_menu_top
		}
	};

	load_next_menu_panel(window, menu_panel, button, N_ELEMENTS(elems), elems);
}

static void load_main_menu_panel(struct status_bar *status_bar)
{
	struct menu_elem term_elems[SUBWINDOW_PERMANENT_MAX];
	size_t n_terms = 0;

	for (size_t i = 0; i < N_ELEMENTS(term_elems); i++) {
		struct subwindow *subwindow =
			get_subwindow_by_index(status_bar->window, i, true);

		if (subwindow != NULL) {
			term_elems[n_terms].caption = display_term_get_name(subwindow->index);
			term_elems[n_terms].info.type = BUTTON_DATA_SUBVAL;
			term_elems[n_terms].info.data.subval = subwindow;
			term_elems[n_terms].info.group = BUTTON_GROUP_MENU;
			term_elems[n_terms].on_render = render_button_menu_terms;
			term_elems[n_terms].on_menu = handle_menu_terms;
			n_terms++;
		}
	}

	struct button_info info = {BUTTON_DATA_NONE, {0}, BUTTON_GROUP_MENU};
	struct menu_elem other_elems[] = {
		{
			"Fullscreen",
			info, render_button_menu_fullscreen, handle_menu_fullscreen
		},
		{
			status_bar->window->index == WINDOW_MAIN ? "Windows" : NULL,
			info, render_button_menu_simple, handle_menu_windows
		},
		{
			"About",
			info, render_button_menu_simple, handle_menu_about
		},
		{
			"Quit", 
			info, render_button_menu_simple, handle_menu_quit
		}
	};

	struct menu_elem elems[N_ELEMENTS(term_elems) + N_ELEMENTS(other_elems)];

	memcpy(elems, term_elems, n_terms * sizeof(term_elems[0]));
	memcpy(elems + n_terms, other_elems, sizeof(other_elems));

	struct button dummy = {0};
	dummy.full_rect.x = status_bar->full_rect.x;
	dummy.full_rect.y = status_bar->full_rect.y + status_bar->full_rect.h;

	status_bar->menu_panel = make_menu_panel(&dummy, status_bar->font,
			n_terms + N_ELEMENTS(other_elems), elems);
}

static void unselect_menu_buttons(struct menu_panel *menu_panel)
{
	if (menu_panel == NULL) {
		return;
	}

	for (size_t i = 0; i < menu_panel->button_bank.number; i++) {
		menu_panel->button_bank.buttons[i].selected = false;
		menu_panel->button_bank.buttons[i].highlighted = false;
	}

	unselect_menu_buttons(menu_panel->next);
}

static bool handle_menu_button_mousemotion(struct window *window,
		const SDL_Event *event)
{
	assert(event->type == SDL_MOUSEMOTION);

	bool handled = false;

	struct menu_panel *menu_panel = get_menu_panel_by_xy(window->status_bar.menu_panel,
				event->motion.x, event->motion.y);

	if (menu_panel == NULL) {
		return handled;
	}

	for (size_t i = 0; i < menu_panel->button_bank.number; i++) {
		struct button *button = &menu_panel->button_bank.buttons[i];
		if (is_point_in_rect(event->motion.x, event->motion.y,
					&button->full_rect))
		{
			/* note that the buttons themselves set "selected" */
			button->highlighted = true;

			assert(button->callbacks.on_menu != NULL);
			button->callbacks.on_menu(window, button, event, menu_panel);

			handled = true;

		} else {
			button->highlighted = false;
			/* but we do unset selected */
			button->selected = false;
		}
	}

	unselect_menu_buttons(menu_panel->next);

	return handled;
}

static bool handle_menu_button_click(struct window *window,
		const SDL_Event *event)
{
	assert(event->type == SDL_MOUSEBUTTONDOWN
			|| event->type == SDL_MOUSEBUTTONUP);

	bool handled = false;

	struct menu_panel *menu_panel = get_menu_panel_by_xy(window->status_bar.menu_panel,
				event->button.x, event->button.y);

	if (menu_panel == NULL) {
		return handled;
	}

	for (size_t i = 0; i < menu_panel->button_bank.number; i++) {
		struct button *button = &menu_panel->button_bank.buttons[i];
		if (is_point_in_rect(event->button.x, event->button.y,
					&button->full_rect))
		{
			assert(button->callbacks.on_menu != NULL);
			button->callbacks.on_menu(window, button, event, menu_panel);

			handled = true;
		}
	}

	return handled;
}

static bool handle_menu_event(struct window *window, const SDL_Event *event)
{
	switch (event->type) {
		case SDL_MOUSEMOTION:
			return handle_menu_button_mousemotion(window, event);
		case SDL_MOUSEBUTTONDOWN: /* fallthru */
		case SDL_MOUSEBUTTONUP:
			return handle_menu_button_click(window, event);
		default:
			return false;
	}
}

static bool is_menu_button_mouse_click(const struct button *button,
		const SDL_Event *event)
{
	if ((event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP)
			&& is_point_in_rect(event->button.x, event->button.y, &button->full_rect))
	{
		return true;
	}

	return false;
}

static bool handle_menu_button(struct window *window,
		struct button *button, const SDL_Event *event)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_MENU, BUTTON_DATA_NONE);

	switch (event->type) {
		case SDL_MOUSEMOTION:
			if (is_point_in_rect(event->motion.x, event->motion.y, &button->full_rect)) {
				/* create menu on mouseover */
				if (window->status_bar.menu_panel == NULL) {
					load_main_menu_panel(&window->status_bar);
				}
				button->highlighted = true;
				return true;
			} else if (handle_menu_event(window, event)) {
				return true;
			} else if (button->highlighted) {
				/* the menu sticks around so that it is not horrible to use */
				return true;
			}
			return false;
		default:
			if (handle_menu_event(window, event)) {
				return true;
			} else if (is_menu_button_mouse_click(button, event)) {
				/* menu button just eats mouse clicks */
				return true;
			}

			if (window->status_bar.menu_panel != NULL) {
				free_menu_panel(window->status_bar.menu_panel);
				window->status_bar.menu_panel = NULL;
			}
			button->highlighted = false;
			return false;
	}
}

static bool do_button(struct window *window,
		struct button *button, const SDL_Event *event)
{
	switch (event->type) {
		case SDL_MOUSEBUTTONDOWN:
			if (is_point_in_rect(event->button.x, event->button.y, &button->full_rect)) {
				button->selected = true;
				return true;
			}
			break;
		case SDL_MOUSEBUTTONUP:
			if (is_point_in_rect(event->button.x, event->button.y, &button->full_rect)
					&& button->selected)
			{
				assert(button->callbacks.on_click != NULL);
				button->callbacks.on_click(window, button);
				button->selected = false;
				return true;
			}
			break;
		case SDL_MOUSEMOTION:
			if (is_point_in_rect(event->button.x, event->button.y, &button->full_rect)) {
				button->highlighted = true;
				return true;
			}
			break;
	}

	button->highlighted = false;
	button->selected = false;

	return false;
}

static bool is_close_to(int a, int b, unsigned range)
{
	if (a > 0 && b > 0) {
		return (unsigned) ABS(a - b) < range;
	} else if (a < 0 && b < 0) {
		return (unsigned) ABS(ABS(a) - ABS(b)) < range;
	} else {
		return (unsigned) (ABS(a) + ABS(b)) < range;
	}
}

static bool is_point_in_rect(int x, int y, const SDL_Rect *rect)
{
	return x >= rect->x && x < rect->x + rect->w
		&& y >= rect->y && y < rect->y + rect->h;
}

static bool is_rect_in_rect(const SDL_Rect *small, const SDL_Rect *big)
{
	return small->x >= big->x
		&& small->x + small->w <= big->x + big->w
		&& small->y >= big->y
		&& small->y + small->h <= big->y + big->h;
}

static void fit_rect_in_rect_by_hw(SDL_Rect *small, const SDL_Rect *big)
{
	if (small->x < big->x) {
		small->w -= big->x - small->x;
		small->x = big->x;
	}
	if (small->x + small->w > big->x + big->w) {
		small->w = big->x + big->w - small->x;
	}
	if (small->y < big->y) {
		small->h -= big->y - small->y;
		small->y = big->y;
	}
	if (small->y + small->h > big->y + big->h) {
		small->h = big->y + big->h - small->y;
	}
}

static void fit_rect_in_rect_by_xy(SDL_Rect *small, const SDL_Rect *big)
{
	if (small->x < big->x) {
		small->x = big->x;
	}
	if (small->y < big->y) {
		small->y = big->y;
	}
	if (small->x + small->w > big->x + big->w) {
		small->x = MAX(big->x, big->x + big->w - small->w);
	}
	if (small->y + small->h > big->y + big->h) {
		small->y = MAX(big->y, big->y + big->h - small->h);
	}
}

static void fit_rect_in_rect_proportional(SDL_Rect *small, const SDL_Rect *big)
{
	if (small->x < big->x) {
		small->x = big->x;
	}
	if (small->y < big->y) {
		small->y = big->y;
	}
	if (small->w > big->w) {
		small->h = small->h * big->w / small->w;
		small->w = big->w;
	}
	if (small->h > big->h) {
		small->w = small->w * big->h / small->h;
		small->h = big->h;
	}
}

static void resize_rect(SDL_Rect *rect,
		int left, int top, int right, int bottom)
{
	if (rect->w - left + right <= 0
			|| rect->h - top + bottom <= 0)
	{
		return;
	}
	rect->x += left;
	rect->w -= left;

	rect->y += top;
	rect->h -= top;

	rect->w += right;
	rect->h += bottom;
}

/* tries to snap to other term in such a way so that their
 * (visible) borders overlap */
static void try_snap(struct window *window,
		struct subwindow *subwindow, SDL_Rect *rect)
{
	for (size_t i = window->permanent.number; i > 0; i--) {
		struct subwindow *other = window->permanent.subwindows[i - 1];
		if (other == NULL
				|| !other->visible
				|| other->index == subwindow->index)
		{
			continue;
		}

		int ox = other->full_rect.x;
		int oy = other->full_rect.y;
		int ow = other->full_rect.w;
		int oh = other->full_rect.h;

		if (oy < rect->y + rect->h && rect->y < oy + oh) {
			if (is_close_to(rect->x, ox + ow, DEFAULT_SNAP_RANGE)) {
				rect->x = ox + ow - DEFAULT_VISIBLE_BORDER;
			}
			if (is_close_to(rect->x + rect->w, ox, DEFAULT_SNAP_RANGE)) {
				rect->x = ox - rect->w + DEFAULT_VISIBLE_BORDER;
			}
		}
		if (ox < rect->x + rect->w && rect->x < ox + ow) {
			if (is_close_to(rect->y, oy + oh, DEFAULT_SNAP_RANGE)) {
				rect->y = oy + oh - DEFAULT_VISIBLE_BORDER;
			}
			if (is_close_to(rect->y + rect->h, oy, DEFAULT_SNAP_RANGE)) {
				rect->y = oy - rect->h + DEFAULT_VISIBLE_BORDER;
			}
		}
	}
}

static void start_moving(struct window *window,
		struct subwindow *subwindow, const SDL_MouseButtonEvent *mouse)
{
	assert(!window->size_state.active);

	bring_to_top(window, subwindow);

	window->move_state.originx = mouse->x;
	window->move_state.originy = mouse->y;

	window->move_state.subwindow = subwindow;
	window->move_state.moving = true;
}

static void start_sizing(struct window *window,
		struct subwindow *subwindow, const SDL_MouseButtonEvent *mouse)
{
	assert(!window->move_state.active);

	bring_to_top(window, subwindow);

	subwindow->sizing_rect = subwindow->full_rect;

	int x = mouse->x - (subwindow->full_rect.x + subwindow->full_rect.w / 2);
	int y = mouse->y - (subwindow->full_rect.y + subwindow->full_rect.h / 2);

	window->size_state.left = x < 0 ? true : false;
	window->size_state.top  = y < 0 ? true : false;

	window->size_state.originx = mouse->x;
	window->size_state.originy = mouse->y;

	window->size_state.subwindow = subwindow;
	window->size_state.sizing = true;
}

static bool handle_menu_mousebuttondown(struct window *window,
		const SDL_MouseButtonEvent *mouse)
{
	if (window->move_state.active || window->size_state.active) {
		struct subwindow *subwindow = get_subwindow_by_xy(window, mouse->x, mouse->y, false);
		if (subwindow != NULL
				&& is_rect_in_rect(&subwindow->full_rect, &window->inner_rect))
		{
			if (window->move_state.active && !window->move_state.moving) {
				start_moving(window, subwindow, mouse);
			} else if (window->size_state.active && !window->size_state.sizing) {
				start_sizing(window, subwindow, mouse);
			}
		}
		return true;
	} else if (is_over_status_bar(&window->status_bar, mouse->x, mouse->y)) {
		return true;
	} else {
		return false;
	}
}

static void handle_window_closed(const SDL_WindowEvent *event)
{
	struct window *window = get_window_by_id(event->windowID);
	assert(window != NULL);

	if (window->index == WINDOW_MAIN) {
		handle_quit();
	} else {
		free_window(window);
	}
}

static void handle_window_focus(const SDL_WindowEvent *event)
{
	assert(event->event == SDL_WINDOWEVENT_FOCUS_GAINED
			|| event->event == SDL_WINDOWEVENT_FOCUS_LOST);

	struct window *window = get_window_by_id(event->windowID);
	if (window == NULL) {
		/* when window is closed, it sends FOCUS_LOST event */
		assert(event->event == SDL_WINDOWEVENT_FOCUS_LOST);
		return;
	}
	
	switch (event->event) {
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			window->focus = true;
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			window->focus = false;
			break;
	}
}

static void handle_last_resize_event(int num_events, const SDL_Event *events)
{
	assert(num_events > 0);

	for (int i = num_events - 1; i >= 0; i--) {
		if (events[i].window.event == SDL_WINDOWEVENT_RESIZED) {
			const SDL_WindowEvent event = events[i].window;

			struct window *window = get_window_by_id(event.windowID);
			assert(window != NULL);
			resize_window(window, event.data1, event.data2);

			return;
		}
	}
}

static void handle_windowevent(const SDL_WindowEvent *event)
{
	SDL_Event events[128];
	events[0].window = *event;

	int num_events = 1 + SDL_PeepEvents(events + 1, (int) N_ELEMENTS(events) - 1,
			SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT);

	bool resize = false;

	for (int i = 0; i < num_events; i++) {
		switch (events[i].window.event) {
			case SDL_WINDOWEVENT_RESIZED:
				/* just for efficiency */
				resize = true;
				break;
			case SDL_WINDOWEVENT_CLOSE:
				handle_window_closed(&events[i].window);
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED: /* fallthru */
			case SDL_WINDOWEVENT_FOCUS_LOST:
				handle_window_focus(&events[i].window);
				break;
		}
	}

	if (resize) {
		handle_last_resize_event(num_events, events);
	}

	redraw_all_windows();
}

static void resize_subwindow(struct subwindow *subwindow)
{
	SDL_DestroyTexture(subwindow->texture);

	subwindow->full_rect = subwindow->sizing_rect;
	if (!adjust_subwindow_geometry(subwindow->window, subwindow)) {
		quit_fmt("bad_geometry of subwindow %u in window %u",
				subwindow->index, subwindow->window->index);
	}

	subwindow->texture = make_subwindow_texture(subwindow->window,
			subwindow->full_rect.w, subwindow->full_rect.h);

	render_clear(subwindow->window, subwindow->texture, &subwindow->color);
	render_borders(subwindow);

	Term_push(subwindow->term);
	Term_resize(subwindow->cols, subwindow->rows);
	Term_flush_output();
	Term_pop();

	refresh_display_terms();
}

static void do_sizing(struct window *window, int x, int y)
{
	struct size_state *size_state = &window->size_state;

	assert(size_state->subwindow != NULL);

	SDL_Rect rect = size_state->subwindow->sizing_rect;

	int newx = x - size_state->originx;
	int newy = y - size_state->originy;

	int left   = size_state->left ? newx : 0;
	int top    = size_state->top  ? newy : 0;
	int right  = size_state->left ? 0 : newx;
	int bottom = size_state->top  ? 0 : newy;

	resize_rect(&rect, left, top, right, bottom);
	fit_rect_in_rect_by_hw(&rect, &window->inner_rect);

	if (is_ok_col_row(size_state->subwindow,
				&rect,
				size_state->subwindow->cell_width,
				size_state->subwindow->cell_height))
	{
		size_state->subwindow->sizing_rect = rect;
	}

	size_state->originx = x;
	size_state->originy = y;
}

static void do_moving(struct window *window, int x, int y)
{
	struct move_state *move_state = &window->move_state;

	assert(move_state->subwindow != NULL);

	SDL_Rect *rect = &move_state->subwindow->full_rect;

	rect->x += x - move_state->originx;
	rect->y += y - move_state->originy;

	try_snap(window, move_state->subwindow, rect);
	fit_rect_in_rect_by_xy(rect, &window->inner_rect);

	move_state->originx = x;
	move_state->originy = y;
}

static bool handle_menu_mousebuttonup(struct window *window,
		const SDL_MouseButtonEvent *mouse)
{
	if (window->move_state.active && window->move_state.moving) {
		window->move_state.moving = false;
	} else if (window->size_state.active && window->size_state.sizing) {
		window->size_state.sizing = false;

		if (window->size_state.subwindow != NULL) {
			resize_subwindow(window->size_state.subwindow);
		}
	} 
	
	if (window->move_state.active
			|| window->size_state.active
			|| is_over_status_bar(&window->status_bar, mouse->x, mouse->y))
	{
		return true;
	} else {
		return false;
	}
}

static bool handle_menu_mousemotion(struct window *window,
		const SDL_MouseMotionEvent *mouse)
{
	if (window->move_state.moving) {
		do_moving(window, mouse->x, mouse->y);
		return true;
	} else if (window->size_state.sizing) {
		do_sizing(window, mouse->x, mouse->y);
		return true;
	} else if (window->move_state.active || window->size_state.active) {
		return true;
	} else if (is_over_status_bar(&window->status_bar, mouse->x, mouse->y)) {
		return true;
	}

	return false;
}

static bool handle_menu_keyboard(struct window *window, const SDL_Event *event)
{
	if (window->move_state.active || window->size_state.active) {
		return true;
	}

	SDL_Event key = *event;
	/* the user pressed a key; probably wants to play? */
	SDL_PushEvent(&key);

	return false;
}

static bool handle_status_bar_buttons(struct window *window,
		const SDL_Event *event)
{
	bool handled = false;

	for (size_t i = 0; i < window->status_bar.button_bank.number; i++) {
		struct button *button = &window->status_bar.button_bank.buttons[i];
		if (button->callbacks.on_event != NULL) {
			handled |= button->callbacks.on_event(window, button, event);
		} else {
			handled |= do_button(window, button, event);
		}
	}

	return handled;
}

static void redraw_status_bar_buttons(struct window *window)
{
	SDL_Event shutdown = {.type = SDL_USEREVENT};
	(void) handle_status_bar_buttons(window, &shutdown);
}

static bool handle_menu_windowevent(struct window *window,
		const SDL_WindowEvent *event)
{
	if (window->move_state.active) {
		signal_move_state(window);
	} else if (window->size_state.active) {
		signal_size_state(window);
	}

	redraw_status_bar_buttons(window);

	handle_windowevent(event);

	return false;
}

static bool is_event_windowid_ok(const struct window *window, const SDL_Event *event)
{
	switch (event->type) {
		case SDL_KEYDOWN: /* fallthru */
		case SDL_KEYUP:
			return event->key.windowID == window->id;
		case SDL_TEXTINPUT:
			return event->text.windowID == window->id;
		case SDL_MOUSEMOTION:
			return event->motion.windowID == window->id;
		case SDL_MOUSEBUTTONDOWN: /* fallthru */
		case SDL_MOUSEBUTTONUP:
			return event->button.windowID == window->id;
		default:
			return true;
	}
}

/* returns true for events that should be processed by buttons */
static bool is_ok_button_event(const struct window *window, const SDL_Event *event)
{
	switch (event->type) {
		case SDL_KEYDOWN:         /* fallthru */
		case SDL_KEYUP:           /* fallthru */
		case SDL_TEXTINPUT:       /* fallthru */
			return is_event_windowid_ok(window, event);
		case SDL_MOUSEMOTION:     /* fallthru */
		case SDL_MOUSEBUTTONDOWN: /* fallthru */
		case SDL_MOUSEBUTTONUP:
			return window->focus && is_event_windowid_ok(window, event);
		case SDL_USEREVENT:
			return true;
		default:
			return false;
	}
}

static bool handle_status_bar_events(struct window *window,
		const SDL_Event *event)
{
	if (!is_event_windowid_ok(window, event)) {
		/* just in case */
		if (window->move_state.active) {
			signal_move_state(window);
		} else if (window->size_state.active) {
			signal_size_state(window);
		}
		return false;
	}

	switch (event->type) {
		case SDL_MOUSEMOTION:
			return handle_menu_mousemotion(window, &event->motion);
		case SDL_MOUSEBUTTONDOWN:
			return handle_menu_mousebuttondown(window, &event->button);
		case SDL_MOUSEBUTTONUP:
			return handle_menu_mousebuttonup(window, &event->button);
		case SDL_KEYDOWN:     /* fallthru */
		case SDL_KEYUP:       /* fallthru */
		case SDL_TEXTEDITING: /* fallthru */
		case SDL_TEXTINPUT:
			return handle_menu_keyboard(window, event);
		case SDL_WINDOWEVENT:
			return handle_menu_windowevent(window, &event->window);
		case SDL_QUIT:
			handle_quit();
			return false;
		default:
			return false;
	}
}

static void do_status_bar_loop(struct window *window)
{
	window->status_bar.is_in_menu = true;

	bool keep_going = true;
	while (keep_going) {
		SDL_Delay(window->delay);

		SDL_Event event;
		SDL_WaitEvent(&event);

		bool handled = false;
		if (is_ok_button_event(window, &event)
				&& !window->move_state.moving
				&& !window->size_state.sizing)
		{
			 handled = handle_status_bar_buttons(window, &event);
		}

		if (event.type == SDL_MOUSEMOTION) {
			/* annoying mousemotion spam! */
			SDL_FlushEvent(SDL_MOUSEMOTION);
		}

		if (!handled) {
			/* so the user didnt click on a button */
			keep_going = handle_status_bar_events(window, &event);
		}

		redraw_window(window);
	}

	window->status_bar.is_in_menu = false;
}

static bool has_visible_subwindow(const struct window *window, unsigned index)
{
	return get_subwindow_by_index(window, index, true) != NULL;
}

static bool handle_mousemotion(const SDL_MouseMotionEvent *mouse)
{
	struct window *window = get_window_by_id(mouse->windowID);

	if (is_over_status_bar(&window->status_bar, mouse->x, mouse->y)) {
		do_status_bar_loop(window);
	}
	/* dont need other mousemotion events */
	SDL_FlushEvent(SDL_MOUSEMOTION);

	return false;
}

/* x and y are relative to window */
static bool get_colrow_from_xy(const struct subwindow *subwindow,
		int x, int y, int *col, int *row)
{
	SDL_Rect rect = {
		subwindow->full_rect.x + subwindow->inner_rect.x,
		subwindow->full_rect.y + subwindow->inner_rect.y,
		subwindow->inner_rect.w,
		subwindow->inner_rect.h
	};

	if (!is_point_in_rect(x, y, &rect)) {
		return false;
	}

	*col = (x - rect.x) / subwindow->cell_width;
	*row = (y - rect.y) / subwindow->cell_height;

	return true;
}

static byte translate_key_mods(Uint16 mods)
{
#define TRANSLATE_K_MOD(m, k) ((m) & mods ? (k) : 0)
	byte angband_mods =
		TRANSLATE_K_MOD(KMOD_SHIFT, KC_MOD_SHIFT)
		| TRANSLATE_K_MOD(KMOD_CTRL, KC_MOD_CONTROL)
		| TRANSLATE_K_MOD(KMOD_ALT, KC_MOD_ALT)
		| TRANSLATE_K_MOD(KMOD_GUI, KC_MOD_META);
#undef TRANSLATE_K_MOD
	return angband_mods;
}

static bool handle_mousebuttondown(const SDL_MouseButtonEvent *mouse)
{
	struct window *window = get_window_by_id(mouse->windowID);
	assert(window != NULL);

	struct subwindow *subwindow = get_subwindow_by_xy(window, mouse->x, mouse->y, true);
	if (subwindow == NULL) {
		/* not an error, the user clicked in some random place */
		return false;
	} else if (!subwindow->is_temporary && !subwindow->is_top) {
		bring_to_top(window, subwindow);
		redraw_window(window);
		return false;
	}

	/* all interactive terms are only in main window */
	if (window->index != WINDOW_MAIN) {
		return false;
	}

	int button;
	switch (mouse->button) {
		case SDL_BUTTON_LEFT:
			button = MOUSE_BUTTON_LEFT;
			break;
		case SDL_BUTTON_RIGHT:
			button = MOUSE_BUTTON_RIGHT;
			break;
		case SDL_BUTTON_MIDDLE:
			button = MOUSE_BUTTON_MIDDLE;
			break;
		default:
			return false;
	}

	int col;
	int row;
	if (!get_colrow_from_xy(subwindow, mouse->x, mouse->y, &col, &row)) {
		return false;
	}

	byte mods = translate_key_mods(SDL_GetModState());
	int index = subwindow->is_temporary ?
		-((int) subwindow->index) : (int) subwindow->index;

	Term_mousepress(col, row, button, mods, index);

	return true;
}

static bool handle_keydown(const SDL_KeyboardEvent *key)
{
	byte mods = translate_key_mods(key->keysym.mod);
	keycode_t ch = 0;

	if (!(key->keysym.mod & KMOD_NUM)
			|| (key->keysym.mod & KMOD_NUM
				&& key->keysym.mod & KMOD_SHIFT))
	{
		switch (key->keysym.sym) {
			/* keypad keys without numlock */
			case SDLK_KP_0:    ch = '0';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_1:    ch = '1';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_2:    ch = '2';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_3:    ch = '3';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_4:    ch = '4';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_5:    ch = '5';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_6:    ch = '6';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_7:    ch = '7';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_8:    ch = '8';      mods |= KC_MOD_KEYPAD; break;
			case SDLK_KP_9:    ch = '9';      mods |= KC_MOD_KEYPAD; break;
		}
	}

	switch (key->keysym.sym) {
		case SDLK_KP_MULTIPLY: ch = '*';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_PERIOD:   ch = '.';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_DIVIDE:   ch = '/';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_EQUALS:   ch = '=';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_MINUS:    ch = '-';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_PLUS:     ch = '+';      mods |= KC_MOD_KEYPAD; break;
		case SDLK_KP_ENTER:    ch = KC_ENTER; mods |= KC_MOD_KEYPAD; break;
		/* arrow keys */
		case SDLK_UP:          ch = ARROW_UP;                        break;
		case SDLK_DOWN:        ch = ARROW_DOWN;                      break;
		case SDLK_LEFT:        ch = ARROW_LEFT;                      break;
		case SDLK_RIGHT:       ch = ARROW_RIGHT;                     break;
		/* text editing keys */
		case SDLK_BACKSPACE:   ch = KC_BACKSPACE;                    break;
		case SDLK_PAGEDOWN:    ch = KC_PGDOWN;                       break;
		case SDLK_PAGEUP:      ch = KC_PGUP;                         break;
		case SDLK_INSERT:      ch = KC_INSERT;                       break;
		case SDLK_DELETE:      ch = KC_DELETE;                       break;
		case SDLK_RETURN:      ch = KC_ENTER;                        break;
		case SDLK_ESCAPE:      ch = ESCAPE;                          break;
		case SDLK_HOME:        ch = KC_HOME;                         break;
		case SDLK_END:         ch = KC_END;                          break;
		case SDLK_TAB:         ch = KC_TAB;                          break;
		/* function keys */
		case SDLK_F1:          ch = KC_F1;                           break;
		case SDLK_F2:          ch = KC_F2;                           break;
		case SDLK_F3:          ch = KC_F3;                           break;
		case SDLK_F4:          ch = KC_F4;                           break;
		case SDLK_F5:          ch = KC_F5;                           break;
		case SDLK_F6:          ch = KC_F6;                           break;
		case SDLK_F7:          ch = KC_F7;                           break;
		case SDLK_F8:          ch = KC_F8;                           break;
		case SDLK_F9:          ch = KC_F9;                           break;
		case SDLK_F10:         ch = KC_F10;                          break;
		case SDLK_F11:         ch = KC_F11;                          break;
		case SDLK_F12:         ch = KC_F12;                          break;
		case SDLK_F13:         ch = KC_F13;                          break;
		case SDLK_F14:         ch = KC_F14;                          break;
		case SDLK_F15:         ch = KC_F15;                          break;
	}

	if (mods & KC_MOD_CONTROL) {
		switch (key->keysym.sym) {
			case SDLK_0: ch = '0'; break;
			case SDLK_1: ch = '1'; break;
			case SDLK_2: ch = '2'; break;
			case SDLK_3: ch = '3'; break;
			case SDLK_4: ch = '4'; break;
			case SDLK_5: ch = '5'; break;
			case SDLK_6: ch = '6'; break;
			case SDLK_7: ch = '7'; break;
			case SDLK_8: ch = '8'; break;
			case SDLK_9: ch = '9'; break;

			case SDLK_a: ch = 'a'; break;
			case SDLK_b: ch = 'b'; break;
			case SDLK_c: ch = 'c'; break;
			case SDLK_d: ch = 'd'; break;
			case SDLK_e: ch = 'e'; break;
			case SDLK_f: ch = 'f'; break;
			case SDLK_g: ch = 'g'; break;
			case SDLK_h: ch = 'h'; break;
			case SDLK_i: ch = 'i'; break;
			case SDLK_j: ch = 'j'; break;
			case SDLK_k: ch = 'k'; break;
			case SDLK_l: ch = 'l'; break;
			case SDLK_m: ch = 'm'; break;
			case SDLK_n: ch = 'n'; break;
			case SDLK_o: ch = 'o'; break;
			case SDLK_p: ch = 'p'; break;
			case SDLK_q: ch = 'q'; break;
			case SDLK_r: ch = 'r'; break;
			case SDLK_s: ch = 's'; break;
			case SDLK_t: ch = 't'; break;
			case SDLK_u: ch = 'u'; break;
			case SDLK_v: ch = 'v'; break;
			case SDLK_w: ch = 'w'; break;
			case SDLK_x: ch = 'x'; break;
			case SDLK_y: ch = 'y'; break;
			case SDLK_z: ch = 'z'; break;
		}
	}

	if (ch) {
		if (mods & KC_MOD_CONTROL && !(mods & KC_MOD_KEYPAD)) {
			ch = KTRL(ch);
			if (!MODS_INCLUDE_CONTROL(ch)) {
				mods &= ~KC_MOD_CONTROL;
			}
		}

		Term_keypress(ch, mods);
		return true;
	} else {
		return false;
	}
}

static keycode_t utf8_to_codepoint(const char *utf8_string)
{
	/* hex  == binary
	 * 0x00 == 00000000
	 * 0x80 == 10000000
	 * 0xc0 == 11000000
	 * 0xe0 == 11100000
	 * 0xf0 == 11110000
	 * 0xf8 == 11111000
	 * 0x3f == 00111111
	 * 0x1f == 00011111
	 * 0x0f == 00001111
	 * 0x07 == 00000111 */

	keycode_t key = 0;

#define IS_UTF8_INFO(mask, result) (((unsigned char) utf8_string[0] & (mask)) == (result))
#define EXTRACT_UTF8_INFO(pos, mask, shift) (((unsigned char) utf8_string[(pos)] & (mask)) << (shift))
	/* 6 is the number of information bits in a utf8 continuation byte (10xxxxxx) */
	if (IS_UTF8_INFO(0x80, 0)) {
		key = utf8_string[0];
	} else if (IS_UTF8_INFO(0xe0, 0xc0)) {
		key = EXTRACT_UTF8_INFO(0, 0x1f, 6)
			| EXTRACT_UTF8_INFO(1, 0x3f, 0);
	} else if (IS_UTF8_INFO(0xf0, 0xe0)) {
		key = EXTRACT_UTF8_INFO(0, 0x0f, 12)
			| EXTRACT_UTF8_INFO(1, 0x3f, 6)
			| EXTRACT_UTF8_INFO(2, 0x3f, 0);
	} else if (IS_UTF8_INFO(0xf8, 0xf0)) {
		key = EXTRACT_UTF8_INFO(0, 0x07, 18)
			| EXTRACT_UTF8_INFO(1, 0x3f, 12)
			| EXTRACT_UTF8_INFO(2, 0x3f, 6)
			| EXTRACT_UTF8_INFO(3, 0x3f, 0);
	}
#undef IS_UTF8_INFO
#undef EXTRACT_UTF8_INFO

	return key;
}

static bool handle_text_input(const SDL_TextInputEvent *input)
{
	keycode_t ch = utf8_to_codepoint(input->text);
	if (ch == 0) {
		return false;
	}

	byte mods = translate_key_mods(SDL_GetModState());

	if (mods & KC_MOD_SHIFT) {
		switch (ch) {
			/* maybe the player pressed key on keypad? */
			case '0': /* fallthru */
			case '1': /* fallthru */
			case '2': /* fallthru */
			case '3': /* fallthru */
			case '4': /* fallthru */
			case '5': /* fallthru */
			case '6': /* fallthru */
			case '7': /* fallthru */
			case '8': /* fallthru */
			case '9':
				return false;
		}
	}

	if (!MODS_INCLUDE_SHIFT(ch)) {
		mods &= ~KC_MOD_SHIFT;
	}

	Term_keypress(ch, mods);

	return true;
}

static void wait_anykey(void)
{
	SDL_Event event;

	SDL_EventType expected = SDL_USEREVENT;
	while (true) {
		SDL_WaitEvent(&event);
		if (event.type == expected) {
			return;
		}

		switch (event.type) {
			case SDL_KEYDOWN:
				expected = SDL_KEYUP;
				break;;
			case SDL_MOUSEBUTTONDOWN:
				expected = SDL_MOUSEBUTTONUP;
				break;
			case SDL_MOUSEMOTION:
				SDL_FlushEvent(SDL_MOUSEMOTION);
				break;
			case SDL_QUIT:
				handle_quit();
				break;
			case SDL_WINDOWEVENT:
				handle_windowevent(&event.window);
				return;
		}
	}
}

static void handle_quit(void)
{
	quit(NULL);
}

static bool get_event(void)
{
	SDL_Event event;
	if (!SDL_PollEvent(&event)) {
		return false;
	}

	switch (event.type) {
		case SDL_KEYDOWN:
			return handle_keydown(&event.key);
		case SDL_TEXTINPUT:
			return handle_text_input(&event.text);
		case SDL_MOUSEMOTION:
			return handle_mousemotion(&event.motion);
		case SDL_MOUSEBUTTONDOWN:
			return handle_mousebuttondown(&event.button);
		case SDL_WINDOWEVENT:
			handle_windowevent(&event.window);
			return false;
		case SDL_QUIT:
			handle_quit();
			return false;
		default:
			return false;
	}
}

static void refresh_display_terms(void)
{
	if (character_dungeon) {
		do_cmd_redraw();
	}

	redraw_all_windows();
}

static void term_event(void *user, bool wait)
{
	struct subwindow *subwindow = user;
	assert(subwindow != NULL);

	if (!get_event() && wait) {
		while (true) {
			for (int i = 0; i < DEFAULT_IDLE_UPDATE_PERIOD; i++) {
				if (get_event()) {
					return;
				}
				SDL_Delay(subwindow->window->delay);
			}
			idle_update();
		}
	}
}

static void term_flush_events(void *user)
{
	(void) user;

	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_WINDOWEVENT) {
			handle_windowevent(&event.window);
		}
	}
}

static void term_redraw(void *user)
{
	struct subwindow *subwindow = user;

	redraw_window(subwindow->window);
}

static void term_big_map_redraw(void *user)
{
	struct subwindow *subwindow = user;

	redraw_big_map(subwindow->window);
}

static void term_delay(void *user, int msecs)
{
	(void) user;

	SDL_Delay(msecs);
}

static void term_pop_new(void *user)
{
	struct subwindow *subwindow = user;

	if (subwindow->big_map) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	}

	detach_subwindow_from_window(subwindow->window, subwindow);
	free_temporary_subwindow(subwindow);
}

static void term_cursor(void *user, int col, int row)
{
	struct subwindow *subwindow = user;
	assert(subwindow != NULL);

	render_cursor(subwindow, col, row);
}

static void term_big_map_cursor(void *user, int col, int row)
{
	struct subwindow *subwindow = user;
	assert(subwindow != NULL);

	render_big_map_cursor(subwindow, col, row);
}

static void term_push_new(const struct term_hints *hints,
		struct term_create_info *info)
{
	struct window *window = get_loaded_window(WINDOW_MAIN);
	assert(window != NULL);

	struct subwindow *subwindow = get_new_temporary_subwindow();

	subwindow->cols = hints->width;
	subwindow->rows = hints->height;

	info->callbacks = default_callbacks;

	if (hints->purpose == TERM_PURPOSE_BIG_MAP) {
		subwindow->big_map = true;
		info->callbacks.redraw = term_big_map_redraw;
		info->callbacks.cursor = term_big_map_cursor;
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	}

	assert(window->game_font != NULL);
	subwindow->font = window->game_font;

	attach_subwindow_to_window(window, subwindow);
	load_subwindow(window, subwindow);
	position_temporary_subwindow(subwindow, hints);

	info->user   = subwindow;
	info->blank  = default_blank_point;
	info->width  = subwindow->cols;
	info->height = subwindow->rows;
}

static void term_draw_text(const struct subwindow *subwindow,
		SDL_Rect rect, const struct term_point *point)
{
	SDL_Color bg;
	switch (point->terrain_attr) {
		case BG_BLACK:
			bg = subwindow->color;
			break;
		case BG_SAME:
			bg = g_colors[point->fg_attr];
			break;
		case BG_DARK:
			bg = g_colors[DEFAULT_SHADE_COLOR];
			break;
		default:
			bg = g_colors[DEFAULT_ERROR_COLOR];
			break;
	}

	bg.a = subwindow->color.a;

	render_fill_rect(subwindow->window,
			subwindow->texture, &rect, &bg);

	if (!IS_BLANK_POINT_FG(point)) {
		SDL_Color fg = g_colors[point->fg_attr];

		render_glyph_mono(subwindow->window, subwindow->font,
				rect.x, rect.y, fg, (uint32_t) point->fg_char);
	}
}

static void term_draw_tile(const struct subwindow *subwindow,
		const struct graphics *graphics,
		int col, int row, SDL_Rect rect,
		const struct term_point *point)
{
	assert(subwindow->window->graphics.texture != NULL);

	int bg_col = point->bg_char & 0x7F;
	int bg_row = point->bg_attr & 0x7F;

	int fg_col = point->fg_char & 0x7F;
	int fg_row = point->fg_attr & 0x7F;

	render_fill_rect(subwindow->window, subwindow->texture, &rect, &subwindow->color);

	if (!IS_BLANK_POINT_BG(point)) {
		render_tile(subwindow, graphics, bg_col, bg_row, col, row, rect);
	}

	if (bg_col == fg_col && bg_row == fg_row) {
		return;
	}

	if (!IS_BLANK_POINT_FG(point)) {
		render_tile(subwindow, graphics, fg_col, fg_row, col, row, rect);
	}
}

static void term_draw(void *user,
		int col, int row, int n_points, struct term_point *points)
{
	struct subwindow *subwindow = user;
	assert(subwindow != NULL);

	const struct graphics *graphics = &subwindow->window->graphics;

	SDL_Rect rect = {
		subwindow->inner_rect.x + col * subwindow->cell_width,
		subwindow->inner_rect.y + row * subwindow->cell_height,
		subwindow->cell_width,
		subwindow->cell_height
	};

	for (int i = 0; i < n_points; i++) {
		if (points[i].fg_attr & 0x80) {
			term_draw_tile(subwindow, graphics, col + i, row, rect, &points[i]);
		} else {
			term_draw_text(subwindow, rect, &points[i]);
		}

		rect.x += subwindow->cell_width;
	}
}

static SDL_Texture *load_image(const struct window *window, const char *path)
{
	SDL_Surface *surface = IMG_Load(path);
	if (surface == NULL) {
		quit_fmt("cant load image '%s': %s", path, IMG_GetError());
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
	if (texture == NULL) {
		quit_fmt("cant create texture from image '%s': %s", path, SDL_GetError());
	}
	SDL_FreeSurface(surface);

	return texture;
}

static void load_wallpaper(struct window *window, const char *path)
{
	if (window->wallpaper.mode == WALLPAPER_DONT_SHOW) {
		return;
	}
	if (window->wallpaper.mode == WALLPAPER_INVALID) {
		quit_fmt("invalid wallpaper mode in window %u", window->index);
	}

	SDL_Texture *wallpaper = load_image(window, path);
	assert(wallpaper != NULL);

	if (window->wallpaper.mode == WALLPAPER_TILED) {
		int w;
		int h;
		SDL_QueryTexture(wallpaper, NULL, NULL, &w, &h);

		SDL_Rect dst = {0, 0, w, h};

		/* make wallpaper 1/4 to 1/2 of the window's width and height
		 * that would be 4 to 16 calls to RenderCopy in wallpaper rendering function */
		while (w < window->inner_rect.w / 4) {
			w *= 2;
		}
		while (h < window->inner_rect.h / 4) {
			h *= 2;
		}
		window->wallpaper.texture = make_subwindow_texture(window, w, h);
		window->wallpaper.width = w;
		window->wallpaper.height = h;

		SDL_Color color = {0};
		render_clear(window, window->wallpaper.texture, &color);

		for (dst.y = 0; dst.y < h; dst.y += dst.h) {
			for (dst.x = 0; dst.x < w; dst.x += dst.w) {
				SDL_RenderCopy(window->renderer, wallpaper, NULL, &dst);
			}
		}
		SDL_DestroyTexture(wallpaper);
	} else {
		SDL_QueryTexture(wallpaper, NULL, NULL,
				&window->wallpaper.width, &window->wallpaper.height);
		window->wallpaper.texture = wallpaper;
	}
}

static void load_default_wallpaper(struct window *window)
{
	if (window->wallpaper.mode == WALLPAPER_DONT_SHOW) {
		return;
	}

	char path[4096];
	path_build(path, sizeof(path), DEFAULT_WALLPAPER_DIR, DEFAULT_WALLPAPER);

	load_wallpaper(window, path);
}

static void load_default_window_icon(const struct window *window)
{
	char path[4096];
	path_build(path, sizeof(path), DEFAULT_WINDOW_ICON_DIR, DEFAULT_WINDOW_ICON);

	SDL_Surface *surface = IMG_Load(path);
	assert(surface != NULL);

	SDL_SetWindowIcon(window->window, surface);

	SDL_FreeSurface(surface);
}

static void load_graphics(struct window *window, graphics_mode *mode)
{
	assert(window->graphics.texture == NULL);

	current_graphics_mode = mode;
	use_graphics = mode->grafID;

	if (use_graphics != GRAPHICS_NONE) {
		char path[4096];
		path_build(path, sizeof(path), mode->path, mode->file);
		if (!file_exists(path)) {
			quit_fmt("cant load graphcis: file '%s' doesnt exist", path);
		}

		window->graphics.texture = load_image(window, path);
		assert(window->graphics.texture != NULL);

		window->graphics.tile_pixel_w = mode->cell_width;
		window->graphics.tile_pixel_h = mode->cell_height;

		window->graphics.overdraw_row = mode->overdrawRow;
		window->graphics.overdraw_max = mode->overdrawMax;
	}

	if (character_dungeon) {
		reset_visuals(true);
	}

	window->graphics.id = mode->grafID;
}

static void reload_graphics(struct window *window, graphics_mode *mode)
{
	if (mode == NULL) {
		return;
	}

	free_graphics(&window->graphics);
	memset(&window->graphics, 0, sizeof(window->graphics));
	window->graphics.texture = NULL;
	window->graphics.id = GRAPHICS_NONE;

	struct subwindow *subwindow =
		get_subwindow_by_index(window, DISPLAY_CAVE, false);

	assert(subwindow != NULL);

	if (mode->grafID != GRAPHICS_NONE) {
		subwindow->use_graphics = true;
		load_graphics(window, mode);
	} else {
		subwindow->use_graphics = false;
	}

	if (!adjust_subwindow_geometry(window, subwindow)) {
		subwindow->full_rect.w =
			SUBWINDOW_WIDTH(get_min_cols(subwindow), subwindow->cell_width);
		subwindow->full_rect.h =
			SUBWINDOW_HEIGHT(get_min_rows(subwindow), subwindow->cell_height);

		adjust_subwindow_geometry(window, subwindow);
	}

	Term_push(subwindow->term);
	Term_resize(subwindow->cols, subwindow->rows);
	Term_flush_output();
	Term_pop();
}

static const struct font_info *find_font_info(const char *name)
{
	for (size_t i = 0; i < N_ELEMENTS(g_font_info); i++) {
		if (g_font_info[i].loaded && streq(g_font_info[i].name, name)) {
			return &g_font_info[i];
		}
	}

	return NULL;
}

static void make_font_cache(const struct window *window, struct font *font)
{
	font->cache.texture = make_subwindow_texture(window,
			(int) ASCII_CACHE_SIZE * font->ttf.glyph.w, font->ttf.glyph.h);
	assert(font->cache.texture != NULL);
		
	/* fill texture with white transparent pixels */
	SDL_Color white = {0xFF, 0xFF, 0xFF, 0};
	render_clear(window, font->cache.texture, &white);
	/* restore the alpha; we will render glyphs in white */
	white.a = 0xFF;

	/* y remains the same during the loop below; other values of rect change */
	SDL_Rect rect = {0};

	for (size_t i = 0; i < ASCII_CACHE_SIZE; i++) {
		SDL_Surface *surface = TTF_RenderGlyph_Blended(font->ttf.handle,
				(Uint16) g_ascii_codepoints_for_cache[i], white);
		if (surface == NULL) {
			quit_fmt("cant render surface for cache in font '%s': %s",
					font->name, TTF_GetError());
		}
		SDL_Texture *texture = SDL_CreateTextureFromSurface(window->renderer, surface);
		if (texture == NULL) {
			quit_fmt("cant create texture for cache in font '%s': %s",
					font->name, SDL_GetError());
		}
		SDL_FreeSurface(surface);

		SDL_QueryTexture(texture, NULL, NULL, &rect.w, &rect.h);
		rect.w = MIN(rect.w, font->ttf.glyph.w - font->ttf.glyph.x);
		rect.h = MIN(rect.h, font->ttf.glyph.h - font->ttf.glyph.y);

		SDL_RenderCopy(window->renderer, texture, NULL, &rect);

		font->cache.rects[i] = rect;
		rect.x += font->ttf.glyph.w;

		SDL_DestroyTexture(texture);
	}

}

static struct font *make_font(const struct window *window,
		const char *name, int size)
{
	const struct font_info *info = find_font_info(name);
	if (info == NULL) {
		return NULL;
	}

	struct font *font = mem_zalloc(sizeof(*font));

	font->index = info->index;
	font->path = string_make(info->path);
	font->name = string_make(info->name);
	font->size = size;

	font->cache.texture = NULL;

	load_font(font);
	make_font_cache(window, font);

	return font;
}

static bool reload_font(struct subwindow *subwindow,
		const struct font_info *info)
{
	struct font *new_font = make_font(subwindow->window, info->name, info->size);
	if (new_font == NULL) {
		return false;
	}

	subwindow->sizing_rect = subwindow->full_rect;
	if (!is_ok_col_row(subwindow,
				&subwindow->sizing_rect,
				new_font->ttf.glyph.w, new_font->ttf.glyph.h))
	{
		subwindow->sizing_rect.w =
			SUBWINDOW_WIDTH(get_min_cols(subwindow), new_font->ttf.glyph.w);
		subwindow->sizing_rect.h =
			SUBWINDOW_HEIGHT(get_min_rows(subwindow), new_font->ttf.glyph.h);
	}

	if (subwindow->sizing_rect.w > subwindow->window->inner_rect.w
			|| subwindow->sizing_rect.h > subwindow->window->inner_rect.h)
	{
		free_font(new_font);
		memset(&subwindow->sizing_rect, 0, sizeof(subwindow->sizing_rect));

		return false;
	}

	fit_rect_in_rect_by_xy(&subwindow->sizing_rect, &subwindow->window->inner_rect);

	free_font(subwindow->font);
	subwindow->font = new_font;

	resize_subwindow(subwindow);

	return true;
}

static void load_font(struct font *font)
{
	assert(font != NULL);
	assert(font->path != NULL);

	font->ttf.handle = TTF_OpenFont(font->path, font->size);
	if (font->ttf.handle == NULL) {
		quit_fmt("cant open font '%s': %s", font->path, TTF_GetError());
	}

	font->ttf.glyph.h = TTF_FontHeight(font->ttf.handle);
	font->ttf.glyph.h += 2 * GLYPH_PADDING;

	if (TTF_GlyphMetrics(font->ttf.handle, GLYPH_FOR_ADVANCE,
				NULL, NULL, NULL, NULL, &font->ttf.glyph.w) != 0)
	{
		quit_fmt("cant query glyph metrics for font '%s': %s",
				font->path, TTF_GetError());
	}
	font->ttf.glyph.w += 2 * GLYPH_PADDING;

	font->ttf.glyph.x = GLYPH_PADDING;
	font->ttf.glyph.y = GLYPH_PADDING;

	TTF_SetFontHinting(font->ttf.handle, DEFAULT_FONT_HINTING);
}

static void free_font(struct font *font)
{
	if (font->name != NULL) {
		mem_free(font->name);
	}
	if (font->path != NULL) {
		mem_free(font->path);
	}
	if (font->ttf.handle != NULL) {
		TTF_CloseFont(font->ttf.handle);
	}
	if (font->cache.texture != NULL) {
		SDL_DestroyTexture(font->cache.texture);
	}

	mem_free(font);
}

static int get_min_cols(const struct subwindow *subwindow)
{
	if (subwindow->is_temporary) {
		return MIN_COLS_TEMPORARY;
	} else {
		assert(subwindow->index < N_ELEMENTS(g_term_info));
		return g_term_info[subwindow->index].min_cols;
	}
}

static int get_min_rows(const struct subwindow *subwindow)
{
	if (subwindow->is_temporary) {
		return MIN_ROWS_TEMPORARY;
	} else {
		assert(subwindow->index < N_ELEMENTS(g_term_info));
		return g_term_info[subwindow->index].min_rows;
	}
}

static bool is_ok_col_row(const struct subwindow *subwindow,
		const SDL_Rect *rect, int cell_w, int cell_h)
{
	if (SUBWINDOW_WIDTH(get_min_cols(subwindow), cell_w) > rect->w) {
		return false;
	}
	if (SUBWINDOW_HEIGHT(get_min_rows(subwindow), cell_h) > rect->h) {
		return false;
	}

	return true;
}

static void adjust_subwindow_cave_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = SUBWINDOW_WIDTH(g_term_info[DISPLAY_CAVE].def_cols,
			subwindow->cell_width);
	rect.h = SUBWINDOW_HEIGHT(g_term_info[DISPLAY_CAVE].def_rows,
			subwindow->cell_height);

	/* center it */
	rect.x = MAX(window->inner_rect.x, 
			window->inner_rect.x + (window->inner_rect.w - rect.w) / 2);
	rect.y = MAX(window->inner_rect.y, 
			window->inner_rect.y + (window->inner_rect.h - rect.h) / 2);

	subwindow->full_rect = rect;
}

static void adjust_subwindow_messages_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = window->inner_rect.w;
	rect.h = SUBWINDOW_HEIGHT(g_term_info[subwindow->index].def_rows,
			subwindow->cell_height);

	rect.x = window->inner_rect.x;
	rect.y = window->inner_rect.y;

	subwindow->full_rect = rect;
}

static void adjust_subwindow_status_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = window->inner_rect.w;
	rect.h = SUBWINDOW_HEIGHT(g_term_info[subwindow->index].def_rows,
			subwindow->cell_height);

	rect.x = window->inner_rect.x;
	rect.y = window->inner_rect.y + window->inner_rect.h - rect.h;

	subwindow->full_rect = rect;
}

static void adjust_subwindow_compact_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = SUBWINDOW_WIDTH(g_term_info[subwindow->index].def_cols,
			subwindow->cell_width);
	rect.h = SUBWINDOW_HEIGHT(g_term_info[subwindow->index].def_rows,
			subwindow->cell_height);

	rect.x = window->inner_rect.x;
	rect.y = MAX(window->inner_rect.y, 
			window->inner_rect.y + (window->inner_rect.h - rect.h) / 2);

	subwindow->full_rect = rect;
}

static void adjust_subwindow_other_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = SUBWINDOW_WIDTH(g_term_info[subwindow->index].def_cols,
			subwindow->cell_width);
	rect.h = SUBWINDOW_HEIGHT(g_term_info[subwindow->index].def_rows,
			subwindow->cell_height);

	/* center it */
	rect.x = MAX(window->inner_rect.x,
			window->inner_rect.x + (window->inner_rect.w - rect.w) / 2);
	rect.y = MAX(window->inner_rect.y,
			window->inner_rect.y + (window->inner_rect.h - rect.h) / 2);

	subwindow->full_rect = rect;
}

static void adjust_subwindow_temporary_default(const struct window *window,
		struct subwindow *subwindow)
{
	SDL_Rect rect = {0};

	rect.w = SUBWINDOW_WIDTH(subwindow->cols, subwindow->cell_width);
	rect.h = SUBWINDOW_HEIGHT(subwindow->rows, subwindow->cell_height);

	rect.x = window->inner_rect.x;
	rect.y = window->inner_rect.y;

	subwindow->full_rect = rect;
}

static void adjust_subwindow_geometry_default(const struct window *window,
		struct subwindow *subwindow)
{
	switch (subwindow->index) {
		case DISPLAY_CAVE:
			adjust_subwindow_cave_default(window, subwindow);
			break;
		case DISPLAY_MESSAGE_LINE:
			adjust_subwindow_messages_default(window, subwindow);
			break;
		case DISPLAY_STATUS_LINE:
			adjust_subwindow_status_default(window, subwindow);
			break;
		case DISPLAY_PLAYER_COMPACT:
			adjust_subwindow_compact_default(window, subwindow);
			break;
		default:
			if (subwindow->is_temporary) {
				adjust_subwindow_temporary_default(window, subwindow);
			} else {
				adjust_subwindow_other_default(window, subwindow);
			}
			break;
	}
}

static bool adjust_subwindow_geometry(const struct window *window,
		struct subwindow *subwindow)
{
	if (subwindow->use_graphics) {
		if (subwindow->big_map) {
			subwindow->cell_width = REASONABLE_MAP_TILE_WIDTH;
			subwindow->cell_height = REASONABLE_MAP_TILE_HEIGHT;
		} else {
			subwindow->cell_width = window->graphics.tile_pixel_w;
			subwindow->cell_height = window->graphics.tile_pixel_h;
		}
	} else {
		subwindow->cell_width = subwindow->font->ttf.glyph.w;
		subwindow->cell_height = subwindow->font->ttf.glyph.h;
	}
	
	if (!subwindow->loaded && subwindow->config == NULL) {
		/* brand new subwindow */
		adjust_subwindow_geometry_default(window, subwindow);
	}

	/* coordinates of inner rect are relative to that of outer rect
	 * (really, they are relative to subwindow's texture */
	subwindow->inner_rect.x = 0;
	subwindow->inner_rect.y = 0;
	subwindow->inner_rect.w = subwindow->full_rect.w;
	subwindow->inner_rect.h = subwindow->full_rect.h;

	memset(&subwindow->sizing_rect, 0, sizeof(subwindow->sizing_rect));

	resize_rect(&subwindow->inner_rect,
			DEFAULT_BORDER, DEFAULT_BORDER,
			-DEFAULT_BORDER, -DEFAULT_BORDER);

	subwindow->borders.width = DEFAULT_VISIBLE_BORDER;

	subwindow->cols = subwindow->inner_rect.w / subwindow->cell_width;
	subwindow->rows = subwindow->inner_rect.h / subwindow->cell_height;

	assert(subwindow->cols > 0);
	assert(subwindow->rows > 0);

	subwindow->inner_rect.w = subwindow->cols * subwindow->cell_width;
	subwindow->inner_rect.h = subwindow->rows * subwindow->cell_height;

	subwindow->inner_rect.x = 
		(subwindow->full_rect.w - subwindow->inner_rect.w) / 2;
	subwindow->inner_rect.y =
		(subwindow->full_rect.h - subwindow->inner_rect.h) / 2;

	if (!is_ok_col_row(subwindow, &subwindow->full_rect,
				subwindow->cell_width, subwindow->cell_height))
	{
		return false;
	}

	if (!is_rect_in_rect(&subwindow->full_rect, &window->inner_rect)
			&& !subwindow->big_map)
	{
		/* big map takes whole window;
		 * other subwindows are not allowed to to that */
		subwindow->borders.error = true;
	}

	return true;
}

static void position_subwindow_exact(struct subwindow *subwindow,
		const struct term_hints *hints)
{
	const struct window *window = subwindow->window;

	const struct subwindow *prev_top;
	if (window->temporary.number > 1) {
		/* we need previous top term here (the current top is this subwindow) */
		prev_top = window->temporary.subwindows[window->temporary.number - 1 - 1];
	} else {
		/* no temporary terms (except this subwindow); use main map then */
		prev_top = get_subwindow_direct(DISPLAY_CAVE);
	}

	assert(prev_top->window == subwindow->window);

	subwindow->full_rect.x = prev_top->full_rect.x +
		hints->x * prev_top->cell_width;

	subwindow->full_rect.y = prev_top->full_rect.y +
		hints->y * prev_top->cell_height;
}

static void position_subwindow_center(struct subwindow *subwindow,
		const struct term_hints *hints)
{
	(void) hints;

	const struct subwindow *cave = get_subwindow_direct(DISPLAY_CAVE);
	assert(cave->window == subwindow->window);

	subwindow->full_rect.x = MAX(cave->full_rect.x,
			cave->full_rect.x + (cave->full_rect.w - subwindow->full_rect.w) / 2);

	subwindow->full_rect.y = MAX(cave->full_rect.y,
			cave->full_rect.y + (cave->full_rect.h - subwindow->full_rect.h) / 2);
}

static void position_subwindow_top_center(struct subwindow *subwindow,
		const struct term_hints *hints)
{
	(void) hints;

	const struct subwindow *message_line = get_subwindow_direct(DISPLAY_MESSAGE_LINE);
	assert(message_line->window == subwindow->window);

	/* center in in the window horizontally */
	subwindow->full_rect.x = MAX(subwindow->full_rect.x,
			subwindow->window->inner_rect.x +
			(subwindow->window->inner_rect.w - subwindow->full_rect.w) / 2);

	/* put it under message line and 'snap' to it */
	subwindow->full_rect.y =
		message_line->full_rect.y + message_line->full_rect.h - DEFAULT_VISIBLE_BORDER;

	if (!is_rect_in_rect(&subwindow->full_rect, &subwindow->window->inner_rect)) {
		/* center it then */
		position_subwindow_center(subwindow, hints);
	}
}

static void position_subwindow_big_map(struct subwindow *subwindow)
{
	subwindow->sizing_rect = subwindow->full_rect;

	fit_rect_in_rect_proportional(&subwindow->sizing_rect,
			&subwindow->window->full_rect);

	subwindow->sizing_rect.x =
		(subwindow->window->full_rect.w - subwindow->sizing_rect.w) / 2;
	subwindow->sizing_rect.y =
		(subwindow->window->full_rect.h - subwindow->sizing_rect.h) / 2;
}

static void position_other_subwindow(struct subwindow *subwindow,
		const struct term_hints *hints)
{
	switch (hints->position) {
		case TERM_POSITION_EXACT:
			position_subwindow_exact(subwindow, hints);
			break;
		case TERM_POSITION_CENTER:
			position_subwindow_center(subwindow, hints);
			break;
		case TERM_POSITION_TOP_CENTER:
			position_subwindow_top_center(subwindow, hints);
			break;
		default:
			position_subwindow_center(subwindow, hints);
			break;
	}
}

static void position_temporary_subwindow(struct subwindow *subwindow,
		const struct term_hints *hints)
{
	if (subwindow->big_map) {
		position_subwindow_big_map(subwindow);
	} else {
		position_other_subwindow(subwindow, hints);
	}
}

static void sort_to_top_aux(struct window *window,
		size_t *next, struct subwindow **subwindows, bool is_top, bool always_top)
{
	for (size_t i = 0; i < window->permanent.number; i++) {
		if (window->permanent.subwindows[i]->is_top == is_top &&
				window->permanent.subwindows[i]->always_top == always_top)
		{
			subwindows[(*next)++] = window->permanent.subwindows[i];
		}
	}
}

static void sort_to_top(struct window *window)
{
	struct subwindow *tmp[N_ELEMENTS(window->permanent.subwindows)] = {NULL};
	assert(sizeof(window->permanent.subwindows) == sizeof(tmp));

	size_t current = 0;
	/* and that's how we sort here :) */
	sort_to_top_aux(window, &current, tmp, false, false);
	sort_to_top_aux(window, &current, tmp, true,  false);
	sort_to_top_aux(window, &current, tmp, false, true);
	sort_to_top_aux(window, &current, tmp, true,  true);

	assert(current == window->permanent.number);

	memcpy(window->permanent.subwindows, tmp, sizeof(window->permanent.subwindows));
}

static void bring_to_top(struct window *window, struct subwindow *subwindow)
{
	assert(subwindow->window == window);

	bool found_subwindow_in_window = false;
	for (size_t i = 0; i < window->permanent.number; i++) {
		window->permanent.subwindows[i]->is_top = false;
		if (window->permanent.subwindows[i] == subwindow) {
			found_subwindow_in_window = true;
		}
	}

	assert(found_subwindow_in_window);

	subwindow->is_top = true;

	sort_to_top(window);
}

static void adjust_status_bar_geometry(struct window *window)
{
	struct status_bar *status_bar = &window->status_bar;

	status_bar->full_rect.x = 0;
	status_bar->full_rect.y = 0;
	status_bar->full_rect.w = window->full_rect.w;
	status_bar->full_rect.h = DEFAULT_LINE_HEIGHT(status_bar->font->ttf.glyph.h);
	status_bar->inner_rect = status_bar->full_rect;

	int border = (status_bar->full_rect.h - status_bar->font->ttf.glyph.h) / 2;
	resize_rect(&status_bar->inner_rect,
			border, border, -border, -border);
}

static struct subwindow *get_subwindow_by_index(const struct window *window,
		unsigned index, bool visible)
{
	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];

		if ((visible ? subwindow->visible : true) && subwindow->index == index) {
			return subwindow;
		}
	}
	
	return NULL;
}

static struct subwindow *get_subwindow_by_xy(const struct window *window,
		int x, int y, bool temporary)
{
	/* checking subwindows in z order */

	for (size_t i = window->temporary.number; i > 0; i--) {
		struct subwindow *subwindow = window->temporary.subwindows[i - 1];

		if (is_point_in_rect(x, y, &subwindow->full_rect)) {
			/* temporary subwindows are always over permanent ones */
			if (temporary) {
				return subwindow;
			} else {
				return NULL;
			}
		}
	}

	for (size_t i = window->permanent.number; i > 0; i--) {
		struct subwindow *subwindow = window->permanent.subwindows[i - 1];

		if (subwindow->visible) {
			if (is_point_in_rect(x, y, &subwindow->full_rect)) {
				return subwindow;
			}
		}
	}

	return NULL;
}

static struct menu_panel *get_menu_panel_by_xy(struct menu_panel *menu_panel,
		int x, int y)
{
	while (menu_panel != NULL) {
		if (is_point_in_rect(x, y, &menu_panel->rect)) {
			return menu_panel;
		}
		menu_panel = menu_panel->next;
	}

	return NULL;
}

static bool is_over_status_bar(const struct status_bar *status_bar, int x, int y)
{
	return is_point_in_rect(x, y, &status_bar->full_rect);
}

static void make_button_bank(struct button_bank *bank)
{
	bank->buttons = mem_zalloc(sizeof(*bank->buttons) * MAX_BUTTONS);

	bank->size = MAX_BUTTONS;
	bank->number = 0;
}

static bool do_button_open_subwindow(struct window *window,
		struct button *button)
{
	CHECK_BUTTON_GROUP_TYPE(button, BUTTON_GROUP_SUBWINDOWS, BUTTON_DATA_UVAL);

	unsigned index = button->info.data.uval;
	struct subwindow *subwindow = NULL;
	
	subwindow = get_subwindow_by_index(window, index, false);
	if (subwindow != NULL) {
		subwindow->visible = !subwindow->visible;
		if (subwindow->visible) {
			bring_to_top(window, subwindow);
		}
	} else if (is_subwindow_loaded(index)) {
		subwindow = transfer_subwindow(window, index);
		subwindow->visible = true;
		bring_to_top(window, subwindow);
	} else {
		subwindow = make_subwindow(window, index);
		assert(subwindow != NULL);
		bring_to_top(window, subwindow);
	}

	redraw_all_windows();

	return true;
}

static void close_status_bar_menu(struct status_bar *status_bar)
{
	if (status_bar->menu_panel != NULL) {
		free_menu_panel(status_bar->menu_panel);
		status_bar->menu_panel = NULL;
	}
}

static void make_default_status_buttons(struct status_bar *status_bar)
{
	SDL_Rect rect;
	struct button_info info;
	struct button_callbacks callbacks;

#define PUSH_BUTTON_LEFT_TO_RIGHT(cap) \
	get_string_metrics(status_bar->font, (cap), &rect.w, NULL); \
	rect.w += DEFAULT_BUTTON_BORDER * 2; \
	push_button(&status_bar->button_bank, status_bar->font, \
			(cap), info, callbacks, &rect, CAPTION_POSITION_CENTER); \
	rect.x += rect.w; \

	rect.x = status_bar->full_rect.x;
	rect.y = status_bar->full_rect.y;
	rect.w = 0;
	rect.h = status_bar->full_rect.h;

	callbacks.on_render = render_menu_button;
	callbacks.on_event = handle_menu_button;
	callbacks.on_menu = NULL;

	info.type = BUTTON_DATA_NONE;
	info.group = BUTTON_GROUP_MENU;
	PUSH_BUTTON_LEFT_TO_RIGHT("Menu");

	callbacks.on_render = render_button_subwindows;
	callbacks.on_event = NULL;

	info.type = BUTTON_DATA_UVAL;
	info.group = BUTTON_GROUP_SUBWINDOWS;

	callbacks.on_click = do_button_open_subwindow;

	for (unsigned i = 0, label = 1; i < N_ELEMENTS(g_term_info); i++) {
		/* We display optional subwindows here */
		if (!g_term_info[i].required) {
			info.data.uval = g_term_info[i].index;
			PUSH_BUTTON_LEFT_TO_RIGHT(format("%X", label));
			label++;
		}
	}
#undef PUSH_BUTTON_LEFT_TO_RIGHT

#define PUSH_BUTTON_RIGHT_TO_LEFT(cap) \
	get_string_metrics(status_bar->font, (cap), &rect.w, NULL); \
	rect.w += DEFAULT_BUTTON_BORDER * 2; \
	rect.x -= rect.w; \
	push_button(&status_bar->button_bank, status_bar->font, \
			(cap), info, callbacks, &rect, CAPTION_POSITION_CENTER); \

	rect.x = status_bar->full_rect.x + status_bar->full_rect.w;
	rect.y = status_bar->full_rect.y;
	rect.w = 0;
	rect.h = status_bar->full_rect.h;

	callbacks.on_render = render_button_movesize;
	callbacks.on_click = do_button_movesize;
	callbacks.on_event = NULL;
	callbacks.on_menu = NULL;

	info.type = BUTTON_DATA_IVAL;
	info.group = BUTTON_GROUP_MOVESIZE;

	info.data.ival = BUTTON_MOVESIZE_MOVING;
	PUSH_BUTTON_RIGHT_TO_LEFT("Move");

	info.data.ival = BUTTON_MOVESIZE_SIZING;
	PUSH_BUTTON_RIGHT_TO_LEFT("Size");
#undef PUSH_BUTTON_RIGHT_TO_LEFT
}

static void reload_status_bar(struct status_bar *status_bar)
{
	close_status_bar_menu(status_bar);

	SDL_DestroyTexture(status_bar->texture);
	status_bar->texture = make_subwindow_texture(status_bar->window,
			status_bar->full_rect.w, status_bar->full_rect.h);
	assert(status_bar->texture != NULL);

	free_button_bank(&status_bar->button_bank);
	make_button_bank(&status_bar->button_bank);
	make_default_status_buttons(status_bar);

	render_status_bar(status_bar->window);
}

static void load_status_bar(struct window *window)
{
	if (window->status_bar.font == NULL) {
		if (window->config != NULL) {
			window->status_bar.font = make_font(window,
					window->config->system_font_name,
					window->config->system_font_size);
		} else {
			window->status_bar.font = make_font(window, DEFAULT_SYSTEM_FONT, 0);
		}
		assert(window->status_bar.font != NULL);
	} else {
		quit_fmt("font '%s' already loaded in status bar in window %u",
				window->status_bar.font->name, window->index);
	}

	adjust_status_bar_geometry(window);

	window->status_bar.texture = make_subwindow_texture(window,
			window->status_bar.full_rect.w, window->status_bar.full_rect.h);

	/* let's try renderer */
	if (SDL_SetRenderDrawColor(window->renderer,
				window->status_bar.color.r, window->status_bar.color.g,
				window->status_bar.color.b, window->status_bar.color.a) != 0) {
		quit_fmt("cant set render color for status bar in window %u: %s",
				window->index, SDL_GetError());
	}
	/* well, renderer seems to work */
	if (SDL_SetRenderTarget(window->renderer, window->status_bar.texture) != 0) {
		quit_fmt("cant set status bar texture as target in window %u: %s",
				window->index, SDL_GetError());
	}
	/* does it render? */
	if (SDL_RenderClear(window->renderer) != 0) {
		quit_fmt("cant clear status bar texture in window %u: %s",
				window->index, SDL_GetError());
	}

	/* well, it probably works */
	window->status_bar.window = window;
}

static void fit_subwindow_in_window(const struct window *window,
		struct subwindow *subwindow)
{
	fit_rect_in_rect_by_xy(&subwindow->full_rect, &window->inner_rect);
	if (!is_rect_in_rect(&subwindow->full_rect, &window->inner_rect)) {
		subwindow->borders.error = true;
		render_borders(subwindow);
	}
}

static void resize_window(struct window *window, int w, int h)
{
	if (window->full_rect.w == w
			&& window->full_rect.h == h)
	{
		return;
	}

	window->full_rect.w = w;
	window->full_rect.h = h;

	adjust_status_bar_geometry(window);
	adjust_window_geometry(window);
	
	clear_all_borders(window);
	for (size_t i = 0; i < window->permanent.number; i++) {
		fit_subwindow_in_window(window, window->permanent.subwindows[i]);
	}
	for (size_t i = 0; i < window->temporary.number; i++) {
		fit_subwindow_in_window(window, window->temporary.subwindows[i]);
	}

	reload_status_bar(&window->status_bar);

	redraw_window(window);
}

static void adjust_window_geometry(struct window *window)
{
	window->inner_rect.x = 0;
	window->inner_rect.y = 0;
	window->inner_rect.w = window->full_rect.w;
	window->inner_rect.h = window->full_rect.h;

	resize_rect(&window->inner_rect,
			0, window->status_bar.full_rect.h, 0, 0);

	if (window->inner_rect.w <= 0 || window->inner_rect.h <= 0) {
		quit_fmt("window %u is too small (%dx%d)",
				window->index, window->inner_rect.w, window->inner_rect.h);
	}
}

static void set_window_delay(struct window *window)
{
	assert(window->window != NULL);

	int display = SDL_GetWindowDisplayIndex(window->window);
	if (display < 0) {
		quit_fmt("cant get display of window %u: %s",
				window->index, SDL_GetError());
	}

	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(display, &mode) != 0)
	{
		/* lets just guess; 60 fps is standard */
		mode.refresh_rate = 60;
	}
	/* delay in milliseconds; refresh rate in hz */
	window->delay = 1000 / mode.refresh_rate;
}

/* initialize miscellaneous things in window */
static void load_window(struct window *window)
{
	load_status_bar(window);
	adjust_window_geometry(window);
	make_button_bank(&window->status_bar.button_bank);
	make_default_status_buttons(&window->status_bar);
	set_window_delay(window);

	if (window->wallpaper.mode != WALLPAPER_DONT_SHOW) {
		if (window->config == NULL) {
			load_default_wallpaper(window);
		} else {
			load_wallpaper(window, window->config->wallpaper_path);
		}
	}

	load_default_window_icon(window);

	if (window->graphics.id != GRAPHICS_NONE) {
		load_graphics(window, get_graphics_mode(window->graphics.id));
	}

	if (window->game_font == NULL) {
		if (window->config != NULL) {
			window->game_font = make_font(window,
					window->config->game_font_name,
					window->config->game_font_size);
		} else {
			window->game_font = make_font(window, DEFAULT_GAME_FONT, 0);
		}
	}

	render_clear(window, NULL, &window->color);
	render_status_bar(window);

	window->loaded = true;
}

static bool choose_pixelformat(struct window *window,
		const struct SDL_RendererInfo *info)
{
#define PIXELFORMAT_CASE(format) \
	case SDL_PIXELFORMAT_ ##format: \
		window->pixelformat = SDL_PIXELFORMAT_ ##format;

	for (size_t i = 0; i < info->num_texture_formats; i++) {
		switch (info->texture_formats[i]) {
			PIXELFORMAT_CASE(ARGB8888); return true;
			PIXELFORMAT_CASE(RGBA8888); return true;
			PIXELFORMAT_CASE(ABGR8888); return true;
			PIXELFORMAT_CASE(BGRA8888); return true;
		}
	}

#undef PIXELFORMAT_CASE

	return false;
}

static void start_window(struct window *window)
{
	assert(!window->loaded);

	if (window->config == NULL) {
		window->window = SDL_CreateWindow(VERSION_NAME,
				window->full_rect.x, window->full_rect.y,
				window->full_rect.w, window->full_rect.h,
				SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE);
	} else {
		window->window = SDL_CreateWindow(VERSION_NAME,
				window->full_rect.x, window->full_rect.y,
				window->full_rect.w, window->full_rect.h,
				window->config->window_flags);
	}
	assert(window->window != NULL);

	if (window->config == NULL) {
		window->renderer = SDL_CreateRenderer(window->window,
				-1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	} else {
		/* this is necessary for subwindows to have their own textures */
		window->config->renderer_flags |= SDL_RENDERER_TARGETTEXTURE;
		window->renderer = SDL_CreateRenderer(window->window,
				-1, window->config->renderer_flags);
	}
	if (window->renderer == NULL) {
		quit_fmt("cant create renderer for window %u: %s",
				window->index, SDL_GetError());
	}

	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(window->renderer, &info) != 0) {
		quit_fmt("cant query renderer in window %u", window->index);
	}

	if (!choose_pixelformat(window, &info)) {
		quit_fmt("cant choose pixelformat for window %u", window->index);
	}

	load_window(window);

	for (size_t i = 0; i < window->permanent.number; i++) {
		load_subwindow(window, window->permanent.subwindows[i]);
		window->permanent.subwindows[i]->visible = true;
	}

	SDL_SetWindowMinimumSize(window->window,
			DEFAULT_WINDOW_MINIMUM_W, DEFAULT_WINDOW_MINIMUM_H);

	window->flags = SDL_GetWindowFlags(window->window);
	window->id = SDL_GetWindowID(window->window);
}

static void wipe_window_aux_config(struct window *window)
{
	assert(window->config == NULL);
	window->config = mem_zalloc(sizeof(*window->config));
	assert(window->config != NULL);

	const struct window *main_window = get_loaded_window(WINDOW_MAIN);
	assert(main_window != NULL);

	SDL_RendererInfo rinfo;
	if (SDL_GetRendererInfo(main_window->renderer, &rinfo) != 0) {
		quit_fmt("cant get renderer info for main window: %s", SDL_GetError());
	}
	window->config->renderer_flags = rinfo.flags;
	window->config->renderer_index = -1;

	window->config->window_flags= SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

	if (main_window->config == NULL) {
		char path[4096];
		path_build(path, sizeof(path), DEFAULT_WALLPAPER_DIR, DEFAULT_WALLPAPER);
		window->config->wallpaper_path = string_make(path);
		window->config->system_font_name = string_make(DEFAULT_SYSTEM_FONT);
		window->config->game_font_name = string_make(DEFAULT_GAME_FONT);
	} else {
		window->config->wallpaper_path = string_make(main_window->config->wallpaper_path);
		window->config->system_font_name = string_make(main_window->config->system_font_name);
		window->config->system_font_size = main_window->config->system_font_size;
		window->config->game_font_name = string_make(main_window->config->game_font_name);
		window->config->game_font_size = main_window->config->game_font_size;
	}

	int display = SDL_GetWindowDisplayIndex(main_window->window);
	if (display < 0) {
		quit_fmt("cant get display from main window: %s", SDL_GetError());
	}

	/* center it on the screen */
	SDL_DisplayMode mode;
	SDL_GetCurrentDisplayMode(display, &mode);
	window->full_rect.w = mode.w / 2;
	window->full_rect.h = mode.h / 2;
	window->full_rect.x = mode.w / 4;
	window->full_rect.y = mode.h / 4;
}

/* initialize window with suitable hardcoded defaults */
static void wipe_window(struct window *window, int display)
{
	{
		const unsigned index = window->index;

		memset(window, 0, sizeof(*window));

		window->index = index;
	}

	for (size_t i = 0; i < N_ELEMENTS(window->permanent.subwindows); i++) {
		window->permanent.subwindows[i] = NULL;
	}
	for (size_t i = 0; i < N_ELEMENTS(window->temporary.subwindows); i++) {
		window->temporary.subwindows[i] = NULL;
	}

	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(display, &mode) != 0) {
		quit_fmt("cant get display mode for window %u: %s",
				window->index, SDL_GetError());
	}

	window->pixelformat = SDL_PIXELFORMAT_UNKNOWN;

	window->full_rect.w = mode.w;
	window->full_rect.h = mode.h;

	window->color = g_colors[DEFAULT_WINDOW_BG_COLOR];
	window->alpha = DEFAULT_ALPHA_FULL;

	window->status_bar.font = NULL;
	window->game_font = NULL;

	window->wallpaper.texture = NULL;
	window->wallpaper.mode = WALLPAPER_TILED;

	window->status_bar.font = NULL;
	window->status_bar.color = g_colors[DEFAULT_STATUS_BAR_BG_COLOR];
	window->status_bar.button_bank.buttons = NULL;
	window->status_bar.menu_panel = NULL;

	window->graphics.texture = NULL;
	window->graphics.id = GRAPHICS_NONE;

	window->config = NULL;
	window->inited = true;
}

static void dump_subwindow(const struct subwindow *subwindow, ang_file *config)
{
#define DUMP_SUBWINDOW(sym, fmt, ...) \
	file_putf(config, "subwindow-" sym ":%u:" fmt "\n", subwindow->index, __VA_ARGS__)
	DUMP_SUBWINDOW("window", "%u", subwindow->window->index);
	DUMP_SUBWINDOW("full-rect", "%d:%d:%d:%d",
			subwindow->full_rect.x, subwindow->full_rect.y,
			subwindow->full_rect.w, subwindow->full_rect.h);
	DUMP_SUBWINDOW("font", "%d:%s",
			subwindow->font->size, subwindow->font->name);
	DUMP_SUBWINDOW("borders", "%s",
			subwindow->borders.visible ? "true" : "false");
	DUMP_SUBWINDOW("top", "%s:%s",
			subwindow->is_top ? "true" : "false",
			subwindow->always_top ? "true" : "false");
	DUMP_SUBWINDOW("alpha", "%u", subwindow->color.a);
	DUMP_SUBWINDOW("graphics", "%s",
			subwindow->use_graphics ? "true" : "false");
#undef DUMP_SUBWINDOW
	file_put(config, "\n");
}

static void dump_window(const struct window *window, ang_file *config)
{
#define DUMP_WINDOW(sym, fmt, ...) \
	file_putf(config, "window-" sym ":%u:" fmt "\n", window->index, __VA_ARGS__)
	DUMP_WINDOW("display", "%d", SDL_GetWindowDisplayIndex(window->window));

	int x;
	int y;
	SDL_GetWindowPosition(window->window, &x, &y);
	DUMP_WINDOW("full-rect", "%d:%d:%d:%d",
			x, y, window->full_rect.w, window->full_rect.h);

	DUMP_WINDOW("fullscreen", "%s",
			(window->flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? "true" : "false");

	SDL_RendererInfo rinfo;
	SDL_GetRendererInfo(window->renderer, &rinfo);
	DUMP_WINDOW("renderer", "%s",
			(rinfo.flags & SDL_RENDERER_ACCELERATED) ? "hardware" : "software");

	if (window->config) {
		DUMP_WINDOW("wallpaper-path", "%s", window->config->wallpaper_path);
	} else {
		DUMP_WINDOW("wallpaper-path", "%s", "default");
	}
	DUMP_WINDOW("wallpaper-mode", "%s",
			window->wallpaper.mode == WALLPAPER_DONT_SHOW ? "none"     :
			window->wallpaper.mode == WALLPAPER_TILED     ? "tiled"    :
			window->wallpaper.mode == WALLPAPER_CENTERED  ? "centered" :
			window->wallpaper.mode == WALLPAPER_SCALED    ? "scaled"   :
			"ERROR");
	DUMP_WINDOW("system-font", "%d:%s",
			window->status_bar.font->size, window->status_bar.font->name);
	DUMP_WINDOW("game-font", "%d:%s",
			window->game_font->size, window->game_font->name);

	DUMP_WINDOW("graphics-id", "%d", window->graphics.id);
#undef DUMP_WINDOW
	file_put(config, "\n");

	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];
		if (subwindow->visible) {
			dump_subwindow(subwindow, config);
		}
	}
}

static void detach_subwindow_from_window(struct window *window,
		struct subwindow *subwindow)
{
	assert(subwindow->window == window);

	if (subwindow->is_temporary) {
		size_t i = window->temporary.number;

		assert(i > 0);
		assert(window->temporary.subwindows[i - 1] == subwindow);

		window->temporary.subwindows[i - 1] = NULL;
		window->temporary.number--;
	} else {
		size_t i = 0;
		while (i < N_ELEMENTS(window->permanent.subwindows)
				&& window->permanent.subwindows[i] != subwindow)
		{
			i++;
		}

		assert(i < N_ELEMENTS(window->permanent.subwindows));

		window->permanent.subwindows[i] = NULL;

		for (i++; i < N_ELEMENTS(window->permanent.subwindows); i++) {
			window->permanent.subwindows[i - 1] = window->permanent.subwindows[i];
		}
	}

	subwindow->window = NULL;
}

static void attach_subwindow_to_window(struct window *window,
		struct subwindow *subwindow)
{
	if (subwindow->is_temporary) {
		assert(window->temporary.number < N_ELEMENTS(window->temporary.subwindows));
		assert(window->temporary.subwindows[window->temporary.number] == NULL);

		window->temporary.subwindows[window->temporary.number] = subwindow;
		window->temporary.number++;
	} else {
		assert(window->permanent.number < N_ELEMENTS(window->permanent.subwindows));
		assert(window->permanent.subwindows[window->permanent.number] == NULL);

		window->permanent.subwindows[window->permanent.number] = subwindow;
		window->permanent.number++;
	}

	subwindow->window = window;
}

static struct subwindow *make_subwindow(struct window *window, unsigned index)
{
	struct subwindow *subwindow = get_new_subwindow(index);
	assert(subwindow != NULL);

	attach_subwindow_to_window(window, subwindow);
	load_subwindow(window, subwindow);
	load_term(subwindow);

	return subwindow;
}

static struct subwindow *transfer_subwindow(struct window *window, unsigned index)
{
	struct subwindow *subwindow = get_subwindow_direct(index);
	assert(subwindow != NULL);
	assert(subwindow->inited);
	assert(subwindow->loaded);

	detach_subwindow_from_window(subwindow->window, subwindow);
	attach_subwindow_to_window(window, subwindow);

	SDL_DestroyTexture(subwindow->texture);
	subwindow->texture = make_subwindow_texture(window,
			subwindow->full_rect.w, subwindow->full_rect.h);
	assert(subwindow->texture != NULL);

	SDL_DestroyTexture(subwindow->aux_texture);
	subwindow->aux_texture = make_subwindow_texture(window, 1, 1);
	assert(subwindow->aux_texture != NULL);

	struct font *new_font = make_font(subwindow->window,
			subwindow->font->name, subwindow->font->size);
	assert(new_font != NULL);
	free_font(subwindow->font);
	subwindow->font = new_font;

	render_clear(window, subwindow->texture, &subwindow->color);

	subwindow->borders.error = false;
	render_borders(subwindow);

	fit_subwindow_in_window(window, subwindow);

	return subwindow;
}

static void load_subwindow(struct window *window,
		struct subwindow *subwindow)
{
	assert(window->loaded);
	assert(subwindow->inited);
	assert(!subwindow->loaded);

	if (subwindow->font == NULL) {
		if (subwindow->config != NULL) {
			subwindow->font = make_font(window,
					subwindow->config->font_name, subwindow->config->font_size);
		} else {
			subwindow->font = make_font(window, DEFAULT_FONT, 0);
		}
		assert(subwindow->font != NULL);
	}

	if (!adjust_subwindow_geometry(window, subwindow)) {
		quit_fmt("cant adjust geometry of subwindow %u in window %u",
				subwindow->index, window->index);
	}

	subwindow->texture = make_subwindow_texture(window,
			subwindow->full_rect.w, subwindow->full_rect.h);
	assert(subwindow->texture != NULL);

	/* just a pixel for sizing rect; temporary subwindows shouldn't be resized */
	if (!subwindow->is_temporary) {
		subwindow->aux_texture = make_subwindow_texture(window, 1, 1);
		assert(subwindow->aux_texture != NULL);
	}

	/* same testing sequence as for status bar */
	if (SDL_SetRenderDrawColor(window->renderer,
				subwindow->color.r, subwindow->color.g,
				subwindow->color.b, subwindow->color.a) != 0)
	{
		quit_fmt("cant set draw color for subwindow %u window %u: %s",
				subwindow->index, window->index, SDL_GetError());
	}
	if (SDL_SetRenderTarget(window->renderer, subwindow->texture) != 0) {
		quit_fmt("cant set subwindow %u as render target in window %u: %s",
				subwindow->index, window->index, SDL_GetError());
	}
	if (SDL_RenderClear(window->renderer) != 0) {
		quit_fmt("cant clear texture in subwindow %u window %u: %s",
				subwindow->index, window->index, SDL_GetError());
	}

	subwindow->loaded = true;

	render_borders(subwindow);
}

static void load_term(struct subwindow *subwindow)
{
	assert(!subwindow->linked);

	struct term_create_info info = {
		.width     = subwindow->cols,
		.height    = subwindow->rows,
		.user      = subwindow,
		.callbacks = default_callbacks,
		.blank     = default_blank_point
	};

	term term = Term_create(&info);

	subwindow->term = term;
	display_term_init(subwindow->index, term);

	subwindow->linked = true;
}

/* initialize subwindow with suitable hardcoded defaults */
static void wipe_subwindow(struct subwindow *subwindow)
{
	{
		const unsigned index = subwindow->index;
		const bool is_temporary = subwindow->is_temporary;

		memset(subwindow, 0, sizeof(*subwindow));

		subwindow->index = index;
		subwindow->is_temporary = is_temporary;
	}

	subwindow->color = g_colors[DEFAULT_SUBWINDOW_BG_COLOR];
	subwindow->borders.color = g_colors[DEFAULT_SUBWINDOW_BORDER_COLOR];
	subwindow->borders.visible = true;

	subwindow->texture     = NULL;
	subwindow->aux_texture = NULL;
	subwindow->window      = NULL;
	subwindow->font        = NULL;
	subwindow->term        = NULL;
	subwindow->config      = NULL;

	subwindow->inited = true;
	subwindow->visible = true;
}

static void get_string_metrics(struct font *font, const char *str, int *w, int *h)
{
	assert(font != NULL);
	assert(font->ttf.handle != NULL);

	if (TTF_SizeUTF8(font->ttf.handle, str, w, h) != 0) {
		quit_fmt("cant get string metrics for string '%s': %s", str, TTF_GetError());
	}
}

static int sort_cb_font_info(const void *infoa, const void *infob)
{
	int typea = ((struct font_info *) infoa)->type;
	int typeb = ((struct font_info *) infob)->type;
	
	const char *namea = ((struct font_info *) infoa)->name;
	const char *nameb = ((struct font_info *) infob)->name;

	if (typea != typeb) {
		/* raster (angband's .fon) fonts go last */
		return typea == FONT_TYPE_RASTER ? 1 : -1;
	} else if (typea == FONT_TYPE_VECTOR && typeb == FONT_TYPE_VECTOR) {
		/* vector (.ttf, etc) fonts go in alphabetical order */
		return strcmp(namea, nameb);
	} else {
		/* otherwise, we'll sort them numbers-wise (6x12x.fon before 6x13x.fon) */
		int wa = 0;
		int ha = 0;
		char facea[4 + 1] = {0};

		int wb = 0;
		int hb = 0;
		char faceb[4 + 1] = {0};

		sscanf(namea, "%dx%d%4[^.]", &wa, &ha, facea);
		sscanf(nameb, "%dx%d%4[^.]", &wb, &hb, faceb);

		if (wa < wb) {
			return -1;
		} else if (wa > wb) {
			return 1;
		} else if (ha < hb) {
			return -1;
		} else if (ha > hb) {
			return 1;
		} else {
			return strcmp(facea, faceb);
		}
	}
}

static bool is_font_file(const char *path)
{
	bool is_font = false;

	TTF_Font *font = TTF_OpenFont(path, 1);

	if (font != NULL) {
		if (TTF_FontFaceIsFixedWidth(font)) {
			is_font = true;
		}
		TTF_CloseFont(font);
	}

	return is_font;
}

static void free_menu_panel(struct menu_panel *menu_panel)
{
	while (menu_panel) {
		struct menu_panel *next = menu_panel->next;
		free_button_bank(&menu_panel->button_bank);
		mem_free(menu_panel);
		menu_panel = next;
	}
}

static void free_button_bank(struct button_bank *button_bank)
{
	for (size_t i = 0; i < button_bank->number; i++) {
		mem_free(button_bank->buttons[i].caption);
	}
	mem_free(button_bank->buttons);
	button_bank->buttons = NULL;
	button_bank->number = 0;
	button_bank->size = 0;
}

static void free_status_bar(struct status_bar *status_bar)
{
	if (status_bar->menu_panel != NULL) {
		free_menu_panel(status_bar->menu_panel);
		status_bar->menu_panel = NULL;
	}
	if (status_bar->button_bank.buttons != NULL) {
		free_button_bank(&status_bar->button_bank);
	}
	if (status_bar->texture != NULL) {
		SDL_DestroyTexture(status_bar->texture);
		status_bar->texture = NULL;
	}

	free_font(status_bar->font);
	status_bar->font = NULL;
}

static void free_font_info(struct font_info *font_info)
{
	if (font_info->name != NULL) {
		mem_free(font_info->name);
		font_info->name = NULL;
	}
	if (font_info->path != NULL) {
		mem_free(font_info->path);
		font_info->path = NULL;
	}
	font_info->loaded = false;
}

static void free_window_config(struct window_config *config)
{
	if (config->wallpaper_path != NULL) {
		mem_free(config->wallpaper_path);
	}
	if (config->system_font_name != NULL) {
		mem_free(config->system_font_name);
	}
	if (config->game_font_name != NULL) {
		mem_free(config->game_font_name);
	}
	mem_free(config);
}

static void free_graphics(struct graphics *graphics)
{
	if (graphics->texture != NULL) {
		SDL_DestroyTexture(graphics->texture);
		graphics->texture = NULL;
	}
}

static void free_subwindow_config(struct subwindow_config *config)
{
	if (config->font_name != NULL) {
		mem_free(config->font_name);
	}
	mem_free(config);
}

static void free_subwindow(struct subwindow *subwindow)
{
	assert(subwindow->loaded);

	if (subwindow->font != NULL) {
		assert(!subwindow->is_temporary);
		free_font(subwindow->font);
		subwindow->font = NULL;
	}
	if (subwindow->texture != NULL) {
		SDL_DestroyTexture(subwindow->texture);
		subwindow->texture = NULL;
	}
	if (subwindow->aux_texture != NULL) {
		SDL_DestroyTexture(subwindow->aux_texture);
		subwindow->aux_texture = NULL;
	}
	if (subwindow->term != NULL) {
		assert(!subwindow->is_temporary);
		display_term_destroy(subwindow->index);
	}
	if (subwindow->config != NULL) {
		free_subwindow_config(subwindow->config);
		subwindow->config = NULL;
	}

	subwindow->window = NULL;
	subwindow->loaded = false;
	subwindow->inited = false;
	subwindow->linked = false;
}

static void free_window(struct window *window)
{
	assert(window->loaded);

	for (size_t i = 0; i < window->permanent.number; i++) {
		struct subwindow *subwindow = window->permanent.subwindows[i];

		free_subwindow(subwindow);
		window->permanent.subwindows[i] = NULL;
	}
	window->permanent.number = 0;

	for (size_t i = 0; i < N_ELEMENTS(window->permanent.subwindows); i++) {
		assert(window->permanent.subwindows[i] == NULL);
	}

	for (size_t i = 0; i < N_ELEMENTS(window->temporary.subwindows); i++) {
		assert(window->temporary.subwindows[i] == NULL);
	}

	free_status_bar(&window->status_bar);

	if (window->wallpaper.texture != NULL) {
		SDL_DestroyTexture(window->wallpaper.texture);
		window->wallpaper.texture = NULL;
	}

	if (window->game_font != NULL) {
		free_font(window->game_font);
	}

	free_graphics(&window->graphics);

	if (window->renderer != NULL) {
		SDL_DestroyRenderer(window->renderer);
		window->renderer = NULL;
	}

	if (window->window != NULL) {
		SDL_DestroyWindow(window->window);
		window->window = NULL;
	}

	if (window->config != NULL) {
		free_window_config(window->config);
		window->config = NULL;
	}

	window->loaded = false;
	window->inited = false;
}

static void init_colors(void)
{
	assert(N_ELEMENTS(g_colors) == N_ELEMENTS(angband_color_table));

	for (size_t i = 0; i < N_ELEMENTS(g_colors); i++) {
		g_colors[i].r = angband_color_table[i][1];
		g_colors[i].g = angband_color_table[i][2];
		g_colors[i].b = angband_color_table[i][3];
		g_colors[i].a = DEFAULT_ALPHA_FULL;
	}
}

static void init_font_info(const char *directory)
{
	/* well maybe it will get ability to be reinitialized at some point */
	for (size_t i = 0; i < N_ELEMENTS(g_font_info); i++) {
		g_font_info[i].name = NULL;
		g_font_info[i].path = NULL;
		g_font_info[i].loaded = false;
	}

	ang_dir *dir = my_dopen(directory);
	assert(dir != NULL);

	char name[1024];
	char path[4096];

	size_t i = 0;
	while (i < N_ELEMENTS(g_font_info) && my_dread(dir, name, sizeof(name))) {
		path_build(path, sizeof(path), directory, name);

		if (is_font_file(path)) {
			g_font_info[i].name = string_make(name);
			g_font_info[i].path = string_make(path);
			g_font_info[i].loaded = true;
			if (suffix(path, ".fon")) {
				g_font_info[i].type = FONT_TYPE_RASTER;
				g_font_info[i].size = 0;
			} else {
				g_font_info[i].type = FONT_TYPE_VECTOR;
				g_font_info[i].size = DEFAULT_VECTOR_FONT_SIZE;
			}
			i++;
		}
	}
	assert(i > 0);

	sort(g_font_info, i, sizeof(g_font_info[0]), sort_cb_font_info);

	for (size_t j = 0; j < i; j++) {
		g_font_info[j].index = j;
	}

	my_dclose(dir);
}

static void create_defaults(void)
{
	struct window *window = get_new_window(WINDOW_MAIN);
	assert(window != NULL);

	for (unsigned i = 0; i < N_ELEMENTS(g_term_info); i++) {
		if (g_term_info[i].required) {
			attach_subwindow_to_window(window, get_new_subwindow(i));
		}
	}

	attach_subwindow_to_window(window, get_new_subwindow(DISPLAY_STATUS_LINE));
	attach_subwindow_to_window(window, get_new_subwindow(DISPLAY_PLAYER_COMPACT));
}

static void quit_systems(void)
{
	SDL_StopTextInput();
	TTF_Quit();
	IMG_Quit();
	SDL_Quit();
}

static void quit_hook(const char *s)
{
	(void) s;

	dump_config_file();
	Term_pop_all();
	free_globals();
	quit_systems();
}

static void init_systems(void)
{
	if (SDL_Init(INIT_SDL_FLAGS) != 0) {
		quit_fmt("SDL_Init: %s", SDL_GetError());
	}
	if (IMG_Init(INIT_IMG_FLAGS) != INIT_IMG_FLAGS) {
		quit_fmt("IMG_Init: %s", IMG_GetError());
	}
	if (TTF_Init() != 0) {
		quit_fmt("TTF_Init: %s", TTF_GetError());
	}

	SDL_StartTextInput();
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
}

int init_sdl2(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	init_systems();
	init_globals();

	if (!init_graphics_modes()) {
		quit_systems();
		return 1;
	}

	if (!read_config_file()) {
		create_defaults();
	}

	start_windows();
	load_terms();

	quit_aux = quit_hook;

	return 0;
}

/* these variables contain windows and terms that the ui operates on */

static struct subwindow g_permanent_subwindows[SUBWINDOW_PERMANENT_MAX];

static struct {
	struct subwindow subwindows[SUBWINDOW_TEMPORARY_MAX];
	size_t number;
} g_shadow_stack;

static struct window g_windows[MAX_WINDOWS];

/* the string ANGBAND_DIR_USER is freed before calling quit_hook(),
 * so we need to save the path to config file here */
static char g_config_file[4096];

static void init_globals(void)
{
	assert(N_ELEMENTS(g_term_info) == N_ELEMENTS(g_permanent_subwindows));

	for (size_t i = 0; i < N_ELEMENTS(g_permanent_subwindows); i++) {
		g_permanent_subwindows[i].index = g_term_info[i].index;
	}

	for (size_t i = 0; i < N_ELEMENTS(g_shadow_stack.subwindows); i++) {
		g_shadow_stack.subwindows[i].is_temporary = true;
		g_shadow_stack.subwindows[i].index = DISPLAY_MAX;
	}

	for (size_t i = 0; i < N_ELEMENTS(g_windows); i++) {
		g_windows[i].index = i;
	}

	init_font_info(ANGBAND_DIR_FONTS);
	init_colors();

	path_build(g_config_file, sizeof(g_config_file),
			DEFAULT_CONFIG_FILE_DIR, DEFAULT_CONFIG_FILE);
}

static bool is_subwindow_loaded(unsigned index)
{
	const struct subwindow *subwindow = get_subwindow_direct(index);
	assert(subwindow != NULL);

	return subwindow->loaded;
}

static void free_temporary_subwindow(struct subwindow *subwindow)
{
	assert(subwindow->is_temporary);
	assert(g_shadow_stack.number > 0);

#define STACK_TOP (g_shadow_stack.number - 1)
	assert(subwindow == &g_shadow_stack.subwindows[STACK_TOP]);
	assert(subwindow->index == g_shadow_stack.subwindows[STACK_TOP].index);
#undef STACK_TOP

	/* font of temporary subwindow 'belongs' to its window;
	 * term of temporary subwindow should not be destroyed manually */
	subwindow->font = NULL;
	subwindow->term = NULL;

	free_subwindow(subwindow);
	g_shadow_stack.number--;
}

static struct subwindow *get_new_temporary_subwindow(void)
{
	assert(g_shadow_stack.number < N_ELEMENTS(g_shadow_stack.subwindows));

	struct subwindow *subwindow = &g_shadow_stack.subwindows[g_shadow_stack.number];

	assert(!subwindow->loaded);
	assert(!subwindow->inited);
	assert(!subwindow->linked);

	wipe_subwindow(subwindow);

	g_shadow_stack.number++;

	return subwindow;
}

static struct subwindow *get_subwindow_direct(unsigned index)
{
	if (index < N_ELEMENTS(g_permanent_subwindows)
			&& g_permanent_subwindows[index].index == index)
	{
		return &g_permanent_subwindows[index];
	} else {
		for (size_t i = 0; i < N_ELEMENTS(g_permanent_subwindows); i++) {
			if (g_permanent_subwindows[i].index == index) {
				return &g_permanent_subwindows[i];
			}
		}
	}

	return NULL;
}

static struct subwindow *get_new_subwindow(unsigned index)
{
	struct subwindow *subwindow = get_subwindow_direct(index);
	assert(subwindow != NULL);
	assert(!subwindow->inited);
	assert(!subwindow->loaded);
	assert(!subwindow->linked);

	wipe_subwindow(subwindow);

	return subwindow;
}

static struct window *get_new_window(unsigned index)
{
	struct window *window = get_window_direct(index);

	assert(window != NULL);
	assert(!window->inited);
	assert(!window->loaded);

	wipe_window(window, DEFAULT_DISPLAY);

	return window;
}

static struct window *get_window_direct(unsigned index)
{
	if (index < N_ELEMENTS(g_windows) && g_windows[index].index == index) {
		return &g_windows[index];
	} else {
		return NULL;
	}
}

static struct window *get_loaded_window(unsigned index)
{
	struct window *window = get_window_direct(index);

	assert(window != NULL);

	if (window->loaded) {
		return window;
	} else {
		return NULL;
	}
}

static struct window *get_window_by_id(Uint32 id)
{
	for (size_t i = 0; i < N_ELEMENTS(g_windows); i++) {
		if (g_windows[i].loaded && g_windows[i].id == id) {
			return &g_windows[i];
		}
	}
	
	return NULL;
}

static void free_globals(void)
{
	for (size_t i = 0; i < N_ELEMENTS(g_font_info); i++) {
		free_font_info(&g_font_info[i]);
	}
	for (size_t i = 0; i < N_ELEMENTS(g_windows); i++) {
		if (g_windows[i].loaded) {
			free_window(&g_windows[i]);
		}
	}
	for (size_t i = 0; i < N_ELEMENTS(g_permanent_subwindows); i++) {
		assert(!g_permanent_subwindows[i].inited);
		assert(!g_permanent_subwindows[i].loaded);
		assert(!g_permanent_subwindows[i].linked);
	}
}

static void start_windows(void)
{
	for (size_t i = N_ELEMENTS(g_windows); i > 0; i--) {
		if (g_windows[i - 1].inited) {
			start_window(&g_windows[i - 1]);
		}
	}
}

static void load_terms(void)
{
	for (size_t i = 0; i < N_ELEMENTS(g_permanent_subwindows); i++) {
		if (g_permanent_subwindows[i].loaded) {
			load_term(&g_permanent_subwindows[i]);
		}
	}
}

/* Config file stuff */

static void dump_config_file(void)
{
	ang_file *config = file_open(g_config_file, MODE_WRITE, FTYPE_TEXT);

	assert(config != NULL);

	for (size_t i = 0; i < N_ELEMENTS(g_windows); i++) {
		if (g_windows[i].loaded) {
			dump_window(&g_windows[i], config);
		}
	}

	file_close(config);
}

#define GET_WINDOW_FROM_INDEX \
	struct window *window = get_window_direct(parser_getuint(parser, "index")); \
	if (window == NULL) { \
		return PARSE_ERROR_INVALID_VALUE; \
	} \

#define WINDOW_INIT_OK \
	if (!window->inited) { \
		return PARSE_ERROR_MISSING_RECORD_HEADER; \
	}

#define GET_SUBWINDOW_FROM_INDEX \
	struct subwindow *subwindow = get_subwindow_direct(parser_getuint(parser, "index")); \
	if (subwindow == NULL) { \
		return PARSE_ERROR_INVALID_VALUE; \
	} \

#define SUBWINDOW_INIT_OK \
	if (!subwindow->inited) { \
		return PARSE_ERROR_MISSING_RECORD_HEADER; \
	}

static enum parser_error config_window_display(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;

	int display = parser_getint(parser, "display");

	if (display < 0 || display > SDL_GetNumVideoDisplays()) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}

	wipe_window(window, display);

	window->config = mem_zalloc(sizeof(*window->config));

	window->config->window_flags = SDL_WINDOW_RESIZABLE;

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_fullscreen(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *mode = parser_getsym(parser, "fullscreen");

	if (streq(mode, "true")) {
		window->config->window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	} else if (streq(mode, "false")) {
		;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}
	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_rect(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	window->full_rect.x = parser_getint(parser, "x");
	window->full_rect.y = parser_getint(parser, "y");
	window->full_rect.w = parser_getint(parser, "w");
	window->full_rect.h = parser_getint(parser, "h");

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_renderer(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *type = parser_getsym(parser, "type");

	if (streq(type, "hardware")) {
		window->config->renderer_flags = SDL_RENDERER_ACCELERATED;
	} else if (streq(type, "software")) {
		window->config->renderer_flags = SDL_RENDERER_SOFTWARE;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}
	
	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_wallpaper_path(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *path = parser_getstr(parser, "path");

	if (streq(path, "default")) {
		char buf[4096];
		path_build(buf, sizeof(buf), DEFAULT_WALLPAPER_DIR, DEFAULT_WALLPAPER);

		window->config->wallpaper_path = string_make(buf);
	} else {
		window->config->wallpaper_path = string_make(path);
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_wallpaper_mode(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *mode = parser_getstr(parser, "mode");

	if (streq(mode, "none")) {
		window->wallpaper.mode = WALLPAPER_DONT_SHOW;
	} else if (streq(mode, "tiled")) {
		window->wallpaper.mode = WALLPAPER_TILED;
	} else if (streq(mode, "centered")) {
		window->wallpaper.mode = WALLPAPER_CENTERED;
	} else if (streq(mode, "scaled")) {
		window->wallpaper.mode = WALLPAPER_SCALED;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_system_font(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *name = parser_getstr(parser, "name");
	int size = parser_getint(parser, "size");

	if (find_font_info(name) == NULL) {
		return PARSE_ERROR_INVALID_VALUE;
	}

	window->config->system_font_name = string_make(name);
	window->config->system_font_size = size;

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_game_font(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	const char *name = parser_getstr(parser, "name");
	int size = parser_getint(parser, "size");

	if (find_font_info(name) == NULL) {
		return PARSE_ERROR_INVALID_VALUE;
	}

	window->config->game_font_name = string_make(name);
	window->config->game_font_size = size;

	return PARSE_ERROR_NONE;
}

static enum parser_error config_window_graphics(struct parser *parser)
{
	GET_WINDOW_FROM_INDEX;
	WINDOW_INIT_OK;

	int id = parser_getint(parser, "id");

	if (get_graphics_mode(id) == NULL) {
		return PARSE_ERROR_INVALID_VALUE;
	}

	window->graphics.id = id;

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_window(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;

	if (subwindow->inited) {
		return PARSE_ERROR_NON_SEQUENTIAL_RECORDS;
	}
	wipe_subwindow(subwindow);

	unsigned windex = parser_getuint(parser, "windex");
	if (windex >= N_ELEMENTS(g_windows)) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}

	struct window *window = &g_windows[windex];
	if (!window->inited) {
		return PARSE_ERROR_NON_SEQUENTIAL_RECORDS;
	}

	subwindow->config = mem_zalloc(sizeof(*subwindow->config));

	attach_subwindow_to_window(window, subwindow);

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_rect(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	subwindow->full_rect.x = parser_getint(parser, "x");
	subwindow->full_rect.y = parser_getint(parser, "y");
	subwindow->full_rect.w = parser_getint(parser, "w");
	subwindow->full_rect.h = parser_getint(parser, "h");

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_font(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	const char *name = parser_getstr(parser, "name");
	int size = parser_getint(parser, "size");

	if (find_font_info(name) == NULL) {
		/* TODO maybe its not really an error? the font file was
		 * probably just deleted and now the ui wont event start... */
		return PARSE_ERROR_INVALID_VALUE;
	}
	subwindow->config->font_name = string_make(name);
	subwindow->config->font_size = size;

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_graphics(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	const char *graphics = parser_getsym(parser, "graphics");

	if (streq(graphics, "true")) {
		subwindow->use_graphics = true;
	} else if (streq(graphics, "false")) {
		subwindow->use_graphics = false;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_borders(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	const char *borders = parser_getsym(parser, "borders");

	if (streq(borders, "true")) {
		subwindow->borders.visible = true;
	} else if (streq(borders, "false")) {
		subwindow->borders.visible = false;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_top(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	const char *top = parser_getsym(parser, "top");

	if (streq(top, "true")) {
		subwindow->is_top = true;
	} else if (streq(top, "false")) {
		subwindow->is_top = false;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	const char *always = parser_getsym(parser, "always");

	if (streq(always, "true")) {
		subwindow->always_top = true;
	} else if (streq(always, "false")) {
		subwindow->always_top = false;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error config_subwindow_alpha(struct parser *parser)
{
	GET_SUBWINDOW_FROM_INDEX;
	SUBWINDOW_INIT_OK;

	int alpha = parser_getint(parser, "alpha");

	if (alpha < 0 || alpha > DEFAULT_ALPHA_FULL) {
		return PARSE_ERROR_INVALID_VALUE;
	}

	subwindow->color.a = alpha;

	return PARSE_ERROR_NONE;
}

#undef GET_WINDOW_FROM_INDEX
#undef WINDOW_INIT_OK
#undef GET_SUBWINDOW_FROM_INDEX
#undef SUBWINDOW_INIT_OK

static struct parser *init_parse_config(void)
{
	struct parser *parser = parser_new();
	
	parser_reg(parser, "window-display uint index int display",
			config_window_display);
	parser_reg(parser, "window-fullscreen uint index sym fullscreen",
			config_window_fullscreen);
	parser_reg(parser, "window-full-rect uint index int x int y int w int h",
			config_window_rect);
	parser_reg(parser, "window-renderer uint index sym type",
			config_window_renderer);
	parser_reg(parser, "window-wallpaper-path uint index str path",
			config_window_wallpaper_path);
	parser_reg(parser, "window-wallpaper-mode uint index str mode",
			config_window_wallpaper_mode);
	parser_reg(parser, "window-system-font uint index int size str name",
			config_window_system_font);
	parser_reg(parser, "window-game-font uint index int size str name",
			config_window_game_font);
	parser_reg(parser, "window-graphics-id uint index int id",
			config_window_graphics);

	parser_reg(parser, "subwindow-window uint index uint windex",
			config_subwindow_window);
	parser_reg(parser, "subwindow-full-rect uint index int x int y int w int h",
			config_subwindow_rect);
	parser_reg(parser, "subwindow-font uint index int size str name",
			config_subwindow_font);
	parser_reg(parser, "subwindow-graphics uint index sym graphics",
			config_subwindow_graphics);
	parser_reg(parser, "subwindow-borders uint index sym borders",
			config_subwindow_borders);
	parser_reg(parser, "subwindow-top uint index sym top sym always",
			config_subwindow_top);
	parser_reg(parser, "subwindow-alpha uint index int alpha",
			config_subwindow_alpha);

	return parser;
}

static void print_error(const char *name, struct parser *parser)
{
	struct parser_state state;
	parser_getstate(parser, &state);

	fprintf(stderr, "parse error in %s line %d column %d: %s: %s\n",
			name,
			state.line,
			state.col,
			state.msg,
			parser_error_str[state.error]);
}

static bool read_config_file(void)
{
	ang_file *config = file_open(g_config_file, MODE_READ, FTYPE_TEXT);
	if (config == NULL) {
		/* not an error, its ok for a config file to not exist */
		return false;
	}

	char line[1024];
	struct parser *parser = init_parse_config();
	errr error = 0;

	while (file_getl(config, line, sizeof(line))) {
		error = parser_parse(parser, line);
		if (error != PARSE_ERROR_NONE) {
			print_error(g_config_file, parser);
			break;
		}
	}

	parser_destroy(parser);
	file_close(config);

	return error == PARSE_ERROR_NONE;
}

#endif /* USE_SDL2 */
