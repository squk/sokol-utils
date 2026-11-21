# Vulkan test image for tests/test_lavapipe.sh: lavapipe, the validation layer and the build tools.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers vulkan-tools \
    libx11-dev libxi-dev libxcursor-dev libxrandr-dev libgl-dev libegl-dev libasound2-dev && rm -rf /var/lib/apt/lists/*
