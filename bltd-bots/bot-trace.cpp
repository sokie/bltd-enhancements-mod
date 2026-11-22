#include "bot-trace.hpp"
#include "bots-internal.hpp"
#include <map>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

using UE3::UObject;
using UE3::FFrame;
using UE3::FVector;
using UE3::FRotator;
using UE3::FString;
using UE3::TArray;
using UE3::Field;

namespace Bots {
namespace Trace {

static const DWORD SAMPLE_INTERVAL_MS = 900;
static const int32_t SQUAD_MEMBER_INFO_SIZE = 0x08;
static const int32_t ENEMY_INFO_SIZE = 0x30;
static const float SEEN_RECENTLY = 0.5f;
static const size_t CHAIN_LENGTH = 300;
static const size_t STATE_LENGTH = 60;
static const float ROTATOR_TO_RADIANS = 3.14159265f / 32768.0f;
static const float RADIANS_TO_DEGREES = 57.2957795f;
static const DWORD KILL_REPEAT_MS = 100;
static const char CONTROLLER_PREFIX[] = "FoxAIControllerInterface_";
static const char* const COVER_TYPES[] = { "no cover", "standing", "mid" };
static const char* const COVER_ACTIONS[] = { "none", "blind-left", "blind-right", "lean-left", "lean-right", "pop-up", "blind-up" };
static const wchar_t TAKE_HIT_EVENT[] = L"Take Hit";
static const wchar_t TARGET_SEEN_EVENT[] = L"VisibleTargetAcquired";
static const wchar_t TARGET_LOST_EVENT[] = L"TargetLost";

struct BotTrace {
	uint32_t Shots;
	uint32_t ImpactsTarget;
	uint32_t ImpactsPawn;
	uint32_t ImpactsWorld;
	uint32_t BlindShots;
	uint32_t ShotErrors;
	float ShotErrorSum;
	float ShotErrorMax;
	uint32_t Aims;
	float AimErrorSum;
	float AimErrorMax;
	float KnownOffsetMax;
	float SpreadMax;
	int32_t DamageOut;
	int32_t DamageIn;
	uint32_t HitsTaken;
	uint32_t ShotAt;
	UObject* RatedLink;
	int32_t RatedSlot;
	float GameRating;
	float Rating;
	DWORD SeenTick;
	bool Reacted;
};

struct TargetView {
	UObject* Pawn;
	FVector Known;
	float SeenAgo;
	float AttackedAgo;
};

struct Line {
	char Text[1600];
	size_t Length;

