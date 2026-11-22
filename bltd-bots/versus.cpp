#include "bots-internal.hpp"
#include "fox-ai.hpp"
#include "lobby.hpp"
#include "navmesh.hpp"
#include "server.hpp"
#include "spawns.hpp"
#include "versus.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using UE3::UObject;
using UE3::FFrame;
using UE3::FVector;
using UE3::TArray;
using UE3::Field;

namespace Bots {
namespace Versus {

static const wchar_t* const AI_CONTENT_PACKAGE = L"FoxGameContent_BO_SF";
static const char* const COVER_PAWN_CLASSES[2] = { "FoxGameContent_BO.FoxPawn_Order_Cover", "FoxGameContent_BO.FoxPawn_Blacklight_Cover" };
static const float GAME_AI_DAMAGE_MOD = 0.5f;
static const wchar_t GOTO_COMMAND[] = L"Command GoTo";
static const float EVALUATION_INTERVAL = 1.0f;
static const float FORGET_ENEMY_SECONDS = 12.0f;
static const float FRESH_ENEMY_SECONDS = 2.0f;
static const DWORD NOISE_HUNT_MS = 6000;
static const float NOISE_REGOAL_DISTANCE = 500.0f;
static const float STAKEOUT_RADIUS = 700.0f;
static const float STAKEOUT_MIN_DISTANCE = 300.0f;
static const DWORD VISIT_FORGET_MS = 120000;
static const float VISIT_RADIUS = 1200.0f;
static const size_t VISIT_LIMIT = 400;
static const int ROAM_CANDIDATES = 6;
static const int32_t ENEMY_INFO_SIZE = 0x30;
static const int32_t PRESENT_TEAM_INFO_SIZE = 0x18;
static const DWORD CP_CAPTURE_HOLD_MS = 20000;
static const DWORD CP_DEFEND_HOLD_MS = 5000;
static const float CP_RETURN_DISTANCE = 400.0f;
static const float CP_STAND_RADIUS = 120.0f;
static const float CP_FIGHT_RANGE = 800.0f;
static const DWORD CP_HOLD_EXTEND_MS = 4000;
static const float FLAG_TOUCH = 160.0f;
static const float FLAG_MOVED = 400.0f;
static const DWORD CTF_CARRY_WAIT_MS = 20000;
static const DWORD CTF_GUARD_MS = 12000;
static const int CTF_CHASERS = 2;
static const int CTF_ESCORTS = 2;
static const uint8_t GAME_STATUS_ROUND_IN_PROGRESS = 3;
static const int CP_NEAREST_PERCENT = 60;
static const float GOAL_REACHED = 250.0f;
static const DWORD GOAL_MAX_AGE_MS = 20000;
static const DWORD GOAL_PROGRESS_MS = 3000;
static const DWORD GOAL_BAN_MS = 60000;
static const float GOAL_BAN_RADIUS = 300.0f;
static const float GOAL_SNAP_RADIUS = 300.0f;
static const uint32_t STALLED_GOAL_LIMIT = 2;
static const float ESCAPE_DISTANCE = 200.0f;
static const DWORD NUDGE_ESCALATE_MS = 25000;
static const DWORD SPAWN_STAGGER_MIN_MS = 800;
static const DWORD SPAWN_STAGGER_MAX_MS = 1600;
static const DWORD RESPAWN_JITTER_MS = 2500;
static const DWORD UNSNAG_MS = 1500;
static const float UNSNAG_REACH = 60.0f;
static const float UNSNAG_STEP = 24.0f;
static const uint32_t UNSNAG_LIMIT = 2;
static const float UNSNAG_SIDE = 30.0f;
static const float WALL_SLIDE = 48.0f;
static const float UNSNAG_BOX_RADIUS = 22.0f;
static const float UNSNAG_BOX_BOTTOM = 36.0f;
static const float UNSNAG_BOX_HALF_HEIGHT = 72.0f;
static const int32_t TRACEFLAG_BLOCKING = 8;
static const float LEDGE_DROP = 40.0f;
static const float LEDGE_REACH = 300.0f;
static const float LEDGE_STEP = 48.0f;
static const float PAWN_STEP = 35.0f;
static const float FLOOR_PROBE_DEPTH = 300.0f;
static const float NEXT_MOVE_DELAY = 0.1f;
static const DWORD ON_MESH_CHECK_MS = 100;
static const DWORD ANCHOR_CHECK_MS = 500;
static const float ANCHOR_STEP_RADIUS = 250.0f;
static const float ANCHOR_STEP_MIN = 64.0f;
static const int ANCHOR_STEP_TRIES = 6;
static const DWORD TURRET_CHECK_MS = 2000;
static const float WALL_STEP_SPEED = 250.0f;
static const float WALL_STEP_REACH = 45.0f;
static const float WALL_STEP = 16.0f;
static const DWORD WALL_STEP_MS = 400;
static const DWORD WALL_STEP_WINDOW_MS = 10000;
static const uint32_t WALL_STEP_LIMIT = 8;
static const float HOP_CHEST = 70.0f;
static const float HOP_THICKNESS = 40.0f;
static const float HOP_HEAD = 150.0f;
static const float HOP_REACH = 250.0f;
static const float HOP_RISE = 60.0f;
static const float HOP_SPEED = 350.0f;
static const DWORD HOP_FLIGHT_MS = 600;
static const float SIDESTEP = 48.0f;
static const float SIDESTEP_CLEAR = 70.0f;
static const float TURRET_SIDESTEP = 96.0f;
static const float SNAP_STEP = 16.0f;
static const float SNAP_REACH = 64.0f;
static const float SNAP_INSIDE = 8.0f;
static const DWORD SNAP_AGAIN_MS = 20000;
static const DWORD START_RETRY_MS = 5000;
static const float STEP_CLEAR = 80.0f;
static const char NO_START_POLY[] = "had no navmesh poly to start a path from";
static const float JUMP_TRIGGER = 60.0f;
static const float JUMP_FLOOR_WINDOW = 150.0f;
static const float JUMP_STEP = 40.0f;
static const float JUMP_MIN_SPEED = 50.0f;
static const DWORD JUMP_AGAIN_MS = 1500;
static const float JUMP_LOG_RADIUS = 150.0f;
static const DWORD JUMP_LOG_MS = 2000;
static const DWORD JUMP_MEASURE_MS = 150;
static const DWORD JUMP_HOLD_EXTRA_MS = 150;
static const float JUMP_MAX_LIFT = 2.0f;
static const float DEFAULT_GRAVITY = 1700.0f;
static const float PAWN_HALF_HEIGHT = 96.0f;
static const uint8_t PHYS_WALKING = 1;
static const uint8_t PHYS_FALLING = 2;
static const uint8_t PHYS_NAVMESHWALKING = 12;
static const float ROAM_MIN_DISTANCE = 1500.0f;
static const float SEEK_RADIUS = 1500.0f;
static const int GOAL_TRIES = 8;
static const int SPAWN_GOAL_PERCENT = 40;
static const float ENEMY_BASE_RADIUS = 1200.0f;
static const DWORD HOLD_MAX_MS = 15000;
static const DWORD COMBAT_MOVE_INTERVAL_MS = 1500;
static const DWORD COMBAT_MOVE_JITTER_MS = 2000;
static const DWORD COMBAT_MOVE_MAX_MS = 2500;
static const DWORD FALL_BACK_MAX_MS = 4000;
static const int DODGE_PERCENT = 50;
static const int32_t DODGE_MIN = 200;
static const int32_t DODGE_MAX = 350;
static const float PUSH_STEP = 500.0f;
static const float PUSH_CLOSEST = 400.0f;
static const float BACK_OFF_STEP = 400.0f;
static const float FALL_BACK_MIN = 500.0f;
static const float FALL_BACK_MAX = 1300.0f;
static const float FALL_BACK_GAIN = 300.0f;
static const float FALL_BACK_HEALTH = 0.35f;
static const float COMBAT_MOVE_SNAP = 150.0f;
static const float COMBAT_MOVE_MIN = 100.0f;
static const DWORD SPOT_REFRESH_MS = 20000;
static const float MAP_EXTENT = 100000.0f;
static const DWORD REACQUIRE_MS = 1500;
static const float WARNING_MISS_MIN = 100.0f;
static const float WARNING_MISS_MAX = 140.0f;
static const float RADIANS_TO_ROTATOR = 32768.0f / 3.14159265f;
static const float ROTATOR_TO_RADIANS = 3.14159265f / 32768.0f;
static const float CP_POST_RADIUS = 500.0f;
static const float CP_POST_MIN = 150.0f;
static const float CP_POST_FACING = 0.3f;
static const float CP_POST_EYE = 60.0f;
static const float COVER_LINK_REACH = 1500.0f;
static const DWORD EVENT_THINK_MIN_MS = 300;
static const wchar_t TARGET_SEEN_EVENT[] = L"VisibleTargetAcquired";
static const wchar_t TARGET_LOST_EVENT[] = L"TargetLost";
static const wchar_t MOVE_FINISHED_EVENT[] = L"MoveFinished";
static const uint16_t BROADCAST_PARMS = 24;
static const int32_t BROADCAST_SENDER = 0;
static const int32_t BROADCAST_MESSAGE = 4;
static const int32_t BROADCAST_SWITCH = 8;
static const int32_t BROADCAST_PRI_1 = 12;
static const int32_t BROADCAST_PRI_2 = 16;
static const int32_t BROADCAST_OBJECT = 20;
static const int32_t DEATH_SUICIDE = 0;
static const int32_t DEATH_KILL = 4;

static const char* const NAMES[] = {
	"Ghost", "Viper", "Raven", "Hawk", "Cobra", "Wolf", "Falcon", "Reaper", "Spectre", "Nomad", "Havoc",
	"Jackal", "Sabre", "Talon", "Echo", "Rook", "Bishop", "Mamba", "Hunter", "Shadow", "Blaze", "Frost",
	"Onyx", "Vandal", "Warden", "Tracer", "Static", "Cipher", "Drifter", "Outlaw", "Mirage", "Kodiak",
};
static const int NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

static struct {
	UObject* MP_Timer;
	UObject* MP_ChangeName;
	UObject* Game_GetNextPlayerID;
	UObject* Object_GotoState;
	UObject* Handle_GetAllPolyCenters;
	UObject* GameEndedProperty;
	UObject* FoxGRI_DM;
	UObject* FoxGRI_TDM;
	UObject* GRI_DM_GetSortedPRIs;
	UObject* GRI_GetWinningTeam;
	UObject* MP_Killed;
	UObject* PRI_AddToPlayerRank;
	UObject* PRI_GetCurrentExperience;
	UObject* BotProperty;
	UObject* InactiveProperty;
	UObject* ScoringDefaults;
	UObject* PlayerStart;
	UObject* FoxGRI_CP;
	UObject* CP_PawnEntered;
	UObject* CP_PawnLeaving;
	UObject* Volume_EncompassesPoint;
	UObject* FightStationary;
	UObject* Handle_FindPylon;
	UObject* Handle_IsAnchorInescapable;
	UObject* FoxGRI_LMS;
	UObject* FoxGRI_LTS;
	UObject* FoxGRI_CTF;
	UObject* LMS_RoundScoring;
	UObject* Game_EndCurrentRound;
	UObject* Game_RestartRound;
	UObject* LTS_ScoreKill;
	UObject* LTS_RoundScoring;
	UObject* DM_IsLevelReadyToStart;
	UObject* TDM_IsLevelReadyToStart;
	UObject* LMS_Game;
	UObject* LTS_Game;
	UObject* Flag_Pickup;
	UObject* Flag_Capture;
	UObject* Flag_ReturnToBase;
	UObject* Carry_IsPickupTouchPossible;
	UObject* Carry_InTeamSpawn;
	UObject* GameObject_CanInteractWith;
	UObject* FlagAtBaseProperty;
	UObject* Pawn_BuildScriptAnimSetList;
	UObject* Pawn_RestoreAnimSetsToDefault;
	UObject* Weapon_FireAmmunition;
	UObject* AI_GetAdjustedAimFor;
	UObject* MP_BroadcastDeathMessage;
	UObject* Game_BroadcastLocalized;
	UObject* DeathMessageClass;
	UObject* Pawn_DoJump;
	UObject* Actor_SetPhysics;
	UObject* Actor_Trace;
} ref;

static struct {
	int32_t ChangeName_Other;
	int32_t ChangeName_S;
	int32_t GetNextPlayerID_ReturnValue;
	int32_t GotoState_NewState;
	int32_t PolyCenters_Extent;
	int32_t PolyCenters_Out;
	int32_t Trace_HitLocation;
	int32_t Trace_HitNormal;
	int32_t Trace_TraceEnd;
	int32_t Trace_TraceStart;
	int32_t Trace_bTraceActors;
	int32_t Trace_Extent;
	int32_t Trace_ExtraTraceFlags;
	int32_t Trace_ReturnValue;
	int32_t Actor_Acceleration;
	int32_t ChoosePlayerStart_Player;
	int32_t ChoosePlayerStart_ReturnValue;
	int32_t Controller_PlayerReplicationInfo;
	int32_t PRI_PlayerID;
	int32_t PRI_Team;
	int32_t TeamInfo_TeamIndex;
	int32_t Team_HumanSize;
	int32_t Game_GameRespawnTime;
	int32_t Squad_EnemyInfoList;
	int32_t EnemyInfo_TargetPawn;
	int32_t EnemyInfo_KnownLocation;
	int32_t EnemyInfo_LastSeenTimes;
	int32_t EnemyInfo_LastEngagedTimes;
	int32_t WorldInfo_TimeSeconds;
	int32_t MeshComponent_AnimTreeTemplate;
	int32_t MeshComponent_AnimSets;
	int32_t FoxPawn_FirstPersonMesh;
	int32_t WorldInfo_GRI;
	int32_t GRI_PRIArray;
	int32_t GRI_Teams;
	int32_t PRI_Score;
	int32_t PRI_Deaths;
	int32_t TeamInfo_Score;
	int32_t Killed_Killer;
	int32_t Killed_KilledPlayer;
	int32_t AddToPlayerRank_Amount;
	int32_t AddToPlayerRank_SkipNotify;
	int32_t GetCurrentExperience_ReturnValue;
	int32_t Scoring_ExpKills_DM;
	int32_t Scoring_ExpKills_TDM;
	int32_t AI_ActiveTargetIndex;
	int32_t AI_GetNextMoveMinDelay;
	int32_t AI_LastGetNextMove;
	int32_t AI_CurrentDestination;
	int32_t WorldInfo_NavigationPointList;
	int32_t NavigationPoint_nextNavigationPoint;
	int32_t PlayerStart_TeamIndex;
	int32_t CPGame_ControlPoints;
	int32_t CP_CPOwner;
	int32_t CP_CaptureInfo;
	int32_t PresentTeam_Pawns;
	int32_t Use_UseVolume;
	int32_t PawnEntered_EnteringPawn;
	int32_t PawnLeaving_LeavingPawn;
	int32_t Encompasses_Loc;
	int32_t Encompasses_ReturnValue;
	int32_t Use_CurrentlyUsingPawn;
	int32_t CP_CurrentStateName;
	int32_t CP_PointHackedTeam;
	int32_t CP_CurrentCapturingTeam;
	int32_t GRI_GameStatus;
	int32_t GRI_LMS_RemainingPlayers;
	int32_t MP_NumReadyPlayersToStart;
	int32_t GRI_LTS_Team1Remaining;
	int32_t GRI_LTS_Team2Remaining;
	int32_t RoundScoring_RoundWinner;
	int32_t CTF_RedFlag;
	int32_t CTF_BlueFlag;
	int32_t GameObject_UsableByTeam;
	int32_t Carry_CarriedByPawn;
	int32_t Carry_SpawnLocation;
	int32_t FoxPawn_CarriedObject;
	int32_t Pickup_PickedUpPawn;
	int32_t ReturnToBase_ReturningPRI;
	int32_t TouchPossible_PlayerTouched;
	int32_t TouchPossible_ReturnValue;
	int32_t InTeamSpawn_PawnToCheck;
	int32_t InTeamSpawn_ReturnValue;
	int32_t CanInteract_InteractingPawn;
	int32_t CanInteract_ReturnValue;
	int32_t Handle_AnchorPylon;
	int32_t FindPylon_ReturnValue;
	int32_t IsAnchorInescapable_ReturnValue;
	int32_t Pawn_MaxStepHeight;
	int32_t Pawn_WalkableFloorZ;
	int32_t Actor_Instigator;
	int32_t Aim_StartFireLoc;
	int32_t DeathMessage_Killer;
	int32_t DeathMessage_Other;
	int32_t DeathMessage_DamageType;
	int32_t Pawn_JumpZ;
	int32_t Pawn_AirSpeed;
	int32_t DoJump_ReturnValue;
	int32_t SetPhysics_NewPhysics;
	int32_t WorldInfo_DefaultGravityZ;
} voff;

struct SpawnSpot {
	FVector Location;
	int32_t Team;
};

struct Visit {
	FVector At;
	DWORD Tick;
};

static bool ready = false;
static int32_t debug_state = -1;
static int32_t choosing_behavior_state = -1;
static std::vector<FVector> spots;
static DWORD spots_tick = 0;
static std::vector<SpawnSpot> spawn_spots;
static std::vector<Visit> visits[2];
static bool round_scored = false;
static FVector flag_base[2];
static bool flag_base_known[2];
static int name_order[NAME_COUNT];
static int names_given = 0;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.MP_Timer, "FoxGame.FoxGameMP.Timer" },
		{ &ref.MP_ChangeName, "FoxGame.FoxGameMP.ChangeName" },
		{ &ref.Game_GetNextPlayerID, "Engine.GameInfo.GetNextPlayerID" },
		{ &ref.Object_GotoState, "Core.Object.GotoState" },
		{ &ref.Handle_GetAllPolyCenters, "Engine.NavigationHandle.GetAllPolyCentersWithinBounds" },
		{ &ref.GameEndedProperty, "Engine.GameInfo.bGameEnded" },
		{ &ref.FoxGRI_DM, "FoxGame.FoxGRI_DM" },
		{ &ref.FoxGRI_TDM, "FoxGame.FoxGRI_TDM" },
		{ &ref.GRI_DM_GetSortedPRIs, "FoxGame.FoxGRI_DM.GetSortedPRIs" },
		{ &ref.GRI_GetWinningTeam, "FoxGame.FoxGRI.GetWinningTeam" },
		{ &ref.MP_Killed, "FoxGame.FoxGameMP.Killed" },
		{ &ref.PRI_AddToPlayerRank, "FoxGame.FoxPlayerReplicationInfo.AddToPlayerRank" },
		{ &ref.PRI_GetCurrentExperience, "FoxGame.FoxPlayerReplicationInfo.GetCurrentExperience" },
		{ &ref.BotProperty, "Engine.PlayerReplicationInfo.bBot" },
		{ &ref.InactiveProperty, "Engine.PlayerReplicationInfo.bIsInactive" },
		{ &ref.ScoringDefaults, "FoxGame.Default__FoxScoringInfo" },
		{ &ref.PlayerStart, "Engine.PlayerStart" },
		{ &ref.FoxGRI_CP, "FoxGame.FoxGRI_CP" },
		{ &ref.CP_PawnEntered, "FoxGame.FoxGameObject_ControlPoint.PawnEnteredUseVolume" },
		{ &ref.CP_PawnLeaving, "FoxGame.FoxGameObject_ControlPoint.PawnLeavingUseVolume" },
		{ &ref.Volume_EncompassesPoint, "Engine.Volume.EncompassesPoint" },
		{ &ref.FightStationary, "FoxGame.FoxAIAbility_FightStationary" },
		{ &ref.Handle_FindPylon, "Engine.NavigationHandle.FindPylon" },
		{ &ref.Handle_IsAnchorInescapable, "Engine.NavigationHandle.IsAnchorInescapable" },
		{ &ref.FoxGRI_LMS, "FoxGame.FoxGRI_LMS" },
		{ &ref.FoxGRI_LTS, "FoxGame.FoxGRI_LTS" },
		{ &ref.FoxGRI_CTF, "FoxGame.FoxGRI_CTF" },
		{ &ref.LMS_RoundScoring, "FoxGame.FoxGameMP_LMS.RoundScoring" },
		{ &ref.Game_EndCurrentRound, "FoxGame.FoxGame.EndCurrentRound" },
		{ &ref.Game_RestartRound, "FoxGame.FoxGame.RestartRound" },
		{ &ref.LTS_ScoreKill, "FoxGame.FoxGameMP_LTS.ScoreKill" },
		{ &ref.LTS_RoundScoring, "FoxGame.FoxGameMP_LTS.RoundScoring" },
		{ &ref.DM_IsLevelReadyToStart, "FoxGame.FoxGameMP_DM.IsLevelReadyToStart" },
		{ &ref.TDM_IsLevelReadyToStart, "FoxGame.FoxGameMP_TDM.IsLevelReadyToStart" },
		{ &ref.LMS_Game, "FoxGame.FoxGameMP_LMS" },
		{ &ref.LTS_Game, "FoxGame.FoxGameMP_LTS" },
		{ &ref.Flag_Pickup, "FoxGame.FoxGameObject_Flag.Pickup" },
		{ &ref.Pawn_DoJump, "FoxGame.FoxPawn.DoJump" },
		{ &ref.Actor_SetPhysics, "Engine.Actor.SetPhysics" },
		{ &ref.Actor_Trace, "Engine.Actor.Trace" },
		{ &ref.Flag_Capture, "FoxGame.FoxGameObject_Flag.Capture" },
		{ &ref.Flag_ReturnToBase, "FoxGame.FoxGameObject_Flag.ReturnToBase" },
		{ &ref.Carry_IsPickupTouchPossible, "FoxGame.FoxGameObject_Carry.IsPickupTouchPossible" },
		{ &ref.Carry_InTeamSpawn, "FoxGame.FoxGameObject_Carry.InTeamSpawn" },
		{ &ref.GameObject_CanInteractWith, "FoxGame.FoxGameObject.CanInteractWith" },
		{ &ref.FlagAtBaseProperty, "FoxGame.FoxGameObject_Flag.bIsAtBase" },
		{ &ref.Pawn_BuildScriptAnimSetList, "FoxGame.FoxPawn.BuildScriptAnimSetList" },
		{ &ref.Pawn_RestoreAnimSetsToDefault, "Engine.Pawn.RestoreAnimSetsToDefault" },
		{ &ref.Weapon_FireAmmunition, "FoxGame.FoxWeapon.FireAmmunition" },
		{ &ref.AI_GetAdjustedAimFor, "FoxGame.FoxAIControllerInterface.GetAdjustedAimFor" },
		{ &ref.MP_BroadcastDeathMessage, "FoxGame.FoxGameMP.BroadcastDeathMessage" },
		{ &ref.Game_BroadcastLocalized, "Engine.GameInfo.BroadcastLocalized" },
		{ &ref.DeathMessageClass, "FoxGame.FoxDeathMessage" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &voff.ChangeName_Other, "FoxGame.FoxGameMP.ChangeName.Other" },
		{ &voff.ChangeName_S, "FoxGame.FoxGameMP.ChangeName.S" },
		{ &voff.GetNextPlayerID_ReturnValue, "Engine.GameInfo.GetNextPlayerID.ReturnValue" },
		{ &voff.GotoState_NewState, "Core.Object.GotoState.NewState" },
		{ &voff.PolyCenters_Extent, "Engine.NavigationHandle.GetAllPolyCentersWithinBounds.Extent" },
		{ &voff.PolyCenters_Out, "Engine.NavigationHandle.GetAllPolyCentersWithinBounds.out_PolyCtrs" },
		{ &voff.Trace_HitLocation, "Engine.Actor.Trace.HitLocation" },
		{ &voff.Trace_HitNormal, "Engine.Actor.Trace.HitNormal" },
		{ &voff.Trace_TraceEnd, "Engine.Actor.Trace.TraceEnd" },
		{ &voff.Trace_TraceStart, "Engine.Actor.Trace.TraceStart" },
		{ &voff.Trace_bTraceActors, "Engine.Actor.Trace.bTraceActors" },
		{ &voff.Trace_Extent, "Engine.Actor.Trace.Extent" },
		{ &voff.Trace_ExtraTraceFlags, "Engine.Actor.Trace.ExtraTraceFlags" },
		{ &voff.Trace_ReturnValue, "Engine.Actor.Trace.ReturnValue" },
		{ &voff.Actor_Acceleration, "Engine.Actor.Acceleration" },
		{ &voff.ChoosePlayerStart_Player, "FoxGame.FoxGameMP.ChoosePlayerStart.Player" },
		{ &voff.ChoosePlayerStart_ReturnValue, "FoxGame.FoxGameMP.ChoosePlayerStart.ReturnValue" },
		{ &voff.Controller_PlayerReplicationInfo, "Engine.Controller.PlayerReplicationInfo" },
		{ &voff.PRI_PlayerID, "Engine.PlayerReplicationInfo.PlayerID" },
		{ &voff.PRI_Team, "Engine.PlayerReplicationInfo.Team" },
		{ &voff.TeamInfo_TeamIndex, "Engine.TeamInfo.TeamIndex" },
		{ &voff.Team_HumanSize, "FoxGame.FoxTeamInfo.HumanSize" },
		{ &voff.Game_GameRespawnTime, "FoxGame.FoxGame.GameRespawnTime" },
		{ &voff.Squad_EnemyInfoList, "FoxGame.FoxSquadInfo.EnemyInfoList" },
		{ &voff.EnemyInfo_TargetPawn, "FoxGame.FoxSquadInfo.EnemyInfo.TargetPawn" },
		{ &voff.EnemyInfo_KnownLocation, "FoxGame.FoxSquadInfo.EnemyInfo.KnownLocation" },
		{ &voff.EnemyInfo_LastSeenTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastSeenTimes" },
		{ &voff.EnemyInfo_LastEngagedTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastEngagedTimes" },
		{ &voff.WorldInfo_TimeSeconds, "Engine.WorldInfo.TimeSeconds" },
		{ &voff.MeshComponent_AnimTreeTemplate, "Engine.SkeletalMeshComponent.AnimTreeTemplate" },
		{ &voff.MeshComponent_AnimSets, "Engine.SkeletalMeshComponent.AnimSets" },
		{ &voff.FoxPawn_FirstPersonMesh, "FoxGame.FoxPawn.FirstPersonMesh" },
		{ &voff.WorldInfo_GRI, "Engine.WorldInfo.GRI" },
		{ &voff.GRI_PRIArray, "Engine.GameReplicationInfo.PRIArray" },
		{ &voff.GRI_Teams, "Engine.GameReplicationInfo.Teams" },
		{ &voff.PRI_Score, "Engine.PlayerReplicationInfo.Score" },
		{ &voff.PRI_Deaths, "Engine.PlayerReplicationInfo.Deaths" },
		{ &voff.TeamInfo_Score, "Engine.TeamInfo.Score" },
		{ &voff.Killed_Killer, "FoxGame.FoxGameMP.Killed.Killer" },
		{ &voff.Killed_KilledPlayer, "FoxGame.FoxGameMP.Killed.KilledPlayer" },
		{ &voff.AddToPlayerRank_Amount, "FoxGame.FoxPlayerReplicationInfo.AddToPlayerRank.Amount" },
		{ &voff.AddToPlayerRank_SkipNotify, "FoxGame.FoxPlayerReplicationInfo.AddToPlayerRank.bSkipNotifyRankChange" },
		{ &voff.GetCurrentExperience_ReturnValue, "FoxGame.FoxPlayerReplicationInfo.GetCurrentExperience.ReturnValue" },
		{ &voff.Scoring_ExpKills_DM, "FoxGame.FoxScoringInfo.ExpKills_DM" },
		{ &voff.Scoring_ExpKills_TDM, "FoxGame.FoxScoringInfo.ExpKills_TDM" },
		{ &voff.AI_ActiveTargetIndex, "FoxGame.FoxAIControllerInterface.ActiveTargetIndex" },
		{ &voff.AI_GetNextMoveMinDelay, "FoxGame.FoxAIControllerInterface.GetNextMoveMinDelay" },
		{ &voff.AI_LastGetNextMove, "FoxGame.FoxAIControllerInterface.LastGetNextMove" },
		{ &voff.AI_CurrentDestination, "FoxGame.FoxAIControllerInterface.CurrentDestination" },
		{ &voff.WorldInfo_NavigationPointList, "Engine.WorldInfo.NavigationPointList" },
		{ &voff.NavigationPoint_nextNavigationPoint, "Engine.NavigationPoint.nextNavigationPoint" },
		{ &voff.PlayerStart_TeamIndex, "Engine.PlayerStart.TeamIndex" },
		{ &voff.CPGame_ControlPoints, "FoxGame.FoxGameMP_CP.ControlPoints" },
		{ &voff.CP_CPOwner, "FoxGame.FoxGameObject_ControlPoint.CPOwner" },
		{ &voff.CP_CaptureInfo, "FoxGame.FoxGameObject_ControlPoint.CaptureInfo" },
		{ &voff.PresentTeam_Pawns, "FoxGame.FoxGameObject_ControlPoint.PresentTeamInfo.Pawns" },
		{ &voff.Use_UseVolume, "FoxGame.FoxGameObject_Use.UseVolume" },
		{ &voff.PawnEntered_EnteringPawn, "FoxGame.FoxGameObject_ControlPoint.PawnEnteredUseVolume.EnteringPawn" },
		{ &voff.PawnLeaving_LeavingPawn, "FoxGame.FoxGameObject_ControlPoint.PawnLeavingUseVolume.LeavingPawn" },
		{ &voff.Encompasses_Loc, "Engine.Volume.EncompassesPoint.Loc" },
		{ &voff.Encompasses_ReturnValue, "Engine.Volume.EncompassesPoint.ReturnValue" },
		{ &voff.Use_CurrentlyUsingPawn, "FoxGame.FoxGameObject_Use.CurrentlyUsingPawn" },
		{ &voff.CP_CurrentStateName, "FoxGame.FoxGameObject_ControlPoint.CurrentStateName" },
		{ &voff.CP_PointHackedTeam, "FoxGame.FoxGameObject_ControlPoint.PointHackedTeam" },
		{ &voff.CP_CurrentCapturingTeam, "FoxGame.FoxGameObject_ControlPoint.CurrentCapturingTeam" },
		{ &voff.GRI_GameStatus, "FoxGame.FoxGRI.GameStatus" },
		{ &voff.GRI_LMS_RemainingPlayers, "FoxGame.FoxGRI_LMS.RemainingPlayers" },
		{ &voff.MP_NumReadyPlayersToStart, "FoxGame.FoxGameMP.NumReadyPlayersToStart" },
		{ &voff.GRI_LTS_Team1Remaining, "FoxGame.FoxGRI_LTS.Team1RemainingPlayers" },
		{ &voff.GRI_LTS_Team2Remaining, "FoxGame.FoxGRI_LTS.Team2RemainingPlayers" },
		{ &voff.RoundScoring_RoundWinner, "FoxGame.FoxGameMP_LMS.RoundScoring.RoundWinner" },
		{ &voff.CTF_RedFlag, "FoxGame.FoxGameMP_CTF.RedFlag" },
		{ &voff.CTF_BlueFlag, "FoxGame.FoxGameMP_CTF.BlueFlag" },
		{ &voff.GameObject_UsableByTeam, "FoxGame.FoxGameObject.UsableByTeam" },
		{ &voff.Carry_CarriedByPawn, "FoxGame.FoxGameObject_Carry.CarriedByPawn" },
		{ &voff.Carry_SpawnLocation, "FoxGame.FoxGameObject_Carry.SpawnLocation" },
		{ &voff.FoxPawn_CarriedObject, "FoxGame.FoxPawn.CarriedObject" },
		{ &voff.Pickup_PickedUpPawn, "FoxGame.FoxGameObject_Flag.Pickup.PickedUpPawn" },
		{ &voff.ReturnToBase_ReturningPRI, "FoxGame.FoxGameObject_Flag.ReturnToBase.ReturningPRI" },
		{ &voff.TouchPossible_PlayerTouched, "FoxGame.FoxGameObject_Carry.IsPickupTouchPossible.PlayerTouched" },
		{ &voff.TouchPossible_ReturnValue, "FoxGame.FoxGameObject_Carry.IsPickupTouchPossible.ReturnValue" },
		{ &voff.InTeamSpawn_PawnToCheck, "FoxGame.FoxGameObject_Carry.InTeamSpawn.PawnToCheck" },
		{ &voff.InTeamSpawn_ReturnValue, "FoxGame.FoxGameObject_Carry.InTeamSpawn.ReturnValue" },
		{ &voff.CanInteract_InteractingPawn, "FoxGame.FoxGameObject.CanInteractWith.InteractingPawn" },
		{ &voff.CanInteract_ReturnValue, "FoxGame.FoxGameObject.CanInteractWith.ReturnValue" },
		{ &voff.Handle_AnchorPylon, "Engine.NavigationHandle.AnchorPylon" },
		{ &voff.Pawn_MaxStepHeight, "Engine.Pawn.MaxStepHeight" },
		{ &voff.Pawn_WalkableFloorZ, "Engine.Pawn.WalkableFloorZ" },
		{ &voff.Actor_Instigator, "Engine.Actor.Instigator" },
		{ &voff.Aim_StartFireLoc, "FoxGame.FoxAIControllerInterface.GetAdjustedAimFor.StartFireLoc" },
		{ &voff.DeathMessage_Killer, "FoxGame.FoxGameMP.BroadcastDeathMessage.Killer" },
		{ &voff.DeathMessage_Other, "FoxGame.FoxGameMP.BroadcastDeathMessage.Other" },
		{ &voff.DeathMessage_DamageType, "FoxGame.FoxGameMP.BroadcastDeathMessage.DamageType" },
		{ &voff.FindPylon_ReturnValue, "Engine.NavigationHandle.FindPylon.ReturnValue" },
		{ &voff.IsAnchorInescapable_ReturnValue, "Engine.NavigationHandle.IsAnchorInescapable.ReturnValue" },
		{ &voff.Pawn_JumpZ, "Engine.Pawn.JumpZ" },
		{ &voff.Pawn_AirSpeed, "Engine.Pawn.AirSpeed" },
		{ &voff.DoJump_ReturnValue, "Engine.Pawn.DoJump.ReturnValue" },
		{ &voff.SetPhysics_NewPhysics, "Engine.Actor.SetPhysics.newPhysics" },
		{ &voff.WorldInfo_DefaultGravityZ, "Engine.WorldInfo.DefaultGravityZ" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	if (voff.PresentTeam_Pawns + (int32_t)sizeof(TArray<UObject*>) != PRESENT_TEAM_INFO_SIZE) {
		BOTS_LOG(LOG_LEVEL_ERROR, "PresentTeamInfo.Pawns is at 0x%x, not 0x%x.", voff.PresentTeam_Pawns, PRESENT_TEAM_INFO_SIZE - (int32_t)sizeof(TArray<UObject*>));
		return false;
	}
	if (voff.EnemyInfo_LastEngagedTimes + (int32_t)sizeof(TArray<float>) != ENEMY_INFO_SIZE) {
		BOTS_LOG(LOG_LEVEL_ERROR, "EnemyInfo.LastEngagedTimes is at 0x%x, not 0x%x.", voff.EnemyInfo_LastEngagedTimes, ENEMY_INFO_SIZE - (int32_t)sizeof(TArray<float>));
		return false;
	}
	if (UE3::FunctionParmsSize(ref.Game_BroadcastLocalized) != BROADCAST_PARMS) {
		BOTS_LOG(LOG_LEVEL_ERROR, "GameInfo.BroadcastLocalized takes %u bytes of parameters, not %u.", UE3::FunctionParmsSize(ref.Game_BroadcastLocalized), BROADCAST_PARMS);
		return false;
	}
	debug_state = UE3::FindNameIndex("DebugState");
	choosing_behavior_state = UE3::FindNameIndex("ChoosingBehavior");
	if (debug_state < 0 || choosing_behavior_state < 0 || !look.DynamicLoadObject) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "The AI state names or the pawn properties were not found.");
		return false;
	}
	srand(GetTickCount());
	ready = true;
	return true;
}

int32_t ScaleDamage(int32_t damage)
{
	return (int32_t)((float)damage * config.Versus.DamageScale / GAME_AI_DAMAGE_MOD);
}

void OnBotHit(UObject* controller, UObject* instigator)
{
	Bot* bot = FindBot(controller);
	UObject* shooter = instigator ? Field<UObject*>(instigator, off.Controller_Pawn) : 0;
	if (bot && IsAlive(shooter)) {
		bot->AttackedFrom = Field<FVector>(shooter, off.Actor_Location);
		bot->AttackedTick = GetTickCount();
	}
}

static void ShuffleNames()
{
	for (int i = 0; i < NAME_COUNT; i++) {
		name_order[i] = i;
	}
	for (int i = NAME_COUNT - 1; i > 0; i--) {
		std::swap(name_order[i], name_order[rand() % (i + 1)]);
	}
	names_given = 0;
}

static void GotoState(UObject* controller, int32_t state_name)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Object_GotoState) + 16, 0);
	Field<UE3::FName>(parms.data(), voff.GotoState_NewState) = UE3::FName{ state_name, 0 };
	UE3::ProcessNativeEvent(controller, ref.Object_GotoState, parms.data());
}

