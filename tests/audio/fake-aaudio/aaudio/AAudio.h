#pragma once

// Test-only device boundary. Production builds include the Android NDK header.
// Only the AAudio operations used by AndroidAudio.cpp are declared here; the
// probe supplies deterministic device failures and invokes the real mixer.
#include <cstdint>

using aaudio_result_t = std::int32_t;
using aaudio_data_callback_result_t = std::int32_t;
using aaudio_stream_state_t = std::int32_t;
struct AAudioStream;
struct AAudioStreamBuilder;
using AAudioStream_dataCallback =
    aaudio_data_callback_result_t (*)(AAudioStream *, void *, void *, std::int32_t);
using AAudioStream_errorCallback = void (*)(AAudioStream *, void *, aaudio_result_t);

constexpr aaudio_result_t AAUDIO_OK = 0;
constexpr aaudio_result_t AAUDIO_ERROR_UNAVAILABLE = -889;
constexpr aaudio_data_callback_result_t AAUDIO_CALLBACK_RESULT_CONTINUE = 0;
constexpr std::int32_t AAUDIO_DIRECTION_OUTPUT = 0;
constexpr std::int32_t AAUDIO_FORMAT_PCM_FLOAT = 2;
constexpr std::int32_t AAUDIO_SHARING_MODE_SHARED = 1;
constexpr std::int32_t AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12;
constexpr std::int32_t AAUDIO_USAGE_GAME = 14;
constexpr std::int32_t AAUDIO_CONTENT_TYPE_MUSIC = 2;
constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_OPEN = 2;
constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STARTING = 3;
constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STARTED = 4;
constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_PAUSED = 6;
constexpr aaudio_stream_state_t AAUDIO_STREAM_STATE_STOPPED = 10;

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *, std::int32_t);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *, AAudioStream_dataCallback, void *);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *, AAudioStream_errorCallback, void *);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *, AAudioStream **);
std::int32_t AAudioStream_getSampleRate(AAudioStream *);
std::int32_t AAudioStream_getChannelCount(AAudioStream *);
std::int32_t AAudioStream_getFormat(AAudioStream *);
aaudio_result_t AAudioStream_requestStart(AAudioStream *);
aaudio_result_t AAudioStream_requestPause(AAudioStream *);
aaudio_result_t AAudioStream_requestStop(AAudioStream *);
aaudio_result_t AAudioStream_close(AAudioStream *);
std::int64_t AAudioStream_getFramesRead(AAudioStream *);
aaudio_stream_state_t AAudioStream_getState(AAudioStream *);
const char *AAudio_convertResultToText(aaudio_result_t);
