#include "bots-internal.hpp"
#include "server.hpp"
#include "versus.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define SERVER_LOG(level, format, ...) Log::Write(level, "[Server] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::Field;

namespace Bots {
namespace Server {

static const uint32_t XLLN_LOGIN_ORDINAL = 41140;
typedef uint32_t (WINAPI *tXLLNLogin)(uint32_t user_index, BOOL live_enabled, uint32_t user_id, const char* username);
static const uint8_t NM_DEDICATED_SERVER = 1;
static const DWORD WAIT_LOG_MS = 10000;
static const char* const RESTART_URL = "?Restart";

static struct {
	UObject* MP_Timer;
	UObject* MP_StartMatch;
	UObject* GRI_StartMatch;
	UObject* GRI_CheckEndGame;
	UObject* World_ServerTravel;
	UObject* World_GetMapName;
	UObject* Pawn_Tick;
	UObject* DelayedStartProperty;
	UObject* GameEndedProperty;
	UObject* ReadyToStartProperty;
	UObject* MatchHasBegunProperty;
	UObject* MP_InitGame;
	UObject* SettingsCommon;
	UObject* SetIntProperty;
	UObject* SetGameVersion;
	UObject* FoxGameDefault;
} ref;

static struct {
	int32_t WorldInfo_NetMode;
	int32_t WorldInfo_TimeSeconds;
	int32_t WorldInfo_GRI;
	int32_t ServerTravel_URL;
	int32_t GetMapName_ReturnValue;
	int32_t SetInt_PropertyId;
	int32_t SetInt_Value;
	int32_t SetGameVersion_NewGameVersion;
	int32_t FoxGame_GameVersion;
} soff;

static const int32_t PROPERTY_GAME_STATUS = 0x10000076;
static const int32_t PROPERTY_PUBLIC_GAME = 0x10000078;

static bool dedicated = false;
static bool ready = false;
static UObject* server_game = 0;
static bool started = false;
static bool travelled = false;
static DWORD end_tick = 0;
static DWORD wait_log_tick = 0;
static float frame_time = -1.0f;
static bool frame_logged = false;

static bool CommandLineHasServerToken()
{
	const wchar_t* p = GetCommandLineW();
	if (*p == L'"') {
		for (p++; *p && *p != L'"'; p++) {
		}
		if (*p) {
			p++;
		}
	}
	else {
		for (; *p && *p != L' ' && *p != L'\t'; p++) {
		}
	}
	for (; *p == L' ' || *p == L'\t'; p++) {
	}
	return _wcsnicmp(p, L"server", 6) == 0 && (p[6] == 0 || p[6] == L' ' || p[6] == L'\t');
}

bool IsDedicatedWorld(UObject* actor)
{
	if (!ready || !actor) {
		return false;
	}
	UObject* world = Field<UObject*>(actor, off.Actor_WorldInfo);
	return world && Field<uint8_t>(world, soff.WorldInfo_NetMode) == NM_DEDICATED_SERVER;
}

void Install()
{
	dedicated = CommandLineHasServerToken();
	if (!dedicated) {
		return;
	}
	LoadServerConfig(config.Server);
	HMODULE xlive = GetModuleHandleW(L"xlive.dll");
	tXLLNLogin login = xlive ? (tXLLNLogin)GetProcAddress(xlive, (LPCSTR)XLLN_LOGIN_ORDINAL) : 0;
	if (!login) {
		SERVER_LOG(LOG_LEVEL_ERROR, "%s", "xlive.dll has no XLLNLogin, the server stays signed out and cannot create its session.");
		return;
	}
	uint32_t result = login(0, TRUE, 0, config.Server.Name);
	if (result) {
		SERVER_LOG(LOG_LEVEL_ERROR, "XLLNLogin as %s failed with error 0x%08X.", config.Server.Name, result);
		return;
	}
	SERVER_LOG(LOG_LEVEL_INFO, "Dedicated server signed in as %s.", config.Server.Name);
}

static void SetSettingsProperty(UObject* settings, int32_t property_id, int32_t value)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.SetIntProperty) + 16, 0);
	Field<int32_t>(parms.data(), soff.SetInt_PropertyId) = property_id;
	Field<int32_t>(parms.data(), soff.SetInt_Value) = value;
	UE3::ProcessEvent(settings, ref.SetIntProperty, parms.data());
}