static int32_t BotsOnTeam(uint8_t team)
{
	int32_t count = 0;
	for (const Bot& bot : bots) {
		count += bot.Team == team ? 1 : 0;
	}
	return count;
}

static bool Banned(const Bot& bot, const FVector& point, DWORD now)
{
	for (const GoalBan& ban : bot.GoalBans) {
		if ((int32_t)(ban.UntilTick - now) > 0 && Distance(ban.Goal, point) < GOAL_BAN_RADIUS) {
			return true;
		}
	}
	return false;
}

static bool InEnemyBase(const Bot& bot, const FVector& point)
{
	if (!IsTeamVersus()) {
		return false;
	}
	for (const SpawnSpot& spawn : spawn_spots) {
		if (spawn.Team != bot.Team && (spawn.Team == ORDER_TEAM || spawn.Team == BLACKLIGHT_TEAM) && Distance(spawn.Location, point) < ENEMY_BASE_RADIUS) {
			return true;
		}
	}
	return false;
}

static int SeekPercent(const Bot& bot)
{
	return (std::max)(0, (std::min)(100, config.Versus.SeekChance * bot.Aggression / 50));
}

static float HuntSeconds(const Bot& bot)
{
	return 4.0f + 0.12f * (float)bot.Aggression;
}

static DWORD HoldMs(const Bot& bot)
{
	float care = (float)(100 - bot.Aggression) / 100.0f;
	return (DWORD)((float)HOLD_MAX_MS * care * care * (0.5f + (float)(rand() % 51) / 100.0f));
}

static float PushRange(const Bot& bot)
{
	return 2000.0f - 12.0f * (float)bot.Aggression;
}

static float KeepRange(const Bot& bot)
{
	return 500.0f + 10.0f * (float)(50 - bot.Aggression);
}

static float Factor(int32_t value, float spread)
{
	return 1.0f + spread * (float)(50 - value) / 100.0f;
}

static DWORD ReactionMs(const Bot& bot)
{
	return (DWORD)((float)config.Versus.ReactionMs * Factor(bot.Skill, 0.4f) * Factor(bot.Aggression, 0.2f) * (0.85f + (float)(rand() % 31) / 100.0f));
}

static DWORD FocusMs(const Bot& bot)
{
	return (DWORD)(config.Versus.FocusSeconds * 1000.0f * Factor(bot.Skill, 0.4f));
}

static float FiringAngle(const Bot& bot)
{
	return config.Versus.FiringAngle * Factor(bot.Skill, 0.3f);
}

