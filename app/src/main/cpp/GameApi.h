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

void Init();
bool IsReady();

// فقط آبجکت‌های زنده (با ۳ لایه فیلتر)
std::vector<BNM::IL2CPP::Il2CppObject*> GetAllInstances(BNM::Class cls);

std::string GetName(BNM::IL2CPP::Il2CppObject* obj);
std::string Normalize(const std::string& s);
bool MatchesName(const std::string& actual,
                 const std::vector<std::string>& names);
bool SetInteractable(BNM::IL2CPP::Il2CppObject* btn, bool value);

} // namespace GameApi