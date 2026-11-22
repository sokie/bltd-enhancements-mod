#include <winsock2.h>
#include <Windows.h>
#include "fox-ai.hpp"
#include "../loader/hook.hpp"
#include <string.h>

using UE3::UObject;
using UE3::FVector;

namespace FoxAI {

static const uint32_t RVA_GET_COVER_RATING_FOR = 0x0097CA70;
static const uint32_t RVA_COVERLINK_GET_SLOT_LOCATION = 0x00300FA0;
static const uint32_t RVA_FOLLOW_ABILITY_VTABLE_INITIALIZE = 0x0102CDF8;
static const uint32_t RVA_FOLLOW_ABILITY_INITIALIZE = 0x009C5720;
static const uint32_t RVA_HIERARCHICAL_ABILITY_INITIALIZE = 0x0096EF70;
static const uint32_t RVA_GET_VALID_TEAM_LIST = 0x009AA4B0;
static const uint32_t RVA_COVERLINK_GET_SLOT_ROTATION = 0x003011C0;
static const uint32_t RVA_GET_ACTIVE_TARGET = 0x00989F40;
static const uint32_t RVA_SELECT_TARGETS = 0x009A88D0;
static const uint32_t RVA_CAN_SEE_PAWN = 0x009C4C30;
static const uint32_t CAN_SEE_PAWN_VTABLE_OFFSET = 0x438;
static const int CAN_SEE_PAWN_VTABLES = 8;
static const int GET_COVER_RATING_FOR_DETOUR_LEN = 13;
static const uint8_t GET_COVER_RATING_FOR_PROLOGUE[GET_COVER_RATING_FOR_DETOUR_LEN] = { 0x6A, 0xFF, 0x68, 0xB8, 0xFC, 0x07, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const int GET_VALID_TEAM_LIST_DETOUR_LEN = 13;
static const uint8_t GET_VALID_TEAM_LIST_PROLOGUE[GET_VALID_TEAM_LIST_DETOUR_LEN] = { 0x6A, 0xFF, 0x68, 0x09, 0x33, 0x08, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const int SELECT_TARGETS_DETOUR_LEN = 13;
static const uint8_t SELECT_TARGETS_PROLOGUE[SELECT_TARGETS_DETOUR_LEN] = { 0x6A, 0xFF, 0x68, 0x98, 0x30, 0x08, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t RVA_GET_AVAILABLE_COVER_ACTIONS = 0x009C5350;
static const int COVER_ACTIONS_DETOUR_LEN = 13;
static const uint8_t COVER_ACTIONS_PROLOGUE[COVER_ACTIONS_DETOUR_LEN] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x1C, 0x53, 0x55, 0x56, 0x33, 0xDB, 0x57 };
static const uint32_t RVA_POPULATE_PATH_PARAMS = 0x003E0840;
static const int POPULATE_PATH_PARAMS_DETOUR_LEN = 9;
static const uint8_t POPULATE_PATH_PARAMS_PROLOGUE[POPULATE_PATH_PARAMS_DETOUR_LEN] = { 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x28, 0x85, 0xF6 };
static const int32_t HANDLE_MIN_WALKABLE_Z = 0xC0;
static const int32_t HANDLE_MAX_HOVER_DISTANCE = 0xC4;

typedef float (__stdcall *tGetCoverRatingFor)(UObject* squad, UObject* controller, CoverInfo* cover, void* conditions);
typedef FVector* (__fastcall *tGetSlotLocation)(UObject* link, void* unused_edx, FVector* result, int32_t slot_idx, uint32_t force_use_offset);
typedef UE3::FRotator* (__fastcall *tGetSlotRotation)(UObject* link, void* unused_edx, UE3::FRotator* result, int32_t slot_idx, uint32_t force_use_offset);
typedef UObject* (__fastcall *tGetActiveTarget)(UObject* controller, void* unused_edx, void* found_info);
typedef void (__stdcall *tSelectTargets)(UObject* squad);
typedef bool (__stdcall *tGetAvailableCoverActions)(UObject* controller, UObject* target, UE3::TArray<int32_t>* actions);
typedef uint32_t (__fastcall *tInitialize)(UObject* ability, void* unused_edx);
typedef UE3::TArray<UObject*>* (__stdcall *tGetValidTeamList)(UObject* movie, UE3::TArray<UObject*>* list);
typedef uint32_t (__fastcall *tCanSeePawn)(UObject* controller, void* unused_edx, UObject* pawn, uint32_t may_skip_checks);
typedef int32_t (__stdcall *tPopulatePathParams)(UObject* handle);

static uint8_t* get_cover_rating_for_address = 0;
static tGetCoverRatingFor get_cover_rating_for_original = 0;
static tGetSlotLocation get_slot_location = 0;
static tCoverRatingHandler cover_rating_handler = 0;
static uint32_t* follow_initialize_slot = 0;
static tInitialize follow_initialize_original = 0;
static tInitialize hierarchical_initialize = 0;
static tFollowInitializedHandler follow_initialized_handler = 0;
static uint8_t* get_valid_team_list_address = 0;
static tGetValidTeamList get_valid_team_list_original = 0;
static tTeamListHandler team_list_handler = 0;
static tGetSlotRotation get_slot_rotation = 0;
static tGetActiveTarget get_active_target = 0;
static uint8_t* select_targets_address = 0;
static tSelectTargets select_targets_original = 0;
static tSelectTargetsHandler select_targets_handler = 0;
static uint8_t* cover_actions_address = 0;
static tGetAvailableCoverActions cover_actions_original = 0;
static tCoverActionsHandler cover_actions_handler = 0;
static tCanSeePawn can_see_pawn_original = 0;
static tCanSeePawnHandler can_see_pawn_handler = 0;
static uint32_t* can_see_pawn_slots[CAN_SEE_PAWN_VTABLES];
static int can_see_pawn_slot_count = 0;
static uint8_t* populate_path_params_address = 0;
static tPopulatePathParams populate_path_params_original = 0;
static float max_hover_distance = 0.0f;
static float min_walkable_z = 1.0f;

bool Init(uintptr_t title_base)
{
	get_slot_rotation = (tGetSlotRotation)(title_base + RVA_COVERLINK_GET_SLOT_ROTATION);
	get_active_target = (tGetActiveTarget)(title_base + RVA_GET_ACTIVE_TARGET);
	can_see_pawn_original = (tCanSeePawn)(title_base + RVA_CAN_SEE_PAWN);
	select_targets_address = (uint8_t*)(title_base + RVA_SELECT_TARGETS);
	cover_actions_address = (uint8_t*)(title_base + RVA_GET_AVAILABLE_COVER_ACTIONS);
	get_valid_team_list_address = (uint8_t*)(title_base + RVA_GET_VALID_TEAM_LIST);
	get_cover_rating_for_address = (uint8_t*)(title_base + RVA_GET_COVER_RATING_FOR);
	populate_path_params_address = (uint8_t*)(title_base + RVA_POPULATE_PATH_PARAMS);
	get_slot_location = (tGetSlotLocation)(title_base + RVA_COVERLINK_GET_SLOT_LOCATION);
	follow_initialize_slot = (uint32_t*)(title_base + RVA_FOLLOW_ABILITY_VTABLE_INITIALIZE);
	follow_initialize_original = (tInitialize)(title_base + RVA_FOLLOW_ABILITY_INITIALIZE);
	hierarchical_initialize = (tInitialize)(title_base + RVA_HIERARCHICAL_ABILITY_INITIALIZE);
	return memcmp(get_cover_rating_for_address, GET_COVER_RATING_FOR_PROLOGUE, GET_COVER_RATING_FOR_DETOUR_LEN) == 0;
}

static float __stdcall GetCoverRatingForHook(UObject* squad, UObject* controller, CoverInfo* cover, void* conditions)
{
	float rating = get_cover_rating_for_original(squad, controller, cover, conditions);
	if (!cover_rating_handler || !controller || !cover || !cover->Link) {
		return rating;
	}
	return cover_rating_handler(controller, cover, rating);
}

bool HookCoverRating(tCoverRatingHandler handler)
{
	cover_rating_handler = handler;
	if (get_cover_rating_for_original) {
		return true;
	}
	get_cover_rating_for_original = (tGetCoverRatingFor)DetourClassFunc(get_cover_rating_for_address, (BYTE*)GetCoverRatingForHook, GET_COVER_RATING_FOR_DETOUR_LEN);
	return get_cover_rating_for_original != 0;
}

static uint32_t __fastcall FollowInitializeHook(UObject* ability, void* unused_edx)
{
	uint32_t result = follow_initialize_original(ability, 0);
	if (!follow_initialized_handler) {
		return result;
	}
	return follow_initialized_handler(ability, result);
}

static bool WriteVtableSlot(uint32_t* slot, uint32_t value)
{
	DWORD protection;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
		return false;
	}
	*slot = value;
	VirtualProtect(slot, sizeof(*slot), protection, &protection);
	return true;
}

bool HookFollowInitialize(tFollowInitializedHandler handler)
{
	if (*follow_initialize_slot == (uint32_t)FollowInitializeHook) {
		follow_initialized_handler = handler;
		return true;
	}
	if (*follow_initialize_slot != (uint32_t)follow_initialize_original) {
		return false;
	}
	follow_initialized_handler = handler;
	return WriteVtableSlot(follow_initialize_slot, (uint32_t)FollowInitializeHook);
}

uint32_t InitializeAbility(UObject* ability)
{
	return hierarchical_initialize(ability, 0);
}

FVector GetSlotLocation(UObject* link, int32_t slot_idx)
{
	FVector location = { 0.0f, 0.0f, 0.0f };
	get_slot_location(link, 0, &location, slot_idx, 0);
	return location;
}

UE3::FRotator GetSlotRotation(UObject* link, int32_t slot_idx)
{
	UE3::FRotator rotation = { 0, 0, 0 };
	get_slot_rotation(link, 0, &rotation, slot_idx, 0);
	return rotation;
}

UObject* GetActiveTarget(UObject* controller)
{
	return get_active_target(controller, 0, 0);
}

static void __stdcall SelectTargetsHook(UObject* squad)
{
	if (select_targets_handler) {
		select_targets_handler(squad);
	}
	select_targets_original(squad);
}

bool HookSelectTargets(tSelectTargetsHandler handler)
{
	select_targets_handler = handler;
	if (select_targets_original) {
		return true;
	}
	if (memcmp(select_targets_address, SELECT_TARGETS_PROLOGUE, SELECT_TARGETS_DETOUR_LEN) != 0) {
		return false;
	}
	select_targets_original = (tSelectTargets)DetourClassFunc(select_targets_address, (BYTE*)SelectTargetsHook, SELECT_TARGETS_DETOUR_LEN);
	return select_targets_original != 0;
}

static bool __stdcall GetAvailableCoverActionsHook(UObject* controller, UObject* target, UE3::TArray<int32_t>* actions)
{
	cover_actions_original(controller, target, actions);
	if (cover_actions_handler && controller && actions) {
		cover_actions_handler(controller, actions);
	}
	return actions && actions->Num > 0;
}

bool HookCoverActions(tCoverActionsHandler handler)
{
	cover_actions_handler = handler;
	if (cover_actions_original) {
		return true;
	}
	if (memcmp(cover_actions_address, COVER_ACTIONS_PROLOGUE, COVER_ACTIONS_DETOUR_LEN) != 0) {
		return false;
	}
	cover_actions_original = (tGetAvailableCoverActions)DetourClassFunc(cover_actions_address, (BYTE*)GetAvailableCoverActionsHook, COVER_ACTIONS_DETOUR_LEN);
	return cover_actions_original != 0;
}

static uint32_t __fastcall CanSeePawnHook(UObject* controller, void* unused_edx, UObject* pawn, uint32_t may_skip_checks)
{
	uint32_t result = can_see_pawn_original(controller, 0, pawn, may_skip_checks);
	if (result && can_see_pawn_handler && pawn && !can_see_pawn_handler(controller, pawn)) {
		return 0;
	}
	return result;
}

bool HookCanSeePawn(UObject* controller, tCanSeePawnHandler handler)
{
	can_see_pawn_handler = handler;
	if (!controller) {
		return false;
	}
	uint32_t* slot = (uint32_t*)(*(uint8_t**)controller + CAN_SEE_PAWN_VTABLE_OFFSET);
	if (*slot == (uint32_t)CanSeePawnHook) {
		return true;
	}
	if (*slot != (uint32_t)can_see_pawn_original || can_see_pawn_slot_count >= CAN_SEE_PAWN_VTABLES) {
		return false;
	}
	if (!WriteVtableSlot(slot, (uint32_t)CanSeePawnHook)) {
		return false;
	}
	can_see_pawn_slots[can_see_pawn_slot_count++] = slot;
	return true;
}

static UE3::TArray<UObject*>* __stdcall GetValidTeamListHook(UObject* movie, UE3::TArray<UObject*>* list)
{
	get_valid_team_list_original(movie, list);
	if (team_list_handler) {
		team_list_handler(movie, list);
	}
	return list;
}

bool HookTeamList(tTeamListHandler handler)
{
	team_list_handler = handler;
	if (get_valid_team_list_original) {
		return true;
	}
	if (memcmp(get_valid_team_list_address, GET_VALID_TEAM_LIST_PROLOGUE, GET_VALID_TEAM_LIST_DETOUR_LEN) != 0) {
		return false;
	}
	get_valid_team_list_original = (tGetValidTeamList)DetourClassFunc(get_valid_team_list_address, (BYTE*)GetValidTeamListHook, GET_VALID_TEAM_LIST_DETOUR_LEN);
	return get_valid_team_list_original != 0;
}

static int32_t __stdcall PopulatePathParamsHook(UObject* handle)
{
	int32_t result = populate_path_params_original(handle);
	if (!result) {
		return result;
	}
	float& hover = UE3::Field<float>(handle, HANDLE_MAX_HOVER_DISTANCE);
	if (hover >= 0.0f && hover < max_hover_distance) {
		hover = max_hover_distance;
	}
	float& walkable = UE3::Field<float>(handle, HANDLE_MIN_WALKABLE_Z);
	if (walkable > min_walkable_z) {
		walkable = min_walkable_z;
	}
	return result;
}

bool HookPathParams(float max_hover, float min_walkable)
{
	max_hover_distance = max_hover;
	min_walkable_z = min_walkable;
	if (populate_path_params_original) {
		return true;
	}
	if (memcmp(populate_path_params_address, POPULATE_PATH_PARAMS_PROLOGUE, POPULATE_PATH_PARAMS_DETOUR_LEN) != 0) {
		return false;
	}
	populate_path_params_original = (tPopulatePathParams)DetourClassFunc(populate_path_params_address, (BYTE*)PopulatePathParamsHook, POPULATE_PATH_PARAMS_DETOUR_LEN);
	return populate_path_params_original != 0;
}

}