static const char* NameBot(UObject* game, Bot& bot)
{
	const char* name = NAMES[name_order[names_given++ % NAME_COUNT]];
	size_t length = strlen(name) + 1;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.MP_ChangeName) + 16, 0);
	UE3::FString& text = Field<UE3::FString>(parms.data(), voff.ChangeName_S);
	text.Data = (wchar_t*)UE3::Malloc((uint32_t)(length * sizeof(wchar_t)), 8);
	if (text.Data) {
		for (size_t i = 0; i < length; i++) {
			text.Data[i] = (wchar_t)(unsigned char)name[i];
		}
		text.Num = text.Max = (int32_t)length;
		Field<UObject*>(parms.data(), voff.ChangeName_Other) = bot.Controller;
		UE3::ProcessEvent(game, ref.MP_ChangeName, parms.data());
		UE3::FreeArray(&text);
	}
	UObject* pri = Field<UObject*>(bot.Controller, voff.Controller_PlayerReplicationInfo);
	if (pri) {
		std::vector<uint8_t> idParms;
		CallFunction(game, ref.Game_GetNextPlayerID, idParms);
		Field<int32_t>(pri, voff.PRI_PlayerID) = Field<int32_t>(idParms.data(), voff.GetNextPlayerID_ReturnValue);
	}
	return name;
}

static bool RunsCommand(UObject* controller)
{
	UObject* current = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	return current && current == Field<UObject*>(controller, off.AI_ScriptedBehavior + BEHAVIOR_INFO_BEHAVIOR);
}

static void EndCombatMove(Bot& bot)
{
	if (bot.CombatMoveEndTick) {
		SetGoToAbortable(bot, true);
		bot.CombatMoveEndTick = 0;
	}
}

static void PawnClassPath(const Bot& bot, char* path, size_t size)
{
	const char* lookPath = config.Slots[bot.Slot].Look;
	const char* blacklight = strstr(lookPath, "Blacklight");
	if (IsTeamVersus() && bot.Team == ORDER_TEAM && blacklight) {
		sprintf_s(path, size, "%.*sOrder%s", (int)(blacklight - lookPath), lookPath, blacklight + strlen("Blacklight"));
		return;
	}
	strcpy_s(path, size, lookPath);
}

static UObject* CoverMeshTemplate(const Bot& bot)
{
	bool order = IsTeamVersus() && bot.Team == ORDER_TEAM;
	UObject* coverClass = UE3::FindObject(COVER_PAWN_CLASSES[order ? 0 : 1]);
	UObject* coverDefaults = coverClass ? DefaultObject(coverClass) : 0;
	return coverDefaults ? Field<UObject*>(coverDefaults, look.Pawn_Mesh) : 0;
}

// A pawn's script copies default.Mesh.AnimSets at each weapon switch. The class template carries the AI's sets then.
struct AnimSetSwap {
	UObject* Pawn;
	UObject* Node;
	UObject* MeshTemplate;
	TArray<UObject*> Saved;
};
static std::vector<AnimSetSwap> anim_set_swaps;
static DWORD versus_start_tick = 0;
static bool match_end_logged = false;
static bool mode_meshes_cut = false;
static DWORD turrets_checked_tick = 0;

static void BeginAnimSetSwap(UObject* pawn, UObject* node)
{
	Bot* bot = FindBot(Field<UObject*>(pawn, off.Pawn_Controller));
	UObject* defaults = bot ? DefaultObject(UE3::ObjectClass(pawn)) : 0;
	UObject* meshTemplate = defaults ? Field<UObject*>(defaults, look.Pawn_Mesh) : 0;
	UObject* coverMesh = bot ? CoverMeshTemplate(*bot) : 0;
	if (!meshTemplate || !coverMesh) {
		return;
	}
	TArray<UObject*>& animSets = Field<TArray<UObject*> >(meshTemplate, voff.MeshComponent_AnimSets);
	AnimSetSwap swap = { pawn, node, meshTemplate, animSets };
	anim_set_swaps.push_back(swap);
	animSets = Field<TArray<UObject*> >(coverMesh, voff.MeshComponent_AnimSets);
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x rebuilds the animation sets of its pawn with the AI's %d sets.", bot->Controller, animSets.Num);
}

static void EndAnimSetSwap(UObject* pawn, UObject* node)
{
	for (size_t i = anim_set_swaps.size(); i-- > 0; ) {
		if (anim_set_swaps[i].Pawn == pawn && anim_set_swaps[i].Node == node) {
			Field<TArray<UObject*> >(anim_set_swaps[i].MeshTemplate, voff.MeshComponent_AnimSets) = anim_set_swaps[i].Saved;
			anim_set_swaps.erase(anim_set_swaps.begin() + i);
			return;
		}
	}
}

static UObject* GiveVersusPawn(UObject* game, Bot& bot, const FVector& location, const UE3::FRotator& rotation)
{
	char path[96];
	PawnClassPath(bot, path, sizeof(path));
	bool order = IsTeamVersus() && bot.Team == ORDER_TEAM;
	UObject* pawnClass = LoadClass(path);
	UObject* defaults = pawnClass ? DefaultObject(pawnClass) : 0;
	UObject* meshTemplate = defaults ? Field<UObject*>(defaults, look.Pawn_Mesh) : 0;
	UObject* coverMesh = CoverMeshTemplate(bot);
	if (!meshTemplate || !coverMesh) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s or %s is not loaded, bot 0x%08x gets no pawn.", path, COVER_PAWN_CLASSES[order ? 0 : 1], bot.Controller);
		return 0;
	}
	UObject*& tree = Field<UObject*>(meshTemplate, voff.MeshComponent_AnimTreeTemplate);
	TArray<UObject*>& animSets = Field<TArray<UObject*> >(meshTemplate, voff.MeshComponent_AnimSets);
	UObject*& firstPerson = Field<UObject*>(defaults, voff.FoxPawn_FirstPersonMesh);
	UObject* savedTree = tree;
	TArray<UObject*> savedSets = animSets;
	UObject* savedFirstPerson = firstPerson;
	tree = Field<UObject*>(coverMesh, voff.MeshComponent_AnimTreeTemplate);
	animSets = Field<TArray<UObject*> >(coverMesh, voff.MeshComponent_AnimSets);
	firstPerson = 0;
	UObject* pawn = SpawnActor(bot.Controller, pawnClass, location, rotation);
	tree = savedTree;
	animSets = savedSets;
	firstPerson = savedFirstPerson;
	if (!pawn) {
		return 0;
	}
	Field<float>(bot.Controller, off.AI_EvaluationInterval) = EVALUATION_INTERVAL;
	PossessPawn(bot.Controller, pawn);
	int32_t healthPercent = Field<int32_t>(defaults, look.FoxPawn_PawnModifiers + look.ModInfo_HealthMax);
	int32_t speedPercent = Field<int32_t>(defaults, look.FoxPawn_PawnModifiers + look.ModInfo_MovementSpeed);
	bot.ArmourHealthPercent = config.ArmourBonus ? healthPercent : 0;
	Field<float>(pawn, look.Pawn_GroundSpeed) *= (100.0f + (config.ArmourBonus ? speedPercent : 0) + config.Versus.SpeedPercent) / 100.0f;
	int32_t voiceIndex = !order && config.Slots[bot.Slot].Voice[0] ? UE3::FindNameIndex(config.Slots[bot.Slot].Voice) : -1;
	if (voiceIndex >= 0) {
		Field<UE3::FName>(pawn, look.FoxPawn_DialogPawnName) = UE3::FName{ voiceIndex, 0 };
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Game_AddDefaultInventory) + 16, 0);
	Field<UObject*>(parms.data(), off.AddDefaultInventory_P) = pawn;
	UE3::ProcessEvent(game, fn.Game_AddDefaultInventory, parms.data());
	return pawn;
}

static bool RandomSpot(const Bot& bot, const FVector& from, DWORD now, FVector& spot)
{
	for (int i = 0; i < GOAL_TRIES && !spots.empty(); i++) {
		const FVector& candidate = spots[rand() % spots.size()];
		if (Distance(candidate, from) >= ROAM_MIN_DISTANCE && !Banned(bot, candidate, now) && !InEnemyBase(bot, candidate)) {
			spot = candidate;
			return true;
		}
	}
	return false;
}

static std::vector<Visit>& TeamVisits(const Bot& bot)
{
	return visits[IsTeamVersus() && bot.Team == BLACKLIGHT_TEAM ? 1 : 0];
}

static void RecordVisit(const Bot& bot, const FVector& at, DWORD now)
{
	std::vector<Visit>& list = TeamVisits(bot);
	for (size_t i = list.size(); i-- > 0; ) {
		if (now - list[i].Tick > VISIT_FORGET_MS) {
			list.erase(list.begin() + i);
		}
	}
	if (list.size() >= VISIT_LIMIT) {
		list.erase(list.begin());
	}
	Visit visit = { at, now };
	list.push_back(visit);
}

static DWORD VisitAge(const Bot& bot, const FVector& place, DWORD now)
{
	DWORD age = VISIT_FORGET_MS;
	for (const Visit& visit : TeamVisits(bot)) {
		if (Distance(visit.At, place) < VISIT_RADIUS) {
			age = (std::min)(age, now - visit.Tick);
		}
	}
	return age;
}

static bool RoamGoal(const Bot& bot, const FVector& from, DWORD now, FVector& goal)
{
	bool found = false;
	DWORD best = 0;
	bool spawns = !spawn_spots.empty() && rand() % 100 < SPAWN_GOAL_PERCENT;
	for (int i = 0; i < ROAM_CANDIDATES; i++) {
		FVector candidate;
		if (spawns) {
			candidate = spawn_spots[rand() % spawn_spots.size()].Location;
			if (Distance(candidate, from) < ROAM_MIN_DISTANCE || Banned(bot, candidate, now) || InEnemyBase(bot, candidate)) {
				continue;
			}
		}
		else if (!RandomSpot(bot, from, now, candidate)) {
			continue;
		}
		DWORD age = VisitAge(bot, candidate, now);
		if (!found || age > best) {
			best = age;
			goal = candidate;
			found = true;
		}
	}
	return found || RandomSpot(bot, from, now, goal);
}

static bool SpawnPawn(UObject* game, Bot& bot, FVector location, const UE3::FRotator& rotation)
{
	DWORD now = GetTickCount();
	FVector spot;
	if (!StandsOnNavMesh(bot.Controller, location) && (FindFreeNavMeshSpot(bot.Controller, 0, location, NAVMESH_NUDGE_RADIUS, spot) || RandomSpot(bot, location, now, spot))) {
		location = spot;
	}
	UObject* pawn = GiveVersusPawn(game, bot, location, rotation);
	if (!pawn && FindFreeNavMeshSpot(bot.Controller, 0, location, GOAL_SNAP_RADIUS, spot)) {
		pawn = GiveVersusPawn(game, bot, spot, rotation);
	}
	if (!pawn || Field<UObject*>(bot.Controller, off.Controller_Pawn) != pawn) {
		return false;
	}
	ApplyBotSettings(bot.Controller, pawn, bot.Slot, config.Versus.Health, config.Versus.HealthRegen);
	Field<float>(bot.Controller, voff.AI_GetNextMoveMinDelay) = NEXT_MOVE_DELAY;
	UE3::SetBoolProperty(pawn, cls.CoverEnabledProperty, true);
	static bool pawnLogged = false;
	if (!pawnLogged) {
		pawnLogged = true;
		BOTS_LOG(LOG_LEVEL_INFO, "A bot pawn steps up to %.0f and walks floors with a normal Z of %.2f or more."
			, Field<float>(pawn, voff.Pawn_MaxStepHeight), Field<float>(pawn, voff.Pawn_WalkableFloorZ));
	}
	bot.LastMoveLocation = Field<FVector>(pawn, off.Actor_Location);
	bot.LastMoveTick = now;
	bot.DiedTick = 0;
	bot.HasGoal = false;
	bot.HoldUntilTick = 0;
	bot.Objective = 0;
	bot.HackStartTick = 0;
	bot.Flag = FLAG_TASK_NONE;
	bot.FlagActor = 0;
	bot.PathFailures = 0;
	bot.HasPost = false;
	bot.SightTarget = 0;
	bot.SightLostTick = 0;
	bot.FireFromTick = 0;
	bot.WarningShotsLeft = 0;
	bot.MissShot = false;
	EndCombatMove(bot);
	return true;
}

static DWORD RandomMs(DWORD low, DWORD high)
{
	return low + (DWORD)(rand() % (int)(high - low + 1));
}

// DebugState runs no code. The Dead state would destroy the controller and its PRI, with the bot's score.
static void Park(Bot& bot, DWORD died_tick, DWORD spawn_delay)
{
	GotoState(bot.Controller, debug_state);
	bot.DiedTick = died_tick | 1;
	bot.SpawnDelayMs = spawn_delay;
	bot.HasGoal = false;
	bot.Objective = 0;
	bot.HasPost = false;
	bot.HackStartTick = 0;
	EndCombatMove(bot);
}

static bool AddBot(UObject* game, UObject* spawner, uint8_t team, DWORD spawn_delay)
{
	int slot = BotsOnTeam(team) % SLOT_COUNT;
	UObject* wavePawn = 0;
	UObject* controller = SpawnBotController(spawner, slot, team, &wavePawn);
	Bot* bot = controller ? FindBot(controller) : 0;
	if (!bot) {
		return false;
	}
	FVector location = wavePawn ? Field<FVector>(wavePawn, off.Actor_Location) : FVector{};
	UE3::FRotator rotation = wavePawn ? Field<UE3::FRotator>(wavePawn, off.Actor_Rotation) : UE3::FRotator{};
	if (wavePawn) {
		DestroyActor(wavePawn);
	}
	const char* name = NameBot(game, *bot);
	int32_t spread = config.Versus.AggressionSpread;
	bot->Aggression = (std::max)(0, (std::min)(100, config.Versus.Aggression + (spread ? rand() % (2 * spread + 1) - spread : 0)));
	int32_t skillSpread = config.Versus.SkillSpread;
	bot->Skill = (std::max)(0, (std::min)(100, config.Versus.Skill + (skillSpread ? rand() % (2 * skillSpread + 1) - skillSpread : 0)));
	bot->UsesCover = rand() % 100 < config.Versus.CoverUse + (50 - bot->Aggression) / 2;
	SetUpBot(*bot);
	bool spawned = !spawn_delay && wavePawn && SpawnPawn(game, *bot, location, rotation);
	if (!spawned) {
		Park(*bot, GetTickCount() - (DWORD)(Field<float>(game, voff.Game_GameRespawnTime) * 1000.0f), spawn_delay);
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Added bot %s 0x%08x on team %u: %s, %s, %s, aggression %d, skill %d, %s, %s in %u ms."
		, name, bot->Controller, team, UE3::ObjectName(UE3::ObjectClass(bot->Controller)), config.Slots[slot].WeaponPreset
		, config.Slots[slot].LookName, bot->Aggression, bot->Skill, bot->UsesCover ? "takes cover" : "no cover", spawned ? "spawned" : "spawns at a player start"
		, spawned ? 0 : spawn_delay);
	return true;
}

static void FillTeam(UObject* game, UObject* spawner, uint8_t team, int32_t wanted, bool can_remove, bool all_at_once)
{
	int32_t have = BotsOnTeam(team);
	wanted = wanted < 0 ? 0 : wanted;
	DWORD delay = 0;
	while (have < wanted) {
		if (!AddBot(game, spawner, team, delay)) {
			return;
		}
		have++;
		if (!all_at_once) {
			return;
		}
		delay += IsRoundVersus() ? 0 : RandomMs(SPAWN_STAGGER_MIN_MS, SPAWN_STAGGER_MAX_MS);
	}
	if (have <= wanted || !can_remove) {
		return;
	}
	size_t victim = bots.size();
	for (size_t i = 0; i < bots.size(); i++) {
		if (bots[i].Team == team && (victim == bots.size() || bots[i].DiedTick)) {
			victim = i;
		}
	}
	if (victim < bots.size()) {
		RemoveBot(victim);
	}
}

static void FillPlaces(UObject* game, bool all_at_once)
{
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	UObject* spawner = humans.empty() ? (Server::IsDedicatedWorld(game) ? game : 0) : humans[0];
	if (!spawner) {
		return;
	}
	int32_t maxPlayers = Field<int32_t>(game, off.GameInfo_MaxPlayers);
	int32_t chosen = config.Versus.BotCount;
	if (!IsTeamVersus()) {
		int32_t players = chosen >= 0 ? (int32_t)humans.size() + chosen : config.Versus.DeathmatchPlayers;
		int32_t wanted = (std::min)(players, maxPlayers) - (int32_t)humans.size();
		FillTeam(game, spawner, 0, wanted, humans.size() >= 2, all_at_once);
		return;
	}
	TArray<UObject*>& teams = Field<TArray<UObject*> >(game, off.Game_Teams);
	if (teams.Num <= BLACKLIGHT_TEAM || !teams.Data[ORDER_TEAM] || !teams.Data[BLACKLIGHT_TEAM]) {
		return;
	}
	int32_t orderHumans = Field<int32_t>(teams.Data[ORDER_TEAM], voff.Team_HumanSize);
	int32_t blacklightHumans = Field<int32_t>(teams.Data[BLACKLIGHT_TEAM], voff.Team_HumanSize);
	int32_t perTeam = (std::min)(chosen >= 0 ? ((int32_t)humans.size() + chosen + 1) / 2 : config.Versus.TeamPlayers, maxPlayers / 2);
	bool canRemove = Lobby::KeepsPublicMatch() || (orderHumans > 0 && blacklightHumans > 0);
	FillTeam(game, spawner, ORDER_TEAM, perTeam - orderHumans, canRemove, all_at_once);
	FillTeam(game, spawner, BLACKLIGHT_TEAM, perTeam - blacklightHumans, canRemove, all_at_once);
}

static void Respawn(UObject* game, Bot& bot, DWORD now)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.MP_ChoosePlayerStart) + 16, 0);
	Field<UObject*>(parms.data(), voff.ChoosePlayerStart_Player) = bot.Controller;
	Field<uint8_t>(parms.data(), off.ChoosePlayerStart_InTeam) = bot.Team;
	UE3::ProcessEvent(game, fn.MP_ChoosePlayerStart, parms.data());
	UObject* start = Field<UObject*>(parms.data(), voff.ChoosePlayerStart_ReturnValue);
	FVector location = {};
	UE3::FRotator rotation = {};
	FVector spot;
	UE3::FRotator spot_rotation;
	if (Spawns::TakeSpot(bot.Controller, spot, spot_rotation) && SpawnPawn(game, bot, spot, spot_rotation)) {
		GotoState(bot.Controller, choosing_behavior_state);
		return;
	}
	if (start) {
		location = Field<FVector>(start, off.Actor_Location);
		rotation = Field<UE3::FRotator>(start, off.Actor_Rotation);
	}
	else if (!RandomSpot(bot, location, now, location)) {
		return;
	}
	if (!SpawnPawn(game, bot, location, rotation)) {
		bot.DiedTick = now | 1;
		bot.SpawnDelayMs = 0;
		BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x found no place to spawn at (%.0f %.0f %.0f), it tries again.", bot.Controller, location.X, location.Y, location.Z);
		return;
	}
	GotoState(bot.Controller, choosing_behavior_state);
}

static void OnPawnDied(UObject* controller)
{
	Bot* bot = FindBot(controller);
	if (bot) {
		Park(*bot, GetTickCount(), RandomMs(0, RESPAWN_JITTER_MS));
	}
}

