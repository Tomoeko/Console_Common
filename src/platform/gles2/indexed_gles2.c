#include "console_common/platform/indexed.h"

#include "console_common/render/viewport.h"
#include "console_common/support/error.h"
#include "indexed.h"
#include "indexed_pass.h"
#include "indexed_gles2.h"

#ifdef _WIN32
#include "../windows/gl_api.h"
#else
#include <GLES2/gl2.h>
#endif

#include <limits.h>
#include <stdlib.h>

struct CcIndexedProgram {
    CcIndexedRenderer *owner;
    CcIndexedProgram *next;
    GLuint program;
    GLint vertex_uniforms[CC_INDEXED_UNIFORMS];
    GLint fragment_uniforms[CC_INDEXED_UNIFORMS];
    size_t vertex_uniform_count;
    size_t fragment_uniform_count;
    size_t texture_count;
    size_t attribute_count;
    size_t attribute_offsets[CC_INDEXED_ATTRIBUTES];
    size_t vertex_stride;
    CcIndexedState state;
};

struct CcIndexedMesh {
    CcIndexedRenderer *owner;
    CcIndexedMesh *next;
    GLuint vertices;
    GLuint indices;
    size_t vertex_stride;
    size_t vertex_bytes;
    size_t index_count;
    bool dynamic_vertices;
};

struct CcIndexedTexture {
    CcIndexedTarget *target;
    CcIndexedRenderer *owner;
    CcIndexedTexture *next;
    GLuint texture;
};

struct CcIndexedTarget {
    CcIndexedRenderer *owner;
    CcIndexedTarget *next;
    CcIndexedTexture texture;
    CcIndexedTargetDescription description;
    GLuint framebuffer;
    GLuint depth;
    bool color_valid;
    bool depth_valid;
};

struct CcIndexedRenderer {
    CcPlatform *platform;
    CcGles2Host *host;
    CcIndexedProgram *programs;
    CcIndexedMesh *meshes;
    CcIndexedTexture *textures;
    CcIndexedFrame frame;
    CcIndexedTarget *targets;
    CcIndexedPassRecord *passes;
    size_t pass_capacity;
    size_t pass_count;
    int width;
    int height;
    GLint viewport_limits[2];
    bool drawable_pass;
    size_t capacity;
    size_t draw_count;
    bool active;
    bool failed;
};

static bool fail(char *error, size_t capacity, const char *message) {
    cc_error_set(error, capacity, message);
    return false;
}

static bool prepare_resources(CcIndexedRenderer *renderer, char *error,
                              size_t error_capacity) {
    if (!renderer || renderer->active || !cc_gles2_host_make_current(renderer->host))
        return fail(error, error_capacity, "indexed resource context unavailable");
    return true;
}

CcIndexedRenderer *cc_indexed_create(CcPlatform *platform, char *error,
                                     size_t error_capacity) {
    CcGles2Host *host = cc_gles2_platform_host(platform);
    if (!host || !cc_gles2_host_make_current(host)) {
        fail(error, error_capacity, "indexed native GLES2 host unavailable");
        return NULL;
    }
    CcIndexedRenderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) {
        fail(error, error_capacity, "cannot allocate indexed renderer");
        return NULL;
    }
    renderer->passes = calloc(1, sizeof(*renderer->passes));
    if (!renderer->passes) {
        free(renderer);
        fail(error, error_capacity, "cannot allocate indexed pass storage");
        return NULL;
    }
    renderer->pass_capacity = 1;
    renderer->host = host;
    renderer->platform = platform;
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, renderer->viewport_limits);
    if (glGetError() != GL_NO_ERROR || renderer->viewport_limits[0] <= 0 ||
        renderer->viewport_limits[1] <= 0) {
        free(renderer->passes);
        free(renderer);
        fail(error, error_capacity, "indexed viewport limits unavailable");
        return NULL;
    }
    return renderer;
}

