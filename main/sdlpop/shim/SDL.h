/*
 * Minimal SDL2 shim for building SDLPoP on the ESP32 (no real SDL).
 *
 * This header provides just enough of the SDL2 API surface that SDLPoP's
 * platform layer (seg009.c) and game logic (seg000-008, seqtbl, data)
 * reference, so the code compiles and links without libSDL2. The actual
 * behaviour is implemented in ../sdlpop_shim.c: timing is real (backed by
 * esp_timer / FreeRTOS), surfaces are real (plain malloc'd pixel buffers so
 * the decoders/fonts keep working), and everything window/renderer/audio/
 * controller related is a no-op stub for now. Later phases flesh out the
 * surface/blit/present functions into the ILI9341 band renderer.
 */
#ifndef POP_SDL_SHIM_H
#define POP_SDL_SHIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Fixed width types ------------------------------------------------- */
typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

/* ---- Byte order -------------------------------------------------------- */
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN  /* ESP32 (Xtensa) is little-endian */

#define SDL_Swap16(x) ((Uint16)((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8)))
#define SDL_Swap32(x) ((Uint32)((((x) & 0x000000ffU) << 24) | (((x) & 0x0000ff00U) << 8) | \
                                (((x) & 0x00ff0000U) >> 8) | (((x) & 0xff000000U) >> 24)))
#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)

/* ---- bool -------------------------------------------------------------- */
typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

/* ---- Compile-time assert ---------------------------------------------- */
#ifndef SDL_COMPILE_TIME_ASSERT
#define SDL_COMPILE_TIME_ASSERT(name, x) typedef int SDL_dummy_##name[(x) * 2 - 1]
#endif

/* ---- Version ----------------------------------------------------------- */
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    20
typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))
#define SDL_VERSION(v)                       \
    do {                                     \
        (v)->major = SDL_MAJOR_VERSION;      \
        (v)->minor = SDL_MINOR_VERSION;      \
        (v)->patch = SDL_PATCHLEVEL;         \
    } while (0)