static void RefreshSpots(UObject* controller, DWORD now)
{
	UObject* handle = Field<UObject*>(controller, off.Controller_NavigationHandle);
	if (!handle) {
		return;
	}
	spawn_spots.clear();
	UObject* world = Field<UObject*>(controller, off.Actor_WorldInfo);
	int listed = 0;
	for (UObject* point = world ? Field<UObject*>(world, voff.WorldInfo_NavigationPointList) : 0; point && listed < NavMesh::LIST_LIMIT; point = Field<UObject*>(point, voff.NavigationPoint_nextNavigationPoint), listed++) {
		if (UE3::IsA(point, ref.PlayerStart) && StandsOnNavMesh(controller, Field<FVector>(point, off.Actor_Location))) {
			SpawnSpot spawn = { Field<FVector>(point, off.Actor_Location), Field<int32_t>(point, voff.PlayerStart_TeamIndex) };
			spawn_spots.push_back(spawn);
		}
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Handle_GetAllPolyCenters) + 16, 0);
	Field<FVector>(parms.data(), voff.PolyCenters_Extent) = FVector{ MAP_EXTENT, MAP_EXTENT, MAP_EXTENT };
	UE3::ProcessNativeEvent(handle, ref.Handle_GetAllPolyCenters, parms.data());
	TArray<FVector>& found = Field<TArray<FVector> >(parms.data(), voff.PolyCenters_Out);
	spots.clear();
	if (found.Data) {
		spots.assign(found.Data, found.Data + found.Num);
		UE3::FreeArray(&found);
	}
	spots_tick = now;
}

static UObject* OwnSquad(UObject* game, const Bot& bot)
{
	TArray<UObject*>& teams = Field<TArray<UObject*> >(game, off.Game_Teams);
	if (bot.Team >= teams.Num || !teams.Data[bot.Team]) {
		return 0;
	}
	TArray<UObject*>& squads = Field<TArray<UObject*> >(teams.Data[bot.Team], off.Team_Squads);
	for (int32_t i = 0; i < squads.Num; i++) {
		if (squads.Data[i] && Field<UObject*>(squads.Data[i], off.Squad_SquadLeader) == bot.Controller) {
			return squads.Data[i];
		}
	}
	return 0;
}

static float Latest(const TArray<float>& times)
{
	float latest = -1.0f;
	for (int32_t i = 0; i < times.Num; i++) {
		latest = (std::max)(latest, times.Data[i]);
	}
	return latest;
}

static bool HuntGoal(const Bot& bot, UObject* squad, const FVector& location, float world_time, FVector& goal, float& when)
{
	if (!squad) {
		return false;
	}
	TArray<uint8_t>& enemies = Field<TArray<uint8_t> >(squad, voff.Squad_EnemyInfoList);
	bool found = false;
	float best = 0.0f;
	for (int32_t i = 0; i < enemies.Num; i++) {
		uint8_t* info = enemies.Data + i * ENEMY_INFO_SIZE;
		const FVector& known = Field<FVector>(info, voff.EnemyInfo_KnownLocation);
		if (!IsAlive(Field<UObject*>(info, voff.EnemyInfo_TargetPawn)) || (known.X == 0.0f && known.Y == 0.0f && known.Z == 0.0f)
			|| Distance(known, bot.LastHunt) < GOAL_REACHED || InEnemyBase(bot, known)) {
			continue;
		}
		float latest = (std::max)(Latest(Field<TArray<float> >(info, voff.EnemyInfo_LastSeenTimes)), Latest(Field<TArray<float> >(info, voff.EnemyInfo_LastEngagedTimes)));
		if (latest <= 0.0f || world_time - latest > HuntSeconds(bot)) {
			continue;
		}
		float distance = Distance(location, known);
		if (!found || distance < best) {
			best = distance;
			goal = known;
			when = latest;
			found = true;
		}
	}
	return found;
}

static bool IsEnemyPawn(const Bot& bot, UObject* other)
{
	UObject* controller = other && IsAlive(other) ? Field<UObject*>(other, off.Pawn_Controller) : 0;
	UObject* pri = controller ? Field<UObject*>(controller, voff.Controller_PlayerReplicationInfo) : 0;
	if (!pri || controller == bot.Controller) {
		return false;
	}
	UObject* team = Field<UObject*>(pri, voff.PRI_Team);
	return !(IsTeamVersus() && team && Field<int32_t>(team, voff.TeamInfo_TeamIndex) == bot.Team);
}

void OnNoise(UObject* controller, UObject* instigator)
{
	Bot* bot = FindBot(controller);
	if (bot && instigator && UE3::IsA(instigator, cls.Pawn) && IsEnemyPawn(*bot, instigator)) {
		bot->NoiseFrom = Field<FVector>(instigator, off.Actor_Location);
		bot->NoiseTick = GetTickCount() | 1;
	}
}

static UObject* NearestEnemy(UObject* game, const Bot& bot, UObject* pawn)
{
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	const FVector& location = Field<FVector>(pawn, off.Actor_Location);
	UObject* nearest = 0;
	float best = 0.0f;
	for (UObject* other = world ? Field<UObject*>(world, off.WorldInfo_PawnList) : 0; other; other = Field<UObject*>(other, off.Pawn_NextPawn)) {
		if (other == pawn || !IsEnemyPawn(bot, other)) {
			continue;
		}
		float distance = Distance(location, Field<FVector>(other, off.Actor_Location));
		if (!nearest || distance < best) {
			best = distance;
			nearest = other;
		}
	}
	return nearest;
}

static bool SeekGoal(UObject* game, const Bot& bot, UObject* pawn, DWORD now, FVector& goal)
{
	UObject* enemy = NearestEnemy(game, bot, pawn);
	if (!enemy) {
		return false;
	}
	const FVector& at = Field<FVector>(enemy, off.Actor_Location);
	std::vector<const FVector*> candidates;
	for (const FVector& spot : spots) {
		if (Distance(spot, at) <= SEEK_RADIUS && !Banned(bot, spot, now) && !InEnemyBase(bot, spot)) {
			candidates.push_back(&spot);
		}
	}
	if (candidates.empty()) {
		return false;
	}
	goal = *candidates[rand() % candidates.size()];
	return true;
}

static void ControlPoints(UObject* game, std::vector<UObject*>& points)
{
	TArray<UObject*>& list = Field<TArray<UObject*> >(game, voff.CPGame_ControlPoints);
	for (int32_t i = 0; i < list.Num; i++) {
		if (list.Data[i] && UE3::IsValidObject(list.Data[i])) {
			points.push_back(list.Data[i]);
		}
	}
}

static TArray<UObject*>* PresentPawns(UObject* point, int32_t team)
{
	TArray<uint8_t>& infos = Field<TArray<uint8_t> >(point, voff.CP_CaptureInfo);
	if (team < 0 || team >= infos.Num) {
		return 0;
	}
	return &Field<TArray<UObject*> >(infos.Data + team * PRESENT_TEAM_INFO_SIZE, voff.PresentTeam_Pawns);
}

static bool EnemiesAt(UObject* point, int32_t team)
{
	TArray<uint8_t>& infos = Field<TArray<uint8_t> >(point, voff.CP_CaptureInfo);
	for (int32_t i = 0; i < infos.Num; i++) {
		if (i != team && PresentPawns(point, i)->Num > 0) {
			return true;
		}
	}
	return false;
}

static bool Encompasses(UObject* volume, const FVector& point)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Volume_EncompassesPoint) + 16, 0);
	Field<FVector>(parms.data(), voff.Encompasses_Loc) = point;
	UE3::ProcessNativeEvent(volume, ref.Volume_EncompassesPoint, parms.data());
	return (Field<uint32_t>(parms.data(), voff.Encompasses_ReturnValue) & 1) != 0;
}

static void SyncPresence(UObject* game, const Bot& bot, UObject* pawn, const FVector& location)
{
	static bool logged = false;
	std::vector<UObject*> points;
	ControlPoints(game, points);
	for (UObject* point : points) {
		UObject* volume = Field<UObject*>(point, voff.Use_UseVolume);
		TArray<UObject*>* present = PresentPawns(point, bot.Team);
		if (!volume || !present) {
			continue;
		}
		bool listed = std::find(present->Data, present->Data + present->Num, pawn) != present->Data + present->Num;
		bool inside = Encompasses(volume, location);
		if (inside == listed) {
			continue;
		}
		if (!logged) {
			logged = true;
			BOTS_LOG(LOG_LEVEL_INFO, "The use volume of %s %s bot 0x%08x; the module %s it.", UE3::ObjectName(point)
				, inside ? "did not list" : "still listed", bot.Controller, inside ? "adds" : "removes");
		}
		UObject* function = inside ? ref.CP_PawnEntered : ref.CP_PawnLeaving;
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(function) + 16, 0);
		Field<UObject*>(parms.data(), inside ? voff.PawnEntered_EnteringPawn : voff.PawnLeaving_LeavingPawn) = pawn;
		UE3::ProcessEvent(point, function, parms.data());
	}
}

static bool PointChangingHands(UObject* point, int32_t team)
{
	const char* state = UE3::NameEntryText(Field<UE3::FName>(point, voff.CP_CurrentStateName).Index);
	int32_t owner = Field<int32_t>(point, voff.CP_CPOwner);
	int32_t capturing = Field<int32_t>(point, voff.CP_CurrentCapturingTeam);
	return strcmp(state, "Contested") == 0 || (strcmp(state, "Uncapturing") == 0 && owner == team) || (strcmp(state, "Capturing") == 0 && capturing == team)
		|| (owner == team && EnemiesAt(point, team));
}

static int PointTier(UObject* point, int32_t team)
{
	int32_t owner = Field<int32_t>(point, voff.CP_CPOwner);
	if (owner == -1) {
		return 0;
	}
	return PointChangingHands(point, team) || owner != team ? 1 : 2;
}

static int TeammatesAt(const Bot& bot, UObject* point)
{
	int count = 0;
	for (const Bot& other : bots) {
		count += (&other != &bot && other.Team == bot.Team && other.Objective == point) ? 1 : 0;
	}
	return count;
}

static UObject* ChoosePoint(UObject* game, const Bot& bot, const FVector& location, DWORD now, UObject* current)
{
	std::vector<UObject*> points;
	ControlPoints(game, points);
	std::vector<UObject*> best;
	int bestTier = 3;
	for (UObject* point : points) {
		const FVector& at = Field<FVector>(point, off.Actor_Location);
		if (InEnemyBase(bot, at) || Banned(bot, at, now)) {
			continue;
		}
		int tier = PointTier(point, bot.Team);
		if (tier < bestTier) {
			best.clear();
			bestTier = tier;
		}
		if (tier == bestTier) {
			best.push_back(point);
		}
	}
	if (best.empty()) {
		return 0;
	}
	if (current && std::find(best.begin(), best.end(), current) != best.end()) {
		return current;
	}
	std::sort(best.begin(), best.end(), [&](UObject* a, UObject* b) {
		int ta = TeammatesAt(bot, a);
		int tb = TeammatesAt(bot, b);
		if (ta != tb) {
			return ta < tb;
		}
		return Distance(location, Field<FVector>(a, off.Actor_Location)) < Distance(location, Field<FVector>(b, off.Actor_Location));
	});
	return rand() % 100 < CP_NEAREST_PERCENT ? best[0] : best[rand() % best.size()];
}

static DWORD ArrivalHoldMs(Bot& bot)
{
	if (mode == MODE_RETRIEVAL) {
		return bot.Flag == FLAG_TASK_CARRY_HOME ? CTF_CARRY_WAIT_MS : (bot.Flag == FLAG_TASK_GUARD ? CTF_GUARD_MS : (bot.WaitsOnArrival ? HoldMs(bot) : 0));
	}
	if (!bot.Objective) {
		return bot.WaitsOnArrival ? HoldMs(bot) : 0;
	}
	bot.Defending = Field<int32_t>(bot.Objective, voff.CP_CPOwner) == bot.Team;
	return bot.Defending ? HoldMs(bot) + CP_DEFEND_HOLD_MS : CP_CAPTURE_HOLD_MS;
}

static bool HackAllowed(UObject* point, int32_t team)
{
	if (Field<int32_t>(point, voff.CP_PointHackedTeam) != -1) {
		return false;
	}
	TArray<UObject*>* own = PresentPawns(point, team);
	if (EnemiesAt(point, team) && own && own->Num > 0) {
		return false;
	}
	const char* state = UE3::NameEntryText(Field<UE3::FName>(point, voff.CP_CurrentStateName).Index);
	int32_t capturing = Field<int32_t>(point, voff.CP_CurrentCapturingTeam);
	if (strcmp(state, "Uncontrolled") == 0) {
		return true;
	}
	if (strcmp(state, "Capturing") == 0) {
		return capturing == team;
	}
	if (strcmp(state, "Captured") == 0 || strcmp(state, "Uncapturing") == 0) {
		return capturing != team;
	}
	return false;
}

static bool HackPoint(Bot& bot, UObject* pawn, UObject* point)
{
	char path[160];
	sprintf_s(path, sizeof(path), "FoxGame.FoxGameObject_ControlPoint.%s.OnSuccessfulUse", UE3::NameEntryText(Field<UE3::FName>(point, voff.CP_CurrentStateName).Index));
	UObject* function = UE3::FindObject(path);
	if (!function) {
		return false;
	}
	Field<UObject*>(point, voff.Use_CurrentlyUsingPawn) = pawn;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(function) + 16, 0);
	UE3::ProcessEvent(point, function, parms.data());
	BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x hacked %s 0x%08x after %u ms.", bot.Controller, UE3::ObjectName(point), point, bot.HackMs);
	return true;
}

static void UpdateHack(Bot& bot, UObject* pawn, UObject* point, bool inside, DWORD now)
{
	if (config.Versus.HackSeconds <= 0.0f || !inside || !HackAllowed(point, bot.Team)) {
		bot.HackStartTick = 0;
		return;
	}
	if (!bot.HackStartTick) {
		bot.HackStartTick = now | 1;
		bot.HackMs = (DWORD)(config.Versus.HackSeconds * 1000.0f * (0.8f + (float)(rand() % 41) / 100.0f));
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x starts hacking %s 0x%08x, done in %u ms.", bot.Controller, UE3::ObjectName(point), point, bot.HackMs);
		return;
	}
	if (now - bot.HackStartTick >= bot.HackMs) {
		if (!HackPoint(bot, pawn, point)) {
			BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x found no hack function for the %s state of %s.", bot.Controller
				, UE3::NameEntryText(Field<UE3::FName>(point, voff.CP_CurrentStateName).Index), UE3::ObjectName(point));
		}
		bot.HackStartTick = 0;
	}
}

static bool FindDefencePost(const Bot& bot, UObject* pawn, const FVector& point, FVector& post)
{
	UObject* world = Field<UObject*>(pawn, off.Actor_WorldInfo);
	std::vector<FVector> candidates;
	for (UObject* link = world ? Field<UObject*>(world, off.WorldInfo_CoverList) : 0; link; link = Field<UObject*>(link, off.CoverLink_NextCoverLink)) {
		if (Distance(Field<FVector>(link, off.Actor_Location), point) > COVER_LINK_REACH) {
			continue;
		}
		int32_t count = Field<TArray<uint8_t> >(link, off.CoverLink_Slots).Num;
		for (int32_t slot = 0; slot < count; slot++) {
			FVector at = FoxAI::GetSlotLocation(link, slot);
			float away = Distance(at, point);
			if (away < CP_POST_MIN || away > CP_POST_RADIUS) {
				continue;
			}
			float dx = at.X - point.X;
			float dy = at.Y - point.Y;
			float flat = sqrtf(dx * dx + dy * dy);
			float yaw = (float)FoxAI::GetSlotRotation(link, slot).Yaw * ROTATOR_TO_RADIANS;
			if (flat < 1.0f || (cosf(yaw) * dx + sinf(yaw) * dy) / flat < CP_POST_FACING) {
				continue;
			}
			FVector eye = { at.X, at.Y, at.Z + CP_POST_EYE };
			FVector pad = { point.X, point.Y, point.Z + CP_POST_EYE };
			if (!IsFreeSpot(pawn, at, pawn) || !CanClaimCover(pawn, link, slot) || !StandsOnNavMesh(bot.Controller, at) || !ClearLine(pawn, eye, pad)) {
				continue;
			}
			candidates.push_back(at);
		}
	}
	if (candidates.empty()) {
		return false;
	}
	post = candidates[rand() % candidates.size()];
	return true;
}

static bool TakePost(Bot& bot, UObject* pawn, DWORD now)
{
	FVector post;
	const FVector at = Field<FVector>(bot.Objective, off.Actor_Location);
	if (bot.HasPost || !FindDefencePost(bot, pawn, at, post)) {
		return false;
	}
	SetGoToSprint(bot, false);
	SetGoToAbortable(bot, true);
	SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, post);
	bot.HasPost = true;
	bot.HasGoal = true;
	bot.Goal = post;
	bot.GoalTick = now;
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x takes a post at (%.0f %.0f %.0f), %.0f from %s.", bot.Controller, post.X, post.Y, post.Z, Distance(post, at), UE3::ObjectName(bot.Objective));
	return true;
}

static void HoldPoint(Bot& bot, UObject* pawn, const FVector& location, DWORD now)
{
	if (!UE3::IsValidObject(bot.Objective)) {
		bot.Objective = 0;
		bot.HasPost = false;
		bot.HoldUntilTick = 0;
		bot.HackStartTick = 0;
		return;
	}
	if (!bot.Defending && Field<int32_t>(bot.Objective, voff.CP_CPOwner) == bot.Team) {
		bot.Defending = true;
		bot.HoldUntilTick = (now + HoldMs(bot) + CP_DEFEND_HOLD_MS) | 1;
		if (TakePost(bot, pawn, now)) {
			return;
		}
	}
	bool contested = PointChangingHands(bot.Objective, bot.Team);
	if (contested && (int32_t)(bot.HoldUntilTick - now) < (int32_t)CP_HOLD_EXTEND_MS) {
		bot.HoldUntilTick = (now + CP_HOLD_EXTEND_MS) | 1;
	}
	const FVector at = Field<FVector>(bot.Objective, off.Actor_Location);
	UObject* volume = Field<UObject*>(bot.Objective, voff.Use_UseVolume);
	bool outside = volume && !Encompasses(volume, location);
	UpdateHack(bot, pawn, bot.Objective, !outside, now);
	if (bot.HasPost) {
		if (!contested) {
			return;
		}
		bot.HasPost = false;
		bot.HasGoal = false;
	}
	FVector goal;
	if ((Distance(location, at) > CP_RETURN_DISTANCE || outside) && IsIdle(bot.Controller)
		&& (FindFreeNavMeshSpotToward(bot.Controller, pawn, at, CP_STAND_RADIUS, location, goal) || FindFreeNavMeshSpot(bot.Controller, pawn, at, GOAL_SNAP_RADIUS, goal))
		&& Distance(goal, location) > COMBAT_MOVE_MIN) {
		SetGoToSprint(bot, false);
		SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, goal);
	}
}

static uint8_t GameStatus(UObject* game)
{
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	UObject* gri = world ? Field<UObject*>(world, voff.WorldInfo_GRI) : 0;
	return gri ? Field<uint8_t>(gri, voff.GRI_GameStatus) : 0;
}

static UObject* GameGRI(UObject* game)
{
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	return world ? Field<UObject*>(world, voff.WorldInfo_GRI) : 0;
}

static void OnRoundEnd(UObject* game)
{
	round_scored = false;
	for (Bot& bot : bots) {
		UObject* pawn = Field<UObject*>(bot.Controller, off.Controller_Pawn);
		if (IsAlive(pawn)) {
			DestroyActor(pawn);
		}
		Park(bot, GetTickCount(), 0);
	}
	BOTS_LOG(LOG_LEVEL_INFO, "%s", "Round over, the bots wait for the next one.");
}

