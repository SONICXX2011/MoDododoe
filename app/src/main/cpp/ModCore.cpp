#include "ModCore.h"
#include "JavaBridge.h"
#include "Globals.h"

#include "BNM/Class.hpp"
#include "BNM/Image.hpp"
#include "BNM/Method.hpp"
#include "BNM/Field.hpp"
#include "BNM/Utils.hpp"

extern "C" {
#include <shadowhook.h>
}

#include <android/log.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>
#include <cstdio>
#include <cstdarg>

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
static BNM::Class cls_Button;
static BNM::Class cls_Selectable;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;

struct BtnEntry {
    BNM::IL2CPP::Il2CppObject* obj;
    std::string name;
};

static std::mutex                g_btnMtx;
static std::vector<BtnEntry>     g_buttons;
static std::unordered_set<void*> g_known;
static std::unordered_set<void*> g_disabled;

static std::atomic<bool> g_running{false};
static std::thread       g_loopThread;

static std::string GetGameObjectName(BNM::IL2CPP::Il2CppObject* comp) {
    if (!comp) return "";
    try {
        auto compCls = BNM::Class(
            "UnityEngine",
            "Component",
            BNM::Image("UnityEngine.CoreModule.dll"));
        auto* go = compCls
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call(comp);
        if (!go) return "";
        auto objCls = BNM::Class(
            "UnityEngine",
            "Object",
            BNM::Image("UnityEngine.CoreModule.dll"));
        auto* nm = objCls
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            .Call(go);
        if (!nm) return "";
        return nm->str();
    } catch (...) {
        return "";
    }
}

static std::string Normalize(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == ' ' || c == '_' || c == '-' || c == '(' || c == ')') continue;
        o += (char) ::tolower((unsigned char) c);
    }
    return o;
}

static bool NameMatches(
    const std::string& actual,
    std::initializer_list<const char*> targets)
{
    std::string a = Normalize(actual);
    for (auto* t : targets) {
        if (a == Normalize(t)) return true;
    }
    return false;
}

typedef void (*AwakeFn)(BNM::IL2CPP::Il2CppObject*);
static AwakeFn orig_Button_Awake = nullptr;

static void Hook_Button_Awake(BNM::IL2CPP::Il2CppObject* self) {
    if (orig_Button_Awake) orig_Button_Awake(self);
    if (!self) return;

    void* key = (void*) self;
    {
        std::lock_guard<std::mutex> lk(g_btnMtx);
        if (g_known.count(key)) return;
        g_known.insert(key);
    }

    std::string nm = GetGameObjectName(self);
    if (nm.empty()) return;

    {
        std::lock_guard<std::mutex> lk(g_btnMtx);
        g_buttons.push_back({ self, nm });
    }

    LOGI("Button.Awake: %s", nm.c_str());
    JB_Log("Btn: " + nm);

    if (NameMatches(nm, { "LACEDITOR", "COMMUNITY", "DOCUMENT" })) {
        if (cls_Button.IsValid()) {
            try {
                cls_Button
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(self, false);
                std::lock_guard<std::mutex> lk(g_btnMtx);
                g_disabled.insert(key);
                L("DISABLED: %s", nm.c_str());
            } catch (...) {
                LOGE("disable fail: %s", nm.c_str());
            }
        }
    }
}