void cc_indexed_destroy(CcIndexedRenderer *renderer) {
    if (!renderer)
        return;
    bool current = cc_gles2_host_make_current(renderer->host);
    if (current)
        cc_gles2_platform_invalidate_graphics(renderer->platform);
    while (renderer->programs) {
        CcIndexedProgram *program = renderer->programs;
        renderer->programs = program->next;
        if (current)
            glDeleteProgram(program->program);
        free(program);
    }
    while (renderer->meshes) {
        CcIndexedMesh *mesh = renderer->meshes;
        renderer->meshes = mesh->next;
        if (current) {
            glDeleteBuffers(1, &mesh->vertices);
            glDeleteBuffers(1, &mesh->indices);
        }
        free(mesh);
    }
    while (renderer->textures) {
        CcIndexedTexture *texture = renderer->textures;
        renderer->textures = texture->next;
        if (current)
            glDeleteTextures(1, &texture->texture);
        free(texture);
    }
    while (renderer->targets) {
        CcIndexedTarget *target = renderer->targets;
        renderer->targets = target->next;
        if (current) {
            glDeleteFramebuffers(1, &target->framebuffer);
            glDeleteRenderbuffers(1, &target->depth);
            glDeleteTextures(1, &target->texture.texture);
        }
        free(target);
    }
    free(renderer->passes);
    free(renderer);
}

bool cc_indexed_reserve(CcIndexedRenderer *renderer, size_t draw_count, char *error,
                        size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    renderer->capacity = draw_count;
    return true;
}

static GLuint compile_shader(GLenum stage, const char *source, char *error,
                             size_t error_capacity) {
    if (!source || !source[0]) {
        fail(error, error_capacity, "missing indexed GLES2 shader source");
        return 0;
    }
    GLuint shader = glCreateShader(stage);
    if (!shader) {
        fail(error, error_capacity, "cannot allocate indexed GLES2 shader");
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        if (error && error_capacity) {
            GLsizei length =
                error_capacity > INT_MAX ? INT_MAX : (GLsizei)error_capacity;
            glGetShaderInfoLog(shader, length, NULL, error);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(const CcIndexedProgramDescription *description, char *error,
                           size_t error_capacity) {
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, description->gles_vertex_source,
                                   error, error_capacity);
    if (!vertex)
        return 0;
    GLuint fragment = compile_shader(
        GL_FRAGMENT_SHADER, description->gles_fragment_source, error, error_capacity);
    GLuint program = fragment ? glCreateProgram() : 0;
    if (program) {
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        for (size_t index = 0; index < description->attribute_count; ++index)
            glBindAttribLocation(program, (GLuint)index,
                                 description->attributes[index].name);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            if (error && error_capacity) {
                GLsizei length =
                    error_capacity > INT_MAX ? INT_MAX : (GLsizei)error_capacity;
                glGetProgramInfoLog(program, length, NULL, error);
            }
            glDeleteProgram(program);
            program = 0;
        }
    } else if (fragment) {
        fail(error, error_capacity, "cannot allocate indexed GLES2 program");
    }
    glDeleteShader(vertex);
    if (fragment)
        glDeleteShader(fragment);
    return program;
}

CcIndexedProgram *
cc_indexed_program_create(CcIndexedRenderer *renderer,
                          const CcIndexedProgramDescription *description, char *error,
                          size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_program_validate(description, error, error_capacity))
        return NULL;
    CcIndexedProgram *program = calloc(1, sizeof(*program));
    if (!program) {
        fail(error, error_capacity, "cannot allocate indexed program");
        return NULL;
    }
    program->program = link_program(description, error, error_capacity);
    if (!program->program) {
        free(program);
        return NULL;
    }
    program->owner = renderer;
    program->state = description->state;
    program->vertex_stride = description->vertex_stride;
    program->attribute_count = description->attribute_count;
    program->vertex_uniform_count = description->vertex_uniform_count;
    program->fragment_uniform_count = description->fragment_uniform_count;
    program->texture_count = description->texture_count;
    for (size_t index = 0; index < program->attribute_count; ++index)
        program->attribute_offsets[index] = description->attributes[index].offset;
    for (size_t index = 0; index < program->vertex_uniform_count; ++index)
        program->vertex_uniforms[index] =
            glGetUniformLocation(program->program, description->vertex_uniforms[index]);
    for (size_t index = 0; index < program->fragment_uniform_count; ++index)
        program->fragment_uniforms[index] = glGetUniformLocation(
            program->program, description->fragment_uniforms[index]);
    glUseProgram(program->program);
    for (size_t index = 0; index < program->texture_count; ++index) {
        if (description->texture_uniforms[index]) {
            GLint location = glGetUniformLocation(program->program,
                                                  description->texture_uniforms[index]);
            glUniform1i(location, (GLint)index);
        }
    }
    program->next = renderer->programs;
    renderer->programs = program;
    return program;
}

