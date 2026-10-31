#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <optional>
#include "g1/Image.hpp"
namespace g1::android {
class AndroidGame {
  public:
    AndroidGame(std::string assets, std::string saves, bool modern);
    ~AndroidGame();
    void attachGraphics();
    void detachGraphics(bool context_lost = false);
    void resize(int width, int height, int left, int top, int right, int bottom);
    void draw(std::uint64_t delta_nanos);
    void presented(bool succeeded);
    void touch(int action, float x, float y, float dx, float dy, float scale);
    bool back();
    void pause();
    void resume();
    void audioFocus(bool focused);
    void resourcePressure(bool low_power, bool thermal_serious, bool memory_pressure);
    std::optional<std::string> accessibilitySnapshot(bool force);
    std::string diagnostics() const;
    void setTextRasterizer(std::function<RgbaImage(const std::string &, float)>);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace g1::android
