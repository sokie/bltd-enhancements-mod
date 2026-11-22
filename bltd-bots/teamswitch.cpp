#include "bots-internal.hpp"
#include "menu.hpp"
#include "teamswitch.hpp"
#include <stdio.h>
#include <vector>
#include <wchar.h>

#define SWITCH_LOG(level, format, ...) Log::Write(level, "[TeamSwitch] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::FString;
using UE3::Field;
using UE3::TArray;

namespace Bots {
namespace TeamSwitch {

static const DWORD PRESS_MS = 500;
static const DWORD SWITCH_WAIT_MS = 60000;
static const wchar_t TOOLTIP_PATH[] = L"_root.p_bottomBar.l_btnTooltip.htmlText";
static const wchar_t TOOLTIP_COMMAND[] = L"normalBtnTooltip";
static const wchar_t DENIED_UNEVEN[] = L"CANNOT SWITCH: THE OTHER TEAM HAS MORE PLAYERS";
static const wchar_t DENIED_LAST[] = L"CANNOT SWITCH: YOU ARE THE LAST PLAYER ON YOUR TEAM";
static const wchar_t DENIED_TIME[] = L"CANNOT SWITCH: ONLY IN THE FIRST HALF OF THE MATCH";
static const wchar_t DENIED_ROUNDS[] = L"CANNOT SWITCH: THE MATCH HAS STARTED";
static const wchar_t DENIED_WAIT[] = L"CANNOT SWITCH: WAIT %u SECONDS";

static struct {
	UObject* BackTDM_SwitchTeams;
	UObject* LobbyTDM_SwitchTeams;
	UObject* BackTDM_Command;
	UObject* LobbyTDM_Command;
	UObject* BackTDM_bOnPlayerList;
	UObject* LobbyTDM_bOnPlayerList;
	UObject* PC_IsGamePublic;
	UObject* PC_ServerToggleTeam;
	UObject* FoxPawn_PlayerChangedTeam;
	UObject* Pawn_PlayerChangedTeam;
	UObject* GRI_bMatchHasBegun;
	UObject* GRI_LTS;
	UObject* GRI_DET;
	UObject* PRI_bBot;
	UObject* PRI_bOnlySpectator;
} ref;

static struct {
	int32_t Movie_PCOwner;
	int32_t BackTDM_Command_Cmd;
	int32_t LobbyTDM_Command_Cmd;
	int32_t IsGamePublic_ReturnValue;
	int32_t WorldInfo_GRI;
	int32_t GRI_PRIArray;
	int32_t GRI_RemainingTime;
	int32_t GRI_ElapsedTime;
	int32_t WorldInfo_Game;
	int32_t Game_TimeLimit;
	int32_t Controller_PRI;
	int32_t PRI_Team;
	int32_t Team_TeamIndex;
	int32_t PC_PlayerInput;
} toff;

static bool ready = false;
static bool pretend_private = false;
struct LastSwitch {
	UObject* Player;
	DWORD Tick;
};
static std::vector<LastSwitch> last_switches;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.BackTDM_SwitchTeams, "FoxGame.FoxMainMenuMovieBackTDM.SwitchTeams" },
		{ &ref.LobbyTDM_SwitchTeams, "FoxGame.FoxMainMenuMovieGameLobbyTDM.SwitchTeams" },
		{ &ref.BackTDM_Command, "FoxGame.FoxMainMenuMovieBackTDM.OnReceiveFSCommand" },
		{ &ref.LobbyTDM_Command, "FoxGame.FoxMainMenuMovieGameLobbyTDM.OnReceiveFSCommand" },
		{ &ref.BackTDM_bOnPlayerList, "FoxGame.FoxMainMenuMovieBackTDM.bOnPlayerList" },
		{ &ref.LobbyTDM_bOnPlayerList, "FoxGame.FoxMainMenuMovieGameLobbyTDM.bOnPlayerList" },
		{ &ref.PC_IsGamePublic, "FoxGame.FoxPC.IsGamePublic" },
		{ &ref.PC_ServerToggleTeam, "FoxGame.FoxPC.ServerToggleTeam" },
		{ &ref.FoxPawn_PlayerChangedTeam, "FoxGame.FoxPawn.PlayerChangedTeam" },
		{ &ref.Pawn_PlayerChangedTeam, "Engine.Pawn.PlayerChangedTeam" },
		{ &ref.GRI_bMatchHasBegun, "Engine.GameReplicationInfo.bMatchHasBegun" },
		{ &ref.GRI_LTS, "FoxGame.FoxGRI_LTS" },
		{ &ref.GRI_DET, "FoxGame.FoxGRI_DET" },
		{ &ref.PRI_bBot, "Engine.PlayerReplicationInfo.bBot" },
		{ &ref.PRI_bOnlySpectator, "Engine.PlayerReplicationInfo.bOnlySpectator" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			SWITCH_LOG(LOG_LEVEL_WARN, "%s not found.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &toff.Movie_PCOwner, "FoxGame.FoxMovieBase.PCOwner" },
		{ &toff.BackTDM_Command_Cmd, "FoxGame.FoxMainMenuMovieBackTDM.OnReceiveFSCommand.Cmd" },
		{ &toff.LobbyTDM_Command_Cmd, "FoxGame.FoxMainMenuMovieGameLobbyTDM.OnReceiveFSCommand.Cmd" },
		{ &toff.IsGamePublic_ReturnValue, "FoxGame.FoxPC.IsGamePublic.ReturnValue" },
		{ &toff.WorldInfo_GRI, "Engine.WorldInfo.GRI" },
		{ &toff.GRI_PRIArray, "Engine.GameReplicationInfo.PRIArray" },
		{ &toff.GRI_RemainingTime, "Engine.GameReplicationInfo.RemainingTime" },
		{ &toff.GRI_ElapsedTime, "Engine.GameReplicationInfo.ElapsedTime" },
		{ &toff.WorldInfo_Game, "Engine.WorldInfo.Game" },
		{ &toff.Game_TimeLimit, "Engine.GameInfo.TimeLimit" },
		{ &toff.Controller_PRI, "Engine.Controller.PlayerReplicationInfo" },
		{ &toff.PRI_Team, "Engine.PlayerReplicationInfo.Team" },
		{ &toff.Team_TeamIndex, "Engine.TeamInfo.TeamIndex" },
		{ &toff.PC_PlayerInput, "Engine.PlayerController.PlayerInput" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			SWITCH_LOG(LOG_LEVEL_WARN, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	ready = true;
	return true;
}

static bool IsPublic(UObject* pc)
{
	bool saved = pretend_private;
	pretend_private = false;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.PC_IsGamePublic) + 16, 0);
	UE3::ProcessEvent(pc, ref.PC_IsGamePublic, parms.data());
	pretend_private = saved;
	return (Field<uint32_t>(parms.data(), toff.IsGamePublic_ReturnValue) & 1) != 0;
}

static const wchar_t* DeniedBecause(UObject* pc)
{
	UObject* world = Field<UObject*>(pc, off.Actor_WorldInfo);
	UObject* gri = world ? Field<UObject*>(world, toff.WorldInfo_GRI) : 0;
	UObject* own_pri = Field<UObject*>(pc, toff.Controller_PRI);
	UObject* own_team = own_pri ? Field<UObject*>(own_pri, toff.PRI_Team) : 0;
	if (!gri || !own_team) {
		return 0;
	}
	int32_t own_index = Field<int32_t>(own_team, toff.Team_TeamIndex);
	if (own_index < 0 || own_index > 1) {
		return 0;
	}
	bool begun = UE3::GetBoolProperty(gri, ref.GRI_bMatchHasBegun);
	if (begun && (UE3::IsA(gri, ref.GRI_LTS) || UE3::IsA(gri, ref.GRI_DET))) {
		return DENIED_ROUNDS;
	}
	int32_t humans[2] = {};
	int32_t bots_on_team[2] = {};
	TArray<UObject*>& pris = Field<TArray<UObject*> >(gri, toff.GRI_PRIArray);
	for (int32_t i = 0; i < pris.Num; i++) {
		UObject* pri = pris.Data[i];
		UObject* team = pri ? Field<UObject*>(pri, toff.PRI_Team) : 0;
		if (!team || UE3::GetBoolProperty(pri, ref.PRI_bOnlySpectator)) {
			continue;
		}
		int32_t index = Field<int32_t>(team, toff.Team_TeamIndex);
		if (index < 0 || index > 1) {
			continue;
		}
		(UE3::GetBoolProperty(pri, ref.PRI_bBot) ? bots_on_team : humans)[index]++;
	}
	if (humans[1 - own_index] > humans[own_index]) {
		return DENIED_UNEVEN;
	}
	if (humans[own_index] + bots_on_team[own_index] <= 1) {
		return DENIED_LAST;
	}
	// The game never sets GameReplicationInfo.TimeLimit.
	UObject* game = Field<UObject*>(world, toff.WorldInfo_Game);
	int32_t remaining = Field<int32_t>(gri, toff.GRI_RemainingTime);
	int32_t limit_seconds = game ? Field<int32_t>(game, toff.Game_TimeLimit) * 60 : Field<int32_t>(gri, toff.GRI_ElapsedTime) + remaining;
	if (begun && limit_seconds > 0 && remaining * 2 < limit_seconds) {
		return DENIED_TIME;
	}
	return 0;
}

static DWORD WaitLeft(UObject* pc)
{
	DWORD now = GetTickCount();
	for (const LastSwitch& entry : last_switches) {
		if (entry.Player == pc && now - entry.Tick < SWITCH_WAIT_MS) {
			return SWITCH_WAIT_MS - (now - entry.Tick);
		}
	}
	return 0;
}

static void RecordSwitch(UObject* pc)
{
	for (LastSwitch& entry : last_switches) {
		if (entry.Player == pc) {
			entry.Tick = GetTickCount();
			return;
		}
	}
	last_switches.push_back({ pc, GetTickCount() });
}

void OnLevelUnload()
{
	last_switches.clear();
}

static bool OnSwitchKey(UObject* movie, UObject* on_player_list)
{
	static DWORD last_press = 0;
	DWORD now = GetTickCount();
	if (now - last_press < PRESS_MS) {
		last_press = now;
		return false;
	}
	last_press = now;
	UObject* pc = Field<UObject*>(movie, toff.Movie_PCOwner);
	if (!pc || !IsPublic(pc) || UE3::GetBoolProperty(movie, on_player_list)) {
		return true;
	}
	const wchar_t* denied = DeniedBecause(pc);
	if (denied) {
		Menu::SetMovieText(movie, TOOLTIP_PATH, denied);
		return false;
	}
	DWORD wait = WaitLeft(pc);
	if (wait) {
		wchar_t text[64];
		swprintf_s(text, DENIED_WAIT, (wait + 999) / 1000);
		Menu::SetMovieText(movie, TOOLTIP_PATH, text);
		return false;
	}
	RecordSwitch(pc);
	pretend_private = true;
	return true;
}

bool OnScriptCall(UObject* self, FFrame* stack, void* result)
{
	if (!ready) {
		return true;
	}
	UObject* node = stack->Node;
	if (node == ref.PC_IsGamePublic) {
		if (pretend_private && result) {
			*(uint32_t*)result = 0;
			return false;
		}
		return true;
	}
	if (node == ref.BackTDM_SwitchTeams) {
		return OnSwitchKey(self, ref.BackTDM_bOnPlayerList);
	}
	if (node == ref.LobbyTDM_SwitchTeams) {
		return OnSwitchKey(self, ref.LobbyTDM_bOnPlayerList);
	}
	if (node == ref.BackTDM_Command || node == ref.LobbyTDM_Command) {
		const FString& command = Field<FString>(stack->Locals, node == ref.BackTDM_Command ? toff.BackTDM_Command_Cmd : toff.LobbyTDM_Command_Cmd);
		UObject* pc = Field<UObject*>(self, toff.Movie_PCOwner);
		if (command.Data && wcscmp(command.Data, TOOLTIP_COMMAND) == 0 && pc && IsPublic(pc)) {
			pretend_private = true;
		}
		return true;
	}
	if (node == ref.PC_ServerToggleTeam) {
		if (IsPublic(self)) {
			const wchar_t* denied = DeniedBecause(self);
			if (denied) {
				SWITCH_LOG(LOG_LEVEL_INFO, "The host refused a team switch and showed %ls.", denied);
				return false;
			}
			bool local = Field<UObject*>(self, toff.PC_PlayerInput) != 0;
			if (!local && WaitLeft(self)) {
				SWITCH_LOG(LOG_LEVEL_INFO, "%s", "The player switched less than a minute ago, so the host refused the team switch.");
				return false;
			}
			if (!local) {
				RecordSwitch(self);
			}
		}
		return true;
	}
	if (node == ref.FoxPawn_PlayerChangedTeam) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Pawn_PlayerChangedTeam) + 16, 0);
		UE3::ProcessEvent(self, ref.Pawn_PlayerChangedTeam, parms.data());
		return false;
	}
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack)
{
	UObject* node = stack->Node;
	if (pretend_private && (node == ref.BackTDM_SwitchTeams || node == ref.LobbyTDM_SwitchTeams || node == ref.BackTDM_Command || node == ref.LobbyTDM_Command)) {
		pretend_private = false;
	}
}

}
}
