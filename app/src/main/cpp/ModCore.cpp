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
#include <string>
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
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;

static std::atomic<bool> g_running{false};
static std::thread       g_loopThread;
static std::atomic<bool> g_hookInstalled{false};

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
// Hook OnClientError — بفهمیم چرا fail میشه
// ═══════════════════════════════════════════════════════
typedef void (*OnClientErrorFn)(void* self, int transportError, void* message);
static OnClientErrorFn orig_OnClientError = nullptr;

static void Hook_OnClientError(void* self, int transportError, void* message) {
    std::string msg = "";
    if (message) {
        try {
            auto* s = (BNM::Structures::Mono::String*) message;
            msg = s->str();
        } catch (...) {}
    }

    L("★★★ OnClientError code=%d msg=%s", transportError, msg.c_str());

    if (orig_OnClientError) {
        orig_OnClientError(self, transportError, message);
    }
}

static void InstallOnClientErrorHook() {
    if (g_hookInstalled) return;

    if (!cls_CustomNetworkManager.IsValid()) {
        L("Hook: CNM invalid");
        return;
    }

    try {
        // پیدا کردن OnClientError با 2 پارامتر
        auto m = cls_CustomNetworkManager.GetMethod("OnClientError", 2);

        if (!m.IsValid()) {
            L("Hook: OnClientError not found");
            return;
        }

        void* addr = (void*) m.GetOffset();
        L("Hook: OnClientError addr=%p", addr);

        if (!addr) {
            L("Hook: addr null");
            return;
        }

        void* stub = shadowhook_hook_func_addr(
            addr,
            (void*) Hook_OnClientError,
            (void**) &orig_OnClientError);

        if (stub) {
            L("Hook: OnClientError installed");
            g_hookInstalled = true;
        } else {
            L("Hook: FAIL err=%d", shadowhook_get_errno());
        }
    } catch (const std::exception& e) {
        L("Hook ex: %s", e.what());
    } catch (...) {
        L("Hook unknown ex");
    }
}

// ═══════════════════════════════════════════════════════
static void LogTransportInfo(void* mgr) {
    try {
        auto trField = cls_NetworkManager.GetField("transport");
        if (!trField.IsValid()) return;

        auto* tr = trField
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [mgr]
            .Get();

        L("transport=%p", (void*) tr);

        if (!tr) {
            L("⚠ transport NULL");
            return;
        }

        auto* klass = tr->klass;
        if (klass && klass->name) {
            L("transport class=%s", klass->name);
        }
    } catch (...) {}
}

// ═══════════════════════════════════════════════════════
// DirectConnect — فقط StartClient(Uri) با scheme kcp
// ═══════════════════════════════════════════════════════
static bool DirectConnect() {
    L("[DC] start");

    if (!cls_CustomNetworkManager.IsValid()) {
        L("[DC] CNM invalid");
        return false;
    }

    auto* mgr = cls_CustomNetworkManager
                    .GetMethod("get_singleton", 0)
                    .cast<BNM::IL2CPP::Il2CppObject*>()
                    .Call();
    if (!mgr) {
        L("[DC] singleton null");
        return false;
    }
    L("[DC] singleton=%p", (void*)mgr);

    LogTransportInfo(mgr);

    // ─── networkAddress هم ست می‌کنیم (برای اطمینان) ───
    try {
        char fullAddr[128];
        snprintf(fullAddr, sizeof(fullAddr), "%s:%d", SERVER_IP, SERVER_PORT);

        auto fld = cls_NetworkManager.GetField("networkAddress")
                       .cast<BNM::Structures::Mono::String*>();
        if (fld.IsValid()) {
            fld[mgr];
            fld.Set(BNM::CreateMonoString(fullAddr));
            L("[DC] networkAddress = %s", fullAddr);
        }
    } catch (...) {
        L("[DC] set networkAddress ex");
    }

    // ─── StartClient(Uri) با scheme kcp ───
    try {
        BNM::Class uriCls("System", "Uri", BNM::Image("System.dll"));
        if (!uriCls.IsValid())
            uriCls = BNM::Class("System", "Uri", BNM::Image("mscorlib.dll"));

        if (!uriCls.IsValid()) {
            L("[DC] Uri class missing");
            return false;
        }

        char uri[64];
        snprintf(uri, sizeof(uri), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

        auto* uriObj = uriCls.CreateNewObjectParameters(
            BNM::CreateMonoString(uri));
        if (!uriObj) {
            L("[DC] Uri alloc failed");
            return false;
        }
        L("[DC] uri = %s | obj=%p", uri, (void*)uriObj);

        auto m = cls_NetworkManager.GetMethod("StartClient", 1);
        if (m.IsValid()) {
            m.cast<void>().Call(mgr, uriObj);
            L("[DC] StartClient(Uri) CALLED");
            return true;
        } else {
            L("[DC] StartClient(Uri) method not found");
        }
    } catch (const std::exception& e) {
        L("[DC] StartClient(Uri) ex: %s", e.what());
    } catch (...) {
        L("[DC] StartClient(Uri) unknown ex");
    }

    L("[DC] FAIL");
    return false;
}

// ═══════════════════════════════════════════════════════
void TriggerStartGame() {
    std::thread([]() {
        L("--- StartGame ---");
        g_state.networkSuppressed = true;

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
static void StateLoop() {
    L("StateLoop started");

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
void InstallGameHooks() {
    L("=== InstallGameHooks ===");

    auto imgAsm    = BNM::Image("Assembly-CSharp.dll");
    if (!imgAsm.IsValid()) imgAsm = BNM::Image("Assembly-CSharp");

    auto imgMirror = BNM::Image("Mirror.dll");
    if (!imgMirror.IsValid()) imgMirror = BNM::Image("Mirror");

    cls_GtaMenu              = BNM::Class("", "GtaMenuControl", imgAsm);
    cls_NetworkManager       = BNM::Class("Mirror", "NetworkManager", imgMirror);
    cls_NetworkClient        = BNM::Class("Mirror", "NetworkClient", imgMirror);
    cls_CustomNetworkManager = BNM::Class("", "CustomNetworkManager", imgAsm);

    L("Gta=%d NM=%d NC=%d CNM=%d",
      (int)cls_GtaMenu.IsValid(),
      (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(),
      (int)cls_CustomNetworkManager.IsValid());

    InstallOnClientErrorHook();

    L("=== done ===");
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
void DisableModButtons()   { L("(disable disabled)"); }