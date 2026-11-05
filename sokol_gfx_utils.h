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

/* Indirect commands live in storage buffers created by this factory. The
   caller owns the returned sg_buffer and destroys it with sg_destroy_buffer.
   Transient, unsealed, injected and staging buffers are not accepted. */
SOKOL_GFX_UTILS_API_DECL sg_buffer sg_make_indirect_buffer(const sg_buffer_desc* desc);
SOKOL_GFX_UTILS_API_DECL bool sg_indirect_supported(void);
/* Call outside a render pass after GPU writes or copies that produce command
   arguments. Dispatch also inserts this barrier within a compute pass. */
SOKOL_GFX_UTILS_API_DECL void sg_indirect_barrier(void);
/* Offsets are four-byte aligned. Draw arguments are four uint32_t values:
   count, instance count, first vertex, first instance. Indexed draws insert
   int32_t base_vertex before first instance. Dispatch uses three uint32_t
   group counts. Apply the pipeline, bindings and uniforms before these calls.
   Argument buffers must come from sg_make_indirect_buffer. */
SOKOL_GFX_UTILS_API_DECL void sg_draw_indirect(sg_buffer args, int offset);
SOKOL_GFX_UTILS_API_DECL void sg_dispatch_indirect(sg_buffer args, int offset);

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
    result for the frame. Vulkan measures the render command buffer (staging
    uploads use a separate command buffer); D3D11 measures from the first pass
    through commit. Metal measures the command buffer. Metal stage-boundary
    counters are optional; unsupported devices report only "Frame". Render timings measure
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

/* Requires a valid sg context and SOKOL_TRACE_HOOKS in the implementation.
   Install timing before other sg_trace_hooks users are initialized. */
SOKOL_GFX_UTILS_API_DECL bool sg_gpu_pass_timings_setup(void);

/* Release timing resources after sg_shutdown has drained in-flight work. */
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_shutdown(void);

/* Enable or disable recording. Unsupported backends remain disabled. */
SOKOL_GFX_UTILS_API_DECL void sg_gpu_pass_timings_set_enabled(bool enabled);

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

#if defined(_SOKOL_GL_HAS_COMPUTE)
#ifndef GL_COMMAND_BARRIER_BIT
#define GL_COMMAND_BARRIER_BIT 0x00000040
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#define GL_DISPATCH_INDIRECT_BUFFER 0x90EE
#endif
#if defined(_SOKOL_USE_WIN32_GL_LOADER)
typedef void (GL_APIENTRY* _sgu_draw_arrays_indirect_fn)(GLenum, const void*);
typedef void (GL_APIENTRY* _sgu_draw_elements_indirect_fn)(GLenum, GLenum, const void*);
typedef void (GL_APIENTRY* _sgu_dispatch_indirect_fn)(GLintptr);
static _sgu_draw_arrays_indirect_fn _sgu_draw_arrays_indirect;
static _sgu_draw_elements_indirect_fn _sgu_draw_elements_indirect;
static _sgu_dispatch_indirect_fn _sgu_dispatch_indirect;
#else
#define _sgu_draw_arrays_indirect glDrawArraysIndirect
#define _sgu_draw_elements_indirect glDrawElementsIndirect
#define _sgu_dispatch_indirect glDispatchComputeIndirect
#endif
#endif

