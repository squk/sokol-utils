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
    /* Pass start and end in nanoseconds on the clock that
       sg_gpu_pass_timings_clock_ns() reads. Zero when the backend cannot place
       the pass on that clock, and always zero for the "Frame" entry.
       end_ns - begin_ns equals milliseconds, so a pass whose vertex and
       fragment stages run apart still reports only its own work. */
    uint64_t begin_ns;
    uint64_t end_ns;
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

/* Reads the clock behind sg_gpu_pass_timing.begin_ns and end_ns, in
   nanoseconds. Returns 0 when the backend does not support it. Callers pair
   one reading with their own profiler clock to place pass timings on it. */
SOKOL_GFX_UTILS_API_DECL uint64_t sg_gpu_pass_timings_clock_ns(void);

/* Native receipts for sg_commit_tracked. These APIs run on the graphics
   context's owning thread, outside passes. A receipt covers preceding work
   submitted by this tracked commit on Sokol's queue/context; it does not prove
   rendered output validity or completion on other queues.

   A null receipt, expired device generation or unavailable tracking reports
   UNKNOWN. Destroying a pending receipt never waits: internal ownership lasts
   until its marker finishes or explicit shutdown. Do not query a destroyed
   receipt. Call sg_shutdown_submission_receipts before sg_shutdown or switching
   graphics backends; surviving public receipts then report UNKNOWN and may be
   destroyed through a later backend dispatcher. */
typedef struct sg_submission_receipt_t* sg_submission_receipt;
typedef enum sg_submission_status {
    SG_SUBMISSION_UNKNOWN,
    SG_SUBMISSION_PENDING,
    SG_SUBMISSION_COMPLETE,
    SG_SUBMISSION_FAILED
} sg_submission_status;
SOKOL_GFX_UTILS_API_DECL bool sg_submission_completion_supported(void);
/* Commits even when optional receipt allocation/marker creation is unavailable.
   Polls issue no waits. Explicit shutdown may wait on Vulkan. */
SOKOL_GFX_UTILS_API_DECL sg_submission_receipt sg_commit_tracked(void);
SOKOL_GFX_UTILS_API_DECL sg_submission_status sg_query_submission_receipt(sg_submission_receipt receipt);
SOKOL_GFX_UTILS_API_DECL void sg_destroy_submission_receipt(sg_submission_receipt receipt);
/* False leaves Vulkan receipts intact: retry before graphics teardown. */
SOKOL_GFX_UTILS_API_DECL bool sg_shutdown_submission_receipts(void);

/*
    Acceleration structures and ray queries for compute shaders.

    A bottom-level structure (BLAS) holds one triangle mesh. A top-level
    structure (TLAS) holds instances of BLASes, each with a transform, an id
    and a mask. A compute shader traces rays against a TLAS with ray queries.
    Vulkan uses VK_KHR_acceleration_structure and VK_KHR_ray_query. Metal uses
    MTLAccelerationStructure. D3D11, WebGPU and GL report no support. Sokol has
    no D3D12 backend: on Windows, ray queries go through Vulkan.

    Contract:
    - Call from the thread that owns the sokol_gfx context. Calls that record
      GPU work (build, refit) run outside passes, in the frame's command order.
      A build must be recorded before the compute pass that reads it. Vulkan
      builds end with a barrier for later passes. Metal orders encoders.
    - Vertex and index data come from sg_buffer objects made with
      sg_make_accel_input_buffer. A TLAS takes its instances from a CPU array
      at each build. A BLAS stores the sg_buffer ids and reads the buffers
      again at every build or refit. Do not destroy those buffers while a build
      that reads them can still be recorded.
    - sg_make_blas reads the buffers to size the structure, so both must be
      valid (initialized) when it runs. Index values are not range checked:
      an index at or above vertex_count reads past the vertex data on the GPU.
    - The caller owns each sg_accel and frees it with sg_destroy_accel. A TLAS
      holds a reference to each BLAS in its desc. A BLAS destroyed earlier
      lives until its last TLAS is destroyed. Destruction is deferred until the
      GPU finished the frames that can use the structure. Destroy every
      sg_accel before sg_accel_shutdown: afterwards sg_destroy_accel does
      nothing.
    - sg_make_blas and sg_make_tlas allocate memory but record no commands.
      The structure is empty until built. Both return NULL when the structure
      exceeds a device limit (Vulkan: maxPrimitiveCount, maxInstanceCount).
    - A TLAS takes one build or refit per frame.
    - Refit keeps topology and updates positions or transforms in place. It
      needs allow_update. After a BLAS changes, refit or rebuild each TLAS
      that uses it.
    - Builds share one scratch buffer per frame in flight. It only grows.
    - Compaction is not supported.
    - Call sg_accel_shutdown after sg_shutdown, while the native device exists.

    Vulkan device setup. sokol_gfx does not create the VkDevice, the
    application does. It must enable, in addition to what sokol_gfx needs:
      extensions: VK_KHR_acceleration_structure, VK_KHR_ray_query,
                  VK_KHR_deferred_host_operations
      features:   VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure
                  VkPhysicalDeviceRayQueryFeaturesKHR::rayQuery
                  bufferDeviceAddress (sokol_gfx already requires it)
    sg_ray_query_supported checks that the GPU lists the three extensions,
    offers the two features, and that the entry points load. It cannot see what
    the application enabled: an application that enables only what the GPU
    offers makes "offered" and "enabled" the same. A device created without
    them while the GPU offers them is a caller error.

    Binding. sokol_gfx has no acceleration structure bind slot type. sokol-slang
    reflects acceleration structures of compute shaders as a separate slot
    namespace 0..SG_ACCEL_MAX_SLOTS-1 (ShaderReflection acceleration_structure_slot)
    and writes the native index of each slot per shader (msl_buffer_n and
    spirv_set1_binding_n of its reflection data). The runtime assumes no index:
    pass those numbers in an sg_accel_layout_desc to sg_make_shader_accel, which
    records them per shader until the shader is destroyed. sg_accel_bind uses
    the recorded numbers.
    sg_make_shader_accel logs and returns an invalid shader when:
    - a slot is repeated or not below SG_ACCEL_MAX_SLOTS, or num_slots is out of
      range;
    - Metal: an msl_buffer_n is below SG_MAX_UNIFORMBLOCK_BINDSLOTS (sokol_gfx
      keeps the uniform buffer bound there for the whole pass), above 30,
      repeated, or equal to the msl_buffer_n of a uniform block or storage
      buffer of the shader;
    - Vulkan: a vk_binding_n is negative, repeated, or equal to the binding of a
      view or sampler of the shader. It must also be at least
      SG_MAX_VIEW_BINDSLOTS + SG_MAX_SAMPLER_BINDSLOTS of the sokol_gfx.h that
      this header is built with. A smaller number means the shader compiler
      and the runtime were built against different sokol_gfx.h versions; the
      error names both numbers.
    This header targets the sokol_gfx.h that goBot vendors (it reads
    _sg_skipdraw_any), not older sokol versions.
    - Metal: sg_accel_bind(tlas, slot) sets the structure at the recorded
      [[buffer(n)]] on the compute encoder and marks the TLAS and its BLASes
      resident. Metal keeps no binding across encoders: call it in every
      compute pass that traces.
    - Vulkan: sokol_gfx builds the set 1 layout without that binding, so the
      shader must come from sg_make_shader_accel. It adds the recorded
      bindings to the layout.
      sg_accel_bind writes the TLAS descriptor into a descriptor buffer chunk:
      * For a shader without views and samplers it allocates and binds its own
        chunk, so no sg_apply_bindings is needed.
      * Otherwise it writes into the chunk that the last sg_apply_bindings
        recorded, which it finds as the last allocation in sokol's descriptor
        buffer. No sg_dispatch or sg_apply_uniforms may sit between
        sg_apply_bindings and sg_accel_bind, and a failed sg_apply_bindings
        makes sg_accel_bind return false.
    Call sg_accel_bind in a compute pass after sg_apply_pipeline and
    sg_apply_bindings, and on Vulkan again after every later sg_apply_bindings:
    that call starts a new descriptor chunk without the TLAS.
*/
typedef struct sg_accel_t* sg_accel;

typedef enum sg_accel_index_format {
    SG_ACCEL_INDEXFORMAT_NONE,      /* triangle list from vertices, vertex_count % 3 == 0 */
    SG_ACCEL_INDEXFORMAT_UINT16,
    SG_ACCEL_INDEXFORMAT_UINT32
} sg_accel_index_format;

/* Vertices are float3 positions. Offsets and strides are in bytes. Vertex
   offset and stride are multiples of four. The index offset is a multiple of
   the index size (2 or 4). The buffers must come from
   sg_make_accel_input_buffer and be valid when sg_make_blas runs. */
typedef struct sg_blas_desc {
    sg_buffer vertex_buffer;
    int vertex_offset;
    int vertex_stride;      /* at least 12 */
    int vertex_count;
    sg_buffer index_buffer; /* unused when index_format is NONE */
    int index_offset;
    sg_accel_index_format index_format;
    int index_count;        /* multiple of 3 */
    bool opaque;            /* skip any-hit tests for every triangle */
    bool allow_update;      /* permit sg_refit_blas */
    const char* label;
} sg_blas_desc;

/* The TLAS holds up to max_instances instances of the listed BLASes. */
typedef struct sg_tlas_desc {
    const sg_accel* blas;
    int blas_count;
    int max_instances;
    bool allow_update;      /* permit sg_refit_tlas */
    const char* label;
} sg_tlas_desc;

#define SG_ACCEL_INSTANCE_OPAQUE (1)    /* treat all geometry of the instance as opaque */
#define SG_ACCEL_INSTANCE_NO_CULL (2)   /* disable triangle facing culling */

typedef struct sg_accel_instance {
    float transform[12];    /* row-major 3x4 object-to-world matrix */
    uint32_t id;            /* low 24 bits only */
    uint8_t mask;           /* ray mask, 8 bits. Zero never matches a ray: set 0xFF to hit always */
    uint8_t flags;          /* SG_ACCEL_INSTANCE_* */
    int blas_index;         /* index into sg_tlas_desc.blas */
} sg_accel_instance;

/* Vulkan: true when the GPU lists and offers what the contract above names.
   The application must have enabled it. */
SOKOL_GFX_UTILS_API_DECL bool sg_ray_query_supported(void);
/* Like sg_make_indirect_buffer: needs usage.storage_buffer, returns an invalid
   buffer when ray queries are unsupported. On Vulkan the buffer also gets
   build-input usage. The caller destroys it with sg_destroy_buffer. */
SOKOL_GFX_UTILS_API_DECL sg_buffer sg_make_accel_input_buffer(const sg_buffer_desc* desc);
/* Return NULL on failure. */
SOKOL_GFX_UTILS_API_DECL sg_accel sg_make_blas(const sg_blas_desc* desc);
SOKOL_GFX_UTILS_API_DECL sg_accel sg_make_tlas(const sg_tlas_desc* desc);
SOKOL_GFX_UTILS_API_DECL void sg_destroy_accel(sg_accel accel);
/* Record a build or refit. Return false when nothing was recorded. A TLAS
   build needs every referenced BLAS built. Refit of a TLAS keeps the instance
   count of its last build. */
SOKOL_GFX_UTILS_API_DECL bool sg_build_blas(sg_accel blas);
SOKOL_GFX_UTILS_API_DECL bool sg_refit_blas(sg_accel blas);
SOKOL_GFX_UTILS_API_DECL bool sg_build_tlas(sg_accel tlas, const sg_accel_instance* instances, int count);
SOKOL_GFX_UTILS_API_DECL bool sg_refit_tlas(sg_accel tlas, const sg_accel_instance* instances, int count);
SOKOL_GFX_UTILS_API_DECL bool sg_accel_built(sg_accel accel);
#define SG_ACCEL_MAX_SLOTS (4)

/* Where one acceleration structure slot sits in a shader. The shader compiler
   decides these numbers and writes them into its reflection data. Pass them
   unchanged. Set the field of a backend the shader does not target to -1. */
typedef struct sg_accel_slot_layout {
    uint8_t slot;       /* acceleration structure slot index, < SG_ACCEL_MAX_SLOTS */
    int msl_buffer_n;   /* Metal: [[buffer(n)]] index the shader reads the structure from; -1 when unused */
    int vk_binding_n;   /* Vulkan: binding number in descriptor set 1; -1 when unused */
} sg_accel_slot_layout;

typedef struct sg_accel_layout_desc {
    int num_slots;      /* 0..SG_ACCEL_MAX_SLOTS */
    sg_accel_slot_layout slots[SG_ACCEL_MAX_SLOTS];
} sg_accel_layout_desc;

/* Like sg_make_shader. layout lists the acceleration structure slots the
   compute shader uses (NULL means none). On failure it logs the cause through
   the sokol logger and returns an invalid shader. See the Binding contract
   above for the checks. The layout is copied. */
SOKOL_GFX_UTILS_API_DECL sg_shader sg_make_shader_accel(const sg_shader_desc* desc, const sg_accel_layout_desc* layout);
/* slot is 0..SG_ACCEL_MAX_SLOTS-1. Returns false outside a compute pass, when
   the TLAS is not built, when the shader was not made for this slot with
   sg_make_shader_accel, or (Vulkan) when the last sg_apply_bindings failed. */
SOKOL_GFX_UTILS_API_DECL bool sg_accel_bind(sg_accel tlas, int slot);
SOKOL_GFX_UTILS_API_DECL void sg_accel_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SOKOL_GFX_UTILS_INCLUDED */

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef SOKOL_GFX_UTILS_IMPL
#define SOKOL_GFX_UTILS_IMPL_INCLUDED (1)

#include <stdlib.h> // malloc, free
#include <string.h> // memcpy
#include <stdarg.h> // va_list, for the accel layout error messages
#include <stdio.h>  // vsnprintf

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
/* Creates a storage buffer with extra Vulkan usage bits. Used for indirect
   argument buffers and for acceleration structure build inputs. */
_SOKOL_PRIVATE sg_resource_state _sgu_vk_create_indirect_buffer(_sg_buffer_t* buf, const sg_buffer_desc* desc, VkBufferUsageFlags extra_usage) {
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
    create_info.usage = _sg_vk_buffer_usage(&buf->cmn.usage) | extra_usage;
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
    buf->slot.state = _sgu_vk_create_indirect_buffer(buf, &d, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
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
        _sg.cur_pass.is_compute != compute || _sg_skipdraw_any() || offset < 0 || (offset & 3)) return 0;
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
    const double nanoseconds_per_tick = milliseconds_per_tick * 1000000.0;
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
        // Start at the earliest stage start, on the CPU clock of sampleTimestamps.
        // End after the summed stage time, not at the last stage end.
        uint64_t first_start = starts[0];
        if (first_start == 0 || (starts[1] != 0 && starts[1] < first_start)) first_start = starts[1];
        const double offset_ns = ((double)first_start - (double)oldest->gpu_begin) * nanoseconds_per_tick;
        const double begin_ns = (double)oldest->cpu_begin + offset_ns;
        if (begin_ns > 0.0) {
            timing->begin_ns = (uint64_t)begin_ns;
            timing->end_ns = (uint64_t)(begin_ns + (double)duration * nanoseconds_per_tick);
        }
    }
    _sgu_mtl_gpu_timing_release_frame(oldest);
    return result_count;
}

