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

// ═══════════════════════════════════════════════════════
// Cached classes
// ═══════════════════════════════════════════════════════
static BNM::Class cls_GtaMenu;
static BNM::Class cls_Button;
static BNM::Class cls_Selectable;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;

// ═══════════════════════════════════════════════════════
// Buttons
// ═══════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════
static std::string GetGameObjectName(BNM::IL2CPP::Il2CppObject* comp) {
    if (!comp) return "";
    try {
        auto compCls = BNM::Class("UnityEngine", "Component",
                                  BNM::Image("UnityEngine.CoreModule.dll"));
        auto* go = compCls.GetMethod("get_gameObject", 0)
                      .cast<BNM::IL2CPP::Il2CppObject*>()
                      .Call(comp);
        if (!go) return "";
        auto objCls = BNM::Class("UnityEngine", "Object",
                                 BNM::Image("UnityEngine.CoreModule.dll"));
        auto* nm = objCls.GetMethod("get_name", 0)
                       .cast<BNM::Structures::Mono::String*>()
                       .Call(go);
        if (!nm) return "";
        return nm->str();
    } catch (...) { return ""; }
}

static std::string Normalize(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c==' '||c=='_'||c=='-'||c=='('||c==')') continue;
        o += (char)::tolower((unsigned char)c);
    }
    return o;
}

static bool NameMatches(const std::string& actual,
                        std::initializer_list<const char*> targets) {
    std::string a = Normalize(actual);
    for (auto* t : targets) if (a == Normalize(t)) return true;
    return false;
}

// ═══════════════════════════════════════════════════════
// Button.Awake Hook
// ═══════════════════════════════════════════════════════
typedef void (*AwakeFn)(BNM::IL2CPP::Il2CppObject*);
static AwakeFn orig_Button_Awake = nullptr;

static void Hook_Button_Awake(BNM::IL2CPP::Il2CppObject* self) {
    if (orig_Button_Awake) orig_Button_Awake(self);
    if (!self) return;

    void* key = (void*)self;
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
        L(">>> MATCHED: %s <<<", nm.c_str());
        
        if (cls_Selectable.IsValid()) {
            try {
                cls_Selectable.GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(self, false);
                std::lock_guard<std::mutex> lk(g_btnMtx);
                g_disabled.insert(key);
                L("DISABLED (Selectable): %s", nm.c_str());
            } catch (const std::exception& e) {
                L("Selectable fail: %s", e.what());
            } catch (...) {
                L("Selectable fail: unknown");
            }
        }
        
        if (cls_Button.IsValid()) {
            try {
                cls_Button.GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(self, false);
                std::lock_guard<std::mutex> lk(g_btnMtx);
                g_disabled.insert(key);
                L("DISABLED (Button): %s", nm.c_str());
            } catch (...) {
                L("Button disable fail: %s", nm.c_str());
            }
        }
    }
}

// ═══════════════════════════════════════════════════════
// GtaMenu
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
        return cls_NetworkClient.GetMethod("get_isConnected", 0)
            .cast<bool>()
            .Call();
    } catch (...) { return false; }
}

