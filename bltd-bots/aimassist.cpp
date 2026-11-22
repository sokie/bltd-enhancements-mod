#include "bots-internal.hpp"
#include "aimassist.hpp"
#include <algorithm>
#include <vector>

#define ASSIST_LOG(level, format, ...) Log::Write(level, "[AimAssist] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::Field;

namespace Bots {
namespace AimAssist {

static const int32_t SETTING_OFF = 1000;
static const int32_t SETTING_ON = 1001;

static struct {
	UObject* PC_GetAdjustedAimFor;
	UObject* PC_ServerToggleTeam;
	UObject* Weapon_ShouldApplyViewFriction;
	UObject* Input_bAimAssistOn;
} ref;

static struct {
	int32_t PC_PlayerInput;
	int32_t GetAdjustedAimFor_W;
	int32_t ServerToggleTeam_N;
	int32_t Weapon_AimAssistScale;
	int32_t Weapon_AimAssistScaleWhileZoomed;
} aoff;

static bool ready = false;
static std::vector<UObject*> players_off;
static UObject* sent_player = 0;
static bool sent_on = true;
static bool local_on = true;
static UObject* zeroed_weapon = 0;
static float saved_scale = 0.0f;
static float saved_zoomed_scale = 0.0f;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.PC_GetAdjustedAimFor, "FoxGame.FoxPC.GetAdjustedAimFor" },
		{ &ref.PC_ServerToggleTeam, "FoxGame.FoxPC.ServerToggleTeam" },
		{ &ref.Weapon_ShouldApplyViewFriction, "FoxGame.FoxWeapon.ShouldApplyViewFriction" },
		{ &ref.Input_bAimAssistOn, "FoxGame.FoxPlayerInput.bAimAssistOn" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			ASSIST_LOG(LOG_LEVEL_WARN, "%s not found, the option only turns off the view adhesion.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &aoff.PC_PlayerInput, "Engine.PlayerController.PlayerInput" },
		{ &aoff.GetAdjustedAimFor_W, "FoxGame.FoxPC.GetAdjustedAimFor.W" },
		{ &aoff.ServerToggleTeam_N, "FoxGame.FoxPC.ServerToggleTeam.N" },
		{ &aoff.Weapon_AimAssistScale, "FoxGame.FoxWeapon.AimAssistScale" },
		{ &aoff.Weapon_AimAssistScaleWhileZoomed, "FoxGame.FoxWeapon.AimAssistScaleWhileZoomed" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			ASSIST_LOG(LOG_LEVEL_WARN, "%s not found, the option only turns off the view adhesion.", offsetRef.Path);
			return false;
		}
	}
	ready = true;
	return true;
}

void OnLevelUnload()
{
	players_off.clear();
	sent_player = 0;
	zeroed_weapon = 0;
}

static void SendSetting(UObject* pc)
{
	UObject* input = Field<UObject*>(pc, aoff.PC_PlayerInput);
	if (!input || !Field<UObject*>(pc, off.Controller_Pawn)) {
		return;
	}
	local_on = UE3::GetBoolProperty(input, ref.Input_bAimAssistOn);
	if (pc == sent_player && local_on == sent_on) {
		return;
	}
	sent_player = pc;
	sent_on = local_on;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.PC_ServerToggleTeam) + 16, 0);
	Field<int32_t>(parms.data(), aoff.ServerToggleTeam_N) = local_on ? SETTING_ON : SETTING_OFF;
	UE3::ProcessEvent(pc, ref.PC_ServerToggleTeam, parms.data());
	ASSIST_LOG(LOG_LEVEL_INFO, "This player's aim assist is %s.", local_on ? "on" : "off");
}

static bool IsOff(UObject* pc)
{
	return Field<UObject*>(pc, aoff.PC_PlayerInput) ? !UE3::GetBoolProperty(Field<UObject*>(pc, aoff.PC_PlayerInput), ref.Input_bAimAssistOn)
		: std::find(players_off.begin(), players_off.end(), pc) != players_off.end();
}

bool OnScriptCall(UObject* self, FFrame* stack, void* result)
{
	if (!ready) {
		return true;
	}
	UObject* node = stack->Node;
	if (node == ref.PC_ServerToggleTeam) {
		int32_t number = Field<int32_t>(stack->Locals, aoff.ServerToggleTeam_N);
		if (number != SETTING_OFF && number != SETTING_ON) {
			return true;
		}
		auto found = std::find(players_off.begin(), players_off.end(), self);
		if (number == SETTING_OFF && found == players_off.end()) {
			players_off.push_back(self);
		}
		else if (number == SETTING_ON && found != players_off.end()) {
			players_off.erase(found);
		}
		return false;
	}
	if (node == ref.Weapon_ShouldApplyViewFriction) {
		if (!local_on) {
			if (result) {
				*(uint32_t*)result = 0;
			}
			return false;
		}
		return true;
	}
	if (node == ref.PC_GetAdjustedAimFor) {
		UObject* weapon = Field<UObject*>(stack->Locals, aoff.GetAdjustedAimFor_W);
		if (weapon && IsOff(self)) {
			zeroed_weapon = weapon;
			saved_scale = Field<float>(weapon, aoff.Weapon_AimAssistScale);
			saved_zoomed_scale = Field<float>(weapon, aoff.Weapon_AimAssistScaleWhileZoomed);
			Field<float>(weapon, aoff.Weapon_AimAssistScale) = 0.0f;
			Field<float>(weapon, aoff.Weapon_AimAssistScaleWhileZoomed) = 0.0f;
		}
		return true;
	}
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == fn.PC_PlayerTick) {
		SendSetting(self);
	}
	else if (node == ref.PC_GetAdjustedAimFor && zeroed_weapon) {
		Field<float>(zeroed_weapon, aoff.Weapon_AimAssistScale) = saved_scale;
		Field<float>(zeroed_weapon, aoff.Weapon_AimAssistScaleWhileZoomed) = saved_zoomed_scale;
		zeroed_weapon = 0;
	}
}

}
}
