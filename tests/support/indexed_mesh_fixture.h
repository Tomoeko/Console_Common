#ifndef CC_INDEXED_MESH_FIXTURE_H
#define CC_INDEXED_MESH_FIXTURE_H

#include "console_common/platform/indexed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            exit(EXIT_FAILURE);                                                        \
        }                                                                              \
    } while (0)

static const char vertex_glsl[] = "attribute vec4 position;\n"
                                  "attribute vec4 coordinates;\n"
                                  "attribute vec4 normal;\n"
                                  "varying vec4 color;\n"
                                  "void main() {\n"
                                  "    gl_Position = position;\n"
                                  "    color = vec4(normal.xyz, coordinates.w);\n"
                                  "}\n";

static const char fragment_glsl[] = "#ifdef GL_ES\n"
                                    "precision mediump float;\n"
                                    "#endif\n"
                                    "varying vec4 color;\n"
                                    "void main() {\n"
                                    "    gl_FragColor = color;\n"
                                    "}\n";

static const char vertex_metal[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[attribute(0)]];\n"
    "    float4 coordinates [[attribute(1)]];\n"
    "    float4 normal [[attribute(2)]];\n"
    "};\n"
    "struct Output {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "vertex Output vertex_main(Input input [[stage_in]]) {\n"
    "    Output output;\n"
    "    output.position = input.position;\n"
    "    output.color = float4(input.normal.xyz, input.coordinates.w);\n"
    "    return output;\n"
    "}\n";

static const char fragment_metal[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Input {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "fragment float4 fragment_main(Input input [[stage_in]]) {\n"
    "    return input.color;\n"
    "}\n";

typedef struct MeshFixture {
    CcIndexedRenderer *renderer;
    CcIndexedProgram *program;
    CcIndexedMesh *mesh;
    CcIndexedMesh *second;
    CcIndexedMesh *immutable;
    CcIndexedDraw draw;
    float vertices[4][12];
} MeshFixture;

static void mesh_vertices(float vertices[4][12], bool right, const uint8_t color[4]) {
    const float position[4][2] = {{-1, -1}, {0, -1}, {0, 1}, {-1, 1}};
    memset(vertices, 0, sizeof(float) * 4 * 12);
    for (size_t index = 0; index < 4; ++index) {
        vertices[index][0] = position[index][0] + (right ? 1.0f : 0.0f);
        vertices[index][1] = position[index][1];
        vertices[index][3] = 1;
        vertices[index][7] = (float)color[3] / 255.0f;
        for (size_t lane = 0; lane < 3; ++lane)
            vertices[index][8 + lane] = (float)color[lane] / 255.0f;
    }
}

static void mesh_fixture_open(MeshFixture *fixture, CcIndexedRenderer *renderer) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->renderer = renderer;
    CcIndexedProgramDescription program = {0};
    program.gles_vertex_source = vertex_glsl;
    program.gles_fragment_source = fragment_glsl;
    program.metal_vertex_source = vertex_metal;
    program.metal_fragment_source = fragment_metal;
    program.metal_vertex_entry = "vertex_main";
    program.metal_fragment_entry = "fragment_main";
    program.attributes[0] = (CcIndexedAttribute){"position", 0};
    program.attributes[1] = (CcIndexedAttribute){"coordinates", 16};
    program.attributes[2] = (CcIndexedAttribute){"normal", 32};
    program.attribute_count = 3;
    program.vertex_stride = sizeof(fixture->vertices[0]);
    program.metal_vertex_uniform_buffer = 1;
    for (size_t lane = 0; lane < 4; ++lane)
        program.state.color_write[lane] = true;
    fixture->program = cc_indexed_program_create(renderer, &program, NULL, 0);
    CHECK(fixture->program);
    const uint8_t white[4] = {255, 255, 255, 255};
    mesh_vertices(fixture->vertices, false, white);
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    CcIndexedMeshDescription mesh = {fixture->vertices, 4, sizeof(fixture->vertices[0]),
                                     indices, 6};
    fixture->mesh = cc_indexed_mesh_create_dynamic(renderer, &mesh, NULL, 0);
    fixture->second = cc_indexed_mesh_create_dynamic(renderer, &mesh, NULL, 0);
    fixture->immutable = cc_indexed_mesh_create(renderer, &mesh, NULL, 0);
    CHECK(fixture->mesh && fixture->second && fixture->immutable);
    fixture->draw = (CcIndexedDraw){
        .program = fixture->program, .mesh = fixture->mesh, .index_count = 6};
    CHECK(cc_indexed_reserve(renderer, 2, NULL, 0));
    CHECK(!cc_indexed_mesh_update(renderer, fixture->immutable, fixture->vertices,
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(!cc_indexed_mesh_update(renderer, (CcIndexedMesh *)(uintptr_t)8,
                                  fixture->vertices, sizeof(fixture->vertices), NULL,
                                  0));
    CHECK(!cc_indexed_mesh_update(renderer, fixture->mesh, NULL,
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(!cc_indexed_mesh_update(renderer, fixture->mesh, fixture->vertices,
                                  sizeof(fixture->vertices) - 1, NULL, 0));
    CHECK(!cc_indexed_mesh_update(renderer, fixture->mesh,
                                  (const void *)(UINTPTR_MAX - 31),
                                  sizeof(fixture->vertices), NULL, 0));
    CHECK(!cc_indexed_mesh_create_dynamic(
        renderer, (const void *)((uintptr_t)&mesh + 1), NULL, 0));
    CHECK(!cc_indexed_mesh_create_dynamic(
        renderer, (const void *)(UINTPTR_MAX - sizeof(mesh) + 1), NULL, 0));
    mesh.vertices = (const void *)(UINTPTR_MAX - 31);
    CHECK(!cc_indexed_mesh_create_dynamic(renderer, &mesh, NULL, 0));
    mesh.vertices = fixture->vertices;
    mesh.indices = (const void *)(UINTPTR_MAX - 3);
    CHECK(!cc_indexed_mesh_create_dynamic(renderer, &mesh, NULL, 0));
    mesh.indices = (const void *)((uintptr_t)indices + 1);
    CHECK(!cc_indexed_mesh_create_dynamic(renderer, &mesh, NULL, 0));
}

static void mesh_frame(MeshFixture *fixture, bool inside, bool right,
                       const uint8_t color[4]) {
    mesh_vertices(fixture->vertices, right, color);
    if (!inside)
        CHECK(cc_indexed_mesh_update(fixture->renderer, fixture->mesh,
                                     fixture->vertices, sizeof(fixture->vertices), NULL,
                                     0));
    CcIndexedFrame frame = {.clear_color = {0, 0, 0, 1}, .clear_color_enabled = true};
    CHECK(cc_indexed_begin(fixture->renderer, &frame, NULL, 0));
    fixture->draw.index_count = 5;
    CHECK(!cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    fixture->draw.index_count = 6;
    if (inside)
        CHECK(cc_indexed_mesh_update(fixture->renderer, fixture->mesh,
                                     fixture->vertices, sizeof(fixture->vertices), NULL,
                                     0));
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    float rejected[4][12];
    const uint8_t red[4] = {255, 0, 0, 255};
    mesh_vertices(rejected, !right, red);
    CHECK(!cc_indexed_mesh_update(fixture->renderer, fixture->mesh, rejected,
                                  sizeof(rejected), NULL, 0));
    /* Use is per mesh, not a global frame prohibition. An accepted empty draw
     * still pins the second mesh for a deferred backend. */
    CHECK(cc_indexed_mesh_update(fixture->renderer, fixture->second, rejected,
                                 sizeof(rejected), NULL, 0));
    fixture->draw.mesh = fixture->second;
    fixture->draw.index_count = 0;
    CHECK(cc_indexed_draw(fixture->renderer, &fixture->draw, NULL, 0));
    CHECK(!cc_indexed_mesh_update(fixture->renderer, fixture->second, fixture->vertices,
                                  sizeof(fixture->vertices), NULL, 0));
    fixture->draw.mesh = fixture->mesh;
    fixture->draw.index_count = 6;
    CHECK(cc_indexed_end(fixture->renderer, NULL, 0));
}

static void mesh_pixels(const uint8_t pixel[4], const uint8_t expected[4]) {
    for (size_t lane = 0; lane < 4; ++lane)
        CHECK(abs((int)pixel[lane] - (int)expected[lane]) <= 2);
}

static void mesh_fixture_close(MeshFixture *fixture) {
    CHECK(cc_indexed_mesh_release(fixture->renderer, &fixture->mesh, NULL, 0));
    CHECK(cc_indexed_mesh_release(fixture->renderer, &fixture->second, NULL, 0));
    CHECK(cc_indexed_mesh_release(fixture->renderer, &fixture->immutable, NULL, 0));
    CHECK(cc_indexed_program_release(fixture->renderer, &fixture->program, NULL, 0));
}

#endif
