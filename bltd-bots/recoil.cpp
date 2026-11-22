#include "bots-internal.hpp"
#include "recoil.hpp"

#define RECOIL_LOG(level, format, ...) Log::Write(level, "[Recoil] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::Field;

namespace Bots {
namespace Recoil {

static UObject* process_view_rotation = 0;

static struct {
	int32_t PC_PlayerInput;
	int32_t Input_aLookUp;
	int32_t PC_CurrentWeaponRecoil;
	int32_t PC_TargetWeaponRecoil;
} roff;

static bool ready = false;
static UObject* held_input = 0;
static float held_look_up = 0.0f;

bool Resolve()
{
	process_view_rotation = UE3::FindObject("FoxGame.FoxPC.ProcessViewRotation");
	if (!process_view_rotation) {
		RECOIL_LOG(LOG_LEVEL_WARN, "%s", "FoxGame.FoxPC.ProcessViewRotation not found, recoil keeps the game's reset.");
		return false;
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &roff.PC_PlayerInput, "Engine.PlayerController.PlayerInput" },
		{ &roff.Input_aLookUp, "Engine.PlayerInput.aLookUp" },
		{ &roff.PC_CurrentWeaponRecoil, "FoxGame.FoxPC.CurrentWeaponRecoil" },
		{ &roff.PC_TargetWeaponRecoil, "FoxGame.FoxPC.TargetWeaponRecoil" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			RECOIL_LOG(LOG_LEVEL_WARN, "%s not found, recoil keeps the game's reset.", offsetRef.Path);
			return false;
		}
	}
	ready = true;
	return true;
}

void OnScriptCall(UObject* self, FFrame* stack)
{
	if (!ready || stack->Node != process_view_rotation) {
		return;
	}
	UObject* input = Field<UObject*>(self, roff.PC_PlayerInput);
	if (!input) {
		return;
	}
	float& look_up = Field<float>(input, roff.Input_aLookUp);
	int32_t& current = Field<int32_t>(self, roff.PC_CurrentWeaponRecoil);
	int32_t& target = Field<int32_t>(self, roff.PC_TargetWeaponRecoil);
	// ProcessViewRotation zeroes the recoil on any downward input. This keeps the rest of the kick.
	if (look_up >= 0.0f || target <= current) {
		return;
	}
	target -= current;
	current = 0;
	held_input = input;
	held_look_up = look_up;
	look_up = 0.0f;
}

void OnScriptReturn(UObject* self, FFrame* stack)
{
	if (held_input && stack->Node == process_view_rotation) {
		Field<float>(held_input, roff.Input_aLookUp) = held_look_up;
		held_input = 0;
	}
}

}
}
