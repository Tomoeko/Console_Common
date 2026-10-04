#ifndef CONSOLE_COMMON_GLES2_RETAINED_FRAME_H
#define CONSOLE_COMMON_GLES2_RETAINED_FRAME_H

#include "frame_damage.h"

#ifdef _WIN32
#include "../windows/gl_api.h"
#else
#include <GLES2/gl2.h>
#endif

typedef struct CcGles2RetainedFrame {
    CcFrameDamage *commands;
    int width;
    int height;
    CcColor clear;
    CcClipRect clip;
    CcViewport region;
    bool allowed;
    bool active;
    bool recording;
    bool region_active;
} CcGles2RetainedFrame;

typedef void (*CcGles2Flush)(CcPlatform *platform);

void cc_gles2_retained_initialize(CcGles2RetainedFrame *frame);
void cc_gles2_retained_destroy(CcGles2RetainedFrame *frame);
bool cc_gles2_retained_begin(CcGles2RetainedFrame *frame, int width, int height,
                             CcColor clear);
void cc_gles2_retained_render(CcGles2RetainedFrame *frame, CcPlatform *platform,
                              CcGles2Flush flush);
/* Materialize deferred commands before destroying a referenced texture or
 * falling back to direct drawing when the fixed command storage fills. */
void cc_gles2_retained_materialize(CcGles2RetainedFrame *frame, CcPlatform *platform,
                                   CcGles2Flush flush);

#endif
