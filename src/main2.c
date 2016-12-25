/**
 * \file main.c
 * \brief Core game initialisation for UNIX (and other) machines
 *
 * Copyright (c) 1997 Ben Harrison, and others
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
#include "init.h"
#include "savefile.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-init.h"
#include "ui2-input.h"
#include "ui2-prefs.h"

#ifdef WINDOWS
#include <windows.h>
#endif

#ifdef SOUND
#include "sound.h"
#endif

/**
 * Angband requires UTF-8
 */
#include "locale.h"

#ifndef WINDOWS
#include "langinfo.h"
#endif

/**
 * Some machines can have a main() function in their
 * main-xxx.c file, all the others use this file
 * for their main() function.
 */

#include "main.h"

/**
 * List of the available modules in the order they are tried.
 */
static const struct module modules[] =
{
#ifdef USE_SDL2
    { "sdl2", help_sdl2, init_sdl2 },
#endif

#ifdef USE_NCURSES
    { "ncurses", help_ncurses, init_ncurses },
#endif

#ifdef USE_TEST
	{ "test", help_test, init_test },
#endif

#ifdef USE_STATS
	{ "stats", help_stats, init_stats },
#endif
};

/**
 * A hook for quit().
 *
 * We use that as a default value of quit_aux();
 * most frontends would want to install their own
 * quit_aux(), but if not, this one will be called.
 */
static void quit_hook(const char *s)
{
	(void) s;
}

/**
 * Windows cannot naturally handle UTF-8 using the standard locale
 * and C library routines, such as mbstowcs().
 *
 * We assume external files are in UTF-8, and explicitly convert.
 *
 * MultiByteToWideChar returns number of wchars, including terminating L'\0';
 * mbstowcs requires the count without the terminating L'\0'
 * dest == NULL corresponds to querying for the size needed, achieved in the
 * Windows function by setting the dstlen (last) param to 0.
 * If n is too small for all the chars in src, the Windows function fails, but
 * we require success and a partial conversion. So allocate space for it
 * to succeed, and do the partial copy into dest.
 */
#ifdef WINDOWS
size_t mbstowcs_windows(wchar_t *dest, const char *src, int n)
{
	int result = MultiByteToWideChar(CP_UTF8,
			MB_ERR_INVALID_CHARS, src, -1, dest, dest == NULL ? 0 : n);

	if (result > 0) {
		/* mbstowcs() returns length
		 * without the terminating null */
		result -= 1;
	} else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		int required_n = MultiByteToWideChar(CP_UTF8,
				MB_ERR_INVALID_CHARS, src, -1, NULL, 0);

		wchar_t *required_buf =
			mem_alloc(required_n * sizeof(*required_buf));

		result = MultiByteToWideChar(CP_UTF8,
				MB_ERR_INVALID_CHARS, src, -1, required_buf, required_n);

		if (result > 0) {
			memcpy(dest, required_buf, n * sizeof(*dest));
			result = n;
		} else {
			result = -1;
		}

		mem_free(required_buf);
	} else {
		/* on error, mbstowcs()
		 * returns (size_t) -1 */
		result = -1;
	}

	return result;
}
#endif /* WINDOWS */

/**
 * Initialize and verify the file paths, and the score file.
 *
 * Use the ANGBAND_PATH environment var if possible, else use
 * DEFAULT_PATH, and in either case, branch off appropriately.
 *
 * First, we'll look for the ANGBAND_PATH environment variable,
 * and then look for the files in there.  If that doesn't work,
 * we'll try the DEFAULT_PATH constants.  So be sure that one of
 * these two things works...
 *
 * We must ensure that the path ends with "PATH_SEP" if needed,
 * since the "init_file_paths()" function will simply append the
 * relevant "sub-directory names" to the given path.
 *
 * Make sure that the path doesn't overflow the buffer.  We have
 * to leave enough space for the path separator, directory, and
 * filenames.
 */
