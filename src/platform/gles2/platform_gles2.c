#include "console_common/platform/platform.h"
#include "console_common/render/viewport.h"
#include "clip.h"
#include "geometry.h"
#include "host.h"
#include "material_blend.h"
#include "material_depth.h"
#include "shaders.h"
#include "retained_frame.h"
#include "texture_dimensions.h"

#include <GLES2/gl2.h>

#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CC_BATCH_QUADS = 1024 };

typedef struct CcVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
} CcVertex;

typedef struct CcMaterialGpuVertex {
    float x;
    float y;
    float depth;
    float clip_w;
    float color[4];
    float uv[CC_MATERIAL_TEXTURES][2];
} CcMaterialGpuVertex;

struct CcPlatform {
    CcGles2Host *host;
    int framebuffer_width;
    int framebuffer_height;
    CcViewport presentation;
    bool scissor_enabled;
    int scissor_x;
    int scissor_y;
    int scissor_width;
    int scissor_height;

    GLuint program;
    GLuint material_program;
    GLuint tev_vertex_shader;
    CcTevProgram *tev_programs;
    bool fragment_highp;
    bool warned_tev_limit;
    bool warned_tev_precision;
    bool warned_tev_encoding;
    GLuint vertex_buffer;
    GLuint white_texture;
    GLuint render_texture;
    GLuint render_framebuffer;
    GLuint render_depth;
    unsigned depth_key;
    bool rendering_target;
    CcGles2RetainedFrame retained;
    CcGles2TextureDimensions texture_dimensions;
    GLint projection_location;
    GLint texture_location;
    GLint material_frame_location;
    GLint material_texture_count_location;
    GLint material_registers_location;
    GLint material_konst_location;
    GLint material_alpha_location;
    GLint material_wrap_locations[2];
    GLint material_sampling_locations[2];

    CcVertex vertices[CC_BATCH_QUADS * CC_VERTICES_PER_QUAD];
    size_t quad_count;
    GLuint batch_texture;
    float fade_alpha;
};

static void cc_set_depth(CcPlatform *platform, unsigned key) {
    if (platform->depth_key == key)
        return;
    static const GLenum comparisons[8] = {
        GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
        GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS,
    };
    if (key) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(comparisons[(key - 1) / 2]);
        glDepthMask((key - 1) & 1 ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    platform->depth_key = key;
}

static void cc_clear_depth(CcPlatform *platform) {
    cc_set_depth(platform, 0);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);
}

static GLuint cc_upload_texture_storage(int width, int height, const uint8_t *rgba,
                                        GLint filter) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    /* CLAMP_TO_EDGE and no mipmaps keep arbitrary texture sizes valid on ES2. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    return texture;
}

static GLuint cc_upload_texture(CcPlatform *platform, int width, int height,
                                const uint8_t *rgba) {
    GLuint texture = cc_upload_texture_storage(width, height, rgba, GL_LINEAR);
    if (!texture)
        return 0;
    /* Mediump cannot reliably represent every large texture's texel center.
     * Such contexts keep a nearest mirror, doubling texture storage but avoiding
     * per-draw allocations and allowing mixed filters on aliased handles. */
    GLuint mirror = platform->fragment_highp
                        ? 0
                        : cc_upload_texture_storage(width, height, rgba, GL_NEAREST);
    if ((!platform->fragment_highp && !mirror) ||
        !cc_gles2_texture_dimensions_set(&platform->texture_dimensions,
                                         (uint32_t)texture, width, height) ||
        !cc_gles2_texture_dimensions_set_nearest(&platform->texture_dimensions,
                                                 (uint32_t)texture, (uint32_t)mirror)) {
        if (mirror)
            glDeleteTextures(1, &mirror);
        glDeleteTextures(1, &texture);
        cc_gles2_texture_dimensions_remove(&platform->texture_dimensions,
                                           (uint32_t)texture);
        return 0;
    }
    return texture;
}

static void cc_delete_texture(CcPlatform *platform, GLuint texture) {
    GLuint mirror = (GLuint)cc_gles2_texture_dimensions_nearest(
        &platform->texture_dimensions, (uint32_t)texture);
    if (mirror)
        glDeleteTextures(1, &mirror);
    cc_gles2_texture_dimensions_remove(&platform->texture_dimensions,
                                       (uint32_t)texture);
    glDeleteTextures(1, &texture);
}