static CcIndexedMesh *create_mesh(CcIndexedRenderer *renderer,
                                  const CcIndexedMeshDescription *description,
                                  char *error, size_t error_capacity,
                                  bool dynamic_vertices) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_mesh_validate(description, error, error_capacity))
        return NULL;
    CcIndexedMesh *mesh = calloc(1, sizeof(*mesh));
    if (!mesh) {
        fail(error, error_capacity, "cannot allocate indexed mesh");
        return NULL;
    }
    glGenBuffers(1, &mesh->vertices);
    glGenBuffers(1, &mesh->indices);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vertices);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(description->vertex_count * description->vertex_stride),
                 description->vertices,
                 dynamic_vertices ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(description->index_count * sizeof(uint16_t)),
                 description->indices, GL_STATIC_DRAW);
    if (!mesh->vertices || !mesh->indices || glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(1, &mesh->vertices);
        glDeleteBuffers(1, &mesh->indices);
        free(mesh);
        fail(error, error_capacity, "cannot upload indexed GLES2 mesh");
        return NULL;
    }
    mesh->owner = renderer;
    mesh->vertex_stride = description->vertex_stride;
    mesh->vertex_bytes = description->vertex_count * description->vertex_stride;
    mesh->index_count = description->index_count;
    mesh->dynamic_vertices = dynamic_vertices;
    mesh->next = renderer->meshes;
    renderer->meshes = mesh;
    return mesh;
}

CcIndexedMesh *cc_indexed_mesh_create(CcIndexedRenderer *renderer,
                                      const CcIndexedMeshDescription *description,
                                      char *error, size_t error_capacity) {
    return create_mesh(renderer, description, error, error_capacity, false);
}

CcIndexedMesh *
cc_indexed_mesh_create_dynamic(CcIndexedRenderer *renderer,
                               const CcIndexedMeshDescription *description, char *error,
                               size_t error_capacity) {
    return create_mesh(renderer, description, error, error_capacity, true);
}

bool cc_indexed_mesh_update(CcIndexedRenderer *renderer, CcIndexedMesh *mesh,
                            const void *vertices, size_t vertex_bytes, char *error,
                            size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    CcIndexedMesh *member = renderer->meshes;
    while (member && member != mesh)
        member = member->next;
    if (!member || !member->dynamic_vertices || !vertices ||
        vertex_bytes != member->vertex_bytes)
        return fail(error, error_capacity, "invalid indexed mesh update");
    glBindBuffer(GL_ARRAY_BUFFER, member->vertices);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vertex_bytes, vertices);
    return glGetError() == GL_NO_ERROR ||
           fail(error, error_capacity, "cannot update indexed GLES2 vertices");
}

bool cc_indexed_program_release(CcIndexedRenderer *renderer, CcIndexedProgram **program,
                                char *error, size_t error_capacity) {
    if (!program)
        return fail(error, error_capacity, "missing indexed program handle");
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    if (!*program)
        return true;
    CcIndexedProgram **slot = &renderer->programs;
    while (*slot && *slot != *program)
        slot = &(*slot)->next;
    if (!*slot)
        return fail(error, error_capacity,
                    "indexed program belongs to another renderer");
    CcIndexedProgram *released = *slot;
    *slot = released->next;
    glUseProgram(0);
    glDeleteProgram(released->program);
    free(released);
    *program = NULL;
    return true;
}

