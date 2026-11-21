#define SOKOL_IMPL
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"
#include "utest.h"

UTEST(sokol_gfx_utils, flush_commands_without_context) {
    EXPECT_FALSE(sg_flush_commands());
}

#if defined(SOKOL_METAL)
#include <time.h>

static bool flush_test_setup(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return false;
    sg_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.environment.metal.device = (__bridge const void*)device;
    sg_setup(&desc);
    return sg_isvalid();
}

/* An offscreen target for one clear pass, so a frame has a command buffer to split. */
static sg_image flush_test_target(sg_view* view) {
    sg_image_desc image_desc;
    memset(&image_desc, 0, sizeof(image_desc));
    image_desc.usage.color_attachment = true;
    image_desc.width = 64;
    image_desc.height = 64;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    sg_image image = sg_make_image(&image_desc);
    sg_view_desc view_desc;
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.color_attachment.image = image;
    *view = sg_make_view(&view_desc);
    return image;
}

static void flush_test_pass(sg_view view) {
    sg_pass pass;
    memset(&pass, 0, sizeof(pass));
    pass.attachments.colors[0] = view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    sg_begin_pass(&pass);
    sg_end_pass();
}

static double flush_test_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

/* Waits until every submitted buffer finished and its completed handlers ran,
   then checks that the frame semaphore is back at SG_NUM_INFLIGHT_FRAMES. */
