#include "HapticEngine.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <jni.h>
#include "platform/android/jni/JniHelper.h"

using namespace geode::prelude;

namespace zaid {
namespace {
constexpr unsigned ix(HapticEvent event) { return static_cast<unsigned>(event); }
constexpr int clampMs(int ms) { return std::clamp(ms, 1, 250); }

bool noException(JNIEnv* env) {
    if (!env->ExceptionCheck()) return true;
    // A missing VIBRATE manifest permission, unsupported vibration effect,
    // or framework error must not crash Geometry Dash.
    env->ExceptionClear();
    return false;
}

// Fallback: Android's view-mediated feedback normally does not require the
// host APK to declare VIBRATE. Android controls strength and duration here.
bool viewFeedback(JNIEnv* env, jobject activity) {
    jclass activityCls = env->GetObjectClass(activity);
    if (!activityCls || !noException(env)) return false;
    jmethodID getWindow = env->GetMethodID(activityCls, "getWindow", "()Landroid/view/Window;");
    if (!getWindow || !noException(env)) { env->DeleteLocalRef(activityCls); return false; }
    jobject window = env->CallObjectMethod(activity, getWindow);
    if (!window || !noException(env)) { env->DeleteLocalRef(activityCls); return false; }
    jclass windowCls = env->GetObjectClass(window);
    jmethodID getDecorView = windowCls ? env->GetMethodID(windowCls, "getDecorView", "()Landroid/view/View;") : nullptr;
    if (!noException(env) || !getDecorView) {
        if (windowCls) env->DeleteLocalRef(windowCls);
        env->DeleteLocalRef(window); env->DeleteLocalRef(activityCls); return false;
    }
    jobject view = env->CallObjectMethod(window, getDecorView);
    if (!view || !noException(env)) {
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(windowCls); env->DeleteLocalRef(window); env->DeleteLocalRef(activityCls);
        return false;
    }
    jclass viewCls = env->GetObjectClass(view);
    jmethodID feedback = viewCls ? env->GetMethodID(viewCls, "performHapticFeedback", "(I)Z") : nullptr;
    bool ok = false;
    if (noException(env) && feedback) {
        // HapticFeedbackConstants.VIRTUAL_KEY = 1
        ok = env->CallBooleanMethod(view, feedback, static_cast<jint>(1)) == JNI_TRUE;
        ok = noException(env) && ok;
    }
    if (viewCls) env->DeleteLocalRef(viewCls);
    env->DeleteLocalRef(view);
    env->DeleteLocalRef(windowCls);
    env->DeleteLocalRef(window);
    env->DeleteLocalRef(activityCls);
    return ok;
}
}

HapticEngine& HapticEngine::instance() {
    static HapticEngine engine;
    return engine;
}

void HapticEngine::updateSettings() {
    auto mod = Mod::get();
    HapticSettings s;
    s.enabled = mod->getSettingValue<bool>("haptic-enable");
    s.amplitude = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>("haptic-intensity")) * 255 / 100, 1, 255);
    s.jumpCooldownMs = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>("haptic-jump-cooldown")), 60, 500);
    constexpr char const* names[] = {"jump", "death", "checkpoint", "complete", "pause"};
    for (unsigned i = 0; i < ix(HapticEvent::Count); ++i) {
        s.events[i] = mod->getSettingValue<bool>(fmt::format("haptic-{}", names[i]));
        s.durationMs[i] = clampMs(static_cast<int>(mod->getSettingValue<int64_t>(fmt::format("haptic-{}-ms", names[i]))));
    }
    m_settings = s;
}

void HapticEngine::trigger(HapticEvent event) {
    auto index = ix(event);
    if (index >= ix(HapticEvent::Count) || !m_settings.enabled || !m_settings.events[index]) return;
    auto now = std::chrono::steady_clock::now();
    // Enforce a minimum interval for jumps and repeated collision events.
    auto cooldown = event == HapticEvent::Jump ? m_settings.jumpCooldownMs : 90;
    if (now - m_last[index] < std::chrono::milliseconds(cooldown)) return;
    m_last[index] = now;
    vibrateAndroid(m_settings.durationMs[index], m_settings.amplitude);
}

bool HapticEngine::vibrateAndroid(int durationMs, int amplitude) {
    JNIEnv* env = cocos2d::JniHelper::getEnv();
    jobject activity = cocos2d::JniHelper::getActivity();
    if (!env || !activity) return false;

    bool ok = false;
    jclass contextCls = env->GetObjectClass(activity);
    if (!contextCls || !noException(env)) return false;
    jmethodID getService = env->GetMethodID(contextCls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!getService || !noException(env)) { env->DeleteLocalRef(contextCls); return false; }
    jstring serviceName = env->NewStringUTF("vibrator");
    jobject vibrator = serviceName ? env->CallObjectMethod(activity, getService, serviceName) : nullptr;
    if (!noException(env) || !vibrator) {
        if (serviceName) env->DeleteLocalRef(serviceName);
        env->DeleteLocalRef(contextCls);
        return viewFeedback(env, activity);
    }
    jclass vibratorCls = env->GetObjectClass(vibrator);
    if (vibratorCls && noException(env)) {
        jmethodID hasVibrator = env->GetMethodID(vibratorCls, "hasVibrator", "()Z");
        if (hasVibrator && noException(env) && env->CallBooleanMethod(vibrator, hasVibrator) == JNI_TRUE && noException(env)) {
            jclass effectCls = env->FindClass("android/os/VibrationEffect");
            if (effectCls && noException(env)) {
                jmethodID makeEffect = env->GetStaticMethodID(effectCls, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
                jobject effect = makeEffect && noException(env) ? env->CallStaticObjectMethod(effectCls, makeEffect, static_cast<jlong>(durationMs), static_cast<jint>(amplitude)) : nullptr;
                if (effect && noException(env)) {
                    jmethodID vibrate = env->GetMethodID(vibratorCls, "vibrate", "(Landroid/os/VibrationEffect;)V");
                    if (vibrate && noException(env)) {
                        env->CallVoidMethod(vibrator, vibrate, effect);
                        ok = noException(env);
                    }
                } else noException(env);
                if (effect) env->DeleteLocalRef(effect);
                env->DeleteLocalRef(effectCls);
            } else noException(env);
        }
    }
    if (vibratorCls) env->DeleteLocalRef(vibratorCls);
    env->DeleteLocalRef(vibrator);
    if (serviceName) env->DeleteLocalRef(serviceName);
    env->DeleteLocalRef(contextCls);
    // If the host APK lacks VIBRATE permission, request a standard UI haptic.
    // The OS may suppress it if system-wide haptics are disabled.
    return ok || viewFeedback(env, activity);
}
}
