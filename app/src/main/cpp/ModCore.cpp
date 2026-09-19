#include "ModCore.h"
#include "JavaBridge.h"
#include "Globals.h"

#include "BNM/Class.hpp"
#include "BNM/Image.hpp"
#include "BNM/Method.hpp"
#include "BNM/Field.hpp"
#include "BNM/BasicMonoStructures.hpp"
#include "BNM/Utils.hpp"

extern "C" {
#include <shadowhook.h>
}

#include <android/log.h>
#include <dlfcn.h>
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
static BNM::Class cls_GtaMenu;
static BNM::Class cls_Button;
static BNM::Class cls_Selectable;
static BNM::Class cls_NetworkManager;
static BNM::Class cls_NetworkClient;
static BNM::Class cls_CustomNetworkManager;

struct BtnEntry {
    BNM::IL2CPP::Il2CppObject* obj;
    std::string name;
    bool disabled;
};

static std::recursive_mutex       g_btnMtx;
static std::vector<BtnEntry>      g_buttons;
static std::unordered_set<void*>  g_known;

static std::atomic<bool> g_running{false};
static std::thread       g_loopThread;
static std::atomic<bool> g_buttonsReady{false};

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
// پیدا کردن دکمه‌ها — از FindObjectOfType استفاده می‌کنیم
// روی Android بعضی متدها حذف شدن، پس fallback داریم
// ═══════════════════════════════════════════════════════
static void ScanButtonsOnce() {
    if (!cls_Button.IsValid()) {
        LOGE("ScanButtons: Button class invalid");
        return;
    }

    try {
        auto clsUnityObject = BNM::Class(
            "UnityEngine", "Object",
            BNM::Image("UnityEngine.CoreModule.dll"));
        if (!clsUnityObject.IsValid()) return;

        auto* typePtr = cls_Button.GetIl2CppType();
        if (!typePtr) { LOGE("Type ptr null"); return; }

        BNM::IL2CPP::Il2CppArray* arr = nullptr;

        // روش ۱: FindObjectsByType
        try {
            auto m = clsUnityObject.GetMethod("FindObjectsByType", 2);
            if (m.IsValid()) {
                arr = m.cast<BNM::IL2CPP::Il2CppArray*>()
                       .Call(typePtr, (int)0);
                if (arr) LOGI("FindObjectsByType OK");
            }
        } catch (...) {}

        // روش ۲: FindObjectsOfType(Type, bool)
        if (!arr) {
            try {
                auto m = clsUnityObject.GetMethod("FindObjectsOfType", 2);
                if (m.IsValid()) {
                    arr = m.cast<BNM::IL2CPP::Il2CppArray*>()
                           .Call(typePtr, false);
                    if (arr) LOGI("FindObjectsOfType(Type,bool) OK");
                }
            } catch (...) {}
        }

        // روش ۳: FindObjectsOfType(Type)
        if (!arr) {
            try {
                auto m = clsUnityObject.GetMethod("FindObjectsOfType", 1);
                if (m.IsValid()) {
                    arr = m.cast<BNM::IL2CPP::Il2CppArray*>()
                           .Call(typePtr);
                    if (arr) LOGI("FindObjectsOfType(Type) OK");
                }
            } catch (...) {}
        }

        if (!arr) {
            LOGE("ScanButtons: all Find methods failed");
            return;
        }

        typedef BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*> ObjArr;
        auto* objArr = (ObjArr*) arr;

        int len = (int) objArr->capacity;
        LOGI("ScanButtons: found %d buttons", len);
        JB_Log("Found " + std::to_string(len) + " buttons");

        std::lock_guard<std::recursive_mutex> lk(g_btnMtx);

        for (int i = 0; i < len; i++) {
            auto* btn = objArr->m_Items[i];
            if (!btn) continue;

            void* key = (void*) btn;
            if (g_known.count(key)) continue;
            g_known.insert(key);

            std::string nm = GetGameObjectName(btn);
            if (nm.empty()) continue;

            g_buttons.push_back({ btn, nm, false });
            LOGI("Found Button: %s", nm.c_str());
            JB_Log("Btn: " + nm);
        }

        g_buttonsReady = true;
    } catch (const std::exception& e) {
        LOGE("ScanButtons ex: %s", e.what());
    } catch (...) {
        LOGE("ScanButtons unknown ex");
    }
}

static void DisableButtonsFromList() {
    if (!g_buttonsReady) return;

    std::lock_guard<std::recursive_mutex> lk(g_btnMtx);

    int total = 0, done = 0;
    for (auto& b : g_buttons) {
        if (b.disabled) continue;
        if (!NameMatches(b.name, {"LACEDITOR", "COMMUNITY", "DOCUMENT"})) continue;

        total++;
        bool ok = false;

        if (cls_Selectable.IsValid()) {
            try {
                cls_Selectable.GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(b.obj, false);
                ok = true;
            } catch (...) {}
        }

        if (!ok && cls_Button.IsValid()) {
            try {
                cls_Button.GetMethod("set_interactable", 1)
                    .cast<void>()
                    .Call(b.obj, false);
                ok = true;
            } catch (...) {}
        }

        if (ok) {
            b.disabled = true;
            done++;
            L("DISABLED: %s", b.name.c_str());
        }
    }

    if (total > 0 && done == 0) {
        L("Wanted to disable %d, failed all", total);
    }
}

// ═══════════════════════════════════════════════════════
// این تابع فقط از main thread (il2cpp thread) صدا زده بشه
// ═══════════════════════════════════════════════════════
void DisableModButtons() {
    L("--- DisableModButtons ---");
    if (!g_buttonsReady) {
        ScanButtonsOnce();
    }
    DisableButtonsFromList();
    L("--- done ---");
}