_SOKOL_PRIVATE uint64_t _sgu_mtl_gpu_timing_clock_ns(void) {
    if (!_sg.mtl.device) return 0;
    MTLTimestamp cpu = 0, gpu = 0;
    [_sg.mtl.device sampleTimestamps:&cpu gpuTimestamp:&gpu];
    return (uint64_t)cpu;
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
    memset(timings, 0, sizeof(sg_gpu_pass_timing) * (size_t)max_timings);
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

SOKOL_API_IMPL uint64_t sg_gpu_pass_timings_clock_ns(void) {
    if (!_sgu_gpu_pass_timing_state.supported) {
        return 0;
    }
#if defined(SOKOL_METAL)
    return _sgu_mtl_gpu_timing_clock_ns();
#else
    return 0;
#endif
}

#if defined(SOKOL_METAL)
typedef struct {
    // The receipt owns one retain; calloc stores no ARC-managed fields.
    void* command_buffer;
} _sgu_mtl_completion_t;

_SOKOL_PRIVATE bool _sgu_mtl_completion_make(_sgu_mtl_completion_t* marker) {
    memset(marker, 0, sizeof(*marker));
    if (!_sg.mtl.cmd_buffer) return false;
#if __has_feature(objc_arc)
    marker->command_buffer = (__bridge_retained void*)_sg.mtl.cmd_buffer;
#else
    marker->command_buffer = (void*)[_sg.mtl.cmd_buffer retain];
#endif
    return true;
}
_SOKOL_PRIVATE sg_submission_status _sgu_mtl_completion_poll(const _sgu_mtl_completion_t* marker) {
    if (!marker || !marker->command_buffer) return SG_SUBMISSION_UNKNOWN;
#if __has_feature(objc_arc)
    id<MTLCommandBuffer> command_buffer = (__bridge id<MTLCommandBuffer>)marker->command_buffer;
#else
    id<MTLCommandBuffer> command_buffer = (id<MTLCommandBuffer>)marker->command_buffer;
#endif
    if (command_buffer.status == MTLCommandBufferStatusCompleted) return SG_SUBMISSION_COMPLETE;
    if (command_buffer.status == MTLCommandBufferStatusError) return SG_SUBMISSION_FAILED;
    return SG_SUBMISSION_PENDING;
}
_SOKOL_PRIVATE void _sgu_mtl_completion_destroy(_sgu_mtl_completion_t* marker) {
    if (!marker || !marker->command_buffer) return;
#if __has_feature(objc_arc)
    CFRelease((CFTypeRef)marker->command_buffer);
#else
    [(id<MTLCommandBuffer>)marker->command_buffer release];
#endif
    marker->command_buffer = NULL;
}
#endif

#if defined(SOKOL_VULKAN)
typedef struct {
    VkDevice device;
    VkFence fence;
    sg_submission_status status;
} _sgu_vk_completion_t;

_SOKOL_PRIVATE bool _sgu_vk_completion_make(_sgu_vk_completion_t* marker) {
    VkFenceCreateInfo info;
    VkSubmitInfo submit;
    VkResult result;
    memset(marker, 0, sizeof(*marker));
    if (!_sg.vk.valid || !_sg.vk.dev || !_sg.vk.queue) return false;
    marker->device = _sg.vk.dev;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(marker->device, &info, NULL, &marker->fence);
    if (result != VK_SUCCESS) {
        marker->status = result == VK_ERROR_DEVICE_LOST ? SG_SUBMISSION_FAILED : SG_SUBMISSION_UNKNOWN;
        return marker->status == SG_SUBMISSION_FAILED;
    }
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    /* A separate fence after sg_commit covers preceding queue submissions and
       cannot be confused with Sokol's reusable frame fence slots. */
    result = vkQueueSubmit(_sg.vk.queue, 1, &submit, marker->fence);
    if (result != VK_SUCCESS) {
        vkDestroyFence(marker->device, marker->fence, NULL);
        marker->fence = VK_NULL_HANDLE;
        marker->status = result == VK_ERROR_DEVICE_LOST ? SG_SUBMISSION_FAILED : SG_SUBMISSION_UNKNOWN;
        return marker->status == SG_SUBMISSION_FAILED;
    }
    marker->status = SG_SUBMISSION_PENDING;
    return true;
}
_SOKOL_PRIVATE sg_submission_status _sgu_vk_completion_poll(const _sgu_vk_completion_t* marker) {
    VkResult result;
    if (!marker) return SG_SUBMISSION_UNKNOWN;
    if (!marker->fence) return marker->status;
    result = vkGetFenceStatus(marker->device, marker->fence);
    if (result == VK_SUCCESS) return SG_SUBMISSION_COMPLETE;
    if (result == VK_ERROR_DEVICE_LOST) return SG_SUBMISSION_FAILED;
    /* Query failures other than device loss do not prove the fence is idle.
       Keep native ownership and retry; shutdown is the only blocking path. */
    return SG_SUBMISSION_PENDING;
}
_SOKOL_PRIVATE void _sgu_vk_completion_destroy(_sgu_vk_completion_t* marker) {
    if (!marker) return;
    if (marker->fence) vkDestroyFence(marker->device, marker->fence, NULL);
    memset(marker, 0, sizeof(*marker));
}
#endif

/* One marker covers commands already issued on Sokol's immediate context.
   Call make/poll/destroy on that context's owning thread, outside Sokol passes.
   Destroy before sg_shutdown; cancellation never proves completion. */
#if defined(SOKOL_D3D11)
typedef struct {
    ID3D11Query* query;
    ID3D11DeviceContext* context;
} _sgu_d3d11_completion_t;

_SOKOL_PRIVATE bool _sgu_d3d11_completion_make(_sgu_d3d11_completion_t* marker) {
    D3D11_QUERY_DESC desc;
    HRESULT result;
    memset(marker, 0, sizeof(*marker));
    if (!sg_isvalid() || _sg.cur_pass.in_pass || !_sg.d3d11.dev || !_sg.d3d11.ctx) return false;
    memset(&desc, 0, sizeof(desc));
    desc.Query = D3D11_QUERY_EVENT;
#if defined(__cplusplus)
    result = _sg.d3d11.dev->CreateQuery(&desc, &marker->query);
#else
    result = _sg.d3d11.dev->lpVtbl->CreateQuery(_sg.d3d11.dev, &desc, &marker->query);
#endif
    if (FAILED(result) || !marker->query) {
        if (marker->query) _sg_d3d11_Release(marker->query);
        marker->query = NULL;
        return false;
    }
    marker->context = _sg.d3d11.ctx;
    _sg_d3d11_AddRef(marker->context);
#if defined(__cplusplus)
    marker->context->End(marker->query);
    marker->context->Flush();
#else
    marker->context->lpVtbl->End(marker->context, (ID3D11Asynchronous*)marker->query);
    marker->context->lpVtbl->Flush(marker->context);
#endif
    /* Submit once at insertion so DONOTFLUSH polling makes progress even if
       the app stops rendering. Flush submits work; it does not wait for it. */
    return true;
}

_SOKOL_PRIVATE sg_submission_status _sgu_d3d11_completion_poll(const _sgu_d3d11_completion_t* marker) {
    BOOL completed = FALSE;
    HRESULT result;
    if (!marker || !marker->query || !marker->context) return SG_SUBMISSION_FAILED;
#if defined(__cplusplus)
    result = marker->context->GetData(marker->query, &completed, sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
#else
    result = marker->context->lpVtbl->GetData(marker->context, (ID3D11Asynchronous*)marker->query,
        &completed, sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
#endif
    if (result == S_FALSE) return SG_SUBMISSION_PENDING;
    if (FAILED(result)) return SG_SUBMISSION_FAILED;
    return completed ? SG_SUBMISSION_COMPLETE : SG_SUBMISSION_PENDING;
}

_SOKOL_PRIVATE void _sgu_d3d11_completion_destroy(_sgu_d3d11_completion_t* marker) {
    if (!marker) return;
    if (marker->query) _sg_d3d11_Release(marker->query);
    if (marker->context) _sg_d3d11_Release(marker->context);
    memset(marker, 0, sizeof(*marker));
}
#endif

/* Use the same current GL context and owning thread for make/poll/destroy.
   Destroy before context teardown and sg_shutdown. Cancellation never proves
   completion. The marker covers commands before insertion in this context. */
#if defined(_SOKOL_ANY_GL)
#if defined(_SOKOL_USE_WIN32_GL_LOADER)
/* Sokol's Win32 loader does not declare GL sync objects or these calls. */
typedef struct __GLsync* _sgu_gl_sync;
typedef _sgu_gl_sync (GL_APIENTRY* _sgu_gl_fence_fn)(GLenum, unsigned int);
typedef GLenum (GL_APIENTRY* _sgu_gl_wait_fn)(_sgu_gl_sync, unsigned int, GLuint64);
typedef void (GL_APIENTRY* _sgu_gl_delete_fn)(_sgu_gl_sync);
typedef void (GL_APIENTRY* _sgu_gl_flush_fn)(void);
static _sgu_gl_fence_fn _sgu_gl_fence;
static _sgu_gl_wait_fn _sgu_gl_wait;
static _sgu_gl_delete_fn _sgu_gl_delete;
static _sgu_gl_flush_fn _sgu_gl_flush;
_SOKOL_PRIVATE void* _sgu_gl_completion_proc(const char* name, _sg_wglGetProcAddress get_proc) {
    PROC result = get_proc(name);
    if (!result || result == (PROC)1 || result == (PROC)2 || result == (PROC)3 || result == (PROC)-1)
        result = GetProcAddress(_sg.gl.opengl32_dll, name);
    return (void*)result;
}
_SOKOL_PRIVATE bool _sgu_gl_completion_supported(void) {
    _sg_wglGetProcAddress get_proc;
    if (!sg_isvalid() || !_sg.gl.opengl32_dll) return false;
    get_proc = (_sg_wglGetProcAddress)GetProcAddress(_sg.gl.opengl32_dll, "wglGetProcAddress");
    if (!get_proc) return false;
    _sgu_gl_fence = (_sgu_gl_fence_fn)_sgu_gl_completion_proc("glFenceSync", get_proc);
    _sgu_gl_wait = (_sgu_gl_wait_fn)_sgu_gl_completion_proc("glClientWaitSync", get_proc);
    _sgu_gl_delete = (_sgu_gl_delete_fn)_sgu_gl_completion_proc("glDeleteSync", get_proc);
    _sgu_gl_flush = (_sgu_gl_flush_fn)_sgu_gl_completion_proc("glFlush", get_proc);
    return _sgu_gl_fence && _sgu_gl_wait && _sgu_gl_delete && _sgu_gl_flush;
}
#else
typedef GLsync _sgu_gl_sync;
#define _sgu_gl_fence glFenceSync
#define _sgu_gl_wait glClientWaitSync
#define _sgu_gl_delete glDeleteSync
#define _sgu_gl_flush glFlush
_SOKOL_PRIVATE bool _sgu_gl_completion_supported(void) { return sg_isvalid(); }
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#endif
typedef struct {
    _sgu_gl_sync sync;
} _sgu_gl_completion_t;

_SOKOL_PRIVATE bool _sgu_gl_completion_make(_sgu_gl_completion_t* marker) {
    marker->sync = 0;
    if (_sg.cur_pass.in_pass || !_sgu_gl_completion_supported()) return false;
    marker->sync = _sgu_gl_fence(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!marker->sync) return false;
    /* Submit the marker once; later polls neither flush nor wait. */
    _sgu_gl_flush();
    return true;
}

_SOKOL_PRIVATE sg_submission_status _sgu_gl_completion_poll(const _sgu_gl_completion_t* marker) {
    GLenum result;
    if (!marker || !marker->sync) return SG_SUBMISSION_FAILED;
    result = _sgu_gl_wait(marker->sync, 0, 0);
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) return SG_SUBMISSION_COMPLETE;
    if (result == GL_TIMEOUT_EXPIRED) return SG_SUBMISSION_PENDING;
    return SG_SUBMISSION_FAILED;
}

_SOKOL_PRIVATE void _sgu_gl_completion_destroy(_sgu_gl_completion_t* marker) {
    if (!marker) return;
    if (marker->sync) _sgu_gl_delete(marker->sync);
    marker->sync = 0;
}
#endif

/* A receipt marks work already submitted to _sg.wgpu.queue, not the current
   unfinished encoder. Call make only after sg_commit has returned. */
#if defined(SOKOL_WGPU)

#if defined(WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT) && defined(__cplusplus) && (__cplusplus >= 201103L)
#include <atomic>
#include <new>
#define _SGU_WGPU_CPP_ATOMICS (1)
#define _SGU_WGPU_COMPLETION_AVAILABLE (1)
#elif defined(WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define _SGU_WGPU_C11_ATOMICS (1)
#define _SGU_WGPU_COMPLETION_AVAILABLE (1)
#elif defined(WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT) && defined(_MSC_VER)
#include <intrin.h>
#define _SGU_WGPU_MSVC_ATOMICS (1)
#define _SGU_WGPU_COMPLETION_AVAILABLE (1)
#elif defined(WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT) && (defined(__GNUC__) || defined(__clang__))
#define _SGU_WGPU_GNU_ATOMICS (1)
#define _SGU_WGPU_COMPLETION_AVAILABLE (1)
#else
#define _SGU_WGPU_COMPLETION_AVAILABLE (0)
#endif

typedef struct _sgu_wgpu_completion_state_t _sgu_wgpu_completion_state_t;
typedef struct _sgu_wgpu_completion_t {
    _sgu_wgpu_completion_state_t* state;
} _sgu_wgpu_completion_t;

#if _SGU_WGPU_COMPLETION_AVAILABLE
struct _sgu_wgpu_completion_state_t {
#if defined(_SGU_WGPU_CPP_ATOMICS)
    std::atomic<int> references;
    std::atomic<int> status;
#elif defined(_SGU_WGPU_C11_ATOMICS)
    atomic_int references;
    atomic_int status;
#elif defined(_SGU_WGPU_MSVC_ATOMICS)
    volatile long references;
    volatile long status;
#else
    int references;
    int status;
#endif
};

_SOKOL_PRIVATE void _sgu_wgpu_completion_release(_sgu_wgpu_completion_state_t* state) {
#if defined(_SGU_WGPU_CPP_ATOMICS)
    if (state->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete state;
#elif defined(_SGU_WGPU_C11_ATOMICS)
    if (atomic_fetch_sub_explicit(&state->references, 1, memory_order_acq_rel) == 1) free(state);
#elif defined(_SGU_WGPU_MSVC_ATOMICS)
    if (_InterlockedDecrement(&state->references) == 0) free(state);
#else
    if (__atomic_fetch_sub(&state->references, 1, __ATOMIC_ACQ_REL) == 1) free(state);
#endif
}

_SOKOL_PRIVATE void _sgu_wgpu_completion_callback(WGPUQueueWorkDoneStatus status,
    WGPUStringView message, void* userdata1, void* userdata2) {
    (void)message;
    (void)userdata2;
    _sgu_wgpu_completion_state_t* state = (_sgu_wgpu_completion_state_t*)userdata1;
    const int result = status == WGPUQueueWorkDoneStatus_Success
        ? SG_SUBMISSION_COMPLETE : SG_SUBMISSION_FAILED;
    /* May run on another thread or after shutdown. Touch only this callback's
       heap state, never Sokol, WebGPU, or the common receipt manager. */
#if defined(_SGU_WGPU_CPP_ATOMICS)
    state->status.store(result, std::memory_order_release);
#elif defined(_SGU_WGPU_C11_ATOMICS)
    atomic_store_explicit(&state->status, result, memory_order_release);
#elif defined(_SGU_WGPU_MSVC_ATOMICS)
    _InterlockedExchange(&state->status, result);
#else
    __atomic_store_n(&state->status, result, __ATOMIC_RELEASE);
#endif
    _sgu_wgpu_completion_release(state);
}
#endif

_SOKOL_PRIVATE bool _sgu_wgpu_completion_supported(void) {
#if _SGU_WGPU_COMPLETION_AVAILABLE
    return sg_isvalid() && _sg.wgpu.queue != 0;
#else
    return false;
#endif
}

_SOKOL_PRIVATE bool _sgu_wgpu_completion_make(_sgu_wgpu_completion_t* out) {
    out->state = 0;
#if _SGU_WGPU_COMPLETION_AVAILABLE
    if (!_sgu_wgpu_completion_supported()) return false;
#if defined(_SGU_WGPU_CPP_ATOMICS)
    _sgu_wgpu_completion_state_t* state = new (std::nothrow) _sgu_wgpu_completion_state_t;
#else
    _sgu_wgpu_completion_state_t* state = (_sgu_wgpu_completion_state_t*)malloc(sizeof(*state));
#endif
    if (!state) return false;
    /* The manager and callback each own one reference. The callback may fire
       before registration returns or long after the manager drops its ref. */
#if defined(_SGU_WGPU_CPP_ATOMICS)
    state->references.store(2, std::memory_order_relaxed);
    state->status.store(SG_SUBMISSION_PENDING, std::memory_order_relaxed);
#elif defined(_SGU_WGPU_C11_ATOMICS)
    atomic_init(&state->references, 2);
    atomic_init(&state->status, SG_SUBMISSION_PENDING);
#elif defined(_SGU_WGPU_MSVC_ATOMICS)
    state->references = 2;
    state->status = SG_SUBMISSION_PENDING;
#else
    __atomic_store_n(&state->references, 2, __ATOMIC_RELAXED);
    __atomic_store_n(&state->status, SG_SUBMISSION_PENDING, __ATOMIC_RELAXED);
#endif
    out->state = state;
    WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = _sgu_wgpu_completion_callback;
    info.userdata1 = state;
    /* No wait, map, extra submit, or event pump. Runtime owns the future; the
       callback releases its reference after success, error, or cancellation. */
    (void)wgpuQueueOnSubmittedWorkDone(_sg.wgpu.queue, info);
    return true;
#else
    return false;
#endif
}

_SOKOL_PRIVATE sg_submission_status _sgu_wgpu_completion_poll(const _sgu_wgpu_completion_t* completion) {
#if _SGU_WGPU_COMPLETION_AVAILABLE
    if (!completion->state) return SG_SUBMISSION_UNKNOWN;
#if defined(_SGU_WGPU_CPP_ATOMICS)
    return (sg_submission_status)completion->state->status.load(std::memory_order_acquire);
#elif defined(_SGU_WGPU_C11_ATOMICS)
    return (sg_submission_status)atomic_load_explicit(&completion->state->status, memory_order_acquire);
#elif defined(_SGU_WGPU_MSVC_ATOMICS)
    return (sg_submission_status)_InterlockedCompareExchange(&completion->state->status, 0, 0);
#else
    return (sg_submission_status)__atomic_load_n(&completion->state->status, __ATOMIC_ACQUIRE);
#endif
#else
    (void)completion;
    return SG_SUBMISSION_UNKNOWN;
#endif
}

_SOKOL_PRIVATE void _sgu_wgpu_completion_destroy(_sgu_wgpu_completion_t* completion) {
#if _SGU_WGPU_COMPLETION_AVAILABLE
    if (completion->state) _sgu_wgpu_completion_release(completion->state);
#endif
    completion->state = 0;
}

#endif /* SOKOL_WGPU */

#ifndef SG_SUBMISSION_RECEIPT_CAPACITY
#define SG_SUBMISSION_RECEIPT_CAPACITY (64)
#endif
#if SG_SUBMISSION_RECEIPT_CAPACITY < 1
#error "SG_SUBMISSION_RECEIPT_CAPACITY must be positive"
#endif

#if defined(SOKOL_METAL)
typedef _sgu_mtl_completion_t _sgu_submission_native_t;
#define _sgu_submission_native_make _sgu_mtl_completion_make
#define _sgu_submission_native_poll _sgu_mtl_completion_poll
#define _sgu_submission_native_destroy _sgu_mtl_completion_destroy
#elif defined(SOKOL_VULKAN)
typedef _sgu_vk_completion_t _sgu_submission_native_t;
#define _sgu_submission_native_make _sgu_vk_completion_make
#define _sgu_submission_native_poll _sgu_vk_completion_poll
#define _sgu_submission_native_destroy _sgu_vk_completion_destroy
#elif defined(SOKOL_D3D11)
typedef _sgu_d3d11_completion_t _sgu_submission_native_t;
#define _sgu_submission_native_make _sgu_d3d11_completion_make
#define _sgu_submission_native_poll _sgu_d3d11_completion_poll
#define _sgu_submission_native_destroy _sgu_d3d11_completion_destroy
#elif defined(_SOKOL_ANY_GL)
typedef _sgu_gl_completion_t _sgu_submission_native_t;
#define _sgu_submission_native_make _sgu_gl_completion_make
#define _sgu_submission_native_poll _sgu_gl_completion_poll
#define _sgu_submission_native_destroy _sgu_gl_completion_destroy
#elif defined(SOKOL_WGPU)
typedef _sgu_wgpu_completion_t _sgu_submission_native_t;
#define _sgu_submission_native_make _sgu_wgpu_completion_make
#define _sgu_submission_native_poll _sgu_wgpu_completion_poll
#define _sgu_submission_native_destroy _sgu_wgpu_completion_destroy
#else
typedef struct { int unused; } _sgu_submission_native_t;
_SOKOL_PRIVATE bool _sgu_submission_native_make(_sgu_submission_native_t* marker) { (void)marker; return false; }
_SOKOL_PRIVATE sg_submission_status _sgu_submission_native_poll(const _sgu_submission_native_t* marker) { (void)marker; return SG_SUBMISSION_UNKNOWN; }
_SOKOL_PRIVATE void _sgu_submission_native_destroy(_sgu_submission_native_t* marker) { (void)marker; }
#endif

typedef struct _sgu_submission_owner_t _sgu_submission_owner_t;
/* This prefix and payload pointer have the same layout in every backend TU.
   A dispatcher may destroy an expired public receipt from a previous backend;
   it must never interpret another backend's native payload. */
struct sg_submission_receipt_t {
    _sgu_submission_owner_t* owner;
    uint64_t generation;
    struct sg_submission_receipt_t* next;
    void* native;
    sg_submission_status status;
    bool listed;
    bool released;
};
struct _sgu_submission_owner_t {
    sg_submission_receipt head;
    uint64_t generation;
    uint32_t count;
};
static _sgu_submission_owner_t _sgu_submission_owner;

_SOKOL_PRIVATE void _sgu_submission_release_native(sg_submission_receipt receipt) {
    if (!receipt->native) return;
    _sgu_submission_native_destroy((_sgu_submission_native_t*)receipt->native);
    free(receipt->native);
    receipt->native = NULL;
}
_SOKOL_PRIVATE void _sgu_submission_collect(void) {
    sg_submission_receipt* link = &_sgu_submission_owner.head;
    while (*link) {
        sg_submission_receipt receipt = *link;
        if (receipt->native) {
            receipt->status = _sgu_submission_native_poll((const _sgu_submission_native_t*)receipt->native);
            /* Unknown does not prove an in-flight native marker is safe to destroy. */
            if (receipt->status == SG_SUBMISSION_COMPLETE || receipt->status == SG_SUBMISSION_FAILED)
                _sgu_submission_release_native(receipt);
        }
        if (receipt->released && !receipt->native) {
            *link = receipt->next;
            --_sgu_submission_owner.count;
            free(receipt);
        } else link = &receipt->next;
    }
}
SOKOL_API_IMPL bool sg_submission_completion_supported(void) {
    if (!sg_isvalid()) return false;
#if defined(SOKOL_METAL)
    return _sg.mtl.valid && _sg.mtl.cmd_queue;
#elif defined(SOKOL_VULKAN)
    return _sg.vk.valid && _sg.vk.dev && _sg.vk.queue;
#elif defined(SOKOL_D3D11)
    return _sg.d3d11.dev && _sg.d3d11.ctx;
#elif defined(_SOKOL_ANY_GL)
    return _sgu_gl_completion_supported();
#elif defined(SOKOL_WGPU)
    return _sgu_wgpu_completion_supported();
#else
    return false;
#endif
}
SOKOL_API_IMPL sg_submission_receipt sg_commit_tracked(void) {
    sg_submission_receipt receipt = NULL;
    bool made = false;
    if (!sg_isvalid() || _sg.cur_pass.in_pass) return NULL;
    _sgu_submission_collect();
    if (sg_submission_completion_supported() && _sgu_submission_owner.count < SG_SUBMISSION_RECEIPT_CAPACITY) {
        receipt = (sg_submission_receipt)calloc(1, sizeof(*receipt));
        if (receipt) {
            receipt->native = calloc(1, sizeof(_sgu_submission_native_t));
            if (!receipt->native) { free(receipt); receipt = NULL; }
        }
    }
#if defined(SOKOL_METAL)
    /* Capture the exact frame command buffer before sg_commit resets it. */
    if (receipt) made = _sgu_submission_native_make((_sgu_submission_native_t*)receipt->native);
#endif
    sg_commit();
#if !defined(SOKOL_METAL)
    if (receipt) made = _sgu_submission_native_make((_sgu_submission_native_t*)receipt->native);
#endif
    if (receipt) {
        if (!made) {
            _sgu_submission_release_native(receipt);
            free(receipt);
            return NULL;
        }
        receipt->owner = &_sgu_submission_owner;
        receipt->generation = _sgu_submission_owner.generation;
        receipt->status = SG_SUBMISSION_PENDING;
        receipt->listed = true;
        receipt->next = _sgu_submission_owner.head;
        _sgu_submission_owner.head = receipt;
        ++_sgu_submission_owner.count;
    }
    return receipt;
}
SOKOL_API_IMPL sg_submission_status sg_query_submission_receipt(sg_submission_receipt receipt) {
    if (!receipt || !receipt->listed || receipt->released || receipt->owner != &_sgu_submission_owner ||
        receipt->generation != _sgu_submission_owner.generation || !sg_isvalid()) return SG_SUBMISSION_UNKNOWN;
    _sgu_submission_collect();
    return receipt->status;
}
SOKOL_API_IMPL void sg_destroy_submission_receipt(sg_submission_receipt receipt) {
    if (!receipt || receipt->released) return;
    if (!receipt->listed) { free(receipt); return; }
    receipt->released = true;
    /* A foreign live record stays with its original owner. Explicit shutdown
       before a backend switch prevents that case in a correct caller. */
    if (receipt->owner == &_sgu_submission_owner && sg_isvalid()) _sgu_submission_collect();
}
SOKOL_API_IMPL bool sg_shutdown_submission_receipts(void) {
    sg_submission_receipt receipt = _sgu_submission_owner.head;
#if defined(SOKOL_VULKAN)
    /* Vulkan fences cannot be destroyed while their queue operations run.
       This wait belongs only to explicit device shutdown, never polling. */
    if (receipt && sg_isvalid()) {
        VkResult result = vkDeviceWaitIdle(_sg.vk.dev);
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) return false;
    }
#endif
    while (receipt) {
        sg_submission_receipt next = receipt->next;
        if (sg_isvalid()) _sgu_submission_release_native(receipt);
        else { free(receipt->native); receipt->native = NULL; }
        receipt->listed = false;
        receipt->next = NULL;
        receipt->status = SG_SUBMISSION_UNKNOWN;
        if (receipt->released) free(receipt);
        receipt = next;
    }
    _sgu_submission_owner.head = NULL;
    _sgu_submission_owner.count = 0;
    ++_sgu_submission_owner.generation;
    return true;
}

/*--- Acceleration Structures ------------------------------------------------*/

/* Native state per backend. Vulkan keeps the structure in its own buffer. The
   instance buffers form a ring with one slot per sokol frame in flight, so the
   CPU never rewrites a slot the GPU still reads. */
#if defined(SOKOL_VULKAN)
typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceAddress addr;
    void* map;
} _sgu_vk_mem_t;

typedef struct {
    VkAccelerationStructureKHR as;
    _sgu_vk_mem_t store;
    VkDeviceAddress addr;
    VkDeviceSize build_scratch;
    VkDeviceSize update_scratch;
    _sgu_vk_mem_t instances[SG_NUM_INFLIGHT_FRAMES];
} _sgu_accel_native_t;
#elif defined(SOKOL_METAL)
/* Objective-C objects are held as retained void* so the structs stay plain C
   under both ARC and manual reference counting. */
typedef struct {
    void* as;
    void* blas_array;   /* NSArray of the BLAS objects of a TLAS */
    void* instances[SG_NUM_INFLIGHT_FRAMES];
    uint64_t build_scratch;
    uint64_t refit_scratch;
} _sgu_accel_native_t;
#else
typedef struct { int unused; } _sgu_accel_native_t;
#endif

/* Backend-neutral part. A TLAS keeps a reference on each BLAS it lists. The
   desc label is not stored. */
struct sg_accel_t {
    bool is_tlas;
    bool built;
    bool allow_update;
    bool destroy_requested;     /* BLAS destroyed while a TLAS still uses it */
    bool recorded;              /* a TLAS build or refit ran in last_frame */
    uint32_t last_frame;
    int refs;
    sg_blas_desc blas;
    sg_accel* blas_list;
    int blas_count;
    int max_instances;
    int built_count;
    _sgu_accel_native_t native;
};

_SOKOL_PRIVATE _sg_buffer_t* _sgu_accel_input_buffer(sg_buffer id, int offset, int64_t bytes) {
    _sg_buffer_t* buf = _sg_lookup_buffer(id.id);
    if (!buf || buf->slot.state != SG_RESOURCESTATE_VALID || !buf->cmn.usage.storage_buffer) return 0;
    if (offset < 0 || bytes < 0 || (int64_t)offset + bytes > (int64_t)buf->cmn.size) return 0;
    #if defined(SOKOL_VULKAN)
    if (!buf->vk.dev_addr) return 0;
    #endif
    return buf;
}

_SOKOL_PRIVATE int _sgu_accel_index_size(sg_accel_index_format format) {
    return format == SG_ACCEL_INDEXFORMAT_UINT16 ? 2 : (format == SG_ACCEL_INDEXFORMAT_UINT32 ? 4 : 0);
}

/* Returns the buffers a BLAS reads, or false when one is gone or too small. */
_SOKOL_PRIVATE bool _sgu_accel_blas_buffers(const sg_accel blas, _sg_buffer_t** vb, _sg_buffer_t** ib) {
    const sg_blas_desc* d = &blas->blas;
    const int64_t vbytes = (int64_t)d->vertex_stride * (int64_t)(d->vertex_count - 1) + 12;
    *vb = _sgu_accel_input_buffer(d->vertex_buffer, d->vertex_offset, vbytes);
    *ib = 0;
    if (!*vb) return false;
    if (d->index_format != SG_ACCEL_INDEXFORMAT_NONE) {
        *ib = _sgu_accel_input_buffer(d->index_buffer, d->index_offset,
            (int64_t)d->index_count * _sgu_accel_index_size(d->index_format));
        if (!*ib) return false;
    }
    return true;
}

_SOKOL_PRIVATE int _sgu_accel_triangle_count(const sg_accel blas) {
    return (blas->blas.index_format == SG_ACCEL_INDEXFORMAT_NONE ? blas->blas.vertex_count : blas->blas.index_count) / 3;
}

/* Sends an error to the sokol logger of sg_setup. sokol_gfx has no log item for
   these messages, so item 0 carries the text. */
_SOKOL_PRIVATE void _sgu_accel_log(const char* fmt, ...) {
    if (!_sg.desc.logger.func) return;
    char msg[512];  /* the accel report lines carry a full instance transform */
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    _sg.desc.logger.func("sg_gfx_utils", 1, 0, msg, 0, 0, _sg.desc.logger.user_data);
}

/* The layouts that sg_make_shader_accel recorded, one entry per shader. Vulkan
   and Metal read them in sg_accel_bind. Entries of destroyed shaders are
   dropped on the next add. */
#define _SGU_ACCEL_MAX_SHADERS (64)
static struct {
    uint32_t id[_SGU_ACCEL_MAX_SHADERS];
    sg_accel_layout_desc layout[_SGU_ACCEL_MAX_SHADERS];
    int count;
} _sgu_accel_shaders;

_SOKOL_PRIVATE void _sgu_accel_prune_shaders(void) {
    int keep = 0;
    for (int i = 0; i < _sgu_accel_shaders.count; i++) {
        if (!_sg_lookup_shader(_sgu_accel_shaders.id[i])) continue;
        _sgu_accel_shaders.id[keep] = _sgu_accel_shaders.id[i];
        _sgu_accel_shaders.layout[keep] = _sgu_accel_shaders.layout[i];
        keep++;
    }
    _sgu_accel_shaders.count = keep;
}

/* Returns the recorded layout of a slot of a shader, or 0 if there is none. */
_SOKOL_PRIVATE const sg_accel_slot_layout* _sgu_accel_find_slot(uint32_t shader_id, int slot) {
    for (int i = 0; i < _sgu_accel_shaders.count; i++) {
        if (_sgu_accel_shaders.id[i] != shader_id) continue;
        const sg_accel_layout_desc* layout = &_sgu_accel_shaders.layout[i];
        for (int j = 0; j < layout->num_slots; j++) {
            if (layout->slots[j].slot == slot) return &layout->slots[j];
        }
    }
    return 0;
}

/* Checks what does not depend on the backend. */
_SOKOL_PRIVATE bool _sgu_accel_validate_slots(const sg_accel_layout_desc* layout) {
    if (layout->num_slots < 0 || layout->num_slots > SG_ACCEL_MAX_SLOTS) {
        _sgu_accel_log("sg_make_shader_accel: num_slots %d is not in 0..%d", layout->num_slots, SG_ACCEL_MAX_SLOTS);
        return false;
    }
    for (int i = 0; i < layout->num_slots; i++) {
        const int slot = layout->slots[i].slot;
        if (slot >= SG_ACCEL_MAX_SLOTS) {
            _sgu_accel_log("sg_make_shader_accel: slot %d is not below %d", slot, SG_ACCEL_MAX_SLOTS);
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (layout->slots[j].slot == slot) {
                _sgu_accel_log("sg_make_shader_accel: slot %d is listed twice", slot);
                return false;
            }
        }
    }
    return true;
}

/*--- Acceleration Structures: Vulkan ----------------------------------------*/
#if defined(SOKOL_VULKAN)

static struct {
    bool init;
    bool supported;
    VkDevice device;
    VkDeviceSize scratch_align;
    uint64_t max_primitives;
    uint64_t max_instances;
    uint64_t max_geometries;
    PFN_vkCreateAccelerationStructureKHR create;
    PFN_vkDestroyAccelerationStructureKHR destroy;
    PFN_vkGetAccelerationStructureBuildSizesKHR get_build_sizes;
    PFN_vkGetAccelerationStructureDeviceAddressKHR get_address;
    PFN_vkCmdBuildAccelerationStructuresKHR cmd_build;
} _sgu_vk_accel_state;

#define _SGU_VK_ACCEL_LOAD(field, name) \
    _sgu_vk_accel_state.field = (PFN_##name)vkGetDeviceProcAddr(_sgu_vk_accel_state.device, #name)

/* True when the GPU lists every extension name in names. */
_SOKOL_PRIVATE bool _sgu_vk_accel_has_extensions(const char* const* names, int name_count) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(_sg.vk.phys_dev, 0, &count, 0) != VK_SUCCESS || count == 0) return false;
    VkExtensionProperties* props = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * count);
    if (!props) return false;
    bool all = false;
    if (vkEnumerateDeviceExtensionProperties(_sg.vk.phys_dev, 0, &count, props) == VK_SUCCESS) {
        all = true;
        for (int n = 0; n < name_count && all; n++) {
            bool found = false;
            for (uint32_t i = 0; i < count && !found; i++) {
                found = 0 == strcmp(props[i].extensionName, names[n]);
            }
            all = found;
        }
    }
    free(props);
    return all;
}