/* ---- Basic geometry / color ------------------------------------------- */
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Rect  { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;

#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

typedef struct SDL_Palette {
    int        ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    Uint32       format;
    SDL_Palette *palette;
    Uint8        BitsPerPixel;
    Uint8        BytesPerPixel;
    Uint32       Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32           flags;
    SDL_PixelFormat *format;
    int              w, h;
    int              pitch;
    void            *pixels;
    SDL_Rect         clip_rect;
    int              refcount;
    /* shim-private */
    Uint32           colorkey;
    SDL_bool         has_colorkey;
    SDL_bool         pool_backed;   /* pixels come from the screen-buffer pool */
    SDL_bool         flash_backed;  /* pixels + format are in read-only flash (sprites) */
} SDL_Surface;

/* Pixel format enums (only those referenced) */
#define SDL_PIXELFORMAT_INDEX8   1
#define SDL_PIXELFORMAT_RGB24    386930691
#define SDL_PIXELFORMAT_ARGB8888 372645892
#define SDL_ISPIXELFORMAT_INDEXED(fmt) ((fmt) == SDL_PIXELFORMAT_INDEX8)

/* Blend modes */
#define SDL_BLENDMODE_NONE  0x00000000
#define SDL_BLENDMODE_BLEND 0x00000001
#define SDL_BLENDMODE_MOD   0x00000004

/* Color key / alpha flags (legacy names used by SDLPoP) */
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_SRCALPHA    0x00010000

/* ---- Opaque handles ---------------------------------------------------- */
typedef struct SDL_Window         SDL_Window;
typedef struct SDL_Renderer       SDL_Renderer;
typedef struct SDL_Texture        SDL_Texture;
typedef struct SDL_GameController  SDL_GameController;
typedef struct SDL_Joystick        SDL_Joystick;
typedef struct SDL_Haptic          SDL_Haptic;
typedef Sint32 SDL_JoystickID;

typedef struct SDL_RendererInfo {
    const char *name;
    Uint32      flags;
    Uint32      num_texture_formats;
    Uint32      texture_formats[16];
    int         max_texture_width;
    int         max_texture_height;
} SDL_RendererInfo;

/* ---- Init / window / renderer flags ----------------------------------- */
#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_NOPARACHUTE    0x00100000u

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001u
#define SDL_WINDOW_RESIZABLE          0x00000020u
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000u

#define SDL_RENDERER_SOFTWARE      0x00000001u
#define SDL_RENDERER_ACCELERATED   0x00000002u
#define SDL_RENDERER_PRESENTVSYNC  0x00000004u
#define SDL_RENDERER_TARGETTEXTURE 0x00000008u

#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

#define SDL_DISABLE 0
#define SDL_ENABLE  1

/* Hints (values unused by the shim) */
#define SDL_HINT_RENDER_SCALE_QUALITY        "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_VSYNC                 "SDL_RENDER_VSYNC"
#define SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING "SDL_WINDOWS_DISABLE_THREAD_NAMING"
#define SDL_HINT_IME_SHOW_UI                  "SDL_IME_SHOW_UI"

/* ---- Scancodes (real SDL2 numeric values) ----------------------------- */
typedef enum SDL_Scancode {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4, SDL_SCANCODE_B = 5, SDL_SCANCODE_C = 6, SDL_SCANCODE_D = 7,
    SDL_SCANCODE_E = 8, SDL_SCANCODE_F = 9, SDL_SCANCODE_G = 10, SDL_SCANCODE_H = 11,
    SDL_SCANCODE_I = 12, SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_O = 18, SDL_SCANCODE_P = 19,
    SDL_SCANCODE_Q = 20, SDL_SCANCODE_R = 21, SDL_SCANCODE_S = 22, SDL_SCANCODE_T = 23,
    SDL_SCANCODE_U = 24, SDL_SCANCODE_V = 25, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28, SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43, SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_LEFTBRACKET = 47, SDL_SCANCODE_RIGHTBRACKET = 48,
    SDL_SCANCODE_GRAVE = 53, SDL_SCANCODE_CAPSLOCK = 57,
    SDL_SCANCODE_F6 = 63, SDL_SCANCODE_F9 = 66, SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_PRINTSCREEN = 70, SDL_SCANCODE_SCROLLLOCK = 71, SDL_SCANCODE_PAUSE = 72,
    SDL_SCANCODE_HOME = 74, SDL_SCANCODE_PAGEUP = 75, SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_NUMLOCKCLEAR = 83,
    SDL_SCANCODE_KP_MINUS = 86, SDL_SCANCODE_KP_PLUS = 87,
    SDL_SCANCODE_KP_2 = 90, SDL_SCANCODE_KP_4 = 92, SDL_SCANCODE_KP_5 = 93,
    SDL_SCANCODE_KP_6 = 94, SDL_SCANCODE_KP_7 = 95, SDL_SCANCODE_KP_8 = 96, SDL_SCANCODE_KP_9 = 97,
    SDL_SCANCODE_APPLICATION = 101,
    SDL_SCANCODE_CLEAR = 156,
    SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT = 225, SDL_SCANCODE_LALT = 226, SDL_SCANCODE_LGUI = 227,
    SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT = 229, SDL_SCANCODE_RALT = 230, SDL_SCANCODE_RGUI = 231,
    SDL_SCANCODE_MUTE = 257, SDL_SCANCODE_VOLUMEUP = 258, SDL_SCANCODE_VOLUMEDOWN = 259,
    SDL_SCANCODE_AUDIOMUTE = 262,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef Sint32 SDL_Keycode;

/* Key modifier masks */
#define KMOD_NONE   0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL  0x0040
#define KMOD_RCTRL  0x0080
#define KMOD_LALT   0x0100
#define KMOD_RALT   0x0200
#define KMOD_LGUI   0x0400
#define KMOD_RGUI   0x0800
#define KMOD_NUM    0x1000
#define KMOD_CAPS   0x2000
#define KMOD_MODE   0x4000
#define KMOD_CTRL   (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT  (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT    (KMOD_LALT | KMOD_RALT)
#define KMOD_GUI    (KMOD_LGUI | KMOD_RGUI)

/* ---- Game controller enums -------------------------------------------- */
typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX = 0,
    SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A = 0,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

#define SDL_BUTTON_LEFT  1
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1    6

/* ---- Event types ------------------------------------------------------- */
typedef enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_APPACTIVE, SDL_APPINPUTFOCUS,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTEDITING, SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL,
    SDL_JOYAXISMOTION = 0x600, SDL_JOYBALLMOTION, SDL_JOYHATMOTION, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP,
    SDL_CONTROLLERAXISMOTION = 0x650, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP,
    SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED, SDL_CONTROLLERDEVICEREMAPPED,
    SDL_USEREVENT = 0x8000
} SDL_EventType;

/* Window event ids */
#define SDL_WINDOWEVENT_MOVED        4
#define SDL_WINDOWEVENT_RESIZED      5
#define SDL_WINDOWEVENT_SIZE_CHANGED 6
#define SDL_WINDOWEVENT_MINIMIZED    7
#define SDL_WINDOWEVENT_RESTORED     9
#define SDL_WINDOWEVENT_EXPOSED      3
#define SDL_WINDOWEVENT_FOCUS_GAINED 12

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode  sym;
    Uint16       mod;
    Uint32       unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID;
    Uint8 state; Uint8 repeat; Uint8 padding2, padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which;
    Uint8 button, state, clicks, padding1;
    Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID; Uint32 which;
    Sint32 x, y; Uint32 direction;
} SDL_MouseWheelEvent;

