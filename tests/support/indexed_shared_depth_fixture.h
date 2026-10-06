#ifndef CC_TEST_INDEXED_SHARED_DEPTH_FIXTURE_H
#define CC_TEST_INDEXED_SHARED_DEPTH_FIXTURE_H

#include "indexed_pass_fixture.h"

static CcIndexedTarget *shared_depth_target(CcIndexedRenderer *renderer,
                                            CcIndexedTarget *source) {
    CcIndexedTargetDescription description = {
        .width = 32, .height = 16, .depth_attachment = true, .depth_source = source};
    CcIndexedTarget *target = cc_indexed_target_create(renderer, &description, NULL, 0);
    PASS_REQUIRE(target);
    return target;
}

static void shared_depth_admission(CcIndexedRenderer *renderer,
                                   CcIndexedRenderer *other) {
    CcIndexedTarget *owner = pass_target(renderer, CC_INDEXED_RGBA8, true);
    CcIndexedTarget *depthless = pass_target(renderer, CC_INDEXED_RGBA8, false);
    CcIndexedTargetDescription description = {
        .width = 32, .height = 16, .depth_attachment = true, .depth_source = owner};
    PASS_REQUIRE(!cc_indexed_target_create(other, &description, NULL, 0));
    description.depth_source = (CcIndexedTarget *)(uintptr_t)1;
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &description, NULL, 0));
    description.depth_source = depthless;
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &description, NULL, 0));
    description.depth_source = owner;
    description.width = 16;
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &description, NULL, 0));
    description.width = 32;
    description.depth_attachment = false;
    PASS_REQUIRE(!cc_indexed_target_create(renderer, &description, NULL, 0));
    CcIndexedTarget *borrower = shared_depth_target(renderer, owner);
    CcIndexedTarget *chain = shared_depth_target(renderer, borrower);
    CcIndexedTarget *retained = owner;
    PASS_REQUIRE(!cc_indexed_target_release(renderer, &owner, NULL, 0));
    PASS_REQUIRE(owner == retained);
    PASS_REQUIRE(cc_indexed_target_release(renderer, &borrower, NULL, 0));
    PASS_REQUIRE(!cc_indexed_target_release(renderer, &owner, NULL, 0));

    PASS_REQUIRE(cc_indexed_reserve_passes(renderer, 10, NULL, 0));
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    CcIndexedPass pass = pass_description(chain, CC_INDEXED_CLEAR);
    pass.depth_attachment = true;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = owner;
    pass.depth_load = CC_INDEXED_CLEAR;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = chain;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.depth_load = CC_INDEXED_DISCARD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = owner;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.depth_attachment = false;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.depth_attachment = true;
    PASS_REQUIRE(!cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.depth_load = CC_INDEXED_CLEAR;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = chain;
    pass.depth_attachment = false;
    pass.depth_load = CC_INDEXED_DISCARD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = owner;
    pass.depth_attachment = true;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass = pass_description(NULL, CC_INDEXED_CLEAR);
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));

    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    pass = pass_description(chain, CC_INDEXED_LOAD);
    pass.depth_attachment = true;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass = pass_description(NULL, CC_INDEXED_CLEAR);
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &chain, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &owner, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &depthless, NULL, 0));
}

/* Equal-depth geometry must be rejected through the second color target. A
 * later clear through that target must allow drawing through the first one. */
static void shared_depth_scene(CcIndexedRenderer *renderer, int width, int height) {
    PASS_REQUIRE(cc_indexed_reserve(renderer, 5, NULL, 0));
    PASS_REQUIRE(cc_indexed_reserve_passes(renderer, 6, NULL, 0));
    CcIndexedProgram *program = pass_program(renderer);
    CcIndexedMesh *mesh = pass_mesh(renderer);
    CcIndexedTexture *white = pass_white(renderer);
    CcIndexedTarget *first = pass_target(renderer, CC_INDEXED_RGBA8, true);
    CcIndexedTarget *second = shared_depth_target(renderer, first);
    float tint[1][4] = {{1, 0, 0, 1}};
    CcIndexedDraw draw = {.program = program,
                          .mesh = mesh,
                          .fragment_uniforms = (const float(*)[4])tint,
                          .fragment_uniform_count = 1,
                          .textures = {white},
                          .texture_count = 1,
                          .index_count = 6};
    CcIndexedFrame frame = {0};
    PASS_REQUIRE(cc_indexed_begin_passes(renderer, &frame, NULL, 0));
    CcIndexedPass pass = pass_description(first, CC_INDEXED_CLEAR);
    pass.depth_attachment = true;
    pass.depth_load = CC_INDEXED_CLEAR;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));

    pass.target = second;
    pass.clear_color = (CcColor){0, 1, 0, 1};
    pass.depth_load = CC_INDEXED_LOAD;
    tint[0][0] = 0;
    tint[0][2] = 1;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass.color_load = CC_INDEXED_LOAD;
    pass.depth_load = CC_INDEXED_CLEAR;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    pass.target = first;
    pass.depth_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));

    pass = pass_description(NULL, CC_INDEXED_CLEAR);
    pass.viewport = (CcViewport){0, 0, width / 2, height};
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    tint[0][0] = tint[0][1] = tint[0][2] = 1;
    draw.textures[0] = cc_indexed_target_texture(renderer, first);
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    pass.viewport.x = width / 2;
    pass.color_load = CC_INDEXED_LOAD;
    PASS_REQUIRE(cc_indexed_pass_begin(renderer, &pass, NULL, 0));
    draw.textures[0] = cc_indexed_target_texture(renderer, second);
    PASS_REQUIRE(cc_indexed_draw(renderer, &draw, NULL, 0));
    PASS_REQUIRE(cc_indexed_end(renderer, NULL, 0));
    PASS_REQUIRE(!cc_indexed_target_release(renderer, &first, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &second, NULL, 0));
    PASS_REQUIRE(cc_indexed_target_release(renderer, &first, NULL, 0));
    PASS_REQUIRE(cc_indexed_texture_release(renderer, &white, NULL, 0));
    PASS_REQUIRE(cc_indexed_mesh_release(renderer, &mesh, NULL, 0));
    PASS_REQUIRE(cc_indexed_program_release(renderer, &program, NULL, 0));
}

static void shared_depth_pixels(const CcFramebuffer *frame) {
    const uint8_t blue[] = {0, 0, 255, 255};
    const uint8_t green[] = {0, 255, 0, 255};
    pass_pixel(frame, frame->width / 4, frame->height / 2, blue);
    pass_pixel(frame, frame->width * 3 / 4, frame->height / 2, green);
}

#endif
