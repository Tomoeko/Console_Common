#ifndef CONSOLE_COMMON_PLATFORM_H
#define CONSOLE_COMMON_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Coordinates use the consumer-configured logical framebuffer with origin
 * at the upper left. Both graphics backends share this projection. */
#ifndef CC_FRAME_WIDTH
#define CC_FRAME_WIDTH 640
#endif
#ifndef CC_FRAME_HEIGHT
#define CC_FRAME_HEIGHT 456
#endif

typedef struct CcPlatform CcPlatform;

typedef struct CcFramebuffer {
    const uint8_t *rgba;
    int width;
    int height;
    size_t stride;
} CcFramebuffer;

typedef enum CcEventType {
    CC_EVENT_NONE,
    CC_EVENT_QUIT,
    CC_EVENT_POINTER_DOWN,
    CC_EVENT_POINTER_UP,
    CC_EVENT_POINTER_MOVE,
    CC_EVENT_POINTER_LEAVE,
    CC_EVENT_KEY_DOWN,
    CC_EVENT_KEY_MODIFIERS,
    CC_EVENT_KEY_UP
} CcEventType;

typedef enum CcKey {
    CC_KEY_UNKNOWN = 0,
    CC_KEY_LEFT = 256,
    CC_KEY_RIGHT,
    CC_KEY_UP,
    CC_KEY_DOWN,
    CC_KEY_ENTER,
    CC_KEY_ESCAPE,
    CC_KEY_BACKSPACE,
    CC_KEY_HOME,
    CC_KEY_SHIFT,
    CC_KEY_CAPS_LOCK
} CcKey;

typedef enum CcPointerButton {
    CC_POINTER_LEFT = 1,
    CC_POINTER_MIDDLE = 2,
    CC_POINTER_RIGHT = 3
} CcPointerButton;

typedef struct CcEvent {
    CcEventType type;
    int x;
    int y;
    CcKey key;
    CcPointerButton button;
    /* Pointer coordinates remain valid outside the fitted picture.
     * Scene input decides whether an active drag consumes that event. */
    bool outside_viewport;
    /* Focus loss cancels a held action; pointer departure may finish it. */
    bool cancel_capture;
    /* A modifier change is separate from text input so held Shift can be
     * released visually without synthesizing a character. */
    bool shift_down;
    bool caps_lock_on;
    /* Native autorepeat is separate from a fresh physical press, including
     * after focus changes reset a consumer's held-key state. */
    bool key_repeat;
} CcEvent;

typedef struct CcColor {
    float r;
    float g;
    float b;
    float a;
} CcColor;

typedef struct CcClipRect {
    float x;
    float y;
    float width;
    float height;
} CcClipRect;

typedef struct CcQuad {
    float x;
    float y;
    float width;
    float height;
    float u0;
    float v0;
    float u1;
    float v1;
    CcColor color;
    uint32_t texture; /* Zero selects the backend's white texture. */
} CcQuad;

/* A transformed textured quad, ordered left-top, right-top, left-bottom,
 * right-bottom. The two triangles are (0,1,3) and (0,3,2). */
typedef struct CcDrawVertex {
    float x;
    float y;
    float u;
    float v;
    CcColor color;
} CcDrawVertex;

enum { CC_MATERIAL_TEXTURES = 4, CC_MATERIAL_TEV_STAGES = 16 };

typedef struct CcMaterialVertex {
    float x;
    float y;
    CcColor color;
    float uv[CC_MATERIAL_TEXTURES][2];
    float depth;  /* Normalized window depth, from near 0 to far 1. */
    float clip_w; /* Positive projection W; zero preserves the 2D default of 1. */
} CcMaterialVertex;

