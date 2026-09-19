#pragma once

#include "BNM/Class.hpp"
#include "BNM/Image.hpp"
#include "BNM/Method.hpp"
#include "BNM/Field.hpp"
#include "BNM/Utils.hpp"
#include "BNM/BasicMonoStructures.hpp"

#include <string>
#include <vector>

namespace GameApi {

// راه‌اندازی اولیه (کش کردن کلاس‌های پایه)
void Init();
bool IsReady();

// گرفتن System.Type از رشته (مثلاً "UnityEngine.UI.Button")
BNM::IL2CPP::Il2CppObject* GetTypeObject(const std::string& fullName);

// گرفتن همه‌ی instanceهای یه کلاس — دقیقاً معادل Il2Cpp.gc.choose
std::vector<BNM::IL2CPP::Il2CppObject*> GetAllObjects(const std::string& fullName);

// گرفتن اسم GameObject از هر component
std::string GetName(BNM::IL2CPP::Il2CppObject* obj);

// normalize کردن اسم (حذف فاصله، _، -)
std::string Normalize(const std::string& s);

// چک کردن match با یه لیست اسم
bool MatchesName(const std::string& actual,
                 const std::vector<std::string>& names);

// پیدا کردن اولین آبجکت با اسم
BNM::IL2CPP::Il2CppObject* FindByName(
    const std::string& className,
    const std::vector<std::string>& names);

// ست کردن interactable روی یه Button
bool SetInteractable(BNM::IL2CPP::Il2CppObject* btn, bool value);

} // namespace GameApi