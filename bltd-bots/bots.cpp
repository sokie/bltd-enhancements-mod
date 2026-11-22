#include "bot-trace.hpp"
#include "bots.hpp"
#include "bots-internal.hpp"
#include "events.hpp"
#include "fox-ai.hpp"
#include "menu.hpp"
#include "navmesh.hpp"
#include "lobby.hpp"
#include "recoil.hpp"
#include "aimassist.hpp"
#include "engine-fixes.hpp"
#include "hud.hpp"
#include "spawns.hpp"
#include "teamswitch.hpp"
#include "server.hpp"
#include "squad.hpp"
#include "versus.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

using UE3::UObject;
using UE3::FFrame;
using UE3::FVector;
using UE3::TArray;
using UE3::Field;

namespace Bots {

static const DWORD MOVIE_VOLUME_INTERVAL_MS = 1000;

static const DWORD FOLLOW_COMMAND_INTERVAL_MS = 5000;
static const float EVALUATION_INTERVAL = 1.0f;
static const float FOLLOW_GROUP_DISTANCE = 250.0f;
static const DWORD RESOLVE_INTERVAL_MS = 1000;
static const int32_t AI_DAMAGE_MOD_NATIVE_OFFSET = 0x4E8;
static const int32_t LAST_DESTINATION_NATIVE_OFFSET = 0x448;
static const wchar_t* const FOLLOW_COMMAND = L"Command Follow";
static const wchar_t PATH_NOT_FOUND_EVENT[] = L"PathNotFound";
static const wchar_t ZERO_MOVE_EVENT[] = L"PathNotFound, NewMove IsNearlyZero";
static const wchar_t HEAR_NOISE_EVENT[] = L"Hear Noise";
static const uint32_t ZERO_MOVE_LIMIT = 6;
static const float OWN_SEARCH_RADIUS = 100.0f;
static const float TELEPORT_BEHIND = 400.0f;
static const float BOT_MAX_HOVER_DISTANCE = 48.0f;
static const float BOT_MIN_WALKABLE_Z = 0.45f;
static const float TRAIL_SPACING = 200.0f;
static const uint8_t PHYS_WALKING = 1;
static const uint8_t PHYS_NAVMESHWALKING = 12;
static const int32_t CA_BLIND_LEFT = 1;
static const int32_t CA_BLIND_RIGHT = 2;
static const int32_t CA_LEAN_LEFT = 3;
static const int32_t CA_LEAN_RIGHT = 4;
static const int32_t CA_POP_UP = 5;
static const int32_t CA_BLIND_UP = 6;
static const float ROTATOR_TO_RADIANS = 3.14159265f / 32768.0f;
static const float COVER_FACING = 0.6f;
static const float PEEK_EYE_UP = 50.0f;
static const float PEEK_LEAN_NEAR = 50.0f;
static const float PEEK_LEAN_FAR = 90.0f;
static const float TARGET_CHEST_UP = 30.0f;
static const size_t SIGHT_SPOT_CANDIDATES = 120;
static const float SIGHT_SPOT_EYE_UP = 60.0f;
static const float SIGHT_SPOT_MIN_MOVE = 100.0f;
static const float COVER_NEAR_TRAIL = 600.0f;
static const float COVER_LINK_SEARCH = 1500.0f;
static const float PAWN_RADIUS = 32.0f;
static const float PAWN_HALF_HEIGHT = 96.0f;
static const float NAVMESH_NEAR_TRAIL = 300.0f;
static const float COVER_GOAL_DISTANCE = 200.0f;
static const DWORD COVER_UNSEEN_MAX_MS = 5000;
static const DWORD COVER_STALE_BAN_MS = 20000;
static const float NAVMESH_NEAR_HUMAN = 600.0f;
static const float SPOT_LIFT = 32.0f;
static const float SPOT_SHIFT_LIMIT = 48.0f;
static const uint32_t PATH_FAIL_LIMIT = 3;
static const DWORD PATH_FAIL_STILL_MS = 3000;
static const DWORD PATH_FAIL_LOG_INTERVAL_MS = 10000;
static const uint32_t FOLLOW_STALL_TICKS = 3;
static const uint32_t LEASH_CALL_LIMIT = 2;
static const DWORD STILL_CHECK_INTERVAL_MS = 3000;
static const char* const ASSAULT_CONTROLLER_CLASS = "FoxGame.FoxAIControllerInterface_Assault";
static const char SPAWNAIWAVE_PRESET[] = "WpnPreset_ASS_0";
static const int TRAIL_LENGTH = 16;
static const char* const BOT_PAWN_CLASS = "FoxGameContent_BO.FoxPawn_Blacklight_Cover";
static const char* const ENEMY_PAWN_CLASS = "FoxGameContent_BO.FoxPawn_Order_Cover";
static const DWORD TEST_ENEMIES_DELAY_MS = 20000;
static const DWORD TEST_HOST_MOVE_DELAY_MS = 5000;
static const float TEST_ENEMY_MIN = 1200.0f;
static const float TEST_ENEMY_MAX = 2200.0f;
static const float TEST_ENEMY_SEARCH = 2600.0f;
static const float TEST_ENEMY_SPACING = 250.0f;

struct Trail {
	UObject* Human;
	FVector Points[TRAIL_LENGTH];
	int Count;
};

Functions fn;
Classes cls;
Offsets off;
Config config;
LookRefs look;
Mode mode = MODE_NONE;

static uint32_t* spawnaiwave_controller_class = 0;
static char* spawnaiwave_preset = 0;
static int spawning_slot = 0;
static uint8_t spawning_team = 0;

static bool resolved = false;
static bool navmesh_ready = false;
static bool resolve_failed = false;
static bool resolve_tried = false;
static DWORD last_resolve_tick = 0;

std::vector<Bot> bots;
UObject* current_game = 0;
static bool spawning_ally = false;
static bool spawning_enemy = false;
static bool test_enemies_done = false;
static bool host_moved = false;
struct Waypoint {
	FVector At;
	float Time;
};
static std::vector<Waypoint> host_path;
static DWORD host_path_tick = 0;
static DWORD host_blocked_tick = 0;
static const DWORD HOST_PATH_STEP_MS = 100;
static const DWORD HOST_BLOCKED_MS = 2000;
static const float HOST_PATH_SPEED = 585.0f;
static DWORD match_start_tick = 0;
static UObject* spawned_controller = 0;
static UObject* spawned_pawn = 0;
static UObject* bot_pawn_class = 0;
static std::vector<Trail> trails;
static int32_t checkpoint_count_before = 0;
static std::vector<UObject*> pending_teleports;
static UObject* pending_checkpoint = 0;
static UObject* damage_scale_game = 0;
static float damage_scale_saved = 0.0f;

static int Resolve()
{
	cls.FoxGameMP_BO = UE3::FindObject("FoxGame.FoxGameMP_BO");
	if (!cls.FoxGameMP_BO) {
		return 0;
	}

	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &cls.FoxGameMP_TDM, "FoxGame.FoxGameMP_TDM" },
		{ &cls.FoxGameMP_DM, "FoxGame.FoxGameMP_DM" },
		{ &cls.FoxGameMP_CP, "FoxGame.FoxGameMP_CP" },
		{ &cls.FoxGameMP_LMS, "FoxGame.FoxGameMP_LMS" },
		{ &cls.FoxGameMP_LTS, "FoxGame.FoxGameMP_LTS" },
		{ &cls.FoxGameMP_CTF, "FoxGame.FoxGameMP_CTF" },
		{ &cls.FoxPC, "FoxGame.FoxPC" },
		{ &cls.Pawn, "Engine.Pawn" },
		{ &fn.BO_StartMatch, "FoxGame.FoxGameMP_BO.StartMatch" },
		{ &fn.BO_Timer, "FoxGame.FoxGameMP_BO.Timer" },
		{ &fn.BO_GetForcedTeam, "FoxGame.FoxGameMP_BO.GetForcedTeam" },
		{ &fn.DM_GetForcedTeam, "FoxGame.FoxGameMP_DM.GetForcedTeam" },
		{ &fn.TDM_GetForcedTeam, "FoxGame.FoxGameMP_TDM.GetForcedTeam" },
		{ &fn.BO_CheckpointReached, "FoxGame.FoxGameMP_BO.CheckpointReached" },
		{ &fn.MP_ChoosePlayerStart, "FoxGame.FoxGameMP.ChoosePlayerStart" },
		{ &fn.MP_StartMatch, "FoxGame.FoxGameMP.StartMatch" },
		{ &fn.Team_AddToSquad, "FoxGame.FoxTeamInfo.AddToSquad" },
		{ &fn.Checkpoint_Touch, "FoxGame.FoxBOCheckpointTriggerVolume.Touch" },
		{ &fn.Game_ReduceDamage, "FoxGame.FoxGame.ReduceDamage" },
		{ &fn.FoxPawn_TakeDamage, "FoxGame.FoxPawn.TakeDamage" },
		{ &fn.Controller_Destroyed, "Engine.Controller.Destroyed" },
		{ &fn.AI_Possess, "FoxGame.FoxAIControllerInterface.Possess" },
		{ &fn.AI_PawnDied, "FoxGame.FoxAIControllerInterface.PawnDied" },
		{ &fn.AI_ProcessBehaviorEvent, "FoxGame.FoxAIControllerInterface.ProcessBehaviorEvent" },
		{ &fn.PC_SpawnAIWave, "FoxGame.FoxPC.SpawnAIWave" },
		{ &fn.Pawn_Suicide, "Engine.Pawn.Suicide" },
		{ &fn.Pawn_EncroachedBy, "Engine.Pawn.EncroachedBy" },
		{ &fn.AI_AIErrorThresholdReached, "FoxGame.FoxAIControllerInterface.AIErrorThresholdReached" },
		{ &fn.BO_Killed, "FoxGame.FoxGameMP_BO.Killed" },
		{ &fn.CoverLink_GetSlotLocation, "Engine.CoverLink.GetSlotLocation" },
		{ &fn.CoverLink_IsValidClaim, "Engine.CoverLink.IsValidClaim" },
		{ &fn.Handle_GetValidPositionsForBox, "Engine.NavigationHandle.GetValidPositionsForBox" },
		{ &fn.Handle_FindPylon, "Engine.NavigationHandle.FindPylon" },
		{ &fn.Actor_Spawn, "Engine.Actor.Spawn" },
		{ &fn.Actor_Destroy, "Engine.Actor.Destroy" },
		{ &fn.Actor_SetLocation, "Engine.Actor.SetLocation" },
		{ &fn.Game_AddDefaultInventory, "FoxGame.FoxGame.AddDefaultInventory" },
		{ &fn.FoxPawn_AddInventory, "FoxGame.FoxPawn.AddInventory" },
		{ &fn.Squad_SetSquadBehavior, "FoxGame.FoxSquadInfo.SetSquadBehavior" },
		{ &fn.Actor_FastTrace, "Engine.Actor.FastTrace" },
		{ &fn.Pawn_StopFire, "Engine.Pawn.StopFire" },
		{ &fn.PC_PlayerTick, "FoxGame.FoxPC.PlayerTick" },
		{ &cls.TraceBulletProperty, "Engine.Actor.FastTrace.bTraceBullet" },
		{ &cls.FastTraceResultProperty, "Engine.Actor.FastTrace.ReturnValue" },
		{ &cls.GoToActionClass, "FoxGame.FoxAIAction_GoTo" },
		{ &cls.CanAbortProperty, "FoxGame.FoxAIHierarchicalAbility.bCanAbort" },
		{ &cls.RunToDestinationProperty, "FoxGame.FoxAIAction_GoTo.bRunToDestination" },
		{ &cls.FightFromCoverClass, "FoxGame.FoxAIAbility_FightFromCover" },
		{ &cls.FightMeleeClass, "FoxGame.FoxAIAbility_FightMelee" },
		{ &cls.FireWeaponClass, "FoxGame.FoxAIAction_FireWeapon" },
		{ &cls.StopFiringIfSuppressedProperty, "FoxGame.FoxAIAction_FireWeapon.bStopFiringIfSuppressed" },
		{ &cls.AssaultController, ASSAULT_CONTROLLER_CLASS },
		{ &cls.CoverEnabledProperty, "FoxGame.FoxPawn.bCoverEnabled" },
		{ &cls.EnemyRangeCondition, "FoxGame.FoxAIBehaviorCondition_EnemyRangeFuzzy" },
		{ &cls.FollowAction, "FoxGame.FoxAIAction_Follow" },
	};
	for (auto& ref : objectRefs) {
		*ref.Target = UE3::FindObject(ref.Path);
		if (!*ref.Target) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", ref.Path);
			return -1;
		}
	}

	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &off.Actor_Location, "Engine.Actor.Location" },
		{ &off.Actor_WorldInfo, "Engine.Actor.WorldInfo" },
		{ &off.Controller_Pawn, "Engine.Controller.Pawn" },
		{ &off.Controller_NextController, "Engine.Controller.NextController" },
		{ &off.WorldInfo_ControllerList, "Engine.WorldInfo.ControllerList" },
		{ &off.Pawn_Controller, "Engine.Pawn.Controller" },
		{ &off.Pawn_Health, "Engine.Pawn.Health" },
		{ &off.GameInfo_MaxPlayers, "Engine.GameInfo.MaxPlayers" },
		{ &off.GameInfo_GameDifficulty, "Engine.GameInfo.GameDifficulty" },
		{ &off.BO_CurrentAIDamageMod, "FoxGame.FoxGameMP_BO.CurrentAIDamageMod" },
		{ &off.BO_PreviousCheckpoints, "FoxGame.FoxGameMP_BO.PreviousCheckpoints" },
		{ &off.GetForcedTeam_Other, "FoxGame.FoxGameMP_BO.GetForcedTeam.Other" },
		{ &off.DM_GetForcedTeam_Other, "FoxGame.FoxGameMP_DM.GetForcedTeam.Other" },
		{ &off.TDM_GetForcedTeam_Other, "FoxGame.FoxGameMP_TDM.GetForcedTeam.Other" },
		{ &off.ChoosePlayerStart_InTeam, "FoxGame.FoxGameMP.ChoosePlayerStart.InTeam" },
		{ &off.AddToSquad_Other, "FoxGame.FoxTeamInfo.AddToSquad.Other" },
		{ &off.AddToSquad_SquadIndex, "FoxGame.FoxTeamInfo.AddToSquad.SquadIndex" },
		{ &off.Touch_Other, "FoxGame.FoxBOCheckpointTriggerVolume.Touch.Other" },
		{ &off.CheckpointReached_LivePawns, "FoxGame.FoxGameMP_BO.CheckpointReached.LivePawnsToRespawn" },
		{ &off.CheckpointReached_Checkpoint, "FoxGame.FoxGameMP_BO.CheckpointReached.Checkpoint" },
		{ &off.ReduceDamage_InstigatedBy, "FoxGame.FoxGame.ReduceDamage.InstigatedBy" },
		{ &off.TakeDamage_Damage, "FoxGame.FoxPawn.TakeDamage.Damage" },
		{ &off.TakeDamage_InstigatedBy, "FoxGame.FoxPawn.TakeDamage.InstigatedBy" },
		{ &off.Possess_inPawn, "FoxGame.FoxAIControllerInterface.Possess.inPawn" },
		{ &off.ProcessBehaviorEvent_EventName, "FoxGame.FoxAIControllerInterface.ProcessBehaviorEvent.EventName" },
		{ &off.ProcessBehaviorEvent_EventInstigator, "FoxGame.FoxAIControllerInterface.ProcessBehaviorEvent.EventInstigator" },
		{ &off.ProcessBehaviorEvent_EventRecipient, "FoxGame.FoxAIControllerInterface.ProcessBehaviorEvent.EventRecipient" },
		{ &off.ProcessBehaviorEvent_EventLocation, "FoxGame.FoxAIControllerInterface.ProcessBehaviorEvent.EventLocation" },
		{ &off.SpawnAIWave_Number, "FoxGame.FoxPC.SpawnAIWave.Number" },
		{ &off.EncroachedBy_Other, "Engine.Pawn.EncroachedBy.Other" },
		{ &off.WorldInfo_PawnList, "Engine.WorldInfo.PawnList" },
		{ &off.Pawn_NextPawn, "Engine.Pawn.NextPawn" },
		{ &off.AI_CurrentBehavior, "FoxGame.FoxAIControllerInterface.CurrentBehavior" },
		{ &off.Game_Teams, "FoxGame.FoxGame.Teams" },
		{ &off.Team_Squads, "FoxGame.FoxTeamInfo.Squads" },
		{ &off.Squad_SquadLeader, "FoxGame.FoxSquadInfo.SquadLeader" },
		{ &off.Squad_SquadMembers, "FoxGame.FoxSquadInfo.SquadMembers" },
		{ &off.Actor_Rotation, "Engine.Actor.Rotation" },
		{ &off.Spawn_SpawnClass, "Engine.Actor.Spawn.SpawnClass" },
		{ &off.Spawn_SpawnLocation, "Engine.Actor.Spawn.SpawnLocation" },
		{ &off.Spawn_SpawnRotation, "Engine.Actor.Spawn.SpawnRotation" },
		{ &off.Spawn_ReturnValue, "Engine.Actor.Spawn.ReturnValue" },
		{ &off.SetLocation_NewLocation, "Engine.Actor.SetLocation.NewLocation" },
		{ &off.SetLocation_ReturnValue, "Engine.Actor.SetLocation.ReturnValue" },
		{ &off.AddDefaultInventory_P, "FoxGame.FoxGame.AddDefaultInventory.P" },
		{ &off.Pawn_HealthMax, "Engine.Pawn.HealthMax" },
		{ &off.Pawn_SightRadius, "Engine.Pawn.SightRadius" },
		{ &off.AI_AggressionRange, "FoxGame.FoxAIControllerInterface.AggressionRange" },
		{ &off.FoxPawn_HealthRechargeMaxPercent, "FoxGame.FoxPawn.HealthRechargeMaxPercent" },
		{ &off.AI_Accuracy, "FoxGame.FoxAIControllerInterface.Accuracy" },
		{ &off.AddInventory_InvClass, "FoxGame.FoxPawn.AddInventory.InvClass" },
		{ &off.SetSquadBehavior_NewSquadBehavior, "FoxGame.FoxSquadInfo.SetSquadBehavior.NewSquadBehavior" },
		{ &off.AI_DefaultBehavior, "FoxGame.FoxAIControllerInterface.DefaultBehavior" },
		{ &off.AI_ScriptedBehavior, "FoxGame.FoxAIControllerInterface.ScriptedBehavior" },
		{ &off.AI_CoverConditions, "FoxGame.FoxAIControllerInterface.CoverConditions" },
		{ &off.Squad_EnemyNotVisibleRemoveTime, "FoxGame.FoxSquadInfo.EnemyNotVisibleRemoveTime" },
		{ &off.AI_CurrentCover, "FoxGame.FoxAIControllerInterface.CurrentCover" },
		{ &off.FoxPawn_CurrentLink, "FoxGame.FoxPawn.CurrentLink" },
		{ &off.FoxPawn_CoverType, "FoxGame.FoxPawn.CoverType" },
		{ &off.AI_AIErrorLevel, "FoxGame.FoxAIControllerInterface.AIErrorLevel" },
		{ &off.Actor_Physics, "Engine.Actor.Physics" },
		{ &off.Killed_Killer, "FoxGame.FoxGameMP_BO.Killed.Killer" },
		{ &off.Killed_KilledPlayer, "FoxGame.FoxGameMP_BO.Killed.KilledPlayer" },
		{ &off.Killed_DamageType, "FoxGame.FoxGameMP_BO.Killed.DamageType" },
		{ &off.WorldInfo_CoverList, "Engine.WorldInfo.CoverList" },
		{ &off.CoverLink_NextCoverLink, "Engine.CoverLink.NextCoverLink" },
		{ &off.CoverLink_Slots, "Engine.CoverLink.Slots" },
		{ &off.GetSlotLocation_SlotIdx, "Engine.CoverLink.GetSlotLocation.SlotIdx" },
		{ &off.GetSlotLocation_ReturnValue, "Engine.CoverLink.GetSlotLocation.ReturnValue" },
		{ &off.IsValidClaim_ChkClaim, "Engine.CoverLink.IsValidClaim.ChkClaim" },
		{ &off.IsValidClaim_SlotIdx, "Engine.CoverLink.IsValidClaim.SlotIdx" },
		{ &off.IsValidClaim_ReturnValue, "Engine.CoverLink.IsValidClaim.ReturnValue" },
		{ &off.Controller_NavigationHandle, "Engine.Controller.NavigationHandle" },
		{ &off.ValidPositions_Pos, "Engine.NavigationHandle.GetValidPositionsForBox.pos" },
		{ &off.ValidPositions_Radius, "Engine.NavigationHandle.GetValidPositionsForBox.Radius" },
		{ &off.ValidPositions_Extent, "Engine.NavigationHandle.GetValidPositionsForBox.Extent" },
		{ &off.ValidPositions_Out, "Engine.NavigationHandle.GetValidPositionsForBox.out_ValidPositions" },
		{ &off.Ability_Owner, "FoxGame.FoxAIHierarchicalAbility.Owner" },
		{ &off.Ability_SubActions, "FoxGame.FoxAIHierarchicalAbility.SubActions" },
		{ &off.FollowAction_FollowingPawn, "FoxGame.FoxAIAction_Follow.FollowingPawn" },
		{ &off.FastTrace_TraceEnd, "Engine.Actor.FastTrace.TraceEnd" },
		{ &off.FastTrace_TraceStart, "Engine.Actor.FastTrace.TraceStart" },
		{ &off.AI_LastDestination, "FoxGame.FoxAIControllerInterface.LastDestination" },
		{ &off.AI_Behaviors, "FoxGame.FoxAIControllerInterface.Behaviors" },
		{ &off.AI_EvaluationInterval, "FoxGame.FoxAIControllerInterface.EvaluationInterval" },
		{ &off.FireWeapon_SuppressionConditions, "FoxGame.FoxAIAction_FireWeapon.SuppressionConditions" },
		{ &off.Pawn_EyeHeight, "Engine.Pawn.EyeHeight" },
		{ &off.StopFire_FireModeNum, "Engine.Pawn.StopFire.FireModeNum" },
		{ &off.Actor_Velocity, "Engine.Actor.Velocity" },
	};
	for (auto& ref : offsetRefs) {
		*ref.Target = UE3::FindPropertyOffset(ref.Path);
		if (*ref.Target < 0) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", ref.Path);
			return -1;
		}
	}

	if (!UE3::DetectSuperField(cls.FoxGameMP_BO, cls.FoxGameMP_TDM)) {
		BOTS_LOG(LOG_LEVEL_ERROR, "The UStruct super field offset was not found: +0x3C \"%s\", +0x44 \"%s\", +0x48 \"%s\"."
			, UE3::ObjectName(Field<UObject*>(cls.FoxGameMP_BO, 0x3C))
			, UE3::ObjectName(Field<UObject*>(cls.FoxGameMP_BO, 0x44))
			, UE3::ObjectName(Field<UObject*>(cls.FoxGameMP_BO, 0x48)));
		return -1;
	}
	if (off.BO_CurrentAIDamageMod != AI_DAMAGE_MOD_NATIVE_OFFSET) {
		BOTS_LOG(LOG_LEVEL_ERROR, "CurrentAIDamageMod is at 0x%x, not 0x%x.", off.BO_CurrentAIDamageMod, AI_DAMAGE_MOD_NATIVE_OFFSET);
		return -1;
	}
	if (off.AI_LastDestination != LAST_DESTINATION_NATIVE_OFFSET) {
		BOTS_LOG(LOG_LEVEL_ERROR, "LastDestination is at 0x%x, not 0x%x.", off.AI_LastDestination, LAST_DESTINATION_NATIVE_OFFSET);
		return -1;
	}
	uint32_t assaultClass = (uint32_t)cls.AssaultController;
	fn.Game_GameEnding = UE3::FindObject("Engine.GameInfo.GameEnding");
	if (!fn.Game_GameEnding) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "Engine.GameInfo.GameEnding not found, the bots keep their match pointers until the next match starts.");
	}
	spawnaiwave_controller_class = (uint32_t*)UE3::FindInScript(fn.PC_SpawnAIWave, &assaultClass, sizeof(assaultClass));
	spawnaiwave_preset = (char*)UE3::FindInScript(fn.PC_SpawnAIWave, SPAWNAIWAVE_PRESET, sizeof(SPAWNAIWAVE_PRESET));
	if (!spawnaiwave_controller_class) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The controller class in SpawnAIWave was not found, the Controller setting is ignored.");
	}
	if (!spawnaiwave_preset) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The weapon preset in SpawnAIWave was not found, the WeaponPreset setting is ignored.");
	}
	struct { UObject** Target; const char* Path; } lookObjectRefs[] = {
		{ &look.DynamicLoadObject, "Core.Object.DynamicLoadObject" },
		{ &look.ClassClass, "Core.Class" },
	};
	struct { int32_t* Target; const char* Path; } lookOffsetRefs[] = {
		{ &look.Pawn_Mesh, "Engine.Pawn.Mesh" },
		{ &look.SkeletalMeshComponent_SkeletalMesh, "Engine.SkeletalMeshComponent.SkeletalMesh" },
		{ &look.FoxPawn_DialogPawnName, "FoxGame.FoxPawn.DialogPawnName" },
		{ &look.DynamicLoadObject_ObjectName, "Core.Object.DynamicLoadObject.ObjectName" },
		{ &look.DynamicLoadObject_ObjectClass, "Core.Object.DynamicLoadObject.ObjectClass" },
		{ &look.DynamicLoadObject_MayFail, "Core.Object.DynamicLoadObject.MayFail" },
		{ &look.Pawn_GroundSpeed, "Engine.Pawn.GroundSpeed" },
		{ &look.FoxPawn_PawnModifiers, "FoxGame.FoxPawn.PawnModifiers" },
		{ &look.ModInfo_HealthMax, "FoxGame.FoxTypes.FoxWeaponModInfo.HealthMax" },
		{ &look.ModInfo_MovementSpeed, "FoxGame.FoxTypes.FoxWeaponModInfo.MovementSpeed" },
		{ &look.DynamicLoadObject_ReturnValue, "Core.Object.DynamicLoadObject.ReturnValue" },
	};
	bool lookReady = true;
	for (auto& ref : lookObjectRefs) {
		*ref.Target = UE3::FindObject(ref.Path);
		lookReady = lookReady && *ref.Target;
	}
	for (auto& ref : lookOffsetRefs) {
		*ref.Target = UE3::FindPropertyOffset(ref.Path);
		lookReady = lookReady && *ref.Target >= 0;
	}
	if (!lookReady) {
		look.DynamicLoadObject = 0;
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The mesh or voice properties were not found, the Look setting is ignored.");
	}
	if (!Versus::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "Deathmatch and Team Deathmatch get no bots.");
	}
	if (!Server::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "A dedicated server starts no match and loads no next map.");
	}
	if (!TeamSwitch::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "Players of public matches cannot switch teams.");
	}
	Hud::Resolve();
	AimAssist::Resolve();
	Recoil::Resolve();
	if (!Spawns::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The team modes keep the game's spawn rules.");
	}
	if (!Lobby::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "A public lobby keeps the game's countdown and player count.");
	}
	if (!Squad::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The Black Ops bots follow their human as before, with no posts.");
	}
	if (!Trace::Resolve()) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The Trace setting is ignored.");
	}
	return 1;
}