#if defined(SOKOL_VULKAN)
_SOKOL_PRIVATE sg_resource_state _sgu_vk_create_indirect_buffer(_sg_buffer_t* buf, const sg_buffer_desc* desc) {
    SOKOL_ASSERT(_sg.vk.dev);
    SOKOL_ASSERT(buf && desc);
    SOKOL_ASSERT(buf->cmn.size > 0);
    SOKOL_ASSERT(0 == buf->vk.buf);
    SOKOL_ASSERT(0 == buf->vk.mem);
    SOKOL_ASSERT(0 == buf->vk.dev_addr);
    VkResult res;
    // FIXME: inject external buffer

    // NOTE: unlike images, the initial access mask for buffers is their
    // default access, this is because buffers are immediately ready for
    // any read type (unless images, which have an UNDEFINED initial image layout)
    buf->vk.cur_access = _sg_vk_default_buffer_access_mask(buf);

    _SG_STRUCT(VkBufferCreateInfo, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = (VkDeviceSize)buf->cmn.size;
    create_info.usage = _sg_vk_buffer_usage(&buf->cmn.usage) | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    res = vkCreateBuffer(_sg.vk.dev, &create_info, 0, &buf->vk.buf);
    if (res != VK_SUCCESS) {
        _SG_ERROR(VULKAN_CREATE_BUFFER_FAILED);
        return SG_RESOURCESTATE_FAILED;
    }
    SOKOL_ASSERT(buf->vk.buf);
    _sg_vk_set_object_label(VK_OBJECT_TYPE_BUFFER, (uint64_t)buf->vk.buf, desc->label);

    if (!_sg_vk_mem_alloc_buffer_device_memory(buf)) {
        return SG_RESOURCESTATE_FAILED;
    }
    SOKOL_ASSERT(buf->vk.mem);
    res = vkBindBufferMemory(_sg.vk.dev, buf->vk.buf, buf->vk.mem, 0);
    if (res != VK_SUCCESS) {
        _SG_ERROR(VULKAN_BIND_BUFFER_MEMORY_FAILED);
        return SG_RESOURCESTATE_FAILED;
    }
    if (buf->cmn.usage.storage_buffer) {
        _SG_STRUCT(VkBufferDeviceAddressInfo, addr_info);
        addr_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addr_info.buffer = buf->vk.buf;
        buf->vk.dev_addr = vkGetBufferDeviceAddress(_sg.vk.dev, &addr_info);
        SOKOL_ASSERT(buf->vk.dev_addr);
    }
    if (desc->data.ptr) {
        _sg_vk_staging_copy_buffer_data(buf, &desc->data, 0, 0, desc->data.size, false);
    }
    return SG_RESOURCESTATE_VALID;
}

#endif

SOKOL_API_IMPL bool sg_indirect_supported(void) {
    if (!sg_isvalid() || !sg_query_features().compute) return false;
    #if defined(_SOKOL_ANY_GL) && !defined(_SOKOL_GL_HAS_COMPUTE)
    return false;
    #else
    #if defined(_SOKOL_USE_WIN32_GL_LOADER)
    _sg_wglGetProcAddress get_proc = (_sg_wglGetProcAddress)GetProcAddress(_sg.gl.opengl32_dll, "wglGetProcAddress");
    if (!get_proc) return false;
    _sgu_draw_arrays_indirect = (_sgu_draw_arrays_indirect_fn)_sg_gl_getprocaddr("glDrawArraysIndirect", get_proc);
    _sgu_draw_elements_indirect = (_sgu_draw_elements_indirect_fn)_sg_gl_getprocaddr("glDrawElementsIndirect", get_proc);
    _sgu_dispatch_indirect = (_sgu_dispatch_indirect_fn)_sg_gl_getprocaddr("glDispatchComputeIndirect", get_proc);
    return _sgu_draw_arrays_indirect && _sgu_draw_elements_indirect && _sgu_dispatch_indirect;
    #else
    return true;
    #endif
    #endif
}

SOKOL_API_IMPL sg_buffer sg_make_indirect_buffer(const sg_buffer_desc* desc) {
    sg_buffer invalid = { SG_INVALID_ID };
    if (!desc || !sg_indirect_supported() || _sg.cur_pass.in_pass) return invalid;
    sg_buffer_desc d = sg_query_buffer_defaults(desc);
    if (!d.usage.storage_buffer || d.usage.write_transient || d.usage.write_unsealed ||
        d.usage.staging_buffer || d.usage.staging_index_buffer || d.gl_buffer ||
        d.mtl_buffer || d.d3d11_buffer || d.wgpu_buffer || !d.size || d.size > INT32_MAX ||
        (d.size & 3) || (d.data.ptr && d.data.size != d.size)) return invalid;
    #if defined(SOKOL_VULKAN)
    sg_buffer result = sg_alloc_buffer();
    _sg_buffer_t* buf = _sg_lookup_buffer(result.id);
    if (!buf) return result;
    _sg_buffer_common_init(&buf->cmn, &d);
    buf->slot.state = _sgu_vk_create_indirect_buffer(buf, &d);
    _sg_resource_stats_inc(buffers.inited);
    _SG_TRACE_ARGS(init_buffer, result, &d);
    return result;
    #elif defined(SOKOL_D3D11)
    _SG_STRUCT(D3D11_BUFFER_DESC, native_desc);
    native_desc.ByteWidth = (UINT)d.size;
    native_desc.Usage = D3D11_USAGE_DEFAULT;
    native_desc.BindFlags = _sg_d3d11_buffer_bind_flags(&d.usage);
    native_desc.MiscFlags = _sg_d3d11_buffer_misc_flags(&d.usage) | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    _SG_STRUCT(D3D11_SUBRESOURCE_DATA, initial);
    initial.pSysMem = d.data.ptr;
    ID3D11Buffer* native = 0;
    HRESULT hr = _sg_d3d11_CreateBuffer(_sg.d3d11.dev, &native_desc, d.data.ptr ? &initial : 0, &native);
    if (FAILED(hr) || !native) return invalid;
    d.d3d11_buffer = native;
    d.data.ptr = 0; d.data.size = 0;
    sg_buffer result = sg_make_buffer(&d);
    _sg_d3d11_Release(native);
    return result;
    #elif defined(SOKOL_WGPU)
    _SG_STRUCT(WGPUBufferDescriptor, native_desc);
    native_desc.size = d.size;
    native_desc.usage = _sg_wgpu_buffer_usage(&d.usage) | WGPUBufferUsage_Indirect;
    native_desc.mappedAtCreation = d.data.ptr != 0;
    WGPUBuffer native = wgpuDeviceCreateBuffer(_sg.wgpu.dev, &native_desc);
    if (!native) return invalid;
    if (d.data.ptr) {
        void* mapped = wgpuBufferGetMappedRange(native, 0, d.size);
        if (!mapped) { wgpuBufferRelease(native); return invalid; }
        memcpy(mapped, d.data.ptr, d.size);
        wgpuBufferUnmap(native);
    }
    d.wgpu_buffer = native;
    d.data.ptr = 0; d.data.size = 0;
    sg_buffer result = sg_make_buffer(&d);
    wgpuBufferRelease(native);
    return result;
    #else
    return sg_make_buffer(&d);
    #endif
}

SOKOL_API_IMPL void sg_indirect_barrier(void) {
    if (!sg_indirect_supported()) return;
    if (_sg.cur_pass.in_pass && !_sg.cur_pass.is_compute) return;
    #if defined(SOKOL_VULKAN)
    _sg_vk_acquire_frame_command_buffers();
    if (!_sg.vk.frame.cmd_buf) return;
    _SG_STRUCT(VkMemoryBarrier2, barrier);
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    _SG_STRUCT(VkDependencyInfo, dependency);
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(_sg.vk.frame.cmd_buf, &dependency);
    #elif defined(_SOKOL_GL_HAS_COMPUTE)
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    #endif
}

_SOKOL_PRIVATE _sg_buffer_t* _sgu_indirect_args(sg_buffer args, int offset, int size, bool compute) {
    if (!sg_indirect_supported() || !_sg.cur_pass.valid || !_sg.cur_pass.in_pass ||
        _sg.cur_pass.is_compute != compute || !_sg.next_draw_valid || offset < 0 || (offset & 3)) return 0;
    _sg_buffer_t* buf = _sg_lookup_buffer(args.id);
    if (!buf || buf->slot.state != SG_RESOURCESTATE_VALID || !buf->cmn.usage.storage_buffer ||
        size > buf->cmn.size || offset > buf->cmn.size - size) return 0;
    #if defined(SOKOL_DEBUG)
    if (!(compute ? _sg_validate_dispatch(1, 1, 1) : _sg_validate_draw(0, 1, 1))) return 0;
    #endif
    return buf;
}

SOKOL_API_IMPL void sg_draw_indirect(sg_buffer args, int offset) {
    _sg_buffer_t* buf = _sgu_indirect_args(args, offset, _sg.use_indexed_draw ? 20 : 16, false);
    if (!buf) return;
    #if defined(SOKOL_METAL)
    const _sg_pipeline_t* pip = _sg_pipeline_ref_ptr(&_sg.cur_pip);
    id<MTLBuffer> native = _sg_mtl_id(buf->mtl.buf[buf->cmn.active_slot]);
    if (_sg.use_indexed_draw) {
        const _sg_buffer_t* ib = _sg_buffer_ref_ptr(&_sg.mtl.cache.cur_ibuf);
        [_sg.mtl.render_cmd_encoder drawIndexedPrimitives:pip->mtl.prim_type
            indexType:pip->mtl.index_type indexBuffer:_sg_mtl_id(ib->mtl.buf[ib->cmn.active_slot])
            indexBufferOffset:(NSUInteger)_sg.mtl.cache.cur_ibuf_offset
            indirectBuffer:native indirectBufferOffset:(NSUInteger)offset];
    } else {
        [_sg.mtl.render_cmd_encoder drawPrimitives:pip->mtl.prim_type
            indirectBuffer:native indirectBufferOffset:(NSUInteger)offset];
    }
    #elif defined(SOKOL_VULKAN)
    VkCommandBuffer cmd = _sg.vk.frame.cmd_buf;
    if (_sg.vk.uniforms.dirty && !_sg_vk_bind_uniform_descriptor_set(cmd)) return;
    if (_sg.use_indexed_draw) vkCmdDrawIndexedIndirect(cmd, buf->vk.buf, (VkDeviceSize)offset, 1, 0);
    else vkCmdDrawIndirect(cmd, buf->vk.buf, (VkDeviceSize)offset, 1, 0);
    #elif defined(SOKOL_D3D11)
    #if defined(__cplusplus)
    if (_sg.use_indexed_draw) _sg.d3d11.ctx->DrawIndexedInstancedIndirect(buf->d3d11.buf, (UINT)offset);
    else _sg.d3d11.ctx->DrawInstancedIndirect(buf->d3d11.buf, (UINT)offset);
    #else
    if (_sg.use_indexed_draw) _sg.d3d11.ctx->lpVtbl->DrawIndexedInstancedIndirect(_sg.d3d11.ctx, buf->d3d11.buf, (UINT)offset);
    else _sg.d3d11.ctx->lpVtbl->DrawInstancedIndirect(_sg.d3d11.ctx, buf->d3d11.buf, (UINT)offset);
    #endif
    #elif defined(SOKOL_WGPU)
    if (_sg.wgpu.uniform.dirty) _sg_wgpu_uniform_system_set_bindgroup();
    if (_sg.use_indexed_draw) wgpuRenderPassEncoderDrawIndexedIndirect(_sg.wgpu.rpass_enc, buf->wgpu.buf, (uint64_t)offset);
    else wgpuRenderPassEncoderDrawIndirect(_sg.wgpu.rpass_enc, buf->wgpu.buf, (uint64_t)offset);
    #elif defined(_SOKOL_GL_HAS_COMPUTE)
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf->gl.buf[buf->cmn.active_slot]);
    if (_sg.use_indexed_draw) _sgu_draw_elements_indirect(_sg.gl.cache.cur_primitive_type, _sg.gl.cache.cur_index_type, (const void*)(intptr_t)offset);
    else _sgu_draw_arrays_indirect(_sg.gl.cache.cur_primitive_type, (const void*)(intptr_t)offset);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    #endif
}