static void cc_bind_material_texture(CcPlatform *platform, GLint location,
                                     GLuint texture, bool nearest) {
    int dimensions[2] = {1, 1};
    (void)cc_gles2_texture_dimensions_get(&platform->texture_dimensions,
                                          (uint32_t)texture, dimensions);
    /* Capture rows follow GL's framebuffer origin. Orientation belongs to the
     * texture slot, because a TEV stage can select a different UV coordinate. */
    bool flip_v = texture == platform->render_texture;
    if (nearest && !platform->fragment_highp) {
        GLuint mirror = (GLuint)cc_gles2_texture_dimensions_nearest(
            &platform->texture_dimensions, (uint32_t)texture);
        if (mirror) {
            texture = mirror;
            /* Nearest capture mirrors are copied into logical row order. */
            flip_v = false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform4f(location, (float)dimensions[0], (float)dimensions[1],
                nearest && platform->fragment_highp ? 1.0f : 0.0f,
                flip_v ? 1.0f : 0.0f);
}

static void cc_refresh_render_texture_mirror(CcPlatform *platform) {
    GLuint mirror = (GLuint)cc_gles2_texture_dimensions_nearest(
        &platform->texture_dimensions, (uint32_t)platform->render_texture);
    if (!mirror)
        return;
    /* The original FBO is still bound. Reverse its rows on the GPU so nearest
     * sampling uses logical texel order, including exact texel boundaries.
     * Row copies avoid mediump rounding in a textured flip draw. This fallback
     * submits one copy per row only when completing a lowp capture. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mirror);
    for (int row = 0; row < CC_FRAME_HEIGHT; ++row)
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, 0, CC_FRAME_HEIGHT - 1 - row,
                            CC_FRAME_WIDTH, 1);
}

static void cc_flush(CcPlatform *platform) {
    if (platform->quad_count == 0) {
        return;
    }
    cc_set_depth(platform, 0);

    GLsizeiptr byte_count =
        (GLsizeiptr)(platform->quad_count * CC_VERTICES_PER_QUAD * sizeof(CcVertex));
    glUseProgram(platform->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, platform->batch_texture);
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    /* Replacing storage avoids waiting for a previous draw using this buffer. */
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices), NULL,
                 GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, byte_count, platform->vertices);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, r));
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0,
                 (GLsizei)(platform->quad_count * CC_VERTICES_PER_QUAD));
    platform->quad_count = 0;
}