Bot* FindBot(UObject* controller)
{
	if (!controller) {
		return 0;
	}
	for (Bot& bot : bots) {
		if (bot.Controller == controller) {
			return &bot;
		}
	}
	return 0;
}

bool IsBot(UObject* controller)
{
	return FindBot(controller) != 0;
}

static void ForgetBot(UObject* controller)
{
	for (size_t i = 0; i < bots.size(); i++) {
		if (bots[i].Controller == controller) {
			bots.erase(bots.begin() + i);
			return;
		}
	}
}

static bool IsBotPawn(UObject* actor)
{
	if (!actor || !UE3::IsA(actor, cls.Pawn)) {
		return false;
	}
	return IsBot(Field<UObject*>(actor, off.Pawn_Controller));
}

bool IsAlive(UObject* pawn)
{
	return pawn && UE3::IsValidObject(pawn) && Field<int32_t>(pawn, off.Pawn_Health) > 0;
}

static float DistanceSquared(UObject* a, UObject* b)
{
	const FVector& la = Field<FVector>(a, off.Actor_Location);
	const FVector& lb = Field<FVector>(b, off.Actor_Location);
	float x = la.X - lb.X;
	float y = la.Y - lb.Y;
	float z = la.Z - lb.Z;
	return x * x + y * y + z * z;
}

