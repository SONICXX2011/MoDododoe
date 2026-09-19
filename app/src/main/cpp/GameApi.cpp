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

static TypeGetObjectFn g_typeGetObject = nullptr;
static bool g_ready = false;
static BNM::Class cls_Object;

// ═══════════════════════════════════════════════════════
// SIGSEGV trap دائمی (نصب یک بار)
// ═══════════════════════════════════════════════════════
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_trap_active = 0;
static struct sigaction g_old_segv;
static bool g_trap_installed = false;

static void trap_handler(int sig) {
    if (g_trap_active) {
        g_trap_active = 0;
        siglongjmp(g_jmp, 1);
    }
    sigaction(sig, &g_old_segv, nullptr);
    raise(sig);
}

static void InstallTrap() {
    if (g_trap_installed) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = trap_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, &g_old_segv);
    g_trap_installed = true;
    LOGI("[GameApi] SIGSEGV trap installed");
}

template <typename Fn>
static bool SafeRun(Fn&& fn) {
    if (sigsetjmp(g_jmp, 1) == 0) {
        g_trap_active = 1;
        fn();
        g_trap_active = 0;
        return true;
    }
    g_trap_active = 0;
    return false;
}

namespace GameApi {

void Init() {
    if (g_ready) return;
    InstallTrap();

    void* lib = BNM::GetIl2CppLibraryHandle();
    if (!lib) {
        lib = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!lib) lib = dlopen("libil2cpp.so", RTLD_LAZY);
    }
    if (lib) {
        g_typeGetObject = (TypeGetObjectFn)dlsym(lib, "il2cpp_type_get_object");
    }

    auto imgCore = BNM::Image("UnityEngine.CoreModule.dll");
    if (!imgCore.IsValid()) imgCore = BNM::Image("UnityEngine.CoreModule");
    cls_Object = BNM::Class("UnityEngine", "Object", imgCore);

    g_ready = (g_typeGetObject != nullptr) && cls_Object.IsValid();
    LOGI("[GameApi] Init: ready=%d type_get=%d Object=%d",
         (int)g_ready, (int)(g_typeGetObject != nullptr),
         (int)cls_Object.IsValid());
}

bool IsReady() { return g_ready; }

// ─── ۳ لایه فیلتر ───
static bool IsFullyAlive(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return false;

    // لایه ۱: m_CachedPtr != 0
    bool layer1 = false;
    SafeRun([&]() {
        uintptr_t cached = *(uintptr_t*)((uint8_t*)obj + sizeof(void*) * 2);
        layer1 = (cached != 0);
    });
    if (!layer1) return false;

    // لایه ۲: get_gameObject != null
    BNM::IL2CPP::Il2CppObject* go = nullptr;
    SafeRun([&]() {
        go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [obj]();
    });
    if (!go) return false;

    // لایه ۳: get_name != null و غیرخالی
    bool layer3 = false;
    SafeRun([&]() {
        auto* n = BNM::Class(go)
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            [go]();
        layer3 = (n != nullptr && n->length > 0);
    });
    return layer3;
}

std::vector<BNM::IL2CPP::Il2CppObject*> GetAllInstances(BNM::Class cls) {
    std::vector<BNM::IL2CPP::Il2CppObject*> result;

    if (!g_typeGetObject || !cls_Object.IsValid()) return result;
    if (!cls.IsValid()) return result;

    auto* il2cppType = cls.GetIl2CppType();
    if (!il2cppType) return result;

    auto* typeObj = g_typeGetObject(il2cppType);
    if (!typeObj) return result;

    // FindObjectsOfType(Type, bool) — فقط scene objects
    auto m = cls_Object.GetMethod("FindObjectsOfType", 2);
    if (!m.IsValid()) {
        LOGI("[GameApi] FindObjectsOfType not found");
        return result;
    }

    auto* arr = m
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        .Call(typeObj, true);

    if (!arr) return result;

    auto cap = arr->GetCapacity();
    LOGI("[GameApi] raw=%zu", (size_t)cap);

    result.reserve(cap);
    int skipped = 0;

    for (size_t i = 0; i < cap; i++) {
        BNM::IL2CPP::Il2CppObject* o = nullptr;
        SafeRun([&]() { o = *arr->At(i); });
        if (!o) { skipped++; continue; }

        if (!IsFullyAlive(o)) { skipped++; continue; }

        result.push_back(o);
    }

    LOGI("[GameApi] alive=%zu skipped=%d", result.size(), skipped);
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