#include "GameApi.h"

#include <android/log.h>
#include <dlfcn.h>
#include <signal.h>
#include <setjmp.h>
#include <cstring>
#include <algorithm>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef BNM::IL2CPP::Il2CppObject* (*TypeGetObjectFn)(
    BNM::IL2CPP::Il2CppType*);

typedef BNM::IL2CPP::Il2CppArray* (*FindObjectsOfTypeAllFn)(
    BNM::IL2CPP::Il2CppObject*);

static TypeGetObjectFn g_typeGetObject = nullptr;
static FindObjectsOfTypeAllFn g_findObjects = nullptr;
static bool g_ready = false;

// ─── SIGSEGV trap ───
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_trap_active = 0;

static void trap_handler(int sig) {
    if (g_trap_active) {
        g_trap_active = 0;
        siglongjmp(g_jmp, 1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

// اجرای امن یه بلاک کد — اگه crash داد، false برمی‌گردونه
template <typename Fn>
static bool SafeRun(Fn&& fn) {
    struct sigaction sa_new, sa_old;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = trap_handler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = SA_NODEFER;

    if (sigaction(SIGSEGV, &sa_new, &sa_old) != 0) {
        // اگه نتونستیم trap کنیم، شانس بیار
        fn();
        return true;
    }

    g_trap_active = 1;
    bool ok = false;
    if (sigsetjmp(g_jmp, 1) == 0) {
        fn();
        ok = true;
    }
    g_trap_active = 0;

    sigaction(SIGSEGV, &sa_old, nullptr);
    return ok;
}

namespace GameApi {

void Init() {
    if (g_ready) return;

    void* lib = BNM::GetIl2CppLibraryHandle();
    if (!lib) {
        lib = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!lib) lib = dlopen("libil2cpp.so", RTLD_LAZY);
    }

    if (lib) {
        g_typeGetObject = (TypeGetObjectFn)dlsym(lib, "il2cpp_type_get_object");
    }

    g_findObjects = (FindObjectsOfTypeAllFn)BNM::GetExternMethod(
        "UnityEngine.ResourcesAPIInternal::FindObjectsOfTypeAll");
    if (!g_findObjects) {
        g_findObjects = (FindObjectsOfTypeAllFn)BNM::GetExternMethod(
            "UnityEngine.Resources::FindObjectsOfTypeAll");
    }

    g_ready = (g_findObjects != nullptr) && (g_typeGetObject != nullptr);
    LOGI("[GameApi] Init: ready=%d", (int)g_ready);
}

bool IsReady() { return g_ready; }

std::vector<BNM::IL2CPP::Il2CppObject*> GetAllInstances(BNM::Class cls) {
    std::vector<BNM::IL2CPP::Il2CppObject*> result;

    if (!g_findObjects || !g_typeGetObject) return result;
    if (!cls.IsValid()) return result;

    auto* il2cppType = cls.GetIl2CppType();
    if (!il2cppType) return result;

    auto* typeObj = g_typeGetObject(il2cppType);
    if (!typeObj) return result;

    auto* arr = g_findObjects(typeObj);
    if (!arr) return result;

    auto* bnArr = (BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*)arr;
    auto cap = bnArr->GetCapacity();
    LOGI("[GameApi] raw capacity=%zu", (size_t)cap);

    result.reserve(cap);
    int crashed = 0;

    for (size_t i = 0; i < cap; i++) {
        BNM::IL2CPP::Il2CppObject* o = nullptr;

        // خوندن entry از آرایه هم ممکنه کرش کنه
        bool readOk = SafeRun([&]() {
            o = *bnArr->At(i);
        });

        if (!readOk || !o) { crashed++; continue; }

        // تست اینکه این آبجکت واقعاً قابل استفاده‌ست
        bool usable = SafeRun([&]() {
            // امتحان کن get_gameObject
            auto* go = BNM::Class(o)
                .GetMethod("get_gameObject", 0)
                .cast<BNM::IL2CPP::Il2CppObject*>()
                [o]();

            if (!go) return;

            // امتحان کن get_name
            auto* n = BNM::Class(go)
                .GetMethod("get_name", 0)
                .cast<BNM::Structures::Mono::String*>()
                [go]();

            (void)n;
        });

        if (!usable) { crashed++; continue; }

        result.push_back(o);
    }

    LOGI("[GameApi] usable=%zu, crashed_skipped=%d", result.size(), crashed);
    return result;
}

std::string GetName(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return "";
    std::string out;
    SafeRun([&]() {
        auto* go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [obj]();
        if (!go) return;

        auto* n = BNM::Class(go)
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            [go]();
        if (!n) return;

        out = n->str();
    });
    return out;
}

std::string Normalize(const std::string& s) {
    std::string n;
    for (char c : s) {
        if (!isspace((unsigned char)c) && c != '_' && c != '-')
            n += toupper((unsigned char)c);
    }
    return n;
}

bool MatchesName(const std::string& actual,
                 const std::vector<std::string>& names) {
    std::string na = Normalize(actual);
    for (const auto& n : names) {
        if (na == Normalize(n)) return true;
    }
    return false;
}

bool SetInteractable(BNM::IL2CPP::Il2CppObject* btn, bool value) {
    if (!btn) return false;
    bool ok = false;
    SafeRun([&]() {
        BNM::Class(btn)
            .GetMethod("set_interactable", 1)
            .cast<void>()
            [btn](value);
        ok = true;
    });
    return ok;
}

} // namespace GameApi