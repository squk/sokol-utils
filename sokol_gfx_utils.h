#if defined(SOKOL_IMPL) && !defined(SOKOL_GFX_UTILS_IMPL)
#define SOKOL_GFX_UTILS_IMPL
#endif
#ifndef SOKOL_GFX_UTILS_INCLUDED
#define SOKOL_GFX_UTILS_INCLUDED

/*
    sokol_gfx_utils.h -- readback support for sokol_gfx.h

    Project URL: https://github.com/floooh/sokol

    Include this header *after* the sokol_gfx.h implementation to access
    internal state.

    Do this:
        #define SOKOL_IMPL or
        #define SOKOL_GFX_UTILS_IMPL
    before you include this file in *one* C or C++ file to create the
    implementation.
*/

#if !defined(SOKOL_GFX_INCLUDED)
#error "Please include sokol_gfx.h before sokol_gfx_utils.h"
#endif

#if defined(SOKOL_API_DECL) && !defined(SOKOL_GFX_UTILS_API_DECL)
#define SOKOL_GFX_UTILS_API_DECL SOKOL_API_DECL
#endif
#ifndef SOKOL_GFX_UTILS_API_DECL
#define SOKOL_GFX_UTILS_API_DECL extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
    sg_readback_request
    Create a request to read back the pixels of an image.
    The image must be a regular image (not a swapchain image).
    Returns a handle to the request, or NULL if it failed.
*/
SOKOL_GFX_UTILS_API_DECL void* sg_readback_request(sg_image img);

/*
    sg_readback_is_ready
    Returns true if the readback request has completed and data is ready to be copied.
*/
SOKOL_GFX_UTILS_API_DECL bool sg_readback_is_ready(void* request);

/*
    sg_readback_wait
    Synchronously wait until the readback request has completed.
*/
SOKOL_GFX_UTILS_API_DECL void sg_readback_wait(void* request);

/*
    sg_readback_copy_data
    Copies the readback data into the provided buffer.
    The buffer must be large enough to hold width * height * 4 bytes (for RGBA8).
    This function should only be called if sg_readback_is_ready returns true.
*/
SOKOL_GFX_UTILS_API_DECL void sg_readback_copy_data(void* request, void* out_pixels);

/*
    sg_readback_destroy
    Frees the resources associated with a readback request.
*/
SOKOL_GFX_UTILS_API_DECL void sg_readback_destroy(void* request);

/*
    Asynchronous GPU durations for labeled sg_pass bodies, plus a "Frame"
    result for the entire command buffer. Metal stage-boundary counters are
    optional; unsupported devices report only "Frame". Render timings measure
    the union of vertex/fragment stage intervals, excluding the scheduling gap
    between stages. Different passes may overlap: their sum is not frame time.
    Results are reported only after the command buffer has completed.
*/
#define SG_GPU_PASS_TIMING_MAX_PASSES (256)
#define SG_GPU_PASS_TIMING_LABEL_SIZE (96)

typedef struct sg_gpu_pass_timing {
    char label[SG_GPU_PASS_TIMING_LABEL_SIZE];
    uint64_t frame_index;
    double milliseconds;
} sg_gpu_pass_timing;

/* Install pass tracing before other sg_trace_hooks users are initialized. */
SOKOL_GFX_UTILS_API_DECL bool sg_gpu_pass_timings_setup(void);

/* Release timing resources after sg_shutdown has drained in-flight work. */
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_shutdown(void);

/* Enable or disable recording. Unsupported backends remain disabled. */
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_set_enabled(bool enabled);

/*
    Metal integration: call once per pass, immediately before creating its
    command encoder, while Sokol's current command buffer is active. descriptor
    is a borrowed MTLRenderPassDescriptor* or MTLComputePassDescriptor*.
    These attach timestamp counters without relying on patched Sokol fields.
    Disabled recording and non-Metal backends leave the descriptor unchanged.
*/
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_mtl_prepare_render_pass(const sg_pass* pass, const void* descriptor);
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_mtl_prepare_compute_pass(const sg_pass* pass, const void* descriptor);

/* Returns true when the active backend supports asynchronous GPU timestamps. */
SOKOL_GFX_UTILS_API_DECL bool sg_gpu_pass_timings_supported(void);

/*
    Copies and consumes the oldest completed GPU frame. This function never
    waits for the GPU. Returns the number of copied timings, starting with
    "Frame" when available. Allow MAX_PASSES + 1 entries to receive all results.
*/
SOKOL_GFX_UTILS_API_DECL int sg_gpu_pass_timings_poll(sg_gpu_pass_timing* timings, int max_timings);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOKOL_GFX_UTILS_INCLUDED */

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef SOKOL_GFX_UTILS_IMPL
#define SOKOL_GFX_UTILS_IMPL_INCLUDED (1)