static void init_paths(void)
{
	char libpath[512];
	char datapath[512];
	char configpath[512];

	/* Use the angband_path, or a default */
	my_strcpy(libpath,    DEFAULT_LIB_PATH,    sizeof(libpath));
	my_strcpy(datapath,   DEFAULT_DATA_PATH,   sizeof(datapath));
	my_strcpy(configpath, DEFAULT_CONFIG_PATH, sizeof(configpath));

	/* Make sure they're terminated */
	libpath[sizeof(libpath) - 1]       = '\0';
	datapath[sizeof(datapath) - 1]     = '\0';
	configpath[sizeof(configpath) - 1] = '\0';

	/* Hack - add a path separator (only if needed) */
	if (!suffix(libpath, PATH_SEP)) {
		my_strcat(libpath, PATH_SEP, sizeof(libpath));
	}
	if (!suffix(datapath, PATH_SEP)) {
		my_strcat(datapath, PATH_SEP, sizeof(datapath));
	}
	if (!suffix(configpath, PATH_SEP)) {
		my_strcat(configpath, PATH_SEP, sizeof(configpath));
	}

	init_file_paths(configpath, libpath, datapath);
}

static const struct {
	const char *name;
	char **path;
	bool setgid_ok;
} change_path_values[] = {
	{ "scores",   &ANGBAND_DIR_SCORES,    true },
	{ "gamedata", &ANGBAND_DIR_GAMEDATA,  false },
	{ "screens",  &ANGBAND_DIR_SCREENS,   false },
	{ "help",     &ANGBAND_DIR_HELP,      true },
	{ "info",     &ANGBAND_DIR_INFO,      true },
	{ "pref",     &ANGBAND_DIR_CUSTOMIZE, true },
	{ "fonts",    &ANGBAND_DIR_FONTS,     true },
	{ "tiles",    &ANGBAND_DIR_TILES,     true },
	{ "sounds",   &ANGBAND_DIR_SOUNDS,    true },
	{ "icons",    &ANGBAND_DIR_ICONS,     true },
	{ "user",     &ANGBAND_DIR_USER,      true },
	{ "save",     &ANGBAND_DIR_SAVE,      false },
};

/**
 * Handle a "-d<dirname>=<dirpath>" option.
 *
 * Sets any of angband's special directories to <dirpath>.
 *
 * The "<dirpath>" can be any legal path for the given system, and should
 * not end in any special path separator (i.e. "/tmp" or "~/.ang-info").
 */
#ifdef UNIX
static void change_path(const char *info)
{
	if (info == NULL || info[0] == 0) {
		quit_fmt("Try '-d<dir>=<path>'");
	}

	char *info_copy = string_make(info);
	char *dirname = strtok(info_copy, "=");
	char *dirpath = strtok(NULL, "=");

	bool changed = false;

	for (size_t i = 0; i < N_ELEMENTS(change_path_values) && !changed; i++) {
		if (my_stricmp(dirname, change_path_values[i].name) == 0) {

#ifdef SETGID
			if (!change_path_values[i].setgid_ok) {
				quit_fmt("Can't redefine path to %s dir on multiuser setup",
						 dirname);
			}
#endif

			string_free(*change_path_values[i].path);
			*change_path_values[i].path = string_make(dirpath);

			/* the directory may not exist and may need to be created. */
			char newpath[512];
			path_build(newpath, sizeof(newpath), dirpath, "");
			if (!dir_create(newpath)) {
				quit_fmt("Cannot create '%s'", newpath);
			}

			changed = true;
		}
	}

	mem_free(info_copy);

	if (!changed) {
		quit_fmt("Unrecognised -d paramater %s", dirname);
	}
}
#endif /* UNIX */

/**
 * Find a default user name from the system.
 */
#ifdef UNIX
static void user_name(char *buf, size_t len, int id)
{
	struct passwd *pw = getpwuid(id);

	if (!pw || !pw->pw_name || !pw->pw_name[0] ) {
		/* Default to PLAYER */
		my_strcpy(buf, "PLAYER", len);
	} else {
		/* Copy and capitalise */
		my_strcpy(buf, pw->pw_name, len);
		my_strcap(buf);
	}
}
#endif /* UNIX */


/**
 * List all savefiles this player can access.
 */