	void Add(const char* format, ...)
	{
		if (Length >= sizeof(Text) - 1) {
			return;
		}
		va_list args;
		va_start(args, format);
		int written = _vsnprintf_s(Text + Length, sizeof(Text) - Length, _TRUNCATE, format, args);
		va_end(args);
		Length = written < 0 ? sizeof(Text) - 1 : Length + written;
	}
};

static struct {
	UObject* MP_Timer;
	UObject* MP_Killed;
	UObject* InstantFire;
	UObject* ProcessInstantHit;
	UObject* GetAdjustedAimFor;
	UObject* GetSquadInfo;
	UObject* FoxWeapon;
	UObject* MP_BroadcastDeathMessage;
	UObject* DeathMessage_ClientReceive;
	UObject* HUD_AddHUDNotification;
} ref;

static struct {
	int32_t Broadcast_Killer;
	int32_t Broadcast_Other;
	int32_t Receive_Switch;
	int32_t Receive_PRI1;
	int32_t Receive_PRI2;
	int32_t Notification_Text;
	int32_t PRI_PlayerName;
} doff;
static UObject* display_ai_deaths_property = 0;

static struct {
	int32_t AI_ActiveTargetIndex;
	int32_t AI_NearbyEnemies;
	int32_t Ability_CurrentSubAction;
	int32_t Ability_StateString;
	int32_t Actor_Velocity;
	int32_t Actor_Instigator;
	int32_t FoxPawn_CoverAction;
	int32_t Pawn_Weapon;
	int32_t Weapon_Spread;
	int32_t FoxWeapon_AmmoLeftCount;
	int32_t Squad_EnemyInfoList;
	int32_t SquadMember_SqCont;
	int32_t EnemyInfo_TargetPawn;
	int32_t EnemyInfo_KnownLocation;
	int32_t EnemyInfo_LastSeenTimes;
	int32_t EnemyInfo_LastEngagedTimes;
	int32_t WorldInfo_TimeSeconds;
	int32_t GetSquadInfo_ReturnValue;
	int32_t ProcessInstantHit_Impact;
	int32_t Impact_HitActor;
	int32_t Impact_RayDir;
	int32_t Impact_StartTrace;
	int32_t Aim_StartFireLoc;
	int32_t MP_Killed_Killer;
	int32_t MP_Killed_KilledPlayer;
	int32_t MP_Killed_KilledPawn;
	int32_t MP_Killed_DamageType;
	int32_t BO_Killed_KilledPawn;
	int32_t CPGame_ControlPoints;
	int32_t CP_CPOwner;
	int32_t CP_CurrentCapturingTeam;
	int32_t CP_PointHackedTeam;
	int32_t CP_CaptureRatio;
	int32_t CP_CurrentStateName;
	int32_t CP_CaptureInfo;
	int32_t PresentTeam_TeamIndex;
	int32_t PresentTeam_CaptureAmount;
	int32_t PresentTeam_Pawns;
	int32_t CTF_RedFlag;
	int32_t CTF_BlueFlag;
	int32_t Carry_CarriedByPawn;
	int32_t GameObject_UsableByTeam;
} toff;
static UObject* flag_at_base_property = 0;
static const int32_t PRESENT_TEAM_INFO_SIZE = 0x18;

static bool ready = false;
static std::map<UObject*, BotTrace> traces;
static DWORD last_sample_tick = 0;
static UObject* damaged_pawn = 0;
static UObject* damaged_controller = 0;
static UObject* damage_instigator = 0;
static int32_t damaged_health = 0;
static UObject* last_killed = 0;
static DWORD last_kill_tick = 0;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.MP_Timer, "FoxGame.FoxGameMP.Timer" },
		{ &ref.MP_Killed, "FoxGame.FoxGameMP.Killed" },
		{ &ref.InstantFire, "FoxGame.FoxWeapon.InstantFire" },
		{ &ref.ProcessInstantHit, "FoxGame.FoxWeapon.ProcessInstantHit" },
		{ &ref.GetAdjustedAimFor, "FoxGame.FoxAIControllerInterface.GetAdjustedAimFor" },
		{ &ref.GetSquadInfo, "FoxGame.FoxAIControllerInterface.GetSquadInfo" },
		{ &ref.FoxWeapon, "FoxGame.FoxWeapon" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &toff.AI_ActiveTargetIndex, "FoxGame.FoxAIControllerInterface.ActiveTargetIndex" },
		{ &toff.AI_NearbyEnemies, "FoxGame.FoxAIControllerInterface.NearbyEnemies" },
		{ &toff.Ability_CurrentSubAction, "FoxGame.FoxAIHierarchicalAbility.CurrentSubAction" },
		{ &toff.Ability_StateString, "FoxGame.FoxAIHierarchicalAbility.StateString" },
		{ &toff.Actor_Velocity, "Engine.Actor.Velocity" },
		{ &toff.Actor_Instigator, "Engine.Actor.Instigator" },
		{ &toff.FoxPawn_CoverAction, "FoxGame.FoxPawn.CoverAction" },
		{ &toff.Pawn_Weapon, "Engine.Pawn.Weapon" },
		{ &toff.Weapon_Spread, "Engine.Weapon.Spread" },
		{ &toff.FoxWeapon_AmmoLeftCount, "FoxGame.FoxWeapon.AmmoLeftCount" },
		{ &toff.Squad_EnemyInfoList, "FoxGame.FoxSquadInfo.EnemyInfoList" },
		{ &toff.SquadMember_SqCont, "FoxGame.FoxSquadInfo.SquadMemberInfo.SqCont" },
		{ &toff.EnemyInfo_TargetPawn, "FoxGame.FoxSquadInfo.EnemyInfo.TargetPawn" },
		{ &toff.EnemyInfo_KnownLocation, "FoxGame.FoxSquadInfo.EnemyInfo.KnownLocation" },
		{ &toff.EnemyInfo_LastSeenTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastSeenTimes" },
		{ &toff.EnemyInfo_LastEngagedTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastEngagedTimes" },
		{ &toff.WorldInfo_TimeSeconds, "Engine.WorldInfo.TimeSeconds" },
		{ &toff.GetSquadInfo_ReturnValue, "FoxGame.FoxAIControllerInterface.GetSquadInfo.ReturnValue" },
		{ &toff.ProcessInstantHit_Impact, "FoxGame.FoxWeapon.ProcessInstantHit.Impact" },
		{ &toff.Impact_HitActor, "Engine.Actor.ImpactInfo.HitActor" },
		{ &toff.Impact_RayDir, "Engine.Actor.ImpactInfo.RayDir" },
		{ &toff.Impact_StartTrace, "Engine.Actor.ImpactInfo.StartTrace" },
		{ &toff.Aim_StartFireLoc, "FoxGame.FoxAIControllerInterface.GetAdjustedAimFor.StartFireLoc" },
		{ &toff.MP_Killed_Killer, "FoxGame.FoxGameMP.Killed.Killer" },
		{ &toff.MP_Killed_KilledPlayer, "FoxGame.FoxGameMP.Killed.KilledPlayer" },
		{ &toff.MP_Killed_KilledPawn, "FoxGame.FoxGameMP.Killed.KilledPawn" },
		{ &toff.MP_Killed_DamageType, "FoxGame.FoxGameMP.Killed.DamageType" },
		{ &toff.BO_Killed_KilledPawn, "FoxGame.FoxGameMP_BO.Killed.KilledPawn" },
		{ &toff.CPGame_ControlPoints, "FoxGame.FoxGameMP_CP.ControlPoints" },
		{ &toff.CP_CPOwner, "FoxGame.FoxGameObject_ControlPoint.CPOwner" },
		{ &toff.CP_CurrentCapturingTeam, "FoxGame.FoxGameObject_ControlPoint.CurrentCapturingTeam" },
		{ &toff.CP_PointHackedTeam, "FoxGame.FoxGameObject_ControlPoint.PointHackedTeam" },
		{ &toff.CP_CaptureRatio, "FoxGame.FoxGameObject_ControlPoint.CaptureRatio" },
		{ &toff.CP_CurrentStateName, "FoxGame.FoxGameObject_ControlPoint.CurrentStateName" },
		{ &toff.CP_CaptureInfo, "FoxGame.FoxGameObject_ControlPoint.CaptureInfo" },
		{ &toff.PresentTeam_TeamIndex, "FoxGame.FoxGameObject_ControlPoint.PresentTeamInfo.TeamIndex" },
		{ &toff.PresentTeam_CaptureAmount, "FoxGame.FoxGameObject_ControlPoint.PresentTeamInfo.CaptureAmount" },
		{ &toff.PresentTeam_Pawns, "FoxGame.FoxGameObject_ControlPoint.PresentTeamInfo.Pawns" },
		{ &toff.CTF_RedFlag, "FoxGame.FoxGameMP_CTF.RedFlag" },
		{ &toff.CTF_BlueFlag, "FoxGame.FoxGameMP_CTF.BlueFlag" },
		{ &toff.Carry_CarriedByPawn, "FoxGame.FoxGameObject_Carry.CarriedByPawn" },
		{ &toff.GameObject_UsableByTeam, "FoxGame.FoxGameObject.UsableByTeam" },
	};
	flag_at_base_property = UE3::FindObject("FoxGame.FoxGameObject_Flag.bIsAtBase");
	if (!flag_at_base_property) {
		BOTS_LOG(LOG_LEVEL_ERROR, "%s", "FoxGame.FoxGameObject_Flag.bIsAtBase not found.");
		return false;
	}
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	struct { UObject** Target; const char* Path; } deathRefs[] = {
		{ &ref.MP_BroadcastDeathMessage, "FoxGame.FoxGameMP.BroadcastDeathMessage" },
		{ &ref.DeathMessage_ClientReceive, "FoxGame.FoxDeathMessage.ClientReceive" },
		{ &ref.HUD_AddHUDNotification, "FoxGame.FoxHUD.AddHUDNotification" },
	};
	struct { int32_t* Target; const char* Path; } deathOffsets[] = {
		{ &doff.Broadcast_Killer, "FoxGame.FoxGameMP.BroadcastDeathMessage.Killer" },
		{ &doff.Broadcast_Other, "FoxGame.FoxGameMP.BroadcastDeathMessage.Other" },
		{ &doff.Receive_Switch, "FoxGame.FoxDeathMessage.ClientReceive.Switch" },
		{ &doff.Notification_Text, "FoxGame.FoxHUD.AddHUDNotification.NewNotification" },
		{ &doff.PRI_PlayerName, "Engine.PlayerReplicationInfo.PlayerName" },
	};
	bool complete = true;
	for (auto& deathRef : deathRefs) {
		*deathRef.Target = UE3::FindObject(deathRef.Path);
		if (!*deathRef.Target) {
			BOTS_LOG(LOG_LEVEL_WARN, "%s not found, the trace skips the kill feed.", deathRef.Path);
			complete = false;
		}
	}
	for (auto& deathOffset : deathOffsets) {
		*deathOffset.Target = UE3::FindPropertyOffset(deathOffset.Path);
		if (*deathOffset.Target < 0) {
			BOTS_LOG(LOG_LEVEL_WARN, "%s not found, the trace skips the kill feed.", deathOffset.Path);
			complete = false;
		}
	}
	doff.Receive_PRI1 = doff.Receive_Switch + 4;
	doff.Receive_PRI2 = doff.Receive_Switch + 8;
	display_ai_deaths_property = UE3::FindObject("FoxGame.FoxGameMP.bDisplayAIDeathMessages");
	if (!complete) {
		ref.MP_BroadcastDeathMessage = ref.DeathMessage_ClientReceive = ref.HUD_AddHUDNotification = 0;
	}
	ready = true;
	return true;
}

static const char* PRIName(UObject* pri)
{
	static char text[64];
	if (!pri) {
		return "none";
	}
	const FString& name = Field<FString>(pri, doff.PRI_PlayerName);
	size_t i = 0;
	for (; name.Data && name.Data[i] && i < sizeof(text) - 1; i++) {
		text[i] = name.Data[i] < 0x80 ? (char)name.Data[i] : '?';
	}
	text[i] = 0;
	return text;
}

void Reset()
{
	traces.clear();
	damaged_pawn = 0;
	last_killed = 0;
}

static BotTrace* FindTrace(UObject* controller)
{
	if (!controller || !IsBot(controller)) {
		return 0;
	}
	return &traces[controller];
}

static UObject* PawnController(UObject* pawn)
{
	return pawn ? Field<UObject*>(pawn, off.Pawn_Controller) : 0;
}

static float WorldTime(UObject* actor)
{
	UObject* world = Field<UObject*>(actor, off.Actor_WorldInfo);
	return world ? Field<float>(world, toff.WorldInfo_TimeSeconds) : 0.0f;
}

static FVector Minus(const FVector& a, const FVector& b)
{
	FVector result = { a.X - b.X, a.Y - b.Y, a.Z - b.Z };
	return result;
}

static float AngleDegrees(const FVector& a, const FVector& b)
{
	float lengths = sqrtf((a.X * a.X + a.Y * a.Y + a.Z * a.Z) * (b.X * b.X + b.Y * b.Y + b.Z * b.Z));
	if (lengths <= 0.0f) {
		return 0.0f;
	}
	float cosine = (a.X * b.X + a.Y * b.Y + a.Z * b.Z) / lengths;
	cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
	return acosf(cosine) * RADIANS_TO_DEGREES;
}

static FVector RotatorDirection(const FRotator& rotation)
{
	float pitch = (float)rotation.Pitch * ROTATOR_TO_RADIANS;
	float yaw = (float)rotation.Yaw * ROTATOR_TO_RADIANS;
	FVector direction = { cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), sinf(pitch) };
	return direction;
}

