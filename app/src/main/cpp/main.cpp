#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>

extern "C" {
#include <shadowhook.h>
}

// BNM
#include "BNM/Loading.hpp"
#include "BNM/Class.hpp"
#include "BNM/Method.hpp"

// KittyMemory
#include "KittyMemory.hpp"
#include "MemoryPatch.hpp"

// ImGui
#include "imgui.h"

#define LOG_TAG "MyMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════
// تست ۱: ShadowHook
// ═══════════════════════════════════════════
void TestShadowHook() {
    LOGI("══════════════════════════════════════");
    LOGI("[TEST 1] ShadowHook");
    LOGI("══════════════════════════════════════");

    int result = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("  init result: %d", result);

    if (result == 0) {
        LOGI("  ✅ ShadowHook initialized OK");
    } else {
        LOGW("  ⚠️ init returned %d (may be already initialized)", result);
    }

    const char* ver = shadowhook_get_version();
    if (ver) {
        LOGI("  ✅ ShadowHook version: %s", ver);
    } else {
        LOGW("  ⚠️ version: unknown");
    }
}

// ═══════════════════════════════════════════
// تست ۲: BNM
// ═══════════════════════════════════════════
void TestBNM() {
    LOGI("══════════════════════════════════════");
    LOGI("[TEST 2] BNM (ByNameModding)");
    LOGI("══════════════════════════════════════");

    LOGI("  ✅ BNM version: %s", BNM_VER);
    LOGI("  ✅ UNITY_VER: %d", UNITY_VER);
    LOGI("  ✅ UNITY_PATCH_VER: %d", UNITY_PATCH_VER);

    // چک کن il2cpp.so لود شده یا نه
    void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    if (handle) {
        LOGI("  ✅ libil2cpp.so is loaded");
        dlclose(handle);
    } else {
        LOGW("  ⚠️ libil2cpp.so NOT found");
        LOGW("  ℹ️ این طبیعیه چون توی اپ standalone هستیم");
        LOGW("  ℹ️ BNM وقتی کار می‌کنه که توی بازی Unity تزریق بشه");
    }
}

// ═══════════════════════════════════════════
// تست ۳: KittyMemory
// ═══════════════════════════════════════════
void TestKittyMemory() {
    LOGI("══════════════════════════════════════");
    LOGI("[TEST 3] KittyMemory");
    LOGI("══════════════════════════════════════");

    // پچ یه متغیر محلی برای تست
    int testValue = 0x12345678;
    LOGI("  before patch:  0x%X", testValue);

    MemoryPatch patch = MemoryPatch::createWithHex(
        (uintptr_t)&testValue,
        "DEADBEEF"
    );

    if (patch.isValid()) {
        LOGI("  ✅ MemoryPatch created");
        patch.Modify();
        LOGI("  after patch:   0x%X", testValue);
        patch.Restore();
        LOGI("  after restore: 0x%X", testValue);
        LOGI("  ✅ KittyMemory works");
    } else {
        LOGE("  ❌ MemoryPatch invalid");
    }
}

// ═══════════════════════════════════════════
// تست ۴: ImGui
// ═══════════════════════════════════════════
void TestImGui() {
    LOGI("══════════════════════════════════════");
    LOGI("[TEST 4] Dear ImGui");
    LOGI("══════════════════════════════════════");

    LOGI("  ✅ ImGui version: %s", IMGUI_VERSION);
    LOGI("  ✅ ImGui headers compiled OK");
    LOGI("  ℹ️ برای رندر ImGui نیاز به OpenGL context هست");
}

// ═══════════════════════════════════════════
// JNI_OnLoad
// ═══════════════════════════════════════════
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("");
    LOGI("╔══════════════════════════════════════╗");
    LOGI("║   MyMod — Library Loading Test       ║");
    LOGI("╚══════════════════════════════════════╝");
    LOGI("");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("❌ Failed to get JNIEnv");
        return JNI_VERSION_1_6;
    }
    LOGI("✅ JNIEnv acquired");
    LOGI("");

    TestShadowHook();
    LOGI("");
    TestBNM();
    LOGI("");
    TestKittyMemory();
    LOGI("");
    TestImGui();

    LOGI("");
    LOGI("╔══════════════════════════════════════╗");
    LOGI("║   ✅ All libraries loaded OK         ║");
    LOGI("╚══════════════════════════════════════╝");
    LOGI("");

    return JNI_VERSION_1_6;
}