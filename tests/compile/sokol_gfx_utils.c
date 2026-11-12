#define SOKOL_IMPL
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"

void use_gfx_utils(void) {
    sg_readback_request((sg_image){0});
    sg_blas_desc blas_desc = {0};
    sg_tlas_desc tlas_desc = {0};
    sg_accel_instance instance = {0};
    sg_accel blas = sg_make_blas(&blas_desc);
    sg_accel tlas = sg_make_tlas(&tlas_desc);
    sg_build_blas(blas);
    sg_build_tlas(tlas, &instance, 1);
    sg_accel_bind(tlas, 0);
    sg_destroy_accel(tlas);
    sg_destroy_accel(blas);
}
