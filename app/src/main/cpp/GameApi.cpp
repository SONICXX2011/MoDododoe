#include "GameApi.h"

#include <android/log.h>
#include <dlfcn.h>
#include <algorithm>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ─── امضاها ───
// il2cpp_type_get_object: Il2CppType* → System.Type (Il2CppObject*)
typedef BNM::IL2CPP::Il2CppObject* (*TypeGetObjectFn)(
    BNM::IL2CPP::Il2CppType*);

// FindObjectsOfTypeAll: System.Type → Il2CppArray*
typedef BNM::IL2CPP::Il2CppArray* (*FindObjectsOfTypeAllFn)(
    BNM::IL2CPP::Il2CppObject*);

static TypeGetObjectFn g_typeGetObject = nullptr;
static FindObjectsOfTypeAllFn g_findObjects = nullptr;
static bool g_ready = false;

namespace GameApi {

void Init() {
    if (g_ready) return;

    // گرفتن libil2cpp handle از BNM
    void* lib = BNM::GetIl2CppLibraryHandle();
    if (!lib) {
        lib = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!lib) lib = dlopen("libil2cpp.so", RTLD_LAZY);
    }

    if (lib) {
        g_typeGetObject = (TypeGetObjectFn)dlsym(lib, "il2cpp_type_get_object");
        LOGI("[GameApi] il2cpp_type_get_object = %p", (void*)g_typeGetObject);
    }

    // ICall: اول ResourcesAPIInternal (Unity 2020+), بعد Resources (قدیمی)
    g_findObjects = (FindObjectsOfTypeAllFn)BNM::GetExternMethod(
        "UnityEngine.ResourcesAPIInternal::FindObjectsOfTypeAll");

    if (!g_findObjects) {
        LOGI("[GameApi] trying Resources::FindObjectsOfTypeAll");
        g_findObjects = (FindObjectsOfTypeAllFn)BNM::GetExternMethod(
            "UnityEngine.Resources::FindObjectsOfTypeAll");
    }

    LOGI("[GameApi] FindObjectsOfTypeAll = %p", (void*)g_findObjects);

    g_ready = (g_findObjects != nullptr) && (g_typeGetObject != nullptr);
    LOGI("[GameApi] Init: ready=%d (icall=%d type_get=%d)",
         (int)g_ready, (int)(g_findObjects != nullptr),
         (int)(g_typeGetObject != nullptr));
}

bool IsReady() { return g_ready; }

std::vector<BNM::IL2CPP::Il2CppObject*> GetAllInstances(BNM::Class cls) {
    std::vector<BNM::IL2CPP::Il2CppObject*> result;

    if (!g_findObjects || !g_typeGetObject) {
        LOGI("[GameApi] not ready");
        return result;
    }
    if (!cls.IsValid()) {
        LOGI("[GameApi] class invalid");
        return result;
    }

    // 1) Il2CppType*
    auto* il2cppType = cls.GetIl2CppType();
    if (!il2cppType) {
        LOGI("[GameApi] il2cppType NULL");
        return result;
    }

    // 2) Il2CppType* → System.Type
    auto* typeObj = g_typeGetObject(il2cppType);
    if (!typeObj) {
        LOGI("[GameApi] typeObj NULL");
        return result;
    }
    LOGI("[GameApi] typeObj=%p", (void*)typeObj);

    // 3) ICall(System.Type) → Il2CppArray*
    auto* arr = g_findObjects(typeObj);
    if (!arr) {
        LOGI("[GameApi] ICall returned NULL");
        return result;
    }

    auto* bnArr = (BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*)arr;
    auto cap = bnArr->GetCapacity();
    LOGI("[GameApi] array capacity=%zu", (size_t)cap);

    result.reserve(cap);
    for (size_t i = 0; i < cap; i++) {
        auto* o = *bnArr->At(i);
        if (o) result.push_back(o);
    }

    return result;
}

std::string GetName(BNM::IL2CPP::Il2CppObject* obj) {
    if (!obj) return "";
    try {
        auto* go = BNM::Class(obj)
            .GetMethod("get_gameObject", 0)
            .cast<BNM::IL2CPP::Il2CppObject*>()
            [obj]();
        if (!go) return "";

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
    if (!btn) return false;
    try {
        BNM::Class(btn)
            .GetMethod("set_interactable", 1)
            .cast<void>()
            [btn](value);
        return true;
    } catch (...) { return false; }
}

} // namespace GameApi