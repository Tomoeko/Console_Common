#include "geometry.h"
#include "frame_damage.h"
#include "material_depth.h"

#include <assert.h>

static void assert_corner(CcDrawVertex corner, float x, float y, float u, float v,
                          CcColor color) {
    assert(corner.x == x);
    assert(corner.y == y);
    assert(corner.u == u);
    assert(corner.v == v);
    assert(corner.color.r == color.r);
    assert(corner.color.g == color.g);
    assert(corner.color.b == color.b);
    assert(corner.color.a == color.a);
}

static void test_quad_corners(void) {
    const CcQuad quad = {.x = 12.5f,
                         .y = -3.0f,
                         .width = 8.0f,
                         .height = 5.5f,
                         .u0 = 0.125f,
                         .v0 = 0.25f,
                         .u1 = 0.75f,
                         .v1 = 0.875f,
                         .color = {0.25f, 0.5f, 0.75f, 1.0f}};
    CcDrawVertex corners[CC_QUAD_CORNERS];
    cc_render_quad_corners(&quad, corners);

    assert_corner(corners[0], 12.5f, -3.0f, 0.125f, 0.25f, quad.color);
    assert_corner(corners[1], 20.5f, -3.0f, 0.75f, 0.25f, quad.color);
    assert_corner(corners[2], 12.5f, 2.5f, 0.125f, 0.875f, quad.color);
    assert_corner(corners[3], 20.5f, 2.5f, 0.75f, 0.875f, quad.color);
}

static void test_signed_dimensions_and_uvs(void) {
    /* Extent policy belongs to the caller. Preserve flipped geometry and
     * image coordinates when producing corners for a drawable quad. */
    const CcQuad quad = {.x = 20.0f,
                         .y = 15.0f,
                         .width = -8.0f,
                         .height = -5.0f,
                         .u0 = 1.0f,
                         .v0 = 1.0f,
                         .u1 = 0.0f,
                         .v1 = 0.0f,
                         .color = {1.0f, 1.0f, 1.0f, 0.5f}};
    CcDrawVertex corners[CC_QUAD_CORNERS];
    cc_render_quad_corners(&quad, corners);

    assert_corner(corners[0], 20.0f, 15.0f, 1.0f, 1.0f, quad.color);
    assert_corner(corners[1], 12.0f, 15.0f, 0.0f, 1.0f, quad.color);
    assert_corner(corners[2], 20.0f, 10.0f, 1.0f, 0.0f, quad.color);
    assert_corner(corners[3], 12.0f, 10.0f, 0.0f, 0.0f, quad.color);
}

static float triangle_area(const CcDrawVertex corners[CC_QUAD_CORNERS],
                           unsigned first) {
    const CcDrawVertex a = corners[cc_quad_triangle_order[first]];
    const CcDrawVertex b = corners[cc_quad_triangle_order[first + 1]];
    const CcDrawVertex c = corners[cc_quad_triangle_order[first + 2]];
    return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5f;
}

static void test_quad_triangulation(void) {
    const CcQuad quad = {.width = 8.0f, .height = 5.0f};
    CcDrawVertex corners[CC_QUAD_CORNERS];
    cc_render_quad_corners(&quad, corners);

    /* The top-left to bottom-right diagonal matters when vertex attributes
     * differ across corners. Both triangles must preserve positive winding. */
    assert(cc_quad_triangle_order[0] == 0);
    assert(cc_quad_triangle_order[1] == 1);
    assert(cc_quad_triangle_order[2] == 3);
    assert(cc_quad_triangle_order[3] == 0);
    assert(cc_quad_triangle_order[4] == 3);
    assert(cc_quad_triangle_order[5] == 2);
    assert(triangle_area(corners, 0) == 20.0f);
    assert(triangle_area(corners, 3) == 20.0f);
}

static void test_material_depth_snapshot(void) {
    CcMaterialQuad quad = {0};
    unsigned key;
    assert(cc_material_depth_key(&quad, &key) && key == 0);
    quad.has_depth_mode = true;
    quad.depth_mode[0] = 1;
    quad.depth_mode[1] = 3;
    quad.depth_mode[2] = 1;
    quad.vertices[0].depth = 0.25f;
    quad.vertices[0].clip_w = 2;
    assert(cc_material_depth_key(&quad, &key) && key == 8);
    assert(cc_material_clip_w(&quad.vertices[0]) == 2);
    assert(cc_material_clip_w(&quad.vertices[1]) == 1);

    CcFrameDamage *damage = cc_frame_damage_create();
    assert(damage);
    cc_frame_damage_begin(damage, 640, 480, (CcColor){0, 0, 0, 1});
    assert(cc_frame_damage_material(damage, &quad, NULL));
    quad.vertices[0].depth = 0.75f;
    quad.depth_mode[2] = 0;
    size_t count;
    const CcFrameCommand *commands = cc_frame_damage_commands(damage, &count);
    assert(count == 1 && commands[0].kind == CC_FRAME_COMMAND_MATERIAL);
    const CcMaterialQuad *snapshot = &commands[0].draw.material;
    assert(snapshot->vertices[0].depth == 0.25f);
    assert(snapshot->vertices[0].clip_w == 2);
    assert(snapshot->has_depth_mode && snapshot->depth_mode[2] == 1);
    assert(cc_material_depth_key(snapshot, &key) && key == 8);
    cc_frame_damage_destroy(damage);

    quad.vertices[0].clip_w = -1;
    assert(!cc_material_depth_key(&quad, &key));
    quad.vertices[0].clip_w = NAN;
    assert(!cc_material_depth_key(&quad, &key));
    quad.vertices[0].clip_w = 1;
    quad.depth_mode[1] = 8;
    assert(!cc_material_depth_key(&quad, &key));
    assert(!cc_material_depth_key(NULL, &key));
    assert(!cc_material_depth_key(&quad, NULL));
}

int main(void) {
    test_quad_corners();
    test_signed_dimensions_and_uvs();
    test_quad_triangulation();
    test_material_depth_snapshot();
    return 0;
}
