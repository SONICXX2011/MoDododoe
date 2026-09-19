#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <thread>
#include <chrono>

extern "C" {
#include <shadowhook.h>
}

#include "BNM/Loading.hpp"
#include "BNM/Image.hpp"
#include "BNM/Utils.hpp"

#include "Globals.h"
#include "JavaBridge.h"
#include "ModCore.h"

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

JavaVM*      g_vm = nullptr;
jclass       g_bridgeClass = nullptr;
jobject      g_bridgeInstance = nullptr;
RuntimeState g_state;

JNIEnv* GetEnv() {
    if (!g_vm) return nullptr;
    JNIEnv* env = nullptr;
    jint r = g_vm->GetEnv((void**) &env, JNI_VERSION_1_6);
    if (r == JNI_OK) return env;
    if (r == JNI_EDETACHED && g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK)
        return env;
    return nullptr;
}

bool AttachThread(JNIEnv** env, bool* needsDetach) {
    *needsDetach = false;
    if (!g_vm) return false;
    jint r = g_vm->GetEnv((void**) env, JNI_VERSION_1_6);
    if (r == JNI_OK) return true;
    if (r == JNI_EDETACHED && g_vm->AttachCurrentThread(env, nullptr) == JNI_OK) {
        *needsDetach = true;
        return true;
    }
    return false;
}

void DetachThreadIfNeeded(bool needsDetach) {
    if (needsDetach && g_vm) g_vm->DetachCurrentThread();
}

// ═══════════════════════════════════════════════════════
// BNM ready callback
// ═══════════════════════════════════════════════════════
static void OnBNMLoaded() {
    LOGI("=== BNM READY ===");
    JB_Log("=== BNM READY ===");

    try {
        auto imgs = BNM::Image::GetImages();
        LOGI("Loaded images (%zu):", imgs.size());
        JB_Log("Images: " + std::to_string(imgs.size()));

        for (auto& img : imgs) {
            auto* info = img.GetInfo();
            if (info && info->name) {
                LOGI("  %s", info->name);
                JB_Log(std::string("  ") + info->name);
            }
        }
    } catch (...) {
        LOGE("Failed to list images");
    }

    InstallGameHooks();
    StartStateLoop();
}

// ═══════════════════════════════════════════════════════
// Fallback poll — فقط اگه AllowLateInitHook جواب نداد
// (الان غیرفعاله، چون AllowLateInitHook کار می‌کنه)
// ═══════════════════════════════════════════════════════
static void PollLibil2cpp() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    LOGI("Poll thread: checking libil2cpp.so");
    JB_Log("Polling libil2cpp...");

    void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!handle) {
        LOGI("libil2cpp.so not loaded, will rely on late hook");
        JB_Log("libil2cpp not loaded");
        return;
    }

    LOGI("libil2cpp.so found, trying TryLoadByDlfcnHandle");
    JB_Log("libil2cpp found");

    bool ok = BNM::Loading::TryLoadByDlfcnHandle(handle);
    LOGI("TryLoadByDlfcnHandle -> %s", ok ? "OK" : "FAIL");
    JB_Log(std::string("TryLoadByDlfcnHandle: ") + (ok ? "OK" : "FAIL"));
}

// ═══════════════════════════════════════════════════════
// JNI_OnLoad
// ═══════════════════════════════════════════════════════
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    LOGI("JNI_OnLoad");
    g_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("no JNIEnv");
        return JNI_VERSION_1_6;
    }

    // 1. ShadowHook
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("shadowhook_init -> %d", r);

    // 2. فعال‌سازی لود دیرهنگام (کلید حل مشکل!)
    BNM::Loading::AllowLateInitHook();
    LOGI("AllowLateInitHook called");

    // 3. callbacks
    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);

    // 4. تلاش اولیه
    bool ok = BNM::Loading::TryLoadByJNI(env);
    LOGI("TryLoadByJNI -> %s", ok ? "OK" : "deferred");

    // 5. پولینگ به عنوان fallback
    std::thread(PollLibil2cpp).detach();

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
    LOGI("JNI_OnUnload");
    StopStateLoop();
}