float Distance(const FVector& a, const FVector& b)
{
	float x = a.X - b.X;
	float y = a.Y - b.Y;
	float z = a.Z - b.Z;
	return sqrtf(x * x + y * y + z * z);
}

void FindHumans(UObject* game, std::vector<UObject*>& humans)
{
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	if (!world) {
		return;
	}
	for (UObject* controller = Field<UObject*>(world, off.WorldInfo_ControllerList); controller; controller = Field<UObject*>(controller, off.Controller_NextController)) {
		if (UE3::IsA(controller, cls.FoxPC)) {
			humans.push_back(controller);
		}
	}
}

static int32_t EmptySlots(UObject* game, size_t human_count)
{
	int32_t open = Field<int32_t>(game, off.GameInfo_MaxPlayers) - (int32_t)human_count;
	if (open > config.Count) {
		open = config.Count;
	}
	return open - (int32_t)bots.size();
}

static int FreeSlot()
{
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		bool used = false;
		for (const Bot& bot : bots) {
			used = used || bot.Slot == slot;
		}
		if (!used) {
			return slot;
		}
	}
	return 0;
}

void CallFunction(UObject* object, UObject* function, std::vector<uint8_t>& parms)
{
	parms.resize(UE3::FunctionParmsSize(function) + 16, 0);
	UE3::ProcessEvent(object, function, parms.data());
}

void RemoveBot(size_t index)
{
	UObject* controller = bots[index].Controller;
	bots.erase(bots.begin() + index);
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	std::vector<uint8_t> parms;
	if (IsAlive(pawn)) {
		CallFunction(pawn, fn.Pawn_Suicide, parms);
	}
	else {
		parms.assign(UE3::FunctionParmsSize(fn.AI_PawnDied) + 16, 0);
		CallFunction(controller, fn.AI_PawnDied, parms);
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Removed bot 0x%08x.", controller);
}

static void RemoveBotController(UObject* controller)
{
	for (size_t i = 0; i < bots.size(); i++) {
		if (bots[i].Controller == controller) {
			RemoveBot(i);
			return;
		}
	}
}

bool DestroyActor(UObject* actor)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Actor_Destroy) + 16, 0);
	UE3::ProcessNativeEvent(actor, fn.Actor_Destroy, parms.data());
	return Field<uint32_t>(parms.data(), 0) != 0;
}

UObject* SpawnActor(UObject* spawner, UObject* actor_class, const FVector& location, const UE3::FRotator& rotation)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Actor_Spawn) + 16, 0);
	Field<UObject*>(parms.data(), off.Spawn_SpawnClass) = actor_class;
	Field<FVector>(parms.data(), off.Spawn_SpawnLocation) = location;
	Field<UE3::FRotator>(parms.data(), off.Spawn_SpawnRotation) = rotation;
	UE3::ProcessNativeEvent(spawner, fn.Actor_Spawn, parms.data());
	return Field<UObject*>(parms.data(), off.Spawn_ReturnValue);
}

static bool MoveActor(UObject* actor, const FVector& location)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Actor_SetLocation) + 16, 0);
	Field<FVector>(parms.data(), off.SetLocation_NewLocation) = location;
	UE3::ProcessNativeEvent(actor, fn.Actor_SetLocation, parms.data());
	return (Field<uint32_t>(parms.data(), off.SetLocation_ReturnValue) & 1) != 0;
}

void PossessPawn(UObject* controller, UObject* pawn)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.AI_Possess) + 16, 0);
	Field<UObject*>(parms.data(), off.Possess_inPawn) = pawn;
	UE3::ProcessEvent(controller, fn.AI_Possess, parms.data());
}

UObject* DefaultObject(UObject* class_object)
{
	char path[256];
	UObject* package = Field<UObject*>(class_object, UE3::Offset::ObjectOuter);
	sprintf_s(path, sizeof(path), "%s.Default__%s", UE3::ObjectName(package), UE3::ObjectName(class_object));
	return UE3::FindObject(path);
}

UObject* LoadClass(const char* path)
{
	UObject* found = UE3::FindObject(path);
	if (found) {
		return found;
	}
	size_t length = strlen(path) + 1;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(look.DynamicLoadObject) + 16, 0);
	UE3::FString& name = Field<UE3::FString>(parms.data(), look.DynamicLoadObject_ObjectName);
	name.Data = (wchar_t*)UE3::Malloc((uint32_t)(length * sizeof(wchar_t)), 8);
	if (!name.Data) {
		return 0;
	}
	for (size_t i = 0; i < length; i++) {
		name.Data[i] = (wchar_t)(unsigned char)path[i];
	}
	name.Num = name.Max = (int32_t)length;
	Field<UObject*>(parms.data(), look.DynamicLoadObject_ObjectClass) = look.ClassClass;
	Field<uint32_t>(parms.data(), look.DynamicLoadObject_MayFail) = 1;
	UE3::ProcessNativeEvent(look.ClassClass, look.DynamicLoadObject, parms.data());
	UE3::FreeArray(&name);
	return Field<UObject*>(parms.data(), look.DynamicLoadObject_ReturnValue);
}

UObject* PawnSkeletalMesh(UObject* pawn)
{
	UObject* mesh = pawn && look.Pawn_Mesh >= 0 ? Field<UObject*>(pawn, look.Pawn_Mesh) : 0;
	return mesh ? Field<UObject*>(mesh, look.SkeletalMeshComponent_SkeletalMesh) : 0;
}

struct Armour {
	UObject* SkeletalMesh;
	UE3::FName Voice;
	int32_t HealthPercent;
	int32_t SpeedPercent;
};

static bool FindArmour(int slot, Armour& armour)
{
	if (!look.DynamicLoadObject) {
		return false;
	}
	const char* path = config.Slots[slot].Look;
	UObject* lookClass = LoadClass(path);
	UObject* defaults = lookClass ? DefaultObject(lookClass) : 0;
	armour.SkeletalMesh = PawnSkeletalMesh(defaults);
	if (!armour.SkeletalMesh) {
		BOTS_LOG(LOG_LEVEL_WARN, "Bot %d: %s was not loaded, the bot keeps its look.", slot + 1, path);
		return false;
	}
	armour.Voice = Field<UE3::FName>(defaults, look.FoxPawn_DialogPawnName);
	int32_t voiceIndex = config.Slots[slot].Voice[0] ? UE3::FindNameIndex(config.Slots[slot].Voice) : -1;
	if (voiceIndex >= 0) {
		armour.Voice = UE3::FName{ voiceIndex, 0 };
	}
	armour.HealthPercent = Field<int32_t>(defaults, look.FoxPawn_PawnModifiers + look.ModInfo_HealthMax);
	armour.SpeedPercent = Field<int32_t>(defaults, look.FoxPawn_PawnModifiers + look.ModInfo_MovementSpeed);
	return true;
}

static UObject* GiveBotPawn(UObject* game, UObject* controller, const FVector& location, const UE3::FRotator& rotation)
{
	Bot* bot = FindBot(controller);
	Armour armour = {};
	bool dressed = bot && FindArmour(bot->Slot, armour);
	UObject* meshTemplate = dressed ? Field<UObject*>(DefaultObject(bot_pawn_class), look.Pawn_Mesh) : 0;
	UObject* classMesh = meshTemplate ? Field<UObject*>(meshTemplate, look.SkeletalMeshComponent_SkeletalMesh) : 0;
	if (meshTemplate) {
		Field<UObject*>(meshTemplate, look.SkeletalMeshComponent_SkeletalMesh) = armour.SkeletalMesh;
	}
	UObject* pawn = SpawnActor(controller, bot_pawn_class, location, rotation);
	if (meshTemplate) {
		Field<UObject*>(meshTemplate, look.SkeletalMeshComponent_SkeletalMesh) = classMesh;
	}
	if (!pawn) {
		return 0;
	}
	Field<float>(controller, off.AI_EvaluationInterval) = EVALUATION_INTERVAL;
	PossessPawn(controller, pawn);
	if (dressed) {
		Field<UE3::FName>(pawn, look.FoxPawn_DialogPawnName) = armour.Voice;
		bot->ArmourHealthPercent = config.ArmourBonus ? armour.HealthPercent : 0;
		if (config.ArmourBonus) {
			Field<float>(pawn, look.Pawn_GroundSpeed) *= (100.0f + armour.SpeedPercent) / 100.0f;
		}
		BOTS_LOG(LOG_LEVEL_INFO, "Bot %d wears %s (%s) with voice %s, armour health %+d%%, speed %+d%% (%.0f)."
			, bot->Slot + 1, config.Slots[bot->Slot].LookName, UE3::ObjectName(PawnSkeletalMesh(pawn)), UE3::NameEntryText(armour.Voice.Index)
			, bot->ArmourHealthPercent, config.ArmourBonus ? armour.SpeedPercent : 0, Field<float>(pawn, look.Pawn_GroundSpeed));
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Game_AddDefaultInventory) + 16, 0);
	Field<UObject*>(parms.data(), off.AddDefaultInventory_P) = pawn;
	UE3::ProcessEvent(game, fn.Game_AddDefaultInventory, parms.data());
	return pawn;
}

bool IsFreeSpot(UObject* any_actor, const FVector& point, UObject* ignore, float radius)
{
	UObject* world = Field<UObject*>(any_actor, off.Actor_WorldInfo);
	if (!world) {
		return false;
	}
	for (UObject* pawn = Field<UObject*>(world, off.WorldInfo_PawnList); pawn; pawn = Field<UObject*>(pawn, off.Pawn_NextPawn)) {
		if (pawn != ignore && Distance(Field<FVector>(pawn, off.Actor_Location), point) < radius) {
			return false;
		}
	}
	return true;
}

void FindNavMeshSpots(UObject* controller, const FVector& point, float radius, std::vector<FVector>& spots)
{
	spots.clear();
	UObject* handle = Field<UObject*>(controller, off.Controller_NavigationHandle);
	if (!handle) {
		return;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Handle_GetValidPositionsForBox) + 16, 0);
	Field<FVector>(parms.data(), off.ValidPositions_Pos) = point;
	Field<float>(parms.data(), off.ValidPositions_Radius) = radius;
	Field<FVector>(parms.data(), off.ValidPositions_Extent) = FVector{ PAWN_RADIUS, PAWN_RADIUS, PAWN_HALF_HEIGHT };
	UE3::ProcessNativeEvent(handle, fn.Handle_GetValidPositionsForBox, parms.data());
	TArray<FVector>& found = Field<TArray<FVector> >(parms.data(), off.ValidPositions_Out);
	if (found.Data) {
		for (int32_t i = 0; i < found.Num; i++) {
			if (!NavMesh::InModeCut(found.Data[i])) {
				spots.push_back(found.Data[i]);
			}
		}
		UE3::FreeArray(&found);
	}
	std::sort(spots.begin(), spots.end(), [&point](const FVector& a, const FVector& b) { return Distance(a, point) < Distance(b, point); });
}

bool FindFreeNavMeshSpot(UObject* controller, UObject* pawn, const FVector& point, float radius, FVector& spot)
{
	std::vector<FVector> spots;
	FindNavMeshSpots(controller, point, radius, spots);
	for (const FVector& candidate : spots) {
		if (IsFreeSpot(controller, candidate, pawn)) {
			spot = candidate;
			return true;
		}
	}
	return false;
}

bool FindFreeNavMeshSpotToward(UObject* controller, UObject* pawn, const FVector& point, float radius, const FVector& from, FVector& spot)
{
	std::vector<FVector> spots;
	FindNavMeshSpots(controller, point, radius, spots);
	std::sort(spots.begin(), spots.end(), [&from](const FVector& a, const FVector& b) { return Distance(a, from) < Distance(b, from); });
	for (const FVector& candidate : spots) {
		if (IsFreeSpot(controller, candidate, pawn)) {
			spot = candidate;
			return true;
		}
	}
	return false;
}

static void Reanchor(UObject* pawn)
{
	UObject* controller = Field<UObject*>(pawn, off.Pawn_Controller);
	UObject* handle = controller ? Field<UObject*>(controller, off.Controller_NavigationHandle) : 0;
	if (!handle) {
		return;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Handle_FindPylon) + 16, 0);
	UE3::ProcessNativeEvent(handle, fn.Handle_FindPylon, parms.data());
	if (IsBot(controller)) {
		Field<FVector>(controller, off.AI_LastDestination) = FVector{};
	}
}

bool StandsOnNavMesh(UObject* controller, const FVector& location)
{
	if (navmesh_ready) {
		return NavMesh::PylonAt(location) != 0 && !NavMesh::InModeCut(location, NavMesh::CUT_STAND_INSET);
	}
	std::vector<FVector> spots;
	FindNavMeshSpots(controller, location, ON_NAVMESH_DISTANCE, spots);
	return !spots.empty();
}

bool MovePawnToSpot(UObject* pawn, const FVector& spot)
{
	FVector before = Field<FVector>(pawn, off.Actor_Location);
	FVector raised = { spot.X, spot.Y, spot.Z + SPOT_LIFT };
	const FVector* target = &spot;
	if (!MoveActor(pawn, spot)) {
		target = &raised;
		if (!MoveActor(pawn, raised)) {
			return false;
		}
	}
	if (Distance(Field<FVector>(pawn, off.Actor_Location), *target) > SPOT_SHIFT_LIMIT) {
		MoveActor(pawn, before);
		return false;
	}
	Reanchor(pawn);
	return true;
}

bool ClearLine(UObject* actor, const FVector& from, const FVector& to)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Actor_FastTrace) + 16, 0);
	Field<FVector>(parms.data(), off.FastTrace_TraceEnd) = to;
	Field<FVector>(parms.data(), off.FastTrace_TraceStart) = from;
	UE3::SetBoolProperty((UObject*)parms.data(), cls.TraceBulletProperty, true);
	UE3::ProcessNativeEvent(actor, fn.Actor_FastTrace, parms.data());
	return UE3::GetBoolProperty((UObject*)parms.data(), cls.FastTraceResultProperty);
}

bool PawnSeesPawn(UObject* pawn, UObject* other)
{
	FVector eye = Field<FVector>(pawn, off.Actor_Location);
	eye.Z += Field<float>(pawn, off.Pawn_EyeHeight);
	FVector chest = Field<FVector>(other, off.Actor_Location);
	FVector head = chest;
	chest.Z += TARGET_CHEST_UP;
	head.Z += Field<float>(other, off.Pawn_EyeHeight);
	return ClearLine(pawn, eye, chest) || ClearLine(pawn, eye, head);
}