/* Loads entry points once per device. The GPU must list the extensions and
   offer the features; the application must have enabled them (see the
   contract above). */
_SOKOL_PRIVATE bool _sgu_vk_accel_supported(void) {
    if (!_sg.vk.valid || !_sg.vk.dev) return false;
    if (_sgu_vk_accel_state.init && _sgu_vk_accel_state.device == _sg.vk.dev) {
        return _sgu_vk_accel_state.supported;
    }
    memset(&_sgu_vk_accel_state, 0, sizeof(_sgu_vk_accel_state));
    _sgu_vk_accel_state.init = true;
    _sgu_vk_accel_state.device = _sg.vk.dev;

    static const char* const extensions[3] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };
    if (!_sgu_vk_accel_has_extensions(extensions, 3)) return false;

    _SG_STRUCT(VkPhysicalDeviceRayQueryFeaturesKHR, rq_features);
    rq_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    _SG_STRUCT(VkPhysicalDeviceAccelerationStructureFeaturesKHR, as_features);
    as_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    as_features.pNext = &rq_features;
    _SG_STRUCT(VkPhysicalDeviceFeatures2, features);
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &as_features;
    vkGetPhysicalDeviceFeatures2(_sg.vk.phys_dev, &features);
    if (!as_features.accelerationStructure || !rq_features.rayQuery) return false;

    _SG_STRUCT(VkPhysicalDeviceAccelerationStructurePropertiesKHR, as_props);
    as_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    _SG_STRUCT(VkPhysicalDeviceProperties2, props);
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &as_props;
    vkGetPhysicalDeviceProperties2(_sg.vk.phys_dev, &props);
    _sgu_vk_accel_state.scratch_align = as_props.minAccelerationStructureScratchOffsetAlignment
        ? as_props.minAccelerationStructureScratchOffsetAlignment : 1;
    _sgu_vk_accel_state.max_primitives = as_props.maxPrimitiveCount;
    _sgu_vk_accel_state.max_instances = as_props.maxInstanceCount;
    _sgu_vk_accel_state.max_geometries = as_props.maxGeometryCount;

    _SGU_VK_ACCEL_LOAD(create, vkCreateAccelerationStructureKHR);
    _SGU_VK_ACCEL_LOAD(destroy, vkDestroyAccelerationStructureKHR);
    _SGU_VK_ACCEL_LOAD(get_build_sizes, vkGetAccelerationStructureBuildSizesKHR);
    _SGU_VK_ACCEL_LOAD(get_address, vkGetAccelerationStructureDeviceAddressKHR);
    _SGU_VK_ACCEL_LOAD(cmd_build, vkCmdBuildAccelerationStructuresKHR);
    _sgu_vk_accel_state.supported = _sgu_vk_accel_state.create && _sgu_vk_accel_state.destroy &&
        _sgu_vk_accel_state.get_build_sizes && _sgu_vk_accel_state.get_address && _sgu_vk_accel_state.cmd_build;
    return _sgu_vk_accel_state.supported;
}

