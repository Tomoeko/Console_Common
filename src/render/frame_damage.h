#ifndef CONSOLE_COMMON_FRAME_DAMAGE_H
#define CONSOLE_COMMON_FRAME_DAMAGE_H

#include "console_common/platform/platform.h"
#include "console_common/render/viewport.h"

#include <stddef.h>

enum { CC_FRAME_COMMAND_CAPACITY = 2048 };

typedef enum CcFrameCommandKind {
    CC_FRAME_COMMAND_QUAD,
    CC_FRAME_COMMAND_MATERIAL
} CcFrameCommandKind;

typedef struct CcFrameCommand {
    CcFrameCommandKind kind;
    CcClipRect clip;
    CcClipRect bounds;
    uint32_t texture;
    union {
        CcDrawVertex vertices[4];
        CcMaterialQuad material;
    } draw;
} CcFrameCommand;

typedef struct CcFrameDamage CcFrameDamage;

/* Fixed-capacity storage is allocated once. Commands own copied draw data;
 * textures remain owned by the caller until recorded draws have completed. */
CcFrameDamage *cc_frame_damage_create(void);
void cc_frame_damage_destroy(CcFrameDamage *damage);
void cc_frame_damage_begin(CcFrameDamage *damage, int width, int height, CcColor clear);
bool cc_frame_damage_quad(CcFrameDamage *damage, const CcDrawVertex vertices[4],
                          uint32_t texture, const CcClipRect *clip);
bool cc_frame_damage_material(CcFrameDamage *damage, const CcMaterialQuad *quad,
                              const CcClipRect *clip);
const CcFrameCommand *cc_frame_damage_commands(const CcFrameDamage *damage,
                                               size_t *count);
/* Disjoint, top-left framebuffer rectangles. Replaying every intersecting
 * command in order after clearing each region preserves alpha composition. */
const CcViewport *cc_frame_damage_regions(CcFrameDamage *damage, size_t *count);
bool cc_frame_command_intersects(const CcFrameCommand *command, CcViewport region,
                                 int width, int height);
void cc_frame_damage_commit(CcFrameDamage *damage);
void cc_frame_damage_invalidate(CcFrameDamage *damage);

#endif
