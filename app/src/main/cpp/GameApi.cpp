#include "GameApi.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static BNM::Class cls_Type;
static BNM::Class cls_Resources;
static bool g_ready = false;

namespace GameApi {

void Init() {
    if (g_ready) return;

    auto imgMscorlib = BNM::Image("mscorlib.dll");
    if (!imgMscorlib.IsValid()) imgMscorlib = BNM::Image("mscorlib");

    auto imgCore = BNM::Image("UnityEngine.CoreModule.dll");
    if (!imgCore.IsValid()) imgCore = BNM::Image("UnityEngine.CoreModule");

    cls_Type = BNM::Class("System", "Type", imgMscorlib);
    cls_Resources = BNM::Class("UnityEngine", "Resources", imgCore);

    g_ready = cls_Type.IsValid() && cls_Resources.IsValid();
    LOGI("[GameApi] Init: Type=%d Resources=%d",
         (int)cls_Type.IsValid(), (int)cls_Resources.IsValid());
}

bool IsReady() {
    return g_ready;
}

BNM::IL2CPP::Il2CppObject* GetTypeObject(const std::string& fullName) {
    if (!cls_Type.IsValid()) return nullptr;
    try {
        auto m = cls_Type.GetMethod("GetType", {"System.String"});
        if (!m.IsValid()) return nullptr;
        return m.cast<BNM::IL2CPP::Il2CppObject*>()
            .Call(BNM::CreateMonoString(fullName));
    } catch (...) { return nullptr; }
}

std::vector<BNM::IL2CPP::Il2CppObject*> GetAllObjects(const std::string& fullName) {
    std::vector<BNM::IL2CPP::Il2CppObject*> result;

    if (!cls_Resources.IsValid()) {
        LOGI("[GameApi] Resources not ready");
        return result;
    }

    auto* typeObj = GetTypeObject(fullName);
    if (!typeObj) {
        LOGI("[GameApi] Type not found: %s", fullName.c_str());
        return result;
    }

    try {
        auto m = cls_Resources.GetMethod("FindObjectsOfTypeAll", 1);
        if (!m.IsValid()) {
            LOGI("[GameApi] FindObjectsOfTypeAll not found");
            return result;
        }

        auto* arr = m
            .cast<BNM::Structures::Mono::Array<BNM::IL2CPP::Il2CppObject*>*>()
            .Call(typeObj);

        if (!arr) {
            LOGI("[GameApi] %s: NULL array", fullName.c_str());
            return result;
        }

        auto cap = arr->GetCapacity();
        result.reserve(cap);

        for (size_t i = 0; i < cap; i++) {
            auto* obj = *arr->At(i);
            if (obj) result.push_back(obj);
        }

        LOGI("[GameApi] %s: %zu objects", fullName.c_str(), result.size());
    } catch (...) {
        LOGI("[GameApi] %s: exception", fullName.c_str());
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

BNM::IL2CPP::Il2CppObject* FindByName(
    const std::string& className,
    const std::vector<std::string>& names) {
    auto all = GetAllObjects(className);
    for (auto* obj : all) {
        std::string n = GetName(obj);
        if (MatchesName(n, names)) return obj;
    }
    return nullptr;
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