bool PawnSeesPoint(UObject* pawn, const FVector& point)
{
	FVector eye = Field<FVector>(pawn, off.Actor_Location);
	eye.Z += Field<float>(pawn, off.Pawn_EyeHeight);
	return ClearLine(pawn, eye, FVector{ point.X, point.Y, point.Z + TARGET_CHEST_UP });
}

bool FindSightSpot(UObject* controller, UObject* pawn, float radius, const FVector& point, float min_distance, FVector& spot)
{
	const FVector location = Field<FVector>(pawn, off.Actor_Location);
	std::vector<FVector> spots;
	FindNavMeshSpots(controller, location, radius, spots);
	FVector chest = { point.X, point.Y, point.Z + TARGET_CHEST_UP };
	size_t tried = 0;
	for (size_t i = 0; i < spots.size() && tried < SIGHT_SPOT_CANDIDATES; i++) {
		if (Distance(spots[i], point) < min_distance || Distance(spots[i], location) < SIGHT_SPOT_MIN_MOVE || !IsFreeSpot(pawn, spots[i], pawn)) {
			continue;
		}
		tried++;
		FVector eye = { spots[i].X, spots[i].Y, spots[i].Z + SIGHT_SPOT_EYE_UP };
		if (ClearLine(pawn, eye, chest)) {
			spot = spots[i];
			return true;
		}
	}
	return false;
}

UObject* CurrentBehavior(UObject* controller)
{
	return Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
}

static bool OwnedBy(UObject* object, UObject* owner)
{
	for (int depth = 0; object && depth < 4; depth++) {
		object = Field<UObject*>(object, UE3::Offset::ObjectOuter);
		if (object == owner) {
			return true;
		}
	}
	return false;
}

void SetUpCommands(Bot& bot)
{
	UObject* commands = Field<UObject*>(bot.Controller, off.AI_ScriptedBehavior + BEHAVIOR_INFO_BEHAVIOR);
	if (!commands) {
		return;
	}
	TArray<UObject*>& children = Field<TArray<UObject*> >(commands, off.Ability_SubActions);
	for (int32_t i = 0; i < children.Num; i++) {
		UObject* child = children.Data[i];
		if (!child || UE3::ObjectClass(child) != cls.GoToActionClass) {
			continue;
		}
		if (!OwnedBy(child, bot.Controller)) {
			BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x shares its GoTo command with its class, so it walks and only fights when it arrives.", bot.Controller);
			continue;
		}
		UE3::SetBoolProperty(child, cls.CanAbortProperty, true);
		UE3::SetBoolProperty(child, cls.RunToDestinationProperty, false);
		bot.GoToAction = child;
	}
}

void SetGoToSprint(Bot& bot, bool sprint)
{
	if (bot.GoToAction) {
		UE3::SetBoolProperty(bot.GoToAction, cls.RunToDestinationProperty, sprint);
	}
}

void SetGoToAbortable(Bot& bot, bool abortable)
{
	if (bot.GoToAction) {
		UE3::SetBoolProperty(bot.GoToAction, cls.CanAbortProperty, abortable);
	}
}

static int FindBehaviors(UObject* controller, UObject* ability_class, UObject** found, int limit)
{
	TArray<uint8_t>& behaviors = Field<TArray<uint8_t> >(controller, off.AI_Behaviors);
	int count = 0;
	for (int32_t i = 0; i < behaviors.Num && count < limit; i++) {
		UObject* behavior = Field<UObject*>(behaviors.Data + i * BEHAVIOR_INFO_SIZE, BEHAVIOR_INFO_BEHAVIOR);
		if (behavior && UE3::IsA(behavior, ability_class)) {
			found[count++] = behavior;
		}
	}
	return count;
}

void SetUpFightFromCover(Bot& bot)
{
	if (config.SuppressedInCover) {
		return;
	}
	UObject* abilities[4];
	int count = FindBehaviors(bot.Controller, cls.FightFromCoverClass, abilities, 4);
	for (int i = 0; i < count; i++) {
		TArray<UObject*>& actions = Field<TArray<UObject*> >(abilities[i], off.Ability_SubActions);
		for (int32_t j = 0; j < actions.Num; j++) {
			UObject* action = actions.Data[j];
			if (!action || !UE3::IsA(action, cls.FireWeaponClass)) {
				continue;
			}
			if (!OwnedBy(action, bot.Controller)) {
				BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x shares its cover fire action with its class, so it ducks while enemies fire.", bot.Controller);
				continue;
			}
			UE3::SetBoolProperty(action, cls.StopFiringIfSuppressedProperty, false);
			Field<TArray<UObject*> >(action, off.FireWeapon_SuppressionConditions).Num = 0;
		}
	}
}

void RemoveMelee(Bot& bot)
{
	TArray<uint8_t>& behaviors = Field<TArray<uint8_t> >(bot.Controller, off.AI_Behaviors);
	UObject* defaults = DefaultObject(UE3::ObjectClass(bot.Controller));
	if (!defaults || Field<TArray<uint8_t> >(defaults, off.AI_Behaviors).Data == behaviors.Data) {
		return;
	}
	for (int32_t i = 0; i < behaviors.Num; i++) {
		UObject* behavior = Field<UObject*>(behaviors.Data + i * BEHAVIOR_INFO_SIZE, BEHAVIOR_INFO_BEHAVIOR);
		if (!behavior || !UE3::IsValidObject(behavior)) {
			BOTS_LOG(LOG_LEVEL_WARN, "The module cannot read the behaviour list of bot 0x%08x, the bot keeps its knife charge.", bot.Controller);
			return;
		}
	}
	int32_t kept = 0;
	for (int32_t i = 0; i < behaviors.Num; i++) {
		uint8_t* entry = behaviors.Data + i * BEHAVIOR_INFO_SIZE;
		UObject* behavior = Field<UObject*>(entry, BEHAVIOR_INFO_BEHAVIOR);
		if (behavior && UE3::IsA(behavior, cls.FightMeleeClass)) {
			continue;
		}
		if (kept != i) {
			memcpy(behaviors.Data + kept * BEHAVIOR_INFO_SIZE, entry, BEHAVIOR_INFO_SIZE);
		}
		kept++;
	}
	behaviors.Num = kept;
}

void StopFiring(UObject* pawn)
{
	for (uint8_t mode = 0; mode < 2; mode++) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Pawn_StopFire) + 16, 0);
		Field<uint8_t>(parms.data(), off.StopFire_FireModeNum) = mode;
		UE3::ProcessEvent(pawn, fn.Pawn_StopFire, parms.data());
	}
}

static bool OnCanSeePawn(UObject* controller, UObject* other)
{
	if (!resolved || !IsBot(controller)) {
		return true;
	}
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!pawn || !UE3::IsA(other, cls.Pawn)) {
		return true;
	}
	UObject* link = Field<UObject*>(controller, off.AI_CurrentCover);
	if (Field<uint8_t>(pawn, off.FoxPawn_CoverType) != 0 && link && UE3::IsValidObject(link)
		&& CoverSeesPoint(pawn, link, Field<int32_t>(controller, off.AI_CurrentCover + 4), Field<FVector>(other, off.Actor_Location))) {
		return true;
	}
	return PawnSeesPawn(pawn, other);
}

void LeaveStaleCover(Bot& bot, UObject* pawn, DWORD now)
{
	UObject* link = Field<UObject*>(bot.Controller, off.AI_CurrentCover);
	int32_t slot = Field<int32_t>(bot.Controller, off.AI_CurrentCover + 4);
	UObject* target = FoxAI::GetActiveTarget(bot.Controller);
	bool inCover = Field<uint8_t>(pawn, off.FoxPawn_CoverType) != 0 && link && UE3::IsValidObject(link);
	if (!inCover || (target && CoverSeesPoint(pawn, link, slot, Field<FVector>(target, off.Actor_Location)))) {
		bot.CoverUnseenTick = 0;
		return;
	}
	if (!bot.CoverUnseenTick) {
		bot.CoverUnseenTick = now | 1;
		return;
	}
	if (now - bot.CoverUnseenTick < COVER_UNSEEN_MAX_MS) {
		return;
	}
	CoverBan ban = { link, slot, now + COVER_STALE_BAN_MS };
	bot.CoverBans.push_back(ban);
	bot.CoverUnseenTick = 0;
	BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x leaves cover 0x%08x/%d after %u ms with no line on its target.", bot.Controller, link, slot, COVER_UNSEEN_MAX_MS);
}

void SetUpBot(Bot& bot)
{
	SetUpCommands(bot);
	SetUpFightFromCover(bot);
	if (IsVersus()) {
		RemoveMelee(bot);
	}
	static bool warned = false;
	if (!FoxAI::HookCanSeePawn(bot.Controller, OnCanSeePawn) && !warned) {
		warned = true;
		BOTS_LOG(LOG_LEVEL_WARN, "The sight code of %s does not match, its bots also see enemies out of the line of fire.", UE3::ObjectName(UE3::ObjectClass(bot.Controller)));
	}
}

static UObject* GiveBotPawnBehind(UObject* game, UObject* controller, UObject* human_pawn)
{
	UObject* human = Field<UObject*>(human_pawn, off.Pawn_Controller);
	const FVector& humanLocation = Field<FVector>(human_pawn, off.Actor_Location);
	UE3::FRotator rotation = Field<UE3::FRotator>(human_pawn, off.Actor_Rotation);
	for (const Trail& trail : trails) {
		if (trail.Human != human) {
			continue;
		}
		for (int i = trail.Count; i-- > 0; ) {
			if (Distance(trail.Points[i], humanLocation) < TELEPORT_BEHIND || !IsFreeSpot(human_pawn, trail.Points[i], 0)) {
				continue;
			}
			FVector location = trail.Points[i];
			FindFreeNavMeshSpot(controller, 0, trail.Points[i], NAVMESH_NEAR_TRAIL, location);
			UObject* pawn = GiveBotPawn(game, controller, location, rotation);
			if (!pawn && Distance(location, trail.Points[i]) > 1.0f) {
				pawn = GiveBotPawn(game, controller, trail.Points[i], rotation);
			}
			if (pawn) {
				return pawn;
			}
		}
	}
	return 0;
}

static int Difficulty(UObject* game)
{
	int difficulty = game ? (int)Field<float>(game, off.GameInfo_GameDifficulty) : 0;
	return difficulty < 0 ? 0 : (difficulty >= DIFFICULTY_COUNT ? DIFFICULTY_COUNT - 1 : difficulty);
}

void ApplyBotSettings(UObject* controller, UObject* pawn, int slot, int32_t base_health, float health_regen)
{
	Bot* bot = FindBot(controller);
	int32_t health = base_health * (100 + (bot ? bot->ArmourHealthPercent : 0)) / 100;
	Field<int32_t>(pawn, off.Pawn_HealthMax) = health;
	Field<int32_t>(pawn, off.Pawn_Health) = health;
	Field<float>(pawn, off.FoxPawn_HealthRechargeMaxPercent) = health_regen;
	Field<float>(pawn, off.Pawn_SightRadius) = config.SightRadius;
	Field<float>(controller, off.AI_AggressionRange) = config.HiddenTargetRange;
	if (bot_pawn_class && UE3::ObjectClass(pawn) == bot_pawn_class) {
		UE3::SetBoolProperty(pawn, cls.CoverEnabledProperty, true);
	}
	TArray<UObject*>& coverConditions = Field<TArray<UObject*> >(controller, off.AI_CoverConditions);
	UObject* defaults = DefaultObject(UE3::ObjectClass(controller));
	bool ownArray = defaults && Field<TArray<UObject*> >(defaults, off.AI_CoverConditions).Data != coverConditions.Data;
	if (config.CoverAnyRange && ownArray) {
		int32_t kept = 0;
		for (int32_t i = 0; i < coverConditions.Num; i++) {
			UObject* condition = coverConditions.Data[i];
			if (!condition || UE3::ObjectClass(condition) != cls.EnemyRangeCondition) {
				coverConditions.Data[kept++] = condition;
			}
		}
		coverConditions.Num = kept;
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Bot %d has %d health.", slot + 1, health);
	float firingAngle = IsVersus() ? config.Versus.FiringAngle : config.FiringAngle;
	if (firingAngle > 0.0f) {
		Field<float>(controller, off.AI_Accuracy) = firingAngle;
	}
	const char* grenade = config.Slots[slot].Grenade;
	if (grenade[0]) {
		UObject* grenadeClass = UE3::FindObject(grenade);
		if (!grenadeClass) {
			BOTS_LOG(LOG_LEVEL_WARN, "%s is not loaded, bot %d gets no grenade.", grenade, slot + 1);
			return;
		}
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.FoxPawn_AddInventory) + 16, 0);
		Field<UObject*>(parms.data(), off.AddInventory_InvClass) = grenadeClass;
		UE3::ProcessEvent(pawn, fn.FoxPawn_AddInventory, parms.data());
	}
}

UObject* SpawnBotController(UObject* spawner, int slot, uint8_t team, UObject** wave_pawn)
{
	const SlotConfig& slotConfig = config.Slots[slot];

	uint32_t savedClass = 0;
	char savedPreset[sizeof(SPAWNAIWAVE_PRESET)];
	UObject* controllerClass = UE3::FindObject(slotConfig.Controller);
	if (!controllerClass) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s is not loaded, bot %d uses %s.", slotConfig.Controller, slot + 1, ASSAULT_CONTROLLER_CLASS);
	}
	bool swapClass = spawnaiwave_controller_class && controllerClass && controllerClass != cls.AssaultController;
	bool swapPreset = spawnaiwave_preset && strcmp(slotConfig.WeaponPreset, SPAWNAIWAVE_PRESET) != 0;
	if (swapClass) {
		savedClass = *spawnaiwave_controller_class;
		*spawnaiwave_controller_class = (uint32_t)controllerClass;
	}
	if (swapPreset) {
		memcpy(savedPreset, spawnaiwave_preset, sizeof(savedPreset));
		memcpy(spawnaiwave_preset, slotConfig.WeaponPreset, sizeof(savedPreset));
	}

	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.PC_SpawnAIWave) + 16, 0);
	Field<int32_t>(parms.data(), off.SpawnAIWave_Number) = 1;
	spawning_ally = true;
	spawning_slot = slot;
	spawning_team = team;
	spawned_controller = 0;
	spawned_pawn = 0;
	UE3::ProcessEvent(spawner, fn.PC_SpawnAIWave, parms.data());
	spawning_ally = false;

	if (swapClass) {
		*spawnaiwave_controller_class = savedClass;
	}
	if (swapPreset) {
		memcpy(spawnaiwave_preset, savedPreset, sizeof(savedPreset));
	}

	if (!spawned_controller) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "SpawnAIWave did not create a bot.");
		return 0;
	}
	*wave_pawn = spawned_pawn;
	return spawned_controller;
}