/* Memory comes from sokol's allocator. STORAGE_BUFFER memory is device local
   with a device address; UNIFORMS memory is host visible, coherent and also
   has a device address, which suits instance data written by the CPU. */
_SOKOL_PRIVATE bool _sgu_vk_accel_alloc(_sgu_vk_mem_t* out, VkDeviceSize size, VkBufferUsageFlags usage, _sg_vk_memtype_t type) {
    memset(out, 0, sizeof(*out));
    VkDevice dev = _sgu_vk_accel_state.device;
    _SG_STRUCT(VkBufferCreateInfo, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size ? size : 16;
    create_info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &create_info, 0, &out->buf) != VK_SUCCESS) {
        _SG_ERROR(VULKAN_CREATE_BUFFER_FAILED);
        memset(out, 0, sizeof(*out));
        return false;
    }
    _SG_STRUCT(VkMemoryRequirements, reqs);
    vkGetBufferMemoryRequirements(dev, out->buf, &reqs);
    out->mem = _sg_vk_mem_alloc_device_memory(type, &reqs);
    if (!out->mem) {
        _SG_ERROR(VULKAN_ALLOC_BUFFER_DEVICE_MEMORY_FAILED);
        vkDestroyBuffer(dev, out->buf, 0);
        memset(out, 0, sizeof(*out));
        return false;
    }
    if (vkBindBufferMemory(dev, out->buf, out->mem, 0) != VK_SUCCESS) {
        _SG_ERROR(VULKAN_BIND_BUFFER_MEMORY_FAILED);
        vkDestroyBuffer(dev, out->buf, 0);
        vkFreeMemory(dev, out->mem, 0);
        memset(out, 0, sizeof(*out));
        return false;
    }
    _SG_STRUCT(VkBufferDeviceAddressInfo, addr_info);
    addr_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addr_info.buffer = out->buf;
    out->addr = vkGetBufferDeviceAddress(dev, &addr_info);
    if (type == _SG_VK_MEMTYPE_UNIFORMS) {
        if (vkMapMemory(dev, out->mem, 0, VK_WHOLE_SIZE, 0, &out->map) != VK_SUCCESS) {
            vkDestroyBuffer(dev, out->buf, 0);
            vkFreeMemory(dev, out->mem, 0);
            memset(out, 0, sizeof(*out));
            return false;
        }
    }
    return true;
}

/* Deferred free goes through sokol's delete queue: it runs after the GPU
   finished the frame slot. After sg_shutdown the device is idle. */
_SOKOL_PRIVATE void _sgu_vk_accel_free(_sgu_vk_mem_t* mem) {
    if (!mem->buf && !mem->mem) return;
    if (sg_isvalid()) {
        if (mem->buf) _sg_vk_delete_queue_add(_sg_vk_buffer_destructor, (void*)mem->buf);
        if (mem->mem) _sg_vk_delete_queue_add(_sg_vk_memory_destructor, (void*)mem->mem);
    } else {
        if (mem->buf) vkDestroyBuffer(_sgu_vk_accel_state.device, mem->buf, 0);
        if (mem->mem) vkFreeMemory(_sgu_vk_accel_state.device, mem->mem, 0);
    }
    memset(mem, 0, sizeof(*mem));
}

_SOKOL_PRIVATE void _sgu_vk_accel_destructor(void* obj) {
    if (!_sgu_vk_accel_state.destroy) return;
    _sgu_vk_accel_state.destroy(_sgu_vk_accel_state.device, (VkAccelerationStructureKHR)obj, 0);
}

_SOKOL_PRIVATE void _sgu_vk_accel_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    _SG_STRUCT(VkMemoryBarrier2, barrier);
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    _SG_STRUCT(VkDependencyInfo, dependency);
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

/* Fills the geometry, build info and range for one structure. Mode, source,
   destination and scratch are left to the caller. For a TLAS, instance_count
   and instance_addr describe the instance buffer. */
