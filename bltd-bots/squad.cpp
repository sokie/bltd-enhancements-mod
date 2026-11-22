#include "bots-internal.hpp"
#include "fox-ai.hpp"
#include "squad.hpp"
#include <algorithm>
#include <map>
#include <math.h>
#include <string.h>

using UE3::UObject;
using UE3::FVector;
using UE3::TArray;
using UE3::Field;

namespace Bots {
namespace Squad {

static const DWORD TICK_INTERVAL_MS = 250;
static const DWORD FORMATION_MIN_MS = 700;
static const DWORD ENGAGE_MIN_MS = 3000;
static const float POST_REACHED = 150.0f;
static const float POST_MOVE_TRIGGER = 300.0f;
static const float POST_SNAP = 250.0f;
static const float SPRINT_DISTANCE = 1000.0f;
static const float HUMAN_MOVING_SPEED = 100.0f;
static const float LEAD_SECONDS = 0.5f;
static const float LANE_TANGENT = 0.21f;
static const float LANE_LENGTH = 1500.0f;
static const float LANE_START = 60.0f;
static const DWORD TARGET_UNSEEN_MS = 1500;
static const DWORD MELEE_STALL_MS = 3000;
static const float MELEE_REACH = 250.0f;
static const float ENEMY_MIN_DISTANCE = 350.0f;
static const float SNIPER_MIN_DISTANCE = 900.0f;
static const float SNIPER_FAR_DISTANCE = 1200.0f;
static const float SHOTGUN_NEAR_DISTANCE = 600.0f;
static const float SHOTGUN_FAR_DISTANCE = 1200.0f;
static const float RIFLE_NEAR_DISTANCE = 500.0f;
static const float RIFLE_FAR_DISTANCE = 1500.0f;
static const float POST_SPACING = 200.0f;
static const float EYE_UP = 60.0f;
static const float CHEST_UP = 30.0f;
static const size_t SPOT_CANDIDATES = 150;
static const float ENEMY_MOVED = 400.0f;
static const float LINK_SEARCH_MARGIN = 300.0f;
static const float COVER_FACING_MIN = 0.3f;
static const int32_t ENEMY_INFO_SIZE = 0x30;
static const wchar_t GOTO_COMMAND[] = L"Command GoTo";

enum PostKind {
	POST_NONE,
	POST_FORMATION,
	POST_COVER,
	POST_SIGHT,
};

struct State {
	PostKind Kind;
	FVector Post;
	FVector Enemy;
	DWORD CommandTick;
	DWORD UnseenSinceTick;
};

struct Candidate {
	FVector Spot;
	float Score;
	PostKind Kind;
};

static struct {
	int32_t Squad_EnemyInfoList;
	int32_t EnemyInfo_TargetPawn;
	int32_t EnemyInfo_KnownLocation;
	int32_t EnemyInfo_LastSeenTimes;
	int32_t EnemyInfo_LastEngagedTimes;
	int32_t WorldInfo_TimeSeconds;
} soff;

static bool ready = false;
static DWORD last_tick = 0;
static std::map<UObject*, State> states;

bool Resolve()
{
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &soff.Squad_EnemyInfoList, "FoxGame.FoxSquadInfo.EnemyInfoList" },
		{ &soff.EnemyInfo_TargetPawn, "FoxGame.FoxSquadInfo.EnemyInfo.TargetPawn" },
		{ &soff.EnemyInfo_KnownLocation, "FoxGame.FoxSquadInfo.EnemyInfo.KnownLocation" },
		{ &soff.EnemyInfo_LastSeenTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastSeenTimes" },
		{ &soff.EnemyInfo_LastEngagedTimes, "FoxGame.FoxSquadInfo.EnemyInfo.LastEngagedTimes" },
		{ &soff.WorldInfo_TimeSeconds, "Engine.WorldInfo.TimeSeconds" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			BOTS_LOG(LOG_LEVEL_ERROR, "%s not found.", offsetRef.Path);
			return false;
		}
	}
	if (soff.EnemyInfo_LastEngagedTimes + (int32_t)sizeof(TArray<float>) != ENEMY_INFO_SIZE) {
		BOTS_LOG(LOG_LEVEL_ERROR, "EnemyInfo.LastEngagedTimes is at 0x%x, not 0x%x.", soff.EnemyInfo_LastEngagedTimes, ENEMY_INFO_SIZE - (int32_t)sizeof(TArray<float>));
		return false;
	}
	ready = true;
	return true;
}

void Reset()
{
	states.clear();
}

bool Placed(UObject* controller)
{
	std::map<UObject*, State>::const_iterator found = states.find(controller);
	return found != states.end() && found->second.Kind != POST_NONE;
}

static float Length2D(float x, float y)
{
	return sqrtf(x * x + y * y);
}

static float Latest(const TArray<float>& times)
{
	float latest = -1.0f;
	for (int32_t i = 0; i < times.Num; i++) {
		latest = (std::max)(latest, times.Data[i]);
	}
	return latest;
}

static bool NearestKnownEnemy(UObject* squad, const FVector& point, float world_time, float forget_seconds, FVector& known)
{
	if (!squad) {
		return false;
	}
	TArray<uint8_t>& enemies = Field<TArray<uint8_t> >(squad, soff.Squad_EnemyInfoList);
	bool found = false;
	float best = 0.0f;
	for (int32_t i = 0; i < enemies.Num; i++) {
		uint8_t* info = enemies.Data + i * ENEMY_INFO_SIZE;
		const FVector& place = Field<FVector>(info, soff.EnemyInfo_KnownLocation);
		if (!IsAlive(Field<UObject*>(info, soff.EnemyInfo_TargetPawn)) || (place.X == 0.0f && place.Y == 0.0f && place.Z == 0.0f)) {
			continue;
		}
		float latest = (std::max)(Latest(Field<TArray<float> >(info, soff.EnemyInfo_LastSeenTimes)), Latest(Field<TArray<float> >(info, soff.EnemyInfo_LastEngagedTimes)));
		if (latest <= 0.0f || world_time - latest > forget_seconds) {
			continue;
		}
		float distance = Distance(point, place);
		if (!found || distance < best) {
			best = distance;
			known = place;
			found = true;
		}
	}
	return found;
}

static bool TargetKnownPlace(UObject* squad, UObject* target, FVector& known)
{
	if (!squad || !target) {
		return false;
	}
	TArray<uint8_t>& enemies = Field<TArray<uint8_t> >(squad, soff.Squad_EnemyInfoList);
	for (int32_t i = 0; i < enemies.Num; i++) {
		uint8_t* info = enemies.Data + i * ENEMY_INFO_SIZE;
		if (Field<UObject*>(info, soff.EnemyInfo_TargetPawn) == target) {
			known = Field<FVector>(info, soff.EnemyInfo_KnownLocation);
			return true;
		}
	}
	return false;
}

struct HumanFrame {
	UObject* Pawn;
	FVector Location;
	FVector Lead;
	FVector Forward;
	FVector Facing;
	bool Moving;
};

static bool ReadHuman(UObject* pawn, HumanFrame& frame)
{
	frame.Pawn = pawn;
	frame.Location = Field<FVector>(pawn, off.Actor_Location);
	const FVector& velocity = Field<FVector>(pawn, off.Actor_Velocity);
	float yaw = (float)Field<UE3::FRotator>(pawn, off.Actor_Rotation).Yaw * (3.14159265f / 32768.0f);
	frame.Facing = FVector{ cosf(yaw), sinf(yaw), 0.0f };
	float speed = Length2D(velocity.X, velocity.Y);
	frame.Moving = speed > HUMAN_MOVING_SPEED;
	frame.Forward = frame.Moving ? FVector{ velocity.X / speed, velocity.Y / speed, 0.0f } : frame.Facing;
	frame.Lead = frame.Moving ? FVector{ frame.Location.X + velocity.X * LEAD_SECONDS, frame.Location.Y + velocity.Y * LEAD_SECONDS, frame.Location.Z } : frame.Location;
	return true;
}

static bool InFireLane(const HumanFrame& human, const FVector& point)
{
	float dx = point.X - human.Location.X;
	float dy = point.Y - human.Location.Y;
	float along = dx * human.Facing.X + dy * human.Facing.Y;
	if (along < LANE_START || along > LANE_LENGTH) {
		return false;
	}
	float across = fabsf(dx * human.Facing.Y - dy * human.Facing.X);
	return across < along * LANE_TANGENT;
}

static bool NearOtherPost(UObject* controller, const FVector& point)
{
	for (std::map<UObject*, State>::const_iterator it = states.begin(); it != states.end(); ++it) {
		if (it->first != controller && it->second.Kind != POST_NONE && Distance(it->second.Post, point) < POST_SPACING) {
			return true;
		}
	}
	return false;
}

static FVector FormationPlace(const HumanFrame& human, int index)
{
	FVector right = { -human.Forward.Y, human.Forward.X, 0.0f };
	float back = config.FollowDistance * (index == 2 ? 1.6f : 1.0f);
	float side = index == 0 ? -config.SpreadDistance : (index == 1 ? config.SpreadDistance : 0.0f);
	return FVector{ human.Lead.X - human.Forward.X * back + right.X * side, human.Lead.Y - human.Forward.Y * back + right.Y * side, human.Lead.Z };
}

static float RoleScore(int role, float enemy_distance)
{
	switch (role) {
	case ROLE_SNIPER:
		return enemy_distance >= SNIPER_FAR_DISTANCE ? 0.4f : 0.0f;
	case ROLE_SHOTGUN:
		return enemy_distance <= SHOTGUN_NEAR_DISTANCE ? 0.4f : (enemy_distance > SHOTGUN_FAR_DISTANCE ? -0.3f : 0.0f);
	default:
		return enemy_distance >= RIFLE_NEAR_DISTANCE && enemy_distance <= RIFLE_FAR_DISTANCE ? 0.2f : 0.0f;
	}
}

static bool PostAllowed(UObject* controller, const HumanFrame& human, const FVector& enemy, int role, const FVector& point)
{
	float dx = enemy.X - human.Location.X;
	float dy = enemy.Y - human.Location.Y;
	float toEnemy = Length2D(dx, dy);
	if (toEnemy < 1.0f) {
		return false;
	}
	float ahead = ((point.X - human.Location.X) * dx + (point.Y - human.Location.Y) * dy) / toEnemy;
	float aheadMax = config.AheadDistance + (role == ROLE_SHOTGUN ? 100.0f : 0.0f);
	float enemyMin = role == ROLE_SNIPER ? SNIPER_MIN_DISTANCE : ENEMY_MIN_DISTANCE;
	return ahead <= aheadMax && Distance(point, human.Location) <= config.PostRadius && Distance(point, human.Location) >= POST_SPACING
		&& Distance(point, enemy) >= enemyMin && !InFireLane(human, point) && !NearOtherPost(controller, point);
}

static bool FindEngagementPost(UObject* game, const Bot& bot, UObject* pawn, const HumanFrame& human, const FVector& enemy, Candidate& best)
{
	const FVector location = Field<FVector>(pawn, off.Actor_Location);
	int role = WeaponRole(bot.Slot);
	bool found = false;
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	for (UObject* link = world ? Field<UObject*>(world, off.WorldInfo_CoverList) : 0; link; link = Field<UObject*>(link, off.CoverLink_NextCoverLink)) {
		if (Distance(Field<FVector>(link, off.Actor_Location), human.Location) > config.PostRadius + LINK_SEARCH_MARGIN) {
			continue;
		}
		int32_t slotCount = Field<TArray<uint8_t> >(link, off.CoverLink_Slots).Num;
		for (int32_t slot = 0; slot < slotCount; slot++) {
			FVector spot = FoxAI::GetSlotLocation(link, slot);
			if (!PostAllowed(bot.Controller, human, enemy, role, spot) || !IsFreeSpot(pawn, spot, pawn) || !CanClaimCover(pawn, link, slot)) {
				continue;
			}
			float yaw = (float)FoxAI::GetSlotRotation(link, slot).Yaw * (3.14159265f / 32768.0f);
			float dx = enemy.X - spot.X;
			float dy = enemy.Y - spot.Y;
			float length = Length2D(dx, dy);
			float facing = length > 0.0f ? (cosf(yaw) * dx + sinf(yaw) * dy) / length : 0.0f;
			if (facing < COVER_FACING_MIN || !CoverSeesPoint(pawn, link, slot, enemy)) {
				continue;
			}
			float score = 1.2f + 0.3f * facing - Distance(location, spot) / 1500.0f + RoleScore(role, length);
			if (!found || score > best.Score) {
				best = Candidate{ spot, score, POST_COVER };
				found = true;
			}
		}
	}
	float dx = enemy.X - human.Location.X;
	float dy = enemy.Y - human.Location.Y;
	float toEnemy = Length2D(dx, dy);
	FVector centre = toEnemy > 1.0f ? FVector{ human.Location.X + dx / toEnemy * 100.0f, human.Location.Y + dy / toEnemy * 100.0f, human.Location.Z } : human.Location;
	std::vector<FVector> spots;
	FindNavMeshSpots(bot.Controller, centre, config.PostRadius, spots);
	FVector chest = { enemy.X, enemy.Y, enemy.Z + CHEST_UP };
	size_t tried = 0;
	for (size_t i = 0; i < spots.size() && tried < SPOT_CANDIDATES; i++) {
		const FVector& spot = spots[i];
		if (!PostAllowed(bot.Controller, human, enemy, role, spot) || !IsFreeSpot(pawn, spot, pawn)) {
			continue;
		}
		tried++;
		float score = 0.5f - Distance(location, spot) / 1500.0f + RoleScore(role, Distance(spot, enemy));
		if (found && score <= best.Score) {
			continue;
		}
		FVector eye = { spot.X, spot.Y, spot.Z + EYE_UP };
		if (!ClearLine(pawn, eye, chest)) {
			continue;
		}
		best = Candidate{ spot, score, POST_SIGHT };
		found = true;
	}
	return found;
}

static const char* KindName(PostKind kind)
{
	switch (kind) {
	case POST_FORMATION: return "formation";
	case POST_COVER: return "cover";
	case POST_SIGHT: return "line of sight";
	default: return "no";
	}
}

static void SendToPost(Bot& bot, UObject* pawn, State& state, const Candidate& post, const HumanFrame& human, const FVector* enemy, DWORD now)
{
	const FVector location = Field<FVector>(pawn, off.Actor_Location);
	float away = Distance(location, post.Spot);
	SetGoToSprint(bot, away > SPRINT_DISTANCE);
	SendBehaviorEvent(bot.Controller, GOTO_COMMAND, pawn, 0, post.Spot);
	bool newKind = state.Kind != post.Kind;
	state.Kind = post.Kind;
	state.Post = post.Spot;
	state.CommandTick = now | 1;
	if (enemy) {
		state.Enemy = *enemy;
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x takes a %s post at (%.0f %.0f %.0f), %.0f away, %.0f from its human, %.0f from the enemy at (%.0f %.0f %.0f)."
			, bot.Controller, KindName(post.Kind), post.Spot.X, post.Spot.Y, post.Spot.Z, away, Distance(post.Spot, human.Location)
			, Distance(post.Spot, *enemy), enemy->X, enemy->Y, enemy->Z);
	}
	else if (newKind) {
		BOTS_LOG(LOG_LEVEL_DEBUG, "Bot 0x%08x falls into formation, %.0f from its post.", bot.Controller, away);
	}
}

