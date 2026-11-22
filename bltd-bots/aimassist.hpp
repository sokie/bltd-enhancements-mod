#pragma once
#include "ue3.hpp"

namespace Bots {
namespace AimAssist {

bool Resolve();
void OnLevelUnload();
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack, void* result);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack);

}
}
