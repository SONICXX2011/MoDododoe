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

static void PollLibil2cpp() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    LOGI("Poll thread: waiting for libil2cpp.so");
    JB_Log("Polling for libil2cpp.so...");

    for (int i = 0; i < 600; i++) {
        void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
        if (handle) {
            LOGI("libil2cpp.so found after %d tries (%d ms)", i, i * 100);
            JB_Log("libil2cpp found at try " + std::to_string(i));

            bool ok = BNM::Loading::TryLoadByDlfcnHandle(handle);
            LOGI("TryLoadByDlfcnHandle -> %s", ok ? "OK" : "FAIL");
            JB_Log(std::string("TryLoadByDlfcnHandle: ") + (ok ? "OK" : "FAIL"));
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOGE("libil2cpp.so never appeared");
    JB_Log("FAIL: libil2cpp never appeared");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    LOGI("JNI_OnLoad");
    g_vm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("no JNIEnv");
        return JNI_VERSION_1_6;
    }

    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("shadowhook_init -> %d", r);

    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);

    std::thread(PollLibil2cpp).detach();

    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
    LOGI("JNI_OnUnload");
    StopStateLoop();
}