static float Ago(const TArray<float>& times, int32_t member, float now)
{
	if (member < 0 || member >= times.Num || times.Data[member] <= 0.0f) {
		return -1.0f;
	}
	return now - times.Data[member];
}

static bool ActiveTarget(UObject* controller, float now, TargetView& view)
{
	view = TargetView{};
	int32_t index = Field<int32_t>(controller, toff.AI_ActiveTargetIndex);
	if (index < 0) {
		return false;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.GetSquadInfo) + 16, 0);
	UE3::ProcessNativeEvent(controller, ref.GetSquadInfo, parms.data());
	UObject* squad = Field<UObject*>(parms.data(), toff.GetSquadInfo_ReturnValue);
	if (!squad) {
		return false;
	}
	TArray<uint8_t>& enemies = Field<TArray<uint8_t> >(squad, toff.Squad_EnemyInfoList);
	if (index >= enemies.Num) {
		return false;
	}
	uint8_t* info = enemies.Data + index * ENEMY_INFO_SIZE;
	view.Pawn = Field<UObject*>(info, toff.EnemyInfo_TargetPawn);
	view.Known = Field<FVector>(info, toff.EnemyInfo_KnownLocation);
	int32_t member = -1;
	TArray<uint8_t>& members = Field<TArray<uint8_t> >(squad, off.Squad_SquadMembers);
	for (int32_t i = 0; i < members.Num && member < 0; i++) {
		if (Field<UObject*>(members.Data + i * SQUAD_MEMBER_INFO_SIZE, toff.SquadMember_SqCont) == controller) {
			member = i;
		}
	}
	view.SeenAgo = Ago(Field<TArray<float> >(info, toff.EnemyInfo_LastSeenTimes), member, now);
	view.AttackedAgo = Ago(Field<TArray<float> >(info, toff.EnemyInfo_LastEngagedTimes), member, now);
	return IsAlive(view.Pawn);
}

