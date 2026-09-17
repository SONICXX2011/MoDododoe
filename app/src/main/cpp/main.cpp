#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include "BNM/Loading.hpp"
#include "BNM/Class.hpp"

#define LOG_TAG "MyMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void OnBNMLoaded() {
    LOGI("[+] ===============================");
    LOGI("[+] BNM loaded successfully");
    LOGI("[+] MyModMenu is ready!");
    LOGI("[+] ===============================");

    // ═══════════════════════════════════════════════════
    // اینجا کد Hook خودت رو بنویس
    // مثال:
    //
    // auto klass = BNM::Class("UnityEngine.UI", "Button");
    // auto method = klass.GetMethod("OnPointerClick");
    // method.Hook([](void* instance, void* eventData) {
    //     LOGI("[+] Button clicked!");
    //     method.Call(instance, eventData);
    // });
    // ═══════════════════════════════════════════════════
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("[+] ===============================");
    LOGI("[+] JNI_OnLoad called");
    LOGI("[+] ===============================");

    // ۱. ShadowHook اول
    int sh = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("[+] ShadowHook init result: %d", sh);

    // ۲. BNM دوم
    BNM::Loading::AllowLateInitHook();
    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);
    BNM::Loading::TryLoadByJNI(vm);

    return JNI_VERSION_1_6;
}