SOKOL_API_IMPL void sg_dispatch_indirect(sg_buffer args, int offset) {
    _sg_buffer_t* buf = _sgu_indirect_args(args, offset, 12, true);
    if (!buf) return;
    sg_indirect_barrier();
    #if defined(SOKOL_METAL)
    const _sg_pipeline_t* pip = _sg_pipeline_ref_ptr(&_sg.cur_pip);
    [_sg.mtl.compute_cmd_encoder dispatchThreadgroupsWithIndirectBuffer:_sg_mtl_id(buf->mtl.buf[buf->cmn.active_slot])
        indirectBufferOffset:(NSUInteger)offset threadsPerThreadgroup:pip->mtl.threads_per_threadgroup];
    #elif defined(SOKOL_VULKAN)
    VkCommandBuffer cmd = _sg.vk.frame.cmd_buf;
    if (_sg.vk.uniforms.dirty && !_sg_vk_bind_uniform_descriptor_set(cmd)) return;
    vkCmdDispatchIndirect(cmd, buf->vk.buf, (VkDeviceSize)offset);
    #elif defined(SOKOL_D3D11)
    #if defined(__cplusplus)
    _sg.d3d11.ctx->DispatchIndirect(buf->d3d11.buf, (UINT)offset);
    #else
    _sg.d3d11.ctx->lpVtbl->DispatchIndirect(_sg.d3d11.ctx, buf->d3d11.buf, (UINT)offset);
    #endif
    #elif defined(SOKOL_WGPU)
    if (_sg.wgpu.uniform.dirty) _sg_wgpu_uniform_system_set_bindgroup();
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(_sg.wgpu.cpass_enc, buf->wgpu.buf, (uint64_t)offset);
    #elif defined(_SOKOL_GL_HAS_COMPUTE)
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, buf->gl.buf[buf->cmn.active_slot]);
    _sgu_dispatch_indirect((GLintptr)offset);
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    #endif
}

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
    if (!_sgu_gpu_pass_timing_state.enabled || !pass || pass->compute || !descriptor) return;
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
    if (!_sgu_gpu_pass_timing_state.enabled || !pass || !pass->compute || !descriptor) return;
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

