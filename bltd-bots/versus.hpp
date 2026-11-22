#pragma once
#include "ue3.hpp"
#include <stdint.h>

namespace Bots {
namespace Versus {

bool Resolve();
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack, void* result);
void OnFrame();
int32_t ScaleDamage(int32_t damage);
void OnBotHit(UE3::UObject* controller, UE3::UObject* instigator);
void OnNoise(UE3::UObject* controller, UE3::UObject* instigator);
void OnTeamList(UE3::UObject* movie, UE3::TArray<UE3::UObject*>* list);

}
}
