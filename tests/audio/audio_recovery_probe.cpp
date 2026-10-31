// Real AndroidAudio, original audio dispatch/decoding, and original iphone.big.
// Only the AAudio output device is substituted. This proves state handling and
// PCM delivery to the callback; it does not claim audible Android hardware output.
#include "AndroidAudio.hpp"
#include <aaudio/AAudio.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct AAudioStreamBuilder {
    std::int32_t channels{1}, rate{22050}, format{AAUDIO_FORMAT_PCM_FLOAT};
    AAudioStream_dataCallback data{};
    AAudioStream_errorCallback error{};
    void *dataContext{}, *errorContext{};
};
struct AAudioStream : AAudioStreamBuilder {
    aaudio_stream_state_t state{AAUDIO_STREAM_STATE_OPEN};
    std::int64_t consumed{};
};

namespace {
struct Device {
    int createFailures{}, openFailures{}, startFailures{}, pauseFailures{};
    int creates{}, opens{}, starts{}, pauses{}, stops{}, closes{};
    int liveBuilders{}, liveStreams{};
    AAudioStream *current{};
} device;

void require(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
bool failOnce(int &remaining) {
    if (!remaining)
        return false;
    --remaining;
    return true;
}
void resetDevice() {
    require(device.liveBuilders == 0 && device.liveStreams == 0,
            "A test left an AAudio builder or stream alive");
    device = {};
}
std::vector<float> callback(int frames) {
    require(device.current && device.current->state == AAUDIO_STREAM_STATE_STARTED,
            "Output device must be running before delivering frames");
    std::vector<float> samples(static_cast<std::size_t>(frames), -99);
    require(device.current->data(device.current, device.current->dataContext,
                                 samples.data(), frames) == AAUDIO_CALLBACK_RESULT_CONTINUE,
            "Real mixer stopped its AAudio callback");
    device.current->consumed += frames;
    return samples;
}
void preload(g1::android::AndroidAudio &audio) {
    for (int group = 0; group < 3; ++group)
        audio.request({0, group});
}
void compareEffect(g1::android::AndroidAudio &audio, const g1::PcmAudio &reference,
                   std::size_t first = 0) {
    const auto count = std::min<std::size_t>(512, reference.frames() - first);
    require(count > 0, "Original effect has no remaining samples");
    const auto samples = callback(static_cast<int>(count));
    for (std::size_t i = 0; i < samples.size(); ++i)
        require(std::abs(samples[i] - std::clamp(reference.samples[first + i], -1.0F, 1.0F)) < 1e-6F,
                "Recovered output differs from original decoded effect PCM");
    require(audio.request({6, 0}), "Source audio query lost a playing effect");
}
void waitForRetry() {
    std::this_thread::sleep_for(std::chrono::milliseconds(525));
}
void noResourcesLeaked() {
    require(device.liveBuilders == 0 && device.liveStreams == 0 && !device.current,
            "Audio lifetime leaked an output device or builder");
}
void disconnectFromAudioThread() {
    require(device.current && device.current->error, "No error callback registered");
    auto *stream = device.current;
    // Android reports disconnection from its audio thread; stream closing must
    // be deferred until the serialized game thread calls maintain().
    std::thread worker([stream] {
        stream->error(stream, stream->errorContext, AAUDIO_ERROR_UNAVAILABLE);
    });
    worker.join();
}
} // namespace

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **out) {
    ++device.creates;
    *out = nullptr;
    if (failOnce(device.createFailures))
        return AAUDIO_ERROR_UNAVAILABLE;
    *out = new AAudioStreamBuilder;
    ++device.liveBuilders;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *value) {
    delete value;
    --device.liveBuilders;
    return AAUDIO_OK;
}
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *, std::int32_t) {}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, std::int32_t n) { b->format = n; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, std::int32_t n) { b->channels = n; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, std::int32_t n) { b->rate = n; }
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *, std::int32_t) {}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *, std::int32_t) {}
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *, std::int32_t) {}
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *, std::int32_t) {}
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback f, void *p) {
    b->data = f;
    b->dataContext = p;
}
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback f, void *p) {
    b->error = f;
    b->errorContext = p;
}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **out) {
    ++device.opens;
    *out = nullptr;
    if (failOnce(device.openFailures))
        return AAUDIO_ERROR_UNAVAILABLE;
    require(device.current == nullptr, "Output stream reopened without closing its predecessor");
    auto *stream = new AAudioStream;
    static_cast<AAudioStreamBuilder &>(*stream) = *b;
    *out = device.current = stream;
    ++device.liveStreams;
    return AAUDIO_OK;
}
std::int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->rate; }
std::int32_t AAudioStream_getChannelCount(AAudioStream *s) { return s->channels; }
std::int32_t AAudioStream_getFormat(AAudioStream *s) { return s->format; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) {
    ++device.starts;
    if (failOnce(device.startFailures))
        return AAUDIO_ERROR_UNAVAILABLE;
    s->state = AAUDIO_STREAM_STATE_STARTED;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) {
    ++device.pauses;
    if (failOnce(device.pauseFailures))
        return AAUDIO_ERROR_UNAVAILABLE;
    s->state = AAUDIO_STREAM_STATE_PAUSED;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) {
    ++device.stops;
    s->state = AAUDIO_STREAM_STATE_STOPPED;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_close(AAudioStream *s) {
    ++device.closes;
    require(s == device.current, "Audio stream was closed twice or from an old callback");
    device.current = nullptr;
    --device.liveStreams;
    delete s;
    return AAUDIO_OK;
}
std::int64_t AAudioStream_getFramesRead(AAudioStream *s) { return s->consumed; }
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { return s->state; }
const char *AAudio_convertResultToText(aaudio_result_t) { return "Injected output unavailable"; }

int main(int argc, char **argv) {
    try {
        require(argc == 2, "Usage: audio_recovery_probe /path/to/iphone.big");
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto reference = g1::decode_audio_slot(archive, 4);
        require(reference.channels == 1 && reference.sample_rate == 22050 && reference.frames() > 1024,
                "Unexpected original effect fixture");
        require(std::any_of(reference.samples.begin(), reference.samples.begin() + 512,
                            [](float sample) { return std::abs(sample) > 1e-6F; }),
                "Original effect fixture must include audible PCM in the tested window");
        std::vector<std::string> passed;

        resetDevice();
        {
            g1::android::AndroidAudio audio(archive);
            require(audio.error().empty() && device.starts == 1, "Healthy startup did not start output");
            preload(audio);
            audio.request({5, 4});
            compareEffect(audio, reference);
            audio.foreground(false);
            require(device.current->state == AAUDIO_STREAM_STATE_PAUSED, "Background did not pause output");
            require(!audio.request({6, 0}), "Paused output reported itself as playing");
            audio.foreground(true);
            compareEffect(audio, reference, 512);
        }
        noResourcesLeaked();
        passed.emplace_back("healthy_start_original_pcm_and_pause_resume");

        for (int stage = 0; stage < 3; ++stage) {
            resetDevice();
            if (stage == 0) device.createFailures = 1;
            if (stage == 1) device.openFailures = 1;
            if (stage == 2) device.startFailures = 1;
            {
                g1::android::AndroidAudio audio(archive);
                require(!audio.error().empty(), "Initial device failure was not recorded");
                require(!device.current && device.liveBuilders == 0, "Initial failure leaked native resources");
                require(!audio.request({6, 0}), "Unavailable output reported itself as playing");
                const auto attempts = device.creates;
                for (int i = 0; i < 8; ++i) audio.maintain();
                require(device.creates == attempts, "Initial failure retries were not rate limited");
                // Preloads must work even though no audio device exists yet.
                preload(audio);
                waitForRetry();
                audio.maintain();
                require(audio.error().empty() && device.current, "Initial failure did not recover");
                audio.request({5, 4});
                compareEffect(audio, reference);
            }
            noResourcesLeaked();
            passed.emplace_back(stage == 0 ? "initial_builder_failure_recovery" :
                                stage == 1 ? "initial_open_failure_recovery" :
                                             "initial_start_failure_recovery");
        }

        resetDevice();
        device.openFailures = 2;
        {
            g1::android::AndroidAudio audio(archive);
            preload(audio);
            waitForRetry();
            audio.maintain();
            require(!audio.error().empty() && device.opens == 2, "Second device failure was not retained");
            const auto attempts = device.creates;
            for (int i = 0; i < 20; ++i) audio.maintain();
            require(device.creates == attempts, "Repeated failure bypassed the retry interval");
            waitForRetry();
            audio.maintain();
            require(audio.error().empty() && device.opens == 3, "Repeated failure did not recover");
            audio.request({5, 4});
            compareEffect(audio, reference);
        }
        noResourcesLeaked();
        passed.emplace_back("repeated_failure_retries_are_bounded");

        resetDevice();
        device.openFailures = 1;
        {
            g1::android::AndroidAudio audio(archive);
            preload(audio);
            audio.foreground(false);
            waitForRetry();
            audio.maintain();
            require(device.opens == 1 && !device.current, "Background opened an audio device");
            audio.foreground(true);
            require(audio.error().empty() && device.current, "Foreground did not recover failed startup");
            audio.request({5, 4});
            compareEffect(audio, reference);
        }
        noResourcesLeaked();
        passed.emplace_back("background_suppresses_retry_until_foreground");

        resetDevice();
        {
            g1::android::AndroidAudio audio(archive);
            preload(audio);
            audio.request({5, 4});
            compareEffect(audio, reference);
            disconnectFromAudioThread();
            require(device.closes == 0, "Error callback closed a device on the audio thread");
            audio.maintain();
            require(audio.error().empty() && device.closes == 1 && device.opens == 2,
                    "Game thread did not replace the disconnected device");
            compareEffect(audio, reference, 512);
        }
        noResourcesLeaked();
        passed.emplace_back("disconnect_callback_defers_close_and_retains_pcm_cursor");

        resetDevice();
        {
            g1::android::AndroidAudio audio(archive);
            preload(audio);
            audio.request({5, 4});
            compareEffect(audio, reference);
            device.pauseFailures = 1;
            audio.foreground(false);
            require(!audio.error().empty(), "Failed pause was not recorded");
            audio.maintain();
            require(!device.current && device.opens == 1, "Failed pause reopened output in background");
            audio.foreground(true);
            require(audio.error().empty() && device.current, "Failed pause did not recover on foreground");
            compareEffect(audio, reference, 512);
        }
        noResourcesLeaked();
        passed.emplace_back("pause_failure_recovery_preserves_playback");

        std::cout << "{\n  \"schema\": 1,\n  \"status\": \"passed\",\n  \"scenario_count\": "
                  << passed.size() << ",\n  \"scenarios\": [\n";
        for (std::size_t i = 0; i < passed.size(); ++i)
            std::cout << "    \"" << passed[i] << "\"" << (i + 1 == passed.size() ? "\n" : ",\n");
        std::cout << "  ],\n  \"scope\": \"Real AndroidAudio host, original AudioEvents planner and Vorbis decoder, original iphone.big; injected AAudio output device only. No hardware audibility claim.\"\n}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "audio recovery probe failed: " << error.what() << '\n';
        return 1;
    }
}