static void SpawnBot(UObject* game, UObject* spawner, UObject* leader_pawn)
{
	int slot = FreeSlot();
	UObject* wavePawn = 0;
	UObject* controller = SpawnBotController(spawner, slot, BLACKLIGHT_TEAM, &wavePawn);
	if (!controller) {
		return;
	}
	UObject* pawn = 0;
	if (wavePawn && !bot_pawn_class) {
		PossessPawn(controller, wavePawn);
		pawn = wavePawn;
	}
	else if (wavePawn) {
		FVector location = Field<FVector>(wavePawn, off.Actor_Location);
		UE3::FRotator rotation = Field<UE3::FRotator>(wavePawn, off.Actor_Rotation);
		DestroyActor(wavePawn);
		pawn = GiveBotPawn(game, controller, location, rotation);
	}
	if (!pawn && bot_pawn_class && leader_pawn) {
		pawn = GiveBotPawnBehind(game, controller, leader_pawn);
		if (pawn) {
			BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x had no free player start and spawned behind a human.", controller);
		}
	}
	if (!pawn || Field<UObject*>(controller, off.Controller_Pawn) != pawn) {
		BOTS_LOG(LOG_LEVEL_WARN, "Bot 0x%08x did not possess its pawn.", controller);
		RemoveBotController(controller);
		return;
	}
	for (Bot& bot : bots) {
		if (bot.Controller == controller) {
			bot.LastMoveLocation = Field<FVector>(pawn, off.Actor_Location);
			bot.LastMoveTick = GetTickCount();
			SetUpBot(bot);
		}
	}
	ApplyBotSettings(controller, pawn, slot, config.Health[Difficulty(current_game)], config.HealthRegen);
	BOTS_LOG(LOG_LEVEL_INFO, "Spawned bot %d 0x%08x with pawn 0x%08x (%s), controller %s, %s."
		, slot + 1, controller, pawn, UE3::ObjectName(UE3::ObjectClass(pawn))
		, UE3::ObjectName(UE3::ObjectClass(controller)), spawnaiwave_preset ? config.Slots[slot].WeaponPreset : SPAWNAIWAVE_PRESET);
}

static UObject* HumanSquadLeader(UObject* game, int32_t* member_count);

static void ReadHostPath()
{
	host_path.clear();
	char text[4096] = {};
	if (GetEnvironmentVariableA("BLTD_HOST_PATH", text, sizeof(text)) == 0) {
		return;
	}
	for (char* item = strtok(text, ";"); item; item = strtok(0, ";")) {
		Waypoint point = {};
		if (sscanf_s(item, "%f,%f,%f@%f", &point.At.X, &point.At.Y, &point.At.Z, &point.Time) == 4) {
			host_path.push_back(point);
		}
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Test host: a route of %u points over %.0f s.", (uint32_t)host_path.size(), host_path.empty() ? 0.0f : host_path.back().Time);
}

static void DriveHost(UObject* game)
{
	DWORD now = GetTickCount();
	if (host_path.empty() || !match_start_tick || now - match_start_tick < TEST_HOST_MOVE_DELAY_MS || now - host_path_tick < HOST_PATH_STEP_MS) {
		return;
	}
	float step = (float)(now - host_path_tick) / 1000.0f;
	host_path_tick = now;
	float elapsed = (float)(now - match_start_tick - TEST_HOST_MOVE_DELAY_MS) / 1000.0f;
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	UObject* pawn = 0;
	for (UObject* human : humans) {
		UObject* candidate = Field<UObject*>(human, off.Controller_Pawn);
		if (!pawn && IsAlive(candidate)) {
			pawn = candidate;
		}
	}
	if (!pawn) {
		return;
	}
	const Waypoint* goal = &host_path.back();
	for (const Waypoint& point : host_path) {
		if (point.Time >= elapsed) {
			goal = &point;
			break;
		}
	}
	FVector at = Field<FVector>(pawn, off.Actor_Location);
	float dx = goal->At.X - at.X;
	float dy = goal->At.Y - at.Y;
	float dz = goal->At.Z - at.Z;
	float distance = sqrtf(dx * dx + dy * dy + dz * dz);
	if (distance < 10.0f) {
		return;
	}
	float move = (std::min)(distance, HOST_PATH_SPEED * (std::min)(step, 0.5f));
	FVector next = { at.X + dx / distance * move, at.Y + dy / distance * move, at.Z + dz / distance * move };
	UE3::FRotator rotation = Field<UE3::FRotator>(pawn, off.Actor_Rotation);
	rotation.Yaw = (int32_t)(atan2f(dy, dx) / ROTATOR_TO_RADIANS);
	UObject* controller = Field<UObject*>(pawn, off.Pawn_Controller);
	if (controller) {
		Field<UE3::FRotator>(controller, off.Actor_Rotation) = rotation;
	}
	Field<UE3::FRotator>(pawn, off.Actor_Rotation) = rotation;
	FVector raised = { next.X, next.Y, next.Z + SPOT_LIFT };
	if (MoveActor(pawn, next) || MoveActor(pawn, raised)) {
		Field<FVector>(pawn, off.Actor_Velocity) = FVector{ dx / distance * HOST_PATH_SPEED, dy / distance * HOST_PATH_SPEED, 0.0f };
		host_blocked_tick = 0;
		return;
	}
	if (!host_blocked_tick) {
		host_blocked_tick = now | 1;
	}
	else if (now - host_blocked_tick >= HOST_BLOCKED_MS) {
		host_blocked_tick = 0;
		FVector lifted = { goal->At.X, goal->At.Y, goal->At.Z + SPOT_LIFT };
		FVector higher = { goal->At.X, goal->At.Y, goal->At.Z + 3.0f * SPOT_LIFT };
		bool jumped = MoveActor(pawn, goal->At) || MoveActor(pawn, lifted) || MoveActor(pawn, higher);
		BOTS_LOG(LOG_LEVEL_INFO, "Test host: blocked at (%.0f %.0f %.0f), jump to (%.0f %.0f %.0f) %s.", at.X, at.Y, at.Z, goal->At.X, goal->At.Y, goal->At.Z, jumped ? "done" : "failed");
	}
}

static void SpawnTestEnemies(UObject* game, int count)
{
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	UObject* hostPawn = 0;
	for (UObject* human : humans) {
		UObject* pawn = Field<UObject*>(human, off.Controller_Pawn);
		if (!hostPawn && IsAlive(pawn)) {
			hostPawn = pawn;
		}
	}
	UObject* enemyClass = UE3::FindObject(ENEMY_PAWN_CLASS);
	UObject* navigator = bots.empty() ? 0 : bots[0].Controller;
	if (!hostPawn || !enemyClass || !navigator) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "Test enemies: no host pawn, enemy class or bot to search the navmesh with.");
		return;
	}
	const FVector host = Field<FVector>(hostPawn, off.Actor_Location);
	FVector chest = { host.X, host.Y, host.Z + TARGET_CHEST_UP };
	std::vector<FVector> spots;
	FindNavMeshSpots(navigator, host, TEST_ENEMY_SEARCH, spots);
	std::vector<FVector> chosen;
	for (int pass = 0; pass < 2 && (int)chosen.size() < count; pass++) {
		for (size_t i = 0; i < spots.size() && (int)chosen.size() < count; i++) {
			float away = Distance(spots[i], host);
			if (away < TEST_ENEMY_MIN || away > TEST_ENEMY_MAX) {
				continue;
			}
			bool apart = true;
			for (const FVector& other : chosen) {
				apart = apart && Distance(other, spots[i]) >= TEST_ENEMY_SPACING;
			}
			FVector eye = { spots[i].X, spots[i].Y, spots[i].Z + SPOT_LIFT + TARGET_CHEST_UP };
			if (!apart || (pass == 0 && !ClearLine(hostPawn, eye, chest))) {
				continue;
			}
			chosen.push_back(spots[i]);
		}
	}
	for (const FVector& spot : chosen) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.PC_SpawnAIWave) + 16, 0);
		Field<int32_t>(parms.data(), off.SpawnAIWave_Number) = 1;
		spawning_enemy = true;
		spawning_team = ORDER_TEAM;
		spawned_controller = 0;
		spawned_pawn = 0;
		UE3::ProcessEvent(humans[0], fn.PC_SpawnAIWave, parms.data());
		spawning_enemy = false;
		if (!spawned_controller) {
			continue;
		}
		if (spawned_pawn) {
			DestroyActor(spawned_pawn);
		}
		UE3::FRotator rotation = {};
		rotation.Yaw = (int32_t)(atan2f(host.Y - spot.Y, host.X - spot.X) / ROTATOR_TO_RADIANS);
		UObject* pawn = SpawnActor(spawned_controller, enemyClass, spot, rotation);
		if (!pawn) {
			FVector raised = { spot.X, spot.Y, spot.Z + SPOT_LIFT };
			pawn = SpawnActor(spawned_controller, enemyClass, raised, rotation);
		}
		if (!pawn) {
			BOTS_LOG(LOG_LEVEL_WARN, "Test enemies: no pawn fits at (%.0f %.0f %.0f).", spot.X, spot.Y, spot.Z);
			continue;
		}
		PossessPawn(spawned_controller, pawn);
		std::vector<uint8_t> inventoryParms(UE3::FunctionParmsSize(fn.Game_AddDefaultInventory) + 16, 0);
		Field<UObject*>(inventoryParms.data(), off.AddDefaultInventory_P) = pawn;
		UE3::ProcessEvent(game, fn.Game_AddDefaultInventory, inventoryParms.data());
		BOTS_LOG(LOG_LEVEL_INFO, "Test enemy 0x%08x with pawn 0x%08x at (%.0f %.0f %.0f), %.0f from the host.", spawned_controller, pawn, spot.X, spot.Y, spot.Z, Distance(spot, host));
	}
	BOTS_LOG(LOG_LEVEL_INFO, "Test enemies: %u of %d placed, %u navmesh spots around the host.", (uint32_t)chosen.size(), count, (uint32_t)spots.size());
}

static void FillEmptySlots(UObject* game)
{
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	if (humans.empty()) {
		return;
	}
	int32_t missing = EmptySlots(game, humans.size());
	if (missing <= 0) {
		return;
	}
	bot_pawn_class = UE3::FindObject(BOT_PAWN_CLASS);
	if (!bot_pawn_class) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s is not loaded, the bots keep the player pawn.", BOT_PAWN_CLASS);
	}
	else {
		UObject* defaults = DefaultObject(bot_pawn_class);
		if (defaults) {
			UE3::SetBoolProperty(defaults, cls.CoverEnabledProperty, true);
		}
		else {
			BOTS_LOG(LOG_LEVEL_WARN, "%s has no class default object.", BOT_PAWN_CLASS);
		}
	}
	UObject* leaderPawn = 0;
	for (UObject* human : humans) {
		UObject* humanPawn = Field<UObject*>(human, off.Controller_Pawn);
		if (!leaderPawn && IsAlive(humanPawn)) {
			leaderPawn = humanPawn;
		}
	}
	for (; missing > 0; missing--) {
		SpawnBot(game, humans[0], leaderPawn);
	}
	int32_t memberCount;
	UObject* leader = HumanSquadLeader(game, &memberCount);
	if (leader) {
		TArray<UObject*>& squads = Field<TArray<UObject*> >(Field<TArray<UObject*> >(game, off.Game_Teams).Data[BLACKLIGHT_TEAM], off.Team_Squads);
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.Squad_SetSquadBehavior) + 16, 0);
		Field<uint8_t>(parms.data(), off.SetSquadBehavior_NewSquadBehavior) = config.SquadBehavior;
		UE3::ProcessNativeEvent(squads.Data[0], fn.Squad_SetSquadBehavior, parms.data());
	}
}

void SendBehaviorEvent(UObject* controller, const wchar_t* event_name, UObject* instigator, UObject* recipient, const FVector& location)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.AI_ProcessBehaviorEvent) + 16, 0);
	size_t length = wcslen(event_name) + 1;
	UE3::FString& eventName = Field<UE3::FString>(parms.data(), off.ProcessBehaviorEvent_EventName);
	eventName.Data = (wchar_t*)event_name;
	eventName.Num = (int32_t)length;
	eventName.Max = (int32_t)length;
	Field<UObject*>(parms.data(), off.ProcessBehaviorEvent_EventInstigator) = instigator;
	Field<UObject*>(parms.data(), off.ProcessBehaviorEvent_EventRecipient) = recipient;
	Field<FVector>(parms.data(), off.ProcessBehaviorEvent_EventLocation) = location;
	UE3::ProcessEvent(controller, fn.AI_ProcessBehaviorEvent, parms.data());
}

static void SendCommand(Bot& bot, const wchar_t* command, UObject* leader_pawn)
{
	SendBehaviorEvent(bot.Controller, command, leader_pawn, leader_pawn, Field<FVector>(leader_pawn, off.Actor_Location));
	bot.LastCommandTick = GetTickCount();
}

static UObject* HumanSquadLeader(UObject* game, int32_t* member_count)
{
	*member_count = -1;
	TArray<UObject*>& teams = Field<TArray<UObject*> >(game, off.Game_Teams);
	if (teams.Num <= BLACKLIGHT_TEAM || !teams.Data[BLACKLIGHT_TEAM]) {
		return 0;
	}
	TArray<UObject*>& squads = Field<TArray<UObject*> >(teams.Data[BLACKLIGHT_TEAM], off.Team_Squads);
	if (squads.Num < 1 || !squads.Data[0]) {
		return 0;
	}
	*member_count = Field<TArray<uint8_t> >(squads.Data[0], off.Squad_SquadMembers).Num;
	return Field<UObject*>(squads.Data[0], off.Squad_SquadLeader);
}

