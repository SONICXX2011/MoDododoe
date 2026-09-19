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

// ═══════════════════════════════════════════════════════
// CLASS CACHE
// ═══════════════════════════════════════════════════════
static BNM::Class cls_GtaMenu;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;
static BNM::Class cls_Button;
static BNM::Class cls_Uri;

// ═══════════════════════════════════════════════════════
// FLAGS
// ═══════════════════════════════════════════════════════
static std::atomic<bool> g_requestConnect{false};
static std::atomic<bool> g_requestDisable{false};
static std::atomic<bool> g_hookInstalled{false};
static std::atomic<bool> g_running{false};
static std::thread       g_retryThread;

static int   g_lastMenu = -1;
static bool  g_lastNetworkActive = false;
static int   g_frameCounter = 0;
static void* orig_Update = nullptr;

// ═══════════════════════════════════════════════════════
// HELPERS
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

static std::string ReadMonoString(BNM::Structures::Mono::String* s) {
    if (!s) return "";
    try { return s->str(); } catch (...) { return ""; }
}

static std::string GetName(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return "";
    try {
        auto* go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call(obj);
        if (!go) return "";
        auto* n = BNM::Class(go)
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            .Call(go);
        return ReadMonoString(n);
    } catch (...) { return ""; }
}

// ═══════════════════════════════════════════════════════
// DISABLE — iterate روی آرایه‌های GtaMenuControl
// ═══════════════════════════════════════════════════════
static void ProcessButtonArray(BNM::IL2CPP::Il2CppObject* gta, const char* fieldName, bool forceDisableAll) {
    auto fld = cls_GtaMenu.GetField(fieldName);
    if (!fld.IsValid()) { L("[DIS] field %s missing", fieldName); return; }

    auto* arr = fld
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        [gta].Get();

    if (!arr) { L("[DIS] %s NULL", fieldName); return; }

    auto cap = arr->GetCapacity();
    L("[DIS] === %s (%zu) ===", fieldName, (size_t)cap);

    for (size_t i = 0; i < cap; i++) {
        auto* btn = *arr->At(i);
        if (!btn) { L("[DIS]   [%zu] NULL", i); continue; }

        std::string name = GetName(btn);

        // لاگ کامل
        if (name.empty()) {
            L("[DIS]   [%zu] (empty name)", i);
        } else {
            L("[DIS]   [%zu] '%s'", i, name.c_str());
        }

        // اگه forceDisableAll یا اسم target بود، disable کن
        std::string norm;
        for (char c : name) {
            if (!isspace((unsigned char)c) && c != '_' && c != '-')
                norm += toupper((unsigned char)c);
        }
        bool isTarget = (norm == "LACEDITOR" || norm == "COMMUNITY" || norm == "DOCUMENT");

        if (forceDisableAll || isTarget) {
            try {
                bool oldVal = BNM::Class(btn)
                    .GetMethod("get_interactable", 0)
                    .cast<bool>()
                    .Call(btn);

                BNM::Class(btn)
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(btn, false);

                bool newVal = BNM::Class(btn)
                    .GetMethod("get_interactable", 0)
                    .cast<bool>()
                    .Call(btn);

                L("[DIS]   [%zu] '%s' interactable: %d -> %d",
                  i, name.c_str(), (int)oldVal, (int)newVal);
            } catch (...) {
                L("[DIS]   [%zu] set_interactable ex", i);
            }
        }
    }
}

static void DisableAllButtons() {
    L("[DIS] === START ===");
    auto* gta = GetGtaMenu();
    if (!gta) { L("[DIS] GtaMenu NULL"); return; }

    // اول فقط لاگ بگیر بدون disable (forceDisableAll=false)
    ProcessButtonArray(gta, "menuButtons", false);
    ProcessButtonArray(gta, "settingsButtons", false);
    ProcessButtonArray(gta, "communityButtons", false);
    ProcessButtonArray(gta, "mapsButton", false);
    L("[DIS] === END ===");
}