static void PlaceBot(UObject* game, Bot& bot, UObject* pawn, const HumanFrame& human, int index, UObject* squad, float world_time, DWORD now)
{
	State& state = states[bot.Controller];
	const FVector location = Field<FVector>(pawn, off.Actor_Location);
	UObject* target = FoxAI::GetActiveTarget(bot.Controller);
	bool inCover = Field<uint8_t>(pawn, off.FoxPawn_CoverType) != 0;
	bool seesTarget = target && PawnSeesPawn(pawn, target);
	if (!target || seesTarget) {
		state.UnseenSinceTick = 0;
	}
	else if (!state.UnseenSinceTick) {
		state.UnseenSinceTick = now ? now : 1;
	}
	DWORD unseenMs = state.UnseenSinceTick && (int32_t)(now - state.UnseenSinceTick) > 0 ? now - state.UnseenSinceTick : 0;
	UObject* behavior = CurrentBehavior(bot.Controller);
	bool meleeStall = behavior && target && UE3::IsA(behavior, cls.FightMeleeClass) && Distance(location, Field<FVector>(target, off.Actor_Location)) > MELEE_REACH
		&& now - bot.LastMoveTick >= MELEE_STALL_MS;
	if ((seesTarget && !meleeStall) || inCover) {
		return;
	}
	FVector enemy;
	bool enemyKnown = false;
	if (target && (meleeStall || unseenMs >= TARGET_UNSEEN_MS)) {
		enemyKnown = TargetKnownPlace(squad, target, enemy);
	}
	else if (!target) {
		float forget = config.ForgetEnemySeconds > 0.0f ? config.ForgetEnemySeconds : 6.0f;
		enemyKnown = NearestKnownEnemy(squad, human.Location, world_time, forget, enemy);
	}
	if (enemyKnown) {
		bool keep = (state.Kind == POST_COVER || state.Kind == POST_SIGHT) && now - state.CommandTick < ENGAGE_MIN_MS && Distance(state.Enemy, enemy) < ENEMY_MOVED;
		if (keep) {
			return;
		}
		Candidate post;
		if (FindEngagementPost(game, bot, pawn, human, enemy, post)) {
			if (Distance(location, post.Spot) > POST_REACHED || state.Kind != post.Kind) {
				SendToPost(bot, pawn, state, post, human, &enemy, now);
			}
			else {
				state.CommandTick = now | 1;
				state.Enemy = enemy;
			}
			return;
		}
	}
	if (target && !meleeStall) {
		return;
	}
	FVector place = FormationPlace(human, index);
	FVector spot;
	if (!FindFreeNavMeshSpot(bot.Controller, pawn, place, POST_SNAP, spot)) {
		FVector nearer = { (place.X + human.Location.X) * 0.5f, (place.Y + human.Location.Y) * 0.5f, human.Location.Z };
		if (!FindFreeNavMeshSpot(bot.Controller, pawn, nearer, POST_SNAP, spot)) {
			state.Kind = POST_NONE;
			return;
		}
	}
	float away = Distance(location, spot);
	float trigger = human.Moving ? POST_MOVE_TRIGGER : POST_REACHED;
	bool moved = state.Kind == POST_FORMATION && Distance(state.Post, spot) > POST_MOVE_TRIGGER;
	if (away <= trigger) {
		state.Kind = POST_FORMATION;
		state.Post = spot;
		return;
	}
	if (state.Kind == POST_FORMATION && now - state.CommandTick < FORMATION_MIN_MS && !moved) {
		return;
	}
	if (!IsIdle(bot.Controller) && !IsFollowingCommand(bot.Controller) && !meleeStall) {
		return;
	}
	SendToPost(bot, pawn, state, Candidate{ spot, 0.0f, POST_FORMATION }, human, 0, now);
}