static void UpdateTrails(std::vector<UObject*>& humans)
{
	for (size_t i = trails.size(); i-- > 0; ) {
		bool present = false;
		for (UObject* human : humans) {
			present = present || trails[i].Human == human;
		}
		if (!present) {
			trails.erase(trails.begin() + i);
		}
	}
	for (UObject* human : humans) {
		UObject* humanPawn = Field<UObject*>(human, off.Controller_Pawn);
		if (!IsAlive(humanPawn)) {
			continue;
		}
		uint8_t physics = Field<uint8_t>(humanPawn, off.Actor_Physics);
		if (physics != PHYS_WALKING && physics != PHYS_NAVMESHWALKING) {
			continue;
		}
		Trail* trail = 0;
		for (Trail& candidate : trails) {
			if (candidate.Human == human) {
				trail = &candidate;
			}
		}
		if (!trail) {
			Trail added = {};
			added.Human = human;
			trails.push_back(added);
			trail = &trails.back();
		}
		const FVector& location = Field<FVector>(humanPawn, off.Actor_Location);
		if (trail->Count > 0 && Distance(trail->Points[trail->Count - 1], location) < TRAIL_SPACING) {
			continue;
		}
		if (trail->Count == TRAIL_LENGTH) {
			memmove(&trail->Points[0], &trail->Points[1], sizeof(FVector) * (TRAIL_LENGTH - 1));
			trail->Count--;
		}
		trail->Points[trail->Count++] = location;
	}
}

bool CanClaimCover(UObject* pawn, UObject* link, int32_t slot)
{
	std::vector<uint8_t> claimParms(UE3::FunctionParmsSize(fn.CoverLink_IsValidClaim) + 16, 0);
	Field<UObject*>(claimParms.data(), off.IsValidClaim_ChkClaim) = pawn;
	Field<int32_t>(claimParms.data(), off.IsValidClaim_SlotIdx) = slot;
	UE3::ProcessNativeEvent(link, fn.CoverLink_IsValidClaim, claimParms.data());
	return (Field<uint32_t>(claimParms.data(), off.IsValidClaim_ReturnValue) & 1) != 0;
}

UObject* HumanSquad(UObject* game)
{
	TArray<UObject*>& teams = Field<TArray<UObject*> >(game, off.Game_Teams);
	if (teams.Num <= BLACKLIGHT_TEAM || !teams.Data[BLACKLIGHT_TEAM]) {
		return 0;
	}
	TArray<UObject*>& squads = Field<TArray<UObject*> >(teams.Data[BLACKLIGHT_TEAM], off.Team_Squads);
	return squads.Num > 0 ? squads.Data[0] : 0;
}

int WeaponRole(int slot)
{
	const char* preset = slot >= 0 && slot < SLOT_COUNT ? config.Slots[slot].WeaponPreset : "";
	if (strstr(preset, "_SNI_")) {
		return ROLE_SNIPER;
	}
	return strstr(preset, "_SHO_") ? ROLE_SHOTGUN : ROLE_RIFLE;
}

static bool FindCoverNear(UObject* pawn, const FVector& point, FVector& slot_location)
{
	UObject* world = Field<UObject*>(pawn, off.Actor_WorldInfo);
	if (!world) {
		return false;
	}
	float best = COVER_NEAR_TRAIL;
	bool found = false;
	for (UObject* link = Field<UObject*>(world, off.WorldInfo_CoverList); link; link = Field<UObject*>(link, off.CoverLink_NextCoverLink)) {
		if (Distance(Field<FVector>(link, off.Actor_Location), point) > COVER_LINK_SEARCH) {
			continue;
		}
		int32_t slotCount = Field<TArray<uint8_t> >(link, off.CoverLink_Slots).Num;
		for (int32_t slot = 0; slot < slotCount; slot++) {
			std::vector<uint8_t> parms(UE3::FunctionParmsSize(fn.CoverLink_GetSlotLocation) + 16, 0);
			Field<int32_t>(parms.data(), off.GetSlotLocation_SlotIdx) = slot;
			UE3::ProcessNativeEvent(link, fn.CoverLink_GetSlotLocation, parms.data());
			FVector location = Field<FVector>(parms.data(), off.GetSlotLocation_ReturnValue);
			float distance = Distance(location, point);
			if (distance >= best || !IsFreeSpot(pawn, location, pawn) || !CanClaimCover(pawn, link, slot)) {
				continue;
			}
			best = distance;
			slot_location = location;
			found = true;
		}
	}
	return found;
}

static bool TeleportBehind(UObject* pawn, UObject* human_pawn)
{
	UObject* controller = Field<UObject*>(pawn, off.Pawn_Controller);
	UObject* human = Field<UObject*>(human_pawn, off.Pawn_Controller);
	const FVector& humanLocation = Field<FVector>(human_pawn, off.Actor_Location);
	std::vector<FVector> spots;
	for (const Trail& trail : trails) {
		if (trail.Human != human) {
			continue;
		}
		for (int i = trail.Count; i-- > 0; ) {
			const FVector& point = trail.Points[i];
			if (Distance(point, humanLocation) < TELEPORT_BEHIND) {
				continue;
			}
			FVector coverLocation;
			if (config.TeleportToCover && FindCoverNear(pawn, point, coverLocation) && Distance(coverLocation, humanLocation) >= TELEPORT_BEHIND) {
				FindNavMeshSpots(controller, coverLocation, ON_NAVMESH_DISTANCE, spots);
				if (!spots.empty() && MoveActor(pawn, coverLocation)) {
					Reanchor(pawn);
					return true;
				}
			}
			FVector spot;
			if (FindFreeNavMeshSpot(controller, pawn, point, NAVMESH_NEAR_TRAIL, spot) && MovePawnToSpot(pawn, spot)) {
				return true;
			}
		}
	}
	FVector spot;
	if (FindFreeNavMeshSpot(controller, pawn, humanLocation, NAVMESH_NEAR_HUMAN, spot) && MovePawnToSpot(pawn, spot)) {
		return true;
	}
	return false;
}

static void StartGrace(UObject* pawn)
{
	UObject* controller = Field<UObject*>(pawn, off.Pawn_Controller);
	for (Bot& bot : bots) {
		if (bot.Controller == controller) {
			bot.GraceUntilTick = GetTickCount() + config.TeleportGraceMs;
		}
	}
}

bool IsFighting(UObject* controller)
{
	UObject* current = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	return current
		&& current != Field<UObject*>(controller, off.AI_DefaultBehavior + BEHAVIOR_INFO_BEHAVIOR)
		&& current != Field<UObject*>(controller, off.AI_ScriptedBehavior + BEHAVIOR_INFO_BEHAVIOR);
}

bool IsIdle(UObject* controller)
{
	UObject* current = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	return !current || current == Field<UObject*>(controller, off.AI_DefaultBehavior + BEHAVIOR_INFO_BEHAVIOR);
}

bool IsFollowingCommand(UObject* controller)
{
	UObject* current = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	return current && current == Field<UObject*>(controller, off.AI_ScriptedBehavior + BEHAVIOR_INFO_BEHAVIOR);
}

bool CoverFacing(UObject* controller, UObject* link, int32_t slot, float& facing)
{
	UObject* target = FoxAI::GetActiveTarget(controller);
	if (!target) {
		return false;
	}
	FVector slotLocation = FoxAI::GetSlotLocation(link, slot);
	const FVector& targetLocation = Field<FVector>(target, off.Actor_Location);
	float dx = targetLocation.X - slotLocation.X;
	float dy = targetLocation.Y - slotLocation.Y;
	float length = sqrtf(dx * dx + dy * dy);
	if (length <= 0.0f) {
		return false;
	}
	float yaw = (float)FoxAI::GetSlotRotation(link, slot).Yaw * ROTATOR_TO_RADIANS;
	facing = (cosf(yaw) * dx + sinf(yaw) * dy) / length;
	return true;
}

bool CoverSeesPoint(UObject* pawn, UObject* link, int32_t slot, const FVector& target)
{
	TArray<uint8_t>& slots = Field<TArray<uint8_t> >(link, off.CoverLink_Slots);
	if (slot < 0 || slot >= slots.Num) {
		return false;
	}
	uint8_t* slotData = slots.Data + slot * COVER_SLOT_SIZE;
	uint8_t type = slotData[COVER_SLOT_TYPE];
	uint32_t flags = Field<uint32_t>(slotData, COVER_SLOT_FLAGS);
	FVector base = FoxAI::GetSlotLocation(link, slot);
	float yaw = (float)FoxAI::GetSlotRotation(link, slot).Yaw * ROTATOR_TO_RADIANS;
	FVector right = { -sinf(yaw), cosf(yaw), 0.0f };
	FVector chest = { target.X, target.Y, target.Z + TARGET_CHEST_UP };
	FVector eyes[5];
	int count = 0;
	if (type == COVER_TYPE_MID && (flags & SLOT_CAN_POPUP)) {
		eyes[count++] = FVector{ base.X, base.Y, base.Z + PEEK_EYE_UP };
	}
	for (int side = -1; side <= 1; side += 2) {
		if (!(flags & (side < 0 ? SLOT_LEAN_LEFT : SLOT_LEAN_RIGHT))) {
			continue;
		}
		float sideNear = PEEK_LEAN_NEAR * (float)side;
		float sideFar = PEEK_LEAN_FAR * (float)side;
		eyes[count++] = FVector{ base.X + right.X * sideNear, base.Y + right.Y * sideNear, base.Z + PEEK_EYE_UP };
		eyes[count++] = FVector{ base.X + right.X * sideFar, base.Y + right.Y * sideFar, base.Z + PEEK_EYE_UP };
	}
	for (int i = 0; i < count; i++) {
		if (ClearLine(pawn, eyes[i], chest)) {
			return true;
		}
	}
	return false;
}

static float RateCover(UObject* controller, const FoxAI::CoverInfo* cover, float rating)
{
	if (!resolved || rating <= -1.0f) {
		return rating;
	}
	Bot* bot = FindBot(controller);
	if (!bot) {
		return rating;
	}
	if (IsVersus() && !bot->UsesCover) {
		return -1.0f;
	}
	DWORD now = GetTickCount();
	for (const CoverBan& ban : bot->CoverBans) {
		if (ban.Link == cover->Link && ban.Slot == cover->SlotIdx && (int32_t)(ban.UntilTick - now) > 0) {
			return -1.0f;
		}
	}
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!pawn) {
		return rating;
	}
	float distance = Distance(Field<FVector>(pawn, off.Actor_Location), FoxAI::GetSlotLocation(cover->Link, cover->SlotIdx));
	if (config.CoverMaxDistance > 0.0f && distance > config.CoverMaxDistance) {
		return -1.0f;
	}
	float preference = IsVersus() ? config.Versus.CoverPreference : config.CoverPreference;
	float facing;
	UObject* target = FoxAI::GetActiveTarget(controller);
	if (!target || !CoverFacing(controller, cover->Link, cover->SlotIdx, facing)) {
		preference = 0.0f;
	}
	else if (facing < 0.0f || !CoverSeesPoint(pawn, cover->Link, cover->SlotIdx, Field<FVector>(target, off.Actor_Location))) {
		return -1.0f;
	}
	else if (facing < COVER_FACING) {
		preference = 0.0f;
	}
	return rating + preference - config.CoverDistancePenalty * distance / 1000.0f;
}

static float OnCoverRating(UObject* controller, const FoxAI::CoverInfo* cover, float rating)
{
	float result = RateCover(controller, cover, rating);
	if (config.Trace) {
		Trace::OnCoverRating(controller, cover, rating, result);
	}
	return result;
}

static void OnSelectTargets(UObject* squad)
{
	if (!resolved) {
		return;
	}
	for (const Bot& bot : bots) {
		UObject* pawn = UE3::IsValidObject(bot.Controller) ? Field<UObject*>(bot.Controller, off.Controller_Pawn) : 0;
		if (!pawn || !UE3::IsValidObject(pawn)) {
			continue;
		}
		bool inCover = Field<uint8_t>(pawn, off.FoxPawn_CoverType) != 0;
		Field<float>(bot.Controller, off.AI_AggressionRange) = inCover ? (std::max)(config.HiddenTargetRange, config.SightRadius) : config.HiddenTargetRange;
	}
}

static void OnCoverActions(UObject* controller, TArray<int32_t>* actions)
{
	if (!resolved || !IsVersus() || !IsBot(controller)) {
		return;
	}
	bool aimed = false;
	for (int32_t i = 0; i < actions->Num; i++) {
		aimed = aimed || actions->Data[i] == CA_LEAN_LEFT || actions->Data[i] == CA_LEAN_RIGHT || actions->Data[i] == CA_POP_UP;
	}
	if (!aimed) {
		return;
	}
	int32_t kept = 0;
	for (int32_t i = 0; i < actions->Num; i++) {
		int32_t action = actions->Data[i];
		if (action != CA_BLIND_LEFT && action != CA_BLIND_RIGHT && action != CA_BLIND_UP) {
			actions->Data[kept++] = action;
		}
	}
	actions->Num = kept;
}

static uint32_t OnFollowInitialized(UObject* ability, uint32_t result)
{
	if (!resolved) {
		return result;
	}
	Bot* bot = FindBot(Field<UObject*>(ability, off.Ability_Owner));
	if (!bot || !IsAlive(bot->TargetPawn)) {
		return result;
	}
	TArray<UObject*>& subActions = Field<TArray<UObject*> >(ability, off.Ability_SubActions);
	UObject* followAction = subActions.Num > 0 ? subActions.Data[0] : 0;
	if (!followAction || UE3::ObjectClass(followAction) != cls.FollowAction) {
		return result;
	}
	bool leaderMissing = !Field<UObject*>(followAction, off.FollowAction_FollowingPawn);
	Field<UObject*>(followAction, off.FollowAction_FollowingPawn) = bot->TargetPawn;
	if (!result && leaderMissing) {
		result = FoxAI::InitializeAbility(ability);
	}
	return result;
}

static void AssignTargets(std::vector<UObject*>& humans)
{
	std::vector<UObject*> living;
	for (UObject* human : humans) {
		if (IsAlive(Field<UObject*>(human, off.Controller_Pawn))) {
			living.push_back(human);
		}
	}
	std::vector<int> counts(living.size(), 0);
	std::vector<Bot*> unassigned;
	for (Bot& bot : bots) {
		bot.TargetPawn = 0;
		size_t index = std::find(living.begin(), living.end(), bot.TargetHuman) - living.begin();
		if (index < living.size()) {
			counts[index]++;
			bot.TargetPawn = Field<UObject*>(bot.TargetHuman, off.Controller_Pawn);
		}
		else {
			bot.TargetHuman = 0;
			unassigned.push_back(&bot);
		}
	}
	for (Bot* bot : unassigned) {
		UObject* pawn = Field<UObject*>(bot->Controller, off.Controller_Pawn);
		int best = -1;
		for (size_t i = 0; i < living.size(); i++) {
			if (best < 0 || counts[i] < counts[best]
				|| (counts[i] == counts[best] && IsAlive(pawn)
					&& DistanceSquared(pawn, Field<UObject*>(living[i], off.Controller_Pawn)) < DistanceSquared(pawn, Field<UObject*>(living[best], off.Controller_Pawn)))) {
				best = (int)i;
			}
		}
		if (best < 0) {
			continue;
		}
		counts[best]++;
		bot->TargetHuman = living[best];
		bot->TargetPawn = Field<UObject*>(living[best], off.Controller_Pawn);
	}
}