_SOKOL_PRIVATE bool _sgu_vk_accel_fill(const sg_accel a, VkAccelerationStructureGeometryKHR* geo,
    VkAccelerationStructureBuildGeometryInfoKHR* info, VkAccelerationStructureBuildRangeInfoKHR* range,
    uint32_t instance_count, VkDeviceAddress instance_addr) {
    geo->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (a->allow_update) info->flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    info->geometryCount = 1;
    info->pGeometries = geo;
    if (a->is_tlas) {
        geo->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geo->geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geo->geometry.instances.arrayOfPointers = VK_FALSE;
        geo->geometry.instances.data.deviceAddress = instance_addr;
        info->type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        range->primitiveCount = instance_count;
        return true;
    }
    _sg_buffer_t* vb = 0;
    _sg_buffer_t* ib = 0;
    if (!_sgu_accel_blas_buffers(a, &vb, &ib)) return false;
    const sg_blas_desc* d = &a->blas;
    geo->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geo->flags = d->opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
    VkAccelerationStructureGeometryTrianglesDataKHR* tri = &geo->geometry.triangles;
    tri->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri->vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri->vertexData.deviceAddress = vb->vk.dev_addr + (VkDeviceAddress)d->vertex_offset;
    tri->vertexStride = (VkDeviceSize)d->vertex_stride;
    tri->maxVertex = (uint32_t)(d->vertex_count - 1);
    tri->indexType = VK_INDEX_TYPE_NONE_KHR;
    if (ib) {
        tri->indexType = (d->index_format == SG_ACCEL_INDEXFORMAT_UINT16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        tri->indexData.deviceAddress = ib->vk.dev_addr + (VkDeviceAddress)d->index_offset;
    }
    info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    range->primitiveCount = (uint32_t)_sgu_accel_triangle_count(a);
    return true;
}

_SOKOL_PRIVATE bool _sgu_vk_accel_make(sg_accel a, const char* label) {
    if (!_sgu_vk_accel_supported()) return false;
    VkDevice dev = _sgu_vk_accel_state.device;
    VkDeviceAddress instance_addr = 0;
    if (_sgu_vk_accel_state.max_geometries < 1) return false;
    if (a->is_tlas ? ((uint64_t)a->max_instances > _sgu_vk_accel_state.max_instances)
                   : ((uint64_t)_sgu_accel_triangle_count(a) > _sgu_vk_accel_state.max_primitives)) {
        return false;
    }
    if (a->is_tlas) {
        const VkDeviceSize bytes = (VkDeviceSize)a->max_instances * sizeof(VkAccelerationStructureInstanceKHR);
        for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) {
            if (!_sgu_vk_accel_alloc(&a->native.instances[i], bytes,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, _SG_VK_MEMTYPE_UNIFORMS)) {
                return false;
            }
        }
        instance_addr = a->native.instances[0].addr;
    }
    _SG_STRUCT(VkAccelerationStructureGeometryKHR, geo);
    _SG_STRUCT(VkAccelerationStructureBuildGeometryInfoKHR, info);
    _SG_STRUCT(VkAccelerationStructureBuildRangeInfoKHR, range);
    if (!_sgu_vk_accel_fill(a, &geo, &info, &range, (uint32_t)a->max_instances, instance_addr)) return false;
    _SG_STRUCT(VkAccelerationStructureBuildSizesInfoKHR, sizes);
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    _sgu_vk_accel_state.get_build_sizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
        &range.primitiveCount, &sizes);
    a->native.build_scratch = sizes.buildScratchSize;
    a->native.update_scratch = sizes.updateScratchSize;
    if (!_sgu_vk_accel_alloc(&a->native.store, sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, _SG_VK_MEMTYPE_STORAGE_BUFFER)) {
        return false;
    }
    _SG_STRUCT(VkAccelerationStructureCreateInfoKHR, create_info);
    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.buffer = a->native.store.buf;
    create_info.size = sizes.accelerationStructureSize;
    create_info.type = info.type;
    if (_sgu_vk_accel_state.create(dev, &create_info, 0, &a->native.as) != VK_SUCCESS || !a->native.as) {
        a->native.as = 0;
        return false;
    }
    _SG_STRUCT(VkAccelerationStructureDeviceAddressInfoKHR, addr_info);
    addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addr_info.accelerationStructure = a->native.as;
    a->native.addr = _sgu_vk_accel_state.get_address(dev, &addr_info);
    _sg_vk_set_object_label(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)a->native.as, label);
    return true;
}

_SOKOL_PRIVATE void _sgu_vk_accel_destroy(sg_accel a) {
    if (a->native.as) {
        if (sg_isvalid()) _sg_vk_delete_queue_add(_sgu_vk_accel_destructor, (void*)a->native.as);
        else _sgu_vk_accel_destructor((void*)a->native.as);
        a->native.as = 0;
    }
    _sgu_vk_accel_free(&a->native.store);
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) _sgu_vk_accel_free(&a->native.instances[i]);
}

/* One scratch buffer per frame slot. It only grows. Builds of one frame take
   consecutive ranges at scratch_align offsets, and the barriers around each
   build order them. Growing replaces the buffer for later builds of the frame;
   builds already recorded keep using the old one, which is freed through the
   delete queue after the GPU finished the slot. */
static struct {
    _sgu_vk_mem_t mem[SG_NUM_INFLIGHT_FRAMES];
    VkDeviceSize capacity[SG_NUM_INFLIGHT_FRAMES];  /* usable bytes after the alignment pad */
    VkDeviceSize pad[SG_NUM_INFLIGHT_FRAMES];       /* bytes from the buffer start to the first aligned address */
    VkDeviceSize used[SG_NUM_INFLIGHT_FRAMES];
    uint32_t frame[SG_NUM_INFLIGHT_FRAMES];
} _sgu_vk_accel_scratch;

/* Returns the aligned device address of size scratch bytes, or 0 on failure. */
_SOKOL_PRIVATE VkDeviceAddress _sgu_vk_accel_scratch_alloc(VkDeviceSize size) {
    const uint32_t slot = _sg.vk.frame_slot;
    const VkDeviceSize align = _sgu_vk_accel_state.scratch_align;
    if (size < 1) size = 1;
    if (_sgu_vk_accel_scratch.frame[slot] != _sg.frame_index) {
        _sgu_vk_accel_scratch.frame[slot] = _sg.frame_index;
        _sgu_vk_accel_scratch.used[slot] = 0;
    }
    VkDeviceSize used = (_sgu_vk_accel_scratch.used[slot] + align - 1) & ~(align - 1);
    if (!_sgu_vk_accel_scratch.mem[slot].buf || used + size > _sgu_vk_accel_scratch.capacity[slot]) {
        VkDeviceSize capacity = _sgu_vk_accel_scratch.capacity[slot] * 2;
        if (capacity < size) capacity = size;
        _sgu_vk_mem_t mem;
        if (!_sgu_vk_accel_alloc(&mem, capacity + align, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, _SG_VK_MEMTYPE_STORAGE_BUFFER)) {
            return 0;
        }
        _sgu_vk_accel_free(&_sgu_vk_accel_scratch.mem[slot]);
        _sgu_vk_accel_scratch.mem[slot] = mem;
        _sgu_vk_accel_scratch.capacity[slot] = capacity;
        _sgu_vk_accel_scratch.pad[slot] = ((mem.addr + align - 1) & ~(align - 1)) - mem.addr;
        used = 0;
    }
    _sgu_vk_accel_scratch.used[slot] = used + size;
    return _sgu_vk_accel_scratch.mem[slot].addr + _sgu_vk_accel_scratch.pad[slot] + used;
}

/* Records a build or refit into the frame's command buffer. Barriers on both
   sides order the build against earlier GPU writes (for example compute
   writes to vertex data) and against later ray queries. The first barrier
   covers compute, transfer and earlier build writes only: render pass writes
   to build inputs are not ordered. */