static void AddLabel(Line& line, UObject* actor)
{
	if (!actor) {
		line.Add("none");
		return;
	}
	if (!UE3::IsA(actor, cls.Pawn)) {
		line.Add("%s 0x%08x", UE3::ObjectName(UE3::ObjectClass(actor)), actor);
		return;
	}
	UObject* controller = PawnController(actor);
	if (controller && IsBot(controller)) {
		line.Add("bot 0x%08x", controller);
	}
	else if (controller && UE3::IsA(controller, cls.FoxPC)) {
		line.Add("human 0x%08x", actor);
	}
	else {
		line.Add("AI 0x%08x", actor);
	}
}

static void AddAgo(Line& line, float ago)
{
	if (ago < 0.0f) {
		line.Add("never");
	}
	else {
		line.Add("%.1f s ago", ago);
	}
}

static void AddChain(Line& line, UObject* controller)
{
	UObject* ability = Field<UObject*>(controller, off.AI_CurrentBehavior + BEHAVIOR_INFO_BEHAVIOR);
	if (!ability) {
		line.Add("nothing");
		return;
	}
	size_t start = line.Length;
	for (int depth = 0; ability && depth < 8 && line.Length - start < CHAIN_LENGTH; depth++) {
		line.Add("%s%s", depth ? ">" : "", UE3::ObjectName(ability));
		const FString& state = Field<FString>(ability, toff.Ability_StateString);
		if (state.Data && state.Num > 1 && wcschr(state.Data, L':')) {
			char text[STATE_LENGTH + 1];
			size_t i = 0;
			for (; i < STATE_LENGTH && state.Data[i]; i++) {
				text[i] = state.Data[i] < 0x80 ? (char)state.Data[i] : '?';
			}
			text[i] = 0;
			line.Add("{%s}", text);
		}
		ability = Field<UObject*>(ability, toff.Ability_CurrentSubAction);
	}
}

static void AddCover(Line& line, UObject* controller, UObject* pawn, const FVector& location, const BotTrace& trace)
{
	UObject* link = Field<UObject*>(controller, off.AI_CurrentCover);
	int32_t slot = Field<int32_t>(controller, off.AI_CurrentCover + 4);
	if (!link || !UE3::IsValidObject(link)) {
		line.Add(" | cover none");
	}
	else {
		line.Add(" | cover 0x%08x/%d", link, slot);
		TArray<uint8_t>& slots = Field<TArray<uint8_t> >(link, off.CoverLink_Slots);
		if (slot >= 0 && slot < slots.Num) {
			uint8_t* slotData = slots.Data + slot * COVER_SLOT_SIZE;
			uint8_t type = slotData[COVER_SLOT_TYPE];
			uint32_t flags = Field<uint32_t>(slotData, COVER_SLOT_FLAGS);
			line.Add(" %s%s%s%s", type < 3 ? COVER_TYPES[type] : "?", (flags & SLOT_LEAN_LEFT) ? " lean-L" : "", (flags & SLOT_LEAN_RIGHT) ? " lean-R" : "", (flags & SLOT_CAN_POPUP) ? " pop" : "");
			line.Add(" %.0f away", Distance(location, FoxAI::GetSlotLocation(link, slot)));
			float facing;
			if (CoverFacing(controller, link, slot, facing)) {
				line.Add(" faces target %.2f", facing);
			}
			if (trace.RatedLink == link && trace.RatedSlot == slot) {
				line.Add(" score %.2f (game %.2f)", trace.Rating, trace.GameRating);
			}
		}
	}
	uint8_t coverType = Field<uint8_t>(pawn, off.FoxPawn_CoverType);
	if (coverType) {
		uint8_t action = Field<uint8_t>(pawn, toff.FoxPawn_CoverAction);
		line.Add(", in %s cover, action %s", coverType < 3 ? COVER_TYPES[coverType] : "?", action < 7 ? COVER_ACTIONS[action] : "?");
	}
}

