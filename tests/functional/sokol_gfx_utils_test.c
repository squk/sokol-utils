#define SOKOL_IMPL
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"
#include "utest.h"

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
