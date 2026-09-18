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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "dat_registry.h"

/* ---------------------------------------------- flash-backed DAT reader ---
 * SDLPoP opens its .DAT files with fopen()/fread()/fseek(). There is no
 * filesystem on the ESP32, so we serve the DAT bytes straight out of flash:
 * every DAT is embedded via EMBED_FILES and listed in the generated
 * g_embedded_dats registry. We wrap the flash blob in a FILE* with
 * fopencookie() and our own read/seek callbacks operating on a tiny cursor
 * cookie. fopencookie is used instead of fmemopen() because ESP-IDF's libc
 * fmemopen read/seek proved unreliable across repeated opens (fseek returned
 * ENOSYS on later opens of the same file); a self-contained cookie stream is
 * fully under our control and has no such limitation. seg009.c's
 * open_dat_from_root_or_data_dir() calls this first (ESP_PLATFORM only). */
static const char *pop_basename(const char *p)
{
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} pop_dat_cookie_t;

static ssize_t pop_dat_cookie_read(void *c, char *buf, size_t n)
{
    pop_dat_cookie_t *ck = (pop_dat_cookie_t *)c;
    size_t avail = ck->size - ck->pos;
    if (n > avail) n = avail;
    memcpy(buf, ck->data + ck->pos, n);
    ck->pos += n;
    return (ssize_t)n;
}

static int pop_dat_cookie_seek(void *c, off_t *offset, int whence)
{
    pop_dat_cookie_t *ck = (pop_dat_cookie_t *)c;
    off_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (off_t)ck->pos; break;
        case SEEK_END: base = (off_t)ck->size; break;
        default: return -1;
    }
    off_t np = base + *offset;
    if (np < 0 || (size_t)np > ck->size) return -1;
    ck->pos = (size_t)np;
    *offset = np;
    return 0;
}

static int pop_dat_cookie_close(void *c)
{
    free(c);
    return 0;
}

FILE *pop_open_embedded_dat(const char *filename)
{
    if (filename == NULL) return NULL;
    const char *base = pop_basename(filename);
    for (size_t i = 0; i < g_embedded_dats_count; i++) {
        if (strcasecmp(g_embedded_dats[i].name, base) == 0) {
            pop_dat_cookie_t *ck = (pop_dat_cookie_t *)malloc(sizeof(*ck));
            if (ck == NULL) return NULL;
            ck->data = g_embedded_dats[i].start;
            ck->size = (size_t)(g_embedded_dats[i].end - g_embedded_dats[i].start);
            ck->pos = 0;
            cookie_io_functions_t io = {
                .read  = pop_dat_cookie_read,
                .write = NULL,
                .seek  = pop_dat_cookie_seek,
                .close = pop_dat_cookie_close,
            };
            FILE *fp = fopencookie(ck, "rb", io);
            if (fp == NULL) free(ck);
            return fp;
        }
    }
    return NULL;
}

/* Direct flash access to an embedded DAT: returns 1 and fills out_ptr/out_size
 * if the named DAT is embedded. The port reads DAT headers, resource tables and
 * resource bytes straight from this flash pointer (memcpy), bypassing FILE*
 * seeking entirely (picolibc's fseek on memory streams returns ENOSYS once the
 * 8-bit heap is fragmented). See open_dat / load_from_opendats_* in seg009.c. */
int pop_get_embedded_dat(const char *filename, const unsigned char **out_ptr, size_t *out_size)
{
    if (filename == NULL) return 0;
    const char *base = pop_basename(filename);
    for (size_t i = 0; i < g_embedded_dats_count; i++) {
        if (strcasecmp(g_embedded_dats[i].name, base) == 0) {
            *out_ptr = g_embedded_dats[i].start;
            *out_size = (size_t)(g_embedded_dats[i].end - g_embedded_dats[i].start);
            return 1;
        }
    }
    return 0;
}

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
/* Screen-buffer pool. SDLPoP keeps several persistent full-screen surfaces; on
 * the ESP32 each is coerced to 8-bit indexed. During gameplay two coexist:
 *   - onscreen_surface_ : 320x200 = 64000 bytes
 *   - offscreen_surface : 320x192 = 61440 bytes (make_offscreen_buffer(rect_top))
 * (overlay_surface and merged_surface alias onscreen_surface_ in init_overlay();
 * the overlay is never displayed in this port, so it costs no RAM.)
 * A late 64KB contiguous request fails on the fragmented internal heap even when
 * total free RAM is ample, so both blocks are reserved once, up front
 * (pop_screen_pool_init(), called from app_main before the heap fragments). Each
 * slot is a full 64000-byte block; the pool also satisfies the slightly smaller
 * 61440-byte offscreen buffer from a slot. Transient smaller surfaces
 * (sprites/font glyphs/small peels) use the general heap. */
