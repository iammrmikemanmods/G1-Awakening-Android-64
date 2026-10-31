#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
host_build="${G1_HOST_BUILD:-build/host}"
deps="${G1_GL_DEPS:-/}"
output="${1:-validation/runtime}"
mkdir -p "$host_build" "$output"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -I"$deps/usr/include" -Iupstream/ios/native/include -Iapp/src/main/cpp \
  tests/ui_probe.cpp app/src/main/cpp/AndroidUI.cpp \
  app/src/main/cpp/GLESRenderer.cpp app/src/main/cpp/AssetStore.cpp \
  "$host_build/libg1_resources.a" "$host_build/libg1_png.a" "$host_build/libg1_vorbis.a" \
  -L"$deps/usr/lib/x86_64-linux-gnu" -Wl,-rpath,"$deps/usr/lib/x86_64-linux-gnu" \
  -lGLESv2 -l:libEGL.so.1 -lz -lpthread -o "$host_build/ui_probe"
MESA_SHADER_CACHE_DISABLE=true EGL_PLATFORM=surfaceless "$host_build/ui_probe" app/src/main/assets/game "$output"
