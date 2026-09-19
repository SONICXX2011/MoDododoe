#include <jni.h>
#include <android/log.h>
#include <pthread.h>

extern "C" {
#include <shadowhook.h>
}

#include "BNM/Loading.hpp"
#include "BNM/Image.hpp"
#include "BNM/Utils.hpp"

#include "Globals.h"
#include "ModCore.h"

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─── Globals ───
JavaVM*      g_vm = nullptr;
jclass       g_bridgeClass = nullptr;
jobject      g_bridgeInstance = nullptr;
RuntimeState g_state;

// ═══════════════════════════════════════════════════════
// JNI helpers
// ═══════════════════════════════════════════════════════
JNIEnv* GetEnv() {
    if (!g_vm) return nullptr;

    JNIEnv* env = nullptr;
    jint r = g_vm->GetEnv((void**) &env, JNI_VERSION_1_6);

    if (r == JNI_OK) return env;

    if (r == JNI_EDETACHED &&
        g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        return env;
    }
    return nullptr;
}

bool AttachThread(JNIEnv** env, bool* needsDetach) {
    *needsDetach = false;
    if (!g_vm) return false;

    jint r = g_vm->GetEnv((void**) env, JNI_VERSION_1_6);
    if (r == JNI_OK) return true;

    if (r == JNI_EDETACHED &&
        g_vm->AttachCurrentThread(env, nullptr) == JNI_OK) {
        *needsDetach = true;
        return true;
    }
    return false;
}

void DetachThreadIfNeeded(bool needsDetach) {
    if (needsDetach && g_vm) {
        g_vm->DetachCurrentThread();
    }
}

// ═══════════════════════════════════════════════════════
// BNM on-loaded callback
// ═══════════════════════════════════════════════════════
static void OnBNMLoaded() {
    LOGI("╔════════════════════════════════════════╗");
    LOGI("║  BNM ready — installing hooks          ║");
    LOGI("╚════════════════════════════════════════╝");

    try {
        auto imgs = BNM::Image::GetImages();
        LOGI("Loaded images (%zu):", imgs.size());

        for (auto& img : imgs) {
            auto* info = img.GetInfo();
            if (info && info->name) {
                LOGI("  %s", info->name);
            }
        }
    } catch (...) {
        LOGE("Failed to enumerate images");
    }

    InstallGameHooks();
    StartStateLoop();
}

// ═══════════════════════════════════════════════════════
// JNI_OnLoad
// ═══════════════════════════════════════════════════════
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    LOGI("JNI_OnLoad");
    g_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("no JNIEnv at OnLoad");
        return JNI_VERSION_1_6;
    }

    // 1. ShadowHook init
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("shadowhook_init -> %d", r);

    // 2. BNM load callback
    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);

    // 3. Trigger il2cpp load
    BNM::Loading::TryLoadByJNI(env);
    LOGI("BNM::TryLoadByJNI called");

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
    LOGI("JNI_OnUnload");
    StopStateLoop();
}