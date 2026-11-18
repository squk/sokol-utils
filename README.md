# sokol-utils

Extension utilities for the [sokol](https://github.com/floooh/sokol) single-header libraries.

- [`sokol_app_utils.h`](sokol_app_utils.h): Window, display, cursor, swap-interval, and macOS background/swapchain helpers for `sokol_app.h`.
- [`sokol_gfx_utils.h`](sokol_gfx_utils.h): Image readback, indirect commands, GPU pass timings, submission completion receipts, ray-query acceleration structures, fragment-stage storage images, and async compute helpers for `sokol_gfx.h`.

## Usage

The tests assume `sokol` is checked out at `../sokol`. Include each utility header *after* the corresponding sokol header implementation, defining `_IMPL` in one C or C++ file:

```c
#define SOKOL_IMPL
#include "sokol_app.h"
#define SOKOL_APP_UTILS_IMPL
#include "sokol_app_utils.h"
```

```c
#define SOKOL_IMPL
#include "sokol_gfx.h"
#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"
```

## Testing & CI

- **macOS (GLCore & Metal):** `./tests/test_macos.sh`
- **Linux (GLCore):** `./tests/test_linux.sh`
- **GitHub Actions locally via [`act`](https://nektosact.com):**
  ```sh
  act push -j native --matrix config:Debug -P ubuntu-24.04=catthehacker/ubuntu:act-24.04 -P windows-2025= --container-architecture linux/amd64
  act push -j web --matrix config:Debug -P ubuntu-24.04=catthehacker/ubuntu:act-24.04 --container-architecture linux/amd64
  ```

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds and runs compile + functional tests in Debug/Release on Linux (`SOKOL_GLCORE`, `SOKOL_VULKAN`), macOS (`SOKOL_GLCORE`, `SOKOL_METAL`), Windows (`SOKOL_D3D11`), and builds WebGL2 (`SOKOL_GLES3`). GPU and browser runtime tests are not covered.

## API

### `sokol_app_utils.h`

```c
void sapp_set_window_position(int x, int y);
void sapp_set_window_size(int w, int h);
void sapp_get_window_position(int *x, int *y);
void sapp_get_window_size(int *w, int *h);
void sapp_set_mouse_position(float x, float y);
void sapp_get_mouse_position(float *x, float *y);
int sapp_display_width(int index);
int sapp_display_height(int index);
bool sapp_window_focused(void);
int sapp_num_displays(void);
int sapp_current_display(void);
void sapp_set_display(int index);
const char* sapp_display_name(int index);
void sapp_set_fullscreen(bool enable);
void sapp_display_position(int index, int *x, int *y);
void sapp_set_window_resizable(bool resizable);
void sapp_set_window_decorated(bool decorated);
bool sapp_can_move_window(void);
void sapp_set_window_visible(bool visible);
bool sapp_window_visible(void);
void sapp_set_swap_interval(int interval);
int sapp_get_swap_interval(void);
void sapp_set_macos_background(bool enabled);
bool sapp_get_macos_background(void);
sapp_swapchain sapp_acquire_swapchain_checked(void);
```

- Most functions are desktop-only (Windows, macOS, Linux).
- Mouse positions are relative to the upper-left corner of the content area in framebuffer pixels (matching `sapp_event.mouse_x`/`mouse_y`); setting while locked or unsupported is a no-op.
- On macOS, a custom frame timer is used when `swap_interval == 0` or running without an app bundle (since `CADisplayLink` cannot run unthrottled or for CLI launches).
- `sapp_acquire_swapchain_checked()` returns an invalid swapchain when the window is covered or no drawable is free (use instead of `sapp_acquire_swapchain()`/`sglue_swapchain()`). `sapp_set_macos_background(true)` before `sapp_run()` opens the window behind other windows without activating the app.

### `sokol_gfx_utils.h`

#### Image readback & GPU pass timings

```c
void* sg_readback_request(sg_image img);
bool sg_readback_is_ready(void* request);
void sg_readback_wait(void* request);
void sg_readback_copy_data(void* request, void* out_pixels);
void sg_readback_destroy(void* request);

#define SG_GPU_PASS_TIMING_MAX_PASSES (256)
#define SG_GPU_PASS_TIMING_LABEL_SIZE (96)

typedef struct sg_gpu_pass_timing {
    char label[SG_GPU_PASS_TIMING_LABEL_SIZE];
    uint64_t frame_index;
    double milliseconds;
} sg_gpu_pass_timing;

bool sg_gpu_pass_timings_setup(void);
void sg_gpu_pass_timings_shutdown(void);
void sg_gpu_pass_timings_set_enabled(bool enabled);
bool sg_gpu_pass_timings_supported(void);
int sg_gpu_pass_timings_poll(sg_gpu_pass_timing* timings, int max_timings);
```

- Supported on Metal, Vulkan, and D3D11; requires `SOKOL_TRACE_HOOKS` defined in the implementation.
- Call setup with a valid context before installing other trace hooks, and call timing functions on Sokol's render thread.
- `sg_gpu_pass_timings_poll()` never waits or flushes; provide space for `SG_GPU_PASS_TIMING_MAX_PASSES + 1` results. `Frame` comes first, followed by labeled passes (passes can overlap; do not sum them).
- Call `sg_gpu_pass_timings_shutdown()` after `sg_shutdown()` has drained in-flight work, while the native device still exists.

#### Submission completion

- Replace `sg_commit()` with `sg_commit_tracked()` to obtain an `sg_submission_receipt`. Poll with `sg_query_submission_receipt()` (`SG_SUBMISSION_UNKNOWN`, `SG_SUBMISSION_PENDING`, `SG_SUBMISSION_COMPLETE`, `SG_SUBMISSION_FAILED`), and destroy every receipt (including pending) with `sg_destroy_submission_receipt()`.
- Supported on Metal, Vulkan, D3D11, GL/GLES, and WebGPU (`sg_submission_completion_supported()`). `SG_SUBMISSION_RECEIPT_CAPACITY` defaults to 64; exhausted capacity returns a null receipt while still committing.
- Call on the graphics thread outside passes. Call `sg_shutdown_submission_receipts()` before `sg_shutdown()` or switching backends (if Vulkan returns `false`, retry cleanup before destroying the device).

#### Acceleration structures & ray queries

```c
bool sg_ray_query_supported(void);
sg_buffer sg_make_accel_input_buffer(const sg_buffer_desc* desc);
sg_accel sg_make_blas(const sg_blas_desc* desc);
sg_accel sg_make_tlas(const sg_tlas_desc* desc);
bool sg_build_blas(sg_accel blas);
bool sg_refit_blas(sg_accel blas);
bool sg_build_tlas(sg_accel tlas, const sg_accel_instance* instances, int count);
bool sg_refit_tlas(sg_accel tlas, const sg_accel_instance* instances, int count);
sg_shader sg_make_shader_accel(const sg_shader_desc* desc, const sg_accel_layout_desc* layout);
bool sg_accel_bind(sg_accel tlas, int slot);
void sg_destroy_accel(sg_accel accel);
void sg_accel_shutdown(void);
```

- Supported on Metal and Vulkan (`sg_ray_query_supported()`). Call on the `sokol_gfx` thread; call `sg_accel_shutdown()` after `sg_shutdown()` while the native device is alive, after destroying all `sg_accel` structures.
- Create vertex/index buffers with `sg_make_accel_input_buffer()` (`usage.storage_buffer`). Build or refit outside passes before the compute pass that traces; refit requires `allow_update` and keeps topology/instance count. Instance masks must be non-zero (`0xFF` hits all).
- Pass `sokol-slang` reflection indices (`msl_buffer_n`, `spirv_set1_binding_n`, or `-1` for untargeted backends) to `sg_make_shader_accel()`. On Metal, call `sg_accel_bind()` in every compute pass; on Vulkan, call it after `sg_apply_bindings()` with no `sg_dispatch()` or `sg_apply_uniforms()` in between.
- On Vulkan, the application's `VkDevice` must enable `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`, and the `accelerationStructure` and `rayQuery` features.
