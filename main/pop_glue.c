/*
 * pop_glue.c - ESP32 port glue between app_main (FreeRTOS/ESP-IDF world) and the
 * SDLPoP engine. This translation unit is the only place that includes the
 * SDLPoP umbrella header, so the rest of the firmware never has to deal with the
 * engine's `word`/`byte`/`far` macros. It exposes plain-int accessors so a
 * monitor task can observe game state without pulling in engine types.
 *
 * NOTE: use the sdlpop/ prefixed path so we don't accidentally pick up PR's
 * unrelated prince_dat/include/common.h, which precedes sdlpop/ in the search
 * path. The seg*.c files avoid this because they include their sibling header.
 */
#include "sdlpop/common.h"

/* Engine entry point (seg000.c). Runs the whole game; does not return. */
void pop_main(void);

int  pop_kid_x(void)        { return Kid.x; }
int  pop_kid_y(void)        { return Kid.y; }
int  pop_kid_frame(void)    { return Kid.frame; }
int  pop_kid_room(void)     { return Kid.room; }
int  pop_kid_alive(void)    { return Kid.alive; }
int  pop_current_level(void){ return current_level; }