bool cc_indexed_mesh_release(CcIndexedRenderer *renderer, CcIndexedMesh **mesh,
                             char *error, size_t error_capacity) {
    if (!mesh)
        return fail(error, error_capacity, "missing indexed mesh handle");
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    if (!*mesh)
        return true;
    CcIndexedMesh **slot = &renderer->meshes;
    while (*slot && *slot != *mesh)
        slot = &(*slot)->next;
    if (!*slot)
        return fail(error, error_capacity, "indexed mesh belongs to another renderer");
    CcIndexedMesh *released = *slot;
    *slot = released->next;
    glDeleteBuffers(1, &released->vertices);
    glDeleteBuffers(1, &released->indices);
    free(released);
    *mesh = NULL;
    return true;
}

bool cc_indexed_texture_release(CcIndexedRenderer *renderer, CcIndexedTexture **texture,
                                char *error, size_t error_capacity) {
    if (!texture)
        return fail(error, error_capacity, "missing indexed texture handle");
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    if (!*texture)
        return true;
    CcIndexedTexture **slot = &renderer->textures;
    while (*slot && *slot != *texture)
        slot = &(*slot)->next;
    if (!*slot)
        return fail(error, error_capacity,
                    "indexed texture belongs to another renderer");
    CcIndexedTexture *released = *slot;
    *slot = released->next;
    glDeleteTextures(1, &released->texture);
    free(released);
    *texture = NULL;
    return true;
}

static GLint min_filter(const CcIndexedTextureDescription *description) {
    if (description->mip_filter == CC_INDEXED_MIP_NONE)
        return description->min_filter == CC_INDEXED_LINEAR ? GL_LINEAR : GL_NEAREST;
    if (description->mip_filter == CC_INDEXED_MIP_NEAREST)
        return description->min_filter == CC_INDEXED_LINEAR ? GL_LINEAR_MIPMAP_NEAREST
                                                            : GL_NEAREST_MIPMAP_NEAREST;
    return description->min_filter == CC_INDEXED_LINEAR ? GL_LINEAR_MIPMAP_LINEAR
                                                        : GL_NEAREST_MIPMAP_LINEAR;
}

static bool power_of_two(unsigned value) {
    return value && !(value & (value - 1));
}

CcIndexedTexture *
cc_indexed_texture_create(CcIndexedRenderer *renderer,
                          const CcIndexedTextureDescription *description, char *error,
                          size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_texture_validate(description, error, error_capacity))
        return NULL;
    float last_level = description->mip_filter == CC_INDEXED_MIP_NONE
                           ? 0.0f
                           : (float)(description->level_count - 1);
    if (description->max_anisotropy != 1 || description->min_lod != 0.0f ||
        description->max_lod < last_level) {
        fail(error, error_capacity, "indexed sampler exceeds core ES2 support");
        return NULL;
    }
    unsigned width = description->levels[0].width;
    unsigned height = description->levels[0].height;
    if ((!power_of_two(width) || !power_of_two(height)) &&
        (description->mip_filter != CC_INDEXED_MIP_NONE ||
         description->wrap_s != CC_INDEXED_CLAMP ||
         description->wrap_t != CC_INDEXED_CLAMP)) {
        fail(error, error_capacity, "indexed NPOT sampler requires ES2 clamp");
        return NULL;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (description->levels[0].width > (unsigned)limit ||
        description->levels[0].height > (unsigned)limit) {
        fail(error, error_capacity, "indexed texture exceeds GLES2 limit");
        return NULL;
    }
    CcIndexedTexture *texture = calloc(1, sizeof(*texture));
    if (!texture) {
        fail(error, error_capacity, "cannot allocate indexed texture");
        return NULL;
    }
    static const GLint wraps[] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};
    glGenTextures(1, &texture->texture);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter(description));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    description->mag_filter == CC_INDEXED_LINEAR ? GL_LINEAR
                                                                 : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[description->wrap_s]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[description->wrap_t]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t index = 0; index < description->level_count; ++index) {
        const CcIndexedMip *level = &description->levels[index];
        glTexImage2D(GL_TEXTURE_2D, (GLint)index, GL_RGBA, (GLsizei)level->width,
                     (GLsizei)level->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, level->rgba);
    }
    if (!texture->texture || glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture->texture);
        free(texture);
        fail(error, error_capacity, "cannot upload indexed GLES2 texture");
        return NULL;
    }
    texture->owner = renderer;
    texture->next = renderer->textures;
    renderer->textures = texture;
    return texture;
}

