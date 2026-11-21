#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string_view>

namespace zaid {
enum class ActionType : uint8_t {
    None,
    JumpP1,
    JumpP2,
    JumpBoth,
    Pause,
    Restart,
    FullRestart,
    PracticeToggle,
    PlaceCheckpoint,
    LoadLastCheckpoint,
    ClearCheckpoints,
};

struct InputBinding {
    int key = -1;
    ActionType action = ActionType::None;
    bool operator==(InputBinding const&) const = default;
};

struct InputConfig {
    bool enabled = false;
    int device = -1;     // Event number only, never a user-supplied shell path
    std::array<InputBinding, 8> bindings{};
    bool operator==(InputConfig const&) const = default;
};

enum class ReaderStatus : int { Off, Waiting, Active, Error };

class RootInputReader {
public:
    static RootInputReader& instance();
    void start();
    // Call from Geode's main thread, including from settings listeners.
    void configure(InputConfig const& config);
    // Restricts privileged device reads to the lifetime of an active level.
    void setGameplayActive(bool active);
    ReaderStatus status() const { return m_status.load(); }
    uint64_t generation() const { return m_generation.load(); }
private:
    RootInputReader() = default;
    RootInputReader(RootInputReader const&) = delete;
    RootInputReader& operator=(RootInputReader const&) = delete;
    void workerLoop();
    void runSession(InputConfig const& config, uint64_t session);
    bool shouldEnd(uint64_t session);
    void postKey(uint64_t session, InputConfig const& config, int code, bool down);
    bool applyEffectiveConfigLocked();

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    InputConfig m_preference;
    bool m_gameplayActive = false;
    InputConfig m_requested;
    uint64_t m_requestedGeneration = 0;
    bool m_started = false;
    std::atomic<uint64_t> m_generation{0};
    std::atomic<ReaderStatus> m_status{ReaderStatus::Off};
};

// Implemented by the Geode integration and always called on its main thread.
void handleMappedInput(int code, bool down, InputConfig const& config);
void releaseMappedInputs();
ActionType mapActionName(std::string_view name);
}