#ifdef UNIX
static void list_saves(void)
{
	char fname[256];
	ang_dir *d = my_dopen(ANGBAND_DIR_SAVE);

#ifdef SETGID
	char uid[10];
	strnfmt(uid, sizeof(uid), "%d.", player_uid);
#endif

	if (!d) {
		quit_fmt("Can't open savefile directory");
	}

	printf("Savefiles you can use are:\n");

	while (my_dread(d, fname, sizeof fname)) {

#ifdef SETGID
		/* Check that the savefile name begins with the user'd ID */
		if (strncmp(fname, uid, strlen(uid) == 0)) {
			continue;
		}
#endif

		char path[1024];
		path_build(path, sizeof path, ANGBAND_DIR_SAVE, fname);

		const char *desc = savefile_get_description(path);

		printf(" %-15s %s\n", fname, desc ? desc : "");
	}

	my_dclose(d);

	printf("\nUse angband -u<name> to use savefile <name>.\n");
}
#endif /* UNIX */

static bool new_game;

#ifdef UNIX
static void debug_opt(const char *arg) {
	if (streq(arg, "mem-poison-alloc")) {
		mem_flags |= MEM_POISON_ALLOC;
	} else if (streq(arg, "mem-poison-free")) {
		mem_flags |= MEM_POISON_FREE;
	} else {
		puts("Debug flags:");
		puts("  mem-poison-alloc: Poison all memory allocations");
		puts("  mem-poison-free:  Poison all freed memory");
		exit(EXIT_SUCCESS);
	}
}
#endif /* UNIX */

#ifdef UNIX
static void dump_modules_usage(void)
{
	int maxlen = 0;
	for (size_t i = 0; i < N_ELEMENTS(modules); i++) {
		int len = strlen(modules[i].name);
		if (maxlen < len) {
			maxlen = len;
		}
	}

	/* Print the name and help for each available module */
	for (size_t i = 0; i < N_ELEMENTS(modules); i++) {
		printf("    %-*s %s\n", maxlen, modules[i].name, modules[i].help);
	}
}
#endif /* UNIX */

#ifdef UNIX
static void dump_dirs_usage(void)
{
	int maxlen = 0;
	for (size_t i = 0; i < N_ELEMENTS(change_path_values); i++) {
#ifdef SETGID
		if (!change_path_values[i].setgid_ok) {
			continue;
		}
#endif
		int len = strlen(change_path_values[i].name);
		if (maxlen < len) {
			maxlen = len;
		}
	}

	for (size_t i = 0; i < N_ELEMENTS(change_path_values); i++) {

#ifdef SETGID
		if (!change_path_values[i].setgid_ok) {
			continue;
		}
#endif
		printf("    %-*s (default is %s)\n",
				maxlen, change_path_values[i].name, *change_path_values[i].path);
	}
}
#endif /* UNIX */

#ifdef UNIX
static void dump_usage(void)
{
	puts("Usage: angband [options] [-- subopts]");
	puts("  -n             Start a new character (WARNING: overwrites default savefile without -u)");
	puts("  -l             Lists all savefiles you can play");
	puts("  -r             Rebalance monsters");
	puts("  -w             Resurrect dead character (marks savefile)");

	putchar('\n');
	puts("  -x<opt>        Debug options; see -xhelp");
	puts("  -u<who>        Use your <who> savefile");

	putchar('\n');
	puts("  -d<dir>=<path> Override a specific directory with <path>. <path> can be:");
	dump_dirs_usage();
	puts("                 Multiple -d options are allowed.");
	putchar('\n');

#ifdef SOUND
	puts("  -s<mod>        Use sound module <sys>:");
	print_sound_help();
#endif

	puts("  -m<sys>        Use module <sys>, where <sys> can be:");
	dump_modules_usage();
}
#endif /* UNIX */

