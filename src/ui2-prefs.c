/**
 * \file ui2-prefs.c
 * \brief Pref file handling code
 *
 * Copyright (c) 2003 Takeshi Mogami, Robert Ruehlmann
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
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
#include "game-input.h"
#include "grafmode.h"
#include "init.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-ignore.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "project.h"
#include "trap.h"
#include "ui2-display.h"
#include "ui2-keymap.h"
#include "ui2-prefs.h"
#include "ui2-term.h"
#include "sound.h"

/* The graphics (tiles) mode is enabled */
int use_graphics;

uint32_t *monster_x_attr;
wchar_t *monster_x_char;

uint32_t *kind_x_attr;
wchar_t *kind_x_char;

uint32_t *feat_x_attr[LIGHTING_MAX];
wchar_t *feat_x_char[LIGHTING_MAX];

uint32_t *trap_x_attr[LIGHTING_MAX];
wchar_t *trap_x_char[LIGHTING_MAX];

uint32_t *flavor_x_attr;
wchar_t *flavor_x_char;

static size_t flavor_max = 0;

/**
 * ------------------------------------------------------------------------
 * Pref file saving code
 * ------------------------------------------------------------------------
 */

/**
 * Header and footer marker string for pref file dumps
 */
static const char *dump_separator = "#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#=#";

/**
 * Remove old lines from pref files
 * If you are using setgid, make sure privileges were raised prior to calling this.
 */
static void remove_old_dump(const char *cur_fname, const char *mark)
{
	char new_fname[1024];

	char start_line[1024];
	char end_line[1024];

	/* Format up some filenames */
	strnfmt(new_fname, sizeof(new_fname), "%s.new", cur_fname);

	/* Work out what we expect to find */
	strnfmt(start_line, sizeof(start_line), "%s begin %s", dump_separator, mark);
	strnfmt(end_line, sizeof(end_line), "%s end %s", dump_separator, mark);

	/* Open current file */
	ang_file *cur_file = file_open(cur_fname, MODE_READ, FTYPE_TEXT);
	if (!cur_file) {
		return;
	}

	/* Open new file */
	ang_file *new_file = file_open(new_fname, MODE_WRITE, FTYPE_TEXT);
	if (!new_file) {
		msg("Failed to create file %s", new_fname);
		file_close(cur_file);
		return;
	}

	bool between_marks = false;
	bool changed = false;

	/* Loop for every line */
	char buf[1024];
	while (file_getl(cur_file, buf, sizeof(buf))) {
		/* Turn on at the start line, turn off at the finish line
		 * (and dont print start or finish lines) */
		if (streq(buf, start_line)) {
			between_marks = true;
		} else if (streq(buf, end_line)) {
			between_marks = false;
			changed = true;
		} else if (!between_marks) {
			file_putf(new_file, "%s\n", buf);
		}
	}

	/* Close files */
	file_close(cur_file);
	file_close(new_file);

	/* If there are changes use the new file. otherwise just destroy it */
	if (changed) {
		char old_fname[1024];
		strnfmt(old_fname, sizeof(old_fname), "%s.old", cur_fname);

		if (file_move(cur_fname, old_fname)) {
			file_move(new_fname, cur_fname);
			file_delete(old_fname);
		}
	} else {
		file_delete(new_fname);
	}
}

/**
 * Output the header of a pref-file dump
 */
static void pref_header(ang_file *f, const char *mark)
{
	/* Start of dump */
	file_putf(f, "%s begin %s\n", dump_separator, mark);

	file_putf(f, "# *Warning!*  The lines below are an automatic dump.\n");
	file_putf(f, "# Don't edit them; changes will be deleted and replaced automatically.\n");
}

/**
 * Output the footer of a pref-file dump
 */
static void pref_footer(ang_file *f, const char *mark)
{
	file_putf(f, "# *Warning!*  The lines above are an automatic dump.\n");
	file_putf(f, "# Don't edit them; changes will be deleted and replaced automatically.\n");

	/* End of dump */
	file_putf(f, "%s end %s\n", dump_separator, mark);
}

/**
 * Dump monsters
 */
void dump_monsters(ang_file *file)
{
	for (int i = 0; i < z_info->r_max; i++) {
		const struct monster_race *race = &r_info[i];

		/* Skip non-entries */
		if (race->name) {
			unsigned long long attr = monster_x_attr[i];
			unsigned long long ch = monster_x_char[i];
			file_putf(file, "monster:%s:0x%02llX:0x%02llX\n", race->name, attr, ch);
		}
	}
}

