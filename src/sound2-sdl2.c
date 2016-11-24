/**
 * \file sound2-sdl2.c
 * \brief SDL2 sound support (probably works with SDL1, too)
 *
 * Copyright (c) 2004 Brendon Oliver <brendon.oliver@gmail.com>
 * Copyright (c) 2007 Andi Sidwell <andi@takkaria.org>
 * Copyright (c) 2016 Graeme Russ <graeme.russ@gmail.com>
 * A large chunk of this file was taken and modified from main-ros.
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
#include "sound.h"

#include <SDL.h>
#include <SDL_mixer.h>

/* SDL can be initialized by main SDL frontend,
 * or this module can be running standalone,
 * in which case it should close SDL */
#define SHOULD_CLOSE_SDL \
	((SDL_WasInit(0) & SDL_INIT_VIDEO) == 0)

/**
 * Kind of sound (as per SDL API)
 */
enum sample_type {
	SAMPLE_NONE = 0,
	SAMPLE_CHUNK,
	SAMPLE_MUSIC,
};

struct sample {
	union {
		Mix_Chunk *chunk;
		Mix_Music *music;
	} data;
	enum sample_type type;
};

/**
 * Supported sound formats
 */
enum sound_file {
	SOUND_FILE_NONE = 0,
	SOUND_FILE_MP3  = MIX_INIT_MP3,
	SOUND_FILE_OGG  = MIX_INIT_OGG,
};

static const struct sound_file_type supported_sound_files[] = {
	{".mp3", SOUND_FILE_MP3},
	{".ogg", SOUND_FILE_OGG},
	{"",     SOUND_FILE_NONE}
};

/* SDL2_mixer 2.0.1 added ability to load
 * MP3s as chunks (instead of just music) */
static bool can_load_mp3_as_chunk(void)
{
	const struct SDL_version *version = Mix_Linked_Version();

	return version->major >= 2
		&& (version->minor > 0 || version->patch > 0);
}

/**
 * Initialise SDL and open the mixer.
 */
static bool open_audio_sdl(void)
{
	const int audio_rate = MIX_DEFAULT_FREQUENCY;
	const Uint16 audio_format = MIX_DEFAULT_FORMAT;
	const int audio_channels = 2; /* stereo */
	const int sample_size = 1024; /* bytes per sample */

	int mix_flags = 0;
	for (size_t i = 0; i < N_ELEMENTS(supported_sound_files); i++) {
		mix_flags |= supported_sound_files[i].type;
	}

	/* Initialize main SDL library */
	if (SDL_Init(SDL_INIT_AUDIO) != 0) {
		plog_fmt("Couldn't initialize SDL: %s", SDL_GetError());
		return false;
	}

	/* Initialize SDL_mixer library */
	if (Mix_Init(mix_flags) == 0) {
		plog_fmt("Couldn't initialize sound: %s", Mix_GetError());
		return false;
	}

	/* Try to open sound device */
	if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, sample_size) == -1) {
		plog_fmt("Couldn't open sound device: %s", Mix_GetError());
		return false;
	}

	return true;
}

static struct sample *sample_new(void)
{
	struct sample *sample = mem_zalloc(sizeof(*sample));

	sample->data.chunk = NULL;
	sample->data.music = NULL;

	sample->type = SAMPLE_NONE;

	return sample;
}

static void sample_free(struct sample *sample)
{
	if (Mix_PlayingMusic() > 0) {
		Mix_HaltMusic();
	}

	if (Mix_Playing(-1) > 0) {
		Mix_HaltChannel(-1);
	}

	switch (sample->type) {
		case SAMPLE_CHUNK:
			if (sample->type == SAMPLE_CHUNK) {
				 Mix_FreeChunk(sample->data.chunk);
			}
			break;

		case SAMPLE_MUSIC:
			if (sample->type == SAMPLE_MUSIC) {
				Mix_FreeMusic(sample->data.music);
			}
			break;

		case SAMPLE_NONE:
			break;
	}

	mem_free(sample);
}

static bool load_sample_wav(struct sample *sample, const char *filename)
{
	assert(sample->type == SAMPLE_NONE);

	sample->data.chunk = Mix_LoadWAV(filename);

	if (sample->data.chunk != NULL) {
		sample->type = SAMPLE_CHUNK;
		return true;
	} else {
		plog_fmt("Couldn't load sound chunk from '%s': %s",
				filename, Mix_GetError());
		return false;
	}
}

static bool load_sample_mus(struct sample *sample, const char *filename)
{
	assert(sample->type == SAMPLE_NONE);

	sample->data.music = Mix_LoadMUS(filename);

	if (sample->data.chunk != NULL) {
		sample->type = SAMPLE_MUSIC;
		return true;
	} else {
		plog_fmt("Couldn't load sound sample from '%s': %s",
				filename, Mix_GetError());
		return false;
	}
}

/**
 * Load a sound from file.
 */
static bool load_sample_sdl(const char *filename, int file_type, struct sample *sample)
{
	bool loaded = false;

	switch (file_type) {
		case SOUND_FILE_OGG:
			loaded = load_sample_wav(sample, filename);
			break;

		case SOUND_FILE_MP3:
			if (can_load_mp3_as_chunk()) {
				loaded = load_sample_wav(sample, filename);
			} else {
				loaded = load_sample_mus(sample, filename);
			}
			break;

		default:
			plog("Unsupported sound file");
			break;
	}

	return loaded;
}

/**
 * Load a sound and return a pointer to the associated
 * sound data structure back to the core sound module.
 */
static bool load_sound_sdl(const char *filename, int file_type, struct sound_data *data)
{
	struct sample *sample = data->plat_data;

	if (sample == NULL) {
		sample = sample_new();
	}

	if (load_sample_sdl(filename, file_type, sample)) {
		data->plat_data = sample;
		data->loaded = true;
		return true;
	} else {
		sample_free(sample);
		return false;
	}
}

/**
 * Play the sound stored in the provided SDL Sound data structure.
 */
static bool play_sound_sdl(struct sound_data *data)
{
	struct sample *sample = data->plat_data;

	if (sample != NULL) {
		switch (sample->type) {
			case SAMPLE_CHUNK:
				return Mix_PlayChannel(-1, sample->data.chunk, 0) == 0;

			case SAMPLE_MUSIC:
				return Mix_PlayMusic(sample->data.music, 1) == 0;

			default:
				return false;
		}
	} else {
		return false;
	}
}

/**
 * Free resources referenced in the provided SDL Sound data structure.
 */
static bool unload_sound_sdl(struct sound_data *data)
{
	if (data->loaded) {
		sample_free(data->plat_data);
		data->plat_data = NULL;
		data->loaded = false;
	}

	return true;
}

/**
 * Shut down the SDL sound module and free resources.
 */
static bool close_audio_sdl(void)
{
	Mix_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	if (SHOULD_CLOSE_SDL) {
		SDL_Quit();
	}

	return true;
}

const struct sound_file_type *supported_files_sdl(void)
{
	return supported_sound_files;
}

/**
 * Init the SDL sound module.
 */
errr init_sound_sdl(struct sound_hooks *hooks, int argc, char **argv)
{
	(void) argc;
	(void) argv;

	hooks->open_audio_hook = open_audio_sdl;
	hooks->supported_files_hook = supported_files_sdl;
	hooks->close_audio_hook = close_audio_sdl;
	hooks->load_sound_hook = load_sound_sdl;
	hooks->unload_sound_hook = unload_sound_sdl;
	hooks->play_sound_hook = play_sound_sdl;

	return 0;
}
