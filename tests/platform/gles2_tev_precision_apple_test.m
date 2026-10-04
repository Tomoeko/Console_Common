#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders.c"

void glGetShaderPrecisionFormat(GLenum type, GLenum precision, GLint range[2],
                                GLint *bits) {
    (void)type;
    (void)precision;
    range[0] = range[1] = 127;
    *bits = 23;
}

/* Desktop GL does not impose mediump precision. Run the actual emitted color
 * operations as Metal half vectors to cover a permitted ES2 precision case. */
static NSString *half_source(const char *source) {
    NSString *result = [NSString stringWithUTF8String:source];
    result = [result stringByReplacingOccurrencesOfString:@"vec3" withString:@"half3"];
    result = [result stringByReplacingOccurrencesOfString:@"vec2" withString:@"half2"];
    result = [result stringByReplacingOccurrencesOfString:@"mod("
                                               withString:@"test_mod("];
    NSRegularExpression *constants =
        [NSRegularExpression regularExpressionWithPattern:@"\\b[0-9]+\\.[0-9]+\\b"
                                                  options:0
                                                    error:NULL];
    return [constants stringByReplacingMatchesInString:result
                                               options:0
                                                 range:NSMakeRange(0, result.length)
                                          withTemplate:@"half($0)"];
}

static NSString *quantization_source(void) {
    CcTevKey key = {.stage_count = 1};
    char *fragment = cc_tev_fragment_source(&key);
    assert(fragment);
    const char *start = strstr(fragment, "vec3 tevColor8(");
    assert(start);
    const char *end = strstr(start, "float tevAlpha8(");
    assert(end);
    char *quantization = calloc((size_t)(end - start) + 1, 1);
    assert(quantization);
    memcpy(quantization, start, (size_t)(end - start));
    NSString *result = [@"half3 test_mod(half3 value, half divisor) {\n"
                         "    return value - divisor * floor(value / divisor);\n"
                         "}\n" stringByAppendingString:half_source(quantization)];
    free(quantization);
    free(fragment);
    return result;
}

static id<MTLComputePipelineState> make_pipeline(id<MTLDevice> device,
                                                 NSString *source) {
    NSError *error = nil;
    MTLCompileOptions *options = [MTLCompileOptions new];
    options.fastMathEnabled = NO;
    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                  options:options
                                                    error:&error];
    if (!library)
        fprintf(stderr, "Half precision shader: %s\n",
                error.localizedDescription.UTF8String);
    assert(library);
    id<MTLFunction> function = [library newFunctionWithName:@"probe"];
    assert(function);
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&error];
    assert(pipeline);
    return pipeline;
}