/**
 * Dump objects
 */
void dump_objects(ang_file *file)
{
	file_putf(file, "# Objects\n");

	for (int i = 1; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		if (kind->name && kind->tval) {
			char name[120];
			const char *tval = tval_find_name(kind->tval);
			unsigned long long attr = kind_x_attr[i];
			unsigned long long ch = kind_x_char[i];

			object_short_name(name, sizeof(name), kind->name);
			file_putf(file, "object:%s:%s:%llu:%llu\n", tval, name, attr, ch);
		}
	}
}

/**
 * Dump autoinscriptions
 */
void dump_autoinscriptions(ang_file *file) {
	for (int i = 1; i < z_info->k_max; i++) {
		struct object_kind *k = &k_info[i];

		if (k->name && k->tval) {
			/* Only aware autoinscriptions go to the prefs file */
			const char *note = get_autoinscription(k, true);
			if (note) {
				char name[120];
				object_short_name(name, sizeof(name), k->name);
				file_putf(file, "inscribe:%s:%s:%s\n", tval_find_name(k->tval), name, note);
			}
		}
	}
}

/**
 * Dump features
 */
void dump_features(ang_file *file)
{
	for (int f = 0; f < z_info->f_max; f++) {
		struct feature *feat = &f_info[f];

		/* Skip non-entries and mimics (except invisible trap) */
		if (!feat->name || feat->mimic != f) {
			continue;
		}

		file_putf(file, "# Terrain: %s\n", feat->name);
		for (size_t l = 0; l < LIGHTING_MAX; l++) {
			unsigned long long attr = feat_x_attr[l][f];
			unsigned long long chr = feat_x_char[l][f];

			const char *light;
			switch (l) {
				case LIGHTING_LOS:   light = "los";   break;
				case LIGHTING_TORCH: light = "torch"; break;
				case LIGHTING_LIT:   light = "lit";   break;
				case LIGHTING_DARK:  light = "dark";  break;
				default:             light = NULL;    break;
			}

			assert(light);

			file_putf(file, "feat:%d:%s:%llu:%llu\n", f, light, attr, chr);
		}
	}
}

/**
 * Dump flavors
 */
void dump_flavors(ang_file *file)
{
	for (struct flavor *flavor = flavors; flavor; flavor = flavor->next) {
		unsigned long long attr = flavor_x_attr[flavor->fidx];
		unsigned long long chr = flavor_x_char[flavor->fidx];

		file_putf(file, "# Item flavor: %s\n", flavor->text);
		file_putf(file, "flavor:%d:%llu:%llu\n\n", flavor->fidx, attr, chr);
	}
}

/**
 * Dump colors
 */
void dump_colors(ang_file *file)
{
	for (int i = 0; i < MAX_COLORS; i++) {
		unsigned a = angband_color_table[i][0];
		unsigned r = angband_color_table[i][1];
		unsigned g = angband_color_table[i][2];
		unsigned b = angband_color_table[i][3];

		/* Skip non-entries (transparent black colors) */
		if (a || r || g || b) {
			const char *name =
				i < BASIC_COLORS ? color_table[i].name : "unknown";

			file_putf(file, "# Color: %s\n", name);
			file_putf(file, "color:%d:%u:%u:%u:%u\n\n", i, a, r, g, b);
		}
	}
}

/**
 * Save a set of preferences to file, overwriting any old preferences with the
 * same title.
 *
 * \param path is the filename to dump to
 * \param dump is a pointer to the function that does the writing to file
 * \param title is the name of this set of preferences
 *
 * \returns true on success, false otherwise.
 */
bool prefs_save(const char *path, void (*dump)(ang_file *), const char *title)
{
	safe_setuid_grab();

	/* Remove old keymaps */
	remove_old_dump(path, title);

	ang_file *file = file_open(path, MODE_APPEND, FTYPE_TEXT);
	if (!file) {
		safe_setuid_drop();
		return false;
	}

	/* Append the header */
	pref_header(file, title);
	file_putf(file, "\n");

	dump(file);

	file_putf(file, "\n");
	pref_footer(file, title);
	file_close(file);

	safe_setuid_drop();

	return true;
}

/**
 * ------------------------------------------------------------------------
 * Pref file parser
 * ------------------------------------------------------------------------
 */