typedef struct SDL_WindowEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID;
    Uint8 event; Uint8 padding1, padding2, padding3;
    Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct SDL_ControllerAxisEvent {
    Uint32 type; Uint32 timestamp; SDL_JoystickID which;
    Uint8 axis; Uint8 padding1, padding2, padding3;
    Sint16 value; Uint16 padding4;
} SDL_ControllerAxisEvent;

typedef struct SDL_ControllerButtonEvent {
    Uint32 type; Uint32 timestamp; SDL_JoystickID which;
    Uint8 button, state, padding1, padding2;
} SDL_ControllerButtonEvent;

typedef struct SDL_ControllerDeviceEvent {
    Uint32 type; Uint32 timestamp; Sint32 which;
} SDL_ControllerDeviceEvent;

typedef struct SDL_JoyAxisEvent {
    Uint32 type; Uint32 timestamp; SDL_JoystickID which;
    Uint8 axis; Uint8 padding1, padding2, padding3;
    Sint16 value; Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type; Uint32 timestamp; SDL_JoystickID which;
    Uint8 button, state, padding1, padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_UserEvent {
    Uint32 type; Uint32 timestamp; Uint32 windowID;
    Sint32 code; void *data1; void *data2;
} SDL_UserEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent        key;
    SDL_TextInputEvent       text;
    SDL_MouseButtonEvent     button;
    SDL_MouseWheelEvent      wheel;
    SDL_WindowEvent          window;
    SDL_ControllerAxisEvent  caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    SDL_JoyAxisEvent         jaxis;
    SDL_JoyButtonEvent       jbutton;
    SDL_UserEvent            user;
    Uint8                    padding[56];
} SDL_Event;

/* ---- Timers ------------------------------------------------------------ */
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);
typedef int SDL_TimerID;

/* ---- Audio ------------------------------------------------------------- */
typedef Uint16 SDL_AudioFormat;
typedef Uint32 SDL_AudioDeviceID;
#define AUDIO_U8     0x0008
#define AUDIO_S16SYS 0x8010
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16    0x8010
#define SDL_AUDIO_STOPPED 0
#define SDL_AUDIO_PLAYING 1
#define SDL_AUDIO_PAUSED  2
typedef int SDL_AudioStatus;

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec {
    int             freq;
    SDL_AudioFormat format;
    Uint8           channels;
    Uint8           silence;
    Uint16          samples;
    Uint16          padding;
    Uint32          size;
    SDL_AudioCallback callback;
    void           *userdata;
} SDL_AudioSpec;

typedef struct SDL_AudioCVT {
    int             needed;
    SDL_AudioFormat src_format;
    SDL_AudioFormat dst_format;
    double          rate_incr;
    Uint8          *buf;
    int             len;
    int             len_cvt;
    int             len_mult;
    double          len_ratio;
    void           *filters[10];
    int             filter_index;
} SDL_AudioCVT;

