/**
 * \file ui2-player.h
 * \brief character info
 */

#ifndef UI2_PLAYER_H
#define UI2_PLAYER_H

enum player_display_mode {
	/* For use in the birth screen */
	PLAYER_DISPLAY_MODE_BIRTH,
	/* For use in the death screen */
	PLAYER_DISPLAY_MODE_DEATH,
	/* Standard in-game display with skills/history */
	PLAYER_DISPLAY_MODE_BASIC,
	/* Special display with equipment flags */
	PLAYER_DISPLAY_MODE_EXTRA,

	PLAYER_DISPLAY_MODE_MAX
};

void display_player(enum player_display_mode mode);
bool dump_save(const char *path);
void do_cmd_view_char(void);

#endif /* UI2_PLAYER_H */