/*--- GPU Pass Timings: Vulkan ---------------------------------------------*/
#if defined(SOKOL_VULKAN)

typedef struct {
    VkQueryPool pool;
    VkFence fence;
    uint64_t frame_index;
    uint32_t pass_count;
    int active_pass;
    bool pending;
    bool recording;
    char labels[SG_GPU_PASS_TIMING_MAX_PASSES][SG_GPU_PASS_TIMING_LABEL_SIZE];
} _sgu_vk_gpu_timing_frame_t;

static struct {
    VkDevice device;
    double milliseconds_per_tick;
    uint64_t timestamp_mask;
    int current_slot;
    _sgu_vk_gpu_timing_frame_t frames[SG_NUM_INFLIGHT_FRAMES];
} _sgu_vk_gpu_timing;

_SOKOL_PRIVATE void _sgu_vk_gpu_timing_shutdown(void) {
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; ++i) {
        if (_sgu_vk_gpu_timing.frames[i].pool) {
            vkDestroyQueryPool(_sgu_vk_gpu_timing.device, _sgu_vk_gpu_timing.frames[i].pool, NULL);
        }
    }
    memset(&_sgu_vk_gpu_timing, 0, sizeof(_sgu_vk_gpu_timing));
    _sgu_vk_gpu_timing.current_slot = -1;
}

