#include "ModCore.h"
#include "GameApi.h"
#include "JavaBridge.h"
#include "Globals.h"

#include "BNM/ClassesManagement.hpp"
#include "BNM/Class.hpp"
#include "BNM/Image.hpp"
#include "BNM/Method.hpp"
#include "BNM/Field.hpp"
#include "BNM/Utils.hpp"
#include "BNM/BasicMonoStructures.hpp"
#include "BNM/Delegates.hpp"

#include <android/log.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
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
// CLICK HANDLERS
// ═══════════════════════════════════════════════════════
struct ClickHandlers : public BNM::IL2CPP::Il2CppObject {
    BNM_CustomClass(ClickHandlers,
        (BNM::CompileTimeClassBuilder("MyModMenu", "ClickHandlers").Build()),
        BNM::Defaults::Get<BNM::IL2CPP::Il2CppObject>(),
        BNM::CompileTimeClass());

    static void OnCharacterClick();
    static void OnBackMenuClick();
    static void OnExitClick();

    BNM_CustomMethod(OnCharacterClick, true, BNM::Defaults::Get<void>(), "OnCharacterClick");
    BNM_CustomMethod(OnBackMenuClick,  true, BNM::Defaults::Get<void>(), "OnBackMenuClick");
    BNM_CustomMethod(OnExitClick,      true, BNM::Defaults::Get<void>(), "OnExitClick");
};

void ClickHandlers::OnCharacterClick() {
    LOGI("[CLICK] Character");
    JB_OnCharacterEvent();
}
void ClickHandlers::OnBackMenuClick() {
    LOGI("[CLICK] BackMenu");
    JB_OnBackMenuEvent();
}
void ClickHandlers::OnExitClick() {
    LOGI("[CLICK] Exit");
    JB_OnExitEvent();
}

// ═══════════════════════════════════════════════════════
// CLASS CACHE
// ═══════════════════════════════════════════════════════
static BNM::Class cls_GtaMenu;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;
static BNM::Class cls_Uri;
static BNM::Class cls_Button;

// ═══════════════════════════════════════════════════════
// FLAGS
// ═══════════════════════════════════════════════════════
static std::atomic<bool> g_requestConnect{false};
static std::atomic<bool> g_requestDisable{false};
static std::atomic<bool> g_requestGetObjects{false};
static std::atomic<bool> g_hookInstalled{false};
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_listenersInstalled{false};
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
            .cast<BNM::IL2CPP::Il2CppObject*>().Call();
    } catch (...) { return nullptr; }
}

static int ReadCurrentMenu(BNM::IL2CPP::Il2CppObject* ctrl) {
    if (!ctrl) return -1;
    try {
        auto f = cls_GtaMenu.GetField("currentMenu").cast<int>();
        f[ctrl]; return f.Get();
    } catch (...) { return -1; }
}

static bool ReadNetworkActive() {
    if (!cls_NetworkClient.IsValid()) return false;
    try {
        if (cls_NetworkClient.GetMethod("get_isConnected", 0).cast<bool>().Call()) return true;
    } catch (...) {}
    try {
        if (cls_NetworkClient.GetMethod("get_active", 0).cast<bool>().Call()) return true;
    } catch (...) {}
    return false;
}

// ═══════════════════════════════════════════════════════
// GET OBJECTS
// ═══════════════════════════════════════════════════════
static void DoGetAllObjects() {
    L("========== GET OBJECTS ==========");

    if (!cls_Button.IsValid()) {
        L("[OBJ] Button class invalid");
        return;
    }

    auto all = GameApi::GetAllInstances(cls_Button);
    L("[OBJ] Total: %zu", all.size());

    for (size_t i = 0; i < all.size(); i++) {
        int id = GameApi::GetInstanceID(all[i]);
        std::string name = GameApi::GetName(all[i]);
        L("[OBJ] [%zu] id=%d name='%s'", i, id, name.c_str());
    }

    L("========== END ==========");
}

// ═══════════════════════════════════════════════════════
// UNITYACTION DELEGATE
// ═══════════════════════════════════════════════════════
static BNM::IL2CPP::Il2CppObject* CreateUnityAction(BNM::MethodBase method) {
    try {
        auto imgCore = BNM::Image("UnityEngine.CoreModule.dll");
        if (!imgCore.IsValid()) imgCore = BNM::Image("UnityEngine.CoreModule");

        BNM::Class uaCls("UnityEngine.Events", "UnityAction", imgCore);
        if (!uaCls.IsValid()) {
            L("[DELEGATE] UnityAction not found");
            return nullptr;
        }

        auto* action = uaCls.CreateNewInstance();
        if (!action) return nullptr;

        auto* mc = (BNM::MulticastDelegateBase*)action;
        auto* del = mc->Add(method);
        if (!del) return nullptr;

        return (BNM::IL2CPP::Il2CppObject*)action;
    } catch (...) { return nullptr; }
}