bool cc_indexed_begin_passes(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                             char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_frame_validate(frame, error, error_capacity))
        return false;
    cc_gles2_host_surface_size(renderer->host, &renderer->width, &renderer->height);
    if (renderer->width <= 0 || renderer->height <= 0)
        return fail(error, error_capacity, "indexed drawable has no area");
    renderer->frame = *frame;
    renderer->draw_count = 0;
    renderer->pass_count = 0;
    renderer->drawable_pass = false;
    renderer->failed = false;
    renderer->active = true;
    return true;
}

bool cc_indexed_begin(CcIndexedRenderer *renderer, const CcIndexedFrame *frame,
                      char *error, size_t error_capacity) {
    if (!cc_indexed_begin_passes(renderer, frame, error, error_capacity))
        return false;
    CcIndexedPass pass = {
        .viewport = cc_viewport_fit(renderer->width, renderer->height),
        .color_load = frame->clear_color_enabled ? CC_INDEXED_CLEAR : CC_INDEXED_LOAD,
        .depth_load = frame->clear_depth_enabled ? CC_INDEXED_CLEAR : CC_INDEXED_LOAD,
        .clear_color = frame->clear_color,
        .clear_depth = frame->clear_depth,
        .depth_attachment = frame->depth_attachment};
    if (cc_indexed_pass_begin(renderer, &pass, error, error_capacity))
        return true;
    renderer->active = false;
    cc_gles2_platform_invalidate_graphics(renderer->platform);
    return false;
}

static void apply_state(const CcIndexedState *state, bool depth_attachment) {
    static const GLenum factors[] = {GL_ZERO,          GL_ONE,
                                     GL_SRC_COLOR,     GL_ONE_MINUS_SRC_COLOR,
                                     GL_DST_COLOR,     GL_ONE_MINUS_DST_COLOR,
                                     GL_SRC_ALPHA,     GL_ONE_MINUS_SRC_ALPHA,
                                     GL_DST_ALPHA,     GL_ONE_MINUS_DST_ALPHA,
                                     GL_CONSTANT_COLOR};
    static const GLenum equations[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT,
                                       GL_FUNC_REVERSE_SUBTRACT};
    if (state->blend) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(equations[state->equation_rgb],
                                equations[state->equation_alpha]);
        glBlendFuncSeparate(factors[state->source_rgb], factors[state->destination_rgb],
                            factors[state->source_alpha],
                            factors[state->destination_alpha]);
        glBlendColor(state->blend_color[0], state->blend_color[1],
                     state->blend_color[2], state->blend_color[3]);
    } else {
        glDisable(GL_BLEND);
    }
    if (state->depth_test && depth_attachment) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc((GLenum)(GL_NEVER + state->depth_compare));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(state->depth_write && depth_attachment ? GL_TRUE : GL_FALSE);
    if (state->cull != CC_INDEXED_CULL_NONE) {
        glEnable(GL_CULL_FACE);
        glCullFace(state->cull == CC_INDEXED_CULL_BACK ? GL_BACK : GL_FRONT);
    } else {
        glDisable(GL_CULL_FACE);
    }
    glFrontFace(state->counterclockwise_front ? GL_CCW : GL_CW);
    glColorMask(state->color_write[0] ? GL_TRUE : GL_FALSE,
                state->color_write[1] ? GL_TRUE : GL_FALSE,
                state->color_write[2] ? GL_TRUE : GL_FALSE,
                state->color_write[3] ? GL_TRUE : GL_FALSE);
}