_SOKOL_PRIVATE bool _sgu_vk_accel_record(sg_accel a, const sg_accel_instance* inst, int count, bool refit) {
    _sg_vk_acquire_frame_command_buffers();
    VkCommandBuffer cmd = _sg.vk.frame.cmd_buf;
    if (!cmd) return false;
    VkDeviceAddress instance_addr = 0;
    if (a->is_tlas) {
        _sgu_vk_mem_t* mem = &a->native.instances[_sg.vk.frame_slot];
        VkAccelerationStructureInstanceKHR* dst = (VkAccelerationStructureInstanceKHR*)mem->map;
        for (int i = 0; i < count; i++) {
            const sg_accel_instance* s = &inst[i];
            VkAccelerationStructureInstanceKHR* d = &dst[i];
            memset(d, 0, sizeof(*d));
            memcpy(d->transform.matrix, s->transform, sizeof(d->transform.matrix));
            d->instanceCustomIndex = s->id & 0xFFFFFFu;
            d->mask = s->mask;
            d->flags = ((s->flags & SG_ACCEL_INSTANCE_OPAQUE) ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : 0) |
                       ((s->flags & SG_ACCEL_INSTANCE_NO_CULL) ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0);
            d->accelerationStructureReference = (uint64_t)a->blas_list[s->blas_index]->native.addr;
        }
        instance_addr = mem->addr;
    }
    _SG_STRUCT(VkAccelerationStructureGeometryKHR, geo);
    _SG_STRUCT(VkAccelerationStructureBuildGeometryInfoKHR, info);
    _SG_STRUCT(VkAccelerationStructureBuildRangeInfoKHR, range);
    if (!_sgu_vk_accel_fill(a, &geo, &info, &range, (uint32_t)count, instance_addr)) return false;
    const VkDeviceSize scratch_size = refit ? a->native.update_scratch : a->native.build_scratch;
    const VkDeviceAddress scratch_addr = _sgu_vk_accel_scratch_alloc(scratch_size);
    if (!scratch_addr) return false;
    info.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.srcAccelerationStructure = refit ? a->native.as : VK_NULL_HANDLE;
    info.dstAccelerationStructure = a->native.as;
    info.scratchData.deviceAddress = scratch_addr;
    const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;

    /* Build inputs are read at the build stage with SHADER_READ. */
    _sgu_vk_accel_barrier(cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_SHADER_READ_BIT |
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    _sgu_vk_accel_state.cmd_build(cmd, 1, &info, &range_ptr);
    _sgu_vk_accel_barrier(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    return true;
}

/* Checks the Vulkan binding numbers of a layout against the shader desc. */
_SOKOL_PRIVATE bool _sgu_vk_accel_validate(const sg_shader_desc* desc, const sg_accel_layout_desc* layout) {
    const int floor_n = SG_MAX_VIEW_BINDSLOTS + SG_MAX_SAMPLER_BINDSLOTS;
    for (int i = 0; i < layout->num_slots; i++) {
        const int n = layout->slots[i].vk_binding_n;
        if (n < 0) {
            _sgu_accel_log("sg_make_shader_accel: slot %d has no Vulkan binding (vk_binding_n %d)", layout->slots[i].slot, n);
            return false;
        }
        if (n < floor_n) {
            _sgu_accel_log("sg_make_shader_accel: slot %d vk_binding_n %d is below %d (SG_MAX_VIEW_BINDSLOTS + "
                "SG_MAX_SAMPLER_BINDSLOTS of this runtime). The shader compiler and the runtime use different "
                "sokol_gfx.h versions", layout->slots[i].slot, n, floor_n);
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (layout->slots[j].vk_binding_n == n) {
                _sgu_accel_log("sg_make_shader_accel: slots %d and %d share vk_binding_n %d",
                    layout->slots[j].slot, layout->slots[i].slot, n);
                return false;
            }
        }
        for (int j = 0; j < SG_MAX_VIEW_BINDSLOTS; j++) {
            const sg_shader_view* v = &desc->views[j];
            const bool hit =
                (v->texture.stage != SG_SHADERSTAGE_NONE && v->texture.spirv_set1_binding_n == n) ||
                (v->storage_buffer.stage != SG_SHADERSTAGE_NONE && v->storage_buffer.spirv_set1_binding_n == n) ||
                (v->storage_image.stage != SG_SHADERSTAGE_NONE && v->storage_image.spirv_set1_binding_n == n);
            if (hit) {
                _sgu_accel_log("sg_make_shader_accel: slot %d vk_binding_n %d collides with view %d",
                    layout->slots[i].slot, n, j);
                return false;
            }
        }
        for (int j = 0; j < SG_MAX_SAMPLER_BINDSLOTS; j++) {
            if (desc->samplers[j].stage != SG_SHADERSTAGE_NONE && desc->samplers[j].spirv_set1_binding_n == n) {
                _sgu_accel_log("sg_make_shader_accel: slot %d vk_binding_n %d collides with sampler %d",
                    layout->slots[i].slot, n, j);
                return false;
            }
        }
    }
    return true;
}

/* Rebuilds the set 1 layout and pipeline layout of a new shader with extra
   acceleration structure bindings at the numbers the layout gives.
   sg_make_shader_accel checked that they meet no view or sampler binding. The
   offsets of the existing bindings are read again from the new layout.
   No pipeline uses the layouts yet, so the old ones can be destroyed at once.
   Returns false and keeps the shader unchanged on failure. */
_SOKOL_PRIVATE bool _sgu_vk_accel_patch_shader(sg_shader id, const sg_accel_layout_desc* layout) {
    _sg_shader_t* shd = _sg_lookup_shader(id.id);
    if (!shd || shd->slot.state != SG_RESOURCESTATE_VALID || !shd->vk.compute_func.module) return false;
    VkDevice dev = _sg.vk.dev;
    VkDescriptorSetLayoutBinding entries[SG_MAX_VIEW_BINDSLOTS + SG_MAX_SAMPLER_BINDSLOTS + SG_ACCEL_MAX_SLOTS];
    memset(entries, 0, sizeof(entries));
    uint32_t n = 0;
    for (int i = 0; i < SG_MAX_VIEW_BINDSLOTS; i++) {
        if (shd->cmn.views[i].stage == SG_SHADERSTAGE_NONE) continue;
        VkDescriptorSetLayoutBinding* e = &entries[n++];
        e->binding = shd->vk.view_set1_bnd_n[i];
        e->descriptorCount = 1;
        e->stageFlags = _sg_vk_shader_stage(shd->cmn.views[i].stage);
        e->descriptorType = shd->cmn.views[i].view_type == SG_VIEWTYPE_TEXTURE ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
            : (shd->cmn.views[i].view_type == SG_VIEWTYPE_STORAGEBUFFER ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
            : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        if (shd->cmn.samplers[i].stage == SG_SHADERSTAGE_NONE) continue;
        VkDescriptorSetLayoutBinding* e = &entries[n++];
        e->binding = shd->vk.smp_set1_bnd_n[i];
        e->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        e->descriptorCount = 1;
        e->stageFlags = _sg_vk_shader_stage(shd->cmn.samplers[i].stage);
    }
    for (int i = 0; i < layout->num_slots; i++) {
        VkDescriptorSetLayoutBinding* e = &entries[n++];
        e->binding = (uint32_t)layout->slots[i].vk_binding_n;
        e->descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        e->descriptorCount = 1;
        e->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    _SG_STRUCT(VkDescriptorSetLayoutCreateInfo, dsl_info);
    dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    dsl_info.bindingCount = n;
    dsl_info.pBindings = entries;
    VkDescriptorSetLayout dsl = 0;
    if (vkCreateDescriptorSetLayout(dev, &dsl_info, 0, &dsl) != VK_SUCCESS) {
        _SG_ERROR(VULKAN_CREATE_DESCRIPTOR_SET_LAYOUT_FAILED);
        return false;
    }
    VkDescriptorSetLayout set_layouts[2] = { shd->vk.ub_dsl, dsl };
    _SG_STRUCT(VkPipelineLayoutCreateInfo, pl_info);
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 2;
    pl_info.pSetLayouts = set_layouts;
    VkPipelineLayout layout = 0;
    if (vkCreatePipelineLayout(dev, &pl_info, 0, &layout) != VK_SUCCESS) {
        _SG_ERROR(VULKAN_CREATE_PIPELINE_LAYOUT_FAILED);
        vkDestroyDescriptorSetLayout(dev, dsl, 0);
        return false;
    }
    vkDestroyPipelineLayout(dev, shd->vk.pip_layout, 0);
    vkDestroyDescriptorSetLayout(dev, shd->vk.view_smp_dsl, 0);
    shd->vk.pip_layout = layout;
    shd->vk.view_smp_dsl = dsl;
    _sg.vk.ext.get_descriptor_set_layout_size(dev, dsl, &shd->vk.view_smp_dset_size);
    for (int i = 0; i < SG_MAX_VIEW_BINDSLOTS; i++) {
        if (shd->cmn.views[i].stage == SG_SHADERSTAGE_NONE) continue;
        VkDeviceSize offset = 0;
        _sg.vk.ext.get_descriptor_set_layout_binding_offset(dev, dsl, shd->vk.view_set1_bnd_n[i], &offset);
        shd->vk.view_dset_offsets[i] = (uint16_t)offset;
    }
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        if (shd->cmn.samplers[i].stage == SG_SHADERSTAGE_NONE) continue;
        VkDeviceSize offset = 0;
        _sg.vk.ext.get_descriptor_set_layout_binding_offset(dev, dsl, shd->vk.smp_set1_bnd_n[i], &offset);
        shd->vk.smp_dset_offsets[i] = (uint16_t)offset;
    }
    return true;
}

/* True when the shader has no view or sampler, so sokol_gfx records no chunk
   for it that sg_accel_bind could reuse. */
_SOKOL_PRIVATE bool _sgu_vk_accel_shader_has_no_views(const _sg_shader_t* shd) {
    for (int i = 0; i < SG_MAX_VIEW_BINDSLOTS; i++) {
        if (shd->cmn.views[i].stage != SG_SHADERSTAGE_NONE) return false;
    }
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        if (shd->cmn.samplers[i].stage != SG_SHADERSTAGE_NONE) return false;
    }
    return true;
}

/* Writes the TLAS descriptor into a chunk of sokol's descriptor buffer. A
   shader without views gets a chunk of its own, bound here. Otherwise the
   chunk is the one the last sg_apply_bindings recorded: sokol_gfx does not
   store its offset, so it is taken as the last allocation, one rounded size
   before the buffer's current offset. A fresh chunk would need a copy of the
   old one, and finding the old one is the same guess. */
_SOKOL_PRIVATE bool _sgu_vk_accel_bind(const sg_accel a, int slot) {
    const _sg_pipeline_t* pip = _sg_pipeline_ref_ptr(&_sg.cur_pip);
    if (!pip || _sg_skipdraw_any()) return false;
    const _sg_shader_t* shd = _sg_shader_ref_ptr(&pip->cmn.shader);
    if (!shd || !shd->vk.view_smp_dset_size || !_sg.vk.bind.cur_mem_ptr || _sg.vk.bind.overflown) return false;
    const sg_accel_slot_layout* slot_layout = _sgu_accel_find_slot(shd->slot.id, slot);
    if (!slot_layout || slot_layout->vk_binding_n < 0) return false;
    const bool own_chunk = _sgu_vk_accel_shader_has_no_views(shd);
    VkDeviceSize chunk_offset = 0;
    if (own_chunk) {
        chunk_offset = _sg_vk_shared_buffer_alloc(&_sg.vk.bind, (uint32_t)shd->vk.view_smp_dset_size);
        if (_sg.vk.bind.overflown) {
            _SG_ERROR(VULKAN_DESCRIPTOR_BUFFER_OVERFLOW);
            return false;
        }
    } else {
        const uint32_t chunk_size = _sg_roundup_pow2_u32((uint32_t)shd->vk.view_smp_dset_size, _sg.vk.bind.align);
        if (_sg.vk.bind.offset < chunk_size) return false;
        chunk_offset = _sg.vk.bind.offset - chunk_size;
    }
    uint8_t* chunk = _sg_vk_shared_buffer_ptr(&_sg.vk.bind, chunk_offset);
    VkDeviceSize binding_offset = 0;
    _sg.vk.ext.get_descriptor_set_layout_binding_offset(_sg.vk.dev, shd->vk.view_smp_dsl,
        (uint32_t)slot_layout->vk_binding_n, &binding_offset);
    _SG_STRUCT(VkDescriptorGetInfoEXT, get_info);
    get_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    get_info.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    get_info.data.accelerationStructure = a->native.addr;
    /* Sokol_gfx writes descriptors into a local copy first: a direct write into
       the mapped buffer is very slow on some drivers. */
    uint8_t data[_SG_VK_MAX_DESCRIPTOR_DATA_SIZE];
    const size_t size = _sg.vk.descriptor_buffer_props.accelerationStructureDescriptorSize;
    if (size > sizeof(data)) return false;
    _sg.vk.ext.get_descriptor(_sg.vk.dev, &get_info, size, data);
    memcpy(chunk + binding_offset, data, size);
    if (own_chunk) {
        const uint32_t dbuf_index = 0;
        _sg.vk.ext.cmd_set_descriptor_buffer_offsets(_sg.vk.frame.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
            shd->vk.pip_layout, _SG_VK_VIEW_SMP_DESCRIPTORSET_INDEX, 1, &dbuf_index, &chunk_offset);
    }
    return true;
}

_SOKOL_PRIVATE void _sgu_vk_accel_shutdown(void) {
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) _sgu_vk_accel_free(&_sgu_vk_accel_scratch.mem[i]);
    memset(&_sgu_vk_accel_scratch, 0, sizeof(_sgu_vk_accel_scratch));
    memset(&_sgu_vk_accel_state, 0, sizeof(_sgu_vk_accel_state));
}

#endif /* SOKOL_VULKAN */

/*--- Acceleration Structures: Metal -----------------------------------------*/
#if defined(SOKOL_METAL)

#if !defined(__OBJC__)
#error "Metal implementation requires Objective-C"
#endif

#if __has_feature(objc_arc)
#define _SGU_MTL_RETAIN(obj) ((__bridge_retained void*)(obj))
#define _SGU_MTL_OBJ(type, ptr) ((__bridge type)(ptr))
#define _SGU_MTL_RELEASE(ptr) CFRelease((CFTypeRef)(ptr))
#define _SGU_MTL_DROP(obj) ((void)0)
#else
#define _SGU_MTL_RETAIN(obj) ((void*)[(obj) retain])
#define _SGU_MTL_OBJ(type, ptr) ((type)(ptr))
#define _SGU_MTL_RELEASE(ptr) [(id)(ptr) release]
/* Drops the +1 reference of a new* method result once it is retained. */
#define _SGU_MTL_DROP(obj) [(obj) release]
#endif

/* Metal has 31 buffer indices per stage: 0..30. */
#define _SGU_MTL_MAX_BUFFER_N (30)

/* Checks the buffer indices of a layout against the uniform blocks and storage
   buffers of the shader desc. Every stage counts, which is stricter than
   needed for a compute shader but never wrong. */
_SOKOL_PRIVATE bool _sgu_mtl_accel_validate(const sg_shader_desc* desc, const sg_accel_layout_desc* layout) {
    for (int i = 0; i < layout->num_slots; i++) {
        const int n = layout->slots[i].msl_buffer_n;
        /* sokol_gfx binds the frame's uniform buffer at indices
           0..SG_MAX_UNIFORMBLOCK_BINDSLOTS-1 once per pass, and sg_apply_uniforms
           then only moves the offset (_sg_mtl_bind_uniform_buffers,
           _sg_mtl_apply_uniforms). A TLAS at one of those indices would replace
           the uniform buffer for every later shader of the pass. */
        if (n < SG_MAX_UNIFORMBLOCK_BINDSLOTS || n > _SGU_MTL_MAX_BUFFER_N) {
            _sgu_accel_log("sg_make_shader_accel: slot %d msl_buffer_n %d is not in %d..%d",
                layout->slots[i].slot, n, SG_MAX_UNIFORMBLOCK_BINDSLOTS, _SGU_MTL_MAX_BUFFER_N);
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (layout->slots[j].msl_buffer_n == n) {
                _sgu_accel_log("sg_make_shader_accel: slots %d and %d share msl_buffer_n %d",
                    layout->slots[j].slot, layout->slots[i].slot, n);
                return false;
            }
        }
        for (int j = 0; j < SG_MAX_UNIFORMBLOCK_BINDSLOTS; j++) {
            const sg_shader_uniform_block* ub = &desc->uniform_blocks[j];
            if (ub->stage != SG_SHADERSTAGE_NONE && ub->msl_buffer_n == n) {
                _sgu_accel_log("sg_make_shader_accel: slot %d msl_buffer_n %d collides with uniform block %d",
                    layout->slots[i].slot, n, j);
                return false;
            }
        }
        for (int j = 0; j < SG_MAX_VIEW_BINDSLOTS; j++) {
            const sg_shader_storage_buffer_view* sbuf = &desc->views[j].storage_buffer;
            if (sbuf->stage != SG_SHADERSTAGE_NONE && sbuf->msl_buffer_n == n) {
                _sgu_accel_log("sg_make_shader_accel: slot %d msl_buffer_n %d collides with storage buffer view %d",
                    layout->slots[i].slot, n, j);
                return false;
            }
        }
    }
    return true;
}

/* Offset alignment of builds inside the shared scratch buffer. */
#define _SGU_MTL_ACCEL_SCRATCH_ALIGN (256)

/* Sokol's command buffer does not retain resources by default. A destroyed
   object waits here until every frame that could use it has completed. */
typedef struct {
    void* obj;
    uint32_t frame;
} _sgu_mtl_accel_garbage_t;

static struct {
    _sgu_mtl_accel_garbage_t* items;
    int count;
    int capacity;
} _sgu_mtl_accel_garbage;

_SOKOL_PRIVATE void _sgu_mtl_accel_collect(bool all) {
    int keep = 0;
    for (int i = 0; i < _sgu_mtl_accel_garbage.count; i++) {
        _sgu_mtl_accel_garbage_t item = _sgu_mtl_accel_garbage.items[i];
        if (all || (uint32_t)(_sg.frame_index - item.frame) > (uint32_t)SG_NUM_INFLIGHT_FRAMES) {
            _SGU_MTL_RELEASE(item.obj);
        } else {
            _sgu_mtl_accel_garbage.items[keep++] = item;
        }
    }
    _sgu_mtl_accel_garbage.count = keep;
}

_SOKOL_PRIVATE void _sgu_mtl_accel_defer(void* obj) {
    if (!obj) return;
    if (_sgu_mtl_accel_garbage.count == _sgu_mtl_accel_garbage.capacity) {
        const int capacity = _sgu_mtl_accel_garbage.capacity ? _sgu_mtl_accel_garbage.capacity * 2 : 32;
        _sgu_mtl_accel_garbage_t* items = (_sgu_mtl_accel_garbage_t*)realloc(
            _sgu_mtl_accel_garbage.items, sizeof(_sgu_mtl_accel_garbage_t) * (size_t)capacity);
        if (!items) {
            /* Out of memory: leak the object, never free it while in flight. */
            return;
        }
        _sgu_mtl_accel_garbage.items = items;
        _sgu_mtl_accel_garbage.capacity = capacity;
    }
    _sgu_mtl_accel_garbage_t* item = &_sgu_mtl_accel_garbage.items[_sgu_mtl_accel_garbage.count++];
    item->obj = obj;
    item->frame = _sg.frame_index;
}

/* One private scratch buffer per frame ring slot. It only grows. Builds of one
   frame take consecutive aligned ranges; Metal's hazard tracking orders the
   encoders that share it. A replaced buffer waits in the garbage list, because
   command buffers already recorded this frame still use it. */
static struct {
    void* buf[SG_NUM_INFLIGHT_FRAMES];  /* retained id<MTLBuffer> */
    uint64_t capacity[SG_NUM_INFLIGHT_FRAMES];
    uint64_t used[SG_NUM_INFLIGHT_FRAMES];
    uint32_t frame[SG_NUM_INFLIGHT_FRAMES];
} _sgu_mtl_accel_scratch;

/* Returns the scratch buffer and sets the offset for size bytes, or nil. */
_SOKOL_PRIVATE id<MTLBuffer> _sgu_mtl_accel_scratch_alloc(uint64_t size, NSUInteger* offset) {
    const uint32_t slot = _sg.mtl.cur_frame_rotate_index;
    if (size < 16) size = 16;
    if (_sgu_mtl_accel_scratch.frame[slot] != _sg.frame_index) {
        _sgu_mtl_accel_scratch.frame[slot] = _sg.frame_index;
        _sgu_mtl_accel_scratch.used[slot] = 0;
    }
    uint64_t used = (_sgu_mtl_accel_scratch.used[slot] + _SGU_MTL_ACCEL_SCRATCH_ALIGN - 1) &
        ~(uint64_t)(_SGU_MTL_ACCEL_SCRATCH_ALIGN - 1);
    if (!_sgu_mtl_accel_scratch.buf[slot] || used + size > _sgu_mtl_accel_scratch.capacity[slot]) {
        uint64_t capacity = _sgu_mtl_accel_scratch.capacity[slot] * 2;
        if (capacity < size) capacity = size;
        id<MTLBuffer> buffer = [_sg.mtl.device newBufferWithLength:(NSUInteger)capacity options:MTLResourceStorageModePrivate];
        if (nil == buffer) return nil;
        _sgu_mtl_accel_defer(_sgu_mtl_accel_scratch.buf[slot]);
        _sgu_mtl_accel_scratch.buf[slot] = _SGU_MTL_RETAIN(buffer);
        _SGU_MTL_DROP(buffer);
        _sgu_mtl_accel_scratch.capacity[slot] = capacity;
        used = 0;
    }
    _sgu_mtl_accel_scratch.used[slot] = used + size;
    *offset = (NSUInteger)used;
    return _SGU_MTL_OBJ(id<MTLBuffer>, _sgu_mtl_accel_scratch.buf[slot]);
}

_SOKOL_PRIVATE bool _sgu_mtl_accel_supported(void) {
    if (!_sg.mtl.device) return false;
    if (@available(macOS 12.0, iOS 15.0, *)) {
        return [_sg.mtl.device supportsRaytracing];
    }
    return false;
}

/* Same frame command buffer setup as sokol_gfx's first pass of a frame, so
   builds can run before any pass. sokol_gfx then reuses this buffer. */
_SOKOL_PRIVATE bool _sgu_mtl_accel_acquire_cmd_buffer(void) {
    if (!_sg.mtl.cmd_queue) return false;
    if (nil == _sg.mtl.cmd_buffer) {
        dispatch_semaphore_wait(_sg.mtl.sem, DISPATCH_TIME_FOREVER);
        if (_sg.desc.metal.use_command_buffer_with_retained_references) {
            _sg.mtl.cmd_buffer = [_sg.mtl.cmd_queue commandBuffer];
        } else {
            _sg.mtl.cmd_buffer = [_sg.mtl.cmd_queue commandBufferWithUnretainedReferences];
        }
        [_sg.mtl.cmd_buffer enqueue];
        [_sg.mtl.cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> cmd_buf) {
            /* This block runs on another thread. */
            _SOKOL_UNUSED(cmd_buf);
            dispatch_semaphore_signal(_sg.mtl.sem);
        }];
    }
    return true;
}