static void OnRoundStart(UObject* game)
{
	if (GameStatus(game) != GAME_STATUS_ROUND_IN_PROGRESS) {
		return;
	}
	round_scored = false;
	DWORD now = GetTickCount();
	int spawned = 0;
	for (Bot& bot : bots) {
		if (!IsAlive(Field<UObject*>(bot.Controller, off.Controller_Pawn))) {
			Respawn(game, bot, now);
			spawned++;
		}
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Round started, %d bots spawned.", spawned);
}

static int32_t AliveOnTeam(UObject* game, uint8_t team, int32_t* humans_alive)
{
	int32_t alive = 0;
	*humans_alive = 0;
	TArray<UObject*>& teams = Field<TArray<UObject*> >(game, off.Game_Teams);
	UObject* teamInfo = team < teams.Num ? teams.Data[team] : 0;
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	for (UObject* human : humans) {
		UObject* pri = Field<UObject*>(human, voff.Controller_PlayerReplicationInfo);
		if (IsAlive(Field<UObject*>(human, off.Controller_Pawn)) && (!IsTeamVersus() || (pri && Field<UObject*>(pri, voff.PRI_Team) == teamInfo))) {
			alive++;
			(*humans_alive)++;
		}
	}
	for (const Bot& bot : bots) {
		if ((!IsTeamVersus() || bot.Team == team) && IsAlive(Field<UObject*>(bot.Controller, off.Controller_Pawn))) {
			alive++;
		}
	}
	return alive;
}

// Last Team Standing counts only players as remaining, so a team of bots would lose each round at its first death.
static void FixTeamRemaining(UObject* game)
{
	UObject* gri = GameGRI(game);
	if (!gri) {
		return;
	}
	int32_t humans;
	Field<int32_t>(gri, voff.GRI_LTS_Team1Remaining) = AliveOnTeam(game, ORDER_TEAM, &humans);
	Field<int32_t>(gri, voff.GRI_LTS_Team2Remaining) = AliveOnTeam(game, BLACKLIGHT_TEAM, &humans);
}

static void FixRemaining(UObject* game)
{
	UObject* gri = GameGRI(game);
	if (!gri) {
		return;
	}
	int32_t humans;
	Field<int32_t>(gri, voff.GRI_LMS_RemainingPlayers) = AliveOnTeam(game, ORDER_TEAM, &humans);
}

static void ScoreLastBot(UObject* game)
{
	UObject* gri = GameGRI(game);
	if (round_scored || !gri || GameStatus(game) != GAME_STATUS_ROUND_IN_PROGRESS || Field<int32_t>(gri, voff.GRI_LMS_RemainingPlayers) != 1) {
		return;
	}
	int32_t humansAlive;
	if (AliveOnTeam(game, ORDER_TEAM, &humansAlive) != 1 || humansAlive) {
		return;
	}
	for (Bot& bot : bots) {
		if (IsAlive(Field<UObject*>(bot.Controller, off.Controller_Pawn))) {
			std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.LMS_RoundScoring) + 16, 0);
			Field<UObject*>(parms.data(), voff.RoundScoring_RoundWinner) = bot.Controller;
			UE3::ProcessEvent(game, ref.LMS_RoundScoring, parms.data());
			round_scored = true;
			BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x is the last one standing and takes the round.", bot.Controller);
			return;
		}
	}
}

static UObject* FlagUsableBy(UObject* game, int32_t usable_by_team)
{
	UObject* flag = Field<UObject*>(game, usable_by_team == 1 ? voff.CTF_RedFlag : voff.CTF_BlueFlag);
	return flag && UE3::IsValidObject(flag) ? flag : 0;
}

static UObject* EnemyFlag(UObject* game, uint8_t team)
{
	return FlagUsableBy(game, team + 1);
}

static UObject* OwnFlag(UObject* game, uint8_t team)
{
	return FlagUsableBy(game, 2 - team);
}

static void RememberBases(UObject* game)
{
	for (uint8_t team = 0; team < 2; team++) {
		UObject* flag = OwnFlag(game, team);
		if (flag) {
			flag_base[team] = Field<FVector>(flag, voff.Carry_SpawnLocation);
			flag_base_known[team] = true;
		}
	}
}

static bool CallPawnCheck(UObject* object, UObject* function, int32_t pawn_offset, int32_t result_offset, UObject* pawn)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(function) + 16, 0);
	Field<UObject*>(parms.data(), pawn_offset) = pawn;
	UE3::ProcessEvent(object, function, parms.data());
	return (Field<uint32_t>(parms.data(), result_offset) & 1) != 0;
}

static bool FlagGoal(UObject* game, Bot& bot, UObject* pawn, int team_index, FVector& target, const char*& reason)
{
	UObject* carried = Field<UObject*>(pawn, voff.FoxPawn_CarriedObject);
	if (carried && UE3::IsValidObject(carried)) {
		if (!flag_base_known[bot.Team]) {
			return false;
		}
		bot.Flag = FLAG_TASK_CARRY_HOME;
		bot.FlagActor = carried;
		target = flag_base[bot.Team];
		reason = "carries the flag home";
		return true;
	}
	UObject* own = OwnFlag(game, bot.Team);
	UObject* ownCarrier = own ? Field<UObject*>(own, voff.Carry_CarriedByPawn) : 0;
	if (own && IsAlive(ownCarrier)) {
		if (team_index < CTF_CHASERS) {
			bot.Flag = FLAG_TASK_CHASE_CARRIER;
			bot.FlagActor = own;
			target = Field<FVector>(own, off.Actor_Location);
			reason = "chases the carrier of its flag";
			return true;
		}
	}
	else if (own && !UE3::GetBoolProperty(own, ref.FlagAtBaseProperty) && team_index == 0) {
		bot.Flag = FLAG_TASK_RETURN;
		bot.FlagActor = own;
		target = Field<FVector>(own, off.Actor_Location);
		reason = "returns its flag";
		return true;
	}
	UObject* enemy = EnemyFlag(game, bot.Team);
	UObject* enemyCarrier = enemy ? Field<UObject*>(enemy, voff.Carry_CarriedByPawn) : 0;
	if (team_index == 0) {
		if (!flag_base_known[bot.Team]) {
			return false;
		}
		bot.Flag = FLAG_TASK_GUARD;
		bot.FlagActor = own;
		target = flag_base[bot.Team];
		reason = "guards its flag";
		return true;
	}
	if (IsAlive(enemyCarrier) && !IsEnemyPawn(bot, enemyCarrier)) {
		if (team_index <= CTF_ESCORTS) {
			bot.Flag = FLAG_TASK_ESCORT;
			bot.FlagActor = enemy;
			target = Field<FVector>(enemy, off.Actor_Location);
			reason = "escorts the flag carrier";
			return true;
		}
		return false;
	}
	if (enemy && !enemyCarrier) {
		bot.Flag = FLAG_TASK_ATTACK;
		bot.FlagActor = enemy;
		target = Field<FVector>(enemy, off.Actor_Location);
		reason = "goes for the enemy flag";
		return true;
	}
	return false;
}

static bool FlagGoalStale(const Bot& bot, UObject* pawn)
{
	UObject* carried = Field<UObject*>(pawn, voff.FoxPawn_CarriedObject);
	if (carried && bot.Flag != FLAG_TASK_CARRY_HOME) {
		return true;
	}
	if ((bot.Flag == FLAG_TASK_CHASE_CARRIER || bot.Flag == FLAG_TASK_ESCORT) && bot.FlagActor && UE3::IsValidObject(bot.FlagActor)
		&& Distance(bot.Goal, Field<FVector>(bot.FlagActor, off.Actor_Location)) > FLAG_MOVED) {
		return true;
	}
	return false;
}

static void TouchFlags(UObject* game, Bot& bot, UObject* pawn, const FVector& location)
{
	UObject* carried = Field<UObject*>(pawn, voff.FoxPawn_CarriedObject);
	UObject* own = OwnFlag(game, bot.Team);
	if (carried && UE3::IsValidObject(carried)) {
		if (own && UE3::GetBoolProperty(own, ref.FlagAtBaseProperty) && Distance(location, Field<FVector>(own, off.Actor_Location)) < FLAG_TOUCH) {
			std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Flag_Capture) + 16, 0);
			UE3::ProcessEvent(carried, ref.Flag_Capture, parms.data());
			BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x captured the flag.", bot.Controller);
		}
		return;
	}
	UObject* enemy = EnemyFlag(game, bot.Team);
	if (enemy && !Field<UObject*>(enemy, voff.Carry_CarriedByPawn) && Distance(location, Field<FVector>(enemy, off.Actor_Location)) < FLAG_TOUCH
		&& CallPawnCheck(enemy, ref.GameObject_CanInteractWith, voff.CanInteract_InteractingPawn, voff.CanInteract_ReturnValue, pawn)
		&& CallPawnCheck(enemy, ref.Carry_IsPickupTouchPossible, voff.TouchPossible_PlayerTouched, voff.TouchPossible_ReturnValue, pawn)
		&& !CallPawnCheck(enemy, ref.Carry_InTeamSpawn, voff.InTeamSpawn_PawnToCheck, voff.InTeamSpawn_ReturnValue, pawn)) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Flag_Pickup) + 16, 0);
		Field<UObject*>(parms.data(), voff.Pickup_PickedUpPawn) = pawn;
		UE3::ProcessEvent(enemy, ref.Flag_Pickup, parms.data());
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x picked up the enemy flag.", bot.Controller);
		return;
	}
	if (own && !UE3::GetBoolProperty(own, ref.FlagAtBaseProperty) && !Field<UObject*>(own, voff.Carry_CarriedByPawn)
		&& Distance(location, Field<FVector>(own, off.Actor_Location)) < FLAG_TOUCH) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Flag_ReturnToBase) + 16, 0);
		Field<UObject*>(parms.data(), voff.ReturnToBase_ReturningPRI) = Field<UObject*>(bot.Controller, voff.Controller_PlayerReplicationInfo);
		UE3::ProcessEvent(own, ref.Flag_ReturnToBase, parms.data());
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x returned its flag.", bot.Controller);
	}
}

static UObject* TraceFrom(UObject* actor, const FVector& start, const FVector& end, FVector* hit_location, FVector* hit_normal, const FVector* extent = 0)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Actor_Trace) + 16, 0);
	Field<FVector>(parms.data(), voff.Trace_TraceStart) = start;
	Field<FVector>(parms.data(), voff.Trace_TraceEnd) = end;
	Field<uint32_t>(parms.data(), voff.Trace_bTraceActors) = 1;
	if (extent) {
		Field<FVector>(parms.data(), voff.Trace_Extent) = *extent;
		Field<int32_t>(parms.data(), voff.Trace_ExtraTraceFlags) = TRACEFLAG_BLOCKING;
	}
	UE3::ProcessNativeEvent(actor, ref.Actor_Trace, parms.data());
	if (hit_location) {
		*hit_location = Field<FVector>(parms.data(), voff.Trace_HitLocation);
	}
	if (hit_normal) {
		*hit_normal = Field<FVector>(parms.data(), voff.Trace_HitNormal);
	}
	return Field<UObject*>(parms.data(), voff.Trace_ReturnValue);
}

static FVector TurretSidestep(const Bot& bot, UObject* turret, const FVector& location)
{
	const FVector& at = Field<FVector>(turret, off.Actor_Location);
	float tx = at.X - location.X;
	float ty = at.Y - location.Y;
	float tl = sqrtf(tx * tx + ty * ty);
	if (tl <= 1.0f) {
		return location;
	}
	tx /= tl;
	ty /= tl;
	float sideX = -ty;
	float sideY = tx;
	if (sideX * (bot.Goal.X - location.X) + sideY * (bot.Goal.Y - location.Y) < 0.0f) {
		sideX = -sideX;
		sideY = -sideY;
	}
	return { location.X + sideX * TURRET_SIDESTEP - tx * UNSNAG_STEP, location.Y + sideY * TURRET_SIDESTEP - ty * UNSNAG_STEP, location.Z };
}

static bool IsTurret(UObject* actor)
{
	return actor && strcmp(UE3::ObjectName(UE3::ObjectClass(actor)), "FoxPawn_Turret") == 0;
}

static float FloorDropAhead(UObject* pawn, const FVector& location, float dx, float dy, float distance)
{
	float feet = location.Z - PAWN_HALF_HEIGHT;
	FVector start = { location.X + dx * distance, location.Y + dy * distance, feet + PAWN_STEP + 10.0f };
	FVector end = { start.X, start.Y, feet - FLOOR_PROBE_DEPTH };
	FVector hit;
	UObject* floor = TraceFrom(pawn, start, end, &hit, 0);
	return floor ? feet - hit.Z : FLOOR_PROBE_DEPTH;
}

static bool Unsnag(Bot& bot, UObject* pawn, const FVector& location, DWORD now)
{
	static const float HEIGHTS[3] = { 45.0f, 96.0f, 150.0f };
	FVector push = Field<FVector>(pawn, voff.Actor_Acceleration);
	float length = sqrtf(push.X * push.X + push.Y * push.Y);
	if (length < 1.0f) {
		return false;
	}
	float feet = location.Z - PAWN_HALF_HEIGHT;
	for (float height : HEIGHTS) {
		FVector start = { location.X, location.Y, feet + height };
		FVector end = { start.X + push.X / length * UNSNAG_REACH, start.Y + push.Y / length * UNSNAG_REACH, start.Z };
		FVector hit;
		FVector normal;
		UObject* wall = TraceFrom(pawn, start, end, &hit, &normal);
		float side = sqrtf(normal.X * normal.X + normal.Y * normal.Y);
		bool byPawn = wall && UE3::IsA(wall, cls.Pawn);
		if (!wall || (!byPawn && side < 0.3f)) {
			continue;
		}
		FVector spot = byPawn ? FVector{ location.X - push.Y / length * SIDESTEP, location.Y + push.X / length * SIDESTEP, location.Z }
			: FVector{ location.X + normal.X / side * UNSNAG_STEP, location.Y + normal.Y / side * UNSNAG_STEP, location.Z };
		if (IsTurret(wall)) {
			spot = TurretSidestep(bot, wall, location);
		}
		bot.Unsnags++;
		bot.LastUnsnagTick = now | 1;
		const FVector next = Field<FVector>(bot.Controller, voff.AI_LastGetNextMove);
		float nextFlat = sqrtf((next.X - location.X) * (next.X - location.X) + (next.Y - location.Y) * (next.Y - location.Y));
		float hitFlat = sqrtf((hit.X - location.X) * (hit.X - location.X) + (hit.Y - location.Y) * (hit.Y - location.Y));
		if (!byPawn && nextFlat > hitFlat) {
			float tx = -normal.Y / side;
			float ty = normal.X / side;
			if (tx * (bot.Goal.X - location.X) + ty * (bot.Goal.Y - location.Y) < 0.0f) {
				tx = -tx;
				ty = -ty;
			}
			FVector slide = { spot.X + tx * WALL_SLIDE, spot.Y + ty * WALL_SLIDE, location.Z };
			if (IsFreeSpot(pawn, slide, pawn, STEP_CLEAR) && StandsOnNavMesh(bot.Controller, slide) && MovePawnToSpot(pawn, slide)) {
				SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
				BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x caught on %s (%s) %.0f ahead at (%.0f %.0f %.0f) with its next point behind it, and slid along it."
					, bot.Controller, UE3::ObjectName(wall), UE3::ObjectName(UE3::ObjectClass(wall)), hitFlat, location.X, location.Y, location.Z);
				return true;
			}
		}
		if (!IsFreeSpot(pawn, spot, pawn, byPawn ? SIDESTEP_CLEAR : STEP_CLEAR) || !StandsOnNavMesh(bot.Controller, spot) || !MovePawnToSpot(pawn, spot)) {
			return false;
		}
		SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x caught on %s (%s) %.0f ahead and %.0f above its feet at (%.0f %.0f %.0f), and stepped %s."
			, bot.Controller, UE3::ObjectName(wall), UE3::ObjectName(UE3::ObjectClass(wall)), Distance(start, hit), height, location.X, location.Y, location.Z
			, byPawn ? "aside" : "off it");
		return true;
	}
	FVector across = { -push.Y / length, push.X / length, 0.0f };
	for (float sign : { 1.0f, -1.0f }) {
		FVector start = { location.X + across.X * sign * UNSNAG_SIDE, location.Y + across.Y * sign * UNSNAG_SIDE, feet + HEIGHTS[0] };
		FVector end = { start.X + push.X / length * UNSNAG_REACH, start.Y + push.Y / length * UNSNAG_REACH, start.Z };
		FVector hit;
		FVector normal;
		UObject* wall = TraceFrom(pawn, start, end, &hit, &normal);
		if (!wall || UE3::IsA(wall, cls.Pawn) || sqrtf(normal.X * normal.X + normal.Y * normal.Y) < 0.3f) {
			continue;
		}
		FVector spot = { location.X - across.X * sign * UNSNAG_STEP, location.Y - across.Y * sign * UNSNAG_STEP, location.Z };
		bot.Unsnags++;
		bot.LastUnsnagTick = now | 1;
		if (!IsFreeSpot(pawn, spot, pawn, STEP_CLEAR) || !StandsOnNavMesh(bot.Controller, spot) || !MovePawnToSpot(pawn, spot)) {
			return false;
		}
		SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x caught its %s side on %s (%s) at (%.0f %.0f %.0f), and stepped away from it."
			, bot.Controller, sign > 0.0f ? "right" : "left", UE3::ObjectName(wall), UE3::ObjectName(UE3::ObjectClass(wall)), location.X, location.Y, location.Z);
		return true;
	}
	const FVector next = Field<FVector>(bot.Controller, voff.AI_LastGetNextMove);
	float tx = next.X - location.X;
	float ty = next.Y - location.Y;
	float flat = sqrtf(tx * tx + ty * ty);
	if (flat > 1.0f && flat < LEDGE_REACH && next.Z < location.Z
		&& (location.Z - next.Z > LEDGE_DROP || FloorDropAhead(pawn, location, tx / flat, ty / flat, LEDGE_STEP) > PAWN_STEP)) {
		FVector spot = { location.X + tx / flat * LEDGE_STEP, location.Y + ty / flat * LEDGE_STEP, location.Z };
		bot.Unsnags++;
		bot.LastUnsnagTick = now | 1;
		if (IsFreeSpot(pawn, spot, pawn, STEP_CLEAR) && MovePawnToSpot(pawn, spot)) {
			SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
			BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x stood at a ledge at (%.0f %.0f %.0f) with its next point %.0f lower, and stepped off it."
				, bot.Controller, location.X, location.Y, location.Z, location.Z - next.Z);
			return true;
		}
	}
	FVector box = { UNSNAG_BOX_RADIUS, UNSNAG_BOX_RADIUS, UNSNAG_BOX_HALF_HEIGHT };
	FVector start = { location.X, location.Y, feet + UNSNAG_BOX_BOTTOM + UNSNAG_BOX_HALF_HEIGHT };
	FVector end = { start.X + push.X / length * UNSNAG_REACH, start.Y + push.Y / length * UNSNAG_REACH, start.Z };
	FVector hit;
	FVector normal;
	UObject* wall = TraceFrom(pawn, start, end, &hit, &normal, &box);
	float side = sqrtf(normal.X * normal.X + normal.Y * normal.Y);
	bool turret = IsTurret(wall);
	if (!wall || (!turret && (UE3::IsA(wall, cls.Pawn) || side < 0.3f))) {
		return false;
	}
	FVector spot = turret ? TurretSidestep(bot, wall, location)
		: FVector{ location.X + normal.X / side * UNSNAG_STEP, location.Y + normal.Y / side * UNSNAG_STEP, location.Z };
	bot.Unsnags++;
	bot.LastUnsnagTick = now | 1;
	if (!IsFreeSpot(pawn, spot, pawn, STEP_CLEAR) || !StandsOnNavMesh(bot.Controller, spot) || !MovePawnToSpot(pawn, spot)) {
		return false;
	}
	SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x caught its body on %s (%s) %.0f ahead at (%.0f %.0f %.0f) and stepped %s. A line trace misses that mesh."
		, bot.Controller, UE3::ObjectName(wall), UE3::ObjectName(UE3::ObjectClass(wall)), Distance(start, hit), location.X, location.Y, location.Z
		, turret ? "past it" : "off it");
	return true;
}