#include <stdlib.h> // malloc, free
#include <string.h> // memcpy

#ifndef GFX_READBACK_TIMEOUT_MS
#define GFX_READBACK_TIMEOUT_MS (2000)
#endif

#ifndef _SOKOL_PRIVATE
    #if defined(__GNUC__) || defined(__clang__)
        #define _SOKOL_PRIVATE __attribute__((unused)) static
    #else
        #define _SOKOL_PRIVATE static
    #endif
#endif

typedef struct {
    bool setup;
    bool enabled;
    bool supported;
    sg_trace_hooks previous_hooks;
} _sgu_gpu_pass_timing_state_t;

static _sgu_gpu_pass_timing_state_t _sgu_gpu_pass_timing_state;

/*--- GPU Pass Timings: Metal -----------------------------------------------*/
#if defined(SOKOL_METAL)

#if !defined(__OBJC__)
#error "Metal implementation requires Objective-C"
#endif
#import <Metal/Metal.h>

#define _SGU_GPU_TIMING_PENDING_FRAMES (4)

typedef struct {
    char label[SG_GPU_PASS_TIMING_LABEL_SIZE];
    uint32_t start_sample;
    bool render;
    bool attached;
} _sgu_mtl_gpu_pass_t;

typedef struct {
    id<MTLCounterSampleBuffer> sample_buffer;
    id<MTLCommandBuffer> command_buffer;
    uint64_t frame_index;
    uint32_t pass_count;
    int active_pass;
    MTLTimestamp cpu_begin;
    MTLTimestamp gpu_begin;
    _sgu_mtl_gpu_pass_t passes[SG_GPU_PASS_TIMING_MAX_PASSES];
} _sgu_mtl_gpu_timing_frame_t;

typedef struct {
    id<MTLCounterSet> timestamp_counter_set;
    _sgu_mtl_gpu_timing_frame_t current;
    _sgu_mtl_gpu_timing_frame_t pending[_SGU_GPU_TIMING_PENDING_FRAMES];
} _sgu_mtl_gpu_timing_state_t;

static _sgu_mtl_gpu_timing_state_t _sgu_mtl_gpu_timing_state;

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_release_frame(_sgu_mtl_gpu_timing_frame_t* frame) {
    if (frame->sample_buffer) {
#if !__has_feature(objc_arc)
        [frame->sample_buffer release];
#endif
    }
    if (frame->command_buffer) {
#if !__has_feature(objc_arc)
        [frame->command_buffer release];
#endif
    }
    memset(frame, 0, sizeof(*frame));
    frame->active_pass = -1;
}

