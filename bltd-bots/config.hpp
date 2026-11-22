#pragma once
#include <stdint.h>

namespace Bots {

static const int SLOT_COUNT = 3;
static const int DIFFICULTY_COUNT = 3;

struct SlotConfig {
	char Controller[64];
	char WeaponPreset[32];
	char Grenade[64];
	char Look[64];
	char LookName[32];
	char Voice[32];
};

struct VersusConfig {
	bool Enabled;
	int32_t DeathmatchPlayers;
	int32_t TeamPlayers;
	bool SmartSpawns;
	int32_t Health;
	float HealthRegen;
	float DamageScale;
	float FiringAngle;
	int32_t SeekChance;
	int32_t Aggression;
	int32_t AggressionSpread;
	bool CombatMoves;
	int32_t ObjectiveChance;
	int32_t CoverUse;
	float CoverPreference;
	int32_t SpeedPercent;
	float HackSeconds;
	int32_t Skill;
	int32_t SkillSpread;
	int32_t ReactionMs;
	int32_t WarningShots;
	float FocusAngle;
	float FocusSeconds;
	int32_t BotCount;
};

struct LobbyConfig {
	int32_t Countdown;
	int32_t MinPlayers;
};

struct ServerConfig {
	char Name[16];
	char Maps[1024];
	bool Random;
	int32_t EndDelay;
};

struct NetConfig {
	bool VersionGate;
};

static const int HALLOWEEN_OFF = 0;
static const int HALLOWEEN_ON = 1;
static const int HALLOWEEN_AUTO = 2;

struct EventsConfig {
	int Halloween;
};

struct HudConfig {
	bool CrosshairDynamic;
	int CrosshairDimensions;
	int CrosshairColor;
};

struct Config {
	bool Enabled;
	int32_t Count;
	float DamageScale[DIFFICULTY_COUNT];
	int32_t Health[DIFFICULTY_COUNT];
	float HealthRegen;
	float SightRadius;
	float HiddenTargetRange;
	float FiringAngle;
	uint8_t SquadBehavior;
	float FollowDistance;
	float SpreadDistance;
	float AheadDistance;
	float PostRadius;
	bool SuppressedInCover;
	float CombatLeash;
	float ForgetEnemySeconds;
	bool CoverAnyRange;
	float CoverMaxDistance;
	float CoverDistancePenalty;
	float CoverPreference;
	uint32_t CoverBanMs;
	bool TeleportToCover;
	uint32_t TeleportGraceMs;
	float TeleportDistance;
	uint32_t StuckMs;
	char Squad[32];
	bool ArmourBonus;
	bool Trace;
	SlotConfig Slots[SLOT_COUNT];
	VersusConfig Versus;
	LobbyConfig Lobby;
	ServerConfig Server;
	HudConfig Hud;
};

void LoadConfig(Config& config);
void LoadServerConfig(ServerConfig& server);
void LoadNetConfig(NetConfig& net);
void LoadLobbyConfig(LobbyConfig& lobby);
void LoadEventsConfig(EventsConfig& events);
void LoadHudConfig(HudConfig& hud);
void SaveHudConfig(const HudConfig& hud);
int SquadCount();
const char* SquadName(int index);

}
