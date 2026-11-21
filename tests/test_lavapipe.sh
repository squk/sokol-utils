#!/usr/bin/env bash
# Runs the tests on the Vulkan backend with Mesa's lavapipe (a CPU Vulkan driver) and the Khronos validation
# layer, in a Linux container. Use it where no GPU driver offers VK_EXT_descriptor_buffer, which sokol_gfx's
# Vulkan backend needs (MoltenVK on macOS does not). Run from the repository root; ../sokol must hold sokol.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
sokol="$(cd "$root/../sokol" && pwd)"
docker build -q -t sokol-utils-lavapipe -f "$root/tests/lavapipe.Dockerfile" "$root/tests" > /dev/null
docker run --rm -v "$sokol:/src/sokol:ro" -v "$root:/src/sokol-utils:ro" sokol-utils-lavapipe sh -c '
    set -e
    cmake -G Ninja -S /src/sokol-utils/tests -B /tmp/build -DSOKOL_BACKEND=SOKOL_VULKAN -DCMAKE_BUILD_TYPE=Debug > /dev/null
    cmake --build /tmp/build
    /tmp/build/sokol-utils-test
    /tmp/build/sokol-utils-compiletest-c
    /tmp/build/sokol-utils-compiletest-cxx'
