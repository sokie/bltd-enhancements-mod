#include "bots-internal.hpp"
#include "lobby.hpp"

#define LOBBY_LOG(level, format, ...) Log::Write(level, "[Lobby] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::Field;

namespace Bots {
namespace Lobby {

static const uint8_t NM_LISTEN_SERVER = 2;

static UObject* start_find_players = 0;
static UObject* check_end_game = 0;
static UObject* is_game_public = 0;

static struct {
	int32_t IsGamePublic_ReturnValue;
	int32_t Movie_PCOwner;
	int32_t WorldInfo_NetMode;
	int32_t Intermission_PlayerSearchTime;
	int32_t Intermission_FullGameWaitTime;
	int32_t Intermission_MinRequiredPlayersToStart;
} loff;

static bool ready = false;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &start_find_players, "FoxGame.FoxMainMenuMovieIntermission.StartFindPlayers" },
		{ &check_end_game, "FoxGame.FoxGRI.CheckEndGame" },
		{ &is_game_public, "FoxGame.FoxGame.IsGamePublic" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			LOBBY_LOG(LOG_LEVEL_WARN, "%s not found.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &loff.IsGamePublic_ReturnValue, "FoxGame.FoxGame.IsGamePublic.ReturnValue" },
		{ &loff.Movie_PCOwner, "FoxGame.FoxMovieBase.PCOwner" },
		{ &loff.WorldInfo_NetMode, "Engine.WorldInfo.NetMode" },
		{ &loff.Intermission_PlayerSearchTime, "FoxGame.FoxMainMenuMovieIntermission.PlayerSearchTime" },
		{ &loff.Intermission_FullGameWaitTime, "FoxGame.FoxMainMenuMovieIntermission.FullGameWaitTime" },
		{ &loff.Intermission_MinRequiredPlayersToStart, "FoxGame.FoxMainMenuMovieIntermission.MinRequiredPlayersToStart" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			LOBBY_LOG(LOG_LEVEL_WARN, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	ready = true;
	return true;
}

bool IsPublic(UObject* game)
{
	if (!ready || !game) {
		return false;
	}
	std::vector<uint8_t> parms;
	CallFunction(game, is_game_public, parms);
	return (Field<uint32_t>(parms.data(), loff.IsGamePublic_ReturnValue) & 1) != 0;
}

bool KeepsPublicMatch()
{
	return ready;
}

void OnScriptCall(UObject* self, FFrame* stack)
{
	if (!ready || stack->Node != start_find_players) {
		return;
	}
	LoadLobbyConfig(config.Lobby);
	if (config.Lobby.MinPlayers > 0) {
		Field<int32_t>(self, loff.Intermission_MinRequiredPlayersToStart) = config.Lobby.MinPlayers;
	}
	UObject* pc = Field<UObject*>(self, loff.Movie_PCOwner);
	UObject* world = pc ? Field<UObject*>(pc, off.Actor_WorldInfo) : 0;
	if (!world || Field<uint8_t>(world, loff.WorldInfo_NetMode) != NM_LISTEN_SERVER) {
		return;
	}
	float wait = Field<float>(self, loff.Intermission_FullGameWaitTime);
	if (config.Lobby.Countdown > 0) {
		float search = (float)config.Lobby.Countdown - wait;
		Field<float>(self, loff.Intermission_PlayerSearchTime) = search > 0.0f ? search : 0.0f;
	}
	LOBBY_LOG(LOG_LEVEL_INFO, "Public lobby: the countdown is %.0f s and the match needs %d player(s)."
		, Field<float>(self, loff.Intermission_PlayerSearchTime) + wait, Field<int32_t>(self, loff.Intermission_MinRequiredPlayersToStart));
}

void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!ready || stack->Node != check_end_game || !result || bots.empty() || !(*(uint32_t*)result & 1)) {
		return;
	}
	*(uint32_t*)result = 0;
	LOBBY_LOG(LOG_LEVEL_INFO, "%s", "A player left and the game would end the public match, the bots keep it going.");
}

}
}
