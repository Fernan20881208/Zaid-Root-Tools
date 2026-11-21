#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "RawEvdev.hpp"
#include "RootInputReader.hpp"
#include <string>

using namespace geode::prelude;

namespace zaid {
namespace {
// These two flags are accessed ONLY from the game/main thread.
bool s_held[2] = {false, false};

InputConfig loadConfig() {
    auto mod = Mod::get();
    InputConfig config;
    config.enabled = mod->getSettingValue<bool>("enable-root-mapper");
    config.device = static_cast<int>(mod->getSettingValue<int64_t>("event-device"));
    config.player1 = mapButtonName(mod->getSettingValue<std::string>("player-1-key"));
    config.player2 = mapButtonName(mod->getSettingValue<std::string>("player-2-key"));
    return config;
}

void reloadConfig() { RootInputReader::instance().configure(loadConfig()); }
}

void releaseMappedInputs() {
    auto pl = PlayLayer::get();
    for (int i = 0; i < 2; ++i) {
        if (!s_held[i]) continue;
        s_held[i] = false;
        if (pl) pl->handleButton(false, 1, i == 0);
    }
}

void handleMappedInput(int code, bool down, int p1, int p2) {
    auto pl = PlayLayer::get();
    if (!pl || pl->m_isPaused || !pl->m_player1) {
        // A physical release can arrive after the player leaves the level.
        if (!down) releaseMappedInputs();
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (code != (i == 0 ? p1 : p2)) continue;
        if (s_held[i] == down) continue; // debounces repeated key events
        s_held[i] = down;
        pl->handleButton(down, 1, i == 0);
    }
}
}

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
    listenForSettingChanges("enable-root-mapper", [](bool) { zaid::reloadConfig(); });
    listenForSettingChanges("event-device", [](int64_t) { zaid::reloadConfig(); });
    listenForSettingChanges("player-1-key", [](std::string) { zaid::reloadConfig(); });
    listenForSettingChanges("player-2-key", [](std::string) { zaid::reloadConfig(); });
    zaid::reloadConfig();
}
