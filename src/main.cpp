#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "RawEvdev.hpp"
#include "RootInputReader.hpp"
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace zaid {
// These flags are accessed ONLY from the game/main thread.
bool s_held[2] = {false, false};

constexpr int kBindingCount = 8;

std::string keySettingName(int index) {
    return "binding-" + std::to_string(index) + "-key";
}

std::string actionSettingName(int index) {
    return "binding-" + std::to_string(index) + "-action";
}

void setJumpHeld(PlayLayer* pl, int player, bool down) {
    if (player < 0 || player > 1) return;
    if (s_held[player] == down) return;
    s_held[player] = down;
    if (pl) pl->handleButton(down, 1, player == 0);
}

InputConfig loadConfig() {
    auto mod = Mod::get();
    InputConfig config;
    config.enabled = mod->getSettingValue<bool>("enable-root-mapper");
    config.device = static_cast<int>(mod->getSettingValue<int64_t>("event-device"));
    for (int i = 0; i < kBindingCount; ++i) {
        auto slot = i + 1;
        config.bindings[i].key = mapButtonName(mod->getSettingValue<std::string>(keySettingName(slot)));
        config.bindings[i].action = mapActionName(mod->getSettingValue<std::string>(actionSettingName(slot)));
    }
    return config;
}

void reloadConfig() { RootInputReader::instance().configure(loadConfig()); }

void runAction(PlayLayer* pl, ActionType action, bool down) {
    if (!pl) return;
    switch (action) {
        case ActionType::None:
            return;
        case ActionType::JumpP1:
            setJumpHeld(pl, 0, down);
            return;
        case ActionType::JumpP2:
            setJumpHeld(pl, 1, down);
            return;
        case ActionType::JumpBoth:
            setJumpHeld(pl, 0, down);
            setJumpHeld(pl, 1, down);
            return;
        case ActionType::Pause:
            if (down && pl->canPauseGame() && !pl->m_isPaused) pl->pauseGame(true);
            return;
        case ActionType::Restart:
            if (down) pl->resetLevel();
            return;
        case ActionType::FullRestart:
            if (down) pl->resetLevelFromStart();
            return;
        case ActionType::PracticeToggle:
            if (down) pl->togglePracticeMode(!pl->m_isPracticeMode);
            return;
        case ActionType::PlaceCheckpoint:
            if (down && pl->m_isPracticeMode) pl->createCheckpoint();
            return;
        case ActionType::LoadLastCheckpoint:
            if (down && pl->m_isPracticeMode) pl->loadLastCheckpoint();
            return;
        case ActionType::ClearCheckpoints:
            if (down && pl->m_isPracticeMode) pl->removeAllCheckpoints();
            return;
    }
}

ActionType mapActionName(std::string_view name) {
    if (name == "NONE") return ActionType::None;
    if (name == "JUMP_P1") return ActionType::JumpP1;
    if (name == "JUMP_P2") return ActionType::JumpP2;
    if (name == "JUMP_BOTH") return ActionType::JumpBoth;
    if (name == "PAUSE") return ActionType::Pause;
    if (name == "RESTART") return ActionType::Restart;
    if (name == "FULL_RESTART") return ActionType::FullRestart;
    if (name == "PRACTICE_TOGGLE") return ActionType::PracticeToggle;
    if (name == "PLACE_CHECKPOINT") return ActionType::PlaceCheckpoint;
    if (name == "LOAD_LAST_CHECKPOINT") return ActionType::LoadLastCheckpoint;
    if (name == "CLEAR_CHECKPOINTS") return ActionType::ClearCheckpoints;
    return ActionType::None;
}

void releaseMappedInputs() {
    auto pl = PlayLayer::get();
    for (int i = 0; i < 2; ++i) {
        if (!s_held[i]) continue;
        s_held[i] = false;
        if (pl) pl->handleButton(false, 1, i == 0);
    }
}

void handleMappedInput(int code, bool down, InputConfig const& config) {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_player1) {
        if (!down) releaseMappedInputs();
        return;
    }
    for (auto const& binding : config.bindings) {
        if (binding.key != code || binding.action == ActionType::None) continue;
        runAction(pl, binding.action, down);
    }
}

} // namespace zaid

class $modify(ZaidRootPlayLayer, PlayLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* statusLabel = nullptr;
        float statusClock = 0.f;
        int statusShown = -1;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        zaid::releaseMappedInputs();
        zaid::RootInputReader::instance().setGameplayActive(true);
        if (Mod::get()->getSettingValue<bool>("show-status")) {
            auto label = cocos2d::CCLabelBMFont::create("ROOT INPUT: ...", "goldFont.fnt");
            if (label) {
                label->setAnchorPoint({0.f, 1.f});
                label->setScale(0.30f);
                label->setPosition({8.f, cocos2d::CCDirector::sharedDirector()->getWinSize().height - 7.f});
                this->addChild(label, 999);
                m_fields->statusLabel = label;
            }
        }
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!m_fields->statusLabel) return;
        m_fields->statusClock += dt;
        if (m_fields->statusClock < 0.20f) return;
        m_fields->statusClock = 0.f;
        auto state = zaid::RootInputReader::instance().status();
        if (m_fields->statusShown == static_cast<int>(state)) return;
        m_fields->statusShown = static_cast<int>(state);
        char const* message = "ROOT INPUT: OFF";
        if (state == zaid::ReaderStatus::Waiting) message = "ROOT INPUT: SU...";
        if (state == zaid::ReaderStatus::Active) message = "ROOT INPUT: ON";
        if (state == zaid::ReaderStatus::Error) message = "ROOT INPUT: ERROR";
        m_fields->statusLabel->setString(message);
    }

    void pauseGame(bool paused) {
        if (paused) zaid::releaseMappedInputs();
        PlayLayer::pauseGame(paused);
    }

    void onQuit() {
        zaid::RootInputReader::instance().setGameplayActive(false);
        zaid::releaseMappedInputs();
        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    zaid::RootInputReader::instance().start();
    listenForSettingChanges<bool>("enable-root-mapper", [](bool) { zaid::reloadConfig(); });
    listenForSettingChanges<int64_t>("event-device", [](int64_t) { zaid::reloadConfig(); });
    for (int i = 1; i <= zaid::kBindingCount; ++i) {
        listenForSettingChanges<std::string>(zaid::keySettingName(i), [](std::string) { zaid::reloadConfig(); });
        listenForSettingChanges<std::string>(zaid::actionSettingName(i), [](std::string) { zaid::reloadConfig(); });
    }
    zaid::reloadConfig();
}