#define POP_SCREEN_BYTES    64000  /* 320 * 200, 8-bit indexed */
#define POP_SCREEN_POOL_MIN 61440  /* smallest full-screen-ish buffer (320x192) */
#define POP_POOL_SLOTS      2
static uint8_t *g_pool_block[POP_POOL_SLOTS];
static bool     g_pool_used[POP_POOL_SLOTS];

void pop_screen_pool_init(void)
{
    for (int i = 0; i < POP_POOL_SLOTS; i++) {
        if (g_pool_block[i] == NULL) {
            g_pool_block[i] = (uint8_t *)heap_caps_malloc(POP_SCREEN_BYTES,
                                                          MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        }
        g_pool_used[i] = false;
        printf("pop_screen_pool_init: slot %d = %p (largest now %u)\n",
               i, (void *)g_pool_block[i],
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    }
}

static uint8_t *pool_take(void)
{
    for (int i = 0; i < POP_POOL_SLOTS; i++) {
        if (g_pool_block[i] != NULL && !g_pool_used[i]) {
            g_pool_used[i] = true;
            memset(g_pool_block[i], 0, POP_SCREEN_BYTES);
            return g_pool_block[i];
        }
    }
    return NULL;
}

static bool pool_return(void *p)
{
    for (int i = 0; i < POP_POOL_SLOTS; i++) {
        if (g_pool_block[i] == p) {
            g_pool_used[i] = false;
            return true;
        }
    }
    return false;
}

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

/* A single shared 256-color palette backs every sprite/font (non-screen) 8-bit
 * surface. Real SDL gives each 8-bit surface its own 1KB palette, but POP's
 * built-in font alone creates ~130 persistent glyph surfaces, which would cost
 * ~130KB — impossible on a device with only ~90KB of byte-addressable RAM left
 * after the screen-buffer pool. In this port the per-sprite palette is never
 * used for rendering (blits are index-preserving; the global game palette is
 * applied only at present time), so sharing one palette is safe and reclaims
 * that RAM. The 3 full-screen buffers still get dedicated 256-color palettes. */
static SDL_Palette *g_shared_sprite_palette = NULL;
/* Shared 1x1 placeholder handed out by decode_image() when a sprite cannot be
 * decoded into RAM (P2 will render sprites straight from flash instead). It is
 * global so SDL_FreeSurface() can refuse to free it. */
SDL_Surface *g_pop_sprite_placeholder = NULL;
static SDL_Palette *shared_sprite_palette(void)
{
    if (g_shared_sprite_palette == NULL) {
        SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
        if (pal) {
            pal->ncolors = 256;
            pal->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
            if (!pal->colors) { free(pal); pal = NULL; }
        }
        g_shared_sprite_palette = pal;
    }
    return g_shared_sprite_palette;
}

static SDL_Surface *create_surface_impl(int width, int height, int depth,
                                        Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    (void)flags;
    /* The ESP32 cannot hold 24/32-bit screen buffers (320x200x3 = 192KB each;
     * onscreen + overlay + merged would be ~640KB). Every 24/32-bit surface the
     * engine creates *through this entry point* is screen-derived (onscreen/
     * overlay/merged buffers and the "peel" regions that save the pixels under a
     * dialog); those are only ever blitted (index-preserving) or FillRect'd, so
     * we coerce them to 8-bit indexed to save RAM. The game palette maps to
     * RGB565 only at present time (P4). This also shrinks small peels (e.g.
     * 220x75) from ~49KB to ~16KB, which matters on the fragmented internal heap.
     *
     * NOTE: surfaces that are directly manipulated at their native depth (e.g.
     * method_3_blit_mono() writes 32-bit pixels into an ARGB8888 conversion
     * output) must NOT be coerced, or the writes overflow the buffer and corrupt
     * the heap. Those go through SDL_ConvertSurface(Format)() which calls
     * create_surface_impl() directly, bypassing this coercion. */
    if (depth == 24 || depth == 32) {
        depth = 8;
        Rmask = Gmask = Bmask = Amask = 0;
    }
    return create_surface_impl(width, height, depth, Rmask, Gmask, Bmask, Amask);
}

static SDL_Surface *create_surface_impl(int width, int height, int depth,
                                        Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (!s || !f) {
        printf("CreateRGBSurface: struct alloc failed w=%d h=%d d=%d\n", width, height, depth);
        free(s); free(f); return NULL;
    }

    int bpp = bytes_per_pixel_for_depth(depth);
    int pitch = (width * bpp + 3) & ~3; /* 4-byte aligned, like SDL */
    /* A full-screen 320x200x8 buffer is exactly POP_SCREEN_BYTES; those get a
     * dedicated palette (each may hold a different game palette). Everything
     * else (sprites/font glyphs) shares one global palette. */
    size_t est_bytes = (size_t)pitch * (size_t)(height > 0 ? height : 1);
    int is_screen = (est_bytes == POP_SCREEN_BYTES);

    f->format = format_for_depth(depth);
    f->BitsPerPixel = (Uint8)depth;
    f->BytesPerPixel = (Uint8)bpp;
    f->Rmask = Rmask; f->Gmask = Gmask; f->Bmask = Bmask; f->Amask = Amask;
    f->palette = NULL;

    if (depth == 8) {
        if (is_screen) {
            SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
            if (!pal) {
                printf("CreateRGBSurface: palette struct alloc failed w=%d h=%d free=%u largest=%u\n",
                       width, height, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                free(s); free(f); return NULL;
            }
            pal->ncolors = 256;
            pal->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
            if (!pal->colors) {
                printf("CreateRGBSurface: palette colors alloc failed w=%d h=%d free=%u largest=%u\n",
                       width, height, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                free(pal); free(s); free(f); return NULL;
            }
            f->palette = pal;
        } else {
            f->palette = shared_sprite_palette();
            if (f->palette == NULL) {
                printf("CreateRGBSurface: shared palette alloc failed w=%d h=%d free=%u largest=%u\n",
                       width, height, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                free(s); free(f); return NULL;
            }
        }
    }

    s->format = f;
    s->w = width;
    s->h = height;
    s->pitch = pitch;
    /* Some glyphs (e.g. the space character) are zero-width; SDL still returns a
     * valid surface for those. Guard against calloc(.,0) returning NULL, which
     * would be mistaken for an out-of-memory failure by callers like
     * decode_image() and trigger quit()/exit(). */
    {
        size_t nbytes = (size_t)pitch * (size_t)(height > 0 ? height : 1);
        if (nbytes == 0) nbytes = 1;
        /* Full-screen (and the slightly smaller 320x192 offscreen) buffers come
         * from the reserved pool to dodge heap fragmentation; everything else
         * uses the general heap. */
        if (nbytes >= POP_SCREEN_POOL_MIN && nbytes <= POP_SCREEN_BYTES) {
            s->pixels = pool_take();
            s->pool_backed = (s->pixels != NULL) ? SDL_TRUE : SDL_FALSE;
        }
        if (s->pixels == NULL) {
            s->pixels = calloc(1, nbytes);
        }
        if (!s->pixels) {
            printf("CreateRGBSurface: pixels alloc failed w=%d h=%d d=%d pitch=%d nbytes=%u free=%u largest=%u\n",
                   width, height, depth, pitch, (unsigned)nbytes,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        }
    }
    if (!s->pixels) {
        if (f->palette && f->palette != g_shared_sprite_palette) {
            free(f->palette->colors); free(f->palette);
        }
        free(f); free(s);
        return NULL;
    }
    s->clip_rect.x = 0; s->clip_rect.y = 0; s->clip_rect.w = width; s->clip_rect.h = height;
    s->refcount = 1;
    s->has_colorkey = SDL_FALSE;
    s->colorkey = 0;
    return s;
}

/* One static INDEX8 pixel format shared by every flash-backed sprite surface.
 * These surfaces never own their pixels (flash) or their format, so sharing one
 * format keeps per-sprite RAM down to just the SDL_Surface header (~64 bytes).
 * The palette is the shared sprite palette; it is unused for rendering (blits
 * preserve indices; the global game palette maps to RGB565 at present time), so
 * its contents don't matter for these surfaces. */
static SDL_PixelFormat g_flash_format;
static SDL_bool g_flash_format_ready = SDL_FALSE;

SDL_Surface *pop_make_flash_surface(const void *pixels, int width, int height)
{
    if (!g_flash_format_ready) {
        g_flash_format.format = SDL_PIXELFORMAT_INDEX8;
        g_flash_format.BitsPerPixel = 8;
        g_flash_format.BytesPerPixel = 1;
        g_flash_format.Rmask = g_flash_format.Gmask = g_flash_format.Bmask = g_flash_format.Amask = 0;
        g_flash_format.palette = shared_sprite_palette();
        g_flash_format_ready = SDL_TRUE;
    }
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    s->format = &g_flash_format;
    s->w = width;
    s->h = height;
    s->pitch = width; /* baked pixels are tightly packed, 1 byte/pixel */
    s->pixels = (void *)pixels; /* read-only flash; sprites are blit sources only */
    s->clip_rect.x = 0; s->clip_rect.y = 0; s->clip_rect.w = width; s->clip_rect.h = height;
    s->refcount = 1;
    s->flash_backed = SDL_TRUE;
    return s;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    /* The sprite placeholder is a single shared surface handed out when a sprite
     * is too big to decode into RAM; many chtab entries point at it, so freeing
     * it (on level teardown) would double-free. Never free it. */
    if (surface == g_pop_sprite_placeholder) return;
    /* Flash-backed sprite surfaces share one static format/palette and point at
     * read-only flash pixels (see pop_sprites.c). Free only the SDL_Surface
     * struct itself; never touch the shared format or the flash pixels. */
    if (surface->flash_backed) {
        free(surface);
        return;
    }
    if (surface->format) {
        /* The shared sprite palette is global and reused; never free it. */
        if (surface->format->palette &&
            surface->format->palette != g_shared_sprite_palette) {
            free(surface->format->palette->colors);
            free(surface->format->palette);
        }
        free(surface->format);
    }
    /* Pool-backed pixels are returned to the reserved pool, not freed. */
    if (surface->pool_backed) {
        pool_return(surface->pixels);
    } else {
        free(surface->pixels);
    }
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

/* Real INDEX8 -> INDEX8 blit. Every surface in the ESP port is 8-bit indexed
 * (sprites are flash-backed INDEX8; screen buffers are INDEX8), so a blit is an
 * index-preserving row copy. Honors the source colorkey (transparent index) and
 * the destination clip rect, and adjusts dstrect to the region actually written
 * (matching SDL2 semantics that callers like method_1_blit_rect rely on). */
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;

    int sx = 0, sy = 0, sw = src->w, sh = src->h;
    if (srcrect) { sx = srcrect->x; sy = srcrect->y; sw = srcrect->w; sh = srcrect->h; }
    int dx = (dstrect) ? dstrect->x : 0;
    int dy = (dstrect) ? dstrect->y : 0;

    /* Clip the source rect to the source surface. */
    if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
    if (sx + sw > src->w) sw = src->w - sx;
    if (sy + sh > src->h) sh = src->h - sy;

    /* Clip the destination against dst->clip_rect (and the surface bounds). */
    int cx0 = dst->clip_rect.x, cy0 = dst->clip_rect.y;
    int cx1 = cx0 + dst->clip_rect.w, cy1 = cy0 + dst->clip_rect.h;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > dst->w) cx1 = dst->w;
    if (cy1 > dst->h) cy1 = dst->h;
    if (dx < cx0) { int d = cx0 - dx; sx += d; sw -= d; dx = cx0; }
    if (dy < cy0) { int d = cy0 - dy; sy += d; sh -= d; dy = cy0; }
    if (dx + sw > cx1) sw = cx1 - dx;
    if (dy + sh > cy1) sh = cy1 - dy;

    if (sw <= 0 || sh <= 0) {
        if (dstrect) { dstrect->w = 0; dstrect->h = 0; }
        return 0;
    }

    int sbpp = src->format ? src->format->BytesPerPixel : 1;
    int dbpp = dst->format ? dst->format->BytesPerPixel : 1;
    const Uint8 *srow = (const Uint8 *)src->pixels + (size_t)sy * src->pitch + (size_t)sx * sbpp;
    Uint8 *drow = (Uint8 *)dst->pixels + (size_t)dy * dst->pitch + (size_t)dx * dbpp;

    if (sbpp == 1 && dbpp == 1) {
        if (src->has_colorkey) {
            Uint8 key = (Uint8)src->colorkey;
            for (int y = 0; y < sh; y++) {
                const Uint8 *s = srow;
                Uint8 *d = drow;
                for (int x = 0; x < sw; x++) {
                    Uint8 v = s[x];
                    if (v != key) d[x] = v;
                }
                srow += src->pitch;
                drow += dst->pitch;
            }
        } else {
            for (int y = 0; y < sh; y++) {
                memcpy(drow, srow, (size_t)sw);
                srow += src->pitch;
                drow += dst->pitch;
            }
        }
    } else if (sbpp == dbpp) {
        /* Non-indexed same-depth copy (rare on ESP: e.g. peel save/restore). */
        for (int y = 0; y < sh; y++) {
            memcpy(drow, srow, (size_t)sw * sbpp);
            srow += src->pitch;
            drow += dst->pitch;
        }
    }

    if (dstrect) { dstrect->x = dx; dstrect->y = dy; dstrect->w = sw; dstrect->h = sh; }
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
    /* Bypass the 24/32->8 coercion in SDL_CreateRGBSurface: a conversion target
     * keeps its requested native depth because callers may write pixels into it
     * directly at that depth. */
    SDL_Surface *dst = create_surface_impl(src->w, src->h, depth, 0, 0, 0, 0);
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
    (void)flags;
    if (!src) return NULL;
    /* Honor the requested pixel format's depth: some callers (e.g.
     * method_3_blit_mono) convert an indexed sprite to ARGB8888 and then write
     * 32-bit pixels, so the destination really must be 32-bit. */
    int depth;
    switch (pixel_format) {
        case SDL_PIXELFORMAT_ARGB8888: depth = 32; break;
        case SDL_PIXELFORMAT_RGB24:    depth = 24; break;
        case SDL_PIXELFORMAT_INDEX8:   depth = 8;  break;
        default: depth = src->format ? src->format->BitsPerPixel : 8; break;
    }
    /* Bypass the 24/32->8 coercion in SDL_CreateRGBSurface: method_3_blit_mono()
     * converts a glyph to ARGB8888 and then writes 32-bit pixels into it, so the
     * destination must really be 32-bit or the writes corrupt the heap. */
    SDL_Surface *dst = create_surface_impl(src->w, src->h, depth, 0, 0, 0, 0);
    if (!dst) return NULL;
    if (dst->pitch == src->pitch && dst->format->BytesPerPixel == src->format->BytesPerPixel)
        memcpy(dst->pixels, src->pixels, (size_t)src->pitch * src->h);
    if (depth == 8 && src->format->palette && dst->format->palette) {
        int n = src->format->palette->ncolors;
        if (n > dst->format->palette->ncolors) n = dst->format->palette->ncolors;
        memcpy(dst->format->palette->colors, src->format->palette->colors, (size_t)n * sizeof(SDL_Color));
    }
    dst->has_colorkey = src->has_colorkey;
    dst->colorkey = src->colorkey;
    return dst;
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
size_t     SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num) { (void)ctx; (void)ptr; (void)size; (void)num; return 0; }
size_t     SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum) { (void)ctx; (void)ptr; (void)size; (void)maxnum; return 0; }
int        SDL_RWclose(SDL_RWops *ctx) { (void)ctx; return 0; }

/* ------------------------------------------------------------ SDL_image */
SDL_Surface *IMG_Load(const char *file) { (void)file; return NULL; }
SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return NULL; }
const char  *IMG_GetError(void) { return ""; }