static void UnstickBot(Bot& bot, UObject* pawn, float distance, const char* reason, DWORD now)
{
	const FVector location = Field<FVector>(pawn, off.Actor_Location);
	std::vector<FVector> spots;
	FindNavMeshSpots(bot.Controller, location, ON_NAVMESH_DISTANCE, spots);
	bool onMesh = !spots.empty();
	if (now - bot.LastPathLogTick >= PATH_FAIL_LOG_INTERVAL_MS) {
		bot.LastPathLogTick = now;
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x at (%.0f %.0f %.0f) %s, its human is %.0f away, bot on the navmesh %d."
			, bot.Controller, location.X, location.Y, location.Z, reason, distance, onMesh ? 1 : 0);
	}
	if (!onMesh) {
		FindNavMeshSpots(bot.Controller, location, NAVMESH_NUDGE_RADIUS, spots);
		for (const FVector& spot : spots) {
			if (IsFreeSpot(pawn, spot, pawn) && MovePawnToSpot(pawn, spot)) {
				BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x stood off the navmesh and moved onto it at (%.0f %.0f %.0f).", bot.Controller, spot.X, spot.Y, spot.Z);
				bot.LastMoveLocation = Field<FVector>(pawn, off.Actor_Location);
				bot.LastMoveTick = now;
				bot.FollowStalls = 0;
				return;
			}
		}
	}
	if (distance > config.FollowDistance + TELEPORT_BEHIND && TeleportBehind(pawn, bot.TargetPawn)) {
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x moved behind its human from %.0f away.", bot.Controller, distance);
		bot.LastMoveLocation = Field<FVector>(pawn, off.Actor_Location);
		bot.LastMoveTick = now;
		bot.LastCommandTick = 0;
		bot.FollowStalls = 0;
		bot.GraceUntilTick = now + config.TeleportGraceMs;
	}
}

static void HandlePathFailures(Bot& bot, UObject* pawn, float distance, bool fighting, DWORD now)
{
	UObject* coverLink = Field<UObject*>(bot.Controller, off.AI_CurrentCover);
	int32_t coverSlot = Field<int32_t>(bot.Controller, off.AI_CurrentCover + 4);
	if (fighting && coverLink) {
		FVector slotLocation = FoxAI::GetSlotLocation(coverLink, coverSlot);
		if (Distance(slotLocation, bot.PathFailGoal) <= COVER_GOAL_DISTANCE) {
			CoverBan ban = { coverLink, coverSlot, now + config.CoverBanMs };
			bot.CoverBans.push_back(ban);
			return;
		}
	}
	if (now - bot.LastMoveTick >= PATH_FAIL_STILL_MS) {
		char reason[96];
		sprintf_s(reason, sizeof(reason), "found no path to (%.0f %.0f %.0f) %u times", bot.PathFailGoal.X, bot.PathFailGoal.Y, bot.PathFailGoal.Z, bot.PathFailures);
		UnstickBot(bot, pawn, distance, reason, now);
	}
}

static void Maintain(UObject* game)
{
	if (game != current_game) {
		return;
	}
	for (size_t i = bots.size(); i-- > 0; ) {
		if (!UE3::IsValidObject(bots[i].Controller)) {
			bots.erase(bots.begin() + i);
		}
	}

	std::vector<UObject*> humans;
	FindHumans(game, humans);
	while (!bots.empty() && EmptySlots(game, humans.size()) < 0) {
		RemoveBot(bots.size() - 1);
	}
	if (!host_moved && match_start_tick && GetTickCount() - match_start_tick >= TEST_HOST_MOVE_DELAY_MS) {
		host_moved = true;
		char text[64] = {};
		FVector at = {};
		if (GetEnvironmentVariableA("BLTD_HOST_AT", text, sizeof(text)) > 0 && sscanf_s(text, "%f,%f,%f", &at.X, &at.Y, &at.Z) == 3) {
			for (UObject* human : humans) {
				UObject* pawn = Field<UObject*>(human, off.Controller_Pawn);
				if (IsAlive(pawn)) {
					BOTS_LOG(LOG_LEVEL_INFO, "Test host: moved to (%.0f %.0f %.0f): %s.", at.X, at.Y, at.Z, MovePawnToSpot(pawn, at) ? "done" : "failed");
				}
			}
		}
	}
	if (!test_enemies_done && match_start_tick && GetTickCount() - match_start_tick >= TEST_ENEMIES_DELAY_MS) {
		test_enemies_done = true;
		char text[16] = {};
		if (GetEnvironmentVariableA("BLTD_ENEMIES", text, sizeof(text)) > 0 && atoi(text) > 0) {
			SpawnTestEnemies(game, atoi(text));
		}
	}

	UpdateTrails(humans);
	AssignTargets(humans);
	int32_t memberCount;
	if (config.ForgetEnemySeconds > 0.0f && HumanSquadLeader(game, &memberCount)) {
		TArray<UObject*>& squads = Field<TArray<UObject*> >(Field<TArray<UObject*> >(game, off.Game_Teams).Data[BLACKLIGHT_TEAM], off.Team_Squads);
		Field<float>(squads.Data[0], off.Squad_EnemyNotVisibleRemoveTime) = config.ForgetEnemySeconds;
	}
	DWORD now = GetTickCount();

	struct BotView {
		UObject* Pawn;
		float Distance;
		bool Fighting;
		bool Idle;
	};
	std::vector<BotView> views(bots.size());
	std::vector<UObject*> groupFollow;
	for (size_t i = 0; i < bots.size(); i++) {
		BotView& view = views[i];
		view = BotView{};
		view.Pawn = Field<UObject*>(bots[i].Controller, off.Controller_Pawn);
		if (!IsAlive(view.Pawn) || !bots[i].TargetPawn) {
			view.Pawn = 0;
			continue;
		}
		view.Distance = sqrtf(DistanceSquared(view.Pawn, bots[i].TargetPawn));
		view.Fighting = IsFighting(bots[i].Controller);
		view.Idle = IsIdle(bots[i].Controller);
		if (bots[i].WasFighting && !view.Fighting) {
			StopFiring(view.Pawn);
		}
		bots[i].WasFighting = view.Fighting;
		LeaveStaleCover(bots[i], view.Pawn, now);
		bool inGrace = (int32_t)(bots[i].GraceUntilTick - now) > 0;
		if (view.Idle && !inGrace && view.Distance > config.FollowDistance) {
			groupFollow.push_back(bots[i].TargetPawn);
		}
	}

	for (size_t i = 0; i < bots.size(); i++) {
		Bot& bot = bots[i];
		const BotView& view = views[i];
		if (!view.Pawn) {
			bot.PathFailures = 0;
			continue;
		}
		const FVector& pawnLocation = Field<FVector>(view.Pawn, off.Actor_Location);
		bool moved = Distance(pawnLocation, bot.LastMoveLocation) >= STUCK_MOVE;
		if (moved) {
			bot.LastMoveLocation = pawnLocation;
			bot.LastMoveTick = now;
			bot.FollowStalls = 0;
			bot.FollowPathFailures = 0;
			bot.LeashCalls = 0;
			bot.ZeroMoves = 0;
		}
		for (size_t ban = bot.CoverBans.size(); ban-- > 0; ) {
			if ((int32_t)(bot.CoverBans[ban].UntilTick - now) <= 0) {
				bot.CoverBans.erase(bot.CoverBans.begin() + ban);
			}
		}

		if (bot.PathFailures >= PATH_FAIL_LIMIT) {
			HandlePathFailures(bot, view.Pawn, view.Distance, view.Fighting, now);
		}
		else if (now - bot.LastMoveTick >= PATH_FAIL_STILL_MS && (int32_t)(now - bot.NextStillCheckTick) >= 0
			&& !Field<UObject*>(view.Pawn, off.FoxPawn_CurrentLink)) {
			bot.NextStillCheckTick = now + STILL_CHECK_INTERVAL_MS;
			std::vector<FVector> spots;
			FindNavMeshSpots(bot.Controller, pawnLocation, ON_NAVMESH_DISTANCE, spots);
			if (spots.empty()) {
				UnstickBot(bot, view.Pawn, view.Distance, "stands still off the navmesh", now);
			}
			else if (bot.FollowStalls >= FOLLOW_STALL_TICKS) {
				char reason[64] = "does not move when told to follow";
				if (bot.FollowPathFailures) {
					sprintf_s(reason, sizeof(reason), "found no path while following %u times", bot.FollowPathFailures);
				}
				UnstickBot(bot, view.Pawn, view.Distance, reason, now);
			}
		}
		bot.PathFailures = 0;

		float teleportDistance = view.Fighting ? config.TeleportDistance * 2.0f : config.TeleportDistance;
		if (view.Distance > teleportDistance && now - bot.LastMoveTick >= config.StuckMs) {
			bool teleported = TeleportBehind(view.Pawn, bot.TargetPawn);
			BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x has not moved for %u ms at %.0f from its human, teleport %s."
				, bot.Controller, now - bot.LastMoveTick, view.Distance, teleported ? "done" : "found no navmesh spot");
			bot.LastMoveTick = now;
			bot.LastMoveLocation = Field<FVector>(view.Pawn, off.Actor_Location);
			if (teleported) {
				bot.LastCommandTick = 0;
				bot.FollowStalls = 0;
				bot.GraceUntilTick = now + config.TeleportGraceMs;
			}
			continue;
		}
		if ((int32_t)(bot.GraceUntilTick - now) > 0) {
			continue;
		}
		if (view.Fighting) {
			if (view.Distance <= config.CombatLeash) {
				bot.LeashCalls = 0;
			}
			else if (now - bot.LastCommandTick >= FOLLOW_COMMAND_INTERVAL_MS) {
				if (bot.LeashCalls >= LEASH_CALL_LIMIT) {
					char reason[64];
					sprintf_s(reason, sizeof(reason), "did not come back when called %u times", bot.LeashCalls);
					bot.LeashCalls = 0;
					UnstickBot(bot, view.Pawn, view.Distance, reason, now);
					continue;
				}
				SendCommand(bot, FOLLOW_COMMAND, bot.TargetPawn);
				bot.LeashCalls++;
			}
			continue;
		}
		if (Squad::Placed(bot.Controller)) {
			bot.FollowStalls = 0;
			bot.FollowPathFailures = 0;
			continue;
		}
		bool groupMember = std::find(groupFollow.begin(), groupFollow.end(), bot.TargetPawn) != groupFollow.end();
		if (view.Idle && (view.Distance > config.FollowDistance || (groupMember && view.Distance > FOLLOW_GROUP_DISTANCE))) {
			if (!moved && bot.LastCommandTick) {
				bot.FollowStalls++;
			}
			SendCommand(bot, FOLLOW_COMMAND, bot.TargetPawn);
		}
		else {
			bot.FollowStalls = 0;
			bot.FollowPathFailures = 0;
		}
	}
}

static void TeleportToCheckpoint(UObject* game, UObject* checkpoint)
{
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	UObject* nearest = 0;
	float nearestDistance = 0.0f;
	for (UObject* human : humans) {
		UObject* humanPawn = Field<UObject*>(human, off.Controller_Pawn);
		if (!IsAlive(humanPawn)) {
			continue;
		}
		float distance = Distance(Field<FVector>(humanPawn, off.Actor_Location), Field<FVector>(checkpoint, off.Actor_Location));
		if (!nearest || distance < nearestDistance) {
			nearest = humanPawn;
			nearestDistance = distance;
		}
	}
	if (!nearest) {
		return;
	}
	for (UObject* pawn : pending_teleports) {
		if (!IsAlive(pawn) || !IsBotPawn(pawn)) {
			continue;
		}
		if (TeleportBehind(pawn, nearest)) {
			StartGrace(pawn);
		}
		else {
			BOTS_LOG(LOG_LEVEL_WARN, "Bot pawn 0x%08x found no free trail point at the checkpoint.", pawn);
		}
	}
}

static Mode MatchMode(UObject* game)
{
	UObject* gameClass = UE3::ObjectClass(game);
	if (UE3::IsA(game, cls.FoxGameMP_BO)) {
		return MODE_BLACK_OPS;
	}
	if (gameClass == cls.FoxGameMP_TDM) {
		return MODE_TEAM_DEATHMATCH;
	}
	if (gameClass == cls.FoxGameMP_DM) {
		return MODE_DEATHMATCH;
	}
	if (gameClass == cls.FoxGameMP_CP) {
		return MODE_CONTROL_POINT;
	}
	if (gameClass == cls.FoxGameMP_LMS) {
		return MODE_LAST_MAN;
	}
	if (gameClass == cls.FoxGameMP_LTS) {
		return MODE_LAST_TEAM;
	}
	if (gameClass == cls.FoxGameMP_CTF) {
		return MODE_RETRIEVAL;
	}
	return MODE_NONE;
}

static void RegisterSpawnedBot(UObject* other)
{
	if (!spawning_ally || !other || IsBot(other)) {
		return;
	}
	Bot bot = {};
	bot.Controller = other;
	bot.LastMoveTick = GetTickCount();
	bot.Slot = spawning_slot;
	bot.Team = spawning_team;
	bot.GraceUntilTick = bot.LastMoveTick;
	bots.push_back(bot);
	spawned_controller = other;
}

static bool TryResolve()
{
	if (resolve_failed) {
		return false;
	}
	DWORD now = GetTickCount();
	if (resolve_tried && now - last_resolve_tick < RESOLVE_INTERVAL_MS) {
		return false;
	}
	resolve_tried = true;
	last_resolve_tick = now;
	int result = Resolve();
	if (result < 0) {
		resolve_failed = true;
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "The bots are disabled.");
		return false;
	}
	resolved = result > 0;
	if (resolved) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "The bot hooks are active.");
		navmesh_ready = NavMesh::Resolve() > 0;
		if (navmesh_ready) {
			BOTS_LOG(LOG_LEVEL_INFO, "%s", "Navmesh patches are ready.");
		}
		if (Menu::Resolve()) {
			BOTS_LOG(LOG_LEVEL_INFO, "%s", "The menus get a Bots row.");
		}
	}
	return resolved;
}

