#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

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

void* WaitForIl2Cpp(void* arg) {
    JNIEnv* env = (JNIEnv*)arg;

    // صبر کن تا il2cpp.so لود بشه (حداکثر ۶۰ ثانیه)
    for (int i = 0; i < 60; i++) {
        void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
        if (handle) {
            LOGI("[+] il2cpp.so found after %d seconds", i);
            dlclose(handle);
            break;
        }
        sleep(1);
    }

    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);
    BNM::Loading::TryLoadByJNI(env);
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("[+] JNI_OnLoad");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_VERSION_1_6;
    }

    int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("[+] ShadowHook init: %d", result);

    pthread_t thread;
    pthread_create(&thread, nullptr, WaitForIl2Cpp, env);
    pthread_detach(thread);

    return JNI_VERSION_1_6;
}