#pragma once
#include "config.hpp"
#include "ue3.hpp"

namespace Bots {
namespace Menu {

bool Resolve();
bool OnScriptCall(UE3::UObject* self, UE3::FFrame* stack);
void OnScriptReturn(UE3::UObject* self, UE3::FFrame* stack);
void ApplyChoice(Config& config, bool public_match);
void SetMovieText(UE3::UObject* movie, const wchar_t* path, const wchar_t* value);
const char* ChosenSquad();

}
}
