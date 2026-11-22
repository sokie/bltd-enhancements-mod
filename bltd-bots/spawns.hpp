#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Spawns {

bool Resolve();
void OnLevelUnload();
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack, void* result);
bool TakeSpot(UE3::UObject* controller, UE3::FVector& spot, UE3::FRotator& rotation);

}
}
