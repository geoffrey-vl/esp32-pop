/* Minimal SDL_image shim for SDLPoP on ESP32. PNG overrides are not supported;
   these are declared so the code compiles and stubbed to fail gracefully. */
#ifndef POP_SDL_IMAGE_SHIM_H
#define POP_SDL_IMAGE_SHIM_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *IMG_Load(const char *file);
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
const char  *IMG_GetError(void);

#ifdef __cplusplus
}
#endif

#endif /* POP_SDL_IMAGE_SHIM_H */
