#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Recoil {

bool Resolve();
void OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack);

}
}
