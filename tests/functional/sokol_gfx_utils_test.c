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
