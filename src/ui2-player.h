/**
 * \file ui2-player.h
 * \brief character info
 */

#ifndef UI2_PLAYER_H
#define UI2_PLAYER_H

enum player_display_mode {
	/* Standard display with skills/history */
	PLAYER_DISPLAY_MODE_STANDARD,
	/* Special display with equipment flags */
	PLAYER_DISPLAY_MODE_SPECIAL
};

void display_player_stat_info(void);
void display_player_xtra_info(void);
void display_player(enum player_display_mode mode);
bool dump_save(const char *path);
void do_cmd_change_name(void);

#endif /* UI2_PLAYER_H */