// ═══════════════════════════════════════════════════════
// DIRECT CONNECT — با Uri درست
// ═══════════════════════════════════════════════════════
static bool DoDirectConnect() {
    L("[DC] ========== START ==========");

    if (!cls_NetworkManager.IsValid()) {
        L("[DC] NetworkManager invalid"); return false;
    }

    auto* mgr = cls_NetworkManager
        .GetMethod("get_singleton", 0)
        .cast<BNM::IL2CPP::Il2CppObject*>()
        .Call();

    if (!mgr) { L("[DC] singleton NULL"); return false; }
    L("[DC] singleton=%p", (void*)mgr);

    // ─── transport check ───
    try {
        auto trField = cls_NetworkManager.GetField("transport");
        if (trField.IsValid()) {
            auto* tr = trField.cast<BNM::IL2CPP::Il2CppObject*>()
                [mgr].Get();
            L("[DC] transport=%p", (void*)tr);
            if (tr) L("[DC] transport class=%s", BNM::Class(tr).str().c_str());
        }
    } catch (...) {}

    // ─── Uri class ───
    if (!cls_Uri.IsValid()) { L("[DC] Uri invalid"); return false; }

    char uriStr[64];
    snprintf(uriStr, sizeof(uriStr), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    // ─── لیست ctorهای Uri ───
    L("[DC] Uri ctors:");
    try {
        auto methods = cls_Uri.GetMethods();
        for (auto& mm : methods) {
            auto* info = mm.GetInfo();
            if (!info || !info->name) continue;
            if (std::string(info->name) != ".ctor") continue;
            L("[DC]   .ctor params=%d ptr=%p",
              (int)info->parameters_count, (void*)info->methodPointer);
        }
    } catch (...) {}

    // ─── ساخت Uri با type matching ───
    L("[DC] creating Uri with ctor(String): %s", uriStr);

    BNM::CompileTimeClass strType =
        BNM::CompileTimeClassBuilder("System", "String").Build();

    auto ctor = cls_Uri.GetMethod(".ctor", {strType});

    if (!ctor.IsValid()) {
        L("[DC] ctor(String) NOT found");
        return false;
    }
    L("[DC] ctor(String) found, ptr=%p", (void*)ctor.GetInfo()->methodPointer);

    auto* uriObj = cls_Uri.CreateNewInstance();
    if (!uriObj) { L("[DC] CreateNewInstance failed"); return false; }
    L("[DC] uriObj=%p", (void*)uriObj);

    L("[DC] calling ctor...");
    ctor.cast<void>().Call(uriObj, BNM::CreateMonoString(uriStr));
    L("[DC] ctor returned");

    // ─── چک content ───
    try {
        auto* abs = BNM::Class(uriObj)
            .GetMethod("get_AbsoluteUri", 0)
            .cast<BNM::Structures::Mono::String*>()
            .Call(uriObj);
        std::string absStr = ReadMonoString(abs);
        L("[DC] uri.AbsoluteUri='%s'", absStr.c_str());
    } catch (...) {
        L("[DC] get_AbsoluteUri FAILED");
    }

    try {
        auto* ts = BNM::Class(uriObj)
            .GetMethod("ToString", 0)
            .cast<BNM::Structures::Mono::String*>()
            .Call(uriObj);
        L("[DC] uri.ToString()='%s'", ReadMonoString(ts).c_str());
    } catch (...) {
        L("[DC] ToString FAILED");
    }

    // ─── resolve StartClient(Uri) ───
    BNM::CompileTimeClass uriType =
        BNM::CompileTimeClassBuilder("System", "Uri").Build();
    auto m = cls_NetworkManager.GetMethod("StartClient", {uriType});

    if (!m.IsValid()) {
        L("[DC] fallback count=1");
        m = cls_NetworkManager.GetMethod("StartClient", 1);
    }

    if (!m.IsValid()) { L("[DC] StartClient NOT FOUND"); return false; }

    L("[DC] resolved: %s", m.str().c_str());

    // ─── CALL ───
    L("[DC] >>> CALLING StartClient");
    m.cast<void>().Call(mgr, uriObj);
    L("[DC] <<< RETURNED");

    // ─── wait & check ───
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    bool netActive = ReadNetworkActive();
    L("[DC] after 500ms: netActive=%d", (int)netActive);

    try {
        bool conn = cls_NetworkClient.GetMethod("get_isConnected", 0)
            .cast<bool>().Call();
        L("[DC] isConnected=%d", (int)conn);
    } catch (...) {}

    try {
        bool act = cls_NetworkClient.GetMethod("get_active", 0)
            .cast<bool>().Call();
        L("[DC] active=%d", (int)act);
    } catch (...) {}

    L("[DC] ========== END ==========");
    return true;
}

// ═══════════════════════════════════════════════════════
// UPDATE HOOK
// ═══════════════════════════════════════════════════════
static void Hook_GtaMenuUpdate(void* self, void* methodInfo) {
    if (orig_Update) {
        ((void(*)(void*,void*))orig_Update)(self, methodInfo);
    }

    auto* selfObj = (BNM::IL2CPP::Il2CppObject*)self;

    if (g_requestConnect.exchange(false)) {
        L("[Update] connect requested");
        DoDirectConnect();
    }

    if (g_requestDisable.exchange(false)) {
        L("[Update] disable requested");
        DisableAllButtons();
    }

    g_frameCounter++;
    if (g_frameCounter >= 120) {
        g_frameCounter = 0;
        int menu = ReadCurrentMenu(selfObj);
        if (menu == 0) DisableAllButtons();
    }

    int menu = ReadCurrentMenu(selfObj);
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
// OnClientError HOOK
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
    if (!m.IsValid()) { L("[Hook] not found"); return; }
    auto* info = m.GetInfo();
    L("[Hook] OnClientError ptr=%p vptr=%p",
      (void*)info->methodPointer, (void*)info->virtualMethodPointer);
    bool ok = BNM::InvokeHook(m, (void*)Hook_OnClientError, orig_OnClientError);
    L("[Hook] InvokeHook=%d orig=%p", (int)ok, orig_OnClientError);
}

// ═══════════════════════════════════════════════════════
// INSTALL Update
// ═══════════════════════════════════════════════════════
static void InstallUpdateHook() {
    if (g_hookInstalled.load()) return;
    if (!cls_GtaMenu.IsValid()) { L("[Update] GtaMenu invalid"); return; }

    auto m = cls_GtaMenu.GetMethod("Update", 0);
    if (!m.IsValid()) m = cls_GtaMenu.GetMethod("LateUpdate", 0);
    if (!m.IsValid()) { L("[Update] no Update!"); return; }

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
        L("!!! Uri NOT found");
    }
}

// ═══════════════════════════════════════════════════════
// CLASSES
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
// JNI
// ═══════════════════════════════════════════════════════
void TriggerStartGame() {
    L("[JNI] queue connect");
    g_requestConnect.store(true);
}

void DisableModButtons() {
    L("[JNI] queue disable");
    g_requestDisable.store(true);
}

void DumpStartClientInfo() { L("[JNI] dump"); }

// ═══════════════════════════════════════════════════════
// START
// ═══════════════════════════════════════════════════════
void StartStateLoop() {
    if (g_running.exchange(true)) return;
    g_retryThread = std::thread([]() {
        InstallGameHooks();
        int attempt = 0;
        while (g_running.load() && !g_hookInstalled.load() && attempt < 60) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            attempt++;
            L("[Retry] attempt %d", attempt);
            ResolveClasses();
            InstallUpdateHook();
        }
        if (g_hookInstalled.load()) L("[Retry] Update hook OK");
        else L("[Retry] FAILED");
    });
}

void StopStateLoop() {
    g_running.store(false);
    if (g_retryThread.joinable()) g_retryThread.join();
}