void Tick(UObject* game)
{
	if (!ready || !game) {
		return;
	}
	DWORD now = GetTickCount();
	if (now - last_tick < TICK_INTERVAL_MS) {
		return;
	}
	last_tick = now;
	for (std::map<UObject*, State>::iterator it = states.begin(); it != states.end(); ) {
		if (!FindBot(it->first)) {
			it = states.erase(it);
		}
		else {
			++it;
		}
	}
	UObject* squad = HumanSquad(game);
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	float worldTime = world ? Field<float>(world, soff.WorldInfo_TimeSeconds) : 0.0f;
	std::vector<Bot*> order;
	for (Bot& bot : bots) {
		order.push_back(&bot);
	}
	std::sort(order.begin(), order.end(), [](const Bot* a, const Bot* b) { return a->Slot < b->Slot; });
	std::map<UObject*, int> indexByHuman;
	for (Bot* bot : order) {
		UObject* pawn = Field<UObject*>(bot->Controller, off.Controller_Pawn);
		if (!IsAlive(pawn) || !IsAlive(bot->TargetPawn)) {
			states[bot->Controller].Kind = POST_NONE;
			continue;
		}
		int index = indexByHuman[bot->TargetPawn]++;
		if ((int32_t)(bot->GraceUntilTick - now) > 0) {
			continue;
		}
		HumanFrame human;
		ReadHuman(bot->TargetPawn, human);
		PlaceBot(game, *bot, pawn, human, index, squad, worldTime, now);
	}
}

}
}