_SOKOL_PRIVATE MTLPrimitiveAccelerationStructureDescriptor* _sgu_mtl_accel_blas_descriptor(const sg_accel a) API_AVAILABLE(macos(12.0), ios(15.0)) {
    _sg_buffer_t* vb = 0;
    _sg_buffer_t* ib = 0;
    if (!_sgu_accel_blas_buffers(a, &vb, &ib)) return nil;
    const sg_blas_desc* d = &a->blas;
    MTLAccelerationStructureTriangleGeometryDescriptor* geo = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
    geo.vertexBuffer = _sg_mtl_id(vb->mtl.buf[vb->cmn.active_slot]);
    geo.vertexBufferOffset = (NSUInteger)d->vertex_offset;
    geo.vertexStride = (NSUInteger)d->vertex_stride;
    geo.triangleCount = (NSUInteger)_sgu_accel_triangle_count(a);
    geo.opaque = d->opaque ? YES : NO;
    if (ib) {
        geo.indexBuffer = _sg_mtl_id(ib->mtl.buf[ib->cmn.active_slot]);
        geo.indexBufferOffset = (NSUInteger)d->index_offset;
        geo.indexType = (d->index_format == SG_ACCEL_INDEXFORMAT_UINT16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    }
    MTLPrimitiveAccelerationStructureDescriptor* desc = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    desc.geometryDescriptors = @[geo];
    desc.usage = a->allow_update ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone;
    return desc;
}

_SOKOL_PRIVATE MTLInstanceAccelerationStructureDescriptor* _sgu_mtl_accel_tlas_descriptor(const sg_accel a, uint32_t ring, int count) API_AVAILABLE(macos(12.0), ios(15.0)) {
    MTLInstanceAccelerationStructureDescriptor* desc = [MTLInstanceAccelerationStructureDescriptor descriptor];
    desc.instanceCount = (NSUInteger)count;
    desc.instanceDescriptorBuffer = _SGU_MTL_OBJ(id<MTLBuffer>, a->native.instances[ring]);
    desc.instanceDescriptorBufferOffset = 0;
    desc.instanceDescriptorStride = sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
    desc.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeUserID;
    desc.instancedAccelerationStructures = _SGU_MTL_OBJ(NSArray*, a->native.blas_array);
    desc.usage = a->allow_update ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone;
    return desc;
}

_SOKOL_PRIVATE bool _sgu_mtl_accel_make(sg_accel a, const char* label) {
    if (@available(macOS 12.0, iOS 15.0, *)) @autoreleasepool {
        _sgu_mtl_accel_collect(false);
        id<MTLDevice> dev = _sg.mtl.device;
        MTLAccelerationStructureDescriptor* desc = nil;
        if (a->is_tlas) {
            NSMutableArray* array = [NSMutableArray arrayWithCapacity:(NSUInteger)a->blas_count];
            for (int i = 0; i < a->blas_count; i++) {
                [array addObject:_SGU_MTL_OBJ(id, a->blas_list[i]->native.as)];
            }
            a->native.blas_array = _SGU_MTL_RETAIN(array);
            const NSUInteger bytes = (NSUInteger)a->max_instances * sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
            for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) {
                id<MTLBuffer> buffer = [dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
                if (nil == buffer) return false;
                a->native.instances[i] = _SGU_MTL_RETAIN(buffer);
                _SGU_MTL_DROP(buffer);
            }
            desc = _sgu_mtl_accel_tlas_descriptor(a, 0, a->max_instances);
        } else {
            desc = _sgu_mtl_accel_blas_descriptor(a);
        }
        if (nil == desc) return false;
        const MTLAccelerationStructureSizes sizes = [dev accelerationStructureSizesWithDescriptor:desc];
        id<MTLAccelerationStructure> as = [dev newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        if (nil == as) return false;
        #if defined(SOKOL_DEBUG)
        if (label) as.label = [NSString stringWithUTF8String:label];
        #else
        (void)label;
        #endif
        a->native.as = _SGU_MTL_RETAIN(as);
        _SGU_MTL_DROP(as);
        a->native.build_scratch = (uint64_t)sizes.buildScratchBufferSize;
        a->native.refit_scratch = (uint64_t)sizes.refitScratchBufferSize;
        return true;
    }
    (void)label;
    return false;
}

_SOKOL_PRIVATE void _sgu_mtl_accel_destroy(sg_accel a) {
    _sgu_mtl_accel_collect(false);
    _sgu_mtl_accel_defer(a->native.as);
    _sgu_mtl_accel_defer(a->native.blas_array);
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) _sgu_mtl_accel_defer(a->native.instances[i]);
    memset(&a->native, 0, sizeof(a->native));
}

/* One-shot report of the first TLAS build and the binds that follow it. It
   settles from one run whether the GPU built the structures and whether the
   compute encoder gets the same TLAS. After the first full TLAS build, the
   same encoder writes the compacted sizes of the TLAS and of its first
   _SGU_MTL_ACCEL_REPORT_BLAS BLASes into a shared buffer. A compacted size is
   0 for a structure that was never built. The command buffer's completed
   handler, which runs on a Metal thread, only copies the sizes and the status;
   the next sg_accel_bind logs them on the sokol thread. */
#define _SGU_MTL_ACCEL_REPORT_BLAS (8)
static struct {
    bool build_logged;          /* the first TLAS build was logged and armed */
    bool bind_logged;           /* the first bind after that build was logged */
    bool result_logged;         /* the GPU result was logged */
    void* tlas;                 /* id<MTLAccelerationStructure> of the reported build, not retained */
    void* sizes;                /* retained id<MTLBuffer>, uint32_t per structure */
    int blas_reported;
    volatile int done;          /* set by the completed handler */
    volatile int status;        /* MTLCommandBufferStatus */
    volatile long error_code;   /* NSError code, 0 for none */
    uint32_t compacted[1 + _SGU_MTL_ACCEL_REPORT_BLAS];
} _sgu_mtl_accel_report;

/* Logs the CPU side of the first TLAS build and arms the GPU readback on its
   encoder. Called after the build is encoded and before endEncoding. */
_SOKOL_PRIVATE void _sgu_mtl_accel_report_build(sg_accel a, int count, uint32_t ring,
    id<MTLAccelerationStructureCommandEncoder> encoder) API_AVAILABLE(macos(12.0), ios(15.0)) {
    _sgu_mtl_accel_report.build_logged = true;
    id<MTLAccelerationStructure> tlas = _SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->native.as);
    id<MTLBuffer> instances = _SGU_MTL_OBJ(id<MTLBuffer>, a->native.instances[ring]);
    _sgu_accel_log("accel report: TLAS %p size %llu, %d of %d instances, %d BLASes, ring %u, frame %u, "
        "instance stride %d, instance buffer %llu bytes", (void*)tlas, (unsigned long long)tlas.size, count,
        a->max_instances, a->blas_count, ring, _sg.frame_index,
        (int)sizeof(MTLAccelerationStructureUserIDInstanceDescriptor), (unsigned long long)instances.length);
    const MTLAccelerationStructureUserIDInstanceDescriptor* d =
        (const MTLAccelerationStructureUserIDInstanceDescriptor*)[instances contents];
    for (int i = 0; i < count && i < 2; i++) {
        const MTLPackedFloat4x3* m = &d[i].transformationMatrix;
        _sgu_accel_log("accel report: instance %d blas %u user %u mask 0x%x options 0x%x ift %u "
            "c0 (%g %g %g) c1 (%g %g %g) c2 (%g %g %g) c3 (%g %g %g)", i, d[i].accelerationStructureIndex,
            d[i].userID, d[i].mask, (unsigned)d[i].options, d[i].intersectionFunctionTableOffset,
            m->columns[0].x, m->columns[0].y, m->columns[0].z, m->columns[1].x, m->columns[1].y, m->columns[1].z,
            m->columns[2].x, m->columns[2].y, m->columns[2].z, m->columns[3].x, m->columns[3].y, m->columns[3].z);
    }
    for (int i = 0; i < a->blas_count; i++) {
        const sg_accel b = a->blas_list[i];
        _sg_buffer_t* vb = 0;
        _sg_buffer_t* ib = 0;
        const bool inputs = _sgu_accel_blas_buffers(b, &vb, &ib);
        _sgu_accel_log("accel report: BLAS %d %p size %llu built %d triangles %d vertices %d stride %d offset %d "
            "index format %d count %d offset %d, inputs %s (vertex buffer %d bytes)", i, b->native.as,
            (unsigned long long)_SGU_MTL_OBJ(id<MTLAccelerationStructure>, b->native.as).size, (int)b->built,
            _sgu_accel_triangle_count(b), b->blas.vertex_count, b->blas.vertex_stride, b->blas.vertex_offset,
            (int)b->blas.index_format, b->blas.index_count, b->blas.index_offset, inputs ? "valid" : "INVALID",
            vb ? (int)vb->cmn.size : -1);
    }
    const int blas_n = a->blas_count < _SGU_MTL_ACCEL_REPORT_BLAS ? a->blas_count : _SGU_MTL_ACCEL_REPORT_BLAS;
    id<MTLBuffer> sizes = [_sg.mtl.device newBufferWithLength:sizeof(uint32_t) * (1 + _SGU_MTL_ACCEL_REPORT_BLAS)
        options:MTLResourceStorageModeShared];
    if (nil == sizes) return;
    memset([sizes contents], 0, sizes.length);
    [encoder writeCompactedAccelerationStructureSize:tlas toBuffer:sizes offset:0];
    for (int i = 0; i < blas_n; i++) {
        [encoder writeCompactedAccelerationStructureSize:_SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->blas_list[i]->native.as)
            toBuffer:sizes offset:(NSUInteger)(sizeof(uint32_t) * (size_t)(1 + i))];
    }
    _sgu_mtl_accel_report.tlas = a->native.as;
    _sgu_mtl_accel_report.sizes = _SGU_MTL_RETAIN(sizes);
    _sgu_mtl_accel_report.blas_reported = blas_n;
    /* The block holds its own reference to sizes, so sg_accel_shutdown may
       release the report's reference first. */
    [_sg.mtl.cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> cmd_buf) {
        /* Metal thread: copy only, the sokol thread logs. */
        const uint32_t* src = (const uint32_t*)[sizes contents];
        for (int i = 0; i < 1 + _SGU_MTL_ACCEL_REPORT_BLAS; i++) _sgu_mtl_accel_report.compacted[i] = src[i];
        _sgu_mtl_accel_report.status = (int)cmd_buf.status;
        _sgu_mtl_accel_report.error_code = cmd_buf.error ? (long)cmd_buf.error.code : 0;
        __atomic_store_n(&_sgu_mtl_accel_report.done, 1, __ATOMIC_RELEASE);
    }];
    _SGU_MTL_DROP(sizes);
}

/* Logs the first bind after the reported build, then the GPU result once the
   completed handler has run. Called from sg_accel_bind on the sokol thread. */
_SOKOL_PRIVATE void _sgu_mtl_accel_report_bind(const sg_accel a, int slot, int buffer_n) {
    if (!_sgu_mtl_accel_report.build_logged) return;
    if (!_sgu_mtl_accel_report.bind_logged) {
        _sgu_mtl_accel_report.bind_logged = true;
        _sgu_accel_log("accel report: first bind TLAS %p (reported build %p, %s) slot %d buffer %d, built %d, "
            "%d BLASes resident, frame %u", a->native.as, _sgu_mtl_accel_report.tlas,
            a->native.as == _sgu_mtl_accel_report.tlas ? "same" : "DIFFERENT", slot, buffer_n, (int)a->built,
            a->blas_count, _sg.frame_index);
    }
    if (_sgu_mtl_accel_report.result_logged || !__atomic_load_n(&_sgu_mtl_accel_report.done, __ATOMIC_ACQUIRE)) return;
    _sgu_mtl_accel_report.result_logged = true;
    _sgu_accel_log("accel report: build command buffer status %d (4 completed, 5 error) error %ld, "
        "TLAS compacted size %u (0 means never built)", _sgu_mtl_accel_report.status,
        _sgu_mtl_accel_report.error_code, _sgu_mtl_accel_report.compacted[0]);
    for (int i = 0; i < _sgu_mtl_accel_report.blas_reported; i++) {
        _sgu_accel_log("accel report: BLAS %d compacted size %u", i, _sgu_mtl_accel_report.compacted[1 + i]);
    }
    _sgu_mtl_accel_defer(_sgu_mtl_accel_report.sizes);
    _sgu_mtl_accel_report.sizes = 0;
}

/* Records a build or refit on an acceleration structure encoder in the frame's
   command buffer. Encoders run in order, so later compute passes see the result. */
_SOKOL_PRIVATE bool _sgu_mtl_accel_record(sg_accel a, const sg_accel_instance* inst, int count, bool refit) {
    if (@available(macOS 12.0, iOS 15.0, *)) @autoreleasepool {
        _sgu_mtl_accel_collect(false);
        if (!_sgu_mtl_accel_acquire_cmd_buffer()) return false;
        /* sg_copy_buffer_to_buffer and the other sg_copy_* calls leave sokol_gfx's
           blit encoder open until the next pass or sg_commit. A command buffer
           takes one active encoder at a time, so an acceleration structure
           encoder made while it is open records nothing that runs, and every
           ray then misses. Close it first, as Unreal ends the active encoder
           before acceleration structure encoding
           (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRayTracing.cpp:2426-2436).
           sokol_gfx opens a new one at the next copy. */
        _sg_mtl_finish_blit_cmd_encoder();
        MTLAccelerationStructureDescriptor* desc = nil;
        if (a->is_tlas) {
            const uint32_t ring = _sg.mtl.cur_frame_rotate_index;
            id<MTLBuffer> buffer = _SGU_MTL_OBJ(id<MTLBuffer>, a->native.instances[ring]);
            MTLAccelerationStructureUserIDInstanceDescriptor* dst = (MTLAccelerationStructureUserIDInstanceDescriptor*)[buffer contents];
            for (int i = 0; i < count; i++) {
                const sg_accel_instance* s = &inst[i];
                MTLAccelerationStructureUserIDInstanceDescriptor* d = &dst[i];
                for (int c = 0; c < 4; c++) {
                    d->transformationMatrix.columns[c] = MTLPackedFloat3Make(s->transform[c], s->transform[4 + c], s->transform[8 + c]);
                }
                uint32_t options = 0;
                if (s->flags & SG_ACCEL_INSTANCE_OPAQUE) options |= (uint32_t)MTLAccelerationStructureInstanceOptionOpaque;
                if (s->flags & SG_ACCEL_INSTANCE_NO_CULL) options |= (uint32_t)MTLAccelerationStructureInstanceOptionDisableTriangleCulling;
                d->options = (MTLAccelerationStructureInstanceOptions)options;
                d->mask = s->mask;
                d->intersectionFunctionTableOffset = 0;
                d->accelerationStructureIndex = (uint32_t)s->blas_index;
                d->userID = s->id & 0xFFFFFFu;
            }
            desc = _sgu_mtl_accel_tlas_descriptor(a, ring, count);
        } else {
            desc = _sgu_mtl_accel_blas_descriptor(a);
        }
        if (nil == desc) return false;
        const uint64_t scratch_size = refit ? a->native.refit_scratch : a->native.build_scratch;
        NSUInteger scratch_offset = 0;
        id<MTLBuffer> scratch = _sgu_mtl_accel_scratch_alloc(scratch_size, &scratch_offset);
        if (nil == scratch) return false;
        id<MTLAccelerationStructure> as = _SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->native.as);
        id<MTLAccelerationStructureCommandEncoder> encoder = [_sg.mtl.cmd_buffer accelerationStructureCommandEncoder];
        if (a->is_tlas) {
            /* The command buffer may not retain resources: name every BLAS. */
            for (int i = 0; i < a->blas_count; i++) {
                [encoder useResource:_SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->blas_list[i]->native.as) usage:MTLResourceUsageRead];
            }
        }
        if (refit) {
            [encoder refitAccelerationStructure:as descriptor:desc destination:as scratchBuffer:scratch scratchBufferOffset:scratch_offset];
        } else {
            [encoder buildAccelerationStructure:as descriptor:desc scratchBuffer:scratch scratchBufferOffset:scratch_offset];
        }
        if (a->is_tlas && !refit && !_sgu_mtl_accel_report.build_logged) {
            _sgu_mtl_accel_report_build(a, count, _sg.mtl.cur_frame_rotate_index, encoder);
        }
        [encoder endEncoding];
        return true;
    }
    (void)a; (void)inst; (void)count; (void)refit;
    return false;
}