/**
 * Load another file.
 */
static enum parser_error parse_prefs_load(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);

	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *file = parser_getstr(p, "file");
	process_pref_file(file, true, d->user);

	return PARSE_ERROR_NONE;
}

/**
 * Helper function for "process_pref_file()"
 *
 * Input:
 *   v: output buffer array
 *   f: final character
 *
 * Output:
 *   result
 *
 * TODO: lexer for this... thing? or maybe don't touch it as it seems to work...
 */
static const char *process_pref_file_expr(char **next_token, char *end_char)
{
	/* Return value */
	const char *retval = "?o?o?";

	char *token = *next_token;

	while (isspace((unsigned char) *token)) {
		token++;
	}

	/* Either the string starts with a [ or it doesn't */
	if (*token == '[') {
		token++;

		char end = ' ';
		const char *expr = process_pref_file_expr(&token, &end);

		/* Check all the different types of connective */
		if (*expr == 0) {
			/* Nothing */
		} else if (streq(expr, "IOR")) {
			retval = "0";
			while (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
				if (*expr && !streq(expr, "0")) {
					retval = "1";
				}
			}
		} else if (streq(expr, "AND")) {
			retval = "1";
			while (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
				if (*expr && streq(expr, "0")) {
					retval = "0";
				}
			}
		} else if (streq(expr, "NOT")) {
			retval = "1";
			while (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
				if (*expr && !streq(expr, "0")) {
					retval = "0";
				}
			}
		} else if (streq(expr, "EQU")) {
			retval = "1";
			if (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
			}
			while (*token && end != ']') {
				const char *saved = expr;
				expr = process_pref_file_expr(&token, &end);
				if (*expr && !streq(saved, expr)) {
					retval = "0";
				}
			}
		} else if (streq(expr, "LEQ")) {
			retval = "1";
			if (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
			}
			while (*token && end != ']') {
				const char *saved = expr;
				expr = process_pref_file_expr(&token, &end);
				if (*expr && strcmp(saved, expr) > 0) {
					retval = "0";
				}
			}
		} else if (streq(expr, "GEQ")) {
			retval = "1";
			if (*token && end != ']') {
				expr = process_pref_file_expr(&token, &end);
			}
			while (*token && end != ']') {
				const char *saved = expr;
				expr = process_pref_file_expr(&token, &end);
				if (*expr && strcmp(saved, expr) < 0) {
					retval = "0";
				}
			}
		} else {
			while (*token && end != ']') {
				/* Unrecognized directive; skip the rest */
				process_pref_file_expr(&token, &end);
			}
		}

		/* Verify ending */
		if (end != ']') {
			retval = "?x?x?";
		}

		*end_char = *token;
		*token = 0;
	} else {
		/* Save the beginning of token */
		char *start = token;

		/* Accept all printables except spaces and brackets */
		while (isprint((unsigned char) *token) && !strchr(" []", *token)) {
			token++;
		}

		*end_char = *token;
		*token = 0;

		/* Variables start with $, otherwise it's a constant */
		if (*start == '$') {
			if (streq(start + 1, "SYS")) {
				retval = ANGBAND_SYS;
			} else if (streq(start + 1, "RACE")) {
				retval = player->race->name + 1;
			} else if (streq(start + 1, "CLASS")) {
				retval = player->class->name;
			} else if (streq(start + 1, "PLAYER")) {
				retval = player_safe_name(player, true);
			}
		} else {
			retval = start;
		}
	}

	if (*end_char == 0) {
		/* End of string; nowhere to advance */
		*next_token = token;
	} else {
		/* Advance to the next token */
		*next_token = token + 1;
	}

	return retval;
}

/**
 * Parse one of the prefix-based logical expressions used in pref files
 */
