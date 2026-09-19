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
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdarg>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

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

// ─── Flags set from JNI thread, consumed by Update hook (main thread) ───
static std::atomic<bool> g_requestConnect{false};
static std::atomic<bool> g_requestDisable{false};
static std::atomic<bool> g_hookInstalled{false};
static std::atomic<bool> g_running{false};
static std::thread       g_retryThread;

// ─── State tracking (main thread only) ───
static int  g_lastMenu = -1;
static bool g_lastNetworkActive = false;
static int  g_frameCounter = 0;
static void* orig_Update = nullptr;

// ═══════════════════════════════════════════════════════
// MAIN-THREAD HELPERS
// ═══════════════════════════════════════════════════════
static BNM::IL2CPP::Il2CppObject* GetGtaMenu() {
    if (!cls_GtaMenu.IsValid()) return nullptr;
    try {
        return cls_GtaMenu.GetMethod("get_Instance", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();
    } catch (...) { return nullptr; }
}

static int ReadCurrentMenu(BNM::IL2CPP::Il2CppObject* ctrl) {
    if (!ctrl) return -1;
    try {
        BNM::Field<int> f = cls_GtaMenu.GetField("currentMenu").cast<int>();
        f[ctrl];
        return f.Get();
    } catch (...) { return -1; }
}

static bool ReadNetworkActive() {
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
// DISABLE BUTTONS
// ═══════════════════════════════════════════════════════
static bool IsTargetName(const std::string& name) {
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
        L("[DIS] field %s missing", fieldName);
        return;
    }

    auto* arr = fld
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        [gta].Get();

    if (!arr) { L("[DIS] %s array NULL", fieldName); return; }

    auto cap = arr->GetCapacity();
    int found = 0, disabled = 0;

    for (size_t i = 0; i < cap; i++) {
        auto* btn = *arr->At(i);
        if (!btn) continue;
        found++;

        std::string name;
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

        if (IsTargetName(name)) {
            try {
                BNM::Class(btn)
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(btn, false);
                disabled++;
                L("[DIS] disabled: %s", name.c_str());
            } catch (...) {}
        }
    }
    L("[DIS] %s: %d buttons, %d disabled", fieldName, found, disabled);
}

static void DoDisableButtons() {
    auto* gta = GetGtaMenu();
    if (!gta) { L("[DIS] GtaMenu NULL"); return; }

    ProcessButtonArray(gta, "menuButtons");
    ProcessButtonArray(gta, "settingsButtons");
    ProcessButtonArray(gta, "communityButtons");
    ProcessButtonArray(gta, "mapsButton");
}

// ═══════════════════════════════════════════════════════
// DIRECT CONNECT
// ═══════════════════════════════════════════════════════
static bool DoDirectConnect() {
    L("[DC] start");

    if (!cls_NetworkManager.IsValid()) {
        L("[DC] NetworkManager invalid"); return false;
    }

    auto* mgr = cls_NetworkManager
        .GetMethod("get_singleton", 0)
        .cast<BNM::IL2CPP::Il2CppObject*>()
        .Call();

    if (!mgr) { L("[DC] singleton NULL"); return false; }
    L("[DC] singleton=%p", (void*)mgr);

    if (!cls_Uri.IsValid()) { L("[DC] Uri invalid"); return false; }

    char uriStr[64];
    snprintf(uriStr, sizeof(uriStr), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    auto* uriObj = cls_Uri.CreateNewObjectParameters(BNM::CreateMonoString(uriStr));
    if (!uriObj) { L("[DC] Uri alloc failed"); return false; }
    L("[DC] uri=%s", uriStr);

    BNM::CompileTimeClass uriType =
        BNM::CompileTimeClassBuilder("System", "Uri").Build();
    auto m = cls_NetworkManager.GetMethod("StartClient", {uriType});

    if (!m.IsValid()) {
        L("[DC] fallback by name+count");
        auto methods = cls_NetworkManager.GetMethods();
        for (auto& mm : methods) {
            auto* info = mm.GetInfo();
            if (!info || !info->name) continue;
            if (std::string(info->name) != "StartClient") continue;
            if (info->parameters_count != 1) continue;
            m = mm; break;
        }
    }

    if (!m.IsValid()) { L("[DC] StartClient NOT FOUND"); return false; }

    L("[DC] calling StartClient(Uri)");
    m.cast<void>().Call(mgr, uriObj);
    L("[DC] StartClient CALLED");
    return true;
}

// ═══════════════════════════════════════════════════════
// HOOK: GtaMenuControl.Update()  ← main thread every frame
// ═══════════════════════════════════════════════════════
static void Hook_GtaMenuUpdate(void* self, void* methodInfo) {
    if (orig_Update) {
        ((void(*)(void*,void*))orig_Update)(self, methodInfo);
    }

    // ─── Connect request from JNI ───
    if (g_requestConnect.exchange(false)) {
        L("[Update] connect requested");
        DoDirectConnect();
    }

    // ─── Disable request from JNI ───
    if (g_requestDisable.exchange(false)) {
        L("[Update] disable requested");
        DoDisableButtons();
    }

    // ─── Auto-disable every ~2s in Main menu ───
    g_frameCounter++;
    if (g_frameCounter >= 120) {
        g_frameCounter = 0;
        int menu = ReadCurrentMenu(self);
        if (menu == 0) DoDisableButtons();
    }

    // ─── State logging ───
    int menu = ReadCurrentMenu(self);
    bool network = ReadNetworkActive();

    if (menu != g_lastMenu) {
        L("[Update] Menu -> %d", menu);
        g_lastMenu = menu;
    }
    if (network != g_lastNetworkActive) {
        L("[Update] Net -> %s", network ? "ON" : "OFF");
        g_lastNetworkActive = network;
    }

    JB_SetGameState(menu, network);
}

// ═══════════════════════════════════════════════════════
// HOOK: OnClientError (InvokeHook — no shadowhook)
// ═══════════════════════════════════════════════════════
static void* orig_OnClientError = nullptr;

static void Hook_OnClientError(void* self, int err, void* message) {
    std::string msg;
    if (message) {
        try { msg = ((BNM::Structures::Mono::String*)message)->str(); }
        catch (...) {}
    }
    L("*** OnClientError err=%d msg=%s", err, msg.c_str());

    if (orig_OnClientError) {
        ((void(*)(void*,int,void*))orig_OnClientError)(self, err, message);
    }
}

static void InstallOnClientErrorHook() {
    if (!cls_CustomNetworkManager.IsValid()) { L("[Hook] CNM invalid"); return; }

    auto m = cls_CustomNetworkManager.GetMethod("OnClientError", 2);
    if (!m.IsValid()) { L("[Hook] OnClientError(2) not found"); return; }

    auto* info = m.GetInfo();
    L("[Hook] ptr=%p vptr=%p",
      (void*)info->methodPointer, (void*)info->virtualMethodPointer);

    bool ok = BNM::InvokeHook(m, (void*)Hook_OnClientError, orig_OnClientError);
    L("[Hook] InvokeHook=%d orig=%p", (int)ok, orig_OnClientError);
}

// ═══════════════════════════════════════════════════════
// HOOK: Update install
// ═══════════════════════════════════════════════════════
static void InstallUpdateHook() {
    if (g_hookInstalled.load()) return;
    if (!cls_GtaMenu.IsValid()) { L("[Update] GtaMenu invalid"); return; }

    auto m = cls_GtaMenu.GetMethod("Update", 0);
    if (!m.IsValid()) {
        L("[Update] Update not found, trying LateUpdate");
        m = cls_GtaMenu.GetMethod("LateUpdate", 0);
    }
    if (!m.IsValid()) { L("[Update] no Update/LateUpdate!"); return; }

    bool ok = BNM::InvokeHook(m, (void*)Hook_GtaMenuUpdate, orig_Update);
    L("[Update] InvokeHook=%d orig=%p", (int)ok, orig_Update);
    if (ok) g_hookInstalled.store(true);
}

// ═══════════════════════════════════════════════════════
// URI
// ═══════════════════════════════════════════════════════
static void ResolveUriClass() {
    cls_Uri = BNM::Class("System", "Uri");
    if (cls_Uri.IsValid()) {
        std::string_view s = cls_Uri.GetImage().str();
        L("Uri in: %.*s", (int)s.size(), s.data());
    } else {
        L("!!! Uri NOT found in ANY assembly");
    }
}

// ═══════════════════════════════════════════════════════
// INSTALL CLASSES
// ═══════════════════════════════════════════════════════
static void ResolveClasses() {
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
}

void InstallGameHooks() {
    L("=== InstallGameHooks ===");
    ResolveClasses();

    L("Gta=%d NM=%d NC=%d CNM=%d Btn=%d",
      (int)cls_GtaMenu.IsValid(),
      (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(),
      (int)cls_CustomNetworkManager.IsValid(),
      (int)cls_Button.IsValid());

    ResolveUriClass();
    InstallOnClientErrorHook();
    InstallUpdateHook();

    L("=== done ===");
}

// ═══════════════════════════════════════════════════════
// JNI-thread entry points (only set flags)
// ═══════════════════════════════════════════════════════
void TriggerStartGame() {
    L("[JNI] queue connect request");
    g_requestConnect.store(true);
}

void DisableModButtons() {
    L("[JNI] queue disable request");
    g_requestDisable.store(true);
}

void DumpStartClientInfo() {
    L("[JNI] dump requested");
}

// ═══════════════════════════════════════════════════════
// START (with retry thread for Update hook)
// ═══════════════════════════════════════════════════════
void StartStateLoop() {
    if (g_running.exchange(true)) return;

    g_retryThread = std::thread([]() {
        // Initial attempt
        InstallGameHooks();

        // Retry if Update hook failed
        int attempt = 0;
        while (g_running.load() && !g_hookInstalled.load() && attempt < 60) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            attempt++;
            L("[Retry] Update hook attempt %d", attempt);
            ResolveClasses();
            InstallUpdateHook();
        }

        if (g_hookInstalled.load()) {
            L("[Retry] Update hook installed OK");
        } else {
            L("[Retry] FAILED to install Update hook");
        }
    });
}

void StopStateLoop() {
    g_running.store(false);
    if (g_retryThread.joinable()) g_retryThread.join();
    L("StateLoop stopped");
}