static void AddFire(Line& line, UObject* pawn, const BotTrace& trace)
{
	line.Add(" | shots %u", trace.Shots);
	if (trace.Shots || trace.ImpactsTarget || trace.ImpactsPawn || trace.ImpactsWorld) {
		line.Add(" (hit target %u, other pawn %u, world %u; blind %u)", trace.ImpactsTarget, trace.ImpactsPawn, trace.ImpactsWorld, trace.BlindShots);
	}
	if (trace.ShotErrors) {
		line.Add(" shot off target %.1f avg %.1f max deg", trace.ShotErrorSum / trace.ShotErrors, trace.ShotErrorMax);
	}
	if (trace.Aims) {
		line.Add(", aim off %.1f avg %.1f max deg, known place off %.0f", trace.AimErrorSum / trace.Aims, trace.AimErrorMax, trace.KnownOffsetMax);
	}
	UObject* weapon = Field<UObject*>(pawn, toff.Pawn_Weapon);
	if (weapon && UE3::IsA(weapon, ref.FoxWeapon)) {
		const TArray<float>& spread = Field<TArray<float> >(weapon, toff.Weapon_Spread);
		line.Add(", %s spread %.2f (max %.2f) ammo %d", UE3::ObjectName(UE3::ObjectClass(weapon)), spread.Num > 0 ? spread.Data[0] : 0.0f, trace.SpreadMax
			, Field<int32_t>(weapon, toff.FoxWeapon_AmmoLeftCount));
	}
}

static void SampleBot(UObject* controller, BotTrace& trace)
{
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!IsAlive(pawn)) {
		return;
	}
	Line line = {};
	const char* className = UE3::ObjectName(UE3::ObjectClass(controller));
	if (strncmp(className, CONTROLLER_PREFIX, sizeof(CONTROLLER_PREFIX) - 1) == 0) {
		className += sizeof(CONTROLLER_PREFIX) - 1;
	}
	const FVector& location = Field<FVector>(pawn, off.Actor_Location);
	const FVector& velocity = Field<FVector>(pawn, toff.Actor_Velocity);
	line.Add("Trace 0x%08x %s hp %d/%d at (%.0f %.0f %.0f) speed %.0f | do ", controller, className
		, Field<int32_t>(pawn, off.Pawn_Health), Field<int32_t>(pawn, off.Pawn_HealthMax), location.X, location.Y, location.Z
		, sqrtf(velocity.X * velocity.X + velocity.Y * velocity.Y));
	AddChain(line, controller);

	TargetView target;
	if (ActiveTarget(controller, WorldTime(pawn), target)) {
		const FVector& targetLocation = Field<FVector>(target.Pawn, off.Actor_Location);
		line.Add(" | target ");
		AddLabel(line, target.Pawn);
		line.Add(" at %.0f, seen ", Distance(location, targetLocation));
		AddAgo(line, target.SeenAgo);
		line.Add(", attacked ");
		AddAgo(line, target.AttackedAgo);
		line.Add(", known place off %.0f", Distance(target.Known, targetLocation));
	}
	else {
		line.Add(" | target none");
	}
	line.Add(", hidden range %.0f", Field<float>(controller, off.AI_AggressionRange));

	TArray<UObject*>& nearby = Field<TArray<UObject*> >(controller, toff.AI_NearbyEnemies);
	UObject* nearest = 0;
	float nearestDistance = 0.0f;
	for (int32_t i = 0; i < nearby.Num; i++) {
		if (!IsAlive(nearby.Data[i])) {
			continue;
		}
		float distance = Distance(location, Field<FVector>(nearby.Data[i], off.Actor_Location));
		if (!nearest || distance < nearestDistance) {
			nearest = nearby.Data[i];
			nearestDistance = distance;
		}
	}
	line.Add(" | sees %d", nearby.Num);
	if (nearest) {
		line.Add(", nearest ");
		AddLabel(line, nearest);
		line.Add(" at %.0f", nearestDistance);
	}

	AddCover(line, controller, pawn, location, trace);
	AddFire(line, pawn, trace);
	line.Add(" | damage out %d in %d (%u hits, shot at %u) | AI errors %.0f", trace.DamageOut, trace.DamageIn, trace.HitsTaken, trace.ShotAt
		, Field<float>(controller, off.AI_AIErrorLevel));
	BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
}

