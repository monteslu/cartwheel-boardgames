/* crokinole's sound (sound.c) */
#ifndef CRK_SOUND_H
#define CRK_SOUND_H
#include "game.h"

void sound_init(void);
void sound_menu(int kind); /* 0 move, 1 select, 2 back */
void sound_events(const game_events *ev, float cam_angle);
void sound_slide(float speed); /* the fastest disc's speed, m/s (0: quiet) */
void sound_silence(void);

#endif
