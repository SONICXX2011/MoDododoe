#include "GameApi.h"

#include <android/log.h>
#include <dlfcn.h>
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
static BNM::Class cls_ObjectBase;

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

    // کلاس پایه UnityEngine.Object برای خوندن m_CachedPtr
    auto imgCore = BNM::Image("UnityEngine.CoreModule.dll");
    if (!imgCore.IsValid()) imgCore = BNM::Image("UnityEngine.CoreModule");
    cls_ObjectBase = BNM::Class("UnityEngine", "Object", imgCore);

    g_ready = (g_findObjects != nullptr) && (g_typeGetObject != nullptr);
    LOGI("[GameApi] Init: ready=%d", (int)g_ready);
}

bool IsReady() { return g_ready; }

// ─── چک کردن زنده بودن آبجکت ───
static bool IsAlive(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return false;
    try {
        auto* ptr = (void*)((uint8_t*)obj + sizeof(void*) * 2);
        uintptr_t cachedPtr = *(uintptr_t*)ptr;
        return cachedPtr != 0;
    } catch (...) { return false; }
}

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
    LOGI("[GameApi] raw array capacity=%zu", (size_t)cap);

    result.reserve(cap);
    int dead = 0;

    for (size_t i = 0; i < cap; i++) {
        auto* o = *bnArr->At(i);
        if (!o) continue;

        if (!IsAlive(o)) {
            dead++;
            continue;
        }

        result.push_back(o);
    }

    LOGI("[GameApi] alive=%zu, dead=%d", result.size(), dead);
    return result;
}

std::string GetName(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj || !IsAlive(obj)) return "";
    try {
        auto* go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [obj]();
        if (!go || !IsAlive(go)) return "";

        auto* n = BNM::Class(go)
            .GetMethod("get_name", 0)
            .cast<BNM::Structures::Mono::String*>()
            [go]();
        if (!n) return "";
        return n->str();
    } catch (...) { return ""; }
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
    if (!btn || !IsAlive(btn)) return false;
    try {
        BNM::Class(btn)
            .GetMethod("set_interactable", 1)
            .cast<void>()
            [btn](value);
        return true;
    } catch (...) { return false; }
}

} // namespace GameApi