_SOKOL_PRIVATE bool _sgu_vk_gpu_timing_initialize(void) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(_sg.vk.phys_dev, &count, NULL);
    if (_sg.vk.queue_family_index >= count) return false;
    VkQueueFamilyProperties* queues = (VkQueueFamilyProperties*)malloc(sizeof(*queues) * count);
    if (!queues) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(_sg.vk.phys_dev, &count, queues);
    const uint32_t bits = queues[_sg.vk.queue_family_index].timestampValidBits;
    free(queues);
    if (!bits) return false;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(_sg.vk.phys_dev, &properties);
    if (!(properties.limits.timestampPeriod > 0)) return false;
    _sgu_vk_gpu_timing.device = _sg.vk.dev;
    _sgu_vk_gpu_timing.milliseconds_per_tick = properties.limits.timestampPeriod * 0.000001;
    _sgu_vk_gpu_timing.timestamp_mask = bits >= 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    _sgu_vk_gpu_timing.current_slot = -1;
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; ++i) {
        VkQueryPoolCreateInfo desc;
        memset(&desc, 0, sizeof(desc));
        desc.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        desc.queryType = VK_QUERY_TYPE_TIMESTAMP;
        desc.queryCount = 2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2;
        if (vkCreateQueryPool(_sg.vk.dev, &desc, NULL, &_sgu_vk_gpu_timing.frames[i].pool) != VK_SUCCESS) {
            _sgu_vk_gpu_timing_shutdown();
            return false;
        }
        _sgu_vk_gpu_timing.frames[i].active_pass = -1;
    }
    return true;
}

_SOKOL_PRIVATE void _sgu_vk_gpu_timing_begin_commands(VkCommandBuffer commands) {
    if (!_sgu_gpu_pass_timing_state.enabled || commands != _sg.vk.frame.cmd_buf) return;
    const int slot = (int)_sg.vk.frame_slot;
    _sgu_vk_gpu_timing_frame_t* frame = &_sgu_vk_gpu_timing.frames[slot];
    // Sokol has already waited for this frame slot before beginning its command
    // buffer. Drop unconsumed readings; the pool is now safe to reset and reuse.
    frame->pending = false;
    frame->fence = _sg.vk.frame.slot[slot].fence;
    frame->recording = true;
    frame->frame_index = _sg.frame_index;
    frame->pass_count = 0;
    frame->active_pass = -1;
    _sgu_vk_gpu_timing.current_slot = slot;
    vkCmdResetQueryPool(commands, frame->pool, 0, 2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame->pool, 0);
}

_SOKOL_PRIVATE void _sgu_vk_gpu_timing_end_commands(VkCommandBuffer commands) {
    const int slot = _sgu_vk_gpu_timing.current_slot;
    if (slot < 0 || commands != _sg.vk.frame.cmd_buf) return;
    _sgu_vk_gpu_timing_frame_t* frame = &_sgu_vk_gpu_timing.frames[slot];
    if (!frame->recording) return;
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->pool, 1);
    frame->recording = false;
    frame->pending = true;
    _sgu_vk_gpu_timing.current_slot = -1;
}

_SOKOL_PRIVATE void _sgu_vk_gpu_timing_begin_pass(const sg_pass* pass) {
    if (!_sgu_gpu_pass_timing_state.enabled) return;
    // This is the same acquisition the backend makes before rendering. The
    // adapter starts frame timing after vkBeginCommandBuffer succeeds.
    _sg_vk_acquire_frame_command_buffers();
    const int slot = _sgu_vk_gpu_timing.current_slot;
    if (slot < 0 || !_sg.vk.frame.cmd_buf) return;
    _sgu_vk_gpu_timing_frame_t* frame = &_sgu_vk_gpu_timing.frames[slot];
    frame->active_pass = -1;
    if (frame->pass_count >= SG_GPU_PASS_TIMING_MAX_PASSES) return;
    const uint32_t index = frame->pass_count++;
    const char* label = pass->label ? pass->label : (pass->compute ? "compute" : "render");
    strncpy(frame->labels[index], label, SG_GPU_PASS_TIMING_LABEL_SIZE - 1);
    frame->labels[index][SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
    frame->active_pass = (int)index;
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame->pool, 2 + index * 2);
}

_SOKOL_PRIVATE void _sgu_vk_gpu_timing_end_pass(void) {
    const int slot = _sgu_vk_gpu_timing.current_slot;
    if (slot < 0) return;
    _sgu_vk_gpu_timing_frame_t* frame = &_sgu_vk_gpu_timing.frames[slot];
    if (frame->active_pass < 0) return;
    vkCmdWriteTimestamp(_sg.vk.frame.cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        frame->pool, 3 + (uint32_t)frame->active_pass * 2);
    frame->active_pass = -1;
}

