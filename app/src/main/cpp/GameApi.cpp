#include "GameApi.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>

#define LOG_TAG "LACMod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using Il2CppObject = BNM::IL2CPP::Il2CppObject;
using ObjectArray  = BNM::Structures::Mono::Array<Il2CppObject*>;

static bool g_ready = false;
static BNM::Class g_objectClass;

namespace GameApi {

void Init() {
    if (g_ready) return;

    auto core = BNM::Image("UnityEngine.CoreModule.dll");
    if (!core.IsValid()) core = BNM::Image("UnityEngine.CoreModule");
    if (!core.IsValid()) { LOGI("[GameApi] CoreModule invalid"); return; }

    g_objectClass = BNM::Class("UnityEngine", "Object", core);
    if (!g_objectClass.IsValid()) { LOGI("[GameApi] Object invalid"); return; }

    auto m = g_objectClass.GetMethod("FindObjectsOfType", 2);
    if (!m.IsValid()) {
        LOGI("[GameApi] FindObjectsOfType(Type,bool) not found — fallback 1-arg");
        m = g_objectClass.GetMethod("FindObjectsOfType", 1);
    }
    if (!m.IsValid()) { LOGI("[GameApi] FindObjectsOfType not found at all"); return; }

    LOGI("[GameApi] FindObjectsOfType = %s", m.str().c_str());
    g_ready = true;
    LOGI("[GameApi] Init OK");
}

bool IsReady() { return g_ready; }

std::vector<Il2CppObject*> GetAllInstances(BNM::Class cls) {
    std::vector<Il2CppObject*> result;

    if (!g_ready) { LOGI("[GameApi] not ready"); return result; }
    if (!cls.IsValid()) { LOGI("[GameApi] invalid class"); return result; }

    // تبدیل Il2CppType* → System.Type از طریق API رسمی BNM
    auto* monoType = cls.GetMonoType();
    if (!monoType) { LOGI("[GameApi] GetMonoType null"); return result; }

    auto findObjects = g_objectClass.GetMethod("FindObjectsOfType", 2);
    bool twoArg = findObjects.IsValid();
    if (!twoArg) findObjects = g_objectClass.GetMethod("FindObjectsOfType", 1);
    if (!findObjects.IsValid()) { LOGI("[GameApi] method gone"); return result; }

    ObjectArray* arr = nullptr;

    // استفاده از TryInvoke رسمی BNM — نه SIGSEGV trap دستی
    auto ex = BNM::TryInvoke([&]() {
        if (twoArg) {
            arr = findObjects.cast<ObjectArray*>().Call(monoType, true);
        } else {
            arr = findObjects.cast<ObjectArray*>().Call(monoType);
        }
    });

    if (ex.IsValid()) {
        LOGI("[GameApi] FindObjectsOfType exception: %s: %s",
             ex.ClassName().c_str(), ex.Message().c_str());
        return result;
    }

    if (!arr) { LOGI("[GameApi] array null"); return result; }

    // Array::ToVector — API رسمی BNM
    auto objects = arr->ToVector();
    LOGI("[GameApi] Unity returned %zu objects", objects.size());

    result.reserve(objects.size());
    for (auto* obj : objects) {
        if (obj) result.push_back(obj);
    }

    LOGI("[GameApi] returning %zu objects", result.size());
    return result;
}

int GetInstanceID(Il2CppObject* obj) {
    if (!obj || !g_ready) return -1;
    int id = -1;
    auto m = g_objectClass.GetMethod("GetInstanceID", 0);
    if (!m.IsValid()) return -1;
    auto ex = BNM::TryInvoke([&]() {
        id = m.cast<int>()[obj]();
    });
    (void)ex;
    return id;
}

std::string GetName(Il2CppObject* obj) {
    if (!obj || !g_ready) return "";
    std::string out;
    auto m = g_objectClass.GetMethod("get_name", 0);
    if (!m.IsValid()) return "";
    auto ex = BNM::TryInvoke([&]() {
        auto* s = m.cast<BNM::Structures::Mono::String*>()[obj]();
        if (s) out = s->str();
    });
    (void)ex;
    return out;
}

std::string Normalize(const std::string& s) {
    std::string n;
    for (unsigned char c : s) {
        if (std::isspace(c) || c == '_' || c == '-') continue;
        n += (char)std::toupper(c);
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

bool SetInteractable(Il2CppObject* btn, bool value) {
    if (!btn) return false;
    auto cls = BNM::Class(btn);
    if (!cls.IsValid()) return false;
    auto m = cls.GetMethod("set_interactable", 1);
    if (!m.IsValid()) return false;
    bool ok = false;
    auto ex = BNM::TryInvoke([&]() {
        m.cast<void>()[btn](value);
        ok = true;
    });
    (void)ex;
    return ok;
}

} // namespace GameApi