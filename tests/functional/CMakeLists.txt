set(c_sources
    sokol-utils-test.c
    sokol_app_utils_test.c
    sokol_gfx_utils_test.c
)

add_executable(sokol-utils-test ${c_sources})
configure_c(sokol-utils-test)
