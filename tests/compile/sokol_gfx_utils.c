#define SOKOL_IMPL
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"

void use_gfx_utils(void) {
    sg_readback_request((sg_image){0});
    sg_blas_geometry geos[2];
    memset(geos, 0, sizeof(geos));
    sg_blas_desc blas_desc = {0};
    blas_desc.geometries = geos;
    blas_desc.geometry_count = 2;
    sg_tlas_desc tlas_desc = {0};
    sg_accel_instance instance = {0};
    sg_accel blas = sg_make_blas(&blas_desc);
    sg_accel tlas = sg_make_tlas(&tlas_desc);
    sg_build_blas(blas);
    sg_blas_set_geometry_vertex_buffer(blas, 1, (sg_buffer){0}, 0);
    sg_build_tlas(tlas, &instance, 1);
    sg_build_tlas_from_buffer(tlas, (sg_buffer){0}, 0, 1);
    sg_refit_tlas_from_buffer(tlas, (sg_buffer){0}, 0, 1);
    sg_accel_bind(tlas, 0);
    sg_destroy_accel(tlas);
    sg_destroy_accel(blas);

    sg_bindless_table_desc table_desc = {0};
    table_desc.kind = SG_BINDLESS_KIND_TEXTURES;
    table_desc.capacity = 16;
    sg_bindless_table table = sg_make_bindless_table(&table_desc);
    sg_bindless_set_texture(table, 0, (sg_view){0});
    sg_bindless_set_sampler(table, 0, (sg_sampler){0});
    sg_bindless_clear_entry(table, 0);
    sg_bindless_prepare_table(table);
    sg_bindless_layout_desc layout = {0};
    sg_shader_ext_desc ext = {0};
    ext.bindless = &layout;
    sg_shader_desc shd_desc = {0};
    sg_make_shader_bindless(&shd_desc, &layout);
    sg_make_shader_ext(&shd_desc, &ext);
    sg_bindless_bind(table, 0);
    sg_destroy_bindless_table(table);
    sg_bindless_shutdown();
}
