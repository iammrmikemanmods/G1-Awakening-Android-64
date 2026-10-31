#include "AndroidAudio.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif
namespace g1::android {
struct AndroidAudio::Impl {
    const Archive &archive;
    AudioConfiguration config{};
    AudioControlState control{};
    std::array<std::shared_ptr<PcmAudio>, 24> clips{};
    struct Voice {
        std::shared_ptr<PcmAudio> clip;
        double cursor{};
        std::uint8_t slot{};
        std::int64_t finish_frame{-1};
    };
    std::array<Voice, 64> voices{};
    std::mutex mutex;
    bool active{true};
    std::string failure;
    std::atomic<bool> disconnected{false};
    std::chrono::steady_clock::time_point retry_after{};
    int rate{22050}, channels{1};
    std::int64_t produced{};
#ifdef __ANDROID__
    AAudioStream *stream{};
    static aaudio_data_callback_result_t callback(AAudioStream *, void *context, void *output,
                                                  int32_t frames) {
        auto &self = *static_cast<Impl *>(context);
        self.mix(static_cast<float *>(output), frames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void streamError(AAudioStream *, void *context, aaudio_result_t) {
        static_cast<Impl *>(context)->disconnected.store(true);
    }
    void open() {
        AAudioStreamBuilder *b = nullptr;
        auto result = AAudio_createStreamBuilder(&b);
        if (result != AAUDIO_OK)
            throw std::runtime_error(AAudio_convertResultToText(result));
        const std::unique_ptr<AAudioStreamBuilder, decltype(&AAudioStreamBuilder_delete)> builder(
            b, AAudioStreamBuilder_delete);
        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, 1);
        AAudioStreamBuilder_setSampleRate(b, 22050);
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setDataCallback(b, callback, this);
        AAudioStreamBuilder_setErrorCallback(b, streamError, this);
        AAudioStream *candidate = nullptr;
        result = AAudioStreamBuilder_openStream(b, &candidate);
        const auto cleanup = [](AAudioStream *value) {
            if (value)
                AAudioStream_close(value);
        };
        std::unique_ptr<AAudioStream, decltype(cleanup)> pending(candidate, cleanup);
        if (result != AAUDIO_OK)
            throw std::runtime_error(std::string("Android audio stream: ") +
                                     AAudio_convertResultToText(result));
        const auto actual_rate = AAudioStream_getSampleRate(candidate);
        const auto actual_channels = AAudioStream_getChannelCount(candidate);
        if (actual_rate <= 0 || actual_channels != 1 ||
            AAudioStream_getFormat(candidate) != AAUDIO_FORMAT_PCM_FLOAT)
            throw std::runtime_error("Unexpected Android PCM format");
        rate = actual_rate;
        channels = actual_channels;
        produced = 0;
        stream = pending.release();
        if (active) {
            result = AAudioStream_requestStart(stream);
            if (result != AAUDIO_OK) {
                close();
                throw std::runtime_error(AAudio_convertResultToText(result));
            }
        }
    }
    void close() {
        if (stream) {
            AAudioStream_requestStop(stream);
            AAudioStream_close(stream);
            stream = nullptr;
        }
    }
    std::int64_t played() const {
        return stream ? AAudioStream_getFramesRead(stream) : produced;
    }
#else
    void open() {}
    void close() {}
    std::int64_t played() const {
        return produced;
    }
#endif
    explicit Impl(const Archive &a) : archive(a) {
        config.enabled_a4 = true;
        // A temporarily unavailable output device must not prevent the native
        // game/session from being created. Retry from the game thread, using
        // the same backoff as a later device disconnection.
        try {
            open();
        } catch (const std::exception &e) {
            failure = e.what();
            retry_after = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
    }
    ~Impl() {
        close();
    }
    void mix(float *out, int frames) {
        std::fill(out, out + frames * channels, 0.0F);
        std::lock_guard<std::mutex> lock(mutex);
        if (!active) {
            produced += frames;
            return;
        }
        const auto consumed = played();
        for (auto &v : voices) {
            if (!v.clip)
                continue;
            if (v.finish_frame >= 0) {
                if (consumed >= v.finish_frame)
                    v = Voice{};
                continue;
            }
            const auto &clip = *v.clip;
            const double step = double(clip.sample_rate) / rate;
            for (int i = 0; i < frames; ++i) {
                auto at = std::size_t(v.cursor);
                if (at >= clip.frames()) {
                    v.finish_frame = produced + i;
                    break;
                }
                const auto next = std::min(at + 1, clip.frames() - 1);
                const float alpha = float(v.cursor - at);
                const float value =
                    clip.samples[at] + (clip.samples[next] - clip.samples[at]) * alpha;
                for (int c = 0; c < channels; ++c)
                    out[i * channels + c] += value;
                v.cursor += step;
            }
        }
        for (int i = 0; i < frames * channels; ++i)
            out[i] = std::clamp(out[i], -1.0F, 1.0F);
        produced += frames;
    }
    bool playing() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!active)
            return false;
        const auto consumed = played();
        bool any = false;
        for (auto &v : voices)
            if (v.clip) {
                if (v.finish_frame >= 0 && consumed >= v.finish_frame)
                    v = Voice{};
                else
                    any = true;
            }
        return any;
    }
    void stop() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &v : voices)
            v = Voice{};
    }
};
AndroidAudio::AndroidAudio(const Archive &a) : impl_(std::make_unique<Impl>(a)) {}
AndroidAudio::~AndroidAudio() = default;
void AndroidAudio::maintain() {
    auto &p = *impl_;
#ifdef __ANDROID__
    const auto now = std::chrono::steady_clock::now();
    if (p.disconnected.exchange(false)) {
        p.close();
        p.failure = "Android audio device disconnected";
        p.retry_after = now;
    }
    if (!p.active || p.failure.empty() || now < p.retry_after)
        return;
    p.close();
    {
        std::lock_guard<std::mutex> l(p.mutex);
        for (auto &v : p.voices)
            if (v.finish_frame >= 0)
                v = {};
    }
    try {
        p.open();
        p.failure.clear();
    } catch (const std::exception &e) {
        p.failure = e.what();
        p.retry_after = now + std::chrono::milliseconds(500);
    }
#endif
}
bool AndroidAudio::request(MissionAudioRequest r) {
    auto &p = *impl_;
    maintain();
    auto next = p.control;
    // Keep the original preload/control plan alive while hardware is missing.
    // Otherwise startup preload groups are discarded and effects remain
    // permanently unloaded after the output stream recovers.
    const bool outputAvailable = p.failure.empty();
    auto plan = dispatch_audio_request(
        next, p.config, {p.active && outputAvailable, outputAvailable && p.playing()}, r);
    for (const auto action : plan.actions)
        switch (action.kind) {
        case AudioActionKind::load:
            if (!p.clips.at(action.slot)) {
                auto clip = std::make_shared<PcmAudio>(decode_audio_slot(p.archive, action.slot));
                if (clip->channels != 1 || clip->sample_rate != 22050 || !clip->frames())
                    throw FormatError("Original audio corpus format mismatch");
                p.clips[action.slot] = std::move(clip);
            }
            break;
        case AudioActionKind::unload:
            p.clips.at(action.slot).reset();
            break;
        case AudioActionKind::stop_all:
            p.stop();
            break;
        case AudioActionKind::play_once:
            if ((action.slot <= 3 && !p.config.enabled_a4) ||
                (action.slot > 3 && !p.config.enabled_a3))
                break;
            {
                auto clip = p.clips.at(action.slot);
                if (!clip)
                    throw FormatError("Audio slot not decoded before playback");
                std::lock_guard<std::mutex> lock(p.mutex);
                auto it = std::find_if(p.voices.begin(), p.voices.end(),
                                       [](const auto &v) { return !v.clip; });
                if (it == p.voices.end())
                    throw FormatError("Native 64-voice audio bound reached");
                *it = {clip, 0, action.slot, -1};
            }
            break;
        }
    p.control = next;
    return plan.return_value;
}
void AndroidAudio::foreground(bool active) {
    auto &p = *impl_;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.active = active;
    }