static bool SnapToNavMesh(Bot& bot, UObject* pawn, const FVector& location, DWORD now, bool before_decision = false)
{
	if (!before_decision && bot.LastSnapTick && now - bot.LastSnapTick < SNAP_AGAIN_MS && Distance(location, bot.LastSnapLocation) < ESCAPE_DISTANCE) {
		return false;
	}
	for (float reach = SNAP_STEP; reach <= SNAP_REACH; reach += SNAP_STEP) {
		for (int i = 0; i < 8; i++) {
			float dx = cosf(i * 3.14159265f / 4.0f);
			float dy = sinf(i * 3.14159265f / 4.0f);
			FVector spot = { location.X + dx * reach, location.Y + dy * reach, location.Z };
			if (!StandsOnNavMesh(bot.Controller, spot) || NavMesh::InModeCut(spot)) {
				continue;
			}
			FVector inside = { spot.X + dx * SNAP_INSIDE, spot.Y + dy * SNAP_INSIDE, spot.Z };
			if (StandsOnNavMesh(bot.Controller, inside) && !NavMesh::InModeCut(inside)) {
				spot = inside;
			}
			if (!IsFreeSpot(pawn, spot, pawn, STEP_CLEAR) || !MovePawnToSpot(pawn, spot)) {
				continue;
			}
			if (!before_decision) {
				bot.LastSnapTick = now | 1;
				bot.LastSnapLocation = location;
			}
			if (!before_decision || config.Trace) {
				BOTS_LOG(before_decision ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO, "Bot 0x%08x stood %.0f off the navmesh at (%.0f %.0f %.0f) and stepped back onto it%s."
					, bot.Controller, Distance(location, spot), location.X, location.Y, location.Z, before_decision ? " before it decided" : "");
			}
			return true;
		}
	}
	return false;
}

static void LogBlocker(const Bot& bot, UObject* pawn, const FVector& location)
{
	static const float HEIGHTS[5] = { 20.0f, 45.0f, 96.0f, 150.0f, 185.0f };
	static const char* const PARTS[5] = { "shin", "knee", "chest", "head", "top" };
	static const float REACH = 80.0f;
	FVector push = Field<FVector>(pawn, voff.Actor_Acceleration);
	if (fabsf(push.X) + fabsf(push.Y) < 1.0f && bot.HasGoal) {
		push = { bot.Goal.X - location.X, bot.Goal.Y - location.Y, 0.0f };
	}
	float length = sqrtf(push.X * push.X + push.Y * push.Y);
	if (length < 1.0f) {
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x is stuck at (%.0f %.0f %.0f) and pushes nowhere.", bot.Controller, location.X, location.Y, location.Z);
		return;
	}
	auto trace = [&](const FVector& start, const FVector& end, float& distance) -> UObject* {
		FVector hit;
		UObject* actor = TraceFrom(pawn, start, end, &hit, 0);
		distance = Distance(start, hit);
		return actor;
	};
	char text[640] = {};
	size_t used = 0;
	float feet = location.Z - PAWN_HALF_HEIGHT;
	for (int i = 0; i < 5; i++) {
		FVector start = { location.X, location.Y, feet + HEIGHTS[i] };
		FVector end = { start.X + push.X / length * REACH, start.Y + push.Y / length * REACH, start.Z };
		float distance = 0.0f;
		UObject* hit = trace(start, end, distance);
		used += sprintf_s(text + used, sizeof(text) - used, hit ? "%s%s %s (%s) at %.0f" : "%s%s clear", i ? ", " : "", PARTS[i]
			, hit ? UE3::ObjectName(hit) : "", hit ? UE3::ObjectName(UE3::ObjectClass(hit)) : "", distance);
	}
	float distance = 0.0f;
	UObject* above = trace({ location.X, location.Y, feet + 100.0f }, { location.X, location.Y, feet + 2.0f * PAWN_HALF_HEIGHT + 10.0f }, distance);
	FVector box = { UNSNAG_BOX_RADIUS, UNSNAG_BOX_RADIUS, UNSNAG_BOX_HALF_HEIGHT };
	FVector boxStart = { location.X, location.Y, feet + UNSNAG_BOX_BOTTOM + UNSNAG_BOX_HALF_HEIGHT };
	FVector boxEnd = { boxStart.X + push.X / length * REACH, boxStart.Y + push.Y / length * REACH, boxStart.Z };
	FVector boxHit;
	UObject* body = TraceFrom(pawn, boxStart, boxEnd, &boxHit, 0, &box);
	used += sprintf_s(text + used, sizeof(text) - used, body ? ", body %s (%s) at %.0f" : ", body clear"
		, body ? UE3::ObjectName(body) : "", body ? UE3::ObjectName(UE3::ObjectClass(body)) : "", Distance(boxStart, boxHit));
	used += sprintf_s(text + used, sizeof(text) - used, ", floor drop ahead %.0f %.0f %.0f", FloorDropAhead(pawn, location, push.X / length, push.Y / length, 32.0f)
		, FloorDropAhead(pawn, location, push.X / length, push.Y / length, 48.0f), FloorDropAhead(pawn, location, push.X / length, push.Y / length, 64.0f));
	const FVector waypoint = Field<FVector>(bot.Controller, voff.AI_LastGetNextMove);
	const FVector destination = Field<FVector>(bot.Controller, voff.AI_CurrentDestination);
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x is stuck at (%.0f %.0f %.0f) pushing toward (%.2f %.2f): %s; above %s%s%s at %.0f; next point (%.0f %.0f %.0f) %.0f away, destination (%.0f %.0f %.0f)."
		, bot.Controller, location.X, location.Y, location.Z, push.X / length, push.Y / length, text, above ? UE3::ObjectName(above) : "clear"
		, above ? " " : "", above ? UE3::ObjectName(UE3::ObjectClass(above)) : "", above ? 100.0f + distance : 0.0f
		, waypoint.X, waypoint.Y, waypoint.Z, Distance(location, waypoint), destination.X, destination.Y, destination.Z);
}

static void Nudge(Bot& bot, UObject* pawn, const FVector& location, float min_distance, const char* reason, DWORD now)
{
	bool farAway = bot.LastNudgeTick && now - bot.LastNudgeTick < NUDGE_ESCALATE_MS && Distance(location, bot.LastNudgeLocation) < ESCAPE_DISTANCE;
	std::vector<FVector> spots;
	if (!farAway) {
		FindNavMeshSpots(bot.Controller, location, NAVMESH_NUDGE_RADIUS, spots);
	}
	FVector spot;
	bool found = false;
	for (size_t i = 0; i < spots.size() && !found; i++) {
		if (Distance(spots[i], location) >= min_distance && IsFreeSpot(pawn, spots[i], pawn) && StandsOnNavMesh(bot.Controller, spots[i])) {
			spot = spots[i];
			found = true;
		}
	}
	FVector random;
	if (!found && (!RandomSpot(bot, location, now, random) || !FindFreeNavMeshSpot(bot.Controller, pawn, random, GOAL_SNAP_RADIUS, spot))) {
		return;
	}
	if (MovePawnToSpot(pawn, spot)) {
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x %s at (%.0f %.0f %.0f) and was moved %s to (%.0f %.0f %.0f)."
			, bot.Controller, reason, location.X, location.Y, location.Z, found ? "nearby" : "far away", spot.X, spot.Y, spot.Z);
		bot.LastMoveLocation = Field<FVector>(pawn, off.Actor_Location);
		bot.LastMoveTick = now;
		bot.LastNudgeTick = now | 1;
		bot.LastNudgeLocation = location;
		bot.HasGoal = false;
		bot.StalledGoals = 0;
	}
}

static bool FightsInTheOpen(UObject* controller)
{
	UObject* current = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	return current && UE3::IsA(current, ref.FightStationary);
}

static UObject* ActiveTarget(UObject* game, const Bot& bot)
{
	UObject* squad = OwnSquad(game, bot);
	int32_t index = Field<int32_t>(bot.Controller, voff.AI_ActiveTargetIndex);
	if (!squad || index < 0) {
		return 0;
	}
	TArray<uint8_t>& enemies = Field<TArray<uint8_t> >(squad, voff.Squad_EnemyInfoList);
	if (index >= enemies.Num) {
		return 0;
	}
	UObject* target = Field<UObject*>(enemies.Data + index * ENEMY_INFO_SIZE, voff.EnemyInfo_TargetPawn);
	return IsAlive(target) ? target : 0;
}

static bool FallBackSpot(const Bot& bot, const FVector& location, const FVector& target, DWORD now, FVector& spot)
{
	float distance = Distance(location, target);
	std::vector<const FVector*> candidates;
	for (const FVector& candidate : spots) {
		float away = Distance(candidate, location);
		if (away >= FALL_BACK_MIN && away <= FALL_BACK_MAX && Distance(candidate, target) >= distance + FALL_BACK_GAIN
			&& !Banned(bot, candidate, now) && !InEnemyBase(bot, candidate)) {
			candidates.push_back(&candidate);
		}
	}
	if (candidates.empty()) {
		return false;
	}
	spot = *candidates[rand() % candidates.size()];
	return true;
}

static void CombatMove(UObject* game, Bot& bot, UObject* pawn, const FVector& location, DWORD now)
{
	if (!config.Versus.CombatMoves || (int32_t)(now - bot.NextCombatMoveTick) < 0 || !FightsInTheOpen(bot.Controller)) {
		return;
	}
	bot.NextCombatMoveTick = now + COMBAT_MOVE_INTERVAL_MS + (DWORD)(rand() % COMBAT_MOVE_JITTER_MS);
	UObject* target = ActiveTarget(game, bot);
	if (!target) {
		return;
	}
	const FVector at = Field<FVector>(target, off.Actor_Location);
	float dx = at.X - location.X;
	float dy = at.Y - location.Y;
	float distance = sqrtf(dx * dx + dy * dy);
	if (distance < 1.0f) {
		return;
	}
	dx /= distance;
	dy /= distance;
	int32_t health = Field<int32_t>(pawn, off.Pawn_Health);
	int32_t healthMax = Field<int32_t>(pawn, off.Pawn_HealthMax);
	FVector point = location;
	const char* move = 0;
	DWORD length = COMBAT_MOVE_MAX_MS;
	bool holding = bot.Objective && bot.HoldUntilTick;
	if (!holding && bot.Aggression < 50 && healthMax > 0 && (float)health < (float)healthMax * FALL_BACK_HEALTH && FallBackSpot(bot, location, at, now, point)) {
		move = "falls back";
		length = FALL_BACK_MAX_MS;
	}
	else if (!holding && bot.Aggression >= 50 && distance > PushRange(bot)) {
		float step = (std::min)(PUSH_STEP, distance - PUSH_CLOSEST);
		point = { location.X + dx * step, location.Y + dy * step, location.Z };
		move = "pushes";
	}
	else if (!holding && bot.Aggression < 50 && distance < KeepRange(bot)) {
		point = { location.X - dx * BACK_OFF_STEP, location.Y - dy * BACK_OFF_STEP, location.Z };
		move = "backs off";
	}
	else if (rand() % 100 < DODGE_PERCENT) {
		float side = (float)(DODGE_MIN + rand() % (DODGE_MAX - DODGE_MIN + 1)) * (rand() % 2 ? 1.0f : -1.0f);
		point = { location.X - dy * side, location.Y + dx * side, location.Z };
		move = "steps aside";
	}
	FVector goal;
	if (!move || !FindFreeNavMeshSpot(bot.Controller, pawn, point, COMBAT_MOVE_SNAP, goal) || Distance(goal, location) < COMBAT_MOVE_MIN) {
		return;
	}
	SetGoToAbortable(bot, false);
	SetGoToSprint(bot, false);
	SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, goal);
	bot.CombatMoveEndTick = (now + length) | 1;
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x %s in a fight, from (%.0f %.0f %.0f) to (%.0f %.0f %.0f), its target %.0f away."
		, bot.Controller, move, location.X, location.Y, location.Z, goal.X, goal.Y, goal.Z, distance);
}

static void OnSightEvent(Bot& bot, const wchar_t* event_name, UObject* recipient)
{
	DWORD now = GetTickCount();
	if (wcscmp(event_name, TARGET_LOST_EVENT) == 0) {
		bot.SightLostTick = now | 1;
		return;
	}
	if (wcscmp(event_name, TARGET_SEEN_EVENT) != 0) {
		return;
	}
	bool fresh = recipient != bot.SightTarget || (bot.SightLostTick && now - bot.SightLostTick >= REACQUIRE_MS);
	bot.SightLostTick = 0;
	if (!fresh) {
		return;
	}
	bot.SightTarget = recipient;
	bot.SightTick = now | 1;
	bot.FireFromTick = (now + ReactionMs(bot)) | 1;
	bot.WarningShotsLeft = config.Versus.WarningShots;
}

static bool OnFireAmmunition(UObject* weapon)
{
	UObject* pawn = Field<UObject*>(weapon, voff.Actor_Instigator);
	UObject* controller = pawn ? Field<UObject*>(pawn, off.Pawn_Controller) : 0;
	Bot* bot = controller ? FindBot(controller) : 0;
	if (!bot) {
		return true;
	}
	DWORD now = GetTickCount();
	if (bot->FireFromTick) {
		if ((int32_t)(bot->FireFromTick - now) > 1) {
			return false;
		}
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x fires at 0x%08x %u ms after it came into view, %d warning shots first.", bot->Controller, bot->SightTarget, now - bot->SightTick, bot->WarningShotsLeft);
		bot->FireFromTick = 0;
	}
	bot->MissShot = bot->WarningShotsLeft > 0;
	if (bot->MissShot) {
		bot->WarningShotsLeft--;
	}
	return true;
}

static void OnAim(Bot& bot)
{
	if (config.Versus.FiringAngle <= 0.0f) {
		return;
	}
	float angle = FiringAngle(bot);
	float focus = config.Versus.FocusAngle;
	if (focus > angle && bot.SightTarget) {
		float part = (float)(GetTickCount() - bot.SightTick) / (float)FocusMs(bot);
		part = part < 0.0f ? 0.0f : (part > 1.0f ? 1.0f : part);
		angle = focus + (angle - focus) * part;
	}
	Field<float>(bot.Controller, off.AI_Accuracy) = angle;
}

static void OnAimed(Bot& bot, FFrame* stack, UE3::FRotator* aim)
{
	if (!bot.MissShot) {
		return;
	}
	bot.MissShot = false;
	UObject* target = current_game ? ActiveTarget(current_game, bot) : 0;
	if (!target) {
		return;
	}
	const FVector& start = Field<FVector>(stack->Locals, voff.Aim_StartFireLoc);
	const FVector& at = Field<FVector>(target, off.Actor_Location);
	float dx = at.X - start.X;
	float dy = at.Y - start.Y;
	float dz = at.Z - start.Z;
	float flat = sqrtf(dx * dx + dy * dy);
	if (flat < 1.0f) {
		return;
	}
	float side = WARNING_MISS_MIN + (float)(rand() % (int)(WARNING_MISS_MAX - WARNING_MISS_MIN + 1.0f));
	float yaw = atan2f(dy, dx) + (rand() % 2 ? 1.0f : -1.0f) * atan2f(side, flat);
	aim->Pitch = (int32_t)(atan2f(dz, flat) * RADIANS_TO_ROTATOR);
	aim->Yaw = (int32_t)(yaw * RADIANS_TO_ROTATOR);
	aim->Roll = 0;
}

// The game's death message function drops every death of an AI controller, bots included.
static bool OnDeathMessage(UObject* game, FFrame* stack)
{
	UObject* killer = Field<UObject*>(stack->Locals, voff.DeathMessage_Killer);
	UObject* other = Field<UObject*>(stack->Locals, voff.DeathMessage_Other);
	if (!other || !IsBot(other)) {
		return true;
	}
	std::vector<uint8_t> parms(BROADCAST_PARMS + 16, 0);
	Field<UObject*>(parms.data(), BROADCAST_SENDER) = game;
	Field<UObject*>(parms.data(), BROADCAST_MESSAGE) = ref.DeathMessageClass;
	Field<int32_t>(parms.data(), BROADCAST_SWITCH) = killer && killer != other ? DEATH_KILL : DEATH_SUICIDE;
	Field<UObject*>(parms.data(), BROADCAST_PRI_1) = killer ? Field<UObject*>(killer, voff.Controller_PlayerReplicationInfo) : 0;
	Field<UObject*>(parms.data(), BROADCAST_PRI_2) = Field<UObject*>(other, voff.Controller_PlayerReplicationInfo);
	Field<UObject*>(parms.data(), BROADCAST_OBJECT) = Field<UObject*>(stack->Locals, voff.DeathMessage_DamageType);
	UE3::ProcessEvent(game, ref.Game_BroadcastLocalized, parms.data());
	return false;
}

static const char* StartProblem(UObject* controller)
{
	UObject* handle = Field<UObject*>(controller, off.Controller_NavigationHandle);
	if (!handle) {
		return 0;
	}
	std::vector<uint8_t> findParms(UE3::FunctionParmsSize(ref.Handle_FindPylon) + 16, 0);
	UE3::ProcessNativeEvent(handle, ref.Handle_FindPylon, findParms.data());
	if (!(Field<uint32_t>(findParms.data(), voff.FindPylon_ReturnValue) & 1) || !Field<UObject*>(handle, voff.Handle_AnchorPylon)) {
		return NO_START_POLY;
	}
	std::vector<uint8_t> escapeParms(UE3::FunctionParmsSize(ref.Handle_IsAnchorInescapable) + 16, 0);
	UE3::ProcessNativeEvent(handle, ref.Handle_IsAnchorInescapable, escapeParms.data());
	return (Field<uint32_t>(escapeParms.data(), voff.IsAnchorInescapable_ReturnValue) & 1) ? "stood on a navmesh poly it cannot leave" : 0;
}

static void HoldJumpVelocity(Bot& bot, UObject* pawn, DWORD now)
{
	if (!bot.JumpUntilTick) {
		return;
	}
	if ((int32_t)(bot.JumpUntilTick - now) <= 0) {
		bot.JumpUntilTick = 0;
		return;
	}
	if (Field<uint8_t>(pawn, off.Actor_Physics) == PHYS_FALLING) {
		Field<FVector>(pawn, off.Actor_Velocity).X = bot.JumpVelocity.X;
		Field<FVector>(pawn, off.Actor_Velocity).Y = bot.JumpVelocity.Y;
	}
}

