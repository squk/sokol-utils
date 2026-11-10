# sokol-utils

Extension utilities for the [sokol](https://github.com/floooh/sokol) headers.

Currently, this repository contains:
- `sokol_app_utils.h`: Additional functionality for `sokol_app.h`.
- `sokol_gfx_utils.h`: Additional functionality for `sokol_gfx.h` (readback, indirect commands, and GPU pass timings).

## Usage

This project's tests assumes that the [sokol](https://github.com/floooh/sokol) repository is located in the parent directory (`../sokol`).

Include the utility headers *after* the corresponding sokol header implementation. To create the implementation, define the corresponding `_IMPL` macro in exactly one C or C++ file.

### sokol_app_utils.h

```c
#define SOKOL_IMPL
#include "sokol_app.h"

#define SOKOL_APP_UTILS_IMPL
#include "sokol_app_utils.h"
```

### sokol_gfx_utils.h

```c
#define SOKOL_IMPL
#include "sokol_gfx.h"

#define SOKOL_GFX_UTILS_IMPL
#include "sokol_gfx_utils.h"
```

## CI

[GitHub Actions](.github/workflows/ci.yml) builds and tests Debug/Release on Linux,
macOS, and Windows, and compiles WebGL2. GPU and browser runtime tests are not covered.

## API

### sokol_app_utils.h

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
sapp_swapchain sapp_acquire_swapchain_checked(void);
```

*Note: Most of these functions are only supported on desktop platforms (Windows, macOS, Linux).*

Mouse positions are relative to the upper-left corner of the window's content
area and use framebuffer pixels, matching `sapp_event.mouse_x` and
`sapp_event.mouse_y`. The getter returns Sokol's last event position so it has
the same capture behavior as mouse events: dragging may report coordinates
outside the content area, movement without capture stops at leave, and the
absolute position remains frozen while the mouse is locked. A warp is reflected
by the getter when Sokol receives the resulting native mouse event. Setting the
position while the mouse is locked, or on an unsupported platform, is a no-op.

On macOS, sokol-utils runs frames from its own timer when the swap interval is
0 or the program runs without an app bundle. CADisplayLink cannot run
unthrottled, and AppKit does not schedule it for command-line launches. It
replaces methods of sokol_app.h's private classes when the program loads.
`sapp_acquire_swapchain_checked()` returns an invalid swapchain while the window
is covered or no drawable is free; call it instead of `sapp_acquire_swapchain()`
(which `sglue_swapchain()` uses). `sapp_set_macos_background(true)` before
`sapp_run()` opens the window behind other windows without activating the app
and keeps rendering while it is covered.

### sokol_gfx_utils.h

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

GPU timing uses the same public API on Metal, Vulkan, and D3D11. Call setup
with a valid Sokol graphics context, before installing other trace hooks.
Call the timing functions on Sokol's render thread.
Define `SOKOL_TRACE_HOOKS` in the implementation. Unsupported backends or
unavailable timestamp queries make setup return `false`.

Enable recording as needed and poll for the oldest completed frame. Polling
never waits for the GPU or flushes pending commands. Provide space for
`SG_GPU_PASS_TIMING_MAX_PASSES + 1` results. Query storage stays bounded;
recording drops samples when readback falls behind. Call shutdown after
`sg_shutdown` has drained in-flight work, while the native device still exists.

The `Frame` result, when available, comes first. It measures the whole Metal
command buffer, the Vulkan
render command buffer, or the interval from D3D11's first pass through commit.
Vulkan staging uploads use a separate command buffer and are excluded.
Remaining results describe labeled render or compute passes. Passes can overlap;
do not sum them to infer frame time. D3D11 rejects disjoint clock samples.
Vulkan checks the submission fence before reading query availability, so a
reused query pool cannot return the previous frame's timestamps as new data.

Backend integration stays inside the utility implementation and its native
adapter; it adds no public backend-specific API. Metal's adapter attaches stage
counters to the render or compute encoder descriptor before encoder creation.
Render durations combine vertex and fragment intervals, excluding gaps and
counting overlap once. Devices without stage counters report only `Frame`.
Vulkan's adapter brackets Sokol's render command buffer and resets query pools
only after Sokol has finished the slot's earlier work. The adapter must share
the utility implementation's translation unit to call its private helpers.

### Submission completion

Replace `sg_commit()` with `sg_commit_tracked()` to get an opaque
`sg_submission_receipt`. Query it with `sg_query_submission_receipt()`; results
are `SG_SUBMISSION_UNKNOWN`, `SG_SUBMISSION_PENDING`, `SG_SUBMISSION_COMPLETE`,
or `SG_SUBMISSION_FAILED`. Destroy each receipt with
`sg_destroy_submission_receipt()`, including pending receipts.

Metal tracks the actual command buffer. Vulkan adds a fence on Sokol's queue;
D3D11 uses an event query, GL/GLES a sync object, and WebGPU a queue callback.
`sg_submission_completion_supported()` checks the current backend. Unsupported
tracking, allocation failure, or exhausted capacity returns a null receipt;
the tracked call still commits. `SG_SUBMISSION_RECEIPT_CAPACITY` defaults to 64.
Completion covers this Sokol queue/context, not other queues or output validity.

Call these functions on the graphics thread, outside passes. Polling and receipt
destruction never wait. Call `sg_shutdown_submission_receipts()` before
`sg_shutdown()` or a backend switch; Vulkan may wait during this explicit
shutdown. If it returns `false`, Vulkan could not confirm idle: receipts stay
intact. Retry cleanup or stop graphics teardown; do not destroy the device.
On success, surviving receipts report unknown and still need destruction.
WebGPU requires the modern callback API and supported atomic operations.
