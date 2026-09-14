/*
 * Mini-SDL implementation backing shim/SDL.h for SDLPoP on the ESP32.
 *
 * Phase P0 scope: make everything compile and link, with real timing and real
 * (plain-RAM) surfaces so SDLPoP's image decoders, fonts and timers work. All
 * window/renderer/texture/audio/controller/event functions are no-op stubs that
 * return benign values (dummy non-NULL handles where the caller null-checks).
 * Later phases replace SDL_CreateRGBSurface (8-bit indexed), SDL_BlitSurface
 * (indexed blit) and SDL_UpdateTexture (LCD band present) with the real thing.
 */
#include "SDL.h"
#include "SDL_image.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* ------------------------------------------------------------------ core */
int         SDL_Init(Uint32 flags) { (void)flags; return 0; }
int         SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void        SDL_Quit(void) {}
const char *SDL_GetError(void) { return ""; }
void        SDL_free(void *mem) { free(mem); }
int         SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; return 1; }
size_t      SDL_strlen(const char *str) { return strlen(str); }
void        SDL_GetVersion(SDL_version *ver) { if (ver) SDL_VERSION(ver); }

/* --------------------------------------------------------------- timing */
Uint32 SDL_GetTicks(void) { return (Uint32)(esp_timer_get_time() / 1000); }
Uint64 SDL_GetPerformanceCounter(void) { return (Uint64)esp_timer_get_time(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000ULL; } /* esp_timer is microseconds */

void SDL_Delay(Uint32 ms)
{
    if (ms == 0) {
        taskYIELD();
        return;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param)
{
    (void)interval; (void)callback; (void)param;
    return 1; /* non-zero = "success"; the game only uses this for sound timing */
}
SDL_bool SDL_RemoveTimer(SDL_TimerID id) { (void)id; return SDL_TRUE; }

/* -------------------------------------------------------------- surfaces */
static int bytes_per_pixel_for_depth(int depth)
{
    switch (depth) {
        case 8:  return 1;
        case 24: return 3;
        case 32: return 4;
        default: return (depth + 7) / 8;
    }
}

static Uint32 format_for_depth(int depth)
{
    switch (depth) {
        case 8:  return SDL_PIXELFORMAT_INDEX8;
        case 24: return SDL_PIXELFORMAT_RGB24;
        case 32: return SDL_PIXELFORMAT_ARGB8888;
        default: return 0;
    }
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    (void)flags;
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (!s || !f) { free(s); free(f); return NULL; }

    int bpp = bytes_per_pixel_for_depth(depth);
    int pitch = (width * bpp + 3) & ~3; /* 4-byte aligned, like SDL */

    f->format = format_for_depth(depth);
    f->BitsPerPixel = (Uint8)depth;
    f->BytesPerPixel = (Uint8)bpp;
    f->Rmask = Rmask; f->Gmask = Gmask; f->Bmask = Bmask; f->Amask = Amask;
    f->palette = NULL;

    if (depth == 8) {
        SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
        if (!pal) { free(s); free(f); return NULL; }
        pal->ncolors = 256;
        pal->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
        if (!pal->colors) { free(pal); free(s); free(f); return NULL; }
        f->palette = pal;
    }

    s->format = f;
    s->w = width;
    s->h = height;
    s->pitch = pitch;
    s->pixels = calloc(1, (size_t)pitch * (height > 0 ? height : 1));
    if (!s->pixels) {
        if (f->palette) { free(f->palette->colors); free(f->palette); }
        free(f); free(s);
        return NULL;
    }
    s->clip_rect.x = 0; s->clip_rect.y = 0; s->clip_rect.w = width; s->clip_rect.h = height;
    s->refcount = 1;
    s->has_colorkey = SDL_FALSE;
    s->colorkey = 0;
    return s;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface->format) {
        if (surface->format->palette) {
            free(surface->format->palette->colors);
            free(surface->format->palette);
        }
        free(surface->format);
    }
    free(surface->pixels);
    free(surface);
}

int  SDL_LockSurface(SDL_Surface *surface) { (void)surface; return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) { (void)surface; }

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (!surface) return -1;
    surface->has_colorkey = flag ? SDL_TRUE : SDL_FALSE;
    surface->colorkey = key;
    return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *s, Uint8 a) { (void)s; (void)a; return 0; }