static bool cc_initialize_graphics(CcPlatform *platform) {
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    platform->fragment_highp = precision > 0;
    platform->program = cc_gles2_create_quad_program();
    if (platform->program == 0) {
        return false;
    }
    platform->material_program = cc_gles2_create_material_program();
    if (!platform->material_program)
        return false;
    platform->tev_vertex_shader =
        cc_gles2_create_tev_vertex_shader(platform->fragment_highp);
    if (!platform->tev_vertex_shader)
        return false;

    glUseProgram(platform->material_program);
    platform->material_frame_location =
        glGetUniformLocation(platform->material_program, "u_frame_size");
    platform->material_texture_count_location =
        glGetUniformLocation(platform->material_program, "u_texture_count");
    platform->material_registers_location =
        glGetUniformLocation(platform->material_program, "u_registers");
    platform->material_konst_location =
        glGetUniformLocation(platform->material_program, "u_konst");
    platform->material_alpha_location =
        glGetUniformLocation(platform->material_program, "u_alpha_compare");
    platform->material_wrap_locations[0] =
        glGetUniformLocation(platform->material_program, "u_wrap0");
    platform->material_wrap_locations[1] =
        glGetUniformLocation(platform->material_program, "u_wrap1");
    platform->material_sampling_locations[0] =
        glGetUniformLocation(platform->material_program, "u_sampling0");
    platform->material_sampling_locations[1] =
        glGetUniformLocation(platform->material_program, "u_sampling1");
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture0"), 0);
    glUniform1i(glGetUniformLocation(platform->material_program, "u_texture1"), 1);
    glUniform2f(platform->material_frame_location, (float)CC_FRAME_WIDTH,
                (float)CC_FRAME_HEIGHT);

    platform->projection_location =
        glGetUniformLocation(platform->program, "u_frame_size");
    platform->texture_location = glGetUniformLocation(platform->program, "u_texture");
    if (platform->projection_location < 0 || platform->texture_location < 0) {
        fprintf(stderr, "GLES2: required shader uniforms are unavailable.\n");
        return false;
    }

    glUseProgram(platform->program);
    glUniform2f(platform->projection_location, (float)CC_FRAME_WIDTH,
                (float)CC_FRAME_HEIGHT);
    glUniform1i(platform->texture_location, 0);

    glGenBuffers(1, &platform->vertex_buffer);
    if (platform->vertex_buffer == 0) {
        fprintf(stderr, "GLES2: could not allocate the quad buffer.\n");
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(platform->vertices), NULL,
                 GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(CcVertex),
                          (const void *)offsetof(CcVertex, r));

    static const uint8_t white_pixel[4] = {255, 255, 255, 255};
    platform->white_texture = cc_upload_texture(platform, 1, 1, white_pixel);
    if (platform->white_texture == 0) {
        fprintf(stderr, "GLES2: could not allocate the white texture.\n");
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    cc_gles2_retained_initialize(&platform->retained);
    if (platform->retained.allowed &&
        !cc_gles2_host_preserve_back_buffer(platform->host)) {
        cc_gles2_retained_destroy(&platform->retained);
    }
    return glGetError() == GL_NO_ERROR;
}

CcPlatform *cc_platform_create(const char *title, int window_width, int window_height) {
    if (window_width <= 0 || window_height <= 0) {
        fprintf(stderr, "GLES2: window dimensions must be positive.\n");
        return NULL;
    }

    CcPlatform *platform = calloc(1, sizeof(*platform));
    if (platform == NULL) {
        return NULL;
    }
    platform->host = cc_gles2_host_create(title, window_width, window_height);
    if (platform->host == NULL) {
        cc_platform_destroy(platform);
        return NULL;
    }
    if (!cc_initialize_graphics(platform)) {
        fprintf(stderr, "GLES2: graphics initialization failed.\n");
        cc_platform_destroy(platform);
        return NULL;
    }
    cc_gles2_host_show(platform->host);
    return platform;
}

void cc_platform_destroy(CcPlatform *platform) {
    if (platform == NULL) {
        return;
    }

    if (cc_gles2_host_make_current(platform->host)) {
        if (platform->white_texture != 0) {
            cc_delete_texture(platform, platform->white_texture);
        }
        if (platform->render_framebuffer != 0) {
            glDeleteFramebuffers(1, &platform->render_framebuffer);
        }
        if (platform->render_depth != 0) {
            glDeleteRenderbuffers(1, &platform->render_depth);
        }
        if (platform->render_texture != 0) {
            cc_delete_texture(platform, platform->render_texture);
        }
        if (platform->vertex_buffer != 0) {
            glDeleteBuffers(1, &platform->vertex_buffer);
        }
        if (platform->program != 0) {
            glDeleteProgram(platform->program);
        }
        if (platform->material_program != 0) {
            glDeleteProgram(platform->material_program);
        }
        for (CcTevProgram *entry = platform->tev_programs; entry; entry = entry->next) {
            if (entry->program)
                glDeleteProgram(entry->program);
        }
        if (platform->tev_vertex_shader) {
            glDeleteShader(platform->tev_vertex_shader);
        }
        for (size_t index = 0; index < platform->texture_dimensions.count; ++index) {
            GLuint mirror =
                (GLuint)platform->texture_dimensions.entries[index].nearest_handle;
            if (mirror)
                glDeleteTextures(1, &mirror);
        }
    }
    cc_gles2_host_destroy(platform->host);
    cc_gles2_retained_destroy(&platform->retained);
    while (platform->tev_programs) {
        CcTevProgram *next = platform->tev_programs->next;
        free(platform->tev_programs);
        platform->tev_programs = next;
    }
    cc_gles2_texture_dimensions_destroy(&platform->texture_dimensions);
    free(platform);
}

bool cc_platform_poll(CcPlatform *platform, CcEvent *event) {
    return platform != NULL && cc_gles2_host_poll(platform->host, event);
}

bool cc_platform_is_fullscreen(CcPlatform *platform) {
    return platform != NULL && cc_gles2_host_is_fullscreen(platform->host);
}

bool cc_platform_set_fullscreen(CcPlatform *platform, bool fullscreen) {
    return platform != NULL && cc_gles2_host_set_fullscreen(platform->host, fullscreen);
}

void cc_platform_begin(CcPlatform *platform, CcColor clear_color) {
    if (platform == NULL) {
        return;
    }

    platform->quad_count = 0;
    platform->rendering_target = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int width = 0;
    int height = 0;
    cc_gles2_host_surface_size(platform->host, &width, &height);
    if (platform->framebuffer_width != width || platform->framebuffer_height != height)
        cc_frame_damage_invalidate(platform->retained.commands);
    platform->framebuffer_width = width;
    platform->framebuffer_height = height;
    platform->presentation = cc_viewport_fit(width, height);
    CcViewport content = platform->presentation;
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
    cc_clear_depth(platform);
    glViewport(content.x, height - content.y - content.height, content.width,
               content.height);
    if (cc_gles2_retained_begin(&platform->retained, content.width, content.height,
                                clear_color)) {
        /* Retain the content pixels in EGL's original back buffer. Clear only
         * the letterbox bars, keeping the original rasterization and precision. */
        glEnable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        const CcViewport bars[] = {
            {0, 0, width, content.y},
            {0, content.y + content.height, width, height - content.y - content.height},
            {0, content.y, content.x, content.height},
            {content.x + content.width, content.y, width - content.x - content.width,
             content.height}};
        for (size_t index = 0; index < sizeof(bars) / sizeof(bars[0]); index++) {
            CcViewport bar = bars[index];
            if (bar.width <= 0 || bar.height <= 0)
                continue;
            glScissor(bar.x, height - bar.y - bar.height, bar.width, bar.height);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(content.x, height - content.y - content.height, content.width,
              content.height);
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
}

void cc_platform_set_clip(CcPlatform *platform, const CcClipRect *rect) {
    if (!platform)
        return;
    CcGles2RetainedFrame *retained = &platform->retained;
    bool cached = retained->active && !platform->rendering_target;
    if (cached && retained->recording) {
        retained->clip =
            rect ? *rect : (CcClipRect){0, 0, CC_FRAME_WIDTH, CC_FRAME_HEIGHT};
        return;
    }
    bool region_active = cached && retained->region_active;
    if (!rect && !region_active) {
        if (!platform->scissor_enabled)
            return;
        cc_flush(platform);
        glDisable(GL_SCISSOR_TEST);
        platform->scissor_enabled = false;
        return;
    }
    CcViewport content = platform->presentation;
    CcViewport clip = cc_render_clip_pixels(rect, content.width, content.height);
    int x0 = clip.x;
    int x1 = clip.x + clip.width;
    int y0 = clip.y;
    int y1 = clip.y + clip.height;
    if (region_active) {
        CcViewport region = retained->region;
        if (x0 < region.x)
            x0 = region.x;
        if (y0 < region.y)
            y0 = region.y;
        if (x1 > region.x + region.width)
            x1 = region.x + region.width;
        if (y1 > region.y + region.height)
            y1 = region.y + region.height;
        if (x1 < x0)
            x1 = x0;
        if (y1 < y0)
            y1 = y0;
    }
    int scissor_x = content.x + x0;
    int scissor_y = platform->framebuffer_height - content.y - y1;
    int scissor_width = x1 - x0;
    int scissor_height = y1 - y0;
    if (platform->scissor_enabled && platform->scissor_x == scissor_x &&
        platform->scissor_y == scissor_y && platform->scissor_width == scissor_width &&
        platform->scissor_height == scissor_height) {
        return;
    }
    cc_flush(platform);
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor_x, scissor_y, scissor_width, scissor_height);
    platform->scissor_enabled = true;
    platform->scissor_x = scissor_x;
    platform->scissor_y = scissor_y;
    platform->scissor_width = scissor_width;
    platform->scissor_height = scissor_height;
}

static CcVertex cc_vertex(float x, float y, float u, float v, CcColor color) {
    return (CcVertex){x, y, u, v, color.r, color.g, color.b, color.a};
}

void cc_platform_draw_quad(CcPlatform *platform, const CcQuad *quad) {
    if (platform == NULL || !cc_render_quad_has_area(quad)) {
        return;
    }

    CcDrawVertex vertices[CC_QUAD_CORNERS];
    cc_render_quad_corners(quad, vertices);
    cc_platform_draw_vertices(platform, vertices, quad->texture);
}

void cc_platform_draw_vertices(CcPlatform *platform, const CcDrawVertex corners[4],
                               uint32_t texture_handle) {
    if (platform == NULL || corners == NULL)
        return;

    if (!platform->rendering_target && platform->retained.recording) {
        if (cc_frame_damage_quad(platform->retained.commands, corners, texture_handle,
                                 &platform->retained.clip))
            return;
        cc_gles2_retained_materialize(&platform->retained, platform, cc_flush);
    }

    GLuint texture =
        texture_handle != 0 ? (GLuint)texture_handle : platform->white_texture;
    if (platform->quad_count != 0 && (platform->batch_texture != texture ||
                                      platform->quad_count == CC_BATCH_QUADS)) {
        cc_flush(platform);
    }
    platform->batch_texture = texture;

    CcVertex *vertices =
        &platform->vertices[platform->quad_count * CC_VERTICES_PER_QUAD];
    for (size_t index = 0; index < CC_VERTICES_PER_QUAD; index++) {
        const CcDrawVertex *corner = &corners[cc_quad_triangle_order[index]];
        /* FBO storage has the OpenGL bottom-left texture origin, while scene
         * quads use top-left image coordinates. */
        float v = texture == platform->render_texture ? 1.0f - corner->v : corner->v;
        vertices[index] = cc_vertex(corner->x, corner->y, corner->u, v, corner->color);
    }
    platform->quad_count++;
}

/* ES 2.0 has no sampler objects and cannot repeat arbitrary NPOT textures.
 * The material shader applies GX wrap coordinates before sampling textures
 * that remain CLAMP_TO_EDGE at the API level. */
static bool cc_tev_supported(CcPlatform *platform, const CcMaterialQuad *quad) {
    CcTevSupport support = cc_material_tev_support(quad, platform->fragment_highp);
    if (support == CC_TEV_STAGE_LIMIT && !platform->warned_tev_limit) {
        fprintf(stderr, "GLES2: materials with over six TEV stages use the "
                        "simple material fallback.\n");
        platform->warned_tev_limit = true;
    } else if (support == CC_TEV_PRECISION_LIMIT && !platform->warned_tev_precision) {
        fprintf(stderr, "GLES2: 24-bit TEV comparisons require "
                        "fragment highp; using the simple fallback.\n");
        platform->warned_tev_precision = true;
    } else if (support == CC_TEV_INVALID_ENCODING && !platform->warned_tev_encoding) {
        fprintf(stderr, "GLES2: invalid TEV selector encoding uses the "
                        "simple material fallback.\n");
        platform->warned_tev_encoding = true;
    }
    return support == CC_TEV_SUPPORTED;
}

void cc_platform_prepare_material(CcPlatform *platform, const CcMaterialQuad *quad) {
    if (!platform || !quad || quad->texture_count > CC_MATERIAL_TEXTURES)
        return;
    if (cc_tev_supported(platform, quad)) {
        (void)cc_gles2_get_tev_program(&platform->tev_programs,
                                       platform->tev_vertex_shader,
                                       platform->fragment_highp, quad);
    }
}

void cc_platform_draw_material_quad(CcPlatform *platform, const CcMaterialQuad *quad) {
    if (!platform || !quad || quad->texture_count > CC_MATERIAL_TEXTURES)
        return;
    CcMaterialBlend blend;
    unsigned depth_key;
    if (!cc_material_blend_resolve(quad, &blend) ||
        !cc_material_depth_key(quad, &depth_key))
        return;
    if (!platform->rendering_target && platform->retained.recording) {
        /* Retained color regions cannot preserve cross-region depth history.
         * Materialize the frame before its first draw that tests depth. */
        if (!depth_key && cc_frame_damage_material(platform->retained.commands, quad,
                                                   &platform->retained.clip))
            return;
        cc_gles2_retained_materialize(&platform->retained, platform, cc_flush);
    }
    cc_flush(platform);
    cc_set_depth(platform, depth_key);

    bool tev = cc_tev_supported(platform, quad);
    CcTevProgram *tev_program =
        tev ? cc_gles2_get_tev_program(&platform->tev_programs,
                                       platform->tev_vertex_shader,
                                       platform->fragment_highp, quad)
            : NULL;
    if (tev && !tev_program)
        tev = false;

    if (tev) {
        glUseProgram(tev_program->program);
        glUniform4fv(tev_program->registers_location, 3, &quad->registers[0][0]);
        glUniform4fv(tev_program->konst_location, 4, &quad->konst_colors[0][0]);
        for (unsigned unit = 0; unit < CC_MATERIAL_TEXTURES; ++unit) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
            GLuint texture = quad->textures[unit] ? (GLuint)quad->textures[unit]
                                                  : platform->white_texture;
            cc_bind_material_texture(platform, tev_program->sampling_locations[unit],
                                     texture, quad->nearest[unit]);
        }
    } else {
        glUseProgram(platform->material_program);
        int texture_count = (int)(quad->texture_count > 2 ? 2 : quad->texture_count);
        glUniform1i(platform->material_texture_count_location, texture_count);
        glUniform4fv(platform->material_registers_location, 3, &quad->registers[0][0]);
        glUniform4fv(platform->material_konst_location, 4, &quad->konst_colors[0][0]);
        bool alpha_test = quad->has_alpha_compare && !(quad->alpha_compare[0] == 0x77 &&
                                                       quad->alpha_compare[1] < 2);
        glUniform4f(platform->material_alpha_location,
                    alpha_test ? (float)quad->alpha_compare[0] : -1.0f,
                    (float)quad->alpha_compare[1], (float)quad->alpha_compare[2],
                    (float)quad->alpha_compare[3]);
        for (unsigned unit = 0; unit < 2; ++unit) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
            GLuint texture = quad->textures[unit] ? (GLuint)quad->textures[unit]
                                                  : platform->white_texture;
            cc_bind_material_texture(platform,
                                     platform->material_sampling_locations[unit],
                                     texture, quad->nearest[unit]);
            glUniform2f(platform->material_wrap_locations[unit],
                        (float)quad->wrap_s[unit], (float)quad->wrap_t[unit]);
        }
    }

    static const GLenum factors[8] = {GL_ZERO,      GL_ONE,
                                      GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                      GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    if (!blend.enabled) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(factors[blend.source], factors[blend.destination], GL_ONE,
                            GL_ONE_MINUS_SRC_ALPHA);
    }

    CcMaterialGpuVertex vertices[CC_VERTICES_PER_QUAD];
    for (size_t index = 0; index < CC_VERTICES_PER_QUAD; index++) {
        const CcMaterialVertex *source = &quad->vertices[cc_quad_triangle_order[index]];
        CcMaterialGpuVertex *target = &vertices[index];
        target->x = source->x;
        target->y = source->y;
        target->depth = source->depth;
        target->clip_w = cc_material_clip_w(source);
        target->color[0] = source->color.r;
        target->color[1] = source->color.g;
        target->color[2] = source->color.b;
        target->color[3] = source->color.a;
        memcpy(target->uv, source->uv, sizeof(target->uv));
    }
    glBindBuffer(GL_ARRAY_BUFFER, platform->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    if (tev) {
        glEnableVertexAttribArray(4);
        glEnableVertexAttribArray(5);
    } else {
        glDisableVertexAttribArray(4);
        glDisableVertexAttribArray(5);
    }
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(CcMaterialGpuVertex),
                          (const void *)offsetof(CcMaterialGpuVertex, x));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(CcMaterialGpuVertex),
                          (const void *)offsetof(CcMaterialGpuVertex, color));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(CcMaterialGpuVertex),
                          (const void *)offsetof(CcMaterialGpuVertex, uv[0]));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE,
                          (GLsizei)sizeof(CcMaterialGpuVertex),
                          (const void *)offsetof(CcMaterialGpuVertex, uv[1]));
    if (tev) {
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)sizeof(CcMaterialGpuVertex),
                              (const void *)offsetof(CcMaterialGpuVertex, uv[2]));
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE,
                              (GLsizei)sizeof(CcMaterialGpuVertex),
                              (const void *)offsetof(CcMaterialGpuVertex, uv[3]));
    }
    glDrawArrays(GL_TRIANGLES, 0, CC_VERTICES_PER_QUAD);
}

