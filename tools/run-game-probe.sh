#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
host_build="${G1_HOST_BUILD:-build/host}"
deps="${G1_GL_DEPS:-/}"
output="${1:-validation/runtime}"
mkdir -p "$host_build" "$output"
make --no-print-directory -j"${G1_HOST_JOBS:-2}" -f - HOST_BUILD="$host_build" GL_DEPS="$deps" <<'MAKE'
SOURCES := tests/game_probe.cpp app/src/main/cpp/AndroidGame.cpp \
 app/src/main/cpp/AndroidAudio.cpp app/src/main/cpp/AndroidUI.cpp \
 app/src/main/cpp/GLESRenderer.cpp app/src/main/cpp/GLESModelRenderer.cpp \
 app/src/main/cpp/AssetStore.cpp
OBJECTS := $(addprefix $(HOST_BUILD)/game-probe-objects/,$(SOURCES:.cpp=.o))
LIBRARIES := $(HOST_BUILD)/libg1_resources.a $(HOST_BUILD)/libg1_png.a $(HOST_BUILD)/libg1_vorbis.a
CPPFLAGS += -I$(GL_DEPS)/usr/include -Iupstream/ios/native/include -Iapp/src/main/cpp
CXXFLAGS += -std=c++17 -O1 -g -Wall -Wextra -Wno-misleading-indentation
$(HOST_BUILD)/game_probe: $(OBJECTS) $(LIBRARIES)
	$(CXX) $^ -L$(GL_DEPS)/usr/lib/x86_64-linux-gnu -Wl,-rpath,$(GL_DEPS)/usr/lib/x86_64-linux-gnu -Wl,--wrap=glCheckFramebufferStatus -lGLESv2 -l:libEGL.so.1 -lz -lpthread -o $@
$(HOST_BUILD)/game-probe-objects/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
-include $(OBJECTS:.o=.d)
MAKE
MESA_SHADER_CACHE_DISABLE=true LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  "$host_build/game_probe" app/src/main/assets/game "$output"
