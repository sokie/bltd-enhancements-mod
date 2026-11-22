#pragma once
#include "ue3.hpp"
#include <stdint.h>

namespace FoxAI {

struct CoverInfo {
	UE3::UObject* Link;
	int32_t SlotIdx;
};

typedef float (*tCoverRatingHandler)(UE3::UObject* controller, const CoverInfo* cover, float rating);

typedef uint32_t (*tFollowInitializedHandler)(UE3::UObject* ability, uint32_t result);

bool Init(uintptr_t title_base);
bool HookCoverRating(tCoverRatingHandler handler);
bool HookFollowInitialize(tFollowInitializedHandler handler);
uint32_t InitializeAbility(UE3::UObject* ability);

UE3::FVector GetSlotLocation(UE3::UObject* link, int32_t slot_idx);
UE3::FRotator GetSlotRotation(UE3::UObject* link, int32_t slot_idx);
UE3::UObject* GetActiveTarget(UE3::UObject* controller);

typedef void (*tSelectTargetsHandler)(UE3::UObject* squad);
bool HookSelectTargets(tSelectTargetsHandler handler);

typedef bool (*tCanSeePawnHandler)(UE3::UObject* controller, UE3::UObject* pawn);
bool HookCanSeePawn(UE3::UObject* controller, tCanSeePawnHandler handler);

typedef void (*tCoverActionsHandler)(UE3::UObject* controller, UE3::TArray<int32_t>* actions);
bool HookCoverActions(tCoverActionsHandler handler);

typedef void (*tTeamListHandler)(UE3::UObject* movie, UE3::TArray<UE3::UObject*>* list);
bool HookTeamList(tTeamListHandler handler);

bool HookPathParams(float max_hover, float min_walkable);

}
