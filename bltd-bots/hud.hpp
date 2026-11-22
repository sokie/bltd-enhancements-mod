#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Hud {

bool Resolve();
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack);
int CrosshairDimensionsCount();
const wchar_t* CrosshairDimensionsName(int choice);
int CrosshairColorCount();
const wchar_t* CrosshairColorName(int choice);

}
}
