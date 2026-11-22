#include "bots-internal.hpp"
#include "spawns.hpp"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#define SPAWNS_LOG(level, format, ...) Log::Write(level, "[Spawns] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::FVector;
using UE3::FRotator;
using UE3::Field;
using UE3::TArray;

namespace Bots {
namespace Spawns {

static const float GROUP_RADIUS = 1800.0f;
static const size_t GROUPS_TRIED = 3;
static const float SPOT_REACH = 1500.0f;
static const size_t SPOTS_TRIED = 40;
static const float START_REACH = 2500.0f;
static const DWORD TRAIL_AGE_MS = 20000;
static const float TRAIL_SPACING = 100.0f;
static const size_t TRAIL_LIMIT = 256;
static const float SPAWN_CLEAR = 120.0f;
static const float ENEMY_CLEAR = 1800.0f;
static const float ENEMY_SIGHT = 4000.0f;
static const float EYE_HEIGHT = 40.0f;
static const float BASE_RADIUS = 1500.0f;
static const float ENEMY_BASE_DANGER = 8.0f;
static const float FORWARD_LIMIT = 1.0f;
static const int32_t NO_TEAM = 255;
static const uint8_t PHYS_WALKING = 1;
static const uint8_t PHYS_NAVMESHWALKING = 12;
static const float FFA_ENEMY_CLEAR = 1500.0f;
static const float FFA_ENEMY_SIGHT = 4000.0f;
static const DWORD FFA_REUSE_MS = 30000;
static const int32_t GET_TEAM_NUM_OFFSET = 0x154;
typedef uint8_t (__fastcall *tGetTeamNum)(UObject* actor, void* unused_edx);

struct Sample {
	FVector At;
	DWORD Tick;
};

struct StartInfo {
	UObject* Start;
	FVector At;
	int32_t Team;
	bool InBase[2];
	DWORD UsedTick;
};

struct Volume {
	UObject* Actor;
	int32_t Team;
};

struct Living {
	UObject* Pawn;
	FVector At;
	uint8_t Team;
	bool Turret;
};

struct Group {
	FVector Centre;
	int32_t Count;
};

static struct {
	UObject* Game_ShouldSpawnAtStartSpot;
	UObject* MP_RestartPlayer;
	UObject* MP_Timer;
	UObject* PlayerStart;
	UObject* PlayerStart_bEnabled;
	UObject* TeamSpawnVolume;
	UObject* Volume_EncompassesPoint;
	UObject* PC_ClientSetRotation;
	UObject* PlayerController;
	UObject* Game_bTeamGame;
} ref;

static struct {
	int32_t RestartPlayer_NewPlayer;
	int32_t ChoosePlayerStart_Player;
	int32_t ChoosePlayerStart_InTeam;
	int32_t PlayerStart_TeamIndex;
	int32_t WorldInfo_NavigationPointList;
	int32_t NavigationPoint_nextNavigationPoint;
	int32_t TeamSpawnVolume_OwningTeamIndex;
	int32_t Encompasses_Loc;
	int32_t Encompasses_ReturnValue;
	int32_t ClientSetRotation_NewRotation;
} soff;

static bool ready = false;
static std::vector<Sample> trail[2];
static std::vector<StartInfo> starts;
static std::vector<Volume> volumes;
static UObject* prepared_world = 0;
static FVector base_centre[2] = {};
static struct {
	UObject* Controller;
	FVector Spot;
	FRotator Rotation;
} pending = {};

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.Game_ShouldSpawnAtStartSpot, "Engine.GameInfo.ShouldSpawnAtStartSpot" },
		{ &ref.MP_RestartPlayer, "FoxGame.FoxGameMP.RestartPlayer" },
		{ &ref.MP_Timer, "FoxGame.FoxGameMP.Timer" },
		{ &ref.PlayerStart, "Engine.PlayerStart" },
		{ &ref.PlayerStart_bEnabled, "Engine.PlayerStart.bEnabled" },
		{ &ref.TeamSpawnVolume, "FoxGame.FoxTeamSpawnVolume" },
		{ &ref.Volume_EncompassesPoint, "Engine.Volume.EncompassesPoint" },
		{ &ref.PC_ClientSetRotation, "Engine.Controller.ClientSetRotation" },
		{ &ref.PlayerController, "Engine.PlayerController" },
		{ &ref.Game_bTeamGame, "Engine.GameInfo.bTeamGame" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			SPAWNS_LOG(LOG_LEVEL_WARN, "%s not found.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &soff.RestartPlayer_NewPlayer, "FoxGame.FoxGameMP.RestartPlayer.NewPlayer" },
		{ &soff.ChoosePlayerStart_Player, "FoxGame.FoxGameMP.ChoosePlayerStart.Player" },
		{ &soff.ChoosePlayerStart_InTeam, "FoxGame.FoxGameMP.ChoosePlayerStart.InTeam" },
		{ &soff.PlayerStart_TeamIndex, "Engine.PlayerStart.TeamIndex" },
		{ &soff.WorldInfo_NavigationPointList, "Engine.WorldInfo.NavigationPointList" },
		{ &soff.NavigationPoint_nextNavigationPoint, "Engine.NavigationPoint.nextNavigationPoint" },
		{ &soff.TeamSpawnVolume_OwningTeamIndex, "FoxGame.FoxTeamSpawnVolume.OwningTeamIndex" },
		{ &soff.Encompasses_Loc, "Engine.Volume.EncompassesPoint.Loc" },
		{ &soff.Encompasses_ReturnValue, "Engine.Volume.EncompassesPoint.ReturnValue" },
		{ &soff.ClientSetRotation_NewRotation, "Engine.Controller.ClientSetRotation.NewRotation" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			SPAWNS_LOG(LOG_LEVEL_WARN, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	ready = true;
	return true;
}

void OnLevelUnload()
{
	trail[0].clear();
	trail[1].clear();
	starts.clear();
	volumes.clear();
	prepared_world = 0;
	pending.Controller = 0;
}

static bool IsTeamMatch(UObject* game)
{
	return config.Versus.SmartSpawns && UE3::IsA(game, cls.FoxGameMP_TDM) && !UE3::IsA(game, cls.FoxGameMP_BO);
}

static bool IsFreeForAll(UObject* game)
{
	return config.Versus.SmartSpawns && UE3::IsA(game, cls.FoxGameMP_DM) && !UE3::GetBoolProperty(game, ref.Game_bTeamGame);
}

static bool InVolume(UObject* volume, const FVector& point)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Volume_EncompassesPoint) + 16, 0);
	Field<FVector>(parms.data(), soff.Encompasses_Loc) = point;
	UE3::ProcessNativeEvent(volume, ref.Volume_EncompassesPoint, parms.data());
	return (Field<uint32_t>(parms.data(), soff.Encompasses_ReturnValue) & 1) != 0;
}

static bool InBaseOf(const FVector& point, int32_t team)
{
	for (const StartInfo& start : starts) {
		if (start.Team == team && Distance(start.At, point) < BASE_RADIUS) {
			return true;
		}
	}
	for (const Volume& volume : volumes) {
		if (volume.Team == team && InVolume(volume.Actor, point)) {
			return true;
		}
	}
	return false;
}

static bool InOwnHalf(const FVector& point, int32_t team)
{
	return Distance(point, base_centre[team]) <= Distance(point, base_centre[1 - team]) * FORWARD_LIMIT;
}

static void Prepare(UObject* world)
{
	if (prepared_world == world) {
		return;
	}
	OnLevelUnload();
	prepared_world = world;
	for (int32_t i = 0; i < UE3::ObjectCount(); i++) {
		UObject* object = UE3::ObjectAt(i);
		if (object && UE3::IsA(object, ref.TeamSpawnVolume) && Field<UObject*>(object, off.Actor_WorldInfo) == world) {
			volumes.push_back({ object, Field<int32_t>(object, soff.TeamSpawnVolume_OwningTeamIndex) });
		}
	}
	int listed = 0;
	for (UObject* point = Field<UObject*>(world, soff.WorldInfo_NavigationPointList); point && listed < NavMesh::LIST_LIMIT; point = Field<UObject*>(point, soff.NavigationPoint_nextNavigationPoint), listed++) {
		if (UE3::IsA(point, ref.PlayerStart)) {
			starts.push_back({ point, Field<FVector>(point, off.Actor_Location), Field<int32_t>(point, soff.PlayerStart_TeamIndex), { false, false }, 0 });
		}
	}
	int32_t counts[2] = {};
	base_centre[0] = base_centre[1] = FVector{};
	for (const StartInfo& start : starts) {
		if (start.Team == 0 || start.Team == 1) {
			counts[start.Team]++;
			base_centre[start.Team].X += start.At.X;
			base_centre[start.Team].Y += start.At.Y;
			base_centre[start.Team].Z += start.At.Z;
		}
	}
	for (int team = 0; team < 2; team++) {
		float count = (float)(counts[team] > 0 ? counts[team] : 1);
		base_centre[team] = { base_centre[team].X / count, base_centre[team].Y / count, base_centre[team].Z / count };
	}
	int32_t own = 0;
	int32_t neutral = 0;
	int32_t neutral_in_base = 0;
	for (StartInfo& start : starts) {
		start.InBase[0] = InBaseOf(start.At, 0);
		start.InBase[1] = InBaseOf(start.At, 1);
		own += start.Team == NO_TEAM ? 0 : 1;
		neutral += start.Team == NO_TEAM ? 1 : 0;
		neutral_in_base += start.Team == NO_TEAM && (start.InBase[0] || start.InBase[1]) ? 1 : 0;
	}
	SPAWNS_LOG(LOG_LEVEL_INFO, "%d team starts, %d neutral starts (%d in a base), %d team volumes.", own, neutral, neutral_in_base, (int32_t)volumes.size());
}

static uint8_t TeamOf(UObject* actor)
{
	tGetTeamNum get_team = *(tGetTeamNum*)(*(uint8_t**)actor + GET_TEAM_NUM_OFFSET);
	return get_team(actor, 0);
}

static void ReadPawns(UObject* world, std::vector<Living>& pawns)
{
	for (UObject* pawn = Field<UObject*>(world, off.WorldInfo_PawnList); pawn; pawn = Field<UObject*>(pawn, off.Pawn_NextPawn)) {
		if (!IsAlive(pawn)) {
			continue;
		}
		uint8_t team = TeamOf(pawn);
		if (team > 1) {
			continue;
		}
		pawns.push_back({ pawn, Field<FVector>(pawn, off.Actor_Location), team, strstr(UE3::ObjectName(UE3::ObjectClass(pawn)), "Turret") != 0 });
	}
}

static float Danger(UObject* tracer, const FVector& point, const std::vector<Living>& pawns, uint8_t team, bool first_only)
{
	float danger = 0.0f;
	FVector eye = { point.X, point.Y, point.Z + EYE_HEIGHT };
	for (const Living& pawn : pawns) {
		if (pawn.Team == team) {
			continue;
		}
		float distance = Distance(pawn.At, point);
		if (distance < ENEMY_CLEAR) {
			danger += 2.0f - distance / ENEMY_CLEAR;
		}
		else if (distance < ENEMY_SIGHT && ClearLine(tracer, { pawn.At.X, pawn.At.Y, pawn.At.Z + EYE_HEIGHT }, eye)) {
			danger += 1.0f;
		}
		if (first_only && danger > 0.0f) {
			return danger;
		}
	}
	return danger;
}

static bool Enabled(const StartInfo& start)
{
	return UE3::GetBoolProperty(start.Start, ref.PlayerStart_bEnabled);
}

static bool IsFree(UObject* game, const FVector& point)
{
	return IsFreeSpot(game, point, 0, SPAWN_CLEAR);
}

static void FindGroups(const std::vector<Living>& pawns, uint8_t team, std::vector<Group>& groups)
{
	std::vector<FVector> left;
	for (const Living& pawn : pawns) {
		if (pawn.Team == team && !pawn.Turret && !InBaseOf(pawn.At, 1 - team)) {
			left.push_back(pawn.At);
		}
	}
	while (!left.empty() && groups.size() < GROUPS_TRIED) {
		int32_t best_count = 0;
		size_t best = 0;
		for (size_t i = 0; i < left.size(); i++) {
			int32_t count = 0;
			for (const FVector& other : left) {
				count += Distance(left[i], other) < GROUP_RADIUS ? 1 : 0;
			}
			if (count > best_count) {
				best_count = count;
				best = i;
			}
		}
		FVector anchor = left[best];
		FVector sum = {};
		std::vector<FVector> rest;
		for (const FVector& other : left) {
			if (Distance(anchor, other) < GROUP_RADIUS) {
				sum.X += other.X;
				sum.Y += other.Y;
				sum.Z += other.Z;
			}
			else {
				rest.push_back(other);
			}
		}
		groups.push_back({ { sum.X / best_count, sum.Y / best_count, sum.Z / best_count }, best_count });
		left.swap(rest);
	}
}

static FRotator Facing(const FVector& from, const FVector& to)
{
	return { 0, (int32_t)lroundf(atan2f(to.Y - from.Y, to.X - from.X) * 32768.0f / 3.14159265f), 0 };
}

static float NearestEnemy(const FVector& point, const std::vector<Living>& pawns, uint8_t team)
{
	float nearest = 1.0e9f;
	for (const Living& pawn : pawns) {
		if (pawn.Team != team) {
			nearest = (std::min)(nearest, Distance(pawn.At, point));
		}
	}
	return nearest;
}

static bool FindTrailSpot(UObject* game, const std::vector<Living>& pawns, uint8_t team, const Group& group, FVector& spot)
{
	std::vector<const Sample*> near_group;
	for (const Sample& sample : trail[team]) {
		if (Distance(sample.At, group.Centre) < SPOT_REACH && InOwnHalf(sample.At, team)) {
			near_group.push_back(&sample);
		}
	}
	std::sort(near_group.begin(), near_group.end(), [&group](const Sample* a, const Sample* b) { return Distance(a->At, group.Centre) < Distance(b->At, group.Centre); });
	bool found = false;
	float best = 0.0f;
	for (size_t i = 0; i < near_group.size() && i < SPOTS_TRIED; i++) {
		const FVector& at = near_group[i]->At;
		if (!IsFree(game, at) || InBaseOf(at, 1 - team) || Danger(game, at, pawns, team, true) > 0.0f) {
			continue;
		}
		float score = NearestEnemy(at, pawns, team) - Distance(at, group.Centre);
		if (!found || score > best) {
			found = true;
			best = score;
			spot = at;
		}
	}
	return found;
}

template <typename Filter>
static const StartInfo* NearestSafeStart(UObject* game, const std::vector<Living>& pawns, uint8_t team, const FVector& point, float reach, Filter filter)
{
	std::vector<const StartInfo*> found;
	for (const StartInfo& start : starts) {
		if (filter(start) && Distance(start.At, point) < reach) {
			found.push_back(&start);
		}
	}
	std::sort(found.begin(), found.end(), [&point](const StartInfo* a, const StartInfo* b) { return Distance(a->At, point) < Distance(b->At, point); });
	for (const StartInfo* start : found) {
		if (Enabled(*start) && IsFree(game, start->At) && Danger(game, start->At, pawns, team, true) == 0.0f) {
			return start;
		}
	}
	return 0;
}

static const StartInfo* LeastDangerousStart(UObject* game, const std::vector<Living>& pawns, uint8_t team)
{
	const StartInfo* best = 0;
	float best_danger = 0.0f;
	float best_distance = 0.0f;
	for (const StartInfo& start : starts) {
		if (!Enabled(start) || !IsFree(game, start.At)) {
			continue;
		}
		float danger = Danger(game, start.At, pawns, team, false);
		if (start.Team == 1 - team || start.InBase[1 - team]) {
			danger += ENEMY_BASE_DANGER;
		}
		float distance = NearestEnemy(start.At, pawns, team);
		if (!best || danger < best_danger || (danger == best_danger && distance > best_distance)) {
			best = &start;
			best_danger = danger;
			best_distance = distance;
		}
	}
	return best;
}

static bool IsSafeAlone(UObject* tracer, const FVector& point, const std::vector<Living>& pawns)
{
	FVector eye = { point.X, point.Y, point.Z + EYE_HEIGHT };
	for (const Living& pawn : pawns) {
		float distance = Distance(pawn.At, point);
		if (distance < FFA_ENEMY_CLEAR || (distance < FFA_ENEMY_SIGHT && ClearLine(tracer, { pawn.At.X, pawn.At.Y, pawn.At.Z + EYE_HEIGHT }, eye))) {
			return false;
		}
	}
	return true;
}

static StartInfo* PickFreeForAll(UObject* game, UObject* world, const char*& how)
{
	std::vector<Living> pawns;
	ReadPawns(world, pawns);
	const DWORD now = GetTickCount();
	std::vector<StartInfo*> fresh;
	std::vector<StartInfo*> used;
	StartInfo* farthest = 0;
	float farthest_distance = -1.0f;
	for (StartInfo& start : starts) {
		if (!Enabled(start) || !IsFree(game, start.At)) {
			continue;
		}
		float distance = NearestEnemy(start.At, pawns, NO_TEAM);
		if (distance > farthest_distance) {
			farthest = &start;
			farthest_distance = distance;
		}
		if (IsSafeAlone(game, start.At, pawns)) {
			(start.UsedTick && now - start.UsedTick < FFA_REUSE_MS ? used : fresh).push_back(&start);
		}
	}
	std::vector<StartInfo*>& safe = fresh.empty() ? used : fresh;
	if (safe.empty()) {
		how = "farthest";
		return farthest;
	}
	std::sort(safe.begin(), safe.end(), [&pawns](const StartInfo* a, const StartInfo* b) { return NearestEnemy(a->At, pawns, NO_TEAM) > NearestEnemy(b->At, pawns, NO_TEAM); });
	how = fresh.empty() ? "safe start used a moment ago" : "safe start";
	return safe[rand() % ((safe.size() + 1) / 2)];
}

static const StartInfo* Pick(UObject* game, UObject* world, UObject* player, uint8_t team, const char*& how)
{
	std::vector<Living> pawns;
	ReadPawns(world, pawns);
	std::vector<Group> groups;
	FindGroups(pawns, team, groups);
	auto own_side = [team](const StartInfo& start) { return start.Team != 1 - team && !start.InBase[1 - team] && InOwnHalf(start.At, team); };
	for (const Group& group : groups) {
		const StartInfo* start = NearestSafeStart(game, pawns, team, group.Centre, START_REACH, own_side);
		FVector spot;
		if (FindTrailSpot(game, pawns, team, group, spot)) {
			pending.Controller = player;
			pending.Spot = spot;
			pending.Rotation = Facing(spot, group.Centre);
			how = "trail";
			return start ? start : LeastDangerousStart(game, pawns, team);
		}
		if (start) {
			how = "group start";
			return start;
		}
	}
	if (!groups.empty()) {
		const StartInfo* toward = NearestSafeStart(game, pawns, team, groups[0].Centre, 1.0e9f, own_side);
		if (toward) {
			how = "start toward the group";
			return toward;
		}
	}
	const StartInfo* own = 0;
	float own_distance = 0.0f;
	for (const StartInfo& start : starts) {
		if (start.Team != team || !Enabled(start) || !IsFree(game, start.At) || Danger(game, start.At, pawns, team, true) > 0.0f) {
			continue;
		}
		float distance = NearestEnemy(start.At, pawns, team);
		if (!own || distance > own_distance) {
			own = &start;
			own_distance = distance;
		}
	}
	if (own) {
		how = "own base";
		return own;
	}
	const StartInfo* safest = LeastDangerousStart(game, pawns, team);
	how = safest && (safest->Team == 1 - team || safest->InBase[1 - team]) ? "enemy base, the least dangerous" : "least dangerous";
	return safest;
}

static void OnChoosePlayerStart(UObject* game, FFrame* stack, void* result)
{
	pending.Controller = 0;
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	if (!result || !world) {
		return;
	}
	if (IsFreeForAll(game)) {
		Prepare(world);
		const char* how = "";
		StartInfo* start = PickFreeForAll(game, world, how);
		if (start) {
			*(UObject**)result = start->Start;
			start->UsedTick = GetTickCount();
			if (config.Trace) {
				SPAWNS_LOG(LOG_LEVEL_INFO, "Team 0 spawns by %s at (%.0f %.0f %.0f).", how, start->At.X, start->At.Y, start->At.Z);
			}
		}
		return;
	}
	if (!IsTeamMatch(game)) {
		return;
	}
	UObject* player = Field<UObject*>(stack->Locals, soff.ChoosePlayerStart_Player);
	uint8_t team = player ? TeamOf(player) : (uint8_t)NO_TEAM;
	if (team > 1) {
		team = Field<uint8_t>(stack->Locals, soff.ChoosePlayerStart_InTeam);
	}
	if (team > 1) {
		return;
	}
	Prepare(world);
	const char* how = "";
	const StartInfo* start = Pick(game, world, player, team, how);
	if (!start) {
		return;
	}
	*(UObject**)result = start->Start;
	if (config.Trace) {
		const FVector& at = pending.Controller ? pending.Spot : start->At;
		SPAWNS_LOG(LOG_LEVEL_INFO, "Team %d spawns by %s at (%.0f %.0f %.0f).", (int32_t)team, how, at.X, at.Y, at.Z);
	}
}

static void OnRestartPlayer(FFrame* stack)
{
	UObject* controller = Field<UObject*>(stack->Locals, soff.RestartPlayer_NewPlayer);
	FVector spot;
	FRotator rotation;
	if (!controller || !UE3::IsA(controller, ref.PlayerController) || !TakeSpot(controller, spot, rotation)) {
		return;
	}
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!pawn || !MovePawnToSpot(pawn, spot)) {
		return;
	}
	Field<FRotator>(pawn, off.Actor_Rotation) = rotation;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.PC_ClientSetRotation) + 16, 0);
	Field<FRotator>(parms.data(), soff.ClientSetRotation_NewRotation) = rotation;
	UE3::ProcessEvent(controller, ref.PC_ClientSetRotation, parms.data());
}

