#include <jni.h>
#include <android/log.h>

extern "C" {
#include <shadowhook.h>
}

#include "BNM/Loading.hpp"

#define LOG_TAG "MyMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void OnBNMLoaded() {
    LOGI("[+] BNM loaded successfully");
    LOGI("[+] Mod is ready!");
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("[+] JNI_OnLoad");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGI("[+] Failed to get JNIEnv");
        return JNI_VERSION_1_6;
    }

    int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("[+] ShadowHook init: %d", result);

    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);
    BNM::Loading::TryLoadByJNI(env);

    return JNI_VERSION_1_6;
}