// Find Game lists only a session with GameStatus 1 and the game's version. A lobby host sets both.
static void AdvertiseAsLobby()
{
	if (!ref.SettingsCommon) {
		return;
	}
	float version = Field<float>(ref.FoxGameDefault, soff.FoxGame_GameVersion);
	int32_t count = UE3::ObjectCount();
	int32_t changed = 0;
	for (int32_t i = 0; i < count; i++) {
		UObject* object = UE3::ObjectAt(i);
		if (!object || !UE3::IsA(object, ref.SettingsCommon)) {
			continue;
		}
		SetSettingsProperty(object, PROPERTY_GAME_STATUS, 1);
		SetSettingsProperty(object, PROPERTY_PUBLIC_GAME, 1);
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.SetGameVersion) + 16, 0);
		Field<float>(parms.data(), soff.SetGameVersion_NewGameVersion) = version;
		UE3::ProcessEvent(object, ref.SetGameVersion, parms.data());
		changed++;
	}
	SERVER_LOG(LOG_LEVEL_INFO, "%d game settings objects advertise an open public lobby of game version %.2f.", changed, version);
}

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.MP_Timer, "FoxGame.FoxGameMP.Timer" },
		{ &ref.MP_StartMatch, "FoxGame.FoxGameMP.StartMatch" },
		{ &ref.GRI_StartMatch, "Engine.GameReplicationInfo.StartMatch" },
		{ &ref.GRI_CheckEndGame, "FoxGame.FoxGRI.CheckEndGame" },
		{ &ref.World_ServerTravel, "Engine.WorldInfo.ServerTravel" },
		{ &ref.World_GetMapName, "Engine.WorldInfo.GetMapName" },
		{ &ref.Pawn_Tick, "FoxGame.FoxPawn.Tick" },
		{ &ref.DelayedStartProperty, "Engine.GameInfo.bDelayedStart" },
		{ &ref.GameEndedProperty, "Engine.GameInfo.bGameEnded" },
		{ &ref.ReadyToStartProperty, "FoxGame.FoxGameMP.bReadyToStart" },
		{ &ref.MatchHasBegunProperty, "Engine.GameReplicationInfo.bMatchHasBegun" },
		{ &ref.MP_InitGame, "FoxGame.FoxGameMP.InitGame" },
	};
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &soff.WorldInfo_NetMode, "Engine.WorldInfo.NetMode" },
		{ &soff.WorldInfo_TimeSeconds, "Engine.WorldInfo.TimeSeconds" },
		{ &soff.WorldInfo_GRI, "Engine.WorldInfo.GRI" },
		{ &soff.ServerTravel_URL, "Engine.WorldInfo.ServerTravel.URL" },
		{ &soff.GetMapName_ReturnValue, "Engine.WorldInfo.GetMapName.ReturnValue" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			SERVER_LOG(LOG_LEVEL_ERROR, "%s not found.", objectRef.Path);
			return false;
		}
	}
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			SERVER_LOG(LOG_LEVEL_ERROR, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	ref.SettingsCommon = UE3::FindObject("FoxGame.FoxGameSettingsCommon");
	ref.SetIntProperty = UE3::FindObject("Engine.Settings.SetIntProperty");
	soff.SetInt_PropertyId = UE3::FindPropertyOffset("Engine.Settings.SetIntProperty.PropertyId");
	soff.SetInt_Value = UE3::FindPropertyOffset("Engine.Settings.SetIntProperty.Value");
	ref.SetGameVersion = UE3::FindObject("FoxGame.FoxGameSettingsCommon.SetGameVersion");
	ref.FoxGameDefault = UE3::FindObject("FoxGame.Default__FoxGame");
	soff.SetGameVersion_NewGameVersion = UE3::FindPropertyOffset("FoxGame.FoxGameSettingsCommon.SetGameVersion.NewGameVersion");
	soff.FoxGame_GameVersion = UE3::FindPropertyOffset("FoxGame.FoxGame.GameVersion");
	if (!ref.SettingsCommon || !ref.SetIntProperty || soff.SetInt_PropertyId < 0 || soff.SetInt_Value < 0
		|| !ref.SetGameVersion || !ref.FoxGameDefault || soff.SetGameVersion_NewGameVersion < 0 || soff.FoxGame_GameVersion < 0) {
		ref.SettingsCommon = 0;
		SERVER_LOG(LOG_LEVEL_WARN, "%s", "The game settings setter was not found, Find Game does not list this server.");
	}
	ready = true;
	if (dedicated) {
		AdvertiseAsLobby();
	}
	return true;
}

static std::string MapName(UObject* world)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.World_GetMapName) + 16, 0);
	UE3::ProcessNativeEvent(world, ref.World_GetMapName, parms.data());
	UE3::FString& value = Field<UE3::FString>(parms.data(), soff.GetMapName_ReturnValue);
	std::string name;
	if (value.Data) {
		for (const wchar_t* c = value.Data; *c; c++) {
			name.push_back((char)*c);
		}
		UE3::FreeArray(&value);
	}
	return name;
}

static std::vector<std::string> Pool()
{
	std::vector<std::string> maps;
	const char* p = config.Server.Maps;
	while (*p) {
		while (*p == ' ' || *p == ',') {
			p++;
		}
		const char* start = p;
		while (*p && *p != ',') {
			p++;
		}
		const char* end = p;
		while (end > start && end[-1] == ' ') {
			end--;
		}
		if (end > start) {
			maps.push_back(std::string(start, end));
		}
	}
	return maps;
}

