#pragma once
#include "fox-ai.hpp"
#include "ue3.hpp"

namespace Bots {
namespace Trace {

bool Resolve();
void Reset();
void OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack, void* result);
void OnCoverRating(UE3::UObject* controller, const FoxAI::CoverInfo* cover, float game_rating, float rating);

}
}
