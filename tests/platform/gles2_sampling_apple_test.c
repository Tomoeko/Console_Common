#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* macOS supplies desktop GL. Translate precision declarations only; the tested
 * fragment operations are the production GLSL ES sampling and TEV source. */
static void test_shader_source(GLuint shader, GLsizei count, const GLchar *const *parts,
                               const GLint *lengths) {
    size_t size = 1;
    for (GLsizei index = 0; index < count; ++index)
        size += lengths && lengths[index] >= 0 ? (size_t)lengths[index]
                                               : strlen(parts[index]);
    char *source = calloc(size, 1);
    assert(source);
    size_t used = 0;
    for (GLsizei index = 0; index < count; ++index) {
        size_t length = lengths && lengths[index] >= 0 ? (size_t)lengths[index]
                                                       : strlen(parts[index]);
        memcpy(source + used, parts[index], length);
        used += length;
    }
    const char *declaration = "precision CC_UV_PRECISION float;\n";
    char *precision = strstr(source, declaration);
    if (precision) {
        size_t length = strlen(declaration);
        memmove(precision, precision + length, strlen(precision + length) + 1);
    }
    const char *translated[] = {
        "#version 120\n#define lowp\n#define mediump\n#define highp\n", source};
    glShaderSource(shader, 2, translated, NULL);
    free(source);
}

#define glShaderSource test_shader_source
#include "shaders.c"
#undef glShaderSource

void glGetShaderPrecisionFormat(GLenum type, GLenum precision, GLint range[2],
                                GLint *bits) {
    (void)type;
    (void)precision;
    range[0] = range[1] = 127;
    *bits = 23;
}

static GLuint make_program(const char *fragment_source) {
    const char *vertex_source = "uniform vec2 point;\n"
                                "varying vec2 v_uv0;\n"
                                "varying vec2 v_uv1;\n"
                                "varying vec4 v_color;\n"
                                "varying vec2 texUV0;\n"
                                "varying vec4 raster;\n"
                                "void main() {\n"
                                "    gl_Position = gl_Vertex;\n"
                                "    v_uv0 = v_uv1 = texUV0 = point;\n"
                                "    v_color = raster = vec4(1.0);\n"
                                "}\n";
    GLuint vertex = cc_compile_shader(GL_VERTEX_SHADER, vertex_source, "");
    GLuint fragment = cc_compile_shader(GL_FRAGMENT_SHADER, fragment_source,
                                        "#define CC_UV_PRECISION highp\n");
    assert(vertex && fragment);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);
    return program;
}

static void draw_pixel(GLuint program, float u, float v, uint8_t rgba[4]) {
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "point"), u, v);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1);
    glVertex2f(1, -1);
    glVertex2f(1, 1);
    glVertex2f(-1, 1);
    glEnd();
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static GLuint production_program(unsigned stages, unsigned wrap_s, unsigned wrap_t) {
    if (!stages)
        return make_program(cc_material_fragment_source);
    CcTevKey key = {.stage_count = 1};
    memset(key.swap, 0xe4, sizeof(key.swap));
    key.wrap_s[0] = (uint8_t)wrap_s;
    key.wrap_t[0] = (uint8_t)wrap_t;
    key.stages[0][4] = 0x8f;
    key.stages[0][5] = 0xfa;
    key.stages[0][7] = 1;
    key.stages[0][8] = 0x47;
    key.stages[0][9] = 0x75;
    key.stages[0][11] = 1;
    char *source = cc_tev_fragment_source(&key);
    assert(source);
    GLuint program = make_program(source);
    free(source);
    return program;
}

static void set_material(GLuint program, unsigned stages, unsigned width,
                         unsigned height, unsigned wrap_s, unsigned wrap_t,
                         float nearest) {
    glUseProgram(program);
    const float registers[12] = {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0};
    if (stages) {
        glUniform4fv(glGetUniformLocation(program, "regs"), 3, registers);
        glUniform4f(glGetUniformLocation(program, "sampling0"), (float)width,
                    (float)height, nearest, 0);
    } else {
        glUniform4fv(glGetUniformLocation(program, "u_registers"), 3, registers);
        glUniform1i(glGetUniformLocation(program, "u_texture_count"), 1);
        glUniform4f(glGetUniformLocation(program, "u_alpha_compare"), -1, 0, 0, 0);
        glUniform4f(glGetUniformLocation(program, "u_sampling0"), (float)width,
                    (float)height, nearest, 0);
        glUniform2f(glGetUniformLocation(program, "u_wrap0"), (float)wrap_s,
                    (float)wrap_t);
    }
}

