#pragma once
#include "ue3.hpp"
#include <stdint.h>
#include <string>
#include <vector>

namespace NavMesh {

static const int LIST_LIMIT = 4096;
static const float MIN_EDGE_WIDTH = 64.0f;

static const uint8_t COVER_LEAN_LEFT = 0x1;
static const uint8_t COVER_LEAN_RIGHT = 0x2;
static const uint8_t COVER_POPUP = 0x4;

struct CoverSpot {
	UE3::FVector Location;
	int32_t Yaw;
	uint8_t CoverType;
	uint8_t Flags;
	UE3::UObject* Link;
};

struct JumpLink {
	UE3::FVector From;
	UE3::FVector To;
};

bool Init(uintptr_t title_base);
int Resolve();
void OnMatchStart(UE3::UObject* game);
void CutModeMeshes(UE3::UObject* game);
void LowerStairs(UE3::UObject* game);
void OnLevelUnload();
bool InModeCut(const UE3::FVector& point, float inset = 0.0f);
static const float CUT_STAND_INSET = 24.0f;
void CutTurrets(UE3::UObject* game);
UE3::UObject* PathSetupFunction();
void OnPathSetup(UE3::UObject* controller, const UE3::FVector& from, const UE3::FVector& to);

UE3::UObject* PylonAt(const UE3::FVector& location);
UE3::UObject* SpawnPylonAt(UE3::UObject* game, const UE3::FVector& location, float radius);
bool LoadMeshes(UE3::UObject* pylon, const uint8_t* nav, uint32_t nav_size, const uint8_t* obstacle, uint32_t obstacle_size);
void AddToOctree(UE3::UObject* pylon);
bool Link(UE3::UObject* pylon);
int32_t WidenDynamicEdges(UE3::UObject* world, UE3::UObject* patch);
int32_t DynamicEdgeCount(UE3::UObject* pylon);
void DetachPylons(UE3::UObject* world, const std::vector<UE3::UObject*>& pylons);

bool ReadCover(const std::string& map, std::vector<CoverSpot>& spots);
UE3::UObject* AddCover(UE3::UObject* game, CoverSpot& spot);
void DisableCover(UE3::UObject* link);

std::vector<JumpLink>& Links();
const char* ApplyLink(UE3::UObject* world, const JumpLink& link);
// Call after the Pylons are linked: linking a Pylon flushes the dynamic edges of its mesh.
void ApplyLinks(UE3::UObject* world);

}