void cc_platform_end(CcPlatform *platform) {
    if (platform == NULL) {
        return;
    }
    if (!platform->rendering_target && platform->fade_alpha > 0.0f) {
        cc_platform_set_clip(platform, NULL);
        CcQuad cover = {.x = 0,
                        .y = 0,
                        .width = CC_FRAME_WIDTH,
                        .height = CC_FRAME_HEIGHT,
                        .u0 = 0,
                        .v0 = 0,
                        .u1 = 1,
                        .v1 = 1,
                        .color = {0, 0, 0, platform->fade_alpha},
                        .texture = 0};
        cc_platform_draw_quad(platform, &cover);
    }
    cc_flush(platform);
    if (platform->rendering_target) {
        cc_refresh_render_texture_mirror(platform);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        platform->rendering_target = false;
        return;
    }
    cc_gles2_retained_render(&platform->retained, platform, cc_flush);
    if (!cc_gles2_host_present(platform->host))
        cc_frame_damage_invalidate(platform->retained.commands);
}

void cc_platform_set_fade_alpha(CcPlatform *platform, float alpha) {
    if (platform)
        platform->fade_alpha = fminf(1.0f, fmaxf(0.0f, alpha));
}

uint32_t cc_platform_create_render_texture(CcPlatform *platform) {
    if (!platform || platform->render_texture != 0)
        return 0;
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (CC_FRAME_WIDTH > limit || CC_FRAME_HEIGHT > limit)
        return 0;

    GLuint texture = cc_upload_texture(platform, CC_FRAME_WIDTH, CC_FRAME_HEIGHT, NULL);
    if (!texture)
        return 0;
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    if (!framebuffer) {
        cc_delete_texture(platform, texture);
        return 0;
    }
    GLuint depth = 0;
    glGenRenderbuffers(1, &depth);
    if (!depth) {
        glDeleteFramebuffers(1, &framebuffer);
        cc_delete_texture(platform, texture);
        return 0;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, CC_FRAME_WIDTH,
                          CC_FRAME_HEIGHT);
    GLint prior = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prior);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture,
                           0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              depth);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prior);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteRenderbuffers(1, &depth);
        glDeleteFramebuffers(1, &framebuffer);
        cc_delete_texture(platform, texture);
        return 0;
    }
    platform->render_texture = texture;
    platform->render_framebuffer = framebuffer;
    platform->render_depth = depth;
    return (uint32_t)texture;
}