static enum parser_error parse_prefs_expr(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);

	assert(d != NULL);

	char *str = string_make(parser_getstr(p, "expr"));

	/* Parse the expr */
	char ch = 0;
	char *expr = str;
	const char *retval = process_pref_file_expr(&expr, &ch);

	/* Set flag */
	d->bypass = streq(retval, "0");

	string_free(str);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_object(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *tval = parser_getsym(p, "tval");
	const char *sval = parser_getsym(p, "sval");
	if (streq(tval, "*")) {
		/* object:*:* means handle all objects and flavors */
		uint32_t attr = parser_getuint(p, "attr");
		wchar_t ch = parser_getuint(p, "char");

		if (!streq(sval, "*")) {
			return PARSE_ERROR_UNRECOGNISED_SVAL;
		}

		for (size_t i = 0; i < z_info->k_max; i++) {
			struct object_kind *kind_local = &k_info[i];

			kind_x_attr[kind_local->kidx] = attr;
			kind_x_char[kind_local->kidx] = ch;
		}

		for (struct flavor *flavor = flavors; flavor; flavor = flavor->next) {
			flavor_x_attr[flavor->fidx] = attr;
			flavor_x_char[flavor->fidx] = ch;
		}
	} else {
		int tvi = tval_find_idx(tval);
		if (tvi < 0) {
			return PARSE_ERROR_UNRECOGNISED_TVAL;
		}

		/* object:tval:* means handle all objects and flavors with this tval */
		if (streq(sval, "*")) {
			uint32_t attr = parser_getuint(p, "attr");
			wchar_t ch = parser_getuint(p, "char");

			for (size_t i = 0; i < z_info->k_max; i++) {
				struct object_kind *kind_local = &k_info[i];

				if (kind_local->tval == tvi) {
					kind_x_attr[kind_local->kidx] = attr;
					kind_x_char[kind_local->kidx] = ch;
				}
			}

			for (struct flavor *flavor = flavors; flavor; flavor = flavor->next) {
				if (flavor->tval == tvi) {
					flavor_x_attr[flavor->fidx] = attr;
					flavor_x_char[flavor->fidx] = ch;
				}
			}
		} else {
			int svi = lookup_sval(tvi, sval);
			if (svi < 0) {
				return PARSE_ERROR_UNRECOGNISED_SVAL;
			}

			struct object_kind *kind = lookup_kind(tvi, svi);
			if (!kind) {
				return PARSE_ERROR_UNRECOGNISED_SVAL;
			}

			kind_x_attr[kind->kidx] = parser_getuint(p, "attr");
			kind_x_char[kind->kidx] = parser_getuint(p, "char");
		}
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_monster(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *name = parser_getsym(p, "name");
	struct monster_race *monster = lookup_monster(name);
	if (!monster) {
		return PARSE_ERROR_NO_KIND_FOUND;
	}

	monster_x_attr[monster->ridx] = parser_getuint(p, "attr");
	monster_x_char[monster->ridx] = parser_getuint(p, "char");

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_monster_base(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *name = parser_getsym(p, "name");
	struct monster_base *mb = lookup_monster_base(name);
	if (!mb) {
		return PARSE_ERROR_NO_KIND_FOUND;
	}

	uint32_t attr = parser_getuint(p, "attr");
	wchar_t ch = parser_getuint(p, "char");

	for (size_t i = 0; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];

		if (race->base == mb) {
			monster_x_attr[race->ridx] = attr;
			monster_x_char[race->ridx] = ch;
		}
	}

	return PARSE_ERROR_NONE;
}

static void set_trap_graphic(int trap_idx, int light_idx, uint32_t attr, wchar_t ch) {
	assert(trap_idx >= 0);
	assert(light_idx >= 0);

	if (light_idx < LIGHTING_MAX) {
		trap_x_attr[light_idx][trap_idx] = attr;
		trap_x_char[light_idx][trap_idx] = ch;
	} else {
		for (light_idx = 0; light_idx < LIGHTING_MAX; light_idx++) {
			trap_x_attr[light_idx][trap_idx] = attr;
			trap_x_char[light_idx][trap_idx] = ch;
		}
	}
}

static enum parser_error parse_prefs_trap(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	/* idx can be "*" or a number */
	const char *idx_sym = parser_getsym(p, "idx");

	int trap_idx;

	if (streq(idx_sym, "*")) {
		trap_idx = -1;
	} else {
		char *endptr;
		trap_idx = strtol(idx_sym, &endptr, 0);
		if (*endptr != 0) {
			return PARSE_ERROR_NOT_NUMBER;
		}

		if (trap_idx < 0 || trap_idx >= z_info->trap_max) {
			return PARSE_ERROR_OUT_OF_BOUNDS;
		}
	}

	int light_idx;

	const char *lighting = parser_getsym(p, "lighting");
	if (streq(lighting, "torch")) {
		light_idx = LIGHTING_TORCH;
	} else if (streq(lighting, "los")) {
		light_idx = LIGHTING_LOS;
	} else if (streq(lighting, "lit")) {
		light_idx = LIGHTING_LIT;
	} else if (streq(lighting, "dark")) {
		light_idx = LIGHTING_DARK;
	} else if (streq(lighting, "*")) {
		light_idx = LIGHTING_MAX;
	} else {
		return PARSE_ERROR_INVALID_LIGHTING;
	}

	set_trap_graphic(trap_idx, light_idx,
			parser_getuint(p, "attr"), parser_getuint(p, "char"));

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_feat(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	unsigned fidx = parser_getuint(p, "idx");
	if (fidx >= z_info->f_max) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}

	int light_idx;

	const char *lighting = parser_getsym(p, "lighting");
	if (streq(lighting, "torch")) {
		light_idx = LIGHTING_TORCH;
	} else if (streq(lighting, "los")) {
		light_idx = LIGHTING_LOS;
	} else if (streq(lighting, "lit")) {
		light_idx = LIGHTING_LIT;
	} else if (streq(lighting, "dark")) {
		light_idx = LIGHTING_DARK;
	} else if (streq(lighting, "*")) {
		light_idx = LIGHTING_MAX;
	} else {
		return PARSE_ERROR_INVALID_LIGHTING;
	}

	assert(light_idx >= 0);

	uint32_t attr = parser_getuint(p, "attr");
	wchar_t ch = parser_getuint(p, "char");

	if (light_idx < LIGHTING_MAX) {
		feat_x_attr[light_idx][fidx] = attr;
		feat_x_char[light_idx][fidx] = ch;
	} else {
		for (light_idx = 0; light_idx < LIGHTING_MAX; light_idx++) {
			feat_x_attr[light_idx][fidx] = attr;
			feat_x_char[light_idx][fidx] = ch;
		}
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_gf(struct parser *p)
{
	bool types[GF_MAX] = {0};

	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	/* Parse the type, which is a | seperated list of GF_ constants */
	char *str = string_make(parser_getsym(p, "type"));
	char *tok = strtok(str, "| ");
	while (tok) {
		if (streq(tok, "*")) {
			memset(types, true, sizeof(types));
		} else {
			int idx = gf_name_to_idx(tok);
			if (idx == -1) {
				string_free(str);
				return PARSE_ERROR_INVALID_VALUE;
			}

			types[idx] = true;
		}

		tok = strtok(NULL, "| ");
	}

	string_free(str);

	int motion;
	const char *direction = parser_getsym(p, "direction");
	if (streq(direction, "static")) {
		motion = BOLT_NO_MOTION;
	} else if (streq(direction, "0")) {
		motion = BOLT_0;
	} else if (streq(direction, "45")) {
		motion = BOLT_45;
	} else if (streq(direction, "90")) {
		motion = BOLT_90;
	} else if (streq(direction, "135")) {
		motion = BOLT_135;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}

	uint32_t attr = parser_getuint(p, "attr");
	wchar_t ch = parser_getuint(p, "char");

	for (size_t i = 0; i < GF_MAX; i++) {
		if (types[i]) {
			gf_to_attr[i][motion] = attr;
			gf_to_char[i][motion] = ch;
		}
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_flavor(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	unsigned idx = parser_getuint(p, "idx");

	for (struct flavor *flavor = flavors; flavor; flavor = flavor->next) {
		if (flavor->fidx == idx) {
			flavor_x_attr[idx] = parser_getuint(p, "attr");
			flavor_x_char[idx] = parser_getuint(p, "char");
			break;
		}
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_inscribe(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	int tvi = tval_find_idx(parser_getsym(p, "tval"));
	if (tvi < 0) {
		return PARSE_ERROR_UNRECOGNISED_TVAL;
	}

	int svi = lookup_sval(tvi, parser_getsym(p, "sval"));
	if (svi < 0) {
		return PARSE_ERROR_UNRECOGNISED_SVAL;
	}

	struct object_kind *kind = lookup_kind(tvi, svi);
	if (!kind) {
		return PARSE_ERROR_UNRECOGNISED_SVAL;
	}

	add_autoinscription(kind->kidx, parser_getstr(p, "text"), true);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_keymap_action(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);	
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *act = "";
	if (parser_hasval(p, "act")) {
		act = parser_getstr(p, "act");
	}

	keypress_from_text(d->keymap_buffer, N_ELEMENTS(d->keymap_buffer), act);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_keymap_input(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	int mode = parser_getint(p, "mode");
	if (mode < 0 || mode >= KEYMAP_MODE_MAX) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}

	struct keypress keys[2];
	keypress_from_text(keys, N_ELEMENTS(keys), parser_getstr(p, "key"));
	if (keys[0].type != EVT_KBRD || keys[1].type != EVT_NONE) {
		return PARSE_ERROR_FIELD_TOO_LONG;
	}

	keymap_add(mode, keys[0], d->keymap_buffer, d->user);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_message(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	const char *type = parser_getsym(p, "type");
	const char *attr = parser_getsym(p, "attr");

	int msg_index = message_lookup_by_name(type);

	if (msg_index < 0) {
		return PARSE_ERROR_INVALID_MESSAGE;
	}

	int color;
	if (strlen(attr) > 1) {
		color = color_text_to_attr(attr);
	} else {
		color = color_char_to_attr(attr[0]);
	}

	if (color < 0) {
		return PARSE_ERROR_INVALID_COLOR;
	}

	message_color_define(msg_index, color);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_prefs_color(struct parser *p)
{
	struct prefs_data *d = parser_priv(p);
	assert(d != NULL);
	if (d->bypass) {
		return PARSE_ERROR_NONE;
	}

	unsigned idx = parser_getuint(p, "idx");
	if (idx >= MAX_COLORS) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}

	angband_color_table[idx][0] = parser_getuint(p, "a");
	angband_color_table[idx][1] = parser_getuint(p, "r");
	angband_color_table[idx][2] = parser_getuint(p, "g");
	angband_color_table[idx][3] = parser_getuint(p, "b");

	return PARSE_ERROR_NONE;
}

enum parser_error parse_prefs_dummy(struct parser *p)
{
	(void) p;

	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_prefs(bool user)
{
	struct parser *p = parser_new();
	struct prefs_data *data = mem_zalloc(sizeof(*data));

	parser_setpriv(p, data);
	data->user = user;

	parser_reg(p, "% str file", parse_prefs_load);
	parser_reg(p, "? str expr", parse_prefs_expr);
	parser_reg(p, "object sym tval sym sval uint attr uint char", parse_prefs_object);
	parser_reg(p, "monster sym name uint attr uint char", parse_prefs_monster);
	parser_reg(p, "monster-base sym name uint attr uint char", parse_prefs_monster_base);
	parser_reg(p, "feat uint idx sym lighting uint attr uint char", parse_prefs_feat);
	parser_reg(p, "trap sym idx sym lighting uint attr uint char", parse_prefs_trap);
	parser_reg(p, "GF sym type sym direction uint attr uint char", parse_prefs_gf);
	parser_reg(p, "flavor uint idx uint attr uint char", parse_prefs_flavor);
	parser_reg(p, "inscribe sym tval sym sval str text", parse_prefs_inscribe);
	parser_reg(p, "keymap-act ?str act", parse_prefs_keymap_action);
	parser_reg(p, "keymap-input int mode str key", parse_prefs_keymap_input);
	parser_reg(p, "message sym type sym attr", parse_prefs_message);
	parser_reg(p, "color uint idx uint a uint r uint g uint b", parse_prefs_color);
	register_sound_pref_parser(p);

	return p;
}

static errr finish_parse_prefs(struct parser *p)
{
	(void) p;

	return PARSE_ERROR_NONE;
}

errr process_pref_file_command(const char *line)
{
	struct parser *p = init_parse_prefs(true);
	errr e = parser_parse(p, line);
	mem_free(parser_priv(p));
	parser_destroy(p);
	return e;
}


static void print_error(const char *name, struct parser *p) {
	struct parser_state state;
	parser_getstate(p, &state);
	msg("Parse error in %s line %d column %d: %s: %s",
			name, state.line, state.col, state.msg, parser_error_str[state.error]);
	event_signal(EVENT_MESSAGE_FLUSH);
}

/**
 * Process the user pref file with a given path.
 *
 * \param path is the name of the pref file.
 * \param quiet means "don't complain about not finding the file".
 * \param user should be true if the pref file is user-specific and not a game
 * default.
 */
static bool process_pref_file_named(const char *path, bool quiet, bool user) {
	ang_file *file = file_open(path, MODE_READ, -1);
	errr e = PARSE_ERROR_NONE;

	if (!file) {
		if (!quiet) {
			msg("Cannot open '%s'.", path);
		}
		e = PARSE_ERROR_INTERNAL; /* signal failure to callers */
	} else {
		char line[1024];

		struct parser *p = init_parse_prefs(user);
		while (e == PARSE_ERROR_NONE && file_getl(file, line, sizeof(line))) {
			e = parser_parse(p, line);
		}

		if (e == PARSE_ERROR_NONE) {
			finish_parse_prefs(p);
		} else {
			print_error(path, p);
		}

		file_close(file);
		mem_free(parser_priv(p));
		parser_destroy(p);
	}

	return e == PARSE_ERROR_NONE;
}

/**
 * Process the user pref file with a given name and search paths.
 *
 * \param name is the name of the pref file.
 * \param quiet means "don't complain about not finding the file".
 * \param user should be true if the pref file is user-specific and not a game
 * default.
 * \param base_search_path is the first path that should be checked for the file
 * \param fallback_search_path is the path that should be checked if the file
 * couldn't be found at the base path.
 * \param used_fallback will be set on return to true if the fallback path was
 * used, false otherwise.
 * \returns true if everything worked OK, false otherwise.
 */
static bool process_pref_file_layered(const char *name,
		bool quiet, bool user,
		const char *base_search_path, const char *fallback_search_path,
		bool *used_fallback)
{
	assert(base_search_path != NULL);

	char buf[4096];
	path_build(buf, sizeof(buf), base_search_path, name);

	bool fallback = false;;

	if (!file_exists(buf) && fallback_search_path != NULL) {
		fallback = true;
		path_build(buf, sizeof(buf), fallback_search_path, name);
	}

	if (used_fallback != NULL) {
		*used_fallback = fallback;
	}

	return process_pref_file_named(buf, quiet, user);
}

/**
 * Look for a pref file at its base location (falling back to another path if
 * needed) and then in the user location. This effectively will layer a user
 * pref file on top of a default pref file.
 *
 * Because of the way this function works, there might be some unexpected
 * effects when a pref file triggers another pref file to be loaded.
 * For example, pref/pref.prf causes message.prf to load. This means that the
 * game will load pref/pref.prf, then pref/message.prf, then user/message.prf,
 * and finally user/pref.prf.
 *
 * \param name is the name of the pref file.
 * \param quiet means "don't complain about not finding the file".
 * \param user should be true if the pref file is user-specific and not a game
 * default.
 * \returns true if everything worked OK, false otherwise.
 */
bool process_pref_file(const char *name, bool quiet, bool user)
{
	bool root_success = false;
	bool user_success = false;
	bool used_fallback = false;

	/* This supports the old behavior: look for a file first in "pref/",
	 * and if not found there, then "user/" */
	root_success = process_pref_file_layered(name, quiet, user,
			ANGBAND_DIR_CUSTOMIZE, ANGBAND_DIR_USER, &used_fallback);

	/* If not found, do a check of the current graphics directory */
	if (!root_success && current_graphics_mode) {
		root_success = process_pref_file_layered(name, quiet, user,
				current_graphics_mode->path, NULL, NULL);
	}

	/* Next, we want to force a check for the file in the user/ directory.
	 * However, since we used the user directory as a fallback in the previous
	 * check, we only want to do this if the fallback wasn't used. This cuts
	 * down on unnecessary parsing. */
	if (!used_fallback) {
		/* Force quiet (since this is an optional file) and force user
		 * (since this should always be considered user-specific). */
		user_success = process_pref_file_layered(name, true, true,
				ANGBAND_DIR_USER, NULL, NULL);
	}

	/* If only one load was successful, that's okay; we loaded something. */
	return root_success || user_success;
}

/**
 * Reset the "visual" lists
 *
 * This involves resetting various things to their "default" state.
 *
 * If the "prefs" flag is true, then we will also load the appropriate
 * "user pref file" based on the current setting of the "use_graphics"
 * flag.  This is useful for switching "graphics" on/off.
 */
void reset_visuals(bool load_prefs)
{
	/* Extract default attr/char code for features */
	for (int i = 0; i < z_info->f_max; i++) {
		struct feature *feat = &f_info[i];

		/* Assume we will use the underlying values */
		for (int l = 0; l < LIGHTING_MAX; l++) {
			feat_x_attr[l][i] = feat->d_attr;
			feat_x_char[l][i] = feat->d_char;
		}
	}

	/* Extract default attr/char code for objects */
	for (int i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		/* Default attr/char */
		kind_x_attr[i] = kind->d_attr;
		kind_x_char[i] = kind->d_char;
	}

	/* Extract default attr/char code for monsters */
	for (int i = 0; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];

		/* Default attr/char */
		monster_x_attr[i] = race->d_attr;
		monster_x_char[i] = race->d_char;
	}

	/* Extract default attr/char code for traps */
	for (int i = 0; i < z_info->trap_max; i++) {
		struct trap_kind *trap = &trap_info[i];

		/* Default attr/char */
		for (int l = 0; l < LIGHTING_MAX; l++) {
			trap_x_attr[l][i] = trap->d_attr;
			trap_x_char[l][i] = trap->d_char;
		}
	}

	/* Extract default attr/char code for flavors */
	for (struct flavor *f = flavors; f; f = f->next) {
		flavor_x_attr[f->fidx] = f->d_attr;
		flavor_x_char[f->fidx] = f->d_char;
	}

	if (!load_prefs) {
		return;
	}

	/* Graphic symbols */
	if (use_graphics) {
		/* if we have a graphics mode, see if the mode has a pref file name */
		graphics_mode *mode = get_graphics_mode(use_graphics);
		assert(mode);

		/* Build path to the pref file */
		char buf[4096];
		path_build(buf, sizeof(buf), mode->path, mode->pref);
		process_pref_file_named(buf, false, false);
	} else {
		/* Normal symbols */
		process_pref_file("font.prf", false, false);
	}
}

/**
 * Initialise the glyphs for monsters, objects, traps, flavors and terrain
 */
void textui_prefs_init(void)
{
	monster_x_attr = mem_zalloc(z_info->r_max * sizeof(*monster_x_attr));
	monster_x_char = mem_zalloc(z_info->r_max * sizeof(*monster_x_char));

	kind_x_attr = mem_zalloc(z_info->k_max * sizeof(*kind_x_attr));
	kind_x_char = mem_zalloc(z_info->k_max * sizeof(*kind_x_char));

	assert(N_ELEMENTS(feat_x_attr) == LIGHTING_MAX);
	assert(N_ELEMENTS(feat_x_char) == LIGHTING_MAX);

	for (int i = 0; i < LIGHTING_MAX; i++) {
		feat_x_attr[i] = mem_zalloc(z_info->f_max * sizeof(feat_x_attr[0][0]));
		feat_x_char[i] = mem_zalloc(z_info->f_max * sizeof(feat_x_char[0][0]));
	}

	assert(N_ELEMENTS(trap_x_attr) == LIGHTING_MAX);
	assert(N_ELEMENTS(trap_x_char) == LIGHTING_MAX);

	for (int i = 0; i < LIGHTING_MAX; i++) {
		trap_x_attr[i] = mem_zalloc(z_info->trap_max * sizeof(trap_x_attr[0][0]));
		trap_x_char[i] = mem_zalloc(z_info->trap_max * sizeof(trap_x_char[0][0]));
	}

	for (struct flavor *f = flavors; f; f = f->next) {
		if (flavor_max < f->fidx) {
			flavor_max = f->fidx;
		}
	}
	flavor_x_attr = mem_zalloc((flavor_max + 1) * sizeof(*flavor_x_attr));
	flavor_x_char = mem_zalloc((flavor_max + 1) * sizeof(*flavor_x_char));

	reset_visuals(false);
}

/**
 * Free the glyph arrays for monsters, objects, traps, flavors and terrain
 */
void textui_prefs_free(void)
{
	mem_free(monster_x_attr);
	mem_free(monster_x_char);

	mem_free(kind_x_attr);
	mem_free(kind_x_char);

	for (int i = 0; i < LIGHTING_MAX; i++) {
		mem_free(feat_x_attr[i]);
		mem_free(feat_x_char[i]);
	}

	for (int i = 0; i < LIGHTING_MAX; i++) {
		mem_free(trap_x_attr[i]);
		mem_free(trap_x_char[i]);
	}

	mem_free(flavor_x_attr);
	mem_free(flavor_x_char);
}

/**
 * Ask for a user pref line and process it
 */
void do_cmd_pref(void)
{
	char buf[80] = {0};

	if (get_string("Pref: ", buf, sizeof(buf))) {
		process_pref_file_command(buf);
	}
}