_SOKOL_PRIVATE int _sgu_vk_gpu_timing_poll(sg_gpu_pass_timing* timings, int max_timings) {
    _sgu_vk_gpu_timing_frame_t* oldest = NULL;
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; ++i) {
        _sgu_vk_gpu_timing_frame_t* frame = &_sgu_vk_gpu_timing.frames[i];
        if (frame->pending && (!oldest || frame->frame_index < oldest->frame_index)) oldest = frame;
    }
    if (!oldest) return 0;
    // A GPU-side reset may not have executed yet, leaving the previous use's
    // availability bits set. Check this submission's fence before reading them.
    VkResult completion = vkGetFenceStatus(_sgu_vk_gpu_timing.device, oldest->fence);
    if (completion == VK_NOT_READY) return 0;
    if (completion != VK_SUCCESS) { oldest->pending = false; return 0; }
    // WITH_AVAILABILITY never waits. No query pool is freed while work is in
    // flight; pools live until shutdown and follow Sokol's frame-slot fences.
    uint64_t results[2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2][2];
    const uint32_t queries = 2 + oldest->pass_count * 2;
    VkResult status = vkGetQueryPoolResults(_sgu_vk_gpu_timing.device, oldest->pool, 0, queries,
        queries * sizeof(results[0]), results, sizeof(results[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (status == VK_NOT_READY) return 0;
    if (status != VK_SUCCESS) { oldest->pending = false; return 0; }
    for (uint32_t i = 0; i < queries; ++i) if (!results[i][1]) return 0;
    int count = 0;
    for (uint32_t i = 0; i <= oldest->pass_count && count < max_timings; ++i) {
        const uint32_t start = i * 2;
        sg_gpu_pass_timing* timing = &timings[count++];
        const char* label = i == 0 ? "Frame" : oldest->labels[i - 1];
        strncpy(timing->label, label, SG_GPU_PASS_TIMING_LABEL_SIZE - 1);
        timing->label[SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
        timing->frame_index = oldest->frame_index;
        const uint64_t ticks = (results[start + 1][0] - results[start][0]) & _sgu_vk_gpu_timing.timestamp_mask;
        timing->milliseconds = (double)ticks * _sgu_vk_gpu_timing.milliseconds_per_tick;
    }
    oldest->pending = false;
    return count;
}
#endif /* SOKOL_VULKAN */

/*--- GPU Pass Timings: D3D11 ----------------------------------------------*/
#if defined(SOKOL_D3D11)
#define _SGU_D3D11_GPU_TIMING_FRAMES (4)
typedef struct {
    ID3D11Query* disjoint;
    ID3D11Query* queries[2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2];
    uint64_t frame_index;
    uint32_t pass_count;
    int active_pass;
    bool pending;
    char labels[SG_GPU_PASS_TIMING_MAX_PASSES][SG_GPU_PASS_TIMING_LABEL_SIZE];
} _sgu_d3d11_gpu_timing_frame_t;
static struct {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    int current_slot;
    _sgu_d3d11_gpu_timing_frame_t frames[_SGU_D3D11_GPU_TIMING_FRAMES];
} _sgu_d3d11_gpu_timing;

_SOKOL_PRIVATE bool _sgu_d3d11_gpu_timing_make_query(ID3D11Query** query, D3D11_QUERY type) {
    if (*query) return true;
    D3D11_QUERY_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Query = type;
    return SUCCEEDED(_sgu_d3d11_gpu_timing.device->CreateQuery(&desc, query));
}

_SOKOL_PRIVATE void _sgu_d3d11_gpu_timing_shutdown(void) {
    for (int i = 0; i < _SGU_D3D11_GPU_TIMING_FRAMES; ++i) {
        _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[i];
        if (frame->disjoint) frame->disjoint->Release();
        for (int j = 0; j < 2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2; ++j) {
            if (frame->queries[j]) frame->queries[j]->Release();
        }
    }
    if (_sgu_d3d11_gpu_timing.context) _sgu_d3d11_gpu_timing.context->Release();
    if (_sgu_d3d11_gpu_timing.device) _sgu_d3d11_gpu_timing.device->Release();
    memset(&_sgu_d3d11_gpu_timing, 0, sizeof(_sgu_d3d11_gpu_timing));
    _sgu_d3d11_gpu_timing.current_slot = -1;
}

_SOKOL_PRIVATE bool _sgu_d3d11_gpu_timing_initialize(void) {
    _sgu_d3d11_gpu_timing.device = _sg.d3d11.dev;
    _sgu_d3d11_gpu_timing.context = _sg.d3d11.ctx;
    if (!_sgu_d3d11_gpu_timing.device || !_sgu_d3d11_gpu_timing.context) {
        _sgu_d3d11_gpu_timing.device = NULL;
        _sgu_d3d11_gpu_timing.context = NULL;
        return false;
    }
    _sgu_d3d11_gpu_timing.device->AddRef();
    _sgu_d3d11_gpu_timing.context->AddRef();
    _sgu_d3d11_gpu_timing.current_slot = -1;
    for (int i = 0; i < _SGU_D3D11_GPU_TIMING_FRAMES; ++i) {
        _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[i];
        frame->active_pass = -1;
        if (!_sgu_d3d11_gpu_timing_make_query(&frame->disjoint, D3D11_QUERY_TIMESTAMP_DISJOINT) ||
            !_sgu_d3d11_gpu_timing_make_query(&frame->queries[0], D3D11_QUERY_TIMESTAMP) ||
            !_sgu_d3d11_gpu_timing_make_query(&frame->queries[1], D3D11_QUERY_TIMESTAMP)) {
            _sgu_d3d11_gpu_timing_shutdown();
            return false;
        }
    }
    return true;
}

_SOKOL_PRIVATE void _sgu_d3d11_gpu_timing_begin_pass(const sg_pass* pass) {
    if (!_sgu_gpu_pass_timing_state.enabled) return;
    int slot = _sgu_d3d11_gpu_timing.current_slot;
    ID3D11DeviceContext* context = _sgu_d3d11_gpu_timing.context;
    if (slot < 0) {
        for (int i = 0; i < _SGU_D3D11_GPU_TIMING_FRAMES; ++i) {
            if (!_sgu_d3d11_gpu_timing.frames[i].pending) { slot = i; break; }
        }
        // Keep unfinished queries intact; skip recording if the GPU is behind.
        if (slot < 0) return;
        _sgu_d3d11_gpu_timing.current_slot = slot;
        _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[slot];
        frame->frame_index = _sg.frame_index;
        frame->pass_count = 0;
        context->Begin(frame->disjoint);
        context->End(frame->queries[0]);
    }
    _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[slot];
    frame->active_pass = -1;
    if (frame->pass_count >= SG_GPU_PASS_TIMING_MAX_PASSES) return;
    const uint32_t index = frame->pass_count;
    if (!_sgu_d3d11_gpu_timing_make_query(&frame->queries[2 + index * 2], D3D11_QUERY_TIMESTAMP) ||
        !_sgu_d3d11_gpu_timing_make_query(&frame->queries[3 + index * 2], D3D11_QUERY_TIMESTAMP)) return;
    const char* label = pass->label ? pass->label : (pass->compute ? "compute" : "render");
    strncpy(frame->labels[index], label, SG_GPU_PASS_TIMING_LABEL_SIZE - 1);
    frame->labels[index][SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
    frame->active_pass = (int)index;
    ++frame->pass_count;
    context->End(frame->queries[2 + index * 2]);
}

_SOKOL_PRIVATE void _sgu_d3d11_gpu_timing_end_pass(void) {
    const int slot = _sgu_d3d11_gpu_timing.current_slot;
    if (slot < 0) return;
    _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[slot];
    if (frame->active_pass < 0) return;
    _sgu_d3d11_gpu_timing.context->End(frame->queries[3 + (uint32_t)frame->active_pass * 2]);
    frame->active_pass = -1;
}

_SOKOL_PRIVATE void _sgu_d3d11_gpu_timing_commit(void) {
    const int slot = _sgu_d3d11_gpu_timing.current_slot;
    if (slot < 0) return;
    _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[slot];
    _sgu_d3d11_gpu_timing.context->End(frame->queries[1]);
    _sgu_d3d11_gpu_timing.context->End(frame->disjoint);
    frame->pending = true;
    _sgu_d3d11_gpu_timing.current_slot = -1;
}

_SOKOL_PRIVATE int _sgu_d3d11_gpu_timing_poll(sg_gpu_pass_timing* timings, int max_timings) {
    _sgu_d3d11_gpu_timing_frame_t* oldest = NULL;
    for (int i = 0; i < _SGU_D3D11_GPU_TIMING_FRAMES; ++i) {
        _sgu_d3d11_gpu_timing_frame_t* frame = &_sgu_d3d11_gpu_timing.frames[i];
        if (frame->pending && (!oldest || frame->frame_index < oldest->frame_index)) oldest = frame;
    }
    if (!oldest) return 0;
    ID3D11DeviceContext* context = _sgu_d3d11_gpu_timing.context;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
    HRESULT status = context->GetData(oldest->disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (status == S_FALSE) return 0;
    if (FAILED(status) || disjoint.Disjoint || !disjoint.Frequency) { oldest->pending = false; return 0; }
    uint64_t results[2 + SG_GPU_PASS_TIMING_MAX_PASSES * 2];
    const uint32_t queries = 2 + oldest->pass_count * 2;
    for (uint32_t i = 0; i < queries; ++i) {
        status = context->GetData(oldest->queries[i], &results[i], sizeof(results[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (status == S_FALSE) return 0;
        if (FAILED(status)) { oldest->pending = false; return 0; }
    }
    int count = 0;
    for (uint32_t i = 0; i <= oldest->pass_count && count < max_timings; ++i) {
        sg_gpu_pass_timing* timing = &timings[count++];
        const char* label = i == 0 ? "Frame" : oldest->labels[i - 1];
        strncpy(timing->label, label, SG_GPU_PASS_TIMING_LABEL_SIZE - 1);
        timing->label[SG_GPU_PASS_TIMING_LABEL_SIZE - 1] = 0;
        timing->frame_index = oldest->frame_index;
        timing->milliseconds = (double)(results[i * 2 + 1] - results[i * 2]) * 1000.0 / (double)disjoint.Frequency;
    }
    oldest->pending = false;
    return count;
}
#endif /* SOKOL_D3D11 */

/*--- GPU Pass Timings: Trace Hooks -----------------------------------------*/
_SOKOL_PRIVATE void _sgu_gpu_timing_trace_begin_pass(const sg_pass* pass, void* user_data) {
    _sgu_gpu_pass_timing_state_t* state = (_sgu_gpu_pass_timing_state_t*)user_data;
#if defined(SOKOL_VULKAN)
    _sgu_vk_gpu_timing_begin_pass(pass);
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_gpu_timing_begin_pass(pass);
#endif
    if (state->previous_hooks.begin_pass) {
        state->previous_hooks.begin_pass(pass, state->previous_hooks.user_data);
    }
}

_SOKOL_PRIVATE void _sgu_gpu_timing_trace_end_pass(void* user_data) {
    _sgu_gpu_pass_timing_state_t* state = (_sgu_gpu_pass_timing_state_t*)user_data;
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_end_pass();
#elif defined(SOKOL_VULKAN)
    _sgu_vk_gpu_timing_end_pass();
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_gpu_timing_end_pass();
#endif
    if (state->previous_hooks.end_pass) {
        state->previous_hooks.end_pass(state->previous_hooks.user_data);
    }
}

_SOKOL_PRIVATE void _sgu_gpu_timing_trace_commit(void* user_data) {
    _sgu_gpu_pass_timing_state_t* state = (_sgu_gpu_pass_timing_state_t*)user_data;
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_commit();
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_gpu_timing_commit();
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
    if (!sg_isvalid()) return false;
#if !defined(SOKOL_TRACE_HOOKS)
    return false;
#endif
    if (_sgu_gpu_pass_timing_state.setup) {
        return _sgu_gpu_pass_timing_state.supported;
    }
    _sgu_gpu_pass_timing_state.setup = true;
#if defined(SOKOL_METAL)
    _sgu_gpu_pass_timing_state.supported = _sgu_mtl_gpu_timing_initialize();
#elif defined(SOKOL_VULKAN)
    _sgu_gpu_pass_timing_state.supported = _sgu_vk_gpu_timing_initialize();
#elif defined(SOKOL_D3D11)
    _sgu_gpu_pass_timing_state.supported = _sgu_d3d11_gpu_timing_initialize();
#else
    _sgu_gpu_pass_timing_state.supported = false;
#endif
    if (!_sgu_gpu_pass_timing_state.supported) {
        return false;
    }
    sg_trace_hooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.user_data = &_sgu_gpu_pass_timing_state;
    hooks.begin_pass = _sgu_gpu_timing_trace_begin_pass;
    hooks.end_pass = _sgu_gpu_timing_trace_end_pass;
    hooks.commit = _sgu_gpu_timing_trace_commit;
    _sgu_gpu_pass_timing_state.previous_hooks = sg_install_trace_hooks(&hooks);
    return true;
}

SOKOL_API_IMPL void sg_gpu_pass_timings_shutdown(void) {
#if defined(SOKOL_METAL)
    _sgu_mtl_gpu_timing_shutdown();
#elif defined(SOKOL_VULKAN)
    _sgu_vk_gpu_timing_shutdown();
#elif defined(SOKOL_D3D11)
    _sgu_d3d11_gpu_timing_shutdown();
#endif
    memset(&_sgu_gpu_pass_timing_state, 0, sizeof(_sgu_gpu_pass_timing_state));
}

SOKOL_API_IMPL void sg_gpu_pass_timings_set_enabled(bool enabled) {
    _sgu_gpu_pass_timing_state.enabled =
        _sgu_gpu_pass_timing_state.setup && _sgu_gpu_pass_timing_state.supported && enabled;
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
#elif defined(SOKOL_VULKAN)
    return _sgu_vk_gpu_timing_poll(timings, max_timings);
#elif defined(SOKOL_D3D11)
    return _sgu_d3d11_gpu_timing_poll(timings, max_timings);
#else
    return 0;
#endif
}

#endif /* SOKOL_GFX_UTILS_IMPL */