static void compare_sampling(GLuint reference, unsigned width, unsigned height,
                             unsigned stages, unsigned wrap_s, unsigned wrap_t) {
    static const GLenum wraps[] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};
    static const float coordinates[] = {-1.13f, -0.08f, 0, 0.03f, 0.24f,
                                        0.63f,  0.97f,  1, 1.09f};
    GLuint program = production_program(stages, wrap_s, wrap_t);
    for (unsigned nearest = 0; nearest < 2; ++nearest) {
        GLenum filter = nearest ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
        /* The negative selector denotes the GLES mediump nearest-mirror path.
         * Its texture already uses NEAREST, so it must skip linear seam taps. */
        for (unsigned mirror = 0; mirror <= nearest; ++mirror) {
            float selector = nearest ? (mirror ? -1.0f : 1.0f) : 0.0f;
            set_material(program, stages, width, height, wrap_s, wrap_t, selector);
            for (unsigned row = 0; row < sizeof(coordinates) / sizeof(coordinates[0]);
                 ++row) {
                for (unsigned column = 0;
                     column < sizeof(coordinates) / sizeof(coordinates[0]); ++column) {
                    uint8_t expected[4];
                    uint8_t actual[4];
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                    (GLint)wraps[wrap_s]);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                    (GLint)wraps[wrap_t]);
                    draw_pixel(reference, coordinates[column], coordinates[row],
                               expected);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    draw_pixel(program, coordinates[column], coordinates[row], actual);
                    for (unsigned component = 0; component < 4; ++component) {
                        if (abs((int)actual[component] - (int)expected[component]) >
                            1) {
                            fprintf(stderr,
                                    "%ux%u stages=%u wrap=%u,%u nearest=%g uv=%g,%g "
                                    "channel=%u: expected=%u actual=%u\n",
                                    width, height, stages, wrap_s, wrap_t,
                                    (double)selector, (double)coordinates[column],
                                    (double)coordinates[row], component,
                                    expected[component], actual[component]);
                            abort();
                        }
                    }
                }
            }
        }
    }
    glDeleteProgram(program);
}

int main(void) {
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFAColorSize, 24,
                                            (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj format = NULL;
    GLint count = 0;
    if (CGLChoosePixelFormat(attributes, &format, &count) != kCGLNoError || !format)
        return 77;
    CGLContextObj context = NULL;
    CGLError result = CGLCreateContext(format, NULL, &context);
    CGLDestroyPixelFormat(format);
    if (result != kCGLNoError || !context)
        return 77;
    assert(CGLSetCurrentContext(context) == kCGLNoError);
    printf("Sampling GPU: %s\n", glGetString(GL_RENDERER));
    GLuint output;
    GLuint framebuffer;
    glGenTextures(1, &output);
    glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output,
                           0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 1, 1);
    GLuint reference =
        make_program("uniform sampler2D image;\n"
                     "varying vec2 v_uv0;\n"
                     "void main() { gl_FragColor = texture2D(image, v_uv0); }\n");
    static const unsigned dimensions[][2] = {{2, 1}, {3, 5}};
    for (unsigned image = 0; image < sizeof(dimensions) / sizeof(dimensions[0]);
         ++image) {
        unsigned width = dimensions[image][0];
        unsigned height = dimensions[image][1];
        uint8_t pixels[3 * 5 * 4];
        for (unsigned pixel = 0; pixel < width * height; ++pixel) {
            pixels[pixel * 4] = (uint8_t)(pixel * 43);
            pixels[pixel * 4 + 1] = (uint8_t)(pixel * 71);
            pixels[pixel * 4 + 2] = (uint8_t)(pixel * 113);
            pixels[pixel * 4 + 3] = (uint8_t)(255 - pixel * 7);
        }
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (unsigned stages = 0; stages < 2; ++stages) {
            for (unsigned wrap_s = 0; wrap_s < 3; ++wrap_s) {
                for (unsigned wrap_t = 0; wrap_t < 3; ++wrap_t)
                    compare_sampling(reference, width, height, stages, wrap_s, wrap_t);
            }
        }
        glDeleteTextures(1, &texture);
    }
    glDeleteProgram(reference);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &output);
    assert(glGetError() == GL_NO_ERROR);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(context);
    return 0;
}
