#pragma once
#include "ue3.hpp"
#include <stdint.h>

namespace Bots {
namespace Server {

bool IsDedicatedWorld(UE3::UObject* game);
void Install();
bool Resolve();
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack, void* result);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack, void* result);

}
}