bool cc_platform_begin_target(CcPlatform *platform, uint32_t texture,
                              CcColor clear_color) {
    if (!platform || texture == 0 || texture != platform->render_texture ||
        platform->render_framebuffer == 0)
        return false;
    cc_gles2_retained_materialize(&platform->retained, platform, cc_flush);
    cc_frame_damage_invalidate(platform->retained.commands);
    platform->quad_count = 0;
    platform->rendering_target = true;
    platform->framebuffer_width = CC_FRAME_WIDTH;
    platform->framebuffer_height = CC_FRAME_HEIGHT;
    platform->presentation = (CcViewport){0, 0, CC_FRAME_WIDTH, CC_FRAME_HEIGHT};
    glBindFramebuffer(GL_FRAMEBUFFER, platform->render_framebuffer);
    glViewport(0, 0, CC_FRAME_WIDTH, CC_FRAME_HEIGHT);
    glDisable(GL_SCISSOR_TEST);
    platform->scissor_enabled = false;
    cc_clear_depth(platform);
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

uint32_t cc_platform_create_texture(CcPlatform *platform, int width, int height,
                                    const uint8_t *rgba) {
    if (platform == NULL || rgba == NULL || width <= 0 || height <= 0) {
        return 0;
    }

    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (width > limit || height > limit) {
        fprintf(stderr, "GLES2: texture exceeds the GPU size limit.\n");
        return 0;
    }
    cc_flush(platform);
    cc_frame_damage_invalidate(platform->retained.commands);
    return (uint32_t)cc_upload_texture(platform, width, height, rgba);
}

void cc_platform_destroy_texture(CcPlatform *platform, uint32_t texture) {
    if (platform == NULL || texture == 0 || texture == platform->white_texture) {
        return;
    }
    cc_gles2_retained_materialize(&platform->retained, platform, cc_flush);
    cc_frame_damage_invalidate(platform->retained.commands);
    cc_flush(platform);
    GLuint name = (GLuint)texture;
    if (name == platform->render_texture) {
        if (platform->rendering_target) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            platform->rendering_target = false;
        }
        glDeleteFramebuffers(1, &platform->render_framebuffer);
        glDeleteRenderbuffers(1, &platform->render_depth);
        platform->render_framebuffer = 0;
        platform->render_depth = 0;
        platform->render_texture = 0;
    }
    cc_delete_texture(platform, name);
}