#ifdef __ANDROID__
    if (active)
        maintain();
    if (p.stream) {
        const auto state = AAudioStream_getState(p.stream);
        aaudio_result_t result = AAUDIO_OK;
        if (active && state != AAUDIO_STREAM_STATE_STARTED && state != AAUDIO_STREAM_STATE_STARTING)
            result = AAudioStream_requestStart(p.stream);
        else if (!active &&
                 (state == AAUDIO_STREAM_STATE_STARTED || state == AAUDIO_STREAM_STATE_STARTING))
            result = AAudioStream_requestPause(p.stream);
        if (result != AAUDIO_OK) {
            p.failure = AAudio_convertResultToText(result);
            p.disconnected.store(true);
        }
    }
#endif
    if (active)
        maintain();
}
void AndroidAudio::configure(bool music, bool sound) {
    auto &p = *impl_;
    p.config.enabled_a4 = music;
    p.config.enabled_a3 = sound;
    p.config.suppress_music_a0 = !music;
    std::lock_guard<std::mutex> lock(p.mutex);
    for (auto &v : p.voices)
        if (v.clip && ((v.slot <= 3 && !music) || (v.slot > 3 && !sound)))
            v = {};
}
bool AndroidAudio::musicEnabled() const {
    return impl_->config.enabled_a4;
}
bool AndroidAudio::soundEnabled() const {
    return impl_->config.enabled_a3;
}
const std::string &AndroidAudio::error() const {
    return impl_->failure;
}
} // namespace g1::android
