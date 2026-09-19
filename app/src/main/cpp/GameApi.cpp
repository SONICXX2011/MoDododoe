#include "GameApi.h"

#include <android/log.h>
#include <dlfcn.h>
#include <algorithm>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef BNM::IL2CPP::Il2CppObject* (*TypeGetObjectFn)(
    BNM::IL2CPP::Il2CppType*);

static TypeGetObjectFn g_typeGetObject = nullptr;
static bool g_ready = false;
static BNM::Class cls_Object;

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

    auto imgCore = BNM::Image("UnityEngine.CoreModule.dll");
    if (!imgCore.IsValid()) imgCore = BNM::Image("UnityEngine.CoreModule");
    cls_Object = BNM::Class("UnityEngine", "Object", imgCore);

    g_ready = (g_typeGetObject != nullptr) && cls_Object.IsValid();
    LOGI("[GameApi] Init: ready=%d type_get=%d Object=%d",
         (int)g_ready, (int)(g_typeGetObject != nullptr),
         (int)cls_Object.IsValid());
}

bool IsReady() { return g_ready; }

std::vector<BNM::IL2CPP::Il2CppObject*> GetAllInstances(BNM::Class cls) {
    std::vector<BNM::IL2CPP::Il2CppObject*> result;

    if (!g_typeGetObject || !cls_Object.IsValid()) return result;
    if (!cls.IsValid()) return result;

    // Il2CppType* → System.Type
    auto* il2cppType = cls.GetIl2CppType();
    if (!il2cppType) return result;

    auto* typeObj = g_typeGetObject(il2cppType);
    if (!typeObj) return result;

    // Object.FindObjectsOfType(Type, bool) ← فقط scene objects
    auto m = cls_Object.GetMethod("FindObjectsOfType", 2);
    if (!m.IsValid()) {
        LOGI("[GameApi] FindObjectsOfType not found");
        return result;
    }

    // includeInactive = true
    auto* arr = m
        .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
        .Call(typeObj, true);

    if (!arr) {
        LOGI("[GameApi] FindObjectsOfType returned NULL");
        return result;
    }

    auto cap = arr->GetCapacity();
    LOGI("[GameApi] live scene objects=%zu", (size_t)cap);

    result.reserve(cap);
    for (size_t i = 0; i < cap; i++) {
        auto* o = *arr->At(i);
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