int SDL_SetSurfaceBlendMode(SDL_Surface *s, int m) { (void)s; (void)m; return 0; }

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (!surface || !surface->format) return -1;
    surface->format->palette = palette;
    return 0;
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors)
{
    if (!palette || !palette->colors) return -1;
    for (int i = 0; i < ncolors && (firstcolor + i) < palette->ncolors; i++) {
        palette->colors[firstcolor + i] = colors[i];
    }
    return 0;
}

int SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    if (!surface) return 0;
    if (rect) surface->clip_rect = *rect;
    else { surface->clip_rect.x = 0; surface->clip_rect.y = 0; surface->clip_rect.w = surface->w; surface->clip_rect.h = surface->h; }
    return 1;
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    if (!dst || !dst->pixels || !dst->format) return -1;
    int bpp = dst->format->BytesPerPixel;
    int x0 = 0, y0 = 0, x1 = dst->w, y1 = dst->h;
    if (rect) { x0 = rect->x; y0 = rect->y; x1 = rect->x + rect->w; y1 = rect->y + rect->h; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    for (int y = y0; y < y1; y++) {
        Uint8 *row = (Uint8 *)dst->pixels + (size_t)y * dst->pitch;
        for (int x = x0; x < x1; x++) {
            Uint8 *px = row + (size_t)x * bpp;
            switch (bpp) {
                case 1: px[0] = (Uint8)color; break;
                case 3: px[0] = color & 0xff; px[1] = (color >> 8) & 0xff; px[2] = (color >> 16) & 0xff; break;
                case 4: *(Uint32 *)px = color; break;
                default: break;
            }
        }
    }
    return 0;
}

/* P0: blits are no-ops (nothing is displayed yet). Real indexed blit lands in P3. */
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    (void)src; (void)srcrect; (void)dst; (void)dstrect;
    return 0;
}
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    (void)src; (void)srcrect; (void)dst; (void)dstrect;
    return 0;
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags)
{
    (void)flags;
    if (!src || !src->format) return NULL;
    int depth = fmt ? fmt->BitsPerPixel : src->format->BitsPerPixel;
    SDL_Surface *dst = SDL_CreateRGBSurface(0, src->w, src->h, depth, 0, 0, 0, 0);
    if (!dst) return NULL;
    size_t bytes = (size_t)src->pitch * src->h;
    if (dst->pitch == src->pitch) memcpy(dst->pixels, src->pixels, bytes);
    if (depth == 8 && src->format->palette && dst->format->palette) {
        int n = src->format->palette->ncolors;
        if (n > dst->format->palette->ncolors) n = dst->format->palette->ncolors;
        memcpy(dst->format->palette->colors, src->format->palette->colors, (size_t)n * sizeof(SDL_Color));
    }
    dst->has_colorkey = src->has_colorkey;
    dst->colorkey = src->colorkey;
    return dst;
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags)
{
    (void)pixel_format;
    return SDL_ConvertSurface(src, src ? src->format : NULL, flags);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    (void)format;
    return ((Uint32)r) | ((Uint32)g << 8) | ((Uint32)b << 16);
}
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void)format;
    return ((Uint32)r) | ((Uint32)g << 8) | ((Uint32)b << 16) | ((Uint32)a << 24);
}
const char *SDL_GetPixelFormatName(Uint32 format) { (void)format; return "SHIM"; }

/* ------------------------------------------ window / renderer / texture */
static int g_dummy_window;
static int g_dummy_renderer;
static int g_dummy_texture;

SDL_Window *SDL_CreateWindow(const char *t, int x, int y, int w, int h, Uint32 f)
{ (void)t; (void)x; (void)y; (void)w; (void)h; (void)f; return (SDL_Window *)&g_dummy_window; }
Uint32 SDL_GetWindowFlags(SDL_Window *w) { (void)w; return 0; }
void   SDL_GetWindowSize(SDL_Window *w, int *ow, int *oh) { (void)w; if (ow) *ow = 320; if (oh) *oh = 200; }
void   SDL_SetWindowTitle(SDL_Window *w, const char *t) { (void)w; (void)t; }
void   SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *i) { (void)w; (void)i; }
int    SDL_SetWindowFullscreen(SDL_Window *w, Uint32 f) { (void)w; (void)f; return 0; }
void   SDL_GL_GetDrawableSize(SDL_Window *w, int *ow, int *oh) { (void)w; if (ow) *ow = 320; if (oh) *oh = 200; }

SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f)
{ (void)w; (void)i; (void)f; return (SDL_Renderer *)&g_dummy_renderer; }
int SDL_GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info)
{
    (void)r;
    if (info) {
        info->name = "shim";
        info->flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
        info->num_texture_formats = 0;
        info->max_texture_width = 4096;
        info->max_texture_height = 4096;
    }
    return 0;
}
int  SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h) { (void)r; if (w) *w = 320; if (h) *h = 200; return 0; }
int  SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }
int  SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d)
{ (void)r; (void)t; (void)s; (void)d; return 0; }
void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
int  SDL_RenderSetIntegerScale(SDL_Renderer *r, SDL_bool e) { (void)r; (void)e; return 0; }
int  SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h) { (void)r; (void)w; (void)h; return 0; }
void SDL_RenderGetLogicalSize(SDL_Renderer *r, int *w, int *h) { (void)r; if (w) *w = 320; if (h) *h = 200; }
int  SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) { (void)r; (void)t; return 0; }
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int a, int w, int h)
{ (void)r; (void)fmt; (void)a; (void)w; (void)h; return (SDL_Texture *)&g_dummy_texture; }
int  SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch)
{ (void)t; (void)r; (void)p; (void)pitch; return 0; } /* P4: becomes the LCD present */
int  SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}
void SDL_SetTextInputRect(SDL_Rect *rect) { (void)rect; }

/* ------------------------------------------------------- events / input */
static Uint8 g_keystate[SDL_NUM_SCANCODES];

int SDL_PollEvent(SDL_Event *event) { (void)event; return 0; }
int SDL_PushEvent(SDL_Event *event) { (void)event; return 0; }
const Uint8 *SDL_GetKeyboardState(int *numkeys) { if (numkeys) *numkeys = SDL_NUM_SCANCODES; return g_keystate; }

/* --------------------------------------------- controller / joystick */
int  SDL_NumJoysticks(void) { return 0; }
SDL_bool SDL_IsGameController(int i) { (void)i; return SDL_FALSE; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void)i; return NULL; }
void SDL_GameControllerClose(SDL_GameController *g) { (void)g; }
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID id) { (void)id; return NULL; }
int  SDL_GameControllerAddMappingsFromFile(const char *f) { (void)f; return 0; }
int  SDL_GameControllerRumble(SDL_GameController *g, Uint16 l, Uint16 h, Uint32 d) { (void)g; (void)l; (void)h; (void)d; return 0; }
SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return NULL; }
int  SDL_JoystickRumble(SDL_Joystick *j, Uint16 l, Uint16 h, Uint32 d) { (void)j; (void)l; (void)h; (void)d; return 0; }
SDL_Haptic *SDL_HapticOpen(int i) { (void)i; return NULL; }
int  SDL_HapticRumbleInit(SDL_Haptic *h) { (void)h; return 0; }
int  SDL_HapticRumblePlay(SDL_Haptic *h, float s, Uint32 l) { (void)h; (void)s; (void)l; return 0; }

/* ---------------------------------------------------------------- audio */
int  SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{ if (obtained && desired) *obtained = *desired; return 0; }
void SDL_CloseAudio(void) {}
void SDL_PauseAudio(int p) { (void)p; }
void SDL_LockAudio(void) {}
void SDL_UnlockAudio(void) {}
SDL_AudioStatus SDL_GetAudioStatus(void) { return SDL_AUDIO_STOPPED; }
int  SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat sf, Uint8 sc, int sr,
                       SDL_AudioFormat df, Uint8 dc, int dr)
{ (void)sf; (void)sc; (void)sr; (void)df; (void)dc; (void)dr; if (cvt) { cvt->needed = 0; cvt->len_mult = 1; cvt->len_ratio = 1.0; } return 0; }
int  SDL_ConvertAudio(SDL_AudioCVT *cvt) { if (cvt) cvt->len_cvt = cvt->len; return 0; }

/* ---------------------------------------------------------------- RWops */
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) { (void)mem; (void)size; return NULL; }
int        SDL_RWclose(SDL_RWops *ctx) { (void)ctx; return 0; }

/* ------------------------------------------------------------ SDL_image */
SDL_Surface *IMG_Load(const char *file) { (void)file; return NULL; }
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }
const char  *IMG_GetError(void) { return ""; }
