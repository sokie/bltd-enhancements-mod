#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Events {

void OnPlayerTick(UE3::UObject* controller);
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnLevelUnload();

}
}