// ═══════════════════════════════════════════════════════
void DumpStartClientInfo() {
    L("═══════ DUMP START ═══════");

    if (cls_CustomNetworkManager.IsValid()) {
        L("[CNM] Methods:");
        try {
            for (auto& m : cls_CustomNetworkManager.GetMethods()) {
                try { L("  %s", m.str().c_str()); } catch (...) {}
            }
        } catch (...) {}
    }

    L("═══════ DUMP END ═══════");
}

// ═══════════════════════════════════════════════════════
// DirectConnect — دو روش رو با هم امتحان می‌کنیم
// ═══════════════════════════════════════════════════════
static bool DirectConnect() {
    L("[DC] === START ===");

    if (!cls_CustomNetworkManager.IsValid()) {
        L("[DC] CNM invalid");
        return false;
    }

    auto* mgr = cls_CustomNetworkManager.GetMethod("get_singleton", 0)
                    .cast<BNM::IL2CPP::Il2CppObject*>()
                    .Call();
    if (!mgr) {
        L("[DC] CNM.singleton null");
        return false;
    }
    L("[DC] CNM.singleton=%p", (void*)mgr);

    // ─── مرحله ۱: ست کردن networkAddress ───
    try {
        auto fld = cls_NetworkManager.GetField("networkAddress")
                       .cast<BNM::Structures::Mono::String*>();
        if (fld.IsValid()) {
            fld[mgr];
            fld.Set(BNM::CreateMonoString(SERVER_IP));
            L("[DC] NM.networkAddress=%s", SERVER_IP);
        }
    } catch (...) {
        L("[DC] set networkAddress failed");
    }

    // ─── مرحله ۲: StartClient() بدون آرگومان (اولویت اول) ───
    try {
        auto m = cls_CustomNetworkManager.GetMethod("StartClient", 0);
        if (m.IsValid()) {
            m.cast<void>().Call(mgr);
            L("[DC] CNM.StartClient() OK");
            return true;
        } else {
            L("[DC] StartClient() not found");
        }
    } catch (const std::exception& e) {
        L("[DC] StartClient() ex: %s", e.what());
    } catch (...) {
        L("[DC] StartClient() unknown ex");
    }

    // ─── مرحله ۳: StartClient(Uri) به عنوان fallback ───
    try {
        BNM::Class uriCls("System", "Uri", BNM::Image("System.dll"));
        if (!uriCls.IsValid())
            uriCls = BNM::Class("System", "Uri", BNM::Image("mscorlib.dll"));
        if (!uriCls.IsValid()) {
            L("[DC] FAIL: Uri missing");
            return false;
        }

        char uri[64];
        snprintf(uri, sizeof(uri), "kcp://%s:%d", SERVER_IP, SERVER_PORT);

        auto* uriObj = uriCls.CreateNewObjectParameters(
            BNM::CreateMonoString(uri));
        if (!uriObj) {
            L("[DC] FAIL: Uri alloc");
            return false;
        }
        L("[DC] uri=%s", uri);

        auto m = cls_CustomNetworkManager.GetMethod("StartClient", 1);
        if (m.IsValid()) {
            m.cast<void>().Call(mgr, uriObj);
            L("[DC] CNM.StartClient(Uri) OK");
            return true;
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
        L("--- StartGame thread ---");

        // صبر کن GtaMenu آماده بشه
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
// StateLoop — بدون AttachIl2Cpp (چون از خود il2cpp thread صدا زده میشه)
// ═══════════════════════════════════════════════════════
static void StateLoop() {
    L("StateLoop: started");

    using clock = std::chrono::steady_clock;
    auto last    = clock::now();
    auto lastBtn = clock::now();

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

        // اسکن دکمه‌ها هر 2 ثانیه (فقط اگه هنوز پیدا نکردیم)
        if (!g_buttonsReady) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastBtn).count() >= 2000) {
                lastBtn = now;
                ScanButtonsOnce();
                if (g_buttonsReady) DisableButtonsFromList();
            }
        }
    }
}

// ═══════════════════════════════════════════════════════
void InstallGameHooks() {
    L("=== InstallGameHooks ===");

    auto imgAsm    = BNM::Image("Assembly-CSharp.dll");
    if (!imgAsm.IsValid()) imgAsm = BNM::Image("Assembly-CSharp");

    auto imgUI     = BNM::Image("UnityEngine.UI.dll");
    if (!imgUI.IsValid()) imgUI = BNM::Image("UnityEngine.UI");

    auto imgMirror = BNM::Image("Mirror.dll");
    if (!imgMirror.IsValid()) imgMirror = BNM::Image("Mirror");

    cls_GtaMenu              = BNM::Class("", "GtaMenuControl", imgAsm);
    cls_Button               = BNM::Class("UnityEngine.UI", "Button", imgUI);
    cls_Selectable           = BNM::Class("UnityEngine.UI", "Selectable", imgUI);
    cls_NetworkManager       = BNM::Class("Mirror", "NetworkManager", imgMirror);
    cls_NetworkClient        = BNM::Class("Mirror", "NetworkClient", imgMirror);
    cls_CustomNetworkManager = BNM::Class("", "CustomNetworkManager", imgAsm);

    L("Gta=%d Btn=%d Sel=%d NM=%d NC=%d CNM=%d",
      (int)cls_GtaMenu.IsValid(),
      (int)cls_Button.IsValid(),
      (int)cls_Selectable.IsValid(),
      (int)cls_NetworkManager.IsValid(),
      (int)cls_NetworkClient.IsValid(),
      (int)cls_CustomNetworkManager.IsValid());

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