static bool OnScriptCall(UObject* self, FFrame* stack, void* result)
{
	if (!resolved) {
		TryResolve();
		return true;
	}
	UObject* node = stack->Node;
	if (config.Trace) {
		Trace::OnScriptCall(self, stack);
	}
	Recoil::OnScriptCall(self, stack);
	if (!Menu::OnScriptCall(self, stack)) {
		return false;
	}
	if (!Versus::OnScriptCall(self, stack)) {
		return false;
	}
	if (!Server::OnScriptCall(self, stack, result)) {
		return false;
	}
	if (!Events::OnScriptCall(self, stack)) {
		return false;
	}
	if (!AimAssist::OnScriptCall(self, stack, result)) {
		return false;
	}
	if (!TeamSwitch::OnScriptCall(self, stack, result)) {
		return false;
	}
	if (!Hud::OnScriptCall(self, stack)) {
		return false;
	}
	Lobby::OnScriptCall(self, stack);
	if (node == fn.Game_GameEnding) {
		Spawns::OnLevelUnload();
		AimAssist::OnLevelUnload();
		TeamSwitch::OnLevelUnload();
	}
	if (node == fn.Game_GameEnding && current_game) {
		current_game = 0;
		mode = MODE_NONE;
		bots.clear();
		trails.clear();
		Squad::Reset();
		Trace::Reset();
		Events::OnLevelUnload();
		NavMesh::OnLevelUnload();
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "The match's level is unloading, the bots are released.");
		return true;
	}
	if (node == fn.MP_StartMatch) {
		LoadConfig(config);
		Menu::ApplyChoice(config, Lobby::IsPublic(self));
		Trace::Reset();
		mode = MatchMode(self);
		NavMesh::OnMatchStart(self);
		return true;
	}
	if (node == fn.Checkpoint_Touch) {
		return !IsBotPawn(Field<UObject*>(stack->Locals, off.Touch_Other));
	}
	if (node == fn.AI_ProcessBehaviorEvent) {
		Bot* bot = FindBot(self);
		const UE3::FString& eventName = Field<UE3::FString>(stack->Locals, off.ProcessBehaviorEvent_EventName);
		if (bot && eventName.Data && wcscmp(eventName.Data, ZERO_MOVE_EVENT) == 0) {
			Field<FVector>(self, off.AI_LastDestination) = FVector{};
			bot->ZeroMoves++;
			if (bot->ZeroMoves >= ZERO_MOVE_LIMIT && GetTickCount() - bot->LastMoveTick >= PATH_FAIL_STILL_MS) {
				bot->ZeroMoves = 0;
				bot->PathFailures++;
				bot->PathFailGoal = Field<FVector>(stack->Locals, off.ProcessBehaviorEvent_EventLocation);
				return true;
			}
			return false;
		}
		else if (bot && eventName.Data && wcsncmp(eventName.Data, PATH_NOT_FOUND_EVENT, wcslen(PATH_NOT_FOUND_EVENT)) == 0) {
			const FVector& searched = Field<FVector>(stack->Locals, off.ProcessBehaviorEvent_EventLocation);
			if (IsVersus() && bot->HasGoal && Distance(searched, bot->Goal) > OWN_SEARCH_RADIUS) {
				return true;
			}
			bot->PathFailures++;
			bot->PathFailGoal = searched;
			if (IsFollowingCommand(self)) {
				bot->FollowStalls++;
				bot->FollowPathFailures++;
			}
		}
		else if (bot && IsVersus() && eventName.Data && wcscmp(eventName.Data, HEAR_NOISE_EVENT) == 0) {
			Versus::OnNoise(self, Field<UObject*>(stack->Locals, off.ProcessBehaviorEvent_EventInstigator));
		}
		return true;
	}
	if (node == fn.AI_AIErrorThresholdReached && IsBot(self)) {
		Field<float>(self, off.AI_AIErrorLevel) = 0.0f;
		BOTS_LOG(LOG_LEVEL_INFO, "Bot 0x%08x reached the AI error threshold, its suicide is skipped.", self);
		return false;
	}
	if (node == fn.BO_Killed) {
		UObject* killed = Field<UObject*>(stack->Locals, off.Killed_KilledPlayer);
		if (IsBot(killed)) {
			UObject* killer = Field<UObject*>(stack->Locals, off.Killed_Killer);
			BOTS_LOG(LOG_LEVEL_INFO, "0x%08x (%s) killed bot 0x%08x with %s."
				, killer, UE3::ObjectName(killer ? UE3::ObjectClass(killer) : 0), killed
				, UE3::ObjectName(Field<UObject*>(stack->Locals, off.Killed_DamageType)));
		}
		return true;
	}
	if (node == fn.Pawn_EncroachedBy) {
		return !IsBotPawn(self) && !IsBotPawn(Field<UObject*>(stack->Locals, off.EncroachedBy_Other));
	}
	if (node == fn.BO_CheckpointReached) {
		checkpoint_count_before = Field<TArray<UObject*> >(self, off.BO_PreviousCheckpoints).Num;
		pending_checkpoint = Field<UObject*>(stack->Locals, off.CheckpointReached_Checkpoint);
		pending_teleports.clear();
		TArray<UObject*>& pawns = Field<TArray<UObject*> >(stack->Locals, off.CheckpointReached_LivePawns);
		int32_t kept = 0;
		for (int32_t i = 0; i < pawns.Num; i++) {
			if (IsBotPawn(pawns.Data[i])) {
				pending_teleports.push_back(pawns.Data[i]);
			}
			else {
				pawns.Data[kept++] = pawns.Data[i];
			}
		}
		pawns.Num = kept;
		return true;
	}
	if (node == fn.FoxPawn_TakeDamage) {
		UObject* controller = Field<UObject*>(self, off.Pawn_Controller);
		UObject* instigator = Field<UObject*>(stack->Locals, off.TakeDamage_InstigatedBy);
		int32_t& damage = Field<int32_t>(stack->Locals, off.TakeDamage_Damage);
		if (IsBot(controller) && instigator == controller) {
			damage = 0;
		}
		else if (IsVersus() && IsBot(instigator)) {
			damage = Versus::ScaleDamage(damage);
		}
		if (IsVersus() && instigator != controller) {
			Versus::OnBotHit(controller, instigator);
		}
		return true;
	}
	if (node == fn.Game_ReduceDamage) {
		if (!damage_scale_game && IsBot(Field<UObject*>(stack->Locals, off.ReduceDamage_InstigatedBy)) && UE3::IsA(self, cls.FoxGameMP_BO)) {
			damage_scale_game = self;
			damage_scale_saved = Field<float>(self, off.BO_CurrentAIDamageMod);
			Field<float>(self, off.BO_CurrentAIDamageMod) = config.DamageScale[Difficulty(self)];
		}
		return true;
	}
	if (node == fn.MP_ChoosePlayerStart) {
		if (spawning_ally || spawning_enemy) {
			Field<uint8_t>(stack->Locals, off.ChoosePlayerStart_InTeam) = spawning_team;
		}
		return true;
	}
	if (node == fn.Team_AddToSquad) {
		if (mode == MODE_BLACK_OPS && (spawning_ally || IsBot(Field<UObject*>(stack->Locals, off.AddToSquad_Other)))) {
			Field<int32_t>(stack->Locals, off.AddToSquad_SquadIndex) = 0;
		}
		return true;
	}
	if (node == fn.AI_Possess) {
		if (spawning_ally) {
			RegisterSpawnedBot(self);
			spawned_pawn = Field<UObject*>(stack->Locals, off.Possess_inPawn);
			return false;
		}
		if (spawning_enemy) {
			spawned_controller = self;
			spawned_pawn = Field<UObject*>(stack->Locals, off.Possess_inPawn);
			return false;
		}
		return true;
	}
	if (node == fn.Controller_Destroyed) {
		ForgetBot(self);
		return true;
	}
	return true;
}

static void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!resolved) {
		return;
	}
	if (config.Trace) {
		Trace::OnScriptReturn(self, stack, result);
	}
	UObject* node = stack->Node;
	if (node == fn.BO_GetForcedTeam) {
		UObject* other = Field<UObject*>(stack->Locals, off.GetForcedTeam_Other);
		RegisterSpawnedBot(other);
		if (IsBot(other)) {
			*(int32_t*)result = BLACKLIGHT_TEAM;
		}
		else if (spawning_enemy) {
			*(int32_t*)result = ORDER_TEAM;
		}
		return;
	}
	if (node == fn.DM_GetForcedTeam || node == fn.TDM_GetForcedTeam) {
		UObject* other = Field<UObject*>(stack->Locals, node == fn.DM_GetForcedTeam ? off.DM_GetForcedTeam_Other : off.TDM_GetForcedTeam_Other);
		RegisterSpawnedBot(other);
		Bot* bot = FindBot(other);
		if (bot && IsTeamVersus()) {
			*(int32_t*)result = bot->Team;
		}
		return;
	}
	if (node == NavMesh::PathSetupFunction()) {
		UObject* pawn = Field<UObject*>(self, off.Controller_Pawn);
		if (pawn) {
			NavMesh::OnPathSetup(self, Field<FVector>(pawn, off.Actor_Location), Field<FVector>(stack->Locals, 0));
		}
		return;
	}
	Menu::OnScriptReturn(self, stack);
	TeamSwitch::OnScriptReturn(self, stack);
	AimAssist::OnScriptReturn(self, stack);
	Recoil::OnScriptReturn(self, stack);
	Hud::OnScriptReturn(self, stack);
	Spawns::OnScriptReturn(self, stack, result);
	Versus::OnScriptReturn(self, stack, result);
	Server::OnScriptReturn(self, stack, result);
	Lobby::OnScriptReturn(self, stack, result);
	if (node == fn.PC_PlayerTick) {
		static DWORD next_movie_volume = 0;
		if ((int32_t)(GetTickCount() - next_movie_volume) >= 0) {
			next_movie_volume = GetTickCount() + MOVIE_VOLUME_INTERVAL_MS;
			EngineFixes::RefreshMovieVolume(self);
		}
		if (UE3::ClearUserModContent()) {
			BOTS_LOG(LOG_LEVEL_INFO, "%s", "A loaded package marked the game as modded and stopped the profile saves. The mod cleared the mark.");
		}
		Events::OnPlayerTick(self);
		Versus::OnFrame();
		if (mode == MODE_BLACK_OPS && config.Enabled && current_game) {
			DriveHost(current_game);
			Squad::Tick(current_game);
		}
		return;
	}
	if (node == fn.BO_Timer) {
		Maintain(self);
		return;
	}
	if (node == fn.Game_ReduceDamage) {
		if (damage_scale_game == self) {
			Field<float>(self, off.BO_CurrentAIDamageMod) = damage_scale_saved;
			damage_scale_game = 0;
		}
		return;
	}
	if (node == fn.BO_StartMatch) {
		if (current_game != self) {
			current_game = self;
			bots.clear();
			trails.clear();
			Squad::Reset();
		}
		match_start_tick = GetTickCount() | 1;
		test_enemies_done = false;
		host_moved = false;
		host_path_tick = match_start_tick;
		ReadHostPath();
		LoadConfig(config);
		Menu::ApplyChoice(config, false);
		if (!config.Enabled) {
			return;
		}
		static const char* const DIFFICULTY_NAMES[DIFFICULTY_COUNT] = { "NORMAL", "HARD", "INSANE" };
		int difficulty = Difficulty(self);
		BOTS_LOG(LOG_LEVEL_INFO, "Black Ops match started on %s, filling empty slots: bot health %d, damage %.2f."
			, DIFFICULTY_NAMES[difficulty], config.Health[difficulty], config.DamageScale[difficulty]);
		FillEmptySlots(self);
		return;
	}
	if (node == fn.BO_CheckpointReached) {
		if (Field<TArray<UObject*> >(self, off.BO_PreviousCheckpoints).Num == checkpoint_count_before) {
			return;
		}
		if (pending_checkpoint) {
			TeleportToCheckpoint(self, pending_checkpoint);
		}
		pending_teleports.clear();
		if (!config.Enabled) {
			return;
		}
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Checkpoint reached, refilling empty slots.");
		FillEmptySlots(self);
		return;
	}
}

bool Install(uintptr_t title_base)
{
	Server::Install();
	if (!UE3::Init(title_base)) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "UObject::ProcessInternal does not match, the bots are not installed.");
		return false;
	}
	UE3::SetScriptHandlers(OnScriptCall, OnScriptReturn);
	if (!UE3::HookScriptCalls()) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "UObject::ProcessInternal is hooked already, the bots are not installed.");
		return false;
	}
	BOTS_LOG(LOG_LEVEL_INFO, "%s", "UObject::ProcessInternal hook installed.");
	if (!NavMesh::Init(title_base)) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The navmesh code does not match, no navmesh patches are loaded.");
	}
	if (!FoxAI::Init(title_base)) {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The FoxGame AI code does not match, the bots choose cover and follow as the Order AI does.");
		return true;
	}
	if (FoxAI::HookCoverRating(OnCoverRating)) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Cover rating hook installed.");
	}
	if (FoxAI::HookSelectTargets(OnSelectTargets)) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Target selection hook installed.");
	}
	else {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The target selection code does not match, a bot in cover keeps a hidden enemy only within HiddenTargetRange.");
	}
	if (FoxAI::HookCoverActions(OnCoverActions)) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Cover action hook installed.");
	}
	else {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The cover action code does not match, a versus bot in cover blind fires as often as it leans out.");
	}
	if (FoxAI::HookFollowInitialize(OnFollowInitialized)) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Follow target hook installed.");
	}
	else {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The Follow vtable entry does not match, the bots follow the squad leader.");
	}
	if (FoxAI::HookTeamList(Versus::OnTeamList)) {
		BOTS_LOG(LOG_LEVEL_INFO, "%s", "Scoreboard hook installed.");
	}
	else {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The scoreboard code does not match, the scoreboards leave the versus bots out.");
	}
	if (FoxAI::HookPathParams(BOT_MAX_HOVER_DISTANCE, BOT_MIN_WALKABLE_Z)) {
		BOTS_LOG(LOG_LEVEL_INFO, "Path params hook installed: an AI stays on its path up to %.0f over a poly, on slopes down to normal Z %.2f."
			, BOT_MAX_HOVER_DISTANCE, BOT_MIN_WALKABLE_Z);
	}
	else {
		BOTS_LOG(LOG_LEVEL_WARN, "%s", "The path params code does not match, the AI loses its path on stairs.");
	}
	return true;
}

}