/* ---- RWops (opaque enough for our compiled set) ------------------------ */
typedef struct SDL_RWops SDL_RWops;

/* ---- Function prototypes (implemented in sdlpop_shim.c) ---------------- */
int          SDL_Init(Uint32 flags);
int          SDL_InitSubSystem(Uint32 flags);
void         SDL_Quit(void);
const char  *SDL_GetError(void);
void         SDL_free(void *mem);
int          SDL_SetHint(const char *name, const char *value);
void         SDL_GetVersion(SDL_version *ver);

Uint32       SDL_GetTicks(void);
Uint64       SDL_GetPerformanceCounter(void);
Uint64       SDL_GetPerformanceFrequency(void);
void         SDL_Delay(Uint32 ms);
SDL_TimerID  SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param);
SDL_bool     SDL_RemoveTimer(SDL_TimerID id);

/* Surfaces */
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void         SDL_FreeSurface(SDL_Surface *surface);
int          SDL_LockSurface(SDL_Surface *surface);
void         SDL_UnlockSurface(SDL_Surface *surface);
int          SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int          SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                             SDL_Surface *dst, SDL_Rect *dstrect);
int          SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
                            SDL_Surface *dst, SDL_Rect *dstrect);
int          SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int          SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
int          SDL_SetSurfaceBlendMode(SDL_Surface *surface, int blendMode);
int          SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int          SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
int          SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);
Uint32       SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32       SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
const char  *SDL_GetPixelFormatName(Uint32 format);

/* Window / renderer / texture (stubs) */
SDL_Window  *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
Uint32       SDL_GetWindowFlags(SDL_Window *window);
void         SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
void         SDL_SetWindowTitle(SDL_Window *window, const char *title);
void         SDL_SetWindowIcon(SDL_Window *window, SDL_Surface *icon);
int          SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
void         SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
int          SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info);
int          SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);
int          SDL_RenderClear(SDL_Renderer *renderer);
int          SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void         SDL_RenderPresent(SDL_Renderer *renderer);
int          SDL_RenderSetIntegerScale(SDL_Renderer *renderer, SDL_bool enable);
int          SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
void         SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h);
int          SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
int          SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
int          SDL_ShowCursor(int toggle);
void         SDL_StartTextInput(void);
void         SDL_StopTextInput(void);
void         SDL_SetTextInputRect(SDL_Rect *rect);

/* Events / keyboard */
int          SDL_PollEvent(SDL_Event *event);
int          SDL_PushEvent(SDL_Event *event);
const Uint8 *SDL_GetKeyboardState(int *numkeys);

/* Game controller / joystick / haptic (stubs) */
int                SDL_NumJoysticks(void);
SDL_bool           SDL_IsGameController(int joystick_index);
SDL_GameController *SDL_GameControllerOpen(int joystick_index);
void               SDL_GameControllerClose(SDL_GameController *gamecontroller);
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID joyid);
int                SDL_GameControllerAddMappingsFromFile(const char *file);
int                SDL_GameControllerRumble(SDL_GameController *gc, Uint16 low, Uint16 high, Uint32 duration_ms);
SDL_Joystick      *SDL_JoystickOpen(int device_index);
int                SDL_JoystickRumble(SDL_Joystick *j, Uint16 low, Uint16 high, Uint32 duration_ms);
SDL_Haptic        *SDL_HapticOpen(int device_index);
int                SDL_HapticRumbleInit(SDL_Haptic *haptic);
int                SDL_HapticRumblePlay(SDL_Haptic *haptic, float strength, Uint32 length);

/* Audio (stubs) */
int              SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void             SDL_CloseAudio(void);
void             SDL_PauseAudio(int pause_on);
void             SDL_LockAudio(void);
void             SDL_UnlockAudio(void);
SDL_AudioStatus  SDL_GetAudioStatus(void);
int              SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
                                   SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate);
int              SDL_ConvertAudio(SDL_AudioCVT *cvt);

/* RWops */
SDL_RWops   *SDL_RWFromConstMem(const void *mem, int size);
size_t       SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num);
size_t       SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
int          SDL_RWclose(SDL_RWops *context);

/* String helpers used by Windows path code (harmless on ESP32) */
size_t       SDL_strlen(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* POP_SDL_SHIM_H */
