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

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════
// Cached classes
// ═══════════════════════════════════════════════════════
static BNM::Class cls_GtaMenu;
static BNM::Class cls_Button;
static BNM::Class cls_Selectable;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;

// ═══════════════════════════════════════════════════════
// Tracked buttons
// ═══════════════════════════════════════════════════════
struct BtnEntry {
    BNM::IL2CPP::Il2CppObject* obj;
    std::string name;
};

static std::mutex                g_btnMtx;
static std::vector<BtnEntry>     g_buttons;
static std::unordered_set<void*> g_known;
static std::unordered_set<void*> g_disabled;

// ═══════════════════════════════════════════════════════
// Thread control
// ═══════════════════════════════════════════════════════
static std::atomic<bool> g_running{false};
static std::thread       g_loopThread;

// ═══════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════
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
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '_' || c == '-' || c == '(' || c == ')') {
            continue;
        }
        out += (char) ::tolower((unsigned char) c);
    }
    return out;
}

static bool NameMatches(
    const std::string& actual,
    std::initializer_list<const char*> targets)
{
    std::string norm = Normalize(actual);

    for (auto* t : targets) {
        if (norm == Normalize(t)) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════
// Button.Awake hook
// ═══════════════════════════════════════════════════════
typedef void (*AwakeFn)(BNM::IL2CPP::Il2CppObject*);
static AwakeFn orig_Button_Awake = nullptr;

static void Hook_Button_Awake(BNM::IL2CPP::Il2CppObject* self) {
    if (orig_Button_Awake) {
        orig_Button_Awake(self);
    }

    if (!self) return;

    void* key = (void*) self;

    // ─── Already known? ───
    {
        std::lock_guard<std::mutex> lk(g_btnMtx);
        if (g_known.count(key)) return;
        g_known.insert(key);
    }

    // ─── Fetch name ───
    std::string nm = GetGameObjectName(self);
    if (nm.empty()) return;

    {
        std::lock_guard<std::mutex> lk(g_btnMtx);
        g_buttons.push_back({ self, nm });
    }

    LOGI("Button.Awake: %s", nm.c_str());

    // ─── Disable specific buttons ───
    if (NameMatches(nm, { "LACEDITOR", "COMMUNITY", "DOCUMENT" })) {
        if (cls_Button.IsValid()) {
            try {
                cls_Button
                    .GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(self, false);

                std::lock_guard<std::mutex> lk(g_btnMtx);
                g_disabled.insert(key);

                LOGI("disabled: %s", nm.c_str());
            } catch (...) {
                LOGE("failed to disable: %s", nm.c_str());
            }
        }
    }
}

// ═══════════════════════════════════════════════════════
// Game state readers
// ═══════════════════════════════════════════════════════
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
        BNM::Field<int> f = cls_GtaMenu
            .GetField("currentMenu")
            .cast<int>();

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

// ═══════════════════════════════════════════════════════
// Direct Connect — Mirror.NetworkManager.singleton.StartClient(Uri)
// ═══════════════════════════════════════════════════════
static bool DirectConnect() {
    try {
        if (!cls_NetworkManager.IsValid()) {
            LOGE("DirectConnect: NetworkManager invalid");
            return false;
        }

        auto* mgr = cls_NetworkManager
            .GetMethod("get_singleton", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            .Call();

        if (!mgr) {
            LOGE("DirectConnect: singleton null");
            return false;
        }

        // ─── System.Uri ───
        BNM::Class uriCls("System", "Uri", BNM::Image("System.dll"));
        if (!uriCls.IsValid()) {
            uriCls = BNM::Class("System", "Uri", BNM::Image("mscorlib.dll"));
        }
        if (!uriCls.IsValid()) {
            LOGE("DirectConnect: Uri class missing");
            return false;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

        auto* uri = uriCls.CreateNewObjectParameters(
            BNM::CreateMonoString(buf));

        if (!uri) {
            LOGE("DirectConnect: Uri alloc failed");
            return false;
        }

        cls_NetworkManager
            .GetMethod("StartClient", 1)
            .cast<void>()
            .Call(mgr, uri);

        LOGI("DIRECT CONNECT -> %s", buf);
        return true;
    } catch (const std::exception& e) {
        LOGE("DirectConnect ex: %s", e.what());
    } catch (...) {
        LOGE("DirectConnect: unknown exception");
    }
    return false;
}

// ═══════════════════════════════════════════════════════
// Start Game (called from JNI)
// ═══════════════════════════════════════════════════════
void TriggerStartGame() {
    std::thread([]() {
        g_state.networkSuppressed = true;
        JB_Hide();

        // ─── Wait for GtaMenuControl ───
        for (int i = 0; i < 100 && !GetGtaMenu(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        JB_ShowJoinNotification();
        DirectConnect();
    }).detach();
}

// ═══════════════════════════════════════════════════════
// StateLoop — network + menu watcher
// ═══════════════════════════════════════════════════════
static void StateLoop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    bool menuShown = false;

    while (g_running) {
        auto now = clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last).count() < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        last = now;

        // ─── Show menu as soon as GtaMenu is ready ───
        if (!menuShown && GetGtaMenu()) {
            if (!GetNetworkActive()) {
                JB_Show();
                menuShown = true;
            }
        }

        int  menu    = GetCurrentMenu();
        bool network = GetNetworkActive();

        // ─── Menu change ───
        if (menu >= 0 && menu != g_state.lastMenu) {
            LOGI("Menu -> %d", menu);
            g_state.lastMenu = menu;
        }

        // ─── Network change ───
        if (network != g_state.lastNetworkActive) {
            g_state.lastNetworkActive = network;
            LOGI("NetworkActive -> %s", network ? "true" : "false");

            if (network) {
                g_state.networkSuppressed = true;
                JB_Hide();
                menuShown = false;
            } else {
                g_state.networkSuppressed = false;
                JB_OnExitEvent();
                JB_Show();
                menuShown = true;
            }
        }

        JB_SetGameState(menu, network);
    }
}

// ═══════════════════════════════════════════════════════
// Install hooks (called from BNM OnLoaded)
// ═══════════════════════════════════════════════════════
void InstallGameHooks() {
    LOGI("=== InstallGameHooks ===");

    auto imgAssembly = BNM::Image("Assembly-CSharp.dll");
    if (!imgAssembly.IsValid()) {
        imgAssembly = BNM::Image("Assembly-CSharp");
    }

    auto imgUI = BNM::Image("UnityEngine.UI.dll");
    if (!imgUI.IsValid()) {
        imgUI = BNM::Image("UnityEngine.UI");
    }

    auto imgMirror = BNM::Image("Mirror.dll");
    if (!imgMirror.IsValid()) {
        imgMirror = BNM::Image("Mirror");
    }

    cls_GtaMenu        = BNM::Class("", "GtaMenuControl", imgAssembly);
    cls_Button         = BNM::Class("UnityEngine.UI", "Button", imgUI);
    cls_Selectable     = BNM::Class("UnityEngine.UI", "Selectable", imgUI);
    cls_NetworkManager = BNM::Class("Mirror", "NetworkManager", imgMirror);
    cls_NetworkClient  = BNM::Class("Mirror", "NetworkClient", imgMirror);

    LOGI("GtaMenuControl  valid=%d", (int) cls_GtaMenu.IsValid());
    LOGI("Button          valid=%d", (int) cls_Button.IsValid());
    LOGI("Selectable      valid=%d", (int) cls_Selectable.IsValid());
    LOGI("NetworkManager  valid=%d", (int) cls_NetworkManager.IsValid());
    LOGI("NetworkClient   valid=%d", (int) cls_NetworkClient.IsValid());

    // ─── Hook Button.Awake ───
    if (cls_Button.IsValid()) {
        auto awake = cls_Button.GetMethod("Awake", 0);
        void* addr = (void*) awake.GetOffset();

        if (addr) {
            void* stub = shadowhook_hook_func_addr(
                addr,
                (void*) Hook_Button_Awake,
                (void**) &orig_Button_Awake);

            if (stub) {
                LOGI("Button.Awake hook installed @ %p", addr);
            } else {
                int err = shadowhook_get_errno();
                LOGE("Button.Awake hook FAILED: %d - %s",
                     err, shadowhook_to_errmsg(err));
            }
        } else {
            LOGE("Button.Awake address null");
        }
    }

    LOGI("=== InstallGameHooks done ===");
}

void StartStateLoop() {
    if (g_running.exchange(true)) return;

    g_loopThread = std::thread(StateLoop);
    LOGI("StateLoop started");
}

void StopStateLoop() {
    if (!g_running.exchange(false)) return;

    if (g_loopThread.joinable()) {
        g_loopThread.join();
    }
    LOGI("StateLoop stopped");
}