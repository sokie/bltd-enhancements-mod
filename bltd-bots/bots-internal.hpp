#pragma once
#include <winsock2.h>
#include <Windows.h>
#include "../loader/log.hpp"
#include "config.hpp"
#include "navmesh.hpp"
#include "ue3.hpp"
#include <vector>

#define BOTS_LOG(level, format, ...) Log::Write(level, "[Bots] " format, __VA_ARGS__)

namespace Bots {

enum Mode {
	MODE_NONE,
	MODE_BLACK_OPS,
	MODE_DEATHMATCH,
	MODE_TEAM_DEATHMATCH,
	MODE_CONTROL_POINT,
	MODE_LAST_MAN,
	MODE_LAST_TEAM,
	MODE_RETRIEVAL,
};

enum FlagTask {
	FLAG_TASK_NONE,
	FLAG_TASK_CARRY_HOME,
	FLAG_TASK_CHASE_CARRIER,
	FLAG_TASK_RETURN,
	FLAG_TASK_ESCORT,
	FLAG_TASK_ATTACK,
	FLAG_TASK_GUARD,
};

static const uint8_t ORDER_TEAM = 0;
static const uint8_t BLACKLIGHT_TEAM = 1;
static const int32_t BEHAVIOR_INFO_BEHAVIOR = 0x08;
static const int32_t BEHAVIOR_INFO_SIZE = 0x0C;
static const int32_t COVER_SLOT_SIZE = 0xA8;
static const int32_t COVER_SLOT_TYPE = 0x09;
static const int32_t COVER_SLOT_FLAGS = 0x90;
static const uint32_t SLOT_LEAN_LEFT = 0x1;
static const uint32_t SLOT_LEAN_RIGHT = 0x2;
static const uint32_t SLOT_CAN_POPUP = 0x8;
static const uint8_t COVER_TYPE_MID = 2;
static const float STUCK_MOVE = 100.0f;
static const float ON_NAVMESH_DISTANCE = 100.0f;
static const float NAVMESH_NUDGE_RADIUS = 800.0f;
static const float FREE_SPOT_RADIUS = 150.0f;

struct CoverBan {
	UE3::UObject* Link;
	int32_t Slot;
	DWORD UntilTick;
};

struct GoalBan {
	UE3::FVector Goal;
	DWORD UntilTick;
};

struct Bot {
	UE3::UObject* Controller;
	DWORD LastCommandTick;
	UE3::FVector LastMoveLocation;
	DWORD LastMoveTick;
	int Slot;
	DWORD GraceUntilTick;
	uint32_t PathFailures;
	UE3::FVector PathFailGoal;
	uint32_t ZeroMoves;
	DWORD LastPathLogTick;
	std::vector<CoverBan> CoverBans;
	UE3::UObject* TargetHuman;
	UE3::UObject* TargetPawn;
	uint32_t FollowStalls;
	uint32_t FollowPathFailures;
	uint32_t LeashCalls;
	DWORD NextStillCheckTick;
	int32_t ArmourHealthPercent;
	uint8_t Team;
	DWORD DiedTick;
	DWORD SpawnDelayMs;
	bool HasGoal;
	UE3::FVector Goal;
	DWORD GoalTick;
	UE3::FVector LastHunt;
	std::vector<GoalBan> GoalBans;
	UE3::FVector AttackedFrom;
	DWORD AttackedTick;
	UE3::FVector NoiseFrom;
	DWORD NoiseTick;
	uint32_t Unsnags;
	DWORD LastUnsnagTick;
	DWORD LastWallStepTick;
	DWORD WallStepWindowTick;
	uint32_t WallSteps;
	DWORD LastMeshCheckTick;
	DWORD LastAnchorCheckTick;
	DWORD LastSnapTick;
	UE3::FVector LastSnapLocation;
	DWORD StartRetryTick;
	UE3::FVector StartRetryLocation;
	uint32_t StalledGoals;
	DWORD LastNudgeTick;
	UE3::FVector LastNudgeLocation;
	DWORD LastJumpTick;
	DWORD LastLinkLogTick;
	int32_t LinkZone;
	float JumpStartZ;
	float JumpSpeedZ;
	UE3::FVector JumpVelocity;
	DWORD JumpUntilTick;
	UE3::FVector FailSpot;
	uint32_t FailsHere;
	uint32_t ObjectiveStalls;
	bool StallResume;
	int32_t Aggression;
	UE3::UObject* GoToAction;
	bool WasFighting;
	DWORD CoverUnseenTick;
	DWORD NextCombatMoveTick;
	DWORD CombatMoveEndTick;
	DWORD HoldUntilTick;
	bool WaitsOnArrival;
	UE3::UObject* Objective;
	bool Defending;
	bool HasPost;
	DWORD HackStartTick;
	DWORD HackMs;
	FlagTask Flag;
	UE3::UObject* FlagActor;
	bool UsesCover;
	int32_t Skill;
	UE3::UObject* SightTarget;
	DWORD SightTick;
	DWORD SightLostTick;
	DWORD FireFromTick;
	int32_t WarningShotsLeft;
	bool MissShot;
	DWORD LastThinkTick;
};

struct Functions {
	UE3::UObject* BO_StartMatch;
	UE3::UObject* BO_Timer;
	UE3::UObject* BO_GetForcedTeam;
	UE3::UObject* DM_GetForcedTeam;
	UE3::UObject* TDM_GetForcedTeam;
	UE3::UObject* BO_CheckpointReached;
	UE3::UObject* MP_ChoosePlayerStart;
	UE3::UObject* MP_StartMatch;
	UE3::UObject* Team_AddToSquad;
	UE3::UObject* Checkpoint_Touch;
	UE3::UObject* Game_ReduceDamage;
	UE3::UObject* FoxPawn_TakeDamage;
	UE3::UObject* Controller_Destroyed;
	UE3::UObject* AI_Possess;
	UE3::UObject* AI_PawnDied;
	UE3::UObject* AI_ProcessBehaviorEvent;
	UE3::UObject* PC_SpawnAIWave;
	UE3::UObject* Pawn_Suicide;
	UE3::UObject* Pawn_EncroachedBy;
	UE3::UObject* AI_AIErrorThresholdReached;
	UE3::UObject* BO_Killed;
	UE3::UObject* CoverLink_GetSlotLocation;
	UE3::UObject* CoverLink_IsValidClaim;
	UE3::UObject* Handle_GetValidPositionsForBox;
	UE3::UObject* Handle_FindPylon;
	UE3::UObject* Actor_Spawn;
	UE3::UObject* Actor_Destroy;
	UE3::UObject* Actor_SetLocation;
	UE3::UObject* Game_AddDefaultInventory;
	UE3::UObject* FoxPawn_AddInventory;
	UE3::UObject* Squad_SetSquadBehavior;
	UE3::UObject* Actor_FastTrace;
	UE3::UObject* Pawn_StopFire;
	UE3::UObject* PC_PlayerTick;
	UE3::UObject* Game_GameEnding;
};

struct Classes {
	UE3::UObject* FoxGameMP_BO;
	UE3::UObject* FoxGameMP_TDM;
	UE3::UObject* FoxGameMP_DM;
	UE3::UObject* FoxGameMP_CP;
	UE3::UObject* FoxGameMP_LMS;
	UE3::UObject* FoxGameMP_LTS;
	UE3::UObject* FoxGameMP_CTF;
	UE3::UObject* FoxPC;
	UE3::UObject* Pawn;
	UE3::UObject* AssaultController;
	UE3::UObject* CoverEnabledProperty;
	UE3::UObject* EnemyRangeCondition;
	UE3::UObject* FollowAction;
	UE3::UObject* TraceBulletProperty;
	UE3::UObject* FastTraceResultProperty;
	UE3::UObject* GoToActionClass;
	UE3::UObject* CanAbortProperty;
	UE3::UObject* RunToDestinationProperty;
	UE3::UObject* FightFromCoverClass;
	UE3::UObject* FightMeleeClass;
	UE3::UObject* FireWeaponClass;
	UE3::UObject* StopFiringIfSuppressedProperty;
};

struct Offsets {
	int32_t Actor_Location;
	int32_t Actor_WorldInfo;
	int32_t Controller_Pawn;
	int32_t Controller_NextController;
	int32_t WorldInfo_ControllerList;
	int32_t Pawn_Controller;
	int32_t Pawn_Health;
	int32_t GameInfo_MaxPlayers;
	int32_t GameInfo_GameDifficulty;
	int32_t BO_CurrentAIDamageMod;
	int32_t BO_PreviousCheckpoints;
	int32_t GetForcedTeam_Other;
	int32_t DM_GetForcedTeam_Other;
	int32_t TDM_GetForcedTeam_Other;
	int32_t ChoosePlayerStart_InTeam;
	int32_t AddToSquad_Other;
	int32_t AddToSquad_SquadIndex;
	int32_t Touch_Other;
	int32_t CheckpointReached_LivePawns;
	int32_t CheckpointReached_Checkpoint;
	int32_t ReduceDamage_InstigatedBy;
	int32_t TakeDamage_Damage;
	int32_t TakeDamage_InstigatedBy;
	int32_t Possess_inPawn;
	int32_t ProcessBehaviorEvent_EventName;
	int32_t ProcessBehaviorEvent_EventInstigator;
	int32_t ProcessBehaviorEvent_EventRecipient;
	int32_t ProcessBehaviorEvent_EventLocation;
	int32_t SpawnAIWave_Number;
	int32_t EncroachedBy_Other;
	int32_t WorldInfo_PawnList;
	int32_t Pawn_NextPawn;
	int32_t AI_CurrentBehavior;
	int32_t Game_Teams;
	int32_t Team_Squads;
	int32_t Squad_SquadLeader;
	int32_t Squad_SquadMembers;
	int32_t Actor_Rotation;
	int32_t Spawn_SpawnClass;
	int32_t Spawn_SpawnLocation;
	int32_t Spawn_SpawnRotation;
	int32_t Spawn_ReturnValue;
	int32_t SetLocation_NewLocation;
	int32_t SetLocation_ReturnValue;
	int32_t AddDefaultInventory_P;
	int32_t Pawn_HealthMax;
	int32_t Pawn_SightRadius;
	int32_t FoxPawn_HealthRechargeMaxPercent;
	int32_t AI_Accuracy;
	int32_t AI_AggressionRange;
	int32_t AddInventory_InvClass;
	int32_t SetSquadBehavior_NewSquadBehavior;
	int32_t AI_DefaultBehavior;
	int32_t AI_ScriptedBehavior;
	int32_t AI_CoverConditions;
	int32_t Squad_EnemyNotVisibleRemoveTime;
	int32_t AI_CurrentCover;
	int32_t FoxPawn_CurrentLink;
	int32_t FoxPawn_CoverType;
	int32_t AI_AIErrorLevel;
	int32_t Actor_Physics;
	int32_t Killed_Killer;
	int32_t Killed_KilledPlayer;
	int32_t Killed_DamageType;
	int32_t WorldInfo_CoverList;
	int32_t CoverLink_NextCoverLink;
	int32_t CoverLink_Slots;
	int32_t GetSlotLocation_SlotIdx;
	int32_t GetSlotLocation_ReturnValue;
	int32_t IsValidClaim_ChkClaim;
	int32_t IsValidClaim_SlotIdx;
	int32_t IsValidClaim_ReturnValue;
	int32_t Controller_NavigationHandle;
	int32_t ValidPositions_Pos;
	int32_t ValidPositions_Radius;
	int32_t ValidPositions_Extent;
	int32_t ValidPositions_Out;
	int32_t Ability_Owner;
	int32_t Ability_SubActions;
	int32_t FollowAction_FollowingPawn;
	int32_t FastTrace_TraceEnd;
	int32_t FastTrace_TraceStart;
	int32_t AI_LastDestination;
	int32_t AI_Behaviors;
	int32_t AI_EvaluationInterval;
	int32_t FireWeapon_SuppressionConditions;
	int32_t Pawn_EyeHeight;
	int32_t StopFire_FireModeNum;
	int32_t Actor_Velocity;
};

struct LookRefs {
	UE3::UObject* DynamicLoadObject;
	UE3::UObject* ClassClass;
	int32_t Pawn_Mesh;
	int32_t SkeletalMeshComponent_SkeletalMesh;
	int32_t FoxPawn_DialogPawnName;
	int32_t DynamicLoadObject_ObjectName;
	int32_t DynamicLoadObject_ObjectClass;
	int32_t DynamicLoadObject_MayFail;
	int32_t DynamicLoadObject_ReturnValue;
	int32_t Pawn_GroundSpeed;
	int32_t FoxPawn_PawnModifiers;
	int32_t ModInfo_HealthMax;
	int32_t ModInfo_MovementSpeed;
};

extern Functions fn;
extern Classes cls;
extern Offsets off;
extern LookRefs look;
extern Config config;
extern Mode mode;

inline bool IsVersus()
{
	return mode == MODE_DEATHMATCH || mode == MODE_TEAM_DEATHMATCH || mode == MODE_CONTROL_POINT || mode == MODE_LAST_MAN || mode == MODE_LAST_TEAM || mode == MODE_RETRIEVAL;
}

inline bool IsTeamVersus()
{
	return mode == MODE_TEAM_DEATHMATCH || mode == MODE_CONTROL_POINT || mode == MODE_LAST_TEAM || mode == MODE_RETRIEVAL;
}

inline bool IsRoundVersus()
{
	return mode == MODE_LAST_MAN || mode == MODE_LAST_TEAM;
}

extern std::vector<Bot> bots;
extern UE3::UObject* current_game;

Bot* FindBot(UE3::UObject* controller);
bool IsBot(UE3::UObject* controller);
bool IsAlive(UE3::UObject* pawn);
float Distance(const UE3::FVector& a, const UE3::FVector& b);
void FindHumans(UE3::UObject* game, std::vector<UE3::UObject*>& humans);
void CallFunction(UE3::UObject* object, UE3::UObject* function, std::vector<uint8_t>& parms);
void RemoveBot(size_t index);
UE3::UObject* SpawnActor(UE3::UObject* spawner, UE3::UObject* actor_class, const UE3::FVector& location, const UE3::FRotator& rotation);
bool DestroyActor(UE3::UObject* actor);
void PossessPawn(UE3::UObject* controller, UE3::UObject* pawn);
UE3::UObject* DefaultObject(UE3::UObject* class_object);
UE3::UObject* LoadClass(const char* path);
UE3::UObject* PawnSkeletalMesh(UE3::UObject* pawn);
bool IsFreeSpot(UE3::UObject* any_actor, const UE3::FVector& point, UE3::UObject* ignore, float radius = FREE_SPOT_RADIUS);
void FindNavMeshSpots(UE3::UObject* controller, const UE3::FVector& point, float radius, std::vector<UE3::FVector>& spots);
bool FindFreeNavMeshSpot(UE3::UObject* controller, UE3::UObject* pawn, const UE3::FVector& point, float radius, UE3::FVector& spot);
bool FindFreeNavMeshSpotToward(UE3::UObject* controller, UE3::UObject* pawn, const UE3::FVector& point, float radius, const UE3::FVector& from, UE3::FVector& spot);
bool StandsOnNavMesh(UE3::UObject* controller, const UE3::FVector& location);
bool MovePawnToSpot(UE3::UObject* pawn, const UE3::FVector& spot);
bool ClearLine(UE3::UObject* actor, const UE3::FVector& from, const UE3::FVector& to);
bool PawnSeesPawn(UE3::UObject* pawn, UE3::UObject* other);
bool PawnSeesPoint(UE3::UObject* pawn, const UE3::FVector& point);
bool FindSightSpot(UE3::UObject* controller, UE3::UObject* pawn, float radius, const UE3::FVector& point, float min_distance, UE3::FVector& spot);
void SetUpCommands(Bot& bot);
void SetGoToSprint(Bot& bot, bool sprint);
void SetGoToAbortable(Bot& bot, bool abortable);
void SetUpFightFromCover(Bot& bot);
void RemoveMelee(Bot& bot);
void StopFiring(UE3::UObject* pawn);
void SetUpBot(Bot& bot);
void LeaveStaleCover(Bot& bot, UE3::UObject* pawn, DWORD now);
bool CanClaimCover(UE3::UObject* pawn, UE3::UObject* link, int32_t slot);
UE3::UObject* HumanSquad(UE3::UObject* game);
int WeaponRole(int slot);
static const int ROLE_RIFLE = 0;
static const int ROLE_SNIPER = 1;
static const int ROLE_SHOTGUN = 2;
UE3::UObject* CurrentBehavior(UE3::UObject* controller);
void ApplyBotSettings(UE3::UObject* controller, UE3::UObject* pawn, int slot, int32_t base_health, float health_regen);
UE3::UObject* SpawnBotController(UE3::UObject* spawner, int slot, uint8_t team, UE3::UObject** wave_pawn);
void SendBehaviorEvent(UE3::UObject* controller, const wchar_t* event_name, UE3::UObject* instigator, UE3::UObject* recipient, const UE3::FVector& location);
bool IsFighting(UE3::UObject* controller);
bool IsIdle(UE3::UObject* controller);
bool IsFollowingCommand(UE3::UObject* controller);
bool CoverFacing(UE3::UObject* controller, UE3::UObject* link, int32_t slot, float& facing);
bool CoverSeesPoint(UE3::UObject* pawn, UE3::UObject* link, int32_t slot, const UE3::FVector& target);

}
