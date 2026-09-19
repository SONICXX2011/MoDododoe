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
// HELPERS — همه با [instance] درست شدن
// ═══════════════════════════════════════════════════════
static BNM::IL2CPP::Il2CppObject* GetGtaMenu() {
    if (!cls_GtaMenu.IsValid()) return nullptr;
    try {
        // static method — Call() بدون instance درسته
        return cls_GtaMenu.GetMethod("get_Instance", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();
    } catch (...) { return nullptr; }
}

static int ReadCurrentMenu(BNM::IL2CPP::Il2CppObject* ctrl) {
    if (!ctrl) return -1;
    try {
        auto f = cls_GtaMenu.GetField("currentMenu").cast<int>();
        f[ctrl];
        return f.Get();
    } catch (...) { return -1; }
}

static bool ReadNetworkActive() {
    if (!cls_NetworkClient.IsValid()) return false;
    try {
        // isConnected — static property در Mirror
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

// ─── خواندن اسم GameObject از روی یه Component ───
// استفاده از [instance] که مشکل اصلی بود
static std::string GetName(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return "";
    try {
        // obj.get_gameObject()
        auto* go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [obj]();   // ← [instance]() = instance method با 0 arg
        if (!go) return "";

        // go.get_name()
        auto* n = BNM::Class(go)
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            [go]();   // ← همین
        return ReadMonoString(n);
    } catch (...) { return ""; }
}

// ═══════════════════════════════════════════════════════
// DISABLE
// ═══════════════════════════════════════════════════════
static void ProcessButtonArray(BNM::IL2CPP::Il2CppObject* gta, const char* fieldName, bool forceDisableAll) {
    auto fld = cls_GtaMenu.GetField(fieldName);
    if (!fld.IsValid()) { L("[DIS] %s missing", fieldName); return; }

    auto* arr = fld
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        [gta].Get();

    if (!arr) { L("[DIS] %s NULL", fieldName); return; }

    auto cap = arr->GetCapacity();
    L("[DIS] === %s (%zu) ===", fieldName, (size_t)cap);

    for (size_t i = 0; i < cap; i++) {
        auto* btn = *arr->At(i);
        if (!btn) { L("[DIS] [%zu] NULL", i); continue; }

        std::string name = GetName(btn);
        L("[DIS] [%zu] '%s'", i, name.c_str());

        std::string norm;
        for (char c : name) {
            if (!isspace((unsigned char)c) && c != '_' && c != '-')
                norm += toupper((unsigned char)c);
        }
        bool isTarget = (norm == "LACEDITOR" || norm == "COMMUNITY" || norm == "DOCUMENT");

        if (forceDisableAll || isTarget) {
            try {
                // get_interactable
                bool oldVal = BNM::Class(btn)
                    .GetMethod("get_interactable", 0)
                    .cast<bool>()
                    [btn]();

                // set_interactable(false)  ← FIX اصلی
                BNM::Class(btn)
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    [btn](false);

                // get_interactable دوباره
                bool newVal = BNM::Class(btn)
                    .GetMethod("get_interactable", 0)
                    .cast<bool>()
                    [btn]();

                L("[DIS] [%zu] '%s' interactable %d -> %d",
                  i, name.c_str(), (int)oldVal, (int)newVal);
            } catch (...) {
                L("[DIS] [%zu] set_interactable ex", i);
            }
        }
    }
}

static void DisableAllButtons() {
    L("[DIS] === START ===");
    auto* gta = GetGtaMenu();
    if (!gta) { L("[DIS] GtaMenu NULL"); return; }

    ProcessButtonArray(gta, "menuButtons", false);
    ProcessButtonArray(gta, "settingsButtons", false);
    ProcessButtonArray(gta, "communityButtons", false);
    ProcessButtonArray(gta, "mapsButton", false);
    L("[DIS] === END ===");
}

// ═══════════════════════════════════════════════════════
// DIRECT CONNECT
// ═══════════════════════════════════════════════════════
static bool DoDirectConnect() {
    L("[DC] ========== START ==========");

    if (!cls_NetworkManager.IsValid()) { L("[DC] NM invalid"); return false; }

    auto* mgr = cls_NetworkManager
        .GetMethod("get_singleton", 0)
        .cast<BNM::IL2CPP::Il2CppObject*>()
        .Call();

    if (!mgr) { L("[DC] singleton NULL"); return false; }
    L("[DC] singleton=%p", (void*)mgr);

    // ─── transport ───
    try {
        auto trField = cls_NetworkManager.GetField("transport");
        if (trField.IsValid()) {
            auto* tr = trField.cast<BNM::IL2CPP::Il2CppObject*>()
                [mgr].Get();
            L("[DC] transport=%p", (void*)tr);
            if (tr) L("[DC] transport=%s", BNM::Class(tr).str().c_str());
        }
    } catch (...) {}

    if (!cls_Uri.IsValid()) { L("[DC] Uri invalid"); return false; }

    char uriStr[64];
    snprintf(uriStr, sizeof(uriStr), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    // ─── ساخت Uri ───
    L("[DC] creating Uri: %s", uriStr);

    auto* uriObj = cls_Uri.CreateNewInstance();
    if (!uriObj) { L("[DC] alloc failed"); return false; }
    L("[DC] uriObj=%p", (void*)uriObj);

    // ctor(String) با type matching
    BNM::CompileTimeClass strType =
        BNM::CompileTimeClassBuilder("System", "String").Build();
    auto ctor = cls_Uri.GetMethod(".ctor", {strType});

    if (!ctor.IsValid()) {
        L("[DC] ctor(String) NOT found");
        return false;
    }
    L("[DC] ctor ptr=%p", (void*)ctor.GetInfo()->methodPointer);

    // ─── Call ctor با [instance] ───
    L("[DC] calling ctor...");
    ctor[uriObj].cast<void>()(BNM::CreateMonoString(uriStr));
    L("[DC] ctor returned");

    // ─── چک Uri ───
    try {
        auto* abs = BNM::Class(uriObj)
            .GetMethod("get_AbsoluteUri", 0)
            .cast<BNM::Structures::Mono::String*>()
            [uriObj]();
        L("[DC] uri.AbsoluteUri='%s'", ReadMonoString(abs).c_str());
    } catch (...) { L("[DC] get_AbsoluteUri FAILED"); }

    try {
        auto* ts = BNM::Class(uriObj)
            .GetMethod("ToString", 0)
            .cast<BNM::Structures::Mono::String*>()
            [uriObj]();
        L("[DC] uri.ToString()='%s'", ReadMonoString(ts).c_str());
    } catch (...) { L("[DC] ToString FAILED"); }

    // ─── StartClient(Uri) ───
    BNM::CompileTimeClass uriType =
        BNM::CompileTimeClassBuilder("System", "Uri").Build();
    auto m = cls_NetworkManager.GetMethod("StartClient", {uriType});
    if (!m.IsValid()) m = cls_NetworkManager.GetMethod("StartClient", 1);
    if (!m.IsValid()) { L("[DC] StartClient NOT FOUND"); return false; }

    L("[DC] resolved: %s", m.str().c_str());

    // ─── CALL با [instance] ← FIX اصلی ───
    L("[DC] >>> CALLING StartClient");
    m[mgr].cast<void>()(uriObj);
    L("[DC] <<< RETURNED");

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    L("[DC] after 500ms: netActive=%d", (int)ReadNetworkActive());

    try {
        L("[DC] isConnected=%d",
          (int)cls_NetworkClient.GetMethod("get_isConnected", 0).cast<bool>().Call());
    } catch (...) {}
    try {
        L("[DC] active=%d",
          (int)cls_NetworkClient.GetMethod("get_active", 0).cast<bool>().Call());
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
        if (ReadCurrentMenu(selfObj) == 0) DisableAllButtons();
    }

    int menu = ReadCurrentMenu(selfObj);
    bool network = ReadNetworkActive();

    if (menu != g_lastMenu) { L("[Update] Menu -> %d", menu); g_lastMenu = menu; }
    if (network != g_lastNetworkActive) {
        L("[Update] Net -> %s", network ? "ON" : "OFF");
        g_lastNetworkActive = network;
    }

    JB_SetGameState(menu, network);
}

// ═══════════════════════════════════════════════════════
// OnClientError
// ═══════════════════════════════════════════════════════
static void* orig_OnClientError = nullptr;

static void Hook_OnClientError(void* self, int err, void* message) {
    std::string msg;
    if (message) {
        try { msg = ((BNM::Structures::Mono::String*)message)->str(); }
        catch (...) {}
    }
    L("*** OnClientError err=%d msg=%s", err, msg.c_str());
    if (orig_OnClientError)
        ((void(*)(void*,int,void*))orig_OnClientError)(self, err, message);
}

static void InstallOnClientErrorHook() {
    if (!cls_CustomNetworkManager.IsValid()) { L("[Hook] CNM invalid"); return; }
    auto m = cls_CustomNetworkManager.GetMethod("OnClientError", 2);
    if (!m.IsValid()) { L("[Hook] not found"); return; }
    auto* info = m.GetInfo();
    L("[Hook] ptr=%p vptr=%p",
      (void*)info->methodPointer, (void*)info->virtualMethodPointer);
    bool ok = BNM::InvokeHook(m, (void*)Hook_OnClientError, orig_OnClientError);
    L("[Hook] InvokeHook=%d", (int)ok);
}

static void InstallUpdateHook() {
    if (g_hookInstalled.load()) return;
    if (!cls_GtaMenu.IsValid()) { L("[Update] GtaMenu invalid"); return; }

    auto m = cls_GtaMenu.GetMethod("Update", 0);
    if (!m.IsValid()) m = cls_GtaMenu.GetMethod("LateUpdate", 0);
    if (!m.IsValid()) { L("[Update] no Update!"); return; }

    bool ok = BNM::InvokeHook(m, (void*)Hook_GtaMenuUpdate, orig_Update);
    L("[Update] InvokeHook=%d", (int)ok);
    if (ok) g_hookInstalled.store(true);
}

static void ResolveUriClass() {
    cls_Uri = BNM::Class("System", "Uri");
    if (cls_Uri.IsValid()) {
        std::string_view s = cls_Uri.GetImage().str();
        L("Uri in: %.*s", (int)s.size(), s.data());
    } else L("!!! Uri NOT found");
}

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
      (int)cls_GtaMenu.IsValid(), (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(), (int)cls_CustomNetworkManager.IsValid(),
      (int)cls_Button.IsValid());
    ResolveUriClass();
    InstallOnClientErrorHook();
    InstallUpdateHook();
    L("=== done ===");
}

void TriggerStartGame() { L("[JNI] queue connect"); g_requestConnect.store(true); }
void DisableModButtons() { L("[JNI] queue disable"); g_requestDisable.store(true); }
void DumpStartClientInfo() { L("[JNI] dump"); }

void StartStateLoop() {
    if (g_running.exchange(true)) return;
    g_retryThread = std::thread([]() {
        InstallGameHooks();
        int attempt = 0;
        while (g_running.load() && !g_hookInstalled.load() && attempt < 60) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            attempt++;
            ResolveClasses();
            InstallUpdateHook();
        }
        if (g_hookInstalled.load()) L("[Retry] OK");
        else L("[Retry] FAILED");
    });
}

void StopStateLoop() {
    g_running.store(false);
    if (g_retryThread.joinable()) g_retryThread.join();
}