static void JumpAtLinks(UObject* game, Bot& bot, UObject* pawn, const FVector& location, DWORD now, const NavMesh::JumpLink* links, uint32_t link_count)
{
	uint8_t physics = Field<uint8_t>(pawn, off.Actor_Physics);
	const FVector velocity = Field<FVector>(pawn, off.Actor_Velocity);
	float speed = sqrtf(velocity.X * velocity.X + velocity.Y * velocity.Y);
	if (bot.JumpStartZ != 0.0f && now - bot.LastJumpTick >= JUMP_MEASURE_MS) {
		float seconds = (now - bot.LastJumpTick) / 1000.0f;
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x %.2f s after its jump: %.0f higher, vertical speed %.0f, physics %u, so gravity about %.0f.", bot.Controller, seconds, location.Z - bot.JumpStartZ, velocity.Z, physics, (bot.JumpSpeedZ - velocity.Z) / seconds);
		bot.JumpStartZ = 0.0f;
	}
	if (bot.LastJumpTick && now - bot.LastJumpTick < JUMP_AGAIN_MS) {
		return;
	}
	int32_t zone = -1;
	for (uint32_t i = 0; i < link_count; i++) {
		const NavMesh::JumpLink& link = links[i];
		float dx = link.From.X - location.X;
		float dy = link.From.Y - location.Y;
		if (dx * dx + dy * dy <= JUMP_TRIGGER * JUMP_TRIGGER) {
			zone = (int32_t)i;
		}
		float gx = link.To.X - link.From.X;
		float gy = link.To.Y - link.From.Y;
		float gap = sqrtf(gx * gx + gy * gy);
		float toward = gap < 1.0f || speed < 1.0f ? 0.0f : (velocity.X * gx + velocity.Y * gy) / (speed * gap);
		float floorGap = fabsf(link.From.Z - (location.Z - PAWN_HALF_HEIGHT));
		if (dx * dx + dy * dy <= JUMP_LOG_RADIUS * JUMP_LOG_RADIUS && now - bot.LastLinkLogTick >= JUMP_LOG_MS) {
			bot.LastLinkLogTick = now | 1;
			BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x near a link start, %.0f away: physics %u, speed %.0f, toward %.2f, floor %.0f off.", bot.Controller, sqrtf(dx * dx + dy * dy), physics, speed, toward, floorGap);
		}
		if ((physics != PHYS_WALKING && physics != PHYS_NAVMESHWALKING) || speed < JUMP_MIN_SPEED || zone != (int32_t)i || bot.LinkZone == zone || floorGap > JUMP_FLOOR_WINDOW) {
			continue;
		}
		float rise = link.To.Z - link.From.Z;
		if (gap < 1.0f || toward < 0.5f || (rise <= JUMP_STEP && rise < -0.5f * gap)) {
			continue;
		}
		bot.LastJumpTick = now | 1;
		bot.LinkZone = zone;
		UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
		float gravity = world ? fabsf(Field<float>(world, voff.WorldInfo_DefaultGravityZ)) : 0.0f;
		gravity = gravity < 1.0f ? DEFAULT_GRAVITY : gravity;
		float jumpZ = Field<float>(pawn, voff.Pawn_JumpZ);
		float sprint = Field<float>(pawn, look.Pawn_GroundSpeed);
		sprint = sprint < 100.0f ? 900.0f : sprint;
		float square = jumpZ * jumpZ - 2.0f * gravity * rise;
		float flight = square > 0.0f ? (jumpZ + sqrtf(square)) / gravity : 0.0f;
		float ground = flight > 0.0f ? gap / flight : sprint + 1.0f;
		if (ground > sprint) {
			flight = gap / sprint;
			ground = sprint;
			jumpZ = (rise + 0.5f * gravity * flight * flight) / flight;
		}
		if (jumpZ > JUMP_MAX_LIFT * Field<float>(pawn, voff.Pawn_JumpZ)) {
			BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x cannot jump %.0f up over %.0f to (%.0f %.0f %.0f). The jump needs a vertical speed of %.0f.", bot.Controller, rise, gap, link.To.X, link.To.Y, link.To.Z, jumpZ);
			return;
		}
		FVector start = { link.From.X, link.From.Y, location.Z };
		if (!MovePawnToSpot(pawn, start)) {
			start = location;
		}
		Field<UE3::FRotator>(pawn, off.Actor_Rotation).Yaw = (int32_t)(atan2f(gy, gx) * RADIANS_TO_ROTATOR);
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Pawn_DoJump) + 16, 0);
		UE3::ProcessEvent(pawn, ref.Pawn_DoJump, parms.data());
		bool jumped = Field<uint32_t>(parms.data(), voff.DoJump_ReturnValue) != 0 && Field<FVector>(pawn, off.Actor_Velocity).Z > 1.0f;
		if (!jumped) {
			std::vector<uint8_t> physicsParms(UE3::FunctionParmsSize(ref.Actor_SetPhysics) + 16, 0);
			Field<uint8_t>(physicsParms.data(), voff.SetPhysics_NewPhysics) = PHYS_FALLING;
			UE3::ProcessEvent(pawn, ref.Actor_SetPhysics, physicsParms.data());
		}
		FVector& velocity = Field<FVector>(pawn, off.Actor_Velocity);
		velocity.X = gx / gap * ground;
		velocity.Y = gy / gap * ground;
		velocity.Z = jumpZ;
		if (Field<float>(pawn, voff.Pawn_AirSpeed) < ground) {
			Field<float>(pawn, voff.Pawn_AirSpeed) = ground;
		}
		bot.JumpVelocity = { velocity.X, velocity.Y, 0.0f };
		bot.JumpUntilTick = (now + (DWORD)(flight * 1000.0f) + JUMP_HOLD_EXTRA_MS) | 1;
		bot.JumpStartZ = start.Z;
		bot.JumpSpeedZ = jumpZ;
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x %s at (%.0f %.0f %.0f) toward (%.0f %.0f %.0f): %.0f up over %.0f, up at %.0f, %.2f s in the air at %.0f."
			, bot.Controller, jumped ? "jumps" : "is thrown", start.X, start.Y, start.Z, link.To.X, link.To.Y, link.To.Z, rise, gap, jumpZ, flight, ground);
		return;
	}
	bot.LinkZone = zone;
}

static bool HopOver(Bot& bot, UObject* pawn, const FVector& location, const FVector& push, float length, const FVector& front, DWORD now)
{
	if (bot.LastJumpTick && now - bot.LastJumpTick < JUMP_AGAIN_MS) {
		return false;
	}
	float feet = location.Z - PAWN_HALF_HEIGHT;
	FVector dir = { push.X / length, push.Y / length, 0.0f };
	for (float height : { HOP_CHEST, HOP_HEAD }) {
		FVector start = { location.X, location.Y, feet + height };
		FVector end = { start.X + dir.X * WALL_STEP_REACH * 2.0f, start.Y + dir.Y * WALL_STEP_REACH * 2.0f, start.Z };
		if (TraceFrom(pawn, start, end, 0, 0)) {
			return false;
		}
	}
	const FVector& next = Field<FVector>(bot.Controller, voff.AI_LastGetNextMove);
	float nx = next.X - location.X;
	float ny = next.Y - location.Y;
	float gap = sqrtf(nx * nx + ny * ny);
	if (gap < WALL_STEP_REACH || gap > HOP_REACH || (nx * dir.X + ny * dir.Y) < 0.7f * gap || next.Z - location.Z > HOP_RISE) {
		return false;
	}
	FVector back;
	FVector from = { next.X, next.Y, front.Z };
	if (!TraceFrom(pawn, from, front, &back, 0) || Distance(front, back) > HOP_THICKNESS) {
		return false;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Pawn_DoJump) + 16, 0);
	UE3::ProcessEvent(pawn, ref.Pawn_DoJump, parms.data());
	if (Field<uint32_t>(parms.data(), voff.DoJump_ReturnValue) == 0 || Field<FVector>(pawn, off.Actor_Velocity).Z <= 1.0f) {
		return false;
	}
	FVector& velocity = Field<FVector>(pawn, off.Actor_Velocity);
	velocity.X = nx / gap * HOP_SPEED;
	velocity.Y = ny / gap * HOP_SPEED;
	if (Field<float>(pawn, voff.Pawn_AirSpeed) < HOP_SPEED) {
		Field<float>(pawn, voff.Pawn_AirSpeed) = HOP_SPEED;
	}
	bot.LastJumpTick = now | 1;
	bot.JumpVelocity = { velocity.X, velocity.Y, 0.0f };
	bot.JumpUntilTick = (now + HOP_FLIGHT_MS) | 1;
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x jumps over a low wall at (%.0f %.0f %.0f) toward (%.0f %.0f %.0f).", bot.Controller, location.X, location.Y, location.Z, next.X, next.Y, next.Z);
	return true;
}

static void StepOffWall(Bot& bot, UObject* pawn, const FVector& location, DWORD now)
{
	uint8_t physics = Field<uint8_t>(pawn, off.Actor_Physics);
	if (!bot.HasGoal || bot.CombatMoveEndTick || (physics != PHYS_WALKING && physics != PHYS_NAVMESHWALKING) || now - bot.LastWallStepTick < WALL_STEP_MS) {
		return;
	}
	if (now - bot.WallStepWindowTick >= WALL_STEP_WINDOW_MS) {
		bot.WallStepWindowTick = now;
		bot.WallSteps = 0;
	}
	const FVector velocity = Field<FVector>(pawn, off.Actor_Velocity);
	FVector push = Field<FVector>(pawn, voff.Actor_Acceleration);
	float length = sqrtf(push.X * push.X + push.Y * push.Y);
	if (bot.WallSteps >= WALL_STEP_LIMIT || length < 1.0f || velocity.X * velocity.X + velocity.Y * velocity.Y >= WALL_STEP_SPEED * WALL_STEP_SPEED) {
		return;
	}
	static const float HEIGHTS[2] = { 45.0f, 96.0f };
	float feet = location.Z - PAWN_HALF_HEIGHT;
	for (float height : HEIGHTS) {
		FVector start = { location.X, location.Y, feet + height };
		FVector end = { start.X + push.X / length * WALL_STEP_REACH, start.Y + push.Y / length * WALL_STEP_REACH, start.Z };
		FVector hit;
		FVector normal;
		UObject* wall = TraceFrom(pawn, start, end, &hit, &normal);
		float side = sqrtf(normal.X * normal.X + normal.Y * normal.Y);
		if (!wall || UE3::IsA(wall, cls.Pawn) || side < 0.3f) {
			continue;
		}
		bot.LastWallStepTick = now | 1;
		if (height == HEIGHTS[0] && HopOver(bot, pawn, location, push, length, hit, now)) {
			return;
		}
		FVector spot = { location.X + normal.X / side * WALL_STEP, location.Y + normal.Y / side * WALL_STEP, location.Z };
		if (MovePawnToSpot(pawn, spot)) {
			bot.WallSteps++;
			if (config.Trace) {
				BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x walked into %s at (%.0f %.0f %.0f) and stepped off it.", bot.Controller, UE3::ObjectName(UE3::ObjectClass(wall)), location.X, location.Y, location.Z);
			}
		}
		return;
	}
}

static bool StepToAnchor(Bot& bot, UObject* pawn, const FVector& location, const char* problem)
{
	float minDistance = problem == NO_START_POLY ? 0.0f : ANCHOR_STEP_MIN;
	std::vector<FVector> spots;
	FindNavMeshSpots(bot.Controller, location, ANCHOR_STEP_RADIUS, spots);
	int tries = 0;
	bool moved = false;
	for (const FVector& spot : spots) {
		if (tries >= ANCHOR_STEP_TRIES) {
			break;
		}
		if (Distance(spot, location) < minDistance || !IsFreeSpot(pawn, spot, pawn, STEP_CLEAR)) {
			continue;
		}
		tries++;
		if (!MovePawnToSpot(pawn, spot)) {
			continue;
		}
		moved = true;
		if (!StartProblem(bot.Controller)) {
			BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x %s at (%.0f %.0f %.0f) and stepped %.0f to the navmesh at (%.0f %.0f %.0f)."
				, bot.Controller, problem, location.X, location.Y, location.Z, Distance(spot, location), spot.X, spot.Y, spot.Z);
			return true;
		}
	}
	if (moved) {
		MovePawnToSpot(pawn, location);
	}
	return false;
}

static void KeepOnNavMesh(Bot& bot, UObject* pawn, DWORD now)
{
	if (!bot.HasGoal || bot.CombatMoveEndTick || now - bot.LastMeshCheckTick < ON_MESH_CHECK_MS) {
		return;
	}
	bot.LastMeshCheckTick = now | 1;
	uint8_t physics = Field<uint8_t>(pawn, off.Actor_Physics);
	FVector location = Field<FVector>(pawn, off.Actor_Location);
	if ((physics != PHYS_WALKING && physics != PHYS_NAVMESHWALKING) || IsFighting(bot.Controller)) {
		return;
	}
	if (!StandsOnNavMesh(bot.Controller, location)) {
		SnapToNavMesh(bot, pawn, location, now, true);
		return;
	}
	if (now - bot.LastAnchorCheckTick < ANCHOR_CHECK_MS) {
		return;
	}
	bot.LastAnchorCheckTick = now | 1;
	const char* problem = StartProblem(bot.Controller);
	if (problem) {
		StepToAnchor(bot, pawn, location, problem);
	}
}

void OnFrame()
{
	static bool announced = false;
	if (!current_game) {
		return;
	}
	const std::vector<NavMesh::JumpLink>& links = NavMesh::Links();
	uint32_t count = (uint32_t)links.size();
	bool versus = mode != MODE_NONE && mode != MODE_BLACK_OPS;
	if (!count && !versus) {
		return;
	}
	DWORD now = GetTickCount();
	if (count && !announced) {
		announced = true;
		BOTS_LOG(LOG_LEVEL_INFO, "The link checks run every frame for %u links.", count);
	}
	for (Bot& bot : bots) {
		UObject* pawn = Field<UObject*>(bot.Controller, off.Controller_Pawn);
		if (!IsAlive(pawn) || bot.DiedTick) {
			continue;
		}
		FVector location = Field<FVector>(pawn, off.Actor_Location);
		HoldJumpVelocity(bot, pawn, now);
		if (count) {
			JumpAtLinks(current_game, bot, pawn, location, now, links.data(), count);
		}
		if (versus) {
			StepOffWall(bot, pawn, location, now);
			KeepOnNavMesh(bot, pawn, now);
		}
	}
}

static void Think(UObject* game, Bot& bot, UObject* pawn, DWORD now, float world_time, int team_index)
{
	bot.LastThinkTick = now | 1;
	FVector location = Field<FVector>(pawn, off.Actor_Location);
	uint8_t physics = Field<uint8_t>(pawn, off.Actor_Physics);
	bool stepped = false;
	if ((physics == PHYS_WALKING || physics == PHYS_NAVMESHWALKING) && !bot.CombatMoveEndTick && !IsFighting(bot.Controller)
		&& !StandsOnNavMesh(bot.Controller, location) && SnapToNavMesh(bot, pawn, location, now, true)) {
		location = Field<FVector>(pawn, off.Actor_Location);
		stepped = true;
	}
	if (stepped && bot.PathFailures && bot.HasGoal
		&& (!bot.StartRetryTick || now - bot.StartRetryTick >= START_RETRY_MS || Distance(location, bot.StartRetryLocation) >= ESCAPE_DISTANCE)) {
		bot.StartRetryTick = now | 1;
		bot.StartRetryLocation = location;
		bot.PathFailures = 0;
		SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
		bot.GoalTick = now;
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x found no path while off the navmesh, and searches again from (%.0f %.0f %.0f)."
			, bot.Controller, location.X, location.Y, location.Z);
		return;
	}
	if (Distance(location, bot.LastMoveLocation) >= STUCK_MOVE) {
		bot.LastMoveLocation = location;
		bot.LastMoveTick = now;
		bot.StalledGoals = 0;
		bot.ZeroMoves = 0;
		bot.Unsnags = 0;
	}
	bool still = now - bot.LastMoveTick >= GOAL_PROGRESS_MS;
	for (size_t i = bot.GoalBans.size(); i-- > 0; ) {
		if ((int32_t)(bot.GoalBans[i].UntilTick - now) <= 0) {
			bot.GoalBans.erase(bot.GoalBans.begin() + i);
		}
	}
	UObject* squad = OwnSquad(game, bot);
	if (squad) {
		Field<float>(squad, off.Squad_EnemyNotVisibleRemoveTime) = FORGET_ENEMY_SECONDS;
	}
	if (bot.PathFailures) {
		const char* startProblem = bot.HasGoal ? StartProblem(bot.Controller) : 0;
		if (bot.HasGoal) {
			BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x found no path from (%.0f %.0f %.0f) to (%.0f %.0f %.0f), %.0f away."
				, bot.Controller, location.X, location.Y, location.Z, bot.Goal.X, bot.Goal.Y, bot.Goal.Z, Distance(location, bot.Goal));
			if (Distance(location, bot.FailSpot) < STUCK_MOVE) {
				bot.FailsHere++;
			}
			else {
				bot.FailSpot = location;
				bot.FailsHere = 1;
			}
			if (!startProblem && bot.FailsHere >= 2) {
				startProblem = "found no path to two goals from";
			}
			if (!startProblem) {
				GoalBan ban = { bot.Goal, now + GOAL_BAN_MS };
				bot.GoalBans.push_back(ban);
			}
			bot.StalledGoals += still ? 1 : 0;
		}
		bot.HasGoal = false;
		bot.PathFailures = 0;
		if (NavMesh::InModeCut(location) && SnapToNavMesh(bot, pawn, location, now)) {
			SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
			bot.HasGoal = true;
			bot.GoalTick = now;
			return;
		}
		if (startProblem) {
			bot.FailsHere = 0;
			if (startProblem == NO_START_POLY && SnapToNavMesh(bot, pawn, location, now)) {
				SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, bot.Goal);
				bot.HasGoal = true;
				bot.GoalTick = now;
				return;
			}
			if (config.Trace) {
				LogBlocker(bot, pawn, location);
			}
			Nudge(bot, pawn, location, ESCAPE_DISTANCE, startProblem, now);
			return;
		}
	}
	if (bot.CombatMoveEndTick && ((int32_t)(now - bot.CombatMoveEndTick) >= 0 || !RunsCommand(bot.Controller))) {
		EndCombatMove(bot);
	}
	bool fighting = IsFighting(bot.Controller);
	if (bot.WasFighting && !fighting) {
		StopFiring(pawn);
	}
	bot.WasFighting = fighting;
	LeaveStaleCover(bot, pawn, now);
	if (fighting) {
		bot.HasGoal = false;
		if (!bot.Objective) {
			bot.HoldUntilTick = 0;
		}
		CombatMove(game, bot, pawn, location, now);
		return;
	}
	if (bot.CombatMoveEndTick) {
		return;
	}
	if (bot.HasGoal && now - bot.LastMoveTick >= UNSNAG_MS && bot.Unsnags < UNSNAG_LIMIT && now - bot.LastUnsnagTick >= UNSNAG_MS
		&& Unsnag(bot, pawn, location, now)) {
		return;
	}
	if (still && !StandsOnNavMesh(bot.Controller, location)) {
		if (!SnapToNavMesh(bot, pawn, location, now)) {
			Nudge(bot, pawn, location, 0.0f, "stood off the navmesh", now);
		}
		return;
	}
	bool stalled = bot.HasGoal && still && now - bot.GoalTick >= GOAL_PROGRESS_MS;
	if (stalled && Distance(location, bot.Goal) >= GOAL_REACHED) {
		if ((bot.Objective || bot.Flag != FLAG_TASK_NONE) && bot.ObjectiveStalls == 0) {
			bot.ObjectiveStalls = 1;
			bot.StallResume = true;
			if (config.Trace) {
				LogBlocker(bot, pawn, location);
			}
			Nudge(bot, pawn, location, ESCAPE_DISTANCE, "stopped on its way to an objective", now);
			return;
		}
		GoalBan ban = { bot.Goal, now + GOAL_BAN_MS };
		bot.GoalBans.push_back(ban);
		bot.StalledGoals++;
	}
	if (bot.StalledGoals >= STALLED_GOAL_LIMIT) {
		if (config.Trace) {
			LogBlocker(bot, pawn, location);
		}
		Nudge(bot, pawn, location, ESCAPE_DISTANCE, "got nowhere with its last goals", now);
		return;
	}
	FVector target;
	float seen = 0.0f;
	bool hunt = HuntGoal(bot, squad, location, world_time, target, seen);
	if (bot.AttackedTick && now - bot.AttackedTick <= (DWORD)(FRESH_ENEMY_SECONDS * 1000.0f) && Distance(bot.AttackedFrom, bot.LastHunt) >= GOAL_REACHED
		&& !InEnemyBase(bot, bot.AttackedFrom)) {
		hunt = true;
		target = bot.AttackedFrom;
		seen = world_time;
	}
	else if (bot.NoiseTick && now - bot.NoiseTick <= NOISE_HUNT_MS && Distance(bot.NoiseFrom, bot.LastHunt) >= GOAL_REACHED
		&& !InEnemyBase(bot, bot.NoiseFrom) && (!hunt || world_time - seen > FRESH_ENEMY_SECONDS)
		&& (!bot.HasGoal || Distance(bot.NoiseFrom, bot.Goal) >= NOISE_REGOAL_DISTANCE)) {
		hunt = true;
		target = bot.NoiseFrom;
		seen = world_time - (float)(now - bot.NoiseTick) / 1000.0f;
	}
	bool fresh = hunt && world_time - seen <= FRESH_ENEMY_SECONDS && (!bot.HasGoal || Distance(target, bot.Goal) >= GOAL_REACHED);
	bool onObjective = (bot.HasGoal || bot.HoldUntilTick) && (bot.Objective || bot.Flag != FLAG_TASK_NONE);
	bool carryingFlag = mode == MODE_RETRIEVAL && Field<UObject*>(pawn, voff.FoxPawn_CarriedObject) != 0;
	if (fresh && onObjective && (carryingFlag || Distance(location, target) >= CP_FIGHT_RANGE)) {
		fresh = false;
	}
	if (bot.HasGoal && Distance(location, bot.Goal) < GOAL_REACHED) {
		bot.HasGoal = false;
		DWORD hold = ArrivalHoldMs(bot);
		bot.HoldUntilTick = hold ? (now + hold) | 1 : 0;
		if (bot.Objective) {
			BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x holds %s at (%.0f %.0f %.0f), its team %s it.", bot.Controller, UE3::ObjectName(bot.Objective)
				, location.X, location.Y, location.Z, bot.Defending ? "owns" : "does not own");
			if (bot.Defending && TakePost(bot, pawn, now)) {
				return;
			}
		}
	}
	if (!fresh && bot.HoldUntilTick && (int32_t)(now - bot.HoldUntilTick) < 0) {
		if (bot.Objective) {
			HoldPoint(bot, pawn, location, now);
		}
		return;
	}
	bot.HoldUntilTick = 0;
	if (mode == MODE_RETRIEVAL && bot.HasGoal && FlagGoalStale(bot, pawn)) {
		bot.HasGoal = false;
	}
	if (bot.HasGoal && !stalled && !fresh && !IsIdle(bot.Controller) && now - bot.GoalTick < GOAL_MAX_AGE_MS) {
		return;
	}
	bool resumes = ((bot.HasGoal && !stalled && !fresh) || bot.StallResume) && (bot.Objective || bot.Flag != FLAG_TASK_NONE);
	bot.StallResume = false;
	if (!resumes) {
		bot.ObjectiveStalls = 0;
	}
	UObject* lastPoint = bot.Objective;
	bot.Objective = 0;
	bot.HasPost = false;
	bot.Flag = FLAG_TASK_NONE;
	bot.FlagActor = 0;
	RecordVisit(bot, location, now);
	const char* reason = "hunts";
	bool hunting = false;
	bool stakeout = false;
	bool engaged = fresh && Distance(location, target) < CP_FIGHT_RANGE;
	bool carrying = carryingFlag;
	if (!engaged && mode == MODE_CONTROL_POINT && (resumes || rand() % 100 < config.Versus.ObjectiveChance)
		&& (bot.Objective = ChoosePoint(game, bot, location, now, lastPoint)) != 0) {
		target = Field<FVector>(bot.Objective, off.Actor_Location);
		bool owned = Field<int32_t>(bot.Objective, voff.CP_CPOwner) == bot.Team;
		if (owned && bot.Objective == lastPoint && Distance(location, target) <= CP_POST_RADIUS) {
			bot.HoldUntilTick = (now + ArrivalHoldMs(bot)) | 1;
			TakePost(bot, pawn, now);
			return;
		}
		reason = owned ? "defends a control point" : "goes for a control point";
	}
	else if (mode == MODE_RETRIEVAL && (carrying || (!engaged && (resumes || rand() % 100 < config.Versus.ObjectiveChance))) && FlagGoal(game, bot, pawn, team_index, target, reason)) {
	}
	else if (hunt) {
		bot.LastHunt = target;
		hunting = true;
		FVector sight;
		if (!PawnSeesPoint(pawn, target) && FindSightSpot(bot.Controller, pawn, STAKEOUT_RADIUS, target, STAKEOUT_MIN_DISTANCE, sight)) {
			target = sight;
			reason = "stakes out";
			stakeout = true;
		}
	}
	else {
		reason = "seeks an enemy";
		if (rand() % 100 >= SeekPercent(bot) || !SeekGoal(game, bot, pawn, now, target)) {
			reason = "roams";
			if (!RoamGoal(bot, location, now, target)) {
				bot.HasGoal = false;
				return;
			}
		}
	}
	FVector goal;
	bool onObject = bot.Objective || (bot.Flag != FLAG_TASK_NONE && bot.Flag != FLAG_TASK_ESCORT && bot.Flag != FLAG_TASK_CHASE_CARRIER);
	bool snapped = (onObject && FindFreeNavMeshSpotToward(bot.Controller, pawn, target, CP_STAND_RADIUS, location, goal)) || FindFreeNavMeshSpot(bot.Controller, pawn, target, GOAL_SNAP_RADIUS, goal);
	if (!snapped || Banned(bot, goal, now)) {
		GoalBan ban = { target, now + GOAL_BAN_MS };
		bot.GoalBans.push_back(ban);
		bot.HasGoal = false;
		bot.Objective = 0;
		bot.Flag = FLAG_TASK_NONE;
		return;
	}
	SetGoToSprint(bot, false);
	SetGoToAbortable(bot, bot.Flag != FLAG_TASK_CARRY_HOME);
	SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, goal);
	bot.HasGoal = true;
	bot.Goal = goal;
	bot.GoalTick = now;
	bot.WaitsOnArrival = stakeout;
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x %s, it goes from (%.0f %.0f %.0f) to (%.0f %.0f %.0f)."
		, bot.Controller, reason, location.X, location.Y, location.Z, goal.X, goal.Y, goal.Z);
}

