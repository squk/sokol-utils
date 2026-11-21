#!/usr/bin/env bash
# Builds the tests for Windows with MinGW and runs them in a Parallels Windows VM, on D3D11 and Vulkan.
#
#   tests/test_windows_vm.sh [--vm NAME] [--install-lavapipe] [d3d11] [vulkan]
#
# Needs prlctl, x86_64-w64-mingw32-gcc, cmake, ninja, zstd and the Vulkan headers (brew install vulkan-headers,
# or set VULKAN_HEADERS to an include directory). The VM must share the host home folder (Parallels shares it
# as \\Mac\Home), and the repository must live under it. Run from anywhere; ../sokol must hold sokol.
#
# The binaries are x64, so on an ARM64 VM they run under emulation and need an x64 Vulkan loader: the script
# puts the Khronos loader from MSYS2's mingw64 package next to the Vulkan test binaries. The VM also needs a
# Vulkan driver; --install-lavapipe installs Mesa's x64 lavapipe (mesa-dist-win) into C:\Program Files\Mesa
# and registers it under HKLM\SOFTWARE\Khronos\Vulkan\Drivers.
set -euo pipefail

vm="Windows 11"
install_lavapipe=0
backends=()
while [ $# -gt 0 ]; do
    case "$1" in
        --vm) vm="$2"; shift 2 ;;
        --install-lavapipe) install_lavapipe=1; shift ;;
        d3d11|vulkan) backends+=("$1"); shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ ${#backends[@]} -eq 0 ] && backends=(d3d11 vulkan)

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/tests/build"
case "$root" in
    "$HOME"/*) ;;
    *) echo "the repository must be under $HOME, which the VM shares as \\\\Mac\\Home" >&2; exit 1 ;;
esac
# A host path under $HOME as the VM sees it.
vm_path() { local rel="${1#"$HOME"/}"; echo "\\\\Mac\\Home\\${rel//\//\\}"; }

vm_ps() { prlctl exec "$vm" --current-user powershell -NoProfile -Command "$1"; }
vm_admin_ps() { prlctl exec "$vm" powershell -NoProfile -Command "$1"; }

if ! prlctl list "$vm" 2>/dev/null | grep -q running; then
    echo "starting VM '$vm'"
    prlctl start "$vm"
    until prlctl exec "$vm" --current-user cmd /c echo ready 2>/dev/null | grep -q ready; do sleep 5; done
fi

if [ "$install_lavapipe" = 1 ]; then
    release=$(curl -s https://api.github.com/repos/pal1000/mesa-dist-win/releases/latest |
              python3 -c "import json,sys; print(json.load(sys.stdin)['tag_name'])")
    url="https://github.com/pal1000/mesa-dist-win/releases/download/$release/mesa3d-$release-release-msvc.7z"
    echo "installing lavapipe from mesa-dist-win $release"
    vm_admin_ps "
        \$ErrorActionPreference = 'Stop'
        \$archive = Join-Path \$env:TEMP 'mesa-dist-win.7z'
        curl.exe -sSL -o \$archive '$url'
        New-Item -ItemType Directory -Force -Path 'C:\Program Files\Mesa' | Out-Null
        tar.exe -xf \$archive -C 'C:\Program Files\Mesa' x64/vulkan_lvp.dll x64/lvp_icd.x86_64.json
        Remove-Item \$archive
        New-Item -Force -Path 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' | Out-Null
        New-ItemProperty -Force -Path 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers' \`
            -Name 'C:\Program Files\Mesa\x64\lvp_icd.x86_64.json' -PropertyType DWord -Value 0 | Out-Null"
fi

# MinGW builds. GCC rejects an enum/int conditional inside sokol's C++ headers under -Werror=extra.
# configure DIR BACKEND C_FLAGS [CMAKE_ARGS...]
configure() {
    local dir="$1" backend="$2" cflags="$3"; shift 3
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$root/tests/mingw_toolchain.cmake" -DCMAKE_BUILD_TYPE=Debug \
        -DSOKOL_BACKEND="$backend" "-DCMAKE_CXX_FLAGS=-Wno-error=extra $cflags" "-DCMAKE_C_FLAGS=$cflags" "$@" \
        -B "$dir" "$root/tests" > /dev/null
}

for backend in "${backends[@]}"; do
    dir="$build/mingw_$backend"
    case "$backend" in
        d3d11)
            configure "$dir" SOKOL_D3D11 ""
            ;;
        vulkan)
            headers="${VULKAN_HEADERS:-$(brew --prefix vulkan-headers 2>/dev/null)/include}"
            [ -f "$headers/vulkan/vulkan.h" ] || { echo "no Vulkan headers at $headers; set VULKAN_HEADERS" >&2; exit 1; }
            loader="$build/msys2-vulkan-loader"
            if [ ! -f "$loader/mingw64/bin/vulkan-1.dll" ]; then
                package=$(curl -s https://repo.msys2.org/mingw/mingw64/ |
                          grep -o 'mingw-w64-x86_64-vulkan-loader-[^"]*\.pkg\.tar\.zst"' | tr -d '"' | sort -V | tail -1)
                echo "fetching $package"
                rm -rf "$loader" && mkdir -p "$loader"
                curl -sSL "https://repo.msys2.org/mingw/mingw64/$package" | zstd -dq | tar -xf - -C "$loader"
                cp "$loader/mingw64/lib/libvulkan.dll.a" "$loader/mingw64/lib/libvulkan-1.dll.a"
            fi
            configure "$dir" SOKOL_VULKAN "-I$headers" "-DCMAKE_EXE_LINKER_FLAGS=-L$loader/mingw64/lib"
            cp "$loader/mingw64/bin/vulkan-1.dll" "$dir/"
            ;;
    esac
    cmake --build "$dir"
done

status=0
for backend in "${backends[@]}"; do
    dir="$build/mingw_$backend"
    for test in sokol-utils-test sokol-utils-compiletest-c sokol-utils-compiletest-cxx; do
        echo "== $backend $test"
        # Output goes to files in the shared build directory: prlctl does not relay a redirected
        # process's console, and a crash would lose buffered stdout.
        out="$dir/$test.out.txt"
        err="$dir/$test.err.txt"
        rm -f "$out" "$err"
        code=$(vm_ps "
            Set-Location '$(vm_path "$dir")'
            \$p = Start-Process -FilePath '.\\$test.exe' -NoNewWindow -Wait -PassThru \`
                -RedirectStandardOutput '$(vm_path "$out")' -RedirectStandardError '$(vm_path "$err")'
            \$p.ExitCode" | tr -d '\r' | tail -1)
        cat "$out" "$err" 2>/dev/null || true
        if [ "$code" != 0 ]; then
            echo "$backend $test failed with exit code $code" >&2
            status=1
        fi
        # A device test that found no GPU device prints a skip note; treat it as a failure here.
        if grep -q "skipped: no GPU device" "$err" 2>/dev/null; then
            echo "$backend $test skipped its device tests (no driver; try --install-lavapipe)" >&2
            status=1
        fi
    done
done
exit $status