static bool SameMap(const std::string& entry, const std::string& map)
{
	size_t length = entry.find('?');
	if (length == std::string::npos) {
		length = entry.size();
	}
	return length == map.size() && _strnicmp(entry.c_str(), map.c_str(), length) == 0;
}

static std::string NextMap(const std::string& current)
{
	std::vector<std::string> maps = Pool();
	if (maps.empty()) {
		return "";
	}
	int index = -1;
	for (size_t i = 0; i < maps.size(); i++) {
		if (SameMap(maps[i], current)) {
			index = (int)i;
			break;
		}
	}
	size_t next = (size_t)(index + 1) % maps.size();
	if (config.Server.Random && maps.size() > 1) {
		do {
			next = (size_t)rand() % maps.size();
		} while ((int)next == index);
	}
	return maps[next];
}

static void ServerTravel(UObject* world, const std::string& url)
{
	std::vector<wchar_t> text(url.size() + 1, 0);
	for (size_t i = 0; i < url.size(); i++) {
		text[i] = (wchar_t)(unsigned char)url[i];
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.World_ServerTravel) + 16, 0);
	UE3::FString& parm = Field<UE3::FString>(parms.data(), soff.ServerTravel_URL);
	parm.Data = text.data();
	parm.Num = parm.Max = (int32_t)text.size();
	UE3::ProcessEvent(world, ref.World_ServerTravel, parms.data());
}

static void Tick(UObject* game)
{
	if (game != server_game) {
		server_game = game;
		started = false;
		travelled = false;
		end_tick = 0;
		wait_log_tick = 0;
	}
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	UObject* gri = world ? Field<UObject*>(world, soff.WorldInfo_GRI) : 0;
	if (!gri) {
		return;
	}
	DWORD now = GetTickCount();
	bool ended = UE3::GetBoolProperty(game, ref.GameEndedProperty);
	if (!started && !ended && !UE3::GetBoolProperty(gri, ref.MatchHasBegunProperty)) {
		if (!UE3::GetBoolProperty(game, ref.ReadyToStartProperty)) {
			if (!wait_log_tick || now - wait_log_tick >= WAIT_LOG_MS) {
				wait_log_tick = now;
				SERVER_LOG(LOG_LEVEL_INFO, "%s", "Waiting for the level to be ready.");
			}
			return;
		}
		started = true;
		UE3::SetBoolProperty(game, ref.DelayedStartProperty, false);
		std::vector<uint8_t> startParms(UE3::FunctionParmsSize(ref.MP_StartMatch) + 16, 0);
		UE3::ProcessEvent(game, ref.MP_StartMatch, startParms.data());
		std::vector<uint8_t> griParms(UE3::FunctionParmsSize(ref.GRI_StartMatch) + 16, 0);
		UE3::ProcessEvent(gri, ref.GRI_StartMatch, griParms.data());
		SERVER_LOG(LOG_LEVEL_INFO, "The match on %s started.", MapName(world).c_str());
		return;
	}
	started = true;
	if (!ended) {
		return;
	}
	if (!end_tick) {
		end_tick = now | 1;
		SERVER_LOG(LOG_LEVEL_INFO, "The match ended, the next map loads in %d s.", config.Server.EndDelay);
		return;
	}
	if (travelled || now - end_tick < (DWORD)config.Server.EndDelay * 1000) {
		return;
	}
	travelled = true;
	std::string next = NextMap(MapName(world));
	if (next.empty()) {
		SERVER_LOG(LOG_LEVEL_WARN, "%s", "The map pool is empty, the map restarts.");
		next = RESTART_URL;
	}
	else {
		SERVER_LOG(LOG_LEVEL_INFO, "Next map: %s.", next.c_str());
	}
	ServerTravel(world, next);
}

bool OnScriptCall(UObject* self, FFrame* stack, void* result)
{
	if (!dedicated || !ready) {
		return true;
	}
	UObject* node = stack->Node;
	if (node == ref.MP_InitGame) {
		AdvertiseAsLobby();
		return true;
	}
	if (node == ref.Pawn_Tick) {
		UObject* world = Field<UObject*>(self, off.Actor_WorldInfo);
		float time = world ? Field<float>(world, soff.WorldInfo_TimeSeconds) : frame_time;
		if (time != frame_time) {
			frame_time = time;
			if (!frame_logged) {
				frame_logged = true;
				SERVER_LOG(LOG_LEVEL_INFO, "%s", "The pawns' Tick drives the per-frame checks.");
			}
			Versus::OnFrame();
		}
		return true;
	}
	if (node == ref.GRI_CheckEndGame && IsDedicatedWorld(self)) {
		if (result) {
			*(uint32_t*)result = 0;
		}
		return false;
	}
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!dedicated || !ready) {
		return;
	}
	if (stack->Node == ref.MP_Timer && IsDedicatedWorld(self)) {
		Tick(self);
	}
}

}
}