_SOKOL_PRIVATE bool _sgu_mtl_gpu_timing_initialize(void) {
    if (@available(macOS 10.15, iOS 14.0, *)) {
        id<MTLDevice> device = _sg.mtl.device;
        if (!device) {
            return false;
        }
        bool stage_sampling = false;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            stage_sampling = [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
        }
        if (stage_sampling) {
            for (id<MTLCounterSet> counter_set in device.counterSets) {
                if ([counter_set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
                    _sgu_mtl_gpu_timing_state.timestamp_counter_set = counter_set;
#if !__has_feature(objc_arc)
                    [counter_set retain];
#endif
                    break;
                }
            }
        }
        _sgu_mtl_gpu_timing_state.current.active_pass = -1;
        for (int index = 0; index < _SGU_GPU_TIMING_PENDING_FRAMES; ++index) {
            _sgu_mtl_gpu_timing_state.pending[index].active_pass = -1;
        }
        /* Command-buffer duration also works without stage-boundary counters. */
        return true;
    }
    return false;
}

_SOKOL_PRIVATE id<MTLCounterSampleBuffer> _sgu_mtl_gpu_timing_make_sample_buffer(void) {
    if (@available(macOS 10.15, iOS 14.0, *)) {
        if (!_sgu_mtl_gpu_timing_state.timestamp_counter_set) {
            return nil;
        }
        MTLCounterSampleBufferDescriptor* descriptor = [[MTLCounterSampleBufferDescriptor alloc] init];
        descriptor.counterSet = _sgu_mtl_gpu_timing_state.timestamp_counter_set;
        descriptor.sampleCount = SG_GPU_PASS_TIMING_MAX_PASSES * 4;
        descriptor.storageMode = MTLStorageModeShared;
        descriptor.label = @"Gobot GPU pass timestamps";
        NSError* error = nil;
        id<MTLCounterSampleBuffer> sample_buffer =
            [_sg.mtl.device newCounterSampleBufferWithDescriptor:descriptor error:&error];
#if !__has_feature(objc_arc)
        [descriptor release];
#endif
        if (error != nil) {
#if !__has_feature(objc_arc)
            [sample_buffer release];
#endif
            return nil;
        }
        return sample_buffer;
    }
    return nil;
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_begin_pass(const sg_pass* pass) {
    _sgu_mtl_gpu_timing_frame_t* frame = &_sgu_mtl_gpu_timing_state.current;
    frame->active_pass = -1;
    if (!_sgu_gpu_pass_timing_state.enabled) {
        return;
    }
    if (frame->pass_count >= SG_GPU_PASS_TIMING_MAX_PASSES) {
        return;
    }
    if (!_sgu_mtl_gpu_timing_state.timestamp_counter_set) {
        return;
    }
    if (!frame->sample_buffer) {
        frame->sample_buffer = _sgu_mtl_gpu_timing_make_sample_buffer();
        if (!frame->sample_buffer) {
            return;
        }
        frame->frame_index = _sg.frame_index;
    }
    const uint32_t pass_index = frame->pass_count++;
    _sgu_mtl_gpu_pass_t* timing = &frame->passes[pass_index];
    const char* label = pass && pass->label ? pass->label : (pass && pass->compute ? "compute" : "render");
    strncpy(timing->label, label, SG_GPU_PASS_TIMING_LABEL_SIZE - 1);
    timing->label[SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
    timing->start_sample = pass_index * 4;
    timing->render = !pass->compute;
    frame->active_pass = (int)pass_index;
}

_SOKOL_PRIVATE _sgu_mtl_gpu_pass_t* _sgu_mtl_gpu_timing_attach(void) {
    _sgu_mtl_gpu_timing_frame_t* frame = &_sgu_mtl_gpu_timing_state.current;
    if (frame->active_pass < 0 || !frame->sample_buffer) {
        return NULL;
    }
    if (!frame->command_buffer) {
        frame->command_buffer = _sg.mtl.cmd_buffer;
#if !__has_feature(objc_arc)
        [frame->command_buffer retain];
#endif
    }
    if (frame->cpu_begin == 0) {
        [_sg.mtl.device sampleTimestamps:&frame->cpu_begin gpuTimestamp:&frame->gpu_begin];
        // Sokol may use unretained references. The copied completion block
        // retains the counter buffer even if overflow/shutdown drops the frame.
        id<MTLCounterSampleBuffer> samples = frame->sample_buffer;
        [frame->command_buffer addCompletedHandler:^(id<MTLCommandBuffer> command_buffer) {
            (void)command_buffer;
            (void)samples;
        }];
    }
    _sgu_mtl_gpu_pass_t* timing = &frame->passes[frame->active_pass];
    timing->attached = true;
    return timing;
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_render_descriptor(const sg_pass* pass, MTLRenderPassDescriptor* descriptor) {
    if (@available(macOS 11.0, iOS 14.0, *)) {
        _sgu_mtl_gpu_timing_begin_pass(pass);
        const _sgu_mtl_gpu_pass_t* timing = _sgu_mtl_gpu_timing_attach();
        if (!timing) return;
        MTLRenderPassSampleBufferAttachmentDescriptor* attachment = descriptor.sampleBufferAttachments[0];
        attachment.sampleBuffer = _sgu_mtl_gpu_timing_state.current.sample_buffer;
        attachment.startOfVertexSampleIndex = timing->start_sample;
        attachment.endOfVertexSampleIndex = timing->start_sample + 1;
        attachment.startOfFragmentSampleIndex = timing->start_sample + 2;
        attachment.endOfFragmentSampleIndex = timing->start_sample + 3;
    }
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_compute_descriptor(const sg_pass* pass, MTLComputePassDescriptor* descriptor) {
    if (@available(macOS 11.0, iOS 14.0, *)) {
        _sgu_mtl_gpu_timing_begin_pass(pass);
        const _sgu_mtl_gpu_pass_t* timing = _sgu_mtl_gpu_timing_attach();
        if (!timing) return;
        MTLComputePassSampleBufferAttachmentDescriptor* attachment = descriptor.sampleBufferAttachments[0];
        attachment.sampleBuffer = _sgu_mtl_gpu_timing_state.current.sample_buffer;
        attachment.startOfEncoderSampleIndex = timing->start_sample;
        attachment.endOfEncoderSampleIndex = timing->start_sample + 1;
    }
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_end_pass(void) {
    _sgu_mtl_gpu_timing_frame_t* frame = &_sgu_mtl_gpu_timing_state.current;
    if (_sgu_gpu_pass_timing_state.enabled && !frame->command_buffer && _sg.mtl.cmd_buffer) {
        frame->command_buffer = _sg.mtl.cmd_buffer;
        frame->frame_index = _sg.frame_index;
#if !__has_feature(objc_arc)
        [frame->command_buffer retain];
#endif
    }
    frame->active_pass = -1;
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_commit(void) {
    _sgu_mtl_gpu_timing_frame_t* current = &_sgu_mtl_gpu_timing_state.current;
    if (!current->command_buffer) {
        _sgu_mtl_gpu_timing_release_frame(current);
        return;
    }
    for (int index = 0; index < _SGU_GPU_TIMING_PENDING_FRAMES; ++index) {
        _sgu_mtl_gpu_timing_frame_t* pending = &_sgu_mtl_gpu_timing_state.pending[index];
        if (!pending->command_buffer) {
            *pending = *current;
            memset(current, 0, sizeof(*current));
            current->active_pass = -1;
            return;
        }
    }
    /* Never wait for profiling. Drop this timing frame if readback is behind. */
    _sgu_mtl_gpu_timing_release_frame(current);
}

_SOKOL_PRIVATE int _sgu_mtl_gpu_timing_poll(sg_gpu_pass_timing* timings, int max_timings) {
    _sgu_mtl_gpu_timing_frame_t* oldest = NULL;
    for (int index = 0; index < _SGU_GPU_TIMING_PENDING_FRAMES; ++index) {
        _sgu_mtl_gpu_timing_frame_t* pending = &_sgu_mtl_gpu_timing_state.pending[index];
        if (pending->command_buffer && (!oldest || pending->frame_index < oldest->frame_index)) {
            oldest = pending;
        }
    }
    if (!oldest || !oldest->command_buffer || [oldest->command_buffer status] < MTLCommandBufferStatusCompleted) {
        return 0;
    }
    if ([oldest->command_buffer status] != MTLCommandBufferStatusCompleted) {
        _sgu_mtl_gpu_timing_release_frame(oldest);
        return 0;
    }
    int result_count = 0;
    const CFTimeInterval start = oldest->command_buffer.GPUStartTime;
    const CFTimeInterval end = oldest->command_buffer.GPUEndTime;
    if (start > 0.0 && end >= start) {
        strncpy(timings[0].label, "Frame", SG_GPU_PASS_TIMING_LABEL_SIZE);
        timings[0].label[SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
        timings[0].frame_index = oldest->frame_index;
        timings[0].milliseconds = (double)(end - start) * 1000.0;
        result_count = 1;
    }
    if (!oldest->sample_buffer || oldest->pass_count == 0 || oldest->cpu_begin == 0) {
        _sgu_mtl_gpu_timing_release_frame(oldest);
        return result_count;
    }
    const uint32_t sample_count = oldest->pass_count * 4;
    NSData* data = nil;
    double milliseconds_per_tick = 0.0;
    if (@available(macOS 10.15, iOS 14.0, *)) {
        // Metal GPU timestamps need not be nanoseconds. sampleTimestamps
        // returns CPU time in nanoseconds (not raw mach_absolute_time ticks).
        MTLTimestamp cpu_end = 0, gpu_end = 0;
        [_sg.mtl.device sampleTimestamps:&cpu_end gpuTimestamp:&gpu_end];
        if (cpu_end > oldest->cpu_begin && gpu_end > oldest->gpu_begin) {
            milliseconds_per_tick = (double)(cpu_end - oldest->cpu_begin) /
                (double)(gpu_end - oldest->gpu_begin) / 1000000.0;
        }
        data = [oldest->sample_buffer resolveCounterRange:NSMakeRange(0, sample_count)];
    }
    if (!data || data.length < sample_count * sizeof(MTLCounterResultTimestamp) || milliseconds_per_tick <= 0.0) {
        _sgu_mtl_gpu_timing_release_frame(oldest);
        return result_count;
    }
    const MTLCounterResultTimestamp* samples = (const MTLCounterResultTimestamp*)data.bytes;
    for (uint32_t index = 0; index < oldest->pass_count && result_count < max_timings; ++index) {
        const _sgu_mtl_gpu_pass_t* pass = &oldest->passes[index];
        if (!pass->attached) continue;
        uint64_t starts[2] = {0, 0}, ends[2] = {0, 0};
        uint64_t duration = 0;
        const uint32_t stages = pass->render ? 2 : 1;
        bool invalid = false;
        for (uint32_t stage = 0; stage < stages; ++stage) {
            const uint64_t stage_start = samples[pass->start_sample + stage * 2].timestamp;
            const uint64_t stage_end = samples[pass->start_sample + stage * 2 + 1].timestamp;
            // An optimized-away stage has no interval; never invent timing.
            if (stage_start == 0 && stage_end == 0) continue;
            if (stage_start == 0 || stage_end < stage_start ||
                stage_start == MTLCounterErrorValue || stage_end == MTLCounterErrorValue ||
                stage_start == MTLCounterDontSample || stage_end == MTLCounterDontSample) {
                invalid = true;
                break;
            }
            starts[stage] = stage_start;
            ends[stage] = stage_end;
            duration += stage_end - stage_start;
        }
        if (invalid || (starts[0] == 0 && starts[1] == 0)) continue;
        // Vertex and fragment stages can be scheduled far apart. The enclosing
        // span charges this pass for unrelated GPU work between its stages.
        // Measure their union instead, also avoiding double-counted overlap.
        if (starts[0] != 0 && starts[1] != 0) {
            const uint64_t overlap_start = starts[0] > starts[1] ? starts[0] : starts[1];
            const uint64_t overlap_end = ends[0] < ends[1] ? ends[0] : ends[1];
            if (overlap_end > overlap_start) duration -= overlap_end - overlap_start;
        }
        sg_gpu_pass_timing* timing = &timings[result_count++];
        strncpy(timing->label, pass->label, SG_GPU_PASS_TIMING_LABEL_SIZE);
        timing->label[SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
        timing->frame_index = oldest->frame_index;
        timing->milliseconds = (double)duration * milliseconds_per_tick;
    }
    _sgu_mtl_gpu_timing_release_frame(oldest);
    return result_count;
}

_SOKOL_PRIVATE void _sgu_mtl_gpu_timing_shutdown(void) {
    _sgu_mtl_gpu_timing_release_frame(&_sgu_mtl_gpu_timing_state.current);
    for (int index = 0; index < _SGU_GPU_TIMING_PENDING_FRAMES; ++index) {
        _sgu_mtl_gpu_timing_release_frame(&_sgu_mtl_gpu_timing_state.pending[index]);
    }
    if (_sgu_mtl_gpu_timing_state.timestamp_counter_set) {
#if !__has_feature(objc_arc)
        [_sgu_mtl_gpu_timing_state.timestamp_counter_set release];
#endif
    }
    memset(&_sgu_mtl_gpu_timing_state, 0, sizeof(_sgu_mtl_gpu_timing_state));
}

#endif /* SOKOL_METAL */

/*--- GPU Pass Timings: Trace Hooks -----------------------------------------*/
_SOKOL_PRIVATE void _sgu_gpu_timing_trace_end_pass(void* user_data) {
    _sgu_gpu_pass_timing_state_t* state = (_sgu_gpu_pass_timing_state_t*)user_data;
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_end_pass();
#endif
    if (state->previous_hooks.end_pass) {
        state->previous_hooks.end_pass(state->previous_hooks.user_data);
    }
}

_SOKOL_PRIVATE void _sgu_gpu_timing_trace_commit(void* user_data) {
    _sgu_gpu_pass_timing_state_t* state = (_sgu_gpu_pass_timing_state_t*)user_data;
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_commit();
#endif
    if (state->previous_hooks.commit) {
        state->previous_hooks.commit(state->previous_hooks.user_data);
    }
}

/*--- Metal Backend ----------------------------------------------------------*/
#if defined(SOKOL_METAL)

#if !defined(__OBJC__)
#error "Metal implementation requires Objective-C"
#endif
#import <Metal/Metal.h>

typedef struct {
    id<MTLBuffer> buffer;
    id<MTLCommandBuffer> cmd_buffer;
    int width;
    int height;
    int padded_bytes_per_row;
    int bytes_per_row;
} _sgu_mtl_readback_request_t;

_SOKOL_PRIVATE void* _sgu_mtl_readback_request(sg_image img) {
    _sg_image_t* img_ptr = _sg_lookup_image(img.id);
    if (!img_ptr || img_ptr->slot.state != SG_RESOURCESTATE_VALID) {
        return NULL;
    }

    int slot = img_ptr->cmn.active_slot;
    int mtl_id = img_ptr->mtl.tex[slot];
    id<MTLTexture> tex = (id<MTLTexture>)_sg.mtl.idpool.pool[(NSUInteger)mtl_id];
    if (!tex) {
        return NULL;
    }

    id<MTLDevice> device = _sg.mtl.device;
    int width = img_ptr->cmn.width;
    int height = img_ptr->cmn.height;
    int bytes_per_pixel = 4; // Assuming RGBA8
    int bytes_per_row = width * bytes_per_pixel;
    int padded_bytes_per_row = (bytes_per_row + 255) & ~255;
    int buffer_size = padded_bytes_per_row * height;

    id<MTLBuffer> read_buffer = [device newBufferWithLength:(NSUInteger)buffer_size options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue = _sg.mtl.cmd_queue;
    id<MTLCommandBuffer> cmd_buffer = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit_encoder = [cmd_buffer blitCommandEncoder];

    [blit_encoder copyFromTexture:tex
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake((NSUInteger)width, (NSUInteger)height, 1)
                      toBuffer:read_buffer
             destinationOffset:0
        destinationBytesPerRow:(NSUInteger)padded_bytes_per_row
      destinationBytesPerImage:(NSUInteger)buffer_size];

    [blit_encoder endEncoding];
    [cmd_buffer commit];

    _sgu_mtl_readback_request_t* req = (_sgu_mtl_readback_request_t*) malloc(sizeof(_sgu_mtl_readback_request_t));
    req->buffer = read_buffer;
    req->cmd_buffer = cmd_buffer;
#if !__has_feature(objc_arc)
    [req->cmd_buffer retain];
#endif
    req->width = width;
    req->height = height;
    req->padded_bytes_per_row = padded_bytes_per_row;
    req->bytes_per_row = bytes_per_row;

    return (void*)req;
}

_SOKOL_PRIVATE bool _sgu_mtl_readback_is_ready(void* request) {
    _sgu_mtl_readback_request_t* req = (_sgu_mtl_readback_request_t*)request;
    return [req->cmd_buffer status] == MTLCommandBufferStatusCompleted;
}

_SOKOL_PRIVATE void _sgu_mtl_readback_wait(void* request) {
    _sgu_mtl_readback_request_t* req = (_sgu_mtl_readback_request_t*)request;
    if (req->cmd_buffer) {
        int timeout_ms = GFX_READBACK_TIMEOUT_MS;
        while (timeout_ms > 0) {
            MTLCommandBufferStatus status = [req->cmd_buffer status];
            if (status >= MTLCommandBufferStatusCompleted) {
                break;
            }
            [NSThread sleepForTimeInterval:0.001];
            timeout_ms--;
        }
    }
}

_SOKOL_PRIVATE void _sgu_mtl_readback_copy_data(void* request, void* out_pixels) {
    _sgu_mtl_readback_request_t* req = (_sgu_mtl_readback_request_t*)request;
    uint8_t* src = (uint8_t*)[req->buffer contents];
    uint8_t* dst = (uint8_t*)out_pixels;

    if (req->padded_bytes_per_row == req->bytes_per_row) {
        memcpy(dst, src, (size_t)req->width * (size_t)req->height * 4);
    } else {
        for (int i = 0; i < req->height; ++i) {
            memcpy(dst + (size_t)i * (size_t)req->bytes_per_row, src + (size_t)i * (size_t)req->padded_bytes_per_row, (size_t)req->bytes_per_row);
        }
    }
}

_SOKOL_PRIVATE void _sgu_mtl_readback_destroy(void* request) {
    _sgu_mtl_readback_request_t* req = (_sgu_mtl_readback_request_t*)request;
#if !__has_feature(objc_arc)
    [req->buffer release];
    [req->cmd_buffer release];
#endif
    free(req);
}

#endif /* SOKOL_METAL */

/*--- GL Backend -------------------------------------------------------------*/
#if defined(_SOKOL_ANY_GL)

typedef struct {
    GLuint pbo;
    GLsync sync;
    int width;
    int height;
    int bytes_per_row;
} _sgu_gl_readback_request_t;

_SOKOL_PRIVATE void* _sgu_gl_readback_request(sg_image img) {
    _sg_image_t* img_ptr = _sg_lookup_image(img.id);
    if (!img_ptr || img_ptr->slot.state != SG_RESOURCESTATE_VALID) {
        return NULL;
    }

    int slot = img_ptr->cmn.active_slot;
    GLuint tex_id = img_ptr->gl.tex[slot];
    int width = img_ptr->cmn.width;
    int height = img_ptr->cmn.height;
    int bytes_per_row = width * 4;
    int buffer_size = bytes_per_row * height;

    GLuint pbo;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, buffer_size, NULL, GL_STREAM_READ);

    GLuint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint*)&prev_fbo);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_id, 0);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    _sgu_gl_readback_request_t* req = (_sgu_gl_readback_request_t*) malloc(sizeof(_sgu_gl_readback_request_t));
    req->pbo = pbo;
    req->sync = sync;
    req->width = width;
    req->height = height;
    req->bytes_per_row = bytes_per_row;
    return (void*)req;
}

_SOKOL_PRIVATE bool _sgu_gl_readback_is_ready(void* request) {
    _sgu_gl_readback_request_t* req = (_sgu_gl_readback_request_t*)request;
    GLenum res = glClientWaitSync(req->sync, 0, 0);
    return (res == GL_ALREADY_SIGNALED || res == GL_CONDITION_SATISFIED);
}

_SOKOL_PRIVATE void _sgu_gl_readback_wait(void* request) {
    _sgu_gl_readback_request_t* req = (_sgu_gl_readback_request_t*)request;
    glClientWaitSync(req->sync, GL_SYNC_FLUSH_COMMANDS_BIT, (GLuint64)GFX_READBACK_TIMEOUT_MS * 1000000);
}

_SOKOL_PRIVATE void _sgu_gl_readback_copy_data(void* request, void* out_pixels) {
    _sgu_gl_readback_request_t* req = (_sgu_gl_readback_request_t*)request;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, req->pbo);
    void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)req->width * (GLsizeiptr)req->height * 4, GL_MAP_READ_BIT);
    if (ptr) {
        memcpy(out_pixels, ptr, (size_t)req->width * (size_t)req->height * 4);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

_SOKOL_PRIVATE void _sgu_gl_readback_destroy(void* request) {
    _sgu_gl_readback_request_t* req = (_sgu_gl_readback_request_t*)request;
    glDeleteBuffers(1, &req->pbo);
    glDeleteSync(req->sync);
    free(req);
}

#endif /* _SOKOL_ANY_GL */

/*--- D3D11 Backend ----------------------------------------------------------*/
#if defined(SOKOL_D3D11)

typedef struct {
    ID3D11Texture2D* staging_tex;
    int width;
    int height;
    int bytes_per_row;
} _sgu_d3d11_readback_request_t;

_SOKOL_PRIVATE void* _sgu_d3d11_readback_request(sg_image img) {
    _sg_image_t* img_ptr = _sg_lookup_image(img.id);
    if (!img_ptr || img_ptr->slot.state != SG_RESOURCESTATE_VALID) {
        return NULL;
    }

    int width = img_ptr->cmn.width;
    int height = img_ptr->cmn.height;
    int bytes_per_row = width * 4;

    ID3D11Device* dev = _sg.d3d11.dev;
    ID3D11DeviceContext* ctx = _sg.d3d11.ctx;
    ID3D11Texture2D* src_tex = img_ptr->d3d11.tex2d;

    D3D11_TEXTURE2D_DESC desc;
    src_tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging_tex = NULL;
    HRESULT hr = dev->CreateTexture2D(&desc, NULL, &staging_tex);
    if (FAILED(hr)) {
        return NULL;
    }

    ctx->CopyResource(staging_tex, src_tex);

    _sgu_d3d11_readback_request_t* req = (_sgu_d3d11_readback_request_t*) malloc(sizeof(_sgu_d3d11_readback_request_t));
    req->staging_tex = staging_tex;
    req->width = width;
    req->height = height;
    req->bytes_per_row = bytes_per_row;
    return (void*)req;
}

_SOKOL_PRIVATE bool _sgu_d3d11_readback_is_ready(void* request) {
    _sgu_d3d11_readback_request_t* req = (_sgu_d3d11_readback_request_t*)request;
    ID3D11DeviceContext* ctx = _sg.d3d11.ctx;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(req->staging_tex, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (hr == S_OK) {
        ctx->Unmap(req->staging_tex, 0);
        return true;
    }
    return false;
}

_SOKOL_PRIVATE void _sgu_d3d11_readback_wait(void* request) {
    _sgu_d3d11_readback_request_t* req = (_sgu_d3d11_readback_request_t*)request;
    ID3D11DeviceContext* ctx = _sg.d3d11.ctx;
    int timeout_ms = GFX_READBACK_TIMEOUT_MS;
    while (timeout_ms > 0) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx->Map(req->staging_tex, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == S_OK) {
            ctx->Unmap(req->staging_tex, 0);
            break;
        }
        timeout_ms--;
    }
}

_SOKOL_PRIVATE void _sgu_d3d11_readback_copy_data(void* request, void* out_pixels) {
    _sgu_d3d11_readback_request_t* req = (_sgu_d3d11_readback_request_t*)request;
    ID3D11DeviceContext* ctx = _sg.d3d11.ctx;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(req->staging_tex, 0, D3D11_MAP_READ, 0, &mapped))) {
        if (mapped.RowPitch == (UINT)req->bytes_per_row) {
            memcpy(out_pixels, mapped.pData, (size_t)req->height * (size_t)req->bytes_per_row);
        } else {
            for (int i = 0; i < req->height; ++i) {
                memcpy((uint8_t*)out_pixels + (size_t)i * (size_t)req->bytes_per_row, (uint8_t*)mapped.pData + (size_t)i * (size_t)mapped.RowPitch, (size_t)req->bytes_per_row);
            }
        }
        ctx->Unmap(req->staging_tex, 0);
    }
}

_SOKOL_PRIVATE void _sgu_d3d11_readback_destroy(void* request) {
    _sgu_d3d11_readback_request_t* req = (_sgu_d3d11_readback_request_t*)request;
    req->staging_tex->Release();
    free(req);
}

#endif /* SOKOL_D3D11 */

/*--- Public API Implementation ----------------------------------------------*/
SOKOL_API_IMPL void* sg_readback_request(sg_image img) {
#if defined(SOKOL_METAL)
    return _sgu_mtl_readback_request(img);
#elif defined(_SOKOL_ANY_GL)
    return _sgu_gl_readback_request(img);
#elif defined(SOKOL_D3D11)
    return _sgu_d3d11_readback_request(img);
#else
    (void)img;
    return NULL;
#endif
}

SOKOL_API_IMPL bool sg_readback_is_ready(void* request) {
    if (!request) return false;
#if defined(SOKOL_METAL)
    return _sgu_mtl_readback_is_ready(request);
#elif defined(_SOKOL_ANY_GL)
    return _sgu_gl_readback_is_ready(request);
#elif defined(SOKOL_D3D11)
    return _sgu_d3d11_readback_is_ready(request);
#else
    return false;
#endif
}

SOKOL_API_IMPL void sg_readback_wait(void* request) {
    if (!request) return;
#if defined(SOKOL_METAL)
    _sgu_mtl_readback_wait(request);
#elif defined(_SOKOL_ANY_GL)
    _sgu_gl_readback_wait(request);
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_readback_wait(request);
#endif
}

SOKOL_API_IMPL void sg_readback_copy_data(void* request, void* out_pixels) {
    if (!request || !out_pixels) return;
#if defined(SOKOL_METAL)
    _sgu_mtl_readback_copy_data(request, out_pixels);
#elif defined(_SOKOL_ANY_GL)
    _sgu_gl_readback_copy_data(request, out_pixels);
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_readback_copy_data(request, out_pixels);
#endif
}

SOKOL_API_IMPL void sg_readback_destroy(void* request) {
    if (!request) return;
#if defined(SOKOL_METAL)
    _sgu_mtl_readback_destroy(request);
#elif defined(_SOKOL_ANY_GL)
    _sgu_gl_readback_destroy(request);
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_readback_destroy(request);
#endif
}

SOKOL_API_IMPL bool sg_gpu_pass_timings_setup(void) {
    if (_sgu_gpu_pass_timing_state.setup) {
        return _sgu_gpu_pass_timing_state.supported;
    }
    _sgu_gpu_pass_timing_state.setup = true;
#if defined(SOKOL_METAL)
    _sgu_gpu_pass_timing_state.supported = _sgu_mtl_gpu_timing_initialize();
#else
    _sgu_gpu_pass_timing_state.supported = false;
#endif
    if (!_sgu_gpu_pass_timing_state.supported) {
        return false;
    }
    sg_trace_hooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.user_data = &_sgu_gpu_pass_timing_state;
    hooks.end_pass = _sgu_gpu_timing_trace_end_pass;
    hooks.commit = _sgu_gpu_timing_trace_commit;
    _sgu_gpu_pass_timing_state.previous_hooks = sg_install_trace_hooks(&hooks);
    return true;
}

SOKOL_API_IMPL void sg_gpu_pass_timings_shutdown(void) {
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_shutdown();
#endif
    memset(&_sgu_gpu_pass_timing_state, 0, sizeof(_sgu_gpu_pass_timing_state));
}

SOKOL_API_IMPL void sg_gpu_pass_timings_set_enabled(bool enabled) {
    _sgu_gpu_pass_timing_state.enabled =
        _sgu_gpu_pass_timing_state.setup && _sgu_gpu_pass_timing_state.supported && enabled;
}

SOKOL_API_IMPL void sg_gpu_pass_timings_mtl_prepare_render_pass(const sg_pass* pass, const void* descriptor) {
#if defined(SOKOL_METAL)
    if (_sgu_gpu_pass_timing_state.enabled && pass && !pass->compute && descriptor) {
        _sgu_mtl_gpu_timing_render_descriptor(pass, (__bridge MTLRenderPassDescriptor*)descriptor);
    }
#else
    (void)pass;
    (void)descriptor;
#endif
}

SOKOL_API_IMPL void sg_gpu_pass_timings_mtl_prepare_compute_pass(const sg_pass* pass, const void* descriptor) {
#if defined(SOKOL_METAL)
    if (_sgu_gpu_pass_timing_state.enabled && pass && pass->compute && descriptor) {
        _sgu_mtl_gpu_timing_compute_descriptor(pass, (__bridge MTLComputePassDescriptor*)descriptor);
    }
#else
    (void)pass;
    (void)descriptor;
#endif
}

SOKOL_API_IMPL bool sg_gpu_pass_timings_supported(void) {
    return _sgu_gpu_pass_timing_state.setup && _sgu_gpu_pass_timing_state.supported;
}

SOKOL_API_IMPL int sg_gpu_pass_timings_poll(sg_gpu_pass_timing* timings, int max_timings) {
    if (!_sgu_gpu_pass_timing_state.supported || !timings || max_timings <= 0) {
        return 0;
    }
#if defined(SOKOL_METAL)
    return _sgu_mtl_gpu_timing_poll(timings, max_timings);
#else
    return 0;
#endif
}

#endif /* SOKOL_GFX_UTILS_IMPL */