typedef struct CcMaterialQuad {
    CcMaterialVertex vertices[4];
    uint32_t textures[CC_MATERIAL_TEXTURES];
    uint8_t wrap_s[CC_MATERIAL_TEXTURES];
    uint8_t wrap_t[CC_MATERIAL_TEXTURES];
    bool nearest[CC_MATERIAL_TEXTURES]; /* Zero keeps linear sampling. */
    unsigned texture_count;
    float registers[3][4];
    float konst_colors[4][4];
    uint8_t tev_stages[CC_MATERIAL_TEV_STAGES][16];
    unsigned tev_stage_count;
    uint8_t tev_swap_table[4];
    uint8_t alpha_compare[4];
    uint8_t blend_mode[4];
    /* GX comparisons: NEVER, LESS, EQUAL, LEQUAL, GREATER, NEQUAL, GEQUAL,
     * ALWAYS. Disabled depth testing also disables depth writes. */
    uint8_t depth_mode[3]; /* Enable, comparison, write. */
    bool has_alpha_compare;
    bool has_blend_mode;
    bool has_depth_mode;
} CcMaterialQuad;

/* Each platform backend implements this API and owns its window and GPU
 * resources. Exactly one backend is linked into an executable. */
CcPlatform *cc_platform_create(const char *title, int window_width, int window_height);
void cc_platform_destroy(CcPlatform *platform);
bool cc_platform_poll(CcPlatform *platform, CcEvent *event);
/* Window controls run on the host event thread. The setter accepts an
 * asynchronous request; the query reports the current native window state.
 * A headless backend cannot enter fullscreen. */
bool cc_platform_is_fullscreen(CcPlatform *platform);
bool cc_platform_set_fullscreen(CcPlatform *platform, bool fullscreen);
/* Optional presentation smoothing preserves original textures and scene geometry.
 * Metal shades four samples per pixel (two when four are unavailable); core ES2
 * renders at twice each output dimension and resolves once with linear sampling.
 * This increases target storage and fill cost. Configure before loading materials.
 * Failure leaves ordinary rendering available. Preview targets remain unchanged. */
bool cc_platform_set_antialiasing(CcPlatform *platform, bool enabled);
void cc_platform_begin(CcPlatform *platform, CcColor clear_color);
/* Clip subsequent draws to a logical framebuffer rectangle. NULL disables
 * clipping. Both backends preserve draw order across clip changes. */
void cc_platform_set_clip(CcPlatform *platform, const CcClipRect *rect);
void cc_platform_draw_quad(CcPlatform *platform, const CcQuad *quad);
void cc_platform_draw_vertices(CcPlatform *platform, const CcDrawVertex vertices[4],
                               uint32_t texture);
void cc_platform_draw_material_quad(CcPlatform *platform, const CcMaterialQuad *quad);
/* Prepare material resources while layouts load, on the rendering thread.
 * GLES2 caches supported TEV programs (up to six stages); Metal prepares the
 * material's blend pipeline using its previously compiled shader library. */
void cc_platform_prepare_material(CcPlatform *platform, const CcMaterialQuad *quad);
/* Apply the source scene fader after the scene's last draw, before presenting.
 * This is ignored for offscreen preview captures. */
void cc_platform_set_fade_alpha(CcPlatform *platform, float alpha);
void cc_platform_end(CcPlatform *platform);
/* Opt-in readback owns reusable GPU and CPU storage. Capture dimensions are
 * fixed at the initial window's backing-pixel size, including its fitted bars.
 * begin writes dimensions with rgba == NULL. After each completed window frame,
 * frame returns top-down RGBA8 pixels including the scene background and fader.
 * Pixels are borrowed until the next window frame or capture_end. Offscreen
 * preview targets never replace the captured window frame. Capture calls run
 * on the rendering thread; ordinary rendering performs no readback. */
bool cc_platform_capture_begin(CcPlatform *platform, CcFramebuffer *frame);
bool cc_platform_capture_frame(CcPlatform *platform, CcFramebuffer *frame);
void cc_platform_capture_end(CcPlatform *platform);
uint32_t cc_platform_create_texture(CcPlatform *platform, int width, int height,
                                    const uint8_t *rgba);
/* A reusable logical-size render target for captured scene composition. The
 * returned handle can be drawn like any other texture. At most one capture
 * target is active per backend instance. */
uint32_t cc_platform_create_render_texture(CcPlatform *platform);
bool cc_platform_begin_target(CcPlatform *platform, uint32_t texture,
                              CcColor clear_color);
void cc_platform_destroy_texture(CcPlatform *platform, uint32_t texture);

#endif