static id<MTLBuffer> dispatch(id<MTLDevice> device,
                              id<MTLComputePipelineState> pipeline, const void *input,
                              size_t input_size, NSUInteger count) {
    id<MTLBuffer> inputs = [device newBufferWithBytes:input
                                               length:input_size
                                              options:MTLResourceStorageModeShared];
    id<MTLBuffer> outputs = [device newBufferWithLength:count * 4
                                                options:MTLResourceStorageModeShared];
    assert(inputs && outputs);
    memset(outputs.contents, 0xcd, count * 4);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    assert(queue && commands && encoder);
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:inputs offset:0 atIndex:0];
    [encoder setBuffer:outputs offset:0 atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
    [encoder endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    assert(commands.status == MTLCommandBufferStatusCompleted);
    return outputs;
}

static void test_byte_quantization(id<MTLDevice> device) {
    NSString *source =
        [NSString stringWithFormat:
                      @"#include <metal_stdlib>\n"
                       "using namespace metal;\n"
                       "%@\n"
                       "kernel void probe(device const float *input [[buffer(0)]],\n"
                       "                  device uchar4 *output [[buffer(1)]],\n"
                       "                  uint index [[thread_position_in_grid]]) {\n"
                       "    half value = half(input[index]);\n"
                       "    half3 bytes = tevColor8(half3(value));\n"
                       "    output[index] = uchar4(uchar3(bytes), 255);\n"
                       "}\n",
                      quantization_source()];
    /* Cover normalized image bytes and the signed native register range. */
    float input[2048];
    for (unsigned index = 0; index < 2048; ++index)
        input[index] = (float)((int)index - 1024) / 255;
    id<MTLBuffer> output =
        dispatch(device, make_pipeline(device, source), input, sizeof(input), 2048);
    const uint8_t *bytes = output.contents;
    for (unsigned index = 0; index < 2048; ++index) {
        int value = (int)index - 1024;
        uint8_t expected = (uint8_t)value;
        for (unsigned channel = 0; channel < 3; ++channel) {
            if (bytes[index * 4 + channel] != expected)
                fprintf(stderr, "Quantization value=%d expected=%u actual=%u\n", value,
                        expected, bytes[index * 4 + channel]);
            assert(bytes[index * 4 + channel] == expected);
        }
        assert(bytes[index * 4 + 3] == 255);
    }
}

static void test_packed_comparison(id<MTLDevice> device, unsigned operation) {
    const uint8_t input[][8] = {
        {1, 128, 0, 0, 0, 128, 0, 0},     {0, 128, 0, 0, 1, 128, 0, 0},
        {1, 255, 0, 0, 0, 255, 0, 0},     {255, 127, 0, 0, 0, 128, 0, 0},
        {0, 128, 0, 0, 255, 127, 0, 0},   {255, 255, 0, 0, 255, 255, 0, 0},
        {0, 0, 0, 0, 0, 0, 255, 0},       {255, 0, 0, 0, 0, 1, 255, 0},
        {255, 255, 127, 0, 0, 0, 128, 0}, {0, 0, 128, 0, 255, 255, 127, 0},
        {1, 128, 255, 0, 0, 128, 255, 0}, {0, 128, 255, 0, 1, 128, 255, 0},
    };
    uint8_t stage[16] = {0};
    stage[6] = (uint8_t)operation;
    stage[7] = 1;
    CcShaderText emitted = {0};
    cc_emit_color_operation(&emitted, 0, stage);
    assert(emitted.data && !emitted.failed);
    NSString *source =
        [NSString stringWithFormat:
                      @"#include <metal_stdlib>\n"
                       "using namespace metal;\n"
                       "struct Colors { uchar4 a; uchar4 b; };\n"
                       "%@\n"
                       "kernel void probe(device const Colors *input [[buffer(0)]],\n"
                       "                  device uchar4 *output [[buffer(1)]],\n"
                       "                  uint index [[thread_position_in_grid]]) {\n"
                       "    half3 ca0 = half3(input[index].a.xyz) / half(255.0);\n"
                       "    half3 cb0 = half3(input[index].b.xyz) / half(255.0);\n"
                       "    half3 cc0 = half3(1.0);\n"
                       "    half3 cd0 = half3(0.0);\n"
                       "%@\n"
                       "    output[index] = uchar4(uchar3(c0 * half(255.0)), 255);\n"
                       "}\n",
                      quantization_source(), half_source(emitted.data)];
    free(emitted.data);
    NSUInteger count = sizeof(input) / sizeof(input[0]);
    id<MTLBuffer> output =
        dispatch(device, make_pipeline(device, source), input, sizeof(input), count);
    const uint8_t *bytes = output.contents;
    for (NSUInteger index = 0; index < count; ++index) {
        unsigned left = (unsigned)input[index][0] + 256u * input[index][1];
        unsigned right = (unsigned)input[index][4] + 256u * input[index][5];
        if (operation >= 12) {
            left += 65536u * input[index][2];
            right += 65536u * input[index][6];
        }
        uint8_t expected = (operation & 1 ? left == right : left > right) ? 255 : 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            if (bytes[index * 4 + channel] != expected)
                fprintf(
                    stderr,
                    "Packed comparison operation=%u case=%lu: expected=%u actual=%u\n",
                    operation, (unsigned long)index, expected,
                    bytes[index * 4 + channel]);
            assert(bytes[index * 4 + channel] == expected);
        }
        assert(bytes[index * 4 + 3] == 255);
    }
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return 77;
        printf("TEV precision GPU: %s\n", device.name.UTF8String);
        test_byte_quantization(device);
        for (unsigned operation = 10; operation <= 13; ++operation)
            test_packed_comparison(device, operation);
    }
    return 0;
}
