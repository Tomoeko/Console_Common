#ifndef CONSOLE_COMMON_GLES2_SHADERS_H
#define CONSOLE_COMMON_GLES2_SHADERS_H

#include "console_common/platform/platform.h"
#include "material.h"

#include <GLES2/gl2.h>

#include <stdbool.h>
#include <stdint.h>

enum { CC_ES2_TEV_STAGES = CC_RENDER_TEV_STAGES };

typedef struct CcTevKey {
    uint8_t stage_count;
    uint8_t stages[CC_ES2_TEV_STAGES][16];
    uint8_t swap[4];
    uint8_t wrap_s[CC_MATERIAL_TEXTURES];
    uint8_t wrap_t[CC_MATERIAL_TEXTURES];
    uint8_t has_alpha_compare;
    uint8_t alpha_compare[4];
} CcTevKey;

typedef struct CcTevProgram {
    CcTevKey key;
    GLuint program;
    GLint registers_location;
    GLint konst_location;
    GLint sampling_locations[CC_MATERIAL_TEXTURES];
    struct CcTevProgram *next;
} CcTevProgram;

GLuint cc_gles2_create_quad_program(void);
GLuint cc_gles2_create_material_program(void);
GLuint cc_gles2_create_tev_vertex_shader(bool fragment_highp);

/* The caller owns the cache and releases its programs before the GL context.
 * Failed entries remain cached to avoid recompiling during later draws. */
CcTevProgram *cc_gles2_get_tev_program(CcTevProgram **programs, GLuint vertex_shader,
                                       bool fragment_highp, const CcMaterialQuad *quad);

#endif
