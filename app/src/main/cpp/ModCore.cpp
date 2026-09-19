#include "ModCore.h"
#include "JavaBridge.h"
#include "Globals.h"

#include "BNM/Class.hpp"
#include "BNM/Image.hpp"
#include "BNM/Method.hpp"
#include "BNM/Field.hpp"
#include "BNM/Utils.hpp"
#include "BNM/BasicMonoStructures.hpp"

#include <android/log.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdarg>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void L(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOGI("%s", buf);
    JB_Log(buf);
}

static BNM::Class cls_GtaMenu;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;
static BNM::Class cls_Button;
static BNM::Class cls_Uri;

static std::atomic<bool> g_running{false};
static std::thread       g_loopThread;

// ═══════════════════════════════════════════════════════
// GTA MENU
// ═══════════════════════════════════════════════════════
static BNM::IL2CPP::Il2CppObject* GetGtaMenu() {
    if (!cls_GtaMenu.IsValid()) return nullptr;
    try {
        return cls_GtaMenu.GetMethod("get_Instance", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();
    } catch (...) { return nullptr; }
}

static int GetCurrentMenu() {
    auto* ctrl = GetGtaMenu();
    if (!ctrl) return -1;
    try {
        BNM::Field<int> f = cls_GtaMenu.GetField("currentMenu").cast<int>();
        f[ctrl];
        return f.Get();
    } catch (...) { return -1; }
}

static bool GetNetworkActive() {
    if (!cls_NetworkClient.IsValid()) return false;
    try {
        if (cls_NetworkClient.GetMethod("get_isConnected", 0)
                .cast<bool>().Call()) return true;
    } catch (...) {}
    try {
        if (cls_NetworkClient.GetMethod("get_active", 0)
                .cast<bool>().Call()) return true;
    } catch (...) {}
    return false;
}

// ═══════════════════════════════════════════════════════
// DIRECT CONNECT (معادل directConnect در TS)
// ═══════════════════════════════════════════════════════
static bool DirectConnect() {
    L("[DC] start");

    // ── 1) singleton از NetworkManager (پدر، نه فرزند) ──
    if (!cls_NetworkManager.IsValid()) {
        L("[DC] cls_NetworkManager invalid");
        return false;
    }

    auto* mgr = cls_NetworkManager
        .GetMethod("get_singleton", 0)
        .cast<BNM::IL2CPP::Il2CppObject*>()
        .Call();

    if (!mgr) {
        L("[DC] singleton NULL");
        return false;
    }
    L("[DC] singleton=%p", (void*)mgr);

    // ── 2) Uri class از همه assemblyها ──
    if (!cls_Uri.IsValid()) {
        L("[DC] Uri class not found in ANY assembly");
        return false;
    }

    // ── 3) ساخت Uri با ctor(String) ──
    char uriStr[64];
    snprintf(uriStr, sizeof(uriStr), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    auto* uriObj = cls_Uri.CreateNewObjectParameters(
        BNM::CreateMonoString(uriStr));
    if (!uriObj) {
        L("[DC] Uri alloc failed");
        return false;
    }
    L("[DC] uri=%s obj=%p", uriStr, (void*)uriObj);

    // ── 4) StartClient(Uri) با type-matching ──
    BNM::CompileTimeClass uriType =
        BNM::CompileTimeClassBuilder("System", "Uri").Build();

    auto m = cls_NetworkManager.GetMethod("StartClient", {uriType});

    if (!m.IsValid()) {
        L("[DC] StartClient(Uri) NOT FOUND by type — fallback by name");
        auto methods = cls_NetworkManager.GetMethods();
        for (auto& mm : methods) {
            try {
                auto* info = mm.GetInfo();
                if (!info || !info->name) continue;
                if (std::string(info->name) != "StartClient") continue;
                if (info->parameters_count != 1) continue;

                L("[DC] candidate StartClient arg-count=%d ptr=%p",
                  (int)info->parameters_count, (void*)info->methodPointer);
                m = mm;
                break;
            } catch (...) {}
        }
    }

    if (!m.IsValid()) {
        L("[DC] StartClient NEVER FOUND");
        return false;
    }

    L("[DC] StartClient sig: %s", m.str().c_str());

    m.cast<void>().Call(mgr, uriObj);
    L("[DC] StartClient(Uri) CALLED OK");
    return true;
}

void TriggerStartGame() {
    std::thread([]() {
        L("--- StartGame ---");

        for (int i = 0; i < 100 && !GetGtaMenu(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!GetGtaMenu()) {
            L("[SG] FAIL: GtaMenu not ready");
            return;
        }

        JB_ShowJoinNotification();
        bool ok = DirectConnect();
        L("[SG] connect=%d", (int)ok);
    }).detach();
}

// ═══════════════════════════════════════════════════════
// HOOK: CustomNetworkManager.OnClientError
//
// مهم: از BNM::InvokeHook استفاده می‌کنیم نه shadowhook!
// دلیل: shadowhook 2.0.1 روی Android 16 با err=12 (INIT_LINKER)
// فیل می‌شه. InvokeHook مستقیم MethodInfo->methodPointer رو
// عوض می‌کنه، بدون وابستگی به shadowhook.
// ═══════════════════════════════════════════════════════
static void* orig_OnClientError = nullptr;

static void Hook_OnClientError(void* self, int transportError, void* message) {
    std::string msg;
    if (message) {
        try {
            msg = ((BNM::Structures::Mono::String*)message)->str();
        } catch (...) {}
    }

    L("*** OnClientError err=%d msg=%s", transportError, msg.c_str());

    if (orig_OnClientError) {
        ((void(*)(void*,int,void*))orig_OnClientError)(self, transportError, message);
    }
}

static void InstallOnClientErrorHook() {
    if (!cls_CustomNetworkManager.IsValid()) {
        L("[Hook] CNM invalid");
        return;
    }

    auto m = cls_CustomNetworkManager.GetMethod("OnClientError", 2);
    if (!m.IsValid()) {
        L("[Hook] OnClientError(2) not found");
        return;
    }

    auto* info = m.GetInfo();
    L("[Hook] methodPointer=%p virtualMethodPointer=%p",
      (void*)info->methodPointer, (void*)info->virtualMethodPointer);

    bool ok = BNM::InvokeHook(m, (void*)Hook_OnClientError, orig_OnClientError);
    L("[Hook] InvokeHook=%d orig=%p", (int)ok, orig_OnClientError);

    if (ok) {
        L("[Hook] OnClientError installed OK");
    } else {
        L("[Hook] FAILED");
    }
}

// ═══════════════════════════════════════════════════════
// DISABLE BUTTONS
// معادل Il2Cpp.gc.choose(Button) در TS
//
// از آرایه‌های GtaMenuControl استفاده می‌کنیم چون معادل
// gc.choose در BNM وجود نداره.
// ═══════════════════════════════════════════════════════

static bool IsTargetName(const std::string& name) {
    if (name.empty()) return false;
    std::string n;
    for (char c : name) {
        if (!isspace((unsigned char)c) && c != '_' && c != '-')
            n += toupper((unsigned char)c);
    }
    return n == "LACEDITOR" || n == "COMMUNITY" || n == "DOCUMENT";
}

static void ProcessButtonArray(BNM::IL2CPP::Il2CppObject* gta, const char* fieldName) {
    auto fld = cls_GtaMenu.GetField(fieldName);
    if (!fld.IsValid()) {
        L("[DIS] field %s not found", fieldName);
        return;
    }

    auto* arr = fld
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        [gta].Get();

    if (!arr) {
        L("[DIS] %s array NULL", fieldName);
        return;
    }

    auto cap = arr->GetCapacity();
    L("[DIS] %s: %zu buttons", fieldName, (size_t)cap);

    for (size_t i = 0; i < cap; i++) {
        auto* btn = *arr->At(i);
        if (!btn) continue;

        std::string name = "";
        try {
            auto* go = BNM::Class(btn)
                .GetMethod("get_gameObject", 0)
                .cast<BNM::IL2CPP::Il2CppObject*>()
                .Call(btn);

            if (go) {
                auto* n = BNM::Class(go)
                    .GetMethod("get_name", 0)
                    .cast<BNM::Structures::Mono::String*>()
                    .Call(go);
                if (n) name = n->str();
            }
        } catch (...) {}

        L("[DIS]   [%zu] '%s'", i, name.c_str());

        if (IsTargetName(name)) {
            try {
                BNM::Class(btn)
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(btn, false);
                L("[DIS]   -> DISABLED '%s'", name.c_str());
            } catch (...) {
                L("[DIS]   -> set_interactable failed");
            }
        }
    }
}

void DisableModButtons() {
    auto* gta = GetGtaMenu();
    if (!gta) {
        L("[DIS] GtaMenu NULL");
        return;
    }

    ProcessButtonArray(gta, "menuButtons");
    ProcessButtonArray(gta, "settingsButtons");
    ProcessButtonArray(gta, "communityButtons");
    ProcessButtonArray(gta, "mapsButton");
}

// ═══════════════════════════════════════════════════════
// STATE LOOP
// ═══════════════════════════════════════════════════════
static void StateLoop() {
    L("StateLoop started");

    using clock = std::chrono::steady_clock;
    auto last        = clock::now();
    auto lastDisable = clock::now();

    while (g_running) {
        auto now = clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last).count() < 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        last = now;

        int  menu    = GetCurrentMenu();
        bool network = GetNetworkActive();

        if (menu >= 0 && menu != g_state.lastMenu) {
            L("Menu -> %d", menu);
            g_state.lastMenu = menu;
        }
        if (network != g_state.lastNetworkActive) {
            g_state.lastNetworkActive = network;
            L("Net -> %s", network ? "ON" : "OFF");
        }

        JB_SetGameState(menu, network);

        // disable هر 2 ثانیه یه بار
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastDisable).count() >= 2000) {
            lastDisable = now;
            if (menu == 0) {  // فقط توی Main Menu
                DisableModButtons();
            }
        }
    }
}

