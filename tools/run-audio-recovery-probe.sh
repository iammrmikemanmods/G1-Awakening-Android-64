#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
g1_audio_build="${G1_HOST_BUILD:-build/host}"
g1_audio_archive="${1:-app/src/main/assets/game/GameData/iphone.big}"
g1_audio_report="${2:-validation/audio-recovery-report.json}"
for g1_audio_required in "$g1_audio_build/libg1_resources.a" "$g1_audio_build/libg1_vorbis.a" "$g1_audio_archive"; do
    if [[ ! -f "$g1_audio_required" ]]; then
        echo "Missing audio probe input: $g1_audio_required" >&2
        echo "Build the host core and import the pinned iOS assets first." >&2
        exit 1
    fi
done
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Werror -D__ANDROID__ \
    -Itests/audio/fake-aaudio -Iupstream/ios/native/include -Iapp/src/main/cpp \
    tests/audio/audio_recovery_probe.cpp app/src/main/cpp/AndroidAudio.cpp \
    "$g1_audio_build/libg1_resources.a" "$g1_audio_build/libg1_vorbis.a" \
    -lz -pthread -o "$g1_audio_build/audio_recovery_probe"
mkdir -p "$(dirname "$g1_audio_report")"
g1_audio_temporary="$(mktemp "$g1_audio_report.XXXXXX")"
trap 'rm -f "$g1_audio_temporary"' EXIT
"$g1_audio_build/audio_recovery_probe" "$g1_audio_archive" > "$g1_audio_temporary"
mv "$g1_audio_temporary" "$g1_audio_report"
cat "$g1_audio_report"