static bool draw_validate(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                          char *error, size_t error_capacity) {
    if (!renderer || !renderer->active || renderer->failed || !renderer->pass_count ||
        !draw || !draw->program || !draw->mesh ||
        renderer->draw_count >= renderer->capacity ||
        draw->program->owner != renderer || draw->mesh->owner != renderer ||
        draw->mesh->vertex_stride != draw->program->vertex_stride ||
        draw->first_index > draw->mesh->index_count ||
        draw->index_count > draw->mesh->index_count - draw->first_index ||
        draw->first_index % 3 || draw->index_count % 3 ||
        draw->texture_count != draw->program->texture_count)
        return fail(error, error_capacity, "invalid indexed draw ownership or range");
    if (!cc_indexed_uniforms_validate(draw->vertex_uniforms, draw->vertex_uniform_count,
                                      draw->program->vertex_uniform_count, error,
                                      error_capacity) ||
        !cc_indexed_uniforms_validate(
            draw->fragment_uniforms, draw->fragment_uniform_count,
            draw->program->fragment_uniform_count, error, error_capacity))
        return false;
    for (size_t index = 0; index < draw->texture_count; ++index) {
        CcIndexedTexture *texture = draw->textures[index];
        bool member = false;
        for (CcIndexedTexture *owned = renderer->textures; owned; owned = owned->next)
            member |= owned == texture;
        for (CcIndexedTarget *target = renderer->targets; target; target = target->next)
            member |= &target->texture == texture;
        if (!member)
            return fail(error, error_capacity, "invalid indexed texture ownership");
        if (texture->target) {
            if (renderer->passes[renderer->pass_count - 1].description.target ==
                texture->target)
                return fail(error, error_capacity,
                            "indexed target feedback is unsupported");
            bool color_valid = texture->target->color_valid;
            bool depth_valid = texture->target->depth_valid;
            cc_indexed_pass_content(renderer->passes, renderer->pass_count - 1,
                                    texture->target, &color_valid, &depth_valid);
            if (!color_valid)
                return fail(error, error_capacity,
                            "indexed target color is uninitialized");
        }
    }
    return true;
}

bool cc_indexed_draw(CcIndexedRenderer *renderer, const CcIndexedDraw *draw,
                     char *error, size_t error_capacity) {
    if (!draw_validate(renderer, draw, error, error_capacity))
        return false;
    CcIndexedProgram *program = draw->program;
    glUseProgram(program->program);
    apply_state(
        &program->state,
        renderer->passes[renderer->pass_count - 1].description.depth_attachment);
    glBindBuffer(GL_ARRAY_BUFFER, draw->mesh->vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, draw->mesh->indices);
    for (size_t index = 0; index < CC_INDEXED_ATTRIBUTES; ++index) {
        if (index < program->attribute_count) {
            glEnableVertexAttribArray((GLuint)index);
            glVertexAttribPointer((GLuint)index, 4, GL_FLOAT, GL_FALSE,
                                  (GLsizei)program->vertex_stride,
                                  (const void *)program->attribute_offsets[index]);
        } else {
            glDisableVertexAttribArray((GLuint)index);
        }
    }
    for (size_t index = 0; index < program->vertex_uniform_count; ++index)
        glUniform4fv(program->vertex_uniforms[index], 1, draw->vertex_uniforms[index]);
    for (size_t index = 0; index < program->fragment_uniform_count; ++index)
        glUniform4fv(program->fragment_uniforms[index], 1,
                     draw->fragment_uniforms[index]);
    for (size_t index = 0; index < draw->texture_count; ++index) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + index));
        glBindTexture(GL_TEXTURE_2D, draw->textures[index]->texture);
    }
    glDrawElements(GL_TRIANGLES, (GLsizei)draw->index_count, GL_UNSIGNED_SHORT,
                   (const void *)(draw->first_index * sizeof(uint16_t)));
    ++renderer->draw_count;
    ++renderer->passes[renderer->pass_count - 1].draw_count;
    return true;
}

bool cc_indexed_end(CcIndexedRenderer *renderer, char *error, size_t error_capacity) {
    if (!renderer || !renderer->active)
        return fail(error, error_capacity, "indexed frame is not active");
    renderer->active = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bool valid = glGetError() == GL_NO_ERROR && !renderer->failed;
    if (!renderer->drawable_pass) {
        for (size_t index = 0; index < renderer->pass_count; ++index) {
            CcIndexedTarget *target = renderer->passes[index].description.target;
            if (target) {
                target->color_valid = false;
                target->depth_valid = false;
            }
        }
        cc_gles2_platform_invalidate_graphics(renderer->platform);
        return fail(error, error_capacity, "indexed frame has no drawable pass");
    }
    for (size_t index = 0; index < renderer->pass_count; ++index) {
        CcIndexedPassRecord *pass = &renderer->passes[index];
        if (pass->description.target) {
            pass->description.target->color_valid = valid && pass->color_valid;
            pass->description.target->depth_valid = valid && pass->depth_valid;
        }
    }
    bool presented = valid && cc_gles2_host_present(renderer->host);
    cc_gles2_platform_invalidate_graphics(renderer->platform);
    if (!valid || !presented)
        return fail(error, error_capacity, "indexed GLES2 frame failed");
    return true;
}

