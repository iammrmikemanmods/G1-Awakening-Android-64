#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
probe_dir="${G1_PROBE_BUILD_DIR:-${project_root}/build/renderer-probe}"
gles_prefix="${G1_GLES_PREFIX:-/usr}"
mkdir -p "$probe_dir"
includes=()
libs=()
if [[ "$gles_prefix" != /usr ]]; then
  includes+=("-I${gles_prefix}/usr/include")
  libs+=("-L${gles_prefix}/usr/lib/x86_64-linux-gnu" "-Wl,-rpath,${gles_prefix}/usr/lib/x86_64-linux-gnu")
fi
"${CXX:-g++}" -std=c++17 -O1 -g -ffp-contract=off -ffunction-sections -fdata-sections -Wall -Wextra \
  "${includes[@]}" -I"${project_root}/upstream/ios/native/include" -I"${project_root}/app/src/main/cpp" \
  "${project_root}/app/src/main/cpp/GLESRenderer.cpp" \
  "${project_root}/app/src/main/cpp/GLESModelRenderer.cpp" \
  "${project_root}/upstream/ios/native/src/M3GScene.cpp" \
  "${project_root}/tests/renderer/renderer_probe.cpp" \
  -Wl,--gc-sections "${libs[@]}" -lGLESv2 -l:libEGL.so.1 -o "${probe_dir}/renderer_probe"
MESA_SHADER_CACHE_DIR="${probe_dir}/shader-cache" LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless "${probe_dir}/renderer_probe"