#ifdef UNIX
static int parse_argv(int argc, char **argv,
		const char **module, const char **sound)
{
	/* Prevent getopt() from printing
	 * error messages; we have our own */
	opterr = 0;

	int opt;

	while ((opt = getopt(argc, argv, ":fhlnprwd:m:s:u:x:")) != -1) {

		switch (opt) {
			case 'f':
				arg_force_name = true;
				break;

			case 'h':
				dump_usage();
				exit(EXIT_SUCCESS);
				break;

			case 'l':
				list_saves();
				quit(NULL);
				break;

			case 'n':
				new_game = true;
				break;

			case 'w':
				arg_wizard = true;
				break;

			case 'd':
				change_path(optarg);
				break;

			case 'm':
				*module = optarg;
				break;

#ifdef SOUND
			case 's':
				*sound = optarg;
				break;
#endif

			case 'u':
				my_strcpy(arg_name, optarg, sizeof(arg_name));

				/* The difference here is because on setgid we have to be
				 * careful to only let the player have savefiles stored in
				 * the central save directory. Sanitising input using
				 * player_safe_name() removes anything like that.
				 *
				 * But if the player is running with per-user saves,
				 * they can do whatever the hell they want.
				 */

#ifdef SETGID
				savefile_set_name(optarg, true, false);
#else
				savefile_set_name(optarg, false, false);
#endif
				break;

			case 'x':
				debug_opt(optarg);
				break;

			case '?':
				printf("Unrecognized option '%c'\n\n", optopt);
				dump_usage();
				exit(EXIT_FAILURE);
				break;

			case ':':
				printf("Missing argument for option '%c'\n\n", optopt);
				dump_usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	return optind;
}
#endif /* UNIX */

void init_module(const char *display, int argc, char **argv)
{
	/* Try the modules in the order specified by modules[] */
	for (size_t i = 0; i < N_ELEMENTS(modules); i++) {
		/* if user requested a specific module, use it,
		 * otherwise use the first one that works */
		if (display == NULL || streq(display, modules[i].name)) {
			ANGBAND_SYS = modules[i].name;
			if (modules[i].init(argc, argv) == 0) {
				return;
			}
		}
	}

	/* Make sure we have a display! */
	quit("Unable to prepare any 'display module'!");
}

/**
 * Simple main() function for multiple platforms.
 * At least, it used to be simple before it started to work
 * on Windows, and then it turned into a horrible ifdef soup :)
 * It wasn't actually simple anyway...
 *
 * Note the special "--" option which terminates the processing of
 * standard options. All non-standard options (if any) are passed
 * directly to the init_xxx() function.
 */
int main(int argc, char *argv[])
{
	/* Save the program name */
	argv0 = argv[0];

#ifdef UNIX
	/* Default permissions on files */
	umask(022);
	/* Get the user id */
	player_uid = getuid();
#endif /* UNIX */

#ifdef SETGID
	/* Save the effective GID for later recall */
	player_egid = getegid();
#endif /* SETGID */

	/* Drop permissions */
	safe_setuid_drop();

	/* Get the file paths.
	 * Paths may be overriden by -d options,
	 * so this has to occur before processing argv */
	init_paths();

	/* Display is the graphics mode (e.g., ncurses) */
	const char *display = NULL;
	/* Sound is the sound module (e.g., sdl2) */
	const char *sound  = NULL;

#ifdef UNIX
	/* Process the command line arguments */
	int next = parse_argv(argc, argv, &display, &sound);

	/* We pass the rest of argv to
	 * sound and graphics modules */
	argv += next;
	argc -= next;
#else
	/* On non Unix systems (that is, on Windows)
	 * we don't support command line arguments */
	argv += argc;
	argc -= argc;
#endif

	quit_aux = quit_hook;

	if (setlocale(LC_CTYPE, "")) {
#ifndef WINDOWS
		if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
			/* UTF-8 is not optional */
			quit("Angband requires UTF-8 support");
		}
#endif
	}

#ifdef WINDOWS
	/* Normal mbstowcs() doesn't really work
	 * on Windows, so we use a custom one */
	text_mbcs_hook = mbstowcs_windows;
#endif

	/* Initialize display module */
	init_module(display, argc, argv);

#ifdef UNIX
	/* Get the user name as player's name,
	 * if it wasn't set with the -u switch */
	if (arg_name[0] == 0) {
		user_name(arg_name, sizeof(arg_name), player_uid);

		/* Sanitise name and set as savefile */
		savefile_set_name(arg_name, true, false);
	}

	/* Create any missing directories */
	create_needed_dirs();
#endif

	/* Set up the command hook */
	cmd_get_hook = textui_get_cmd;

#ifdef SOUND
	/* Initialise sound */
	init_sound(sound, argc, argv);
#else
	/* Ignore compiler warning */
	(void) sound;
#endif

	/* Initialize the game */
	init_display();
	init_angband();
	textui_init();

	/* Play the game */
	play_game(new_game);

	/* Free resources */
	textui_cleanup();
	cleanup_angband();

	/* Call quit_hook() */
	quit(NULL);

	/* quit() actually quits the game,
	 * so we shouldn't get there */
	exit(EXIT_FAILURE);
}
