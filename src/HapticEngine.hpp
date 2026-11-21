#pragma once
#include <array>
#include <chrono>

namespace zaid {

enum class HapticEvent : unsigned { Jump, Death, Checkpoint, Complete, Pause, Count };

struct HapticSettings {
    bool enabled = false;
    std::array<bool, static_cast<unsigned>(HapticEvent::Count)> events{};
    std::array<int, static_cast<unsigned>(HapticEvent::Count)> durationMs{};
    int amplitude = 110; // 1..255; hardware may quantize or ignore this
    int jumpCooldownMs = 90;
};

class HapticEngine {
public:
    static HapticEngine& instance();
    // Called on Geode's main/game thread only.
    void updateSettings();
    void trigger(HapticEvent event);
private:
    HapticEngine() = default;
    bool vibrateAndroid(int durationMs, int amplitude);
    HapticSettings m_settings;
    std::array<std::chrono::steady_clock::time_point,
               static_cast<unsigned>(HapticEvent::Count)> m_last{};
};
}