static void Sample(UObject* game)
{
	DWORD now = GetTickCount();
	if (now - last_sample_tick < SAMPLE_INTERVAL_MS) {
		return;
	}
	last_sample_tick = now;
	std::vector<UObject*> humans;
	FindHumans(game, humans);
	for (UObject* human : humans) {
		UObject* pawn = Field<UObject*>(human, off.Controller_Pawn);
		if (!IsAlive(pawn)) {
			continue;
		}
		const FVector& location = Field<FVector>(pawn, off.Actor_Location);
		const FVector& velocity = Field<FVector>(pawn, toff.Actor_Velocity);
		const FRotator& rotation = Field<FRotator>(pawn, off.Actor_Rotation);
		BOTS_LOG(LOG_LEVEL_DEBUG, "Trace human 0x%08x hp %d/%d at (%.0f %.0f %.0f) speed %.0f yaw %.0f", pawn
			, Field<int32_t>(pawn, off.Pawn_Health), Field<int32_t>(pawn, off.Pawn_HealthMax), location.X, location.Y, location.Z
			, sqrtf(velocity.X * velocity.X + velocity.Y * velocity.Y), (float)rotation.Yaw * ROTATOR_TO_RADIANS * RADIANS_TO_DEGREES);
	}
	if (mode == MODE_CONTROL_POINT) {
		TArray<UObject*>& points = Field<TArray<UObject*> >(game, toff.CPGame_ControlPoints);
		for (int32_t i = 0; i < points.Num; i++) {
			UObject* point = points.Data[i];
			if (!point || !UE3::IsValidObject(point)) {
				continue;
			}
			Line line = {};
			const FVector& at = Field<FVector>(point, off.Actor_Location);
			line.Add("Trace point 0x%08x at (%.0f %.0f %.0f) owner %d capturing %d hacked %d ratio %.2f state %s |", point, at.X, at.Y, at.Z, Field<int32_t>(point, toff.CP_CPOwner)
				, Field<int32_t>(point, toff.CP_CurrentCapturingTeam), Field<int32_t>(point, toff.CP_PointHackedTeam), Field<float>(point, toff.CP_CaptureRatio)
				, UE3::NameEntryText(Field<UE3::FName>(point, toff.CP_CurrentStateName).Index));
			TArray<uint8_t>& infos = Field<TArray<uint8_t> >(point, toff.CP_CaptureInfo);
			for (int32_t t = 0; t < infos.Num; t++) {
				uint8_t* info = infos.Data + t * PRESENT_TEAM_INFO_SIZE;
				line.Add(" team %d amount %.2f pawns %d", Field<int32_t>(info, toff.PresentTeam_TeamIndex), Field<float>(info, toff.PresentTeam_CaptureAmount)
					, Field<TArray<UObject*> >(info, toff.PresentTeam_Pawns).Num);
			}
			BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
		}
	}
	if (mode == MODE_RETRIEVAL) {
		for (int i = 0; i < 2; i++) {
			UObject* flag = Field<UObject*>(game, i == 0 ? toff.CTF_RedFlag : toff.CTF_BlueFlag);
			if (!flag || !UE3::IsValidObject(flag)) {
				BOTS_LOG(LOG_LEVEL_DEBUG, "Trace flag %s gone", i == 0 ? "red" : "blue");
				continue;
			}
			const FVector& at = Field<FVector>(flag, off.Actor_Location);
			Line line = {};
			line.Add("Trace flag %s 0x%08x usable by %d at (%.0f %.0f %.0f) at base %d carrier ", i == 0 ? "red" : "blue", flag, Field<int32_t>(flag, toff.GameObject_UsableByTeam)
				, at.X, at.Y, at.Z, UE3::GetBoolProperty(flag, flag_at_base_property) ? 1 : 0);
			AddLabel(line, Field<UObject*>(flag, toff.Carry_CarriedByPawn));
			BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
		}
	}
	for (const Bot& bot : bots) {
		BotTrace& trace = traces[bot.Controller];
		SampleBot(bot.Controller, trace);
		UObject* ratedLink = trace.RatedLink;
		int32_t ratedSlot = trace.RatedSlot;
		float gameRating = trace.GameRating;
		float rating = trace.Rating;
		DWORD seenTick = trace.SeenTick;
		bool reacted = trace.Reacted;
		trace = BotTrace{};
		trace.RatedLink = ratedLink;
		trace.RatedSlot = ratedSlot;
		trace.GameRating = gameRating;
		trace.Rating = rating;
		trace.SeenTick = seenTick;
		trace.Reacted = reacted;
	}
}

static void OnEvent(UObject* controller, FFrame* stack)
{
	BotTrace* trace = FindTrace(controller);
	if (!trace) {
		return;
	}
	const FString& eventName = Field<FString>(stack->Locals, off.ProcessBehaviorEvent_EventName);
	if (!eventName.Data) {
		return;
	}
	if (wcscmp(eventName.Data, TAKE_HIT_EVENT) == 0) {
		trace->ShotAt++;
		return;
	}
	if (wcscmp(eventName.Data, TARGET_SEEN_EVENT) == 0) {
		trace->SeenTick = GetTickCount() | 1;
		trace->Reacted = false;
	}
	else if (wcscmp(eventName.Data, TARGET_LOST_EVENT) == 0) {
		trace->SeenTick = 0;
	}
	Line line = {};
	line.Add("Trace 0x%08x event \"%ls\"", controller, eventName.Data);
	UObject* instigator = Field<UObject*>(stack->Locals, off.ProcessBehaviorEvent_EventInstigator);
	UObject* recipient = Field<UObject*>(stack->Locals, off.ProcessBehaviorEvent_EventRecipient);
	const FVector& location = Field<FVector>(stack->Locals, off.ProcessBehaviorEvent_EventLocation);
	if (instigator) {
		line.Add(" from ");
		AddLabel(line, instigator);
	}
	if (recipient) {
		line.Add(" about ");
		AddLabel(line, recipient);
	}
	if (location.X != 0.0f || location.Y != 0.0f || location.Z != 0.0f) {
		line.Add(" at (%.0f %.0f %.0f)", location.X, location.Y, location.Z);
	}
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (IsAlive(pawn)) {
		const FVector& own = Field<FVector>(pawn, off.Actor_Location);
		line.Add(", bot at (%.0f %.0f %.0f)", own.X, own.Y, own.Z);
	}
	BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
}

