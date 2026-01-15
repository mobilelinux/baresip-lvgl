#ifndef FAKE_SDL_H
#define FAKE_SDL_H

#include <stdint.h>

typedef uint32_t Uint32;

// Basic Types
typedef struct {
    int x, y, w, h;
} SDL_Rect;

typedef struct {
    int padding;
} SDL_Color;

typedef void SDL_Window;
typedef void SDL_Renderer;
typedef void SDL_Texture;
typedef void SDL_Surface;
typedef void SDL_Cursor;

// Events
#define SDL_WINDOWEVENT 1
#define SDL_WINDOWEVENT_EXPOSED 2
#define SDL_USEREVENT 3
#define SDL_QUIT 4
#define SDL_KEYDOWN 5
#define SDL_MOUSEBUTTONDOWN 6

typedef struct {
    uint32_t type;
} SDL_CommonEvent;

typedef struct {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t event;
    int32_t data1;
    int32_t data2;
} SDL_WindowEvent;

typedef struct {
    uint32_t type;
    uint32_t code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef union {
    uint32_t type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_UserEvent user;
    uint8_t padding[56];
} SDL_Event;

// Functions
static inline int SDL_Init(uint32_t flags) { return 0; }
static inline void SDL_Quit(void) {}
static inline int SDL_PushEvent(SDL_Event *event) { return 0; }
static inline int SDL_PollEvent(SDL_Event *event) { return 0; }

static inline SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags) { return (SDL_Window*)1; }
static inline void SDL_DestroyWindow(SDL_Window *window) {}

static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags) { return (SDL_Renderer*)1; }
static inline void SDL_DestroyRenderer(SDL_Renderer *renderer) {}

static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h) { return (SDL_Texture*)1; }
static inline void SDL_DestroyTexture(SDL_Texture *texture) {}
static inline int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) { return 0; }

static inline int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect) { return 0; }
static inline int SDL_RenderClear(SDL_Renderer *renderer) { return 0; }
static inline void SDL_RenderPresent(SDL_Renderer *renderer) {}
static inline int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return 0; }

static inline uint32_t SDL_GetTicks(void) { return 0; }
static inline void SDL_Delay(uint32_t ms) {}

static inline void SDL_FreeSurface(SDL_Surface *surface) {}
static inline void SDL_FreeCursor(SDL_Cursor *cursor) {}

// Defines
#define SDL_INIT_VIDEO 0x00000020
#define SDL_WINDOW_SHOWN 0x00000004
#define SDL_WINDOW_RESIZABLE 0x00000020
#define SDL_RENDERER_ACCELERATED 0x00000002
#define SDL_RENDERER_PRESENTVSYNC 0x00000004
#define SDL_PIXELFORMAT_YV12 0x32315659
#define SDL_PIXELFORMAT_IYUV 0x56555949
#define SDL_PIXELFORMAT_YUY2 0x32595559
#define SDL_PIXELFORMAT_UYVY 0x59565955
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define SDL_PIXELFORMAT_UNKNOWN 0
#define SDL_TEXTUREACCESS_STREAMING 1

static inline int SDL_QueryTexture(SDL_Texture *texture, uint32_t *format, int *access, int *w, int *h) { if(w) *w=0; if(h) *h=0; return 0; }
static inline int SDL_UpdateYUVTexture(SDL_Texture *texture, const SDL_Rect *rect, const uint8_t *Yplane, int Ypitch, const uint8_t *Uplane, int Upitch, const uint8_t *Vplane, int Vpitch) { return 0; }

#endif /* FAKE_SDL_H */
