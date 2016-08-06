/**
 * \file ui2-player.h
 * \brief character info
 */

#ifndef UI2_PLAYER_H
#define UI2_PLAYER_H

void display_player_stat_info(void);
void display_player_xtra_info(void);
void display_player(int mode);
bool dump_save(const char *path);
void do_cmd_change_name(void);

#endif /* UI2_PLAYER_H */