bool cc_indexed_wait(CcIndexedRenderer *renderer, char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    glFinish();
    if (glGetError() != GL_NO_ERROR)
        return fail(error, error_capacity, "indexed GLES2 execution failed");
    return true;
}

bool cc_indexed_prepare_drawable_depth(CcIndexedRenderer *renderer, char *error,
                                       size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLint depth_bits = 0;
    glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
    if (depth_bits <= 0 || glGetError() != GL_NO_ERROR)
        return fail(error, error_capacity, "indexed drawable has no depth storage");
    return true;
}

bool cc_indexed_reserve_passes(CcIndexedRenderer *renderer, size_t pass_count,
                               char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity))
        return false;
    if (pass_count > SIZE_MAX / sizeof(*renderer->passes))
        return fail(error, error_capacity, "indexed pass capacity overflows storage");
    if (pass_count <= renderer->pass_capacity)
        return true;
    CcIndexedPassRecord *passes = calloc(pass_count, sizeof(*passes));
    if (!passes)
        return fail(error, error_capacity, "cannot allocate indexed pass storage");
    free(renderer->passes);
    renderer->passes = passes;
    renderer->pass_capacity = pass_count;
    return true;
}

static void delete_target(CcIndexedTarget *target) {
    glDeleteFramebuffers(1, &target->framebuffer);
    glDeleteRenderbuffers(1, &target->depth);
    glDeleteTextures(1, &target->texture.texture);
    free(target);
}

CcIndexedTarget *cc_indexed_target_create(CcIndexedRenderer *renderer,
                                          const CcIndexedTargetDescription *description,
                                          char *error, size_t error_capacity) {
    if (!prepare_resources(renderer, error, error_capacity) ||
        !cc_indexed_target_validate(description, error, error_capacity))
        return NULL;
    if (description->color_format != CC_INDEXED_RGBA8) {
        fail(error, error_capacity,
             "indexed half-float targets are unsupported by core GLES2");
        return NULL;
    }
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    GLint depth_maximum = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &depth_maximum);
    if (maximum <= 0 || description->width > (unsigned)maximum ||
        description->height > (unsigned)maximum ||
        (description->depth_attachment &&
         (depth_maximum <= 0 || description->width > (unsigned)depth_maximum ||
          description->height > (unsigned)depth_maximum))) {
        fail(error, error_capacity, "indexed target exceeds GLES2 attachment limit");
        return NULL;
    }
    CcIndexedTarget *target = calloc(1, sizeof(*target));
    if (!target) {
        fail(error, error_capacity, "cannot allocate indexed target handle");
        return NULL;
    }
    glGenTextures(1, &target->texture.texture);
    glBindTexture(GL_TEXTURE_2D, target->texture.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    description->min_filter == CC_INDEXED_LINEAR ? GL_LINEAR
                                                                 : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    description->mag_filter == CC_INDEXED_LINEAR ? GL_LINEAR
                                                                 : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)description->width,
                 (GLsizei)description->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &target->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target->texture.texture, 0);
    if (description->depth_attachment) {
        glGenRenderbuffers(1, &target->depth);
        glBindRenderbuffer(GL_RENDERBUFFER, target->depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                              (GLsizei)description->width,
                              (GLsizei)description->height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  target->depth);
    }
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (!target->texture.texture || !target->framebuffer ||
        (description->depth_attachment && !target->depth) || !complete ||
        glGetError() != GL_NO_ERROR) {
        delete_target(target);
        fail(error, error_capacity, "cannot create complete indexed GLES2 target");
        return NULL;
    }
    target->owner = renderer;
    target->description = *description;
    target->texture.owner = renderer;
    target->texture.target = target;
    target->next = renderer->targets;
    renderer->targets = target;
    return target;
}

