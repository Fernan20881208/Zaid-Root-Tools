#include "HapticEngine.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <jni.h>
#include <Geode/cocos/platform/android/jni/JniHelper.h>

using namespace geode::prelude;

namespace zaid {
namespace {
constexpr unsigned ix(HapticEvent event) { return static_cast<unsigned>(event); }
constexpr int clampMs(int ms) { return std::clamp(ms, 1, 250); }

// JNI exceptions must be cleared BEFORE returning to Android's GL renderer.
// Do not use short-circuit expressions that can skip this call.
bool jniOK(JNIEnv* env) {
    if (!env->ExceptionCheck()) return true;
    env->ExceptionClear();
    return false;
}

struct LocalRef {
    JNIEnv* env;
    jobject ref;
    ~LocalRef() { if (ref) env->DeleteLocalRef(ref); }
    LocalRef(LocalRef const&) = delete;
    LocalRef& operator=(LocalRef const&) = delete;
    LocalRef(JNIEnv* e, jobject r) : env(e), ref(r) {}
};

// Cocos2dxHelper doesn't expose an sContext field on every Geometry Dash
// Android version. Use public methods instead, and tolerate absent methods.
jobject resolveContext(JNIEnv* env) {
    {
        LocalRef helper(env, env->FindClass("org/cocos2dx/lib/Cocos2dxHelper"));
        if (jniOK(env) && helper.ref) {
            jmethodID getter = env->GetStaticMethodID(
                static_cast<jclass>(helper.ref), "getActivity", "()Landroid/app/Activity;");
            if (jniOK(env) && getter) {
                jobject activity = env->CallStaticObjectMethod(static_cast<jclass>(helper.ref), getter);
                if (jniOK(env) && activity) return activity;
                if (activity) env->DeleteLocalRef(activity);
            }
        }
    }
    {
        LocalRef cls(env, env->FindClass("org/cocos2dx/lib/Cocos2dxActivity"));
        if (jniOK(env) && cls.ref) {
            jmethodID getter = env->GetStaticMethodID(
                static_cast<jclass>(cls.ref), "getContext", "()Landroid/content/Context;");
            if (jniOK(env) && getter) {
                jobject context = env->CallStaticObjectMethod(static_cast<jclass>(cls.ref), getter);
                if (jniOK(env) && context) return context;
                if (context) env->DeleteLocalRef(context);
            }
        }
    }
    // Optional fallback: on some Cocos variants helper getActivity is absent,
    // but the VM can give us the app context. This is a non-SDK Android method;
    // if blocked, JNI errors are cleared and haptics are simply skipped.
    {
        LocalRef cls(env, env->FindClass("android/app/ActivityThread"));
        if (jniOK(env) && cls.ref) {
            jmethodID getter = env->GetStaticMethodID(
                static_cast<jclass>(cls.ref), "currentApplication", "()Landroid/app/Application;");
            if (jniOK(env) && getter) {
                jobject app = env->CallStaticObjectMethod(static_cast<jclass>(cls.ref), getter);
                if (jniOK(env) && app) return app;
                if (app) env->DeleteLocalRef(app);
            }
        }
    }
    jniOK(env);
    return nullptr;
}

// View.performHapticFeedback is a safe, short fallback when the app doesn't
// declare android.permission.VIBRATE. It only works with an Activity context.
bool viewFeedback(JNIEnv* env, jobject context) {
    LocalRef activityClass(env, env->FindClass("android/app/Activity"));
    if (!jniOK(env) || !activityClass.ref) return false;
    auto isActivity = env->IsInstanceOf(context, static_cast<jclass>(activityClass.ref));
    if (!jniOK(env) || isActivity != JNI_TRUE) return false;

    LocalRef contextClass(env, env->GetObjectClass(context));
    if (!jniOK(env) || !contextClass.ref) return false;
    auto getWindow = env->GetMethodID(static_cast<jclass>(contextClass.ref), "getWindow", "()Landroid/view/Window;");
    if (!jniOK(env) || !getWindow) return false;
    LocalRef window(env, env->CallObjectMethod(context, getWindow));
    if (!jniOK(env) || !window.ref) return false;
    LocalRef windowClass(env, env->GetObjectClass(window.ref));
    if (!jniOK(env) || !windowClass.ref) return false;
    auto getDecorView = env->GetMethodID(static_cast<jclass>(windowClass.ref), "getDecorView", "()Landroid/view/View;");
    if (!jniOK(env) || !getDecorView) return false;
    LocalRef view(env, env->CallObjectMethod(window.ref, getDecorView));
    if (!jniOK(env) || !view.ref) return false;
    LocalRef viewClass(env, env->GetObjectClass(view.ref));
    if (!jniOK(env) || !viewClass.ref) return false;
    auto perform = env->GetMethodID(static_cast<jclass>(viewClass.ref), "performHapticFeedback", "(I)Z");
    if (!jniOK(env) || !perform) return false;
    auto result = env->CallBooleanMethod(view.ref, perform, static_cast<jint>(1)); // VIRTUAL_KEY
    return jniOK(env) && result == JNI_TRUE;
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
    JavaVM* vm = cocos2d::JniHelper::getJavaVM();
    if (!vm) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    auto status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    } else if (status != JNI_OK) return false;
    struct DetachGuard {
        JavaVM* vm;
        JNIEnv* env;
        bool attached;
        ~DetachGuard() {
            // The VM must NEVER see an unhandled Java exception from a mod.
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (attached) vm->DetachCurrentThread();
        }
    } detach{vm, env, attached};