// ═══════════════════════════════════════════════════════
// DirectConnect — اصلاح شده
// ═══════════════════════════════════════════════════════
static bool DirectConnect() {
    L("[DC] === START ===");

    // ─── ۱. ساخت URI ───
    BNM::Class uriCls("System", "Uri", BNM::Image("System.dll"));
    if (!uriCls.IsValid()) {
        uriCls = BNM::Class("System", "Uri", BNM::Image("mscorlib.dll"));
    }
    if (!uriCls.IsValid()) {
        L("[DC] FAIL: System.Uri not found");
        return false;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

    // روش ۱: CreateNewObjectParameters
    BNM::IL2CPP::Il2CppObject* uri = nullptr;
    try {
        uri = uriCls.CreateNewObjectParameters(BNM::CreateMonoString(buf));
    } catch (...) {}
    
    // روش ۲: alloc + ctor
    if (!uri) {
        try {
            uri = uriCls.CreateNewInstance();
            if (uri) {
                uriCls.GetMethod(".ctor", 1)
                    .cast<void>()
                    .Call(uri, BNM::CreateMonoString(buf));
            }
        } catch (...) {}
    }

    if (!uri) {
        L("[DC] FAIL: Uri creation failed");
        return false;
    }
    L("[DC] uri=%s -> %p", buf, (void*)uri);

    // ─── ۲. تلاش با CustomNetworkManager ───
    if (cls_CustomNetworkManager.IsValid()) {
        L("[DC] Trying CustomNetworkManager");
        
        try {
            // singleton
            auto* mgr = cls_CustomNetworkManager.GetMethod("get_singleton", 0)
                            .cast<BNM::IL2CPP::Il2CppObject*>()
                            .Call();
            
            if (mgr) {
                L("[DC] Custom.singleton=%p", (void*)mgr);
                
                // StartClient(Uri)
                auto startClient = cls_CustomNetworkManager.GetMethod("StartClient", 1);
                if (startClient.IsValid()) {
                    startClient.cast<void>().Call(mgr, uri);
                    L("[DC] Custom.StartClient(Uri) CALLED");
                    return true;
                }
            }
        } catch (const std::exception& e) {
            L("[DC] Custom ex: %s", e.what());
        } catch (...) {
            L("[DC] Custom ex unknown");
        }
    }

    // ─── ۳. تلاش با NetworkManager ───
    if (cls_NetworkManager.IsValid()) {
        L("[DC] Trying NetworkManager");
        
        try {
            auto* mgr = cls_NetworkManager.GetMethod("get_singleton", 0)
                            .cast<BNM::IL2CPP::Il2CppObject*>()
                            .Call();
            
            if (mgr) {
                L("[DC] NM.singleton=%p", (void*)mgr);
                
                // روش ۱: StartClient(Uri)
                auto startClient = cls_NetworkManager.GetMethod("StartClient", 1);
                if (startClient.IsValid()) {
                    startClient.cast<void>().Call(mgr, uri);
                    L("[DC] NM.StartClient(Uri) CALLED");
                    return true;
                }
                
                // روش ۲: networkAddress + StartClient()
                try {
                    cls_NetworkManager.GetField("networkAddress")
                        .cast<BNM::Structures::Mono::String*>()
                        .Set(mgr, BNM::CreateMonoString(buf));
                    L("[DC] networkAddress set");
                    
                    auto sc0 = cls_NetworkManager.GetMethod("StartClient", 0);
                    if (sc0.IsValid()) {
                        sc0.cast<void>().Call(mgr);
                        L("[DC] NM.StartClient() CALLED");
                        return true;
                    }
                } catch (...) {
                    L("[DC] networkAddress method failed");
                }
            }
        } catch (const std::exception& e) {
            L("[DC] NM ex: %s", e.what());
        } catch (...) {
            L("[DC] NM ex unknown");
        }
    }

    L("[DC] FAIL: all methods failed");
    return false;
}

// ═══════════════════════════════════════════════════════
// Trigger Start Game
// ═══════════════════════════════════════════════════════
void TriggerStartGame() {
    std::thread([]() {
        L("--- StartGame ---");
        g_state.networkSuppressed = true;

        int tries = 0;
        while (tries < 100) {
            auto* gta = GetGtaMenu();
            if (gta) {
                L("[SG] GtaMenu found after %d tries", tries);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tries++;
        }

        if (!GetGtaMenu()) {
            L("[SG] FAIL: GtaMenu never appeared");
            return;
        }

        JB_ShowJoinNotification();
        bool ok = DirectConnect();
        L("[SG] connect=%d", (int)ok);
    }).detach();
}

// ═══════════════════════════════════════════════════════
// StateLoop
// ═══════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════
// Install hooks
// ═══════════════════════════════════════════════════════
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
    cls_CustomNetworkManager = BNM::Class("", "CustomNetworkManager", imgAssembly);

    L("GtaMenu=%d", (int)cls_GtaMenu.IsValid());
    L("Button=%d", (int)cls_Button.IsValid());
    L("Selectable=%d", (int)cls_Selectable.IsValid());
    L("NetworkManager=%d", (int)cls_NetworkManager.IsValid());
    L("NetworkClient=%d", (int)cls_NetworkClient.IsValid());
    L("CustomNetworkManager=%d", (int)cls_CustomNetworkManager.IsValid());

    // ─── Button.Awake hook ───
    if (cls_Button.IsValid()) {
        auto awake = cls_Button.GetMethod("Awake", 0);
        void* addr = (void*)awake.GetOffset();
        L("Button.Awake addr=%p", addr);
        
        if (addr) {
            void* stub = shadowhook_hook_func_addr(
                addr,
                (void*)Hook_Button_Awake,
                (void**)&orig_Button_Awake);
            
            if (stub) {
                L("Button.Awake hook OK");
            } else {
                L("Button.Awake hook FAIL err=%d", shadowhook_get_errno());
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