CcIndexedTexture *cc_indexed_target_texture(CcIndexedRenderer *renderer,
                                            CcIndexedTarget *target) {
    if (!renderer)
        return NULL;
    for (CcIndexedTarget *member = renderer->targets; member; member = member->next) {
        if (member == target)
            return &member->texture;
    }
    return NULL;
}

bool cc_indexed_target_release(CcIndexedRenderer *renderer, CcIndexedTarget **target,
                               char *error, size_t error_capacity) {
    if (!target || !prepare_resources(renderer, error, error_capacity))
        return false;
    if (!*target)
        return true;
    CcIndexedTarget **slot = &renderer->targets;
    while (*slot && *slot != *target)
        slot = &(*slot)->next;
    if (!*slot)
        return fail(error, error_capacity,
                    "indexed target belongs to another renderer");
    CcIndexedTarget *released = *slot;
    *slot = released->next;
    delete_target(released);
    *target = NULL;
    return true;
}

bool cc_indexed_pass_begin(CcIndexedRenderer *renderer, const CcIndexedPass *pass,
                           char *error, size_t error_capacity) {
    if (!renderer || !renderer->active || renderer->failed || !pass ||
        renderer->pass_count >= renderer->pass_capacity)
        return fail(error, error_capacity,
                    "indexed pass state or capacity unavailable");
    CcIndexedTarget *target = NULL;
    if (pass->target) {
        for (CcIndexedTarget *member = renderer->targets; member;
             member = member->next) {
            if (member == pass->target)
                target = member;
        }
        if (!target)
            return fail(error, error_capacity,
                        "indexed target belongs to another renderer");
    }
    unsigned width = target ? target->description.width : (unsigned)renderer->width;
    unsigned height = target ? target->description.height : (unsigned)renderer->height;
    bool color_valid = target ? target->color_valid : true;
    bool depth_valid = target ? target->depth_valid : true;
    if (target)
        cc_indexed_pass_content(renderer->passes, renderer->pass_count, target,
                                &color_valid, &depth_valid);
    bool depth_available = target ? target->description.depth_attachment
                                  : renderer->frame.depth_attachment;
    if (!cc_indexed_pass_validate(pass, width, height, depth_available, color_valid,
                                  depth_valid, error, error_capacity))
        return false;
    if (pass->viewport.width > renderer->viewport_limits[0] ||
        pass->viewport.height > renderer->viewport_limits[1])
        return fail(error, error_capacity, "indexed viewport exceeds GLES2 dimensions");
    glBindFramebuffer(GL_FRAMEBUFFER, target ? target->framebuffer : 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLbitfield clear = 0;
    if (pass->color_load == CC_INDEXED_CLEAR) {
        glClearColor(pass->clear_color.r, pass->clear_color.g, pass->clear_color.b,
                     pass->clear_color.a);
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (pass->depth_attachment && pass->depth_load == CC_INDEXED_CLEAR) {
        glDepthMask(GL_TRUE);
        glClearDepthf(pass->clear_depth);
        clear |= GL_DEPTH_BUFFER_BIT;
    }
    /* DISCARD requires no optional invalidate extension. Contents are undefined
     * until the caller's declared complete writes finish. */
    if (clear)
        glClear(clear);
    CcViewport viewport = pass->viewport;
    GLint bottom = (GLint)((int64_t)height - viewport.y - viewport.height);
    glViewport(viewport.x, bottom, viewport.width, viewport.height);
    CcViewport scissor = cc_indexed_pass_scissor(pass);
    GLint scissor_bottom = (GLint)((int64_t)height - scissor.y - scissor.height);
    glScissor(scissor.x, scissor_bottom, scissor.width, scissor.height);
    glEnable(GL_SCISSOR_TEST);
    if (glGetError() != GL_NO_ERROR) {
        renderer->failed = true;
        if (target) {
            target->color_valid = false;
            target->depth_valid = false;
        }
        return fail(error, error_capacity, "cannot begin indexed GLES2 pass");
    }
    renderer->passes[renderer->pass_count++] =
        cc_indexed_pass_record(pass, renderer->draw_count, color_valid, depth_valid);
    renderer->drawable_pass |= target == NULL;
    return true;
}
