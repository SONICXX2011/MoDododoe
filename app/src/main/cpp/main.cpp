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

// نگه‌داشتن handle کتابخانه il2cpp
static void* g_il2cppHandle = nullptr;

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
// Custom method finder — با dlsym مستقیم
// ═══════════════════════════════════════════════════════
static void* CustomMethodFinder(const char* name, void* userData) {
    void* handle = userData;
    if (!handle) return nullptr;

    void* addr = dlsym(handle, name);
    if (addr) {
        LOGI("Finder: %s -> %p", name, addr);
    }
    return addr;
}

// ═══════════════════════════════════════════════════════
// BNM loaded callback
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
// Poll thread
// ═══════════════════════════════════════════════════════
static void PollAndLoad() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    LOGI("Poll: looking for libil2cpp.so");
    JB_Log("Polling libil2cpp...");

    for (int i = 0; i < 100; i++) {
        g_il2cppHandle = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (g_il2cppHandle) {
            LOGI("libil2cpp found at try %d", i);
            JB_Log("libil2cpp found at try " + std::to_string(i));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!g_il2cppHandle) {
        LOGE("libil2cpp.so never found");
        JB_Log("FAIL: libil2cpp not found");
        return;
    }

    // روش ۱: با custom finder و TryLoadByUsersFinder
    LOGI("Setting custom method finder");
    JB_Log("Setting custom finder...");

    BNM::Loading::SetMethodFinder(CustomMethodFinder, g_il2cppHandle);

    LOGI("Calling TryLoadByUsersFinder");
    JB_Log("TryLoadByUsersFinder...");

    bool ok = BNM::Loading::TryLoadByUsersFinder();

    LOGI("TryLoadByUsersFinder -> %s", ok ? "OK" : "FAIL");
    JB_Log(std::string("TryLoadByUsersFinder: ") + (ok ? "OK" : "FAIL"));

    if (!ok) {
        LOGE("UsersFinder failed — trying fallback dlfcn handle");
        JB_Log("Fallback TryLoadByDlfcnHandle...");

        bool ok2 = BNM::Loading::TryLoadByDlfcnHandle(g_il2cppHandle);
        LOGI("TryLoadByDlfcnHandle -> %s", ok2 ? "OK" : "FAIL");
        JB_Log(std::string("TryLoadByDlfcnHandle: ") + (ok2 ? "OK" : "FAIL"));
    }
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

    // ShadowHook
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("shadowhook_init -> %d", r);

    // late init
    BNM::Loading::AllowLateInitHook();
    LOGI("AllowLateInitHook called");

    // callback
    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);

    // اولین تلاش (احتمالاً fail میشه چون il2cpp نیست)
    bool ok = BNM::Loading::TryLoadByJNI(env);
    LOGI("TryLoadByJNI -> %s", ok ? "OK" : "deferred");

    // Poll thread — با custom finder
    std::thread(PollAndLoad).detach();

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
    LOGI("JNI_OnUnload");
    StopStateLoop();
}