static void OnMoveFinished(Bot& bot)
{
	static bool thinking = false;
	UObject* pawn = Field<UObject*>(bot.Controller, off.Controller_Pawn);
	DWORD now = GetTickCount();
	if (thinking || !current_game || !IsAlive(pawn) || bot.DiedTick || now - bot.LastThinkTick < EVENT_THINK_MIN_MS
		|| UE3::GetBoolProperty(current_game, ref.GameEndedProperty)) {
		return;
	}
	UObject* world = Field<UObject*>(current_game, off.Actor_WorldInfo);
	float worldTime = world ? Field<float>(world, voff.WorldInfo_TimeSeconds) : 0.0f;
	int teamIndex = 0;
	for (const Bot& other : bots) {
		if (&other == &bot) {
			break;
		}
		if (other.Team == bot.Team && IsAlive(Field<UObject*>(other.Controller, off.Controller_Pawn))) {
			teamIndex++;
		}
	}
	thinking = true;
	Think(current_game, bot, pawn, now, worldTime, teamIndex);
	thinking = false;
}

static void OnMatchStart(UObject* game)
{
	if (current_game != game) {
		current_game = game;
		bots.clear();
		anim_set_swaps.clear();
		spots.clear();
		spawn_spots.clear();
		visits[0].clear();
		visits[1].clear();
		spots_tick = 0;
		ShuffleNames();
	}
	mode_meshes_cut = false;
	NavMesh::LowerStairs(game);
	if (!config.Versus.Enabled) {
		return;
	}
	if (!UE3::LoadPackage(AI_CONTENT_PACKAGE)) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%ls did not load, the match gets no bots.", AI_CONTENT_PACKAGE);
		return;
	}
	static const char* const MODE_NAMES[] = { "No", "Black Ops", "Deathmatch", "Team Deathmatch", "Domination", "Last Man Standing", "Last Team Standing", "Retrieval" };
	BOTS_LOG(LOG_LEVEL_INFO, "%s match started, filling empty places: bot health %d, damage %.2f.", MODE_NAMES[mode], config.Versus.Health, config.Versus.DamageScale);
	versus_start_tick = GetTickCount();
	match_end_logged = false;
	round_scored = false;
	flag_base_known[0] = flag_base_known[1] = false;
	FillPlaces(game, true);
	if (mode == MODE_LAST_MAN) {
		FixRemaining(game);
	}
	else if (mode == MODE_LAST_TEAM) {
		FixTeamRemaining(game);
	}
}

static void Maintain(UObject* game)
{
	if (game != current_game || !config.Versus.Enabled) {
		return;
	}
	for (size_t i = bots.size(); i-- > 0; ) {
		if (!UE3::IsValidObject(bots[i].Controller)) {
			bots.erase(bots.begin() + i);
		}
	}
	if (UE3::GetBoolProperty(game, ref.GameEndedProperty)) {
		if (!match_end_logged) {
			match_end_logged = true;
			BOTS_LOG(LOG_LEVEL_INFO, "Match ended after %u s of play.", (GetTickCount() - versus_start_tick) / 1000);
		}
		return;
	}
	FillPlaces(game, false);
	DWORD now = GetTickCount();
	static const bool no_cuts = GetEnvironmentVariableA("BLTD_NO_CUTS", 0, 0) > 0;
	if (!mode_meshes_cut && !no_cuts) {
		mode_meshes_cut = true;
		NavMesh::CutModeMeshes(game);
	}
	if (!no_cuts && now - turrets_checked_tick >= TURRET_CHECK_MS) {
		turrets_checked_tick = now;
		NavMesh::CutTurrets(game);
	}
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	float worldTime = world ? Field<float>(world, voff.WorldInfo_TimeSeconds) : 0.0f;
	DWORD respawnMs = (DWORD)(Field<float>(game, voff.Game_GameRespawnTime) * 1000.0f);
	if (mode == MODE_RETRIEVAL) {
		RememberBases(game);
	}
	int teamIndex[2] = { 0, 0 };
	for (size_t i = 0; i < bots.size(); i++) {
		Bot& bot = bots[i];
		UObject* pawn = Field<UObject*>(bot.Controller, off.Controller_Pawn);
		if (!IsAlive(pawn)) {
			if (bot.DiedTick && !IsRoundVersus() && now - bot.DiedTick >= respawnMs + bot.SpawnDelayMs) {
				Respawn(game, bot, now);
			}
			continue;
		}
		if (spots.empty() || now - spots_tick >= SPOT_REFRESH_MS) {
			RefreshSpots(bot.Controller, now);
		}
		const FVector location = Field<FVector>(pawn, off.Actor_Location);
		if (mode == MODE_CONTROL_POINT) {
			SyncPresence(game, bot, pawn, location);
		}
		if (mode == MODE_RETRIEVAL) {
			TouchFlags(game, bot, pawn, location);
		}
		Think(game, bot, pawn, now, worldTime, teamIndex[bot.Team < 2 ? bot.Team : 0]++);
	}
	if (mode == MODE_LAST_TEAM) {
		FixTeamRemaining(game);
	}
	else if (mode == MODE_LAST_MAN) {
		FixRemaining(game);
		ScoreLastBot(game);
	}
}

static bool IsVersusGRI(UObject* gri)
{
	UObject* griClass = gri ? UE3::ObjectClass(gri) : 0;
	return griClass && (griClass == ref.FoxGRI_DM || griClass == ref.FoxGRI_TDM || griClass == ref.FoxGRI_CP || griClass == ref.FoxGRI_LMS
		|| griClass == ref.FoxGRI_LTS || griClass == ref.FoxGRI_CTF);
}

static bool IsVersusBotPRI(UObject* pri)
{
	return pri && UE3::GetBoolProperty(pri, ref.BotProperty) && !UE3::GetBoolProperty(pri, ref.InactiveProperty)
		&& Field<int32_t>(pri, voff.PRI_PlayerID) != 0;
}

static void ReplaceArray(TArray<UObject*>& array, const std::vector<UObject*>& items)
{
	UObject** data = (UObject**)UE3::Malloc((uint32_t)(items.size() * sizeof(UObject*)), 8);
	if (!data) {
		return;
	}
	memcpy(data, items.data(), items.size() * sizeof(UObject*));
	UE3::FreeArray(&array);
	array.Data = data;
	array.Num = array.Max = (int32_t)items.size();
}

static bool AddVersusBots(UObject* gri, std::vector<UObject*>& items)
{
	size_t before = items.size();
	TArray<UObject*>& pris = Field<TArray<UObject*> >(gri, voff.GRI_PRIArray);
	for (int32_t i = 0; i < pris.Num; i++) {
		if (IsVersusBotPRI(pris.Data[i]) && std::find(items.begin(), items.end(), pris.Data[i]) == items.end()) {
			items.push_back(pris.Data[i]);
		}
	}
	return items.size() > before;
}

void OnTeamList(UObject* movie, TArray<UObject*>* list)
{
	if (!ready || !list || list->Num <= 0 || !list->Data[0]) {
		return;
	}
	UObject* world = Field<UObject*>(list->Data[0], off.Actor_WorldInfo);
	UObject* gri = world ? Field<UObject*>(world, voff.WorldInfo_GRI) : 0;
	if (!IsVersusGRI(gri)) {
		return;
	}
	std::vector<UObject*> items(list->Data, list->Data + list->Num);
	if (AddVersusBots(gri, items)) {
		ReplaceArray(*list, items);
	}
}

struct Placement {
	UObject* PRI;
	float Score;
	int32_t Experience;
	int32_t Deaths;
};

static void PlaceVersusBots(UObject* gri, TArray<UObject*>* sorted)
{
	std::vector<UObject*> items(sorted->Data, sorted->Data + sorted->Num);
	if (!IsVersusGRI(gri) || !AddVersusBots(gri, items)) {
		return;
	}
	std::vector<Placement> placements;
	for (UObject* pri : items) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.PRI_GetCurrentExperience) + 16, 0);
		UE3::ProcessNativeEvent(pri, ref.PRI_GetCurrentExperience, parms.data());
		Placement placement = { pri, Field<float>(pri, voff.PRI_Score), Field<int32_t>(parms.data(), voff.GetCurrentExperience_ReturnValue), Field<int32_t>(pri, voff.PRI_Deaths) };
		placements.push_back(placement);
	}
	std::stable_sort(placements.begin(), placements.end(), [](const Placement& a, const Placement& b) {
		if (a.Score != b.Score) {
			return a.Score > b.Score;
		}
		if (a.Experience != b.Experience) {
			return a.Experience > b.Experience;
		}
		return a.Deaths < b.Deaths;
	});
	for (size_t i = 0; i < placements.size(); i++) {
		items[i] = placements[i].PRI;
	}
	ReplaceArray(*sorted, items);
}

static void DecideWinningTeam(UObject* gri, UObject** winner)
{
	TArray<UObject*>& teams = Field<TArray<UObject*> >(gri, voff.GRI_Teams);
	if (teams.Num <= BLACKLIGHT_TEAM || !teams.Data[ORDER_TEAM] || !teams.Data[BLACKLIGHT_TEAM]) {
		return;
	}
	if (Field<int32_t>(teams.Data[ORDER_TEAM], voff.Team_HumanSize) > 0 && Field<int32_t>(teams.Data[BLACKLIGHT_TEAM], voff.Team_HumanSize) > 0) {
		return;
	}
	float orderScore = Field<float>(teams.Data[ORDER_TEAM], voff.TeamInfo_Score);
	float blacklightScore = Field<float>(teams.Data[BLACKLIGHT_TEAM], voff.TeamInfo_Score);
	if (orderScore != blacklightScore) {
		*winner = teams.Data[orderScore > blacklightScore ? ORDER_TEAM : BLACKLIGHT_TEAM];
	}
}

static void RewardKill(UObject* killer, UObject* killed)
{
	if (!IsBot(killer) || !killed || killed == killer) {
		return;
	}
	UObject* pri = Field<UObject*>(killer, voff.Controller_PlayerReplicationInfo);
	UObject* killedPri = Field<UObject*>(killed, voff.Controller_PlayerReplicationInfo);
	if (!pri || (IsTeamVersus() && killedPri && Field<UObject*>(killedPri, voff.PRI_Team) == Field<UObject*>(pri, voff.PRI_Team))) {
		return;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.PRI_AddToPlayerRank) + 16, 0);
	Field<int32_t>(parms.data(), voff.AddToPlayerRank_Amount) = Field<int32_t>(ref.ScoringDefaults, IsTeamVersus() ? voff.Scoring_ExpKills_TDM : voff.Scoring_ExpKills_DM);
	Field<uint32_t>(parms.data(), voff.AddToPlayerRank_SkipNotify) = 1;
	UE3::ProcessEvent(pri, ref.PRI_AddToPlayerRank, parms.data());
}

bool OnScriptCall(UObject* self, FFrame* stack)
{
	if (!ready) {
		return true;
	}
	// Last Man Standing and Last Team Standing wait for ready humans, so a host alone with bots never starts them.
	if ((stack->Node == ref.DM_IsLevelReadyToStart || stack->Node == ref.TDM_IsLevelReadyToStart) && config.Versus.Enabled) {
		UObject* gameClass = UE3::ObjectClass(self);
		float& needed = Field<float>(self, voff.MP_NumReadyPlayersToStart);
		float before = needed;
		if (gameClass == ref.LMS_Game && needed > 1.0f) {
			needed = 1.0f;
		}
		else if (gameClass == ref.LTS_Game && needed > 0.0f) {
			needed = 0.0f;
		}
		if (needed != before) {
			BOTS_LOG(LOG_LEVEL_INFO, "%s starts with %.0f ready players instead of %.0f, the bots fill the rest.", UE3::ObjectName(gameClass), needed, before);
		}
		return true;
	}
	if (!IsVersus()) {
		return true;
	}
	if (stack->Node == ref.Pawn_BuildScriptAnimSetList || stack->Node == ref.Pawn_RestoreAnimSetsToDefault) {
		BeginAnimSetSwap(self, stack->Node);
		return true;
	}
	if (stack->Node == ref.Weapon_FireAmmunition) {
		return OnFireAmmunition(self);
	}
	if (stack->Node == ref.MP_BroadcastDeathMessage) {
		return OnDeathMessage(self, stack);
	}
	if (stack->Node == ref.AI_GetAdjustedAimFor) {
		Bot* bot = FindBot(self);
		if (bot) {
			OnAim(*bot);
		}
		return true;
	}
	if (stack->Node == fn.AI_ProcessBehaviorEvent) {
		Bot* bot = FindBot(self);
		const UE3::FString& eventName = Field<UE3::FString>(stack->Locals, off.ProcessBehaviorEvent_EventName);
		if (bot && eventName.Data) {
			OnSightEvent(*bot, eventName.Data, Field<UObject*>(stack->Locals, off.ProcessBehaviorEvent_EventRecipient));
		}
		return true;
	}
	if (stack->Node == ref.LTS_RoundScoring && mode == MODE_LAST_TEAM && self == current_game) {
		int32_t humans;
		if (AliveOnTeam(self, ORDER_TEAM, &humans) > 0 && AliveOnTeam(self, BLACKLIGHT_TEAM, &humans) > 0) {
			FixTeamRemaining(self);
			return false;
		}
	}
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == ref.GRI_DM_GetSortedPRIs) {
		if (result) {
			PlaceVersusBots(self, (TArray<UObject*>*)result);
		}
		return;
	}
	if (!IsVersus()) {
		return;
	}
	if (node == ref.Pawn_BuildScriptAnimSetList || node == ref.Pawn_RestoreAnimSetsToDefault) {
		EndAnimSetSwap(self, node);
		return;
	}
	if (node == ref.AI_GetAdjustedAimFor) {
		Bot* bot = result ? FindBot(self) : 0;
		if (bot) {
			OnAimed(*bot, stack, (UE3::FRotator*)result);
		}
		return;
	}
	if (node == fn.AI_ProcessBehaviorEvent) {
		Bot* bot = FindBot(self);
		const UE3::FString& eventName = Field<UE3::FString>(stack->Locals, off.ProcessBehaviorEvent_EventName);
		if (bot && eventName.Data && wcscmp(eventName.Data, MOVE_FINISHED_EVENT) == 0) {
			OnMoveFinished(*bot);
		}
		return;
	}
	if (node == fn.MP_StartMatch) {
		OnMatchStart(self);
		return;
	}
	if (node == ref.MP_Timer) {
		Maintain(self);
		return;
	}
	if (node == fn.AI_PawnDied) {
		OnPawnDied(self);
		return;
	}
	if (node == ref.MP_Killed) {
		RewardKill(Field<UObject*>(stack->Locals, voff.Killed_Killer), Field<UObject*>(stack->Locals, voff.Killed_KilledPlayer));
		return;
	}
	if (node == ref.GRI_GetWinningTeam) {
		if (IsTeamVersus() && result) {
			DecideWinningTeam(self, (UObject**)result);
		}
		return;
	}
	if (node == ref.Game_EndCurrentRound) {
		if (IsRoundVersus() && self == current_game) {
			OnRoundEnd(self);
		}
		return;
	}
	if (node == ref.Game_RestartRound) {
		if (IsRoundVersus() && self == current_game) {
			OnRoundStart(self);
		}
		return;
	}
	if (node == ref.LTS_ScoreKill) {
		if (mode == MODE_LAST_TEAM && self == current_game) {
			FixTeamRemaining(self);
		}
		return;
	}
}

}
}
