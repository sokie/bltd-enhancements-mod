#include <winsock2.h>
#include <Windows.h>
#include "../loader/log.hpp"
#include "config.hpp"
#include "menu.hpp"
#include "platform.hpp"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CONFIG_LOG(level, format, ...) Log::Write(level, "[Bots] " format, __VA_ARGS__)

#ifndef BLTD_LOBBY_MIN_PLAYERS
#define BLTD_LOBBY_MIN_PLAYERS 4
#endif
#define CONFIG_TEXT(value) #value
#define CONFIG_NUMBER_TEXT(value) CONFIG_TEXT(value)

namespace Bots {

static const char* const DEFAULT_CONFIG =
	"[Versus]\r\n"
	"; Selecting FILL adds bots until the match has this many players, humans included. A human who joins takes a bot's place.\r\n"
	"DeathmatchPlayers=10\r\n"
	"; Players on each team when selecting FILL in team game modes\r\n"
	"TeamPlayers=6\r\n"
	"; 1 spawns players of team game modes next to the biggest group of their team, and Deathmatch players away from others. 0 keeps the game's spawn rules.\r\n"
	"SmartSpawns=1\r\n"
	"\r\n"
	"[Lobby]\r\n"
	"; Countdown for public lobbies. 0 keeps the game's 105.\r\n"
	"Countdown=40\r\n"
	"; Minimum players to start public lobbies. 0 keeps the game's 4. 1 lets the host start alone.\r\n"
	"MinPlayers=" CONFIG_NUMBER_TEXT(BLTD_LOBBY_MIN_PLAYERS) "\r\n"
	"\r\n"
	"[Server]\r\n"
	"; A dedicated server: BLTD.exe server <map>?game=<mode> -unattended -nosound.\r\n"
	"; Modes: FoxGame.FoxGameMP_DM, _TDM, _CP (Domination), _CTF (Retrieval), _LMS, _LTS.\r\n"
	"; The server's name in the game list.\r\n"
	"Name=BLTD Bots\r\n"
	"; The maps it plays in turn. A map can set its own mode: SkyFire?game=FoxGame.FoxGameMP_TDM.\r\n"
	"Maps=Curfew,SkyFire,ShadowMarket,Dropzone,Wharfare,Slumlord,Brutalism,Derailer,Floodgate,Pitfall,PostAlley,ZeroPoint\r\n"
	"; 1 plays the maps in a random order.\r\n"
	"Random=0\r\n"
	"\r\n"
	"[Net]\r\n"
	"; 1 makes games without the mod not able to join our lobbies. 0 lets every game join.\r\n"
	"VersionGate=1\r\n"
	"\r\n"
	"; Special in-game events\r\n"
	"[Events]\r\n"
	"; Halloween event: auto turns it on on the 31st of October, on keeps it on, off turns it off.\r\n"
	"Halloween=auto\r\n";

struct SlotNames {
	const char* Controller;
	const char* WeaponPreset;
	const char* Grenade;
	const char* Look;
	const char* Voice;
};

struct Squad {
	const char* Name;
	SlotNames Slots[SLOT_COUNT];
};

static const Squad SQUADS[] = {
	{ "Balanced", {
		{ "Assault", "WpnPreset_ASS_4", "None", "Recon", "B" },
		{ "Grenadier", "WpnPreset_SMG_6", "Flash", "ShieldBL04", "B" },
		{ "Assault", "WpnPreset_ASS_2", "None", "OperatorTactical", "A" },
	} },
	{ "CloseQuarters", {
		{ "Assault", "WpnPreset_SHO_5", "None", "OperatorTactical", "A" },
		{ "Grenadier", "WpnPreset_SMG_6", "Flash", "Recon", "B" },
		{ "Assault", "WpnPreset_SHO_2", "None", "ShieldBL04", "B" },
	} },
	{ "HeavyAssault", {
		{ "Grenadier", "WpnPreset_BKT_3", "Frag", "ShieldBL04", "A" },
		{ "Grenadier", "WpnPreset_ASS_7", "Flash", "OperatorTactical", "B" },
		{ "Assault", "WpnPreset_ASS_2", "None", "StandardIssue", "A" },
	} },
	{ "SpecOps", {
		{ "Assault", "WpnPreset_ASS_8", "None", "ReconTactical", "B" },
		{ "Grenadier", "WpnPreset_SMG_4", "Smoke", "Recon", "A" },
		{ "Sniper", "WpnPreset_SNI_1", "None", "ReconBL04", "B" },
	} },
};

static const char* const PRESET_PREFIX = "WpnPreset_";
// SpawnAIWave holds its preset as an inline string, so a replacement keeps its length.
static const size_t PRESET_LENGTH = 15;

struct NamedPath {
	const char* Name;
	const char* Path;
};

static const NamedPath CONTROLLERS[] = {
	{ "Assault", "FoxGame.FoxAIControllerInterface_Assault" },
	{ "Sniper", "FoxGame.FoxAIControllerInterface_Sniper" },
	{ "Grenadier", "FoxGameContent_BO.FoxAIControllerInterface_Grenadier" },
	{ "MeleeAttacker", "FoxGame.FoxAIControllerInterface_MeleeAttacker" },
};

static const NamedPath GRENADES[] = {
	{ "None", "" },
	{ "Frag", "FoxGameContent.FoxWeapon_FragGrenade" },
	{ "Flash", "FoxGameContent.FoxWeapon_FlashGrenade" },
	{ "Smoke", "FoxGameContent.FoxWeapon_SmokeGrenade" },
};

static const NamedPath LOOKS[] = {
	{ "StandardIssue", "FoxGameContent.FoxPawn_BlacklightA" },
	{ "Recon", "FoxGameContent.FoxPawn_BlacklightB" },
	{ "OperatorTactical", "FoxGameContent.FoxPawn_BlacklightC" },
	{ "ShieldBL04", "FoxGameContent.FoxPawn_BlacklightD" },
	{ "ReconTactical", "FoxGameContent.FoxPawn_BlacklightE" },
	{ "ReconBL04", "FoxGameContent.FoxPawn_BlacklightF" },
};

static const NamedPath VOICES[] = {
	{ "Armour", "" },
	{ "A", "Blacklight_A" },
	{ "B", "Blacklight_B" },
};

int SquadCount()
{
	return (int)(sizeof(SQUADS) / sizeof(SQUADS[0]));
}

const char* SquadName(int index)
{
	return index >= 0 && index < SquadCount() ? SQUADS[index].Name : SQUADS[0].Name;
}

static wchar_t* ConfigPath()
{
	static wchar_t path[MAX_PATH];
	std::wstring file = Platform::FilePath(Platform::FILE_INI, 0, true);
	if (file.empty() || file.size() >= MAX_PATH) {
		return 0;
	}
	wcscpy_s(path, MAX_PATH, file.c_str());
	return path;
}

static void ReadString(const wchar_t* path, const char* section, const char* key, const char* fallback, char* result, size_t result_size)
{
	char buffer[128];
	char pathA[MAX_PATH * 3];
	WideCharToMultiByte(CP_ACP, 0, path, -1, pathA, sizeof(pathA), 0, 0);
	GetPrivateProfileStringA(section, key, fallback, buffer, sizeof(buffer), pathA);
	strncpy_s(result, result_size, buffer, _TRUNCATE);
}

static float ReadFloatIn(const wchar_t* path, const char* section, const char* key, float fallback)
{
	char fallbackText[32];
	char value[32];
	sprintf_s(fallbackText, sizeof(fallbackText), "%g", fallback);
	ReadString(path, section, key, fallbackText, value, sizeof(value));
	return (float)atof(value);
}

static const char* PathForName(const NamedPath* table, size_t count, const char* name, const char* setting)
{
	for (size_t i = 0; i < count; i++) {
		if (_stricmp(table[i].Name, name) == 0) {
			return table[i].Path;
		}
	}
	CONFIG_LOG(LOG_LEVEL_WARN, "%s=%s is not known, %s is used.", setting, name, table[0].Name);
	return table[0].Path;
}

static void FillSlot(const SlotNames& names, SlotConfig& slot)
{
	strcpy_s(slot.Controller, sizeof(slot.Controller), PathForName(CONTROLLERS, sizeof(CONTROLLERS) / sizeof(CONTROLLERS[0]), names.Controller, "Controller"));
	strcpy_s(slot.WeaponPreset, sizeof(slot.WeaponPreset), names.WeaponPreset);
	strcpy_s(slot.Grenade, sizeof(slot.Grenade), PathForName(GRENADES, sizeof(GRENADES) / sizeof(GRENADES[0]), names.Grenade, "Grenade"));
	strcpy_s(slot.Look, sizeof(slot.Look), PathForName(LOOKS, sizeof(LOOKS) / sizeof(LOOKS[0]), names.Look, "Look"));
	strcpy_s(slot.LookName, sizeof(slot.LookName), names.Look);
	strcpy_s(slot.Voice, sizeof(slot.Voice), PathForName(VOICES, sizeof(VOICES) / sizeof(VOICES[0]), names.Voice, "Voice"));
	if (strlen(slot.WeaponPreset) != PRESET_LENGTH || strncmp(slot.WeaponPreset, PRESET_PREFIX, strlen(PRESET_PREFIX)) != 0) {
		CONFIG_LOG(LOG_LEVEL_WARN, "WeaponPreset=%s is not a %u character preset name, WpnPreset_ASS_0 is used.", slot.WeaponPreset, (uint32_t)PRESET_LENGTH);
		strcpy_s(slot.WeaponPreset, sizeof(slot.WeaponPreset), "WpnPreset_ASS_0");
	}
}

static const char* const DEFAULT_MAPS = "Curfew,SkyFire,ShadowMarket,Dropzone,Wharfare,Slumlord,Brutalism,Derailer,Floodgate,Pitfall,PostAlley,ZeroPoint";

void LoadServerConfig(ServerConfig& server)
{
	const wchar_t* path = ConfigPath();
	if (!path) {
		path = L"";
	}
	ReadString(path, "Server", "Name", "BLTD Bots", server.Name, sizeof(server.Name));
	char pathA[MAX_PATH * 3];
	WideCharToMultiByte(CP_ACP, 0, path, -1, pathA, sizeof(pathA), 0, 0);
	GetPrivateProfileStringA("Server", "Maps", DEFAULT_MAPS, server.Maps, sizeof(server.Maps), pathA);
	server.Random = ReadFloatIn(path, "Server", "Random", 0.0f) != 0.0f;
	server.EndDelay = 20;
}

void LoadNetConfig(NetConfig& net)
{
	const wchar_t* path = ConfigPath();
	if (!path) {
		path = L"";
	}
	net.VersionGate = ReadFloatIn(path, "Net", "VersionGate", 1.0f) != 0.0f;
}

void LoadLobbyConfig(LobbyConfig& lobby)
{
	const wchar_t* path = ConfigPath();
	if (!path) {
		path = L"";
	}
	lobby.Countdown = (std::max)(0, (int32_t)ReadFloatIn(path, "Lobby", "Countdown", 40.0f));
	lobby.MinPlayers = (std::max)(0, (int32_t)ReadFloatIn(path, "Lobby", "MinPlayers", (float)BLTD_LOBBY_MIN_PLAYERS));
}

void LoadEventsConfig(EventsConfig& events)
{
	const wchar_t* path = ConfigPath();
	if (!path) {
		path = L"";
	}
	char value[16];
	ReadString(path, "Events", "Halloween", "auto", value, sizeof(value));
	if (_stricmp(value, "on") == 0 || strcmp(value, "1") == 0) {
		events.Halloween = HALLOWEEN_ON;
	}
	else if (_stricmp(value, "off") == 0 || strcmp(value, "0") == 0) {
		events.Halloween = HALLOWEEN_OFF;
	}
	else {
		events.Halloween = HALLOWEEN_AUTO;
	}
}

void LoadHudConfig(HudConfig& hud)
{
	const wchar_t* path = ConfigPath();
	hud.CrosshairDynamic = !path || ReadFloatIn(path, "Hud", "CrosshairDynamic", 1.0f) != 0.0f;
	hud.CrosshairDimensions = path ? (int)ReadFloatIn(path, "Hud", "CrosshairDimensions", 0.0f) : 0;
	hud.CrosshairColor = path ? (int)ReadFloatIn(path, "Hud", "CrosshairColor", 0.0f) : 0;
}

void SaveHudConfig(const HudConfig& hud)
{
	const wchar_t* path = ConfigPath();
	if (path) {
		WritePrivateProfileStringW(L"Hud", L"CrosshairDynamic", hud.CrosshairDynamic ? L"1" : L"0", path);
		wchar_t number[16];
		swprintf_s(number, L"%d", hud.CrosshairDimensions);
		WritePrivateProfileStringW(L"Hud", L"CrosshairDimensions", number, path);
		swprintf_s(number, L"%d", hud.CrosshairColor);
		WritePrivateProfileStringW(L"Hud", L"CrosshairColor", number, path);
	}
}

void LoadConfig(Config& config)
{
	const wchar_t* path = ConfigPath();
	if (path && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
		FILE* file = 0;
		if (_wfopen_s(&file, path, L"wb") == 0 && file) {
			fwrite(DEFAULT_CONFIG, 1, strlen(DEFAULT_CONFIG), file);
			fclose(file);
		}
	}
	if (!path) {
		path = L"";
	}

	config.Enabled = true;
	config.Count = SLOT_COUNT;
	static const float DAMAGE_SCALE[DIFFICULTY_COUNT] = { 0.8f, 1.0f, 1.2f };
	static const int32_t HEALTH[DIFFICULTY_COUNT] = { 262, 394, 525 };
	memcpy(config.DamageScale, DAMAGE_SCALE, sizeof(config.DamageScale));
	memcpy(config.Health, HEALTH, sizeof(config.Health));
	config.HealthRegen = 0.5f;
	config.SightRadius = 3500.0f;
	config.HiddenTargetRange = 800.0f;
	config.FiringAngle = 0.0f;
	config.SquadBehavior = 1;
	config.FollowDistance = 350.0f;
	config.SpreadDistance = 220.0f;
	config.AheadDistance = 400.0f;
	config.PostRadius = 700.0f;
	config.SuppressedInCover = false;
	config.CombatLeash = 3000.0f;
	config.ForgetEnemySeconds = 6.0f;
	config.CoverAnyRange = true;
	config.CoverMaxDistance = 900.0f;
	config.CoverDistancePenalty = 0.5f;
	config.CoverPreference = 0.5f;
	config.CoverBanMs = 30000;
	config.TeleportDistance = 2500.0f;
	config.StuckMs = 8000;
	config.TeleportToCover = true;
	config.TeleportGraceMs = 8000;
	config.ArmourBonus = true;
	config.Trace = GetEnvironmentVariableA("BLTD_TRACE", 0, 0) > 0;

	const Squad* squad = &SQUADS[0];
	if (const char* chosen = Menu::ChosenSquad()) {
		for (const Squad& candidate : SQUADS) {
			if (strcmp(candidate.Name, chosen) == 0) {
				squad = &candidate;
			}
		}
	}
	strcpy_s(config.Squad, sizeof(config.Squad), squad->Name);
	for (int i = 0; i < SLOT_COUNT; i++) {
		FillSlot(squad->Slots[i], config.Slots[i]);
	}

	VersusConfig& versus = config.Versus;
	versus.Enabled = true;
	versus.DeathmatchPlayers = (int32_t)ReadFloatIn(path, "Versus", "DeathmatchPlayers", 10.0f);
	versus.TeamPlayers = (int32_t)ReadFloatIn(path, "Versus", "TeamPlayers", 6.0f);
	versus.SmartSpawns = ReadFloatIn(path, "Versus", "SmartSpawns", 1.0f) != 0.0f;
	versus.BotCount = -1;
	versus.Health = 175;
	versus.HealthRegen = 0.5f;
	versus.FocusSeconds = 1.5f;
	versus.SkillSpread = 30;
	versus.SeekChance = 60;
	versus.Aggression = 50;
	versus.AggressionSpread = 30;
	versus.CombatMoves = true;
	versus.ObjectiveChance = 70;
	versus.CoverUse = 85;
	versus.CoverPreference = 0.2f;
	versus.SpeedPercent = 0;
	versus.HackSeconds = 10.0f;

	CONFIG_LOG(LOG_LEVEL_INFO, "Config: squad %s, deathmatch players %d, team players %d, trace %d."
		, config.Squad, versus.DeathmatchPlayers, versus.TeamPlayers, config.Trace ? 1 : 0);
	LoadLobbyConfig(config.Lobby);
	LoadHudConfig(config.Hud);
	CONFIG_LOG(LOG_LEVEL_INFO, "Config: lobby countdown %d s, min players %d.", config.Lobby.Countdown, config.Lobby.MinPlayers);
	LoadServerConfig(config.Server);
	CONFIG_LOG(LOG_LEVEL_INFO, "Config: server name %s, maps %s, random %d."
		, config.Server.Name, config.Server.Maps, config.Server.Random ? 1 : 0);
}

}
