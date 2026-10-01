#include "retained_frame.h"

#include <stdlib.h>
#include <string.h>

void cc_gles2_retained_initialize(CcGles2RetainedFrame *frame) {
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *setting = getenv("CC_GLES2_RETAIN_FRAME");
    frame->allowed =
        renderer && (strstr(renderer, "llvmpipe") || strstr(renderer, "softpipe"));
    if (setting)
        frame->allowed = strcmp(setting, "0") != 0;
    if (frame->allowed) {
        frame->commands = cc_frame_damage_create();
        if (!frame->commands)
            frame->allowed = false;
    }
}

void cc_gles2_retained_destroy(CcGles2RetainedFrame *frame) {
    cc_frame_damage_destroy(frame->commands);
    memset(frame, 0, sizeof(*frame));
}

bool cc_gles2_retained_begin(CcGles2RetainedFrame *frame, int width, int height,
                             CcColor clear) {
    frame->active = false;
    frame->recording = false;
    if (!frame->allowed || width <= 0 || height <= 0)
        return false;
    frame->width = width;
    frame->height = height;
    frame->clear = clear;
    frame->clip = (CcClipRect){0, 0, CC_FRAME_WIDTH, CC_FRAME_HEIGHT};
    frame->active = true;
    frame->recording = true;
    frame->region_active = false;
    cc_frame_damage_begin(frame->commands, width, height, clear);
    return true;
}

static void replay_region(CcGles2RetainedFrame *frame, CcPlatform *platform,
                          CcGles2Flush flush, CcViewport region) {
    frame->region = region;
    frame->region_active = true;
    cc_platform_set_clip(platform, NULL);
    glClearColor(frame->clear.r, frame->clear.g, frame->clear.b, frame->clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    size_t count = 0;
    const CcFrameCommand *commands = cc_frame_damage_commands(frame->commands, &count);
    for (size_t index = 0; index < count; index++) {
        const CcFrameCommand *command = &commands[index];
        if (!cc_frame_command_intersects(command, region, frame->width, frame->height))
            continue;
        cc_platform_set_clip(platform, &command->clip);
        if (command->kind == CC_FRAME_COMMAND_MATERIAL) {
            cc_platform_draw_material_quad(platform, &command->draw.material);
        } else {
            cc_platform_draw_vertices(platform, command->draw.vertices,
                                      command->texture);
        }
    }
    flush(platform);
}

void cc_gles2_retained_render(CcGles2RetainedFrame *frame, CcPlatform *platform,
                              CcGles2Flush flush) {
    if (!frame->recording)
        return;
    frame->recording = false;
    size_t count = 0;
    const CcViewport *regions = cc_frame_damage_regions(frame->commands, &count);
    for (size_t index = 0; index < count; index++)
        replay_region(frame, platform, flush, regions[index]);
    frame->region_active = false;
    cc_platform_set_clip(platform, NULL);
    cc_frame_damage_commit(frame->commands);
}

void cc_gles2_retained_materialize(CcGles2RetainedFrame *frame, CcPlatform *platform,
                                   CcGles2Flush flush) {
    if (!frame->recording)
        return;
    frame->recording = false;
    replay_region(frame, platform, flush,
                  (CcViewport){0, 0, frame->width, frame->height});
    frame->region_active = false;
    cc_platform_set_clip(platform, &frame->clip);
    cc_frame_damage_invalidate(frame->commands);
}
