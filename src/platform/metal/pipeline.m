#include "platform_metal_internal.h"

#include <stdio.h>

static id<MTLRenderPipelineState> cc_make_pipeline(CcMetalState *state,
                                                   id<MTLFunction> fragment,
                                                   uint8_t blend_key,
                                                   NSUInteger samples) {
    static const MTLBlendFactor factors[8] = {
        MTLBlendFactorZero,
        MTLBlendFactorOne,
        MTLBlendFactorDestinationColor,
        MTLBlendFactorOneMinusDestinationColor,
        MTLBlendFactorSourceAlpha,
        MTLBlendFactorOneMinusSourceAlpha,
        MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha,
    };

    /* GX codes 2 and 3 refer to the other operand's color. */
    static const MTLBlendFactor destination_factors[8] = {
        MTLBlendFactorZero,
        MTLBlendFactorOne,
        MTLBlendFactorSourceColor,
        MTLBlendFactorOneMinusSourceColor,
        MTLBlendFactorSourceAlpha,
        MTLBlendFactorOneMinusSourceAlpha,
        MTLBlendFactorDestinationAlpha,
        MTLBlendFactorOneMinusDestinationAlpha,
    };

    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.rasterSampleCount = samples;
    description.vertexFunction = state->vertex_function;
    description.fragmentFunction = fragment;
    description.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    MTLRenderPipelineColorAttachmentDescriptor *attachment =
        description.colorAttachments[0];
    attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
    attachment.blendingEnabled = blend_key != CC_BLEND_DISABLED;
    if (blend_key != CC_BLEND_DISABLED) {
        attachment.rgbBlendOperation = MTLBlendOperationAdd;
        attachment.alphaBlendOperation = MTLBlendOperationAdd;
        attachment.sourceRGBBlendFactor = factors[blend_key / 8];
        attachment.destinationRGBBlendFactor = destination_factors[blend_key % 8];
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline =
        [state->device newRenderPipelineStateWithDescriptor:description error:&error];
    if (!pipeline) {
        const char *message = error.localizedDescription.UTF8String;
        fprintf(stderr, "Metal pipeline unavailable: %s\n",
                message ? message : "unknown error");
    }
    return pipeline;
}

id<MTLRenderPipelineState>
cc_metal_material_pipeline(CcMetalState *state, CcBatchKind kind, uint8_t blend_key) {
    if (!state || (kind != CC_BATCH_MATERIAL && kind != CC_BATCH_TEV) ||
        blend_key >= CC_MATERIAL_PIPELINE_VARIANTS) {
        return nil;
    }
    unsigned material_kind = (unsigned)kind - 1;
    bool *attempted = state->rendering_multisample
                          ? state->antialias_pipeline_attempted[material_kind]
                          : state->material_pipeline_attempted[material_kind];
    __strong id<MTLRenderPipelineState> *pipelines =
        state->rendering_multisample
            ? state->antialias_material_pipelines[material_kind]
            : state->material_pipelines[material_kind];
    if (!attempted[blend_key]) {
        /* Retain failures too, so an unavailable pipeline cannot repeatedly
         * compile and report errors in the frame loop. */
        attempted[blend_key] = true;
        id<MTLFunction> fragment = kind == CC_BATCH_TEV
                                       ? state->tev_fragment_function
                                       : state->material_fragment_function;
        pipelines[blend_key] = cc_make_pipeline(
            state, fragment, blend_key,
            state->rendering_multisample ? state->antialias_samples : 1);
    }
    return pipelines[blend_key];
}

static bool cc_prepare_depth_states(CcMetalState *state) {
    static const MTLCompareFunction comparisons[8] = {
        MTLCompareFunctionNever,        MTLCompareFunctionLess,
        MTLCompareFunctionEqual,        MTLCompareFunctionLessEqual,
        MTLCompareFunctionGreater,      MTLCompareFunctionNotEqual,
        MTLCompareFunctionGreaterEqual, MTLCompareFunctionAlways,
    };
    for (unsigned key = 0; key < 17; ++key) {
        MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];
        descriptor.depthCompareFunction =
            key ? comparisons[(key - 1) / 2] : MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = key && ((key - 1) & 1);
        state->depth_states[key] =
            [state->device newDepthStencilStateWithDescriptor:descriptor];
        if (!state->depth_states[key]) {
            return false;
        }
    }
    return true;
}

bool cc_metal_prepare_pipelines(CcMetalState *state, id<MTLFunction> basic_fragment) {
    state->basic_fragment_function = basic_fragment;
    state->pipeline = cc_make_pipeline(state, basic_fragment, CC_BLEND_DEFAULT, 1);
    state->presentation_pipeline =
        cc_make_pipeline(state, basic_fragment, CC_BLEND_DISABLED, 1);
    if (!state->pipeline || !state->presentation_pipeline ||
        !cc_prepare_depth_states(state)) {
        return false;
    }
    return cc_metal_material_pipeline(state, CC_BATCH_MATERIAL, CC_BLEND_DEFAULT) &&
           cc_metal_material_pipeline(state, CC_BATCH_MATERIAL, CC_BLEND_DISABLED) &&
           cc_metal_material_pipeline(state, CC_BATCH_TEV, CC_BLEND_DEFAULT) &&
           cc_metal_material_pipeline(state, CC_BATCH_TEV, CC_BLEND_DISABLED);
}

bool cc_metal_prepare_antialias_pipelines(CcMetalState *state) {
    if (!state->antialias_pipeline) {
        state->antialias_pipeline =
            cc_make_pipeline(state, state->basic_fragment_function, CC_BLEND_DEFAULT,
                             state->antialias_samples);
    }
    if (!state->antialias_pipeline)
        return false;
    bool previous = state->rendering_multisample;
    state->rendering_multisample = true;
    bool okay = true;
    for (unsigned kind = 0; kind < 2; ++kind) {
        for (unsigned blend = 0; blend < CC_MATERIAL_PIPELINE_VARIANTS; ++blend) {
            if (state->material_pipelines[kind][blend] &&
                !cc_metal_material_pipeline(state, (CcBatchKind)(kind + 1),
                                            (uint8_t)blend))
                okay = false;
        }
    }
    state->rendering_multisample = previous;
    return okay;
}