// ═══════════════════════════════════════════════════════
// INSTALL BUTTON LISTENERS (با STAGE logging)
// ═══════════════════════════════════════════════════════
static const std::vector<std::string> CHARACTER_NAMES = {
    "Character", "CHARACTER", "CharacterButton", "CharSelect", "CharSelectButton"
};
static const std::vector<std::string> BACKMENU_NAMES = {
    "BackMenu", "Back Menu", "MenuBack", "Back"
};
static const std::vector<std::string> EXIT_NAMES = {
    "Exit", "ExitButton", "Quit", "Leave"
};

static void InstallButtonListeners() {
    L("[LISTENERS] === START ===");

    if (!cls_Button.IsValid()) { L("[LISTENERS] Button invalid"); return; }

    // ─── STAGE-1: فقط گرفتن آرایه ───
    auto buttons = GameApi::GetAllInstances(cls_Button);
    L("[LISTENERS] STAGE-1: got %zu objects", buttons.size());

    if (buttons.empty()) { L("[LISTENERS] empty"); return; }

    // ─── STAGE-2: لاگ ID + Name ───
    size_t logLimit = buttons.size() < 15 ? buttons.size() : 15;
    for (size_t i = 0; i < logLimit; i++) {
        int id = GameApi::GetInstanceID(buttons[i]);
        std::string name = GameApi::GetName(buttons[i]);
        L("[LISTENERS] STAGE-2: [%zu] id=%d name='%s'", i, id, name.c_str());
    }
    L("[LISTENERS] STAGE-2: done");

    // ─── STAGE-3: چک ClickHandlers ───
    auto handlerCls = BNM::Class("MyModMenu", "ClickHandlers");
    if (!handlerCls.IsValid()) { L("[LISTENERS] ClickHandlers not found"); return; }

    auto charMethod = handlerCls.GetMethod("OnCharacterClick", 0);
    auto backMethod = handlerCls.GetMethod("OnBackMenuClick", 0);
    auto exitMethod = handlerCls.GetMethod("OnExitClick", 0);
    if (!charMethod.IsValid() || !backMethod.IsValid() || !exitMethod.IsValid()) {
        L("[LISTENERS] methods not found");
        return;
    }
    L("[LISTENERS] STAGE-3: methods ok");

    // ─── STAGE-4: ساخت delegateها ───
    auto* charDel = CreateUnityAction(charMethod);
    auto* backDel = CreateUnityAction(backMethod);
    auto* exitDel = CreateUnityAction(exitMethod);
    if (!charDel || !backDel || !exitDel) {
        L("[LISTENERS] delegate creation failed");
        return;
    }
    L("[LISTENERS] STAGE-4: delegates ok");

    // ─── STAGE-5: iterate و bind ───
    int bound = 0;
    for (size_t i = 0; i < buttons.size(); i++) {
        auto* btn = buttons[i];
        if (!btn) continue;

        std::string name = GameApi::GetName(btn);
        if (name.empty()) continue;

        BNM::IL2CPP::Il2CppObject* target = nullptr;
        const char* role = nullptr;
        if (GameApi::MatchesName(name, CHARACTER_NAMES)) { target = charDel; role = "Character"; }
        else if (GameApi::MatchesName(name, BACKMENU_NAMES)) { target = backDel; role = "BackMenu"; }
        else if (GameApi::MatchesName(name, EXIT_NAMES)) { target = exitDel; role = "Exit"; }
        else continue;

        L("[LISTENERS] STAGE-5: trying %s -> %s", name.c_str(), role);

        auto* onClick = BNM::Class(btn)
            .GetMethod("get_onClick", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [btn]();
        if (!onClick) { L("[LISTENERS] STAGE-5: %s onClick null", name.c_str()); continue; }

        BNM::Class(onClick)
            .GetMethod("AddListener", 1)
            .cast<void>()
            [onClick](target);

        L("[LISTENERS] STAGE-5: bound %s -> %s", name.c_str(), role);
        bound++;
    }

    L("[LISTENERS] === END: bound %d ===", bound);
}

// ═══════════════════════════════════════════════════════
// DIRECT CONNECT
// ═══════════════════════════════════════════════════════
static bool DoDirectConnect() {
    L("[DC] === START ===");
    if (!cls_NetworkManager.IsValid()) return false;

    auto* mgr = cls_NetworkManager.GetMethod("get_singleton", 0)
        .cast<BNM::IL2CPP::Il2CppObject*>().Call();
    if (!mgr) { L("[DC] singleton NULL"); return false; }

    if (!cls_Uri.IsValid()) return false;

    char uriStr[64];
    snprintf(uriStr, sizeof(uriStr), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    auto* uriObj = cls_Uri.CreateNewInstance();
    if (!uriObj) return false;

    BNM::CompileTimeClass strType =
        BNM::CompileTimeClassBuilder("System", "String").Build();
    auto ctor = cls_Uri.GetMethod(".ctor", {strType});
    if (!ctor.IsValid()) return false;
    ctor[uriObj].cast<void>()(BNM::CreateMonoString(uriStr));

    BNM::CompileTimeClass uriType =
        BNM::CompileTimeClassBuilder("System", "Uri").Build();
    auto m = cls_NetworkManager.GetMethod("StartClient", {uriType});
    if (!m.IsValid()) m = cls_NetworkManager.GetMethod("StartClient", 1);
    if (!m.IsValid()) { L("[DC] StartClient NOT FOUND"); return false; }

    L("[DC] >>> CALLING");
    m[mgr].cast<void>()(uriObj);
    L("[DC] <<< RETURNED");

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    L("[DC] netActive=%d", (int)ReadNetworkActive());
    return true;
}

// ═══════════════════════════════════════════════════════
// UPDATE HOOK
// ═══════════════════════════════════════════════════════
static void Hook_GtaMenuUpdate(void* self, void* methodInfo) {
    if (orig_Update) ((void(*)(void*,void*))orig_Update)(self, methodInfo);
    auto* selfObj = (BNM::IL2CPP::Il2CppObject*)self;

    // ─── Install listeners روی main thread (فقط یک بار) ───
    if (!g_listenersInstalled.load()) {
        g_listenersInstalled.store(true);
        L("[Update] installing listeners on main thread...");
        InstallButtonListeners();
    }

    if (g_requestConnect.exchange(false)) {
        L("[Update] connect");
        DoDirectConnect();
    }
    if (g_requestDisable.exchange(false)) {
        L("[Update] disable");
        if (cls_Button.IsValid()) {
            auto buttons = GameApi::GetAllInstances(cls_Button);
            int dis = 0;
            for (auto* btn : buttons) {
                std::string name = GameApi::GetName(btn);
                if (GameApi::MatchesName(name, {"LACEDITOR", "COMMUNITY", "DOCUMENT", "LAN"}))
                    if (GameApi::SetInteractable(btn, false)) dis++;
            }
            L("[DIS] %d", dis);
        }
    }
    if (g_requestGetObjects.exchange(false)) {
        L("[Update] get objects");
        DoGetAllObjects();
    }

    g_frameCounter++;
    if (g_frameCounter >= 120) {
        g_frameCounter = 0;
        if (ReadCurrentMenu(selfObj) == 0 && cls_Button.IsValid()) {
            auto buttons = GameApi::GetAllInstances(cls_Button);
            for (auto* btn : buttons) {
                std::string name = GameApi::GetName(btn);
                if (GameApi::MatchesName(name, {"LACEDITOR", "COMMUNITY", "DOCUMENT", "LAN"}))
                    GameApi::SetInteractable(btn, false);
            }
        }
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
        try { msg = ((BNM::Structures::Mono::String*)message)->str(); } catch (...) {}
    }
    L("*** OnClientError err=%d msg=%s", err, msg.c_str());
    if (orig_OnClientError)
        ((void(*)(void*,int,void*))orig_OnClientError)(self, err, message);
}

static void InstallOnClientErrorHook() {
    if (!cls_CustomNetworkManager.IsValid()) return;
    auto m = cls_CustomNetworkManager.GetMethod("OnClientError", 2);
    if (!m.IsValid()) return;
    bool ok = BNM::InvokeHook(m, (void*)Hook_OnClientError, orig_OnClientError);
    L("[Hook] OnClientError=%d", (int)ok);
}

static void InstallUpdateHook() {
    if (g_hookInstalled.load()) return;
    if (!cls_GtaMenu.IsValid()) return;
    auto m = cls_GtaMenu.GetMethod("Update", 0);
    if (!m.IsValid()) m = cls_GtaMenu.GetMethod("LateUpdate", 0);
    if (!m.IsValid()) return;
    bool ok = BNM::InvokeHook(m, (void*)Hook_GtaMenuUpdate, orig_Update);
    L("[Update] InvokeHook=%d", (int)ok);
    if (ok) g_hookInstalled.store(true);
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
    cls_Uri                  = BNM::Class("System", "Uri");
    cls_Button               = BNM::Class("UnityEngine.UI", "Button", imgUI);
}

void InstallGameHooks() {
    L("=== InstallGameHooks ===");
    GameApi::Init();
    ResolveClasses();

    L("Gta=%d NM=%d NC=%d CNM=%d Uri=%d Btn=%d",
      (int)cls_GtaMenu.IsValid(), (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(), (int)cls_CustomNetworkManager.IsValid(),
      (int)cls_Uri.IsValid(), (int)cls_Button.IsValid());

    InstallOnClientErrorHook();
    InstallUpdateHook();
    // InstallButtonListeners(); ← میره توی Update hook (main thread)

    L("=== done ===");
}

void TriggerStartGame() { g_requestConnect.store(true); }
void DisableModButtons() { g_requestDisable.store(true); }
void GetObjectsRequest() { g_requestGetObjects.store(true); }
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
        L("[Retry] done");
    });
}

void StopStateLoop() {
    g_running.store(false);
    if (g_retryThread.joinable()) g_retryThread.join();
}