// ═══════════════════════════════════════════════════════
// CLASS CACHE
// ═══════════════════════════════════════════════════════
static void ResolveUriClass() {
    // استراتژی Frida: همه‌ی assemblyها رو بگرد
    cls_Uri = BNM::Class("System", "Uri");

    if (cls_Uri.IsValid()) {
        std::string_view imgName = cls_Uri.GetImage().str();
        L("Uri found in: %.*s", (int)imgName.size(), imgName.data());
        return;
    }

    L("!!! Uri NOT found in ANY assembly");
}

void InstallGameHooks() {
    L("=== InstallGameHooks ===");

    auto imgAsm    = BNM::Image("Assembly-CSharp.dll");
    if (!imgAsm.IsValid()) imgAsm = BNM::Image("Assembly-CSharp");

    auto imgMirror = BNM::Image("Mirror.dll");
    if (!imgMirror.IsValid()) imgMirror = BNM::Image("Mirror");

    auto imgUI     = BNM::Image("UnityEngine.UI.dll");
    if (!imgUI.IsValid()) imgUI = BNM::Image("UnityEngine.UI");

    cls_GtaMenu              = BNM::Class("", "GtaMenuControl", imgAsm);
    cls_NetworkManager       = BNM::Class("Mirror", "NetworkManager", imgMirror);
    cls_NetworkClient        = BNM::Class("Mirror", "NetworkClient", imgMirror);
    cls_CustomNetworkManager = BNM::Class("", "CustomNetworkManager", imgAsm);
    cls_Button               = BNM::Class("UnityEngine.UI", "Button", imgUI);

    L("Gta=%d NM=%d NC=%d CNM=%d Btn=%d",
      (int)cls_GtaMenu.IsValid(),
      (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(),
      (int)cls_CustomNetworkManager.IsValid(),
      (int)cls_Button.IsValid());

    ResolveUriClass();
    InstallOnClientErrorHook();

    L("=== hooks done ===");
}

void StartStateLoop() {
    if (g_running.exchange(true)) return;
    g_loopThread = std::thread(StateLoop);
    L("StateLoop thread launched");
}

void StopStateLoop() {
    if (!g_running.exchange(false)) return;
    if (g_loopThread.joinable()) g_loopThread.join();
    L("StateLoop stopped");
}

void DumpStartClientInfo() { L("(dump disabled)"); }