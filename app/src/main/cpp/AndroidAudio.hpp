#pragma once
#include "g1/AudioEvents.hpp"
#include <memory>
namespace g1::android {
class AndroidAudio {
  public:
    explicit AndroidAudio(const Archive &);
    ~AndroidAudio();
    bool request(MissionAudioRequest);
    void foreground(bool);
    void configure(bool music, bool sound);
    void maintain();
    bool musicEnabled() const;
    bool soundEnabled() const;
    const std::string &error() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace g1::android
