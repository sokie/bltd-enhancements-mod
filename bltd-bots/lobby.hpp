#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Lobby {

bool Resolve();
bool IsPublic(UE3::UObject* game);
bool KeepsPublicMatch();
void OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack, void* result);

}
}