static void RecordTrail(UObject* game)
{
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	if (!world || !IsTeamMatch(game)) {
		return;
	}
	Prepare(world);
	DWORD now = GetTickCount();
	for (std::vector<Sample>& samples : trail) {
		samples.erase(std::remove_if(samples.begin(), samples.end(), [now](const Sample& sample) { return now - sample.Tick > TRAIL_AGE_MS; }), samples.end());
	}
	std::vector<Living> pawns;
	ReadPawns(world, pawns);
	for (const Living& pawn : pawns) {
		uint8_t physics = Field<uint8_t>(pawn.Pawn, off.Actor_Physics);
		if (pawn.Turret || (physics != PHYS_WALKING && physics != PHYS_NAVMESHWALKING) || InBaseOf(pawn.At, 1 - pawn.Team)) {
			continue;
		}
		std::vector<Sample>& samples = trail[pawn.Team];
		bool seen = false;
		for (Sample& sample : samples) {
			if (Distance(sample.At, pawn.At) < TRAIL_SPACING) {
				sample.Tick = now;
				seen = true;
				break;
			}
		}
		if (!seen && samples.size() < TRAIL_LIMIT) {
			samples.push_back({ pawn.At, now });
		}
	}
}

bool TakeSpot(UObject* controller, FVector& spot, FRotator& rotation)
{
	if (!controller || pending.Controller != controller) {
		return false;
	}
	spot = pending.Spot;
	rotation = pending.Rotation;
	pending.Controller = 0;
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack, void* result)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == ref.Game_ShouldSpawnAtStartSpot) {
		if (result && (IsTeamMatch(self) || IsFreeForAll(self))) {
			*(uint32_t*)result = 0;
		}
	}
	else if (node == fn.MP_ChoosePlayerStart) {
		OnChoosePlayerStart(self, stack, result);
	}
	else if (node == ref.MP_RestartPlayer) {
		OnRestartPlayer(stack);
	}
	else if (node == ref.MP_Timer) {
		RecordTrail(self);
	}
}

}
}