/* Binds the TLAS at the buffer index recorded for the slot of the current
   pipeline's shader and marks it and its BLASes resident. */
_SOKOL_PRIVATE bool _sgu_mtl_accel_bind(const sg_accel a, int slot) {
    if (@available(macOS 12.0, iOS 15.0, *)) {
        if (nil == _sg.mtl.compute_cmd_encoder) return false;
        const _sg_pipeline_t* pip = _sg_pipeline_ref_ptr(&_sg.cur_pip);
        if (!pip) return false;
        const _sg_shader_t* shd = _sg_shader_ref_ptr(&pip->cmn.shader);
        if (!shd) return false;
        const sg_accel_slot_layout* slot_layout = _sgu_accel_find_slot(shd->slot.id, slot);
        if (!slot_layout || slot_layout->msl_buffer_n < 0) return false;
        id<MTLComputeCommandEncoder> encoder = _sg.mtl.compute_cmd_encoder;
        id<MTLAccelerationStructure> tlas = _SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->native.as);
        const int buffer_n = slot_layout->msl_buffer_n;
        [encoder setAccelerationStructure:tlas atBufferIndex:(NSUInteger)buffer_n];
        /* sokol_gfx caches the storage buffer at each compute buffer index and
           skips setBuffer, or sets only the offset, when the cache matches. The
           TLAS now sits at buffer_n, so clear that entry: the next storage
           buffer at buffer_n then gets a full setBuffer. Unreal clears its
           buffer binding the same way when it sets an acceleration structure
           (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1446-1456,
           MetalCommandEncoder.cpp:1185-1189). */
        memset(&_sg.mtl.cache.cur_csbufs[buffer_n], 0, sizeof(_sg.mtl.cache.cur_csbufs[buffer_n]));
        [encoder useResource:tlas usage:MTLResourceUsageRead];
        for (int i = 0; i < a->blas_count; i++) {
            [encoder useResource:_SGU_MTL_OBJ(id<MTLAccelerationStructure>, a->blas_list[i]->native.as) usage:MTLResourceUsageRead];
        }
        _sgu_mtl_accel_report_bind(a, slot, buffer_n);
        return true;
    }
    (void)a; (void)slot;
    return false;
}

_SOKOL_PRIVATE void _sgu_mtl_accel_shutdown(void) {
    for (int i = 0; i < SG_NUM_INFLIGHT_FRAMES; i++) _sgu_mtl_accel_defer(_sgu_mtl_accel_scratch.buf[i]);
    memset(&_sgu_mtl_accel_scratch, 0, sizeof(_sgu_mtl_accel_scratch));
    _sgu_mtl_accel_defer(_sgu_mtl_accel_report.sizes);
    memset(&_sgu_mtl_accel_report, 0, sizeof(_sgu_mtl_accel_report));
    _sgu_mtl_accel_collect(true);
    free(_sgu_mtl_accel_garbage.items);
    memset(&_sgu_mtl_accel_garbage, 0, sizeof(_sgu_mtl_accel_garbage));
}

#endif /* SOKOL_METAL */

/*--- Acceleration Structures: Backend Dispatch ------------------------------*/
#if defined(SOKOL_VULKAN)
#define _sgu_accel_native_supported _sgu_vk_accel_supported
#define _sgu_accel_native_make _sgu_vk_accel_make
#define _sgu_accel_native_destroy _sgu_vk_accel_destroy
#define _sgu_accel_native_record _sgu_vk_accel_record
#define _sgu_accel_native_bind _sgu_vk_accel_bind
#define _sgu_accel_native_shutdown _sgu_vk_accel_shutdown
#elif defined(SOKOL_METAL)
#define _sgu_accel_native_supported _sgu_mtl_accel_supported
#define _sgu_accel_native_make _sgu_mtl_accel_make
#define _sgu_accel_native_destroy _sgu_mtl_accel_destroy
#define _sgu_accel_native_record _sgu_mtl_accel_record
#define _sgu_accel_native_bind _sgu_mtl_accel_bind
#define _sgu_accel_native_shutdown _sgu_mtl_accel_shutdown
#else
/* D3D11, WebGPU, GL and the dummy backend report no support. */
_SOKOL_PRIVATE bool _sgu_accel_native_supported(void) { return false; }
_SOKOL_PRIVATE bool _sgu_accel_native_make(sg_accel a, const char* label) { (void)a; (void)label; return false; }
_SOKOL_PRIVATE void _sgu_accel_native_destroy(sg_accel a) { (void)a; }
_SOKOL_PRIVATE bool _sgu_accel_native_record(sg_accel a, const sg_accel_instance* inst, int count, bool refit) {
    (void)a; (void)inst; (void)count; (void)refit; return false;
}
_SOKOL_PRIVATE bool _sgu_accel_native_bind(const sg_accel a, int slot) { (void)a; (void)slot; return false; }
_SOKOL_PRIVATE void _sgu_accel_native_shutdown(void) {}
#endif

/*--- Acceleration Structures: Public API ------------------------------------*/

/* True between sg_accel_shutdown and the next successful sg_make_blas or
   sg_make_tlas. */
static bool _sgu_accel_down;

SOKOL_API_IMPL bool sg_ray_query_supported(void) {
    return sg_isvalid() && _sgu_accel_native_supported();
}

SOKOL_API_IMPL sg_buffer sg_make_accel_input_buffer(const sg_buffer_desc* desc) {
    sg_buffer invalid = { SG_INVALID_ID };
    if (!desc || !sg_ray_query_supported() || _sg.cur_pass.in_pass) return invalid;
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
    buf->slot.state = _sgu_vk_create_indirect_buffer(buf, &d, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    _sg_resource_stats_inc(buffers.inited);
    _SG_TRACE_ARGS(init_buffer, result, &d);
    return result;
    #else
    return sg_make_buffer(&d);
    #endif
}

SOKOL_API_IMPL sg_accel sg_make_blas(const sg_blas_desc* desc) {
    if (!desc || !sg_ray_query_supported()) return NULL;
    const sg_blas_desc* d = desc;
    if ((int)d->index_format < (int)SG_ACCEL_INDEXFORMAT_NONE || (int)d->index_format > (int)SG_ACCEL_INDEXFORMAT_UINT32) return NULL;
    if (d->vertex_count < 3 || d->vertex_stride < 12 || (d->vertex_stride & 3) ||
        d->vertex_offset < 0 || (d->vertex_offset & 3)) return NULL;
    if (d->index_format == SG_ACCEL_INDEXFORMAT_NONE) {
        if (d->vertex_count % 3) return NULL;
    } else if (d->index_count < 3 || (d->index_count % 3) || d->index_offset < 0 ||
               (d->index_offset % _sgu_accel_index_size(d->index_format))) {
        return NULL;
    }
    sg_accel a = (sg_accel)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->blas = *d;
    a->blas.label = NULL;
    a->allow_update = d->allow_update;
    if (!_sgu_accel_native_make(a, d->label)) {
        _sgu_accel_native_destroy(a);
        free(a);
        return NULL;
    }
    _sgu_accel_down = false;
    return a;
}

SOKOL_API_IMPL sg_accel sg_make_tlas(const sg_tlas_desc* desc) {
    if (!desc || !sg_ray_query_supported() || !desc->blas || desc->blas_count < 1 || desc->max_instances < 1) return NULL;
    for (int i = 0; i < desc->blas_count; i++) {
        if (!desc->blas[i] || desc->blas[i]->is_tlas || desc->blas[i]->destroy_requested) return NULL;
    }
    sg_accel a = (sg_accel)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->blas_list = (sg_accel*)malloc(sizeof(sg_accel) * (size_t)desc->blas_count);
    if (!a->blas_list) { free(a); return NULL; }
    memcpy(a->blas_list, desc->blas, sizeof(sg_accel) * (size_t)desc->blas_count);
    a->is_tlas = true;
    a->allow_update = desc->allow_update;
    a->blas_count = desc->blas_count;
    a->max_instances = desc->max_instances;
    if (!_sgu_accel_native_make(a, desc->label)) {
        _sgu_accel_native_destroy(a);
        free(a->blas_list);
        free(a);
        return NULL;
    }
    for (int i = 0; i < a->blas_count; i++) a->blas_list[i]->refs++;
    _sgu_accel_down = false;
    return a;
}

_SOKOL_PRIVATE void _sgu_accel_release(sg_accel a) {
    _sgu_accel_native_destroy(a);
    if (a->is_tlas) {
        for (int i = 0; i < a->blas_count; i++) {
            sg_accel blas = a->blas_list[i];
            if (--blas->refs == 0 && blas->destroy_requested) _sgu_accel_release(blas);
        }
        free(a->blas_list);
    }
    free(a);
}

SOKOL_API_IMPL void sg_destroy_accel(sg_accel accel) {
    /* The native device is gone after sg_accel_shutdown. */
    if (!accel || _sgu_accel_down) return;
    if (accel->refs > 0) {
        accel->destroy_requested = true;
        return;
    }
    _sgu_accel_release(accel);
}

_SOKOL_PRIVATE bool _sgu_accel_can_record(const sg_accel a, bool want_tlas) {
    return a && a->is_tlas == want_tlas && !a->destroy_requested &&
        sg_ray_query_supported() && !_sg.cur_pass.in_pass;
}

SOKOL_API_IMPL bool sg_build_blas(sg_accel blas) {
    if (!_sgu_accel_can_record(blas, false)) return false;
    if (!_sgu_accel_native_record(blas, 0, 0, false)) return false;
    blas->built = true;
    return true;
}

SOKOL_API_IMPL bool sg_refit_blas(sg_accel blas) {
    if (!_sgu_accel_can_record(blas, false) || !blas->built || !blas->allow_update) return false;
    return _sgu_accel_native_record(blas, 0, 0, true);
}

/* A TLAS owns one instance buffer slot per frame in flight, so it takes one
   build or refit per frame. */
_SOKOL_PRIVATE bool _sgu_accel_record_tlas(sg_accel tlas, const sg_accel_instance* instances, int count, bool refit) {
    if (!_sgu_accel_can_record(tlas, true) || !instances || count < 1 || count > tlas->max_instances) return false;
    if (refit && (!tlas->built || !tlas->allow_update || count != tlas->built_count)) return false;
    if (tlas->recorded && tlas->last_frame == _sg.frame_index) return false;
    for (int i = 0; i < count; i++) {
        const int index = instances[i].blas_index;
        if (index < 0 || index >= tlas->blas_count || !tlas->blas_list[index]->built) return false;
    }
    if (!_sgu_accel_native_record(tlas, instances, count, refit)) return false;
    tlas->built = true;
    tlas->built_count = count;
    tlas->recorded = true;
    tlas->last_frame = _sg.frame_index;
    return true;
}

SOKOL_API_IMPL bool sg_build_tlas(sg_accel tlas, const sg_accel_instance* instances, int count) {
    return _sgu_accel_record_tlas(tlas, instances, count, false);
}

SOKOL_API_IMPL bool sg_refit_tlas(sg_accel tlas, const sg_accel_instance* instances, int count) {
    return _sgu_accel_record_tlas(tlas, instances, count, true);
}

SOKOL_API_IMPL bool sg_accel_built(sg_accel accel) {
    return accel && accel->built;
}

SOKOL_API_IMPL sg_shader sg_make_shader_accel(const sg_shader_desc* desc, const sg_accel_layout_desc* layout) {
    const sg_shader invalid = { SG_INVALID_ID };
    sg_accel_layout_desc none;
    memset(&none, 0, sizeof(none));
    if (!layout) layout = &none;
    if (!desc || !_sgu_accel_validate_slots(layout)) return invalid;
    #if defined(SOKOL_VULKAN)
    if (!_sgu_vk_accel_validate(desc, layout)) return invalid;
    #elif defined(SOKOL_METAL)
    if (!_sgu_mtl_accel_validate(desc, layout)) return invalid;
    #endif
    #if defined(SOKOL_VULKAN) || defined(SOKOL_METAL)
    if (layout->num_slots > 0) {
        _sgu_accel_prune_shaders();
        if (_sgu_accel_shaders.count >= _SGU_ACCEL_MAX_SHADERS) {
            _sgu_accel_log("sg_make_shader_accel: more than %d live shaders with acceleration structures", _SGU_ACCEL_MAX_SHADERS);
            return invalid;
        }
    }
    #endif
    sg_shader shader = sg_make_shader(desc);
    #if defined(SOKOL_VULKAN)
    if (layout->num_slots > 0) {
        if (!sg_ray_query_supported() || !_sgu_vk_accel_patch_shader(shader, layout)) {
            sg_destroy_shader(shader);
            return invalid;
        }
    }
    #endif
    #if defined(SOKOL_VULKAN) || defined(SOKOL_METAL)
    if (layout->num_slots > 0 && shader.id != SG_INVALID_ID) {
        _sgu_accel_shaders.id[_sgu_accel_shaders.count] = shader.id;
        _sgu_accel_shaders.layout[_sgu_accel_shaders.count] = *layout;
        _sgu_accel_shaders.count++;
    }
    #endif
    return shader;
}

SOKOL_API_IMPL bool sg_accel_bind(sg_accel tlas, int slot) {
    if (!tlas || !tlas->is_tlas || !tlas->built || slot < 0 || slot >= SG_ACCEL_MAX_SLOTS || !sg_ray_query_supported()) return false;
    if (!_sg.cur_pass.valid || !_sg.cur_pass.in_pass || !_sg.cur_pass.is_compute) return false;
    return _sgu_accel_native_bind(tlas, slot);
}

SOKOL_API_IMPL void sg_accel_shutdown(void) {
    _sgu_accel_native_shutdown();
    memset(&_sgu_accel_shaders, 0, sizeof(_sgu_accel_shaders));
    _sgu_accel_down = true;
}

#endif /* SOKOL_GFX_UTILS_IMPL */