    LocalRef context(env, resolveContext(env));
    if (!context.ref) return false;
    LocalRef contextClass(env, env->GetObjectClass(context.ref));
    if (!jniOK(env) || !contextClass.ref) return false;
    auto getService = env->GetMethodID(static_cast<jclass>(contextClass.ref),
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!jniOK(env) || !getService) return viewFeedback(env, context.ref);
    LocalRef serviceName(env, env->NewStringUTF("vibrator"));
    if (!jniOK(env) || !serviceName.ref) return viewFeedback(env, context.ref);
    LocalRef vibrator(env, env->CallObjectMethod(context.ref, getService, serviceName.ref));
    if (!jniOK(env) || !vibrator.ref) return viewFeedback(env, context.ref);
    LocalRef vibratorClass(env, env->GetObjectClass(vibrator.ref));
    if (!jniOK(env) || !vibratorClass.ref) return viewFeedback(env, context.ref);
    auto hasVibrator = env->GetMethodID(static_cast<jclass>(vibratorClass.ref), "hasVibrator", "()Z");
    if (!jniOK(env) || !hasVibrator) return viewFeedback(env, context.ref);
    auto available = env->CallBooleanMethod(vibrator.ref, hasVibrator);
    if (!jniOK(env) || available != JNI_TRUE) return false;

    bool ok = false;
    LocalRef effectClass(env, env->FindClass("android/os/VibrationEffect"));
    if (jniOK(env) && effectClass.ref) {
        auto create = env->GetStaticMethodID(static_cast<jclass>(effectClass.ref),
            "createOneShot", "(JI)Landroid/os/VibrationEffect;");
        if (jniOK(env) && create) {
            LocalRef effect(env, env->CallStaticObjectMethod(static_cast<jclass>(effectClass.ref),
                create, static_cast<jlong>(durationMs), static_cast<jint>(amplitude)));
            if (jniOK(env) && effect.ref) {
                auto vibrate = env->GetMethodID(static_cast<jclass>(vibratorClass.ref),
                    "vibrate", "(Landroid/os/VibrationEffect;)V");
                if (jniOK(env) && vibrate) {
                    env->CallVoidMethod(vibrator.ref, vibrate, effect.ref);
                    ok = jniOK(env);
                }
            }
        }
    }
    // If Android disallows direct vibration, use standard view feedback.
    if (!ok) ok = viewFeedback(env, context.ref);
    return ok;
}

} // namespace zaid