static void OnKilled(UObject* killer, UObject* killed, UObject* killed_pawn, UObject* damage_type)
{
	if (!IsBot(killer) && !IsBot(killed)) {
		return;
	}
	DWORD now = GetTickCount();
	if (killed == last_killed && now - last_kill_tick < KILL_REPEAT_MS) {
		return;
	}
	last_killed = killed;
	last_kill_tick = now;
	Line line = {};
	line.Add("Trace kill: ");
	UObject* killerPawn = killer ? Field<UObject*>(killer, off.Controller_Pawn) : 0;
	AddLabel(line, killerPawn ? killerPawn : killer);
	line.Add(" killed ");
	AddLabel(line, killed_pawn ? killed_pawn : killed);
	line.Add(" with %s", UE3::ObjectName(damage_type));
	if (killerPawn && killed_pawn) {
		line.Add(" at %.0f", Distance(Field<FVector>(killerPawn, off.Actor_Location), Field<FVector>(killed_pawn, off.Actor_Location)));
		uint8_t killerCover = Field<uint8_t>(killerPawn, off.FoxPawn_CoverType);
		uint8_t killedCover = Field<uint8_t>(killed_pawn, off.FoxPawn_CoverType);
		const FVector& velocity = Field<FVector>(killed_pawn, toff.Actor_Velocity);
		line.Add(", killer %s, victim %s at speed %.0f", killerCover < 3 ? COVER_TYPES[killerCover] : "?", killedCover < 3 ? COVER_TYPES[killedCover] : "?"
			, sqrtf(velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z));
	}
	BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
}

static void OnShot(UObject* weapon)
{
	UObject* pawn = Field<UObject*>(weapon, toff.Actor_Instigator);
	UObject* controller = PawnController(pawn);
	BotTrace* trace = FindTrace(controller);
	if (!trace) {
		return;
	}
	if (trace->SeenTick && !trace->Reacted) {
		trace->Reacted = true;
		TargetView target;
		float distance = ActiveTarget(controller, WorldTime(pawn), target) ? Distance(Field<FVector>(pawn, off.Actor_Location), Field<FVector>(target.Pawn, off.Actor_Location)) : 0.0f;
		BOTS_LOG(LOG_LEVEL_DEBUG, "Trace react: bot 0x%08x fired %u ms after seeing its target, %.0f away.", controller, GetTickCount() - trace->SeenTick, distance);
	}
	trace->Shots++;
	const TArray<float>& spread = Field<TArray<float> >(weapon, toff.Weapon_Spread);
	if (spread.Num > 0 && spread.Data[0] > trace->SpreadMax) {
		trace->SpreadMax = spread.Data[0];
	}
}

static void OnImpact(UObject* weapon, FFrame* stack)
{
	UObject* pawn = Field<UObject*>(weapon, toff.Actor_Instigator);
	UObject* controller = PawnController(pawn);
	BotTrace* trace = FindTrace(controller);
	if (!trace) {
		return;
	}
	uint8_t* impact = stack->Locals + toff.ProcessInstantHit_Impact;
	UObject* hit = Field<UObject*>(impact, toff.Impact_HitActor);
	TargetView target;
	bool hasTarget = ActiveTarget(controller, WorldTime(pawn), target);
	if (hasTarget && hit == target.Pawn) {
		trace->ImpactsTarget++;
	}
	else if (hit && UE3::IsA(hit, cls.Pawn)) {
		trace->ImpactsPawn++;
	}
	else {
		trace->ImpactsWorld++;
	}
	if (!hasTarget) {
		return;
	}
	if (target.SeenAgo < 0.0f || target.SeenAgo > SEEN_RECENTLY) {
		trace->BlindShots++;
	}
	float error = AngleDegrees(Field<FVector>(impact, toff.Impact_RayDir), Minus(Field<FVector>(target.Pawn, off.Actor_Location), Field<FVector>(impact, toff.Impact_StartTrace)));
	trace->ShotErrors++;
	trace->ShotErrorSum += error;
	if (error > trace->ShotErrorMax) {
		trace->ShotErrorMax = error;
	}
}

static void OnAim(UObject* controller, FFrame* stack, const FRotator* aim)
{
	BotTrace* trace = FindTrace(controller);
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	TargetView target;
	if (!trace || !aim || !IsAlive(pawn) || !ActiveTarget(controller, WorldTime(pawn), target)) {
		return;
	}
	const FVector& targetLocation = Field<FVector>(target.Pawn, off.Actor_Location);
	float error = AngleDegrees(RotatorDirection(*aim), Minus(targetLocation, Field<FVector>(stack->Locals, toff.Aim_StartFireLoc)));
	trace->Aims++;
	trace->AimErrorSum += error;
	if (error > trace->AimErrorMax) {
		trace->AimErrorMax = error;
	}
	float knownOffset = Distance(target.Known, targetLocation);
	if (knownOffset > trace->KnownOffsetMax) {
		trace->KnownOffsetMax = knownOffset;
	}
}