static BNM::IL2CPP::Il2CppObject* GetGtaMenu() {
    if (!cls_GtaMenu.IsValid()) return nullptr;
    try {
        return cls_GtaMenu
            .GetMethod("get_Instance", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();
    } catch (...) {
        return nullptr;
    }
}

static int GetCurrentMenu() {
    auto* ctrl = GetGtaMenu();
    if (!ctrl) return -1;
    try {
        BNM::Field<int> f = cls_GtaMenu.GetField("currentMenu").cast<int>();
        f[ctrl];
        return f.Get();
    } catch (...) {
        return -1;
    }
}

static bool GetNetworkActive() {
    if (!cls_NetworkClient.IsValid()) return false;
    try {
        return cls_NetworkClient
            .GetMethod("get_isConnected", 0)
            .cast<bool>()
            .Call();
    } catch (...) {
        return false;
    }
}

static bool DirectConnect() {
    L("[DC] begin");
    try {
        L("[DC] NM valid=%d", (int) cls_NetworkManager.IsValid());
        if (!cls_NetworkManager.IsValid()) {
            L("[DC] FAIL: NetworkManager missing");
            return false;
        }

        auto* mgr = cls_NetworkManager
            .GetMethod("get_singleton", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();
        L("[DC] singleton=%p", (void*) mgr);
        if (!mgr) {
            L("[DC] FAIL: singleton null");
            return false;
        }

        BNM::Class uriCls("System", "Uri", BNM::Image("System.dll"));
        L("[DC] Uri(System)=%d", (int) uriCls.IsValid());
        if (!uriCls.IsValid()) {
            uriCls = BNM::Class("System", "Uri", BNM::Image("mscorlib.dll"));
            L("[DC] Uri(mscorlib)=%d", (int) uriCls.IsValid());
        }
        if (!uriCls.IsValid()) {
            L("[DC] FAIL: Uri class missing");
            return false;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "kcp://%s:%d", SERVER_IP, SERVER_PORT);
        L("[DC] addr=%s", buf);

        auto* uri = uriCls.CreateNewObjectParameters(
            BNM::CreateMonoString(buf));
        L("[DC] uri_obj=%p", (void*) uri);
        if (!uri) {
            L("[DC] FAIL: uri alloc");
            return false;
        }

        cls_NetworkManager
            .GetMethod("StartClient", 1)
            .cast<void>()
            .Call(mgr, uri);

        L("[DC] StartClient OK");
        return true;
    } catch (const std::exception& e) {
        L("[DC] EX: %s", e.what());
    } catch (...) {
        L("[DC] EX unknown");
    }
    return false;
}

void TriggerStartGame() {
    std::thread([]() {
        L("--- StartGame ---");
        g_state.networkSuppressed = true;

        int tries = 0;
        while (tries < 100 && !GetGtaMenu()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tries++;
        }
        L("[SG] waited %d tries, gta=%p", tries, (void*) GetGtaMenu());

        JB_ShowJoinNotification();
        bool ok = DirectConnect();
        L("[SG] connect=%d", (int) ok);
    }).detach();
}

static void StateLoop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    while (g_running) {
        auto now = clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last).count() < 200) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
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
    }
}

void InstallGameHooks() {
    L("=== InstallGameHooks ===");

    auto imgAssembly = BNM::Image("Assembly-CSharp.dll");
    if (!imgAssembly.IsValid()) imgAssembly = BNM::Image("Assembly-CSharp");

    auto imgUI = BNM::Image("UnityEngine.UI.dll");
    if (!imgUI.IsValid()) imgUI = BNM::Image("UnityEngine.UI");

    auto imgMirror = BNM::Image("Mirror.dll");
    if (!imgMirror.IsValid()) imgMirror = BNM::Image("Mirror");

    cls_GtaMenu        = BNM::Class("", "GtaMenuControl", imgAssembly);
    cls_Button         = BNM::Class("UnityEngine.UI", "Button", imgUI);
    cls_Selectable     = BNM::Class("UnityEngine.UI", "Selectable", imgUI);
    cls_NetworkManager = BNM::Class("Mirror", "NetworkManager", imgMirror);
    cls_NetworkClient  = BNM::Class("Mirror", "NetworkClient", imgMirror);

    L("Gta=%d Btn=%d NM=%d NC=%d",
      (int) cls_GtaMenu.IsValid(),
      (int) cls_Button.IsValid(),
      (int) cls_NetworkManager.IsValid(),
      (int) cls_NetworkClient.IsValid());

    if (cls_Button.IsValid()) {
        auto awake = cls_Button.GetMethod("Awake", 0);
        void* addr = (void*) awake.GetOffset();
        L("Awake addr=%p", addr);
        if (addr) {
            void* stub = shadowhook_hook_func_addr(
                addr,
                (void*) Hook_Button_Awake,
                (void**) &orig_Button_Awake);
            if (stub) {
                L("Button.Awake hooked OK");
            } else {
                L("Button.Awake FAIL err=%d", shadowhook_get_errno());
            }
        }
    } else {
        L("Button class invalid");
    }

    L("=== done ===");
}

void StartStateLoop() {
    if (g_running.exchange(true)) return;
    g_loopThread = std::thread(StateLoop);
    L("StateLoop started");
}

void StopStateLoop() {
    if (!g_running.exchange(false)) return;
    if (g_loopThread.joinable()) g_loopThread.join();
    L("StateLoop stopped");
}