static bool flush_test_semaphore_balanced(void) {
    id<MTLCommandBuffer> drain = [_sg.mtl.cmd_queue commandBuffer];
    [drain commit];
    [drain waitUntilCompleted];
    int taken = 0;
    while (taken < SG_NUM_INFLIGHT_FRAMES &&
           dispatch_semaphore_wait(_sg.mtl.sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0) {
        taken++;
    }
    const bool extra = taken == SG_NUM_INFLIGHT_FRAMES &&
        dispatch_semaphore_wait(_sg.mtl.sem, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0;
    if (extra) taken++;
    for (int i = 0; i < taken; i++) dispatch_semaphore_signal(_sg.mtl.sem);
    return taken == SG_NUM_INFLIGHT_FRAMES && !extra;
}

/* A split continues the frame in a new command buffer; with no pass since the
   last split or commit there is nothing to submit. */
UTEST(sokol_gfx_utils, flush_commands_metal_splits_frame) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    EXPECT_TRUE(sg_flush_commands());
    EXPECT_TRUE(_sg.mtl.cmd_buffer == nil);
    flush_test_pass(view);
    id<MTLCommandBuffer> first = _sg.mtl.cmd_buffer;
    EXPECT_TRUE(first != nil);
    EXPECT_TRUE(sg_flush_commands());
    EXPECT_TRUE(_sg.mtl.cmd_buffer != nil);
    EXPECT_TRUE(_sg.mtl.cmd_buffer != first);
    EXPECT_TRUE(_sgu_mtl_split.tail != NULL);
    flush_test_pass(view);
    sg_commit();
    EXPECT_TRUE(_sgu_mtl_split.tail == NULL);
    EXPECT_TRUE(flush_test_semaphore_balanced());
    sg_destroy_view(view);
    sg_destroy_image(image);
    sg_shutdown();
}

/* Many splits per frame over many frames: an extra semaphore wait would block
   the frame (deadlocking it once SG_NUM_INFLIGHT_FRAMES buffers are pending),
   and an extra signal would let a frame start before its uniform ring slot is
   free. After each commit the frame that last used the next slot has finished. */
UTEST(sokol_gfx_utils, flush_commands_metal_takes_no_frame_slot) {
    if (!flush_test_setup()) return;
    sg_view view;
    sg_image image = flush_test_target(&view);
    const double start = flush_test_seconds();
    for (int frame = 0; frame < 3 * SG_NUM_INFLIGHT_FRAMES + 1; frame++) {
        for (int split = 0; split < 4 * SG_NUM_INFLIGHT_FRAMES; split++) {
            flush_test_pass(view);
            EXPECT_TRUE(sg_flush_commands());
        }
        flush_test_pass(view);
        sg_commit();
        EXPECT_TRUE(_sgu_mtl_split.slot_tail[_sg.mtl.cur_frame_rotate_index] == NULL);
    }
    EXPECT_LT(flush_test_seconds() - start, 10.0);
    EXPECT_TRUE(flush_test_semaphore_balanced());
    sg_destroy_view(view);
    sg_destroy_image(image);
    sg_shutdown();
}
#endif

UTEST(sokol_gfx_utils, readback_request_null) {
    sg_image img = { .id = 0 };
    void* req = sg_readback_request(img);
    EXPECT_TRUE(req == NULL);
}

UTEST(sokol_gfx_utils, ray_query_unsupported_without_context) {
    EXPECT_FALSE(sg_ray_query_supported());
}

UTEST(sokol_gfx_utils, accel_invalid_arguments) {
    sg_blas_desc blas_desc = { 0 };
    sg_tlas_desc tlas_desc = { 0 };
    EXPECT_TRUE(sg_make_blas(NULL) == NULL);
    EXPECT_TRUE(sg_make_blas(&blas_desc) == NULL);
    EXPECT_TRUE(sg_make_tlas(&tlas_desc) == NULL);
    EXPECT_FALSE(sg_build_blas(NULL));
    EXPECT_FALSE(sg_accel_bind(NULL, 0));
    EXPECT_FALSE(sg_accel_built(NULL));
    sg_destroy_accel(NULL);
}

UTEST(sokol_gfx_utils, multi_geometry_blas_invalid_arguments) {
    sg_blas_geometry geos[2] = { { 0 } };
    sg_blas_desc desc = { 0 };
    desc.geometries = geos;
    desc.geometry_count = 2;
    EXPECT_TRUE(sg_make_blas(&desc) == NULL);
    desc.geometry_count = 0;
    EXPECT_TRUE(sg_make_blas(&desc) == NULL);
    EXPECT_FALSE(sg_blas_set_geometry_vertex_buffer(NULL, 0, (sg_buffer){ 0 }, 0));
    EXPECT_FALSE(sg_blas_set_vertex_buffer(NULL, (sg_buffer){ 0 }, 0));
}

UTEST(sokol_gfx_utils, gpu_tlas_build_invalid_arguments) {
    EXPECT_EQ(sizeof(sg_gpu_accel_instance), (size_t)64);
    EXPECT_FALSE(sg_build_tlas_from_buffer(NULL, (sg_buffer){ 0 }, 0, 1));
    EXPECT_FALSE(sg_refit_tlas_from_buffer(NULL, (sg_buffer){ 0 }, 0, 1));
}

UTEST(sokol_gfx_utils, bindless_unsupported_without_context) {
    EXPECT_FALSE(sg_bindless_supported());
    EXPECT_EQ(sg_bindless_max_capacity(SG_BINDLESS_KIND_TEXTURES), 0);
    sg_bindless_table_desc desc = { 0 };
    desc.kind = SG_BINDLESS_KIND_TEXTURES;
    desc.capacity = 4;
    EXPECT_TRUE(sg_make_bindless_table(&desc) == NULL);
    EXPECT_TRUE(sg_make_bindless_table(NULL) == NULL);
    EXPECT_FALSE(sg_bindless_set_texture(NULL, 0, (sg_view){ 0 }));
    EXPECT_FALSE(sg_bindless_set_sampler(NULL, 0, (sg_sampler){ 0 }));
    EXPECT_FALSE(sg_bindless_clear_entry(NULL, 0));
    EXPECT_FALSE(sg_bindless_prepare_table(NULL));
    EXPECT_FALSE(sg_bindless_bind(NULL, 0));
    EXPECT_EQ(sg_make_shader_bindless(NULL, NULL).id, (uint32_t)SG_INVALID_ID);
    EXPECT_EQ(sg_make_shader_ext(NULL, NULL).id, (uint32_t)SG_INVALID_ID);
    sg_destroy_bindless_table(NULL);
}