void OnScriptCall(UObject* self, FFrame* stack)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == fn.AI_ProcessBehaviorEvent) {
		OnEvent(self, stack);
	}
	else if (node == ref.InstantFire) {
		OnShot(self);
	}
	else if (node == ref.ProcessInstantHit) {
		OnImpact(self, stack);
	}
	else if (node == fn.FoxPawn_TakeDamage) {
		damaged_pawn = self;
		damaged_controller = PawnController(self);
		damage_instigator = Field<UObject*>(stack->Locals, off.TakeDamage_InstigatedBy);
		damaged_health = Field<int32_t>(self, off.Pawn_Health);
	}
	else if (node == ref.MP_Killed) {
		OnKilled(Field<UObject*>(stack->Locals, toff.MP_Killed_Killer), Field<UObject*>(stack->Locals, toff.MP_Killed_KilledPlayer)
			, Field<UObject*>(stack->Locals, toff.MP_Killed_KilledPawn), Field<UObject*>(stack->Locals, toff.MP_Killed_DamageType));
	}
	else if (node == fn.BO_Killed) {
		OnKilled(Field<UObject*>(stack->Locals, off.Killed_Killer), Field<UObject*>(stack->Locals, off.Killed_KilledPlayer)
			, Field<UObject*>(stack->Locals, toff.BO_Killed_KilledPawn), Field<UObject*>(stack->Locals, off.Killed_DamageType));
	}
	else if (node == ref.MP_BroadcastDeathMessage && node) {
		UObject* killer = Field<UObject*>(stack->Locals, doff.Broadcast_Killer);
		UObject* other = Field<UObject*>(stack->Locals, doff.Broadcast_Other);
		Line line = {};
		line.Add("Trace death message broadcast: killer ");
		AddLabel(line, killer ? Field<UObject*>(killer, off.Controller_Pawn) : 0);
		line.Add(" (%s), victim ", killer ? UE3::ObjectName(UE3::ObjectClass(killer)) : "none");
		AddLabel(line, other ? Field<UObject*>(other, off.Controller_Pawn) : 0);
		line.Add(" (%s), AI deaths shown %d", other ? UE3::ObjectName(UE3::ObjectClass(other)) : "none"
			, display_ai_deaths_property ? (UE3::GetBoolProperty(self, display_ai_deaths_property) ? 1 : 0) : -1);
		BOTS_LOG(LOG_LEVEL_DEBUG, "%s", line.Text);
	}
	else if (node == ref.DeathMessage_ClientReceive && node) {
		char killer[64];
		strcpy_s(killer, sizeof(killer), PRIName(Field<UObject*>(stack->Locals, doff.Receive_PRI1)));
		BOTS_LOG(LOG_LEVEL_DEBUG, "Trace death message received: switch %d, %s -> %s.", Field<int32_t>(stack->Locals, doff.Receive_Switch)
			, killer, PRIName(Field<UObject*>(stack->Locals, doff.Receive_PRI2)));
	}
	else if (node == ref.HUD_AddHUDNotification && node) {
		const FString& text = Field<FString>(stack->Locals, doff.Notification_Text);
		char narrow[200];
		size_t i = 0;
		for (; text.Data && text.Data[i] && i < sizeof(narrow) - 1; i++) {
			narrow[i] = text.Data[i] < 0x80 ? (char)text.Data[i] : '?';
		}
		narrow[i] = 0;
		BOTS_LOG(LOG_LEVEL_DEBUG, "Trace HUD notification: %s", narrow);
	}
}

void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == ref.GetAdjustedAimFor) {
		OnAim(self, stack, (const FRotator*)result);
	}
	else if (node == fn.FoxPawn_TakeDamage) {
		if (self != damaged_pawn) {
			return;
		}
		damaged_pawn = 0;
		int32_t health = Field<int32_t>(self, off.Pawn_Health);
		int32_t taken = damaged_health - (health > 0 ? health : 0);
		if (taken <= 0) {
			return;
		}
		BotTrace* victim = FindTrace(damaged_controller);
		if (victim) {
			victim->DamageIn += taken;
			victim->HitsTaken++;
		}
		BotTrace* shooter = FindTrace(damage_instigator);
		if (shooter) {
			shooter->DamageOut += taken;
		}
	}
	else if (node == fn.BO_Timer || node == ref.MP_Timer) {
		Sample(self);
	}
}

void OnCoverRating(UObject* controller, const FoxAI::CoverInfo* cover, float game_rating, float rating)
{
	if (!ready || Field<UObject*>(controller, off.AI_CurrentCover) != cover->Link || Field<int32_t>(controller, off.AI_CurrentCover + 4) != cover->SlotIdx) {
		return;
	}
	BotTrace* trace = FindTrace(controller);
	if (!trace) {
		return;
	}
	trace->RatedLink = cover->Link;
	trace->RatedSlot = cover->SlotIdx;
	trace->GameRating = game_rating;
	trace->Rating = rating;
}

}
}
