#include <winsock2.h>
#include <Windows.h>
#include "../loader/log.hpp"
#include "navcache.hpp"
#include "navmesh.hpp"
#include "platform.hpp"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using UE3::UObject;
using UE3::FVector;
using UE3::Field;

#define NAVMESH_LOG(level, format, ...) Log::Write(level, "[NavMesh] " format, __VA_ARGS__)

namespace NavMesh {

static const uint32_t RVA_PYLON_ADD_TO_OCTREE = 0x00412950;
static const uint32_t RVA_PYLON_REMOVE_FROM_OCTREE = 0x00412F80;
static const uint32_t RVA_PYLON_POST_LOAD = 0x003F9FE0;
static const uint32_t RVA_REBUILD_DYNAMIC_EDGES = 0x00418EE0;
static const uint32_t RVA_FLUSH_DYNAMIC_EDGES = 0x00418610;
static const uint32_t RVA_NAVIGATION_POINT_ADD_TO_OCTREE = 0x00445CE0;
static const uint32_t RVA_CROSS_PYLON_EDGE_VTABLE_STORE = 0x003F2D38;
static const uint8_t CROSS_PYLON_EDGE_VTABLE_STORE[] = { 0xC7, 0x06 };
static const uint32_t RVA_POLY_BORDER_SEGMENTS = 0x00401DC0;
static const uint8_t POLY_BORDER_SEGMENTS_PROLOGUE[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x44, 0x53, 0x56 };
static const int32_t MESH_BORDER_SEGMENTS = 0xF4;
static const uint32_t RVA_ADD_CROSS_PYLON_EDGE = 0x003FFA30;
static const uint8_t ADD_CROSS_PYLON_EDGE_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0x46, 0xA3, 0x04, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC, 0x1C, 0x53, 0x55, 0x56, 0x57 };
static const int32_t MESH_LOCAL_TO_WORLD = 0x240;
static const uint32_t LINK_MAGIC = 0x4B4E4C42;
static const uint32_t LINK_VERSION = 1;
static const float LINK_WIDTH = 128.0f;
static const float LINK_FLOOR_WINDOW = 100.0f;
static const uint8_t PYLON_POST_LOAD_PROLOGUE[] = { 0x51, 0x53, 0x55, 0x56, 0x57, 0x8B, 0xD9 };
static const uint8_t REBUILD_DYNAMIC_EDGES_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0xC0, 0xB7, 0x04, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t FLUSH_DYNAMIC_EDGES_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0xF8, 0xB6, 0x04, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t CACHE_MAGIC = 0x434E4C42;
static const uint32_t CACHE_VERSION = 1;
static const uint32_t CACHE_EXISTING_PYLON = 0;
static const uint32_t CACHE_NEW_PYLON = 1;
static const uint32_t PYLON_ADD_TO_OCTREE_SLOT = 0x3B0 / 4;
static const uint32_t PYLON_REMOVE_FROM_OCTREE_SLOT = 0x3B4 / 4;
static const int32_t WORLDINFO_PYLON_LIST = 0x3F4;
static const int32_t PYLON_NAVMESH_PTR = 0x2AC;
static const int32_t MESH_DYNAMIC_EDGE_DATA = 0xAC;
static const int32_t MESH_DYNAMIC_EDGE_SLOTS = 0xB0;
static const int32_t MESH_DYNAMIC_EDGE_BITS = 0xB8;
static const int32_t MESH_DYNAMIC_EDGE_BITS_HEAP = 0xC8;
static const int32_t MESH_DYNAMIC_EDGE_FREE = 0xD8;
static const int32_t DYNAMIC_EDGE_SLOT_SIZE = 0x10;
static const int32_t DYNAMIC_EDGE_SLOT_EDGE = 0x04;
static const int32_t EDGE_MESH = 0x04;
static const int32_t EDGE_VERTS = 0x08;
static const int32_t EDGE_LENGTH = 0x20;
static const int32_t EDGE_EFFECTIVE = 0x24;
static const int DYNAMIC_EDGE_LIMIT = 4096;
static const float EDGE_JOIN_DISTANCE = 2.0f;
static const float EDGE_JOIN_HEIGHT = 40.0f;
static const int NARROW_LINK_SAMPLES = 12;
static const float SEAM_LINK_WIDTH = 128.0f;
static const int32_t MAX_PATH_VISITS = 16384;
static const uint32_t COVER_MAGIC = 0x56434C42;
static const uint32_t COVER_VERSION = 1;
static const int32_t COVER_SLOT_SIZE = 0xA8;
static const float UNREAL_YAW_TO_RADIANS = 3.14159265f / 32768.0f;
static const uint32_t RVA_OBSTACLE_INTERFACE_VTABLE = 0x00E4EEE4;
static const int32_t OBSTACLE_SHAPE_SLOT = 3;
static const int32_t OBSTACLE_PRESERVE_SLOT = 4;
static const uint32_t RVA_OBSTACLE_PRESERVE = 0x0021B070;
static const uint32_t RVA_OBSTACLE_SHAPE = 0x003D6C50;
static const int32_t OBSTACLE_INTERFACE = 0x1D0;
static const int32_t POLY_OBSTACLE_COUNT = 0xAC;
static const uint8_t OBSTACLE_SHAPE_PROLOGUE[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x54, 0x53, 0x56, 0x57, 0x8B, 0xF1 };
static const uint32_t RVA_VECTOR_ADD = 0x00092A80;
static const uint8_t VECTOR_ADD_PROLOGUE[] = { 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x08, 0x57, 0x8B, 0x7E, 0x04 };
static const uint32_t RVA_INTERSECTING_POLYS = 0x00406E60;
static const uint8_t INTERSECTING_POLYS_PROLOGUE[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x6A, 0xFF, 0x68, 0x6B, 0x9E, 0x04, 0x01, 0x64, 0xA1 };
static const uint32_t RVA_REGISTER_OBSTACLE = 0x0040E900;
static const uint8_t REGISTER_OBSTACLE_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0xE8, 0xAC, 0x04, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50 };
static const uint32_t RVA_UPDATE_OBSTACLES = 0x0041FB20;
static const uint8_t UPDATE_OBSTACLES_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0x65, 0xBE, 0x04, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50 };
static const float OBSTACLE_MARGIN = 32.0f;
static const float TURRET_MARGIN = 0.0f;
static const float OBSTACLE_PLANE_HEIGHT = 30.0f;
static const float OBSTACLE_FLOOR_WINDOW = 40.0f;
static const float OBSTACLE_HEADROOM = 80.0f;
static const float OBSTACLE_STEP = 30.0f;
static const float OBSTACLE_SPOT_HEIGHT = 240.0f;
static const float OBSTACLE_SIZE_LIMIT = 1500.0f;
static const int OBSTACLE_LIMIT = 128;

typedef void (__fastcall *tPylonOctree)(UObject* pylon, void* unused_edx);
typedef void (__fastcall *tPylonPostLoad)(UObject* pylon, void* unused_edx);
typedef void (__fastcall *tMeshDynamicEdges)(void* mesh, void* unused_edx);
typedef void (__cdecl *tPolyBorderSegments)(void* poly, void* segments);

static tPylonPostLoad pylon_post_load = 0;
static tPolyBorderSegments poly_border_segments = 0;
static tMeshDynamicEdges rebuild_dynamic_edges = 0;
static tMeshDynamicEdges flush_dynamic_edges = 0;
static uintptr_t pylon_add_address = 0;
static uintptr_t cross_edge_vtable = 0;
static uintptr_t pylon_remove_address = 0;
static bool ready = false;
static std::vector<UObject*> spawned;
static std::vector<JumpLink> links;
static std::vector<JumpLink> seams;
struct PtrArray {
	void* Data;
	int32_t Num;
	int32_t Max;
};
typedef void (__stdcall *tAddCrossEdge)(const FVector* v1, const FVector* v2, PtrArray* polys, int32_t one_way, PtrArray* out);
static tAddCrossEdge add_cross_edge = 0;
typedef int32_t (__fastcall *tObstacleShape)(uint8_t* obstacle_interface, void* unused_edx, PtrArray* shape);
typedef int32_t (__fastcall *tVectorAdd)(PtrArray* array, void* unused_edx, const FVector* item);
typedef int32_t (__cdecl *tIntersectingPolys)(const FVector* centre, const FVector* extent, PtrArray* polys, int32_t ignore_dynamic, int32_t both);
typedef int32_t (__fastcall *tRegisterObstacle)(uint8_t* obstacle_interface, void* unused_edx, const PtrArray* shape, const PtrArray* polys);
static tObstacleShape obstacle_shape = 0;
typedef int32_t (__fastcall *tObstaclePreserve)(uint8_t* obstacle_interface, void* unused_edx);
static tObstaclePreserve obstacle_preserve = 0;
static tVectorAdd vector_add = 0;
static tIntersectingPolys intersecting_polys = 0;
static tRegisterObstacle register_obstacle = 0;
static tMeshDynamicEdges update_obstacles = 0;
struct ModeObstacle {
	UObject* Obstacle;
	UObject* Mesh;
	FVector Corners[4];
	FVector Centre;
	float HalfX;
	float HalfY;
	float Yaw;
	float Floor;
};
static std::vector<ModeObstacle> obstacles;
static std::vector<UObject*> turrets_seen;
static bool turrets_ready = false;
static const char TURRET_CLASS[] = "FoxPawn_Turret";
static const char OBJECTIVE_CLASS[] = "FoxGameObject_ControlPointContent";
static bool cover_ready = false;
static uintptr_t navigation_point_add_address = 0;

static struct {
	UObject* Pylon;
	UObject* PylonDefault;
	UObject* HandleDefault;
	UObject* bStatic;
	UObject* bNoDelete;
	UObject* Spawn;
	UObject* GetPylonFromPos;
	UObject* GetMapName;
	UObject* GoalAt;
	UObject* GoalAtDefault;
	UObject* CoverLink;
	UObject* CoverLinkDefault;
	UObject* CoverLink_bDisabled;
	UObject* Slot_bLeanLeft;
	UObject* Slot_bLeanRight;
	UObject* Slot_bCanPopUp;
	UObject* Slot_bEnabled;
	UObject* NavMeshObstacle;
	UObject* ModeMesh;
	UObject* bBlockActors;
	UObject* bCollideActors;
	UObject* bDeleteMe;
	UObject* UnRegisterObstacle;
	UObject* PrimitiveComponent;
	UObject* SetComponentHidden;
	UObject* StaticMeshComponent;
	UObject* MeshCollection;
	UObject* ReattachComponent;
} obj;

static struct {
	int32_t Actor_WorldInfo;
	int32_t WorldInfo_PylonList;
	int32_t Pylon_NextPylon;
	int32_t Pylon_ExpansionRadius;
	int32_t Pylon_NavMeshPtr;
	int32_t Pylon_ObstacleMesh;
	int32_t Spawn_SpawnClass;
	int32_t Spawn_SpawnLocation;
	int32_t Spawn_bNoCollisionFail;
	int32_t Spawn_ReturnValue;
	int32_t GetPylonFromPos_Position;
	int32_t GetPylonFromPos_ReturnValue;
	int32_t GetMapName_ReturnValue;
	int32_t Evaluator_MaxPathVisits;
	int32_t Actor_Location;
	int32_t Actor_Rotation;
	int32_t Spawn_SpawnRotation;
	int32_t WorldInfo_CoverList;
	int32_t CoverLink_NextCoverLink;
	int32_t CoverLink_Slots;
	int32_t Slot_CoverType;
	int32_t Slot_LocationOffset;
	int32_t Slot_RotationOffset;
	int32_t Slot_bFailedToFindSurface;
	int32_t MeshActor_StaticMeshComponent;
	int32_t Component_Bounds;
	int32_t Actor_CollisionComponent;
	int32_t WorldInfo_PawnList;
	int32_t Pawn_NextPawn;
	int32_t Actor_Components;
	int32_t SetComponentHidden_NewHidden;
	int32_t Component_Owner;
	int32_t Component_LocalToWorld;
	int32_t Component_Translation;
	int32_t Component_Rotation;
	int32_t StaticMeshComponent_StaticMesh;
	int32_t ReattachComponent_ComponentToReattach;
} off;

static struct {
	UObject* ChooseNavMeshGoal;
	UObject* Handle_bUltraVerbosePathDebugging;
	UObject* Handle_bDebugConstraintsAndGoalEvals;
	int32_t Controller_NavigationHandle;
	int32_t Handle_PathGoalList;
	int32_t Handle_PathConstraintList;
	int32_t Evaluator_NextEvaluator;
	int32_t Constraint_NextConstraint;
} setup;
static int path_traces_left = 0;
static UObject* traced_handle = 0;
static int setups_logged = 0;
static const float PATH_TRACE_DISTANCE = 5000.0f;
static float path_trace_distance = PATH_TRACE_DISTANCE;
static const int SETUP_LOG_LIMIT = 3;
static const int LIST_LOG_LIMIT = 8;

static void CountLinks(UObject* world, int32_t& total, int32_t& narrow, bool report);
static void DumpLinks(UObject* world);

static int32_t __fastcall ObstacleShape(uint8_t* obstacle_interface, void* unused_edx, PtrArray* shape)
{
	UObject* obstacle = (UObject*)(obstacle_interface - OBSTACLE_INTERFACE);
	for (const ModeObstacle& cut : obstacles) {
		if (cut.Obstacle == obstacle) {
			for (const FVector& corner : cut.Corners) {
				vector_add(shape, 0, &corner);
			}
			return 1;
		}
	}
	return obstacle_shape(obstacle_interface, unused_edx, shape);
}

static int32_t __fastcall ObstaclePreserve(uint8_t* obstacle_interface, void* unused_edx)
{
	UObject* obstacle = (UObject*)(obstacle_interface - OBSTACLE_INTERFACE);
	for (const ModeObstacle& cut : obstacles) {
		if (cut.Obstacle == obstacle) {
			return 0;
		}
	}
	return obstacle_preserve(obstacle_interface, unused_edx);
}

static bool WriteSlot(void** slot, void* value)
{
	DWORD protection = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
		return false;
	}
	*slot = value;
	VirtualProtect(slot, sizeof(*slot), protection, &protection);
	return true;
}

static void HookObstacleShape(uintptr_t title_base)
{
	void** slot = (void**)(title_base + RVA_OBSTACLE_INTERFACE_VTABLE) + OBSTACLE_SHAPE_SLOT;
	if (memcmp((void*)(title_base + RVA_OBSTACLE_SHAPE), OBSTACLE_SHAPE_PROLOGUE, sizeof(OBSTACLE_SHAPE_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_VECTOR_ADD), VECTOR_ADD_PROLOGUE, sizeof(VECTOR_ADD_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_INTERSECTING_POLYS), INTERSECTING_POLYS_PROLOGUE, sizeof(INTERSECTING_POLYS_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_REGISTER_OBSTACLE), REGISTER_OBSTACLE_PROLOGUE, sizeof(REGISTER_OBSTACLE_PROLOGUE)) != 0) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The engine's NavMeshObstacle functions are not where expected, the meshes of a game mode are not cut.");
		return;
	}
	void* previous = *slot;
	if (!WriteSlot(slot, (void*)ObstacleShape)) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The NavMeshObstacle vtable cannot be written, the meshes of a game mode are not cut.");
		return;
	}
	obstacle_shape = (tObstacleShape)previous;
	vector_add = (tVectorAdd)(title_base + RVA_VECTOR_ADD);
	intersecting_polys = (tIntersectingPolys)(title_base + RVA_INTERSECTING_POLYS);
	register_obstacle = (tRegisterObstacle)(title_base + RVA_REGISTER_OBSTACLE);
	if (memcmp((void*)(title_base + RVA_UPDATE_OBSTACLES), UPDATE_OBSTACLES_PROLOGUE, sizeof(UPDATE_OBSTACLES_PROLOGUE)) == 0) {
		update_obstacles = (tMeshDynamicEdges)(title_base + RVA_UPDATE_OBSTACLES);
	}
	void** preserve = (void**)(title_base + RVA_OBSTACLE_INTERFACE_VTABLE) + OBSTACLE_PRESERVE_SLOT;
	void* kept = *preserve;
	if (kept != (void*)(title_base + RVA_OBSTACLE_PRESERVE) || !WriteSlot(preserve, (void*)ObstaclePreserve)) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "NavMeshObstacle's PreserveInternalPolys is not where expected, paths still cross the cuts.");
		return;
	}
	obstacle_preserve = (tObstaclePreserve)kept;
}

bool Init(uintptr_t title_base)
{
	if (memcmp((void*)(title_base + RVA_PYLON_POST_LOAD), PYLON_POST_LOAD_PROLOGUE, sizeof(PYLON_POST_LOAD_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_REBUILD_DYNAMIC_EDGES), REBUILD_DYNAMIC_EDGES_PROLOGUE, sizeof(REBUILD_DYNAMIC_EDGES_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_FLUSH_DYNAMIC_EDGES), FLUSH_DYNAMIC_EDGES_PROLOGUE, sizeof(FLUSH_DYNAMIC_EDGES_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_CROSS_PYLON_EDGE_VTABLE_STORE), CROSS_PYLON_EDGE_VTABLE_STORE, sizeof(CROSS_PYLON_EDGE_VTABLE_STORE)) != 0
		|| memcmp((void*)(title_base + RVA_POLY_BORDER_SEGMENTS), POLY_BORDER_SEGMENTS_PROLOGUE, sizeof(POLY_BORDER_SEGMENTS_PROLOGUE)) != 0
		|| !NavCache::Init(title_base)) {
		return false;
	}
	cross_edge_vtable = *(uintptr_t*)(title_base + RVA_CROSS_PYLON_EDGE_VTABLE_STORE + sizeof(CROSS_PYLON_EDGE_VTABLE_STORE));
	pylon_post_load = (tPylonPostLoad)(title_base + RVA_PYLON_POST_LOAD);
	poly_border_segments = (tPolyBorderSegments)(title_base + RVA_POLY_BORDER_SEGMENTS);
	if (memcmp((void*)(title_base + RVA_ADD_CROSS_PYLON_EDGE), ADD_CROSS_PYLON_EDGE_PROLOGUE, sizeof(ADD_CROSS_PYLON_EDGE_PROLOGUE)) == 0) {
		add_cross_edge = (tAddCrossEdge)(title_base + RVA_ADD_CROSS_PYLON_EDGE);
	}
	else {
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "AddDynamicCrossPylonEdge is not where expected; links make no edges.");
	}
	rebuild_dynamic_edges = (tMeshDynamicEdges)(title_base + RVA_REBUILD_DYNAMIC_EDGES);
	flush_dynamic_edges = (tMeshDynamicEdges)(title_base + RVA_FLUSH_DYNAMIC_EDGES);
	pylon_add_address = title_base + RVA_PYLON_ADD_TO_OCTREE;
	navigation_point_add_address = title_base + RVA_NAVIGATION_POINT_ADD_TO_OCTREE;
	pylon_remove_address = title_base + RVA_PYLON_REMOVE_FROM_OCTREE;
	HookObstacleShape(title_base);
	return true;
}

int Resolve()
{
	if (!pylon_post_load) {
		return -1;
	}
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &obj.Pylon, "Engine.Pylon" },
		{ &obj.PylonDefault, "Engine.Default__Pylon" },
		{ &obj.HandleDefault, "Engine.Default__NavigationHandle" },
		{ &obj.bStatic, "Engine.Actor.bStatic" },
		{ &obj.bNoDelete, "Engine.Actor.bNoDelete" },
		{ &obj.Spawn, "Engine.Actor.Spawn" },
		{ &obj.GetPylonFromPos, "Engine.NavigationHandle.GetPylonFromPos" },
		{ &obj.GetMapName, "Engine.WorldInfo.GetMapName" },
	};
	for (auto& ref : objectRefs) {
		*ref.Target = UE3::FindObject(ref.Path);
		if (!*ref.Target) {
			NAVMESH_LOG(LOG_LEVEL_ERROR, "%s not found.", ref.Path);
			return -1;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &off.Actor_WorldInfo, "Engine.Actor.WorldInfo" },
		{ &off.WorldInfo_PylonList, "Engine.WorldInfo.PylonList" },
		{ &off.Pylon_NextPylon, "Engine.Pylon.NextPylon" },
		{ &off.Pylon_ExpansionRadius, "Engine.Pylon.ExpansionRadius" },
		{ &off.Pylon_NavMeshPtr, "Engine.Pylon.NavMeshPtr" },
		{ &off.Pylon_ObstacleMesh, "Engine.Pylon.ObstacleMesh" },
		{ &off.Spawn_SpawnClass, "Engine.Actor.Spawn.SpawnClass" },
		{ &off.Spawn_SpawnLocation, "Engine.Actor.Spawn.SpawnLocation" },
		{ &off.Spawn_bNoCollisionFail, "Engine.Actor.Spawn.bNoCollisionFail" },
		{ &off.Spawn_ReturnValue, "Engine.Actor.Spawn.ReturnValue" },
		{ &off.GetPylonFromPos_Position, "Engine.NavigationHandle.GetPylonFromPos.Position" },
		{ &off.GetPylonFromPos_ReturnValue, "Engine.NavigationHandle.GetPylonFromPos.ReturnValue" },
		{ &off.GetMapName_ReturnValue, "Engine.WorldInfo.GetMapName.ReturnValue" },
	};
	for (auto& ref : offsetRefs) {
		*ref.Target = UE3::FindPropertyOffset(ref.Path);
		if (*ref.Target < 0) {
			NAVMESH_LOG(LOG_LEVEL_ERROR, "%s not found.", ref.Path);
			return -1;
		}
	}
	if (off.WorldInfo_PylonList != WORLDINFO_PYLON_LIST || off.Pylon_NavMeshPtr != PYLON_NAVMESH_PTR) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "PylonList is at 0x%x and NavMeshPtr at 0x%x, the engine reads 0x%x and 0x%x."
			, off.WorldInfo_PylonList, off.Pylon_NavMeshPtr, WORLDINFO_PYLON_LIST, PYLON_NAVMESH_PTR);
		return -1;
	}
	obj.GoalAt = UE3::FindObject("Engine.NavMeshGoal_At");
	obj.GoalAtDefault = UE3::FindObject("Engine.Default__NavMeshGoal_At");
	off.Evaluator_MaxPathVisits = UE3::FindPropertyOffset("Engine.NavMeshPathGoalEvaluator.MaxPathVisits");
	if (obj.GoalAt && obj.GoalAtDefault && off.Evaluator_MaxPathVisits >= 0) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "A path search visits up to %d polys, the engine's default is %d."
			, MAX_PATH_VISITS, Field<int32_t>(obj.GoalAtDefault, off.Evaluator_MaxPathVisits));
		Field<int32_t>(obj.GoalAtDefault, off.Evaluator_MaxPathVisits) = MAX_PATH_VISITS;
	}
	else {
		obj.GoalAt = 0;
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "NavMeshGoal_At.MaxPathVisits was not found, long paths may fail on large meshes.");
	}
	setup.ChooseNavMeshGoal = UE3::FindObject("FoxGame.FoxAIControllerInterface.ChooseNavMeshGoal");
	setup.Handle_bUltraVerbosePathDebugging = UE3::FindObject("Engine.NavigationHandle.bUltraVerbosePathDebugging");
	setup.Handle_bDebugConstraintsAndGoalEvals = UE3::FindObject("Engine.NavigationHandle.bDebugConstraintsAndGoalEvals");
	setup.Controller_NavigationHandle = UE3::FindPropertyOffset("Engine.Controller.NavigationHandle");
	setup.Handle_PathGoalList = UE3::FindPropertyOffset("Engine.NavigationHandle.PathGoalList");
	setup.Handle_PathConstraintList = UE3::FindPropertyOffset("Engine.NavigationHandle.PathConstraintList");
	setup.Evaluator_NextEvaluator = UE3::FindPropertyOffset("Engine.NavMeshPathGoalEvaluator.NextEvaluator");
	setup.Constraint_NextConstraint = UE3::FindPropertyOffset("Engine.NavMeshPathConstraint.NextConstraint");
	if (!setup.ChooseNavMeshGoal || setup.Controller_NavigationHandle < 0 || setup.Handle_PathGoalList < 0 || setup.Handle_PathConstraintList < 0
		|| setup.Evaluator_NextEvaluator < 0 || setup.Constraint_NextConstraint < 0) {
		setup.ChooseNavMeshGoal = 0;
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The AI's path setup was not found, its path goals keep their own visit limit.");
	}
	char traces[16] = {};
	path_traces_left = GetEnvironmentVariableA("BLTD_PATH_DEBUG", traces, sizeof(traces)) > 0 ? atoi(traces) : 0;
	const char* comma = strchr(traces, ',');
	path_trace_distance = path_traces_left > 0 && comma ? (float)atof(comma + 1) : PATH_TRACE_DISTANCE;
	struct { UObject** Target; const char* Path; } coverObjectRefs[] = {
		{ &obj.CoverLink, "Engine.CoverLink" },
		{ &obj.CoverLinkDefault, "Engine.Default__CoverLink" },
		{ &obj.CoverLink_bDisabled, "Engine.CoverLink.bDisabled" },
		{ &obj.Slot_bLeanLeft, "Engine.CoverLink.CoverSlot.bLeanLeft" },
		{ &obj.Slot_bLeanRight, "Engine.CoverLink.CoverSlot.bLeanRight" },
		{ &obj.Slot_bCanPopUp, "Engine.CoverLink.CoverSlot.bCanPopUp" },
		{ &obj.Slot_bEnabled, "Engine.CoverLink.CoverSlot.bEnabled" },
	};
	struct { int32_t* Target; const char* Path; } coverOffsetRefs[] = {
		{ &off.Actor_Location, "Engine.Actor.Location" },
		{ &off.Actor_Rotation, "Engine.Actor.Rotation" },
		{ &off.Spawn_SpawnRotation, "Engine.Actor.Spawn.SpawnRotation" },
		{ &off.WorldInfo_CoverList, "Engine.WorldInfo.CoverList" },
		{ &off.CoverLink_NextCoverLink, "Engine.CoverLink.NextCoverLink" },
		{ &off.CoverLink_Slots, "Engine.CoverLink.Slots" },
		{ &off.Slot_CoverType, "Engine.CoverLink.CoverSlot.CoverType" },
		{ &off.Slot_LocationOffset, "Engine.CoverLink.CoverSlot.LocationOffset" },
		{ &off.Slot_RotationOffset, "Engine.CoverLink.CoverSlot.RotationOffset" },
		{ &off.Slot_bFailedToFindSurface, "Engine.CoverLink.CoverSlot.bFailedToFindSurface" },
	};
	cover_ready = true;
	for (auto& ref : coverObjectRefs) {
		*ref.Target = UE3::FindObject(ref.Path);
		cover_ready = cover_ready && *ref.Target;
	}
	for (auto& ref : coverOffsetRefs) {
		*ref.Target = UE3::FindPropertyOffset(ref.Path);
		cover_ready = cover_ready && *ref.Target >= 0;
	}
	if (cover_ready && off.Slot_bFailedToFindSurface + (int32_t)sizeof(uint32_t) != COVER_SLOT_SIZE) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "CoverSlot.bFailedToFindSurface is at 0x%x, not 0x%x.", off.Slot_bFailedToFindSurface, COVER_SLOT_SIZE - (int32_t)sizeof(uint32_t));
		cover_ready = false;
	}
	if (!cover_ready) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The CoverLink properties were not all found, no cover is added.");
	}
	obj.NavMeshObstacle = UE3::FindObject("Engine.NavMeshObstacle");
	obj.ModeMesh = UE3::FindObject("FoxGame.FoxGameTypeDynamicSMActor");
	obj.bBlockActors = UE3::FindObject("Engine.Actor.bBlockActors");
	obj.bCollideActors = UE3::FindObject("Engine.Actor.bCollideActors");
	obj.bDeleteMe = UE3::FindObject("Engine.Actor.bDeleteMe");
	obj.UnRegisterObstacle = UE3::FindObject("Engine.NavMeshObstacle.UnRegisterObstacle");
	off.Actor_CollisionComponent = UE3::FindPropertyOffset("Engine.Actor.CollisionComponent");
	off.WorldInfo_PawnList = UE3::FindPropertyOffset("Engine.WorldInfo.PawnList");
	off.Pawn_NextPawn = UE3::FindPropertyOffset("Engine.Pawn.NextPawn");
	turrets_ready = off.Actor_CollisionComponent >= 0 && off.WorldInfo_PawnList >= 0 && off.Pawn_NextPawn >= 0;
	off.MeshActor_StaticMeshComponent = UE3::FindPropertyOffset("Engine.DynamicSMActor.StaticMeshComponent");
	off.Component_Bounds = UE3::FindPropertyOffset("Engine.PrimitiveComponent.Bounds");
	if (register_obstacle && (!obj.NavMeshObstacle || !obj.ModeMesh || !obj.bBlockActors || !obj.bCollideActors || !obj.bDeleteMe || !obj.UnRegisterObstacle
		|| off.MeshActor_StaticMeshComponent < 0 || off.Component_Bounds < 0 || off.Actor_Rotation < 0)) {
		register_obstacle = 0;
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The classes and properties of the mode meshes were not all found, the meshes of a game mode are not cut.");
	}
	obj.PrimitiveComponent = UE3::FindObject("Engine.PrimitiveComponent");
	obj.SetComponentHidden = UE3::FindObject("Engine.PrimitiveComponent.SetHidden");
	off.Actor_Components = UE3::FindPropertyOffset("Engine.Actor.Components");
	off.SetComponentHidden_NewHidden = UE3::FindPropertyOffset("Engine.PrimitiveComponent.SetHidden.NewHidden");
	if (register_obstacle && (!obj.PrimitiveComponent || !obj.SetComponentHidden || off.Actor_Components < 0 || off.SetComponentHidden_NewHidden < 0)) {
		obj.SetComponentHidden = 0;
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "PrimitiveComponent.SetHidden was not found, the cuts show the engine's box around crates and turrets.");
	}
	obj.StaticMeshComponent = UE3::FindObject("Engine.StaticMeshComponent");
	obj.MeshCollection = UE3::FindObject("Engine.StaticMeshCollectionActor");
	obj.ReattachComponent = UE3::FindObject("Engine.Actor.ReattachComponent");
	off.Component_Owner = UE3::FindPropertyOffset("Engine.ActorComponent.Owner");
	off.Component_LocalToWorld = UE3::FindPropertyOffset("Engine.PrimitiveComponent.LocalToWorld");
	off.Component_Translation = UE3::FindPropertyOffset("Engine.PrimitiveComponent.Translation");
	off.Component_Rotation = UE3::FindPropertyOffset("Engine.PrimitiveComponent.Rotation");
	off.StaticMeshComponent_StaticMesh = UE3::FindPropertyOffset("Engine.StaticMeshComponent.StaticMesh");
	off.ReattachComponent_ComponentToReattach = UE3::FindPropertyOffset("Engine.Actor.ReattachComponent.ComponentToReattach");
	if (!obj.StaticMeshComponent || !obj.MeshCollection || !obj.ReattachComponent || off.Component_Owner < 0 || off.Component_LocalToWorld < 0
		|| off.Component_Translation < 0 || off.Component_Rotation < 0
		|| off.StaticMeshComponent_StaticMesh < 0 || off.ReattachComponent_ComponentToReattach < 0) {
		obj.ReattachComponent = 0;
		NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The static mesh component properties were not all found, the stairs keep their lips.");
	}
	ready = true;
	return 1;
}

static void RaisePathVisits()
{
	if (!obj.GoalAt) {
		return;
	}
	int32_t count = UE3::ObjectCount();
	for (int32_t i = 0; i < count; i++) {
		UObject* object = UE3::ObjectAt(i);
		if (object && UE3::IsA(object, obj.GoalAt) && Field<int32_t>(object, off.Evaluator_MaxPathVisits) < MAX_PATH_VISITS) {
			Field<int32_t>(object, off.Evaluator_MaxPathVisits) = MAX_PATH_VISITS;
		}
	}
}

UObject* PathSetupFunction()
{
	return setup.ChooseNavMeshGoal;
}

static void AppendClassName(char* text, size_t size, UObject* object, int32_t visits)
{
	size_t length = strlen(text);
	const char* name = UE3::ObjectName(UE3::ObjectClass(object));
	if (visits >= 0) {
		_snprintf_s(text + length, size - length, _TRUNCATE, "%s%s(%d)", length ? ", " : "", name, visits);
	}
	else {
		_snprintf_s(text + length, size - length, _TRUNCATE, "%s%s", length ? ", " : "", name);
	}
}

void OnPathSetup(UObject* controller, const FVector& from, const FVector& to)
{
	if (!setup.ChooseNavMeshGoal) {
		return;
	}
	if (traced_handle) {
		UE3::SetBoolProperty(traced_handle, setup.Handle_bUltraVerbosePathDebugging, false);
		UE3::SetBoolProperty(traced_handle, setup.Handle_bDebugConstraintsAndGoalEvals, false);
		traced_handle = 0;
	}
	UObject* handle = Field<UObject*>(controller, setup.Controller_NavigationHandle);
	if (!handle) {
		return;
	}
	char goals[256] = "";
	char constraints[256] = "";
	int count = 0;
	for (UObject* goal = Field<UObject*>(handle, setup.Handle_PathGoalList); goal && count < LIST_LOG_LIMIT; goal = Field<UObject*>(goal, setup.Evaluator_NextEvaluator), count++) {
		int32_t visits = -1;
		if (off.Evaluator_MaxPathVisits >= 0) {
			visits = Field<int32_t>(goal, off.Evaluator_MaxPathVisits);
			if (visits < MAX_PATH_VISITS) {
				Field<int32_t>(goal, off.Evaluator_MaxPathVisits) = MAX_PATH_VISITS;
			}
		}
		AppendClassName(goals, sizeof(goals), goal, visits);
	}
	count = 0;
	for (UObject* constraint = Field<UObject*>(handle, setup.Handle_PathConstraintList); constraint && count < LIST_LOG_LIMIT; constraint = Field<UObject*>(constraint, setup.Constraint_NextConstraint), count++) {
		AppendClassName(constraints, sizeof(constraints), constraint, -1);
	}
	if (setups_logged < SETUP_LOG_LIMIT) {
		setups_logged++;
		int32_t links = 0;
		int32_t narrow = 0;
		UObject* world = Field<UObject*>(controller, off.Actor_WorldInfo);
		if (world) {
			CountLinks(world, links, narrow, setups_logged == 1);
			if (setups_logged == 1 && GetEnvironmentVariableA("BLTD_LINK_DUMP", 0, 0) > 0) {
				DumpLinks(world);
			}
		}
		NAVMESH_LOG(LOG_LEVEL_INFO, "Path setup of 0x%08x: goals %s, constraints %s; %d dynamic links, %d narrower than a bot."
			, controller, goals, constraints, links, narrow);
	}
	float dx = to.X - from.X;
	float dy = to.Y - from.Y;
	if (path_traces_left > 0 && setup.Handle_bUltraVerbosePathDebugging && setup.Handle_bDebugConstraintsAndGoalEvals && sqrtf(dx * dx + dy * dy) >= path_trace_distance) {
		path_traces_left--;
		traced_handle = handle;
		UE3::SetBoolProperty(handle, setup.Handle_bUltraVerbosePathDebugging, true);
		UE3::SetBoolProperty(handle, setup.Handle_bDebugConstraintsAndGoalEvals, true);
		NAVMESH_LOG(LOG_LEVEL_INFO, "The game's log traces the path search of 0x%08x from (%.0f %.0f %.0f) to (%.0f %.0f %.0f)."
			, controller, from.X, from.Y, from.Z, to.X, to.Y, to.Z);
	}
}

static std::string MapName(UObject* world)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.GetMapName) + 16, 0);
	UE3::ProcessNativeEvent(world, obj.GetMapName, parms.data());
	UE3::FString& value = Field<UE3::FString>(parms.data(), off.GetMapName_ReturnValue);
	std::string name;
	if (value.Data) {
		for (const wchar_t* c = value.Data; *c; c++) {
			name.push_back((char)*c);
		}
		UE3::FreeArray(&value);
	}
	return name;
}

UObject* PylonAt(const FVector& location)
{
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.GetPylonFromPos) + 16, 0);
	Field<FVector>(parms.data(), off.GetPylonFromPos_Position) = location;
	UE3::ProcessNativeEvent(obj.HandleDefault, obj.GetPylonFromPos, parms.data());
	return Field<UObject*>(parms.data(), off.GetPylonFromPos_ReturnValue);
}

// After BeginPlay the engine refuses bStatic and bNoDelete classes, so the class default clears both for the spawn.
static UObject* SpawnNavigationPoint(UObject* game, UObject* cls, UObject* defaults, const FVector& location, const UE3::FRotator* rotation)
{
	UE3::SetBoolProperty(defaults, obj.bStatic, false);
	UE3::SetBoolProperty(defaults, obj.bNoDelete, false);
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.Spawn) + 16, 0);
	Field<UObject*>(parms.data(), off.Spawn_SpawnClass) = cls;
	Field<FVector>(parms.data(), off.Spawn_SpawnLocation) = location;
	if (rotation) {
		Field<UE3::FRotator>(parms.data(), off.Spawn_SpawnRotation) = *rotation;
	}
	Field<uint32_t>(parms.data(), off.Spawn_bNoCollisionFail) = 1;
	UE3::ProcessNativeEvent(game, obj.Spawn, parms.data());
	UE3::SetBoolProperty(defaults, obj.bStatic, true);
	UE3::SetBoolProperty(defaults, obj.bNoDelete, true);
	UObject* actor = Field<UObject*>(parms.data(), off.Spawn_ReturnValue);
	if (actor) {
		UE3::SetBoolProperty(actor, obj.bStatic, true);
		UE3::SetBoolProperty(actor, obj.bNoDelete, true);
	}
	return actor;
}

UObject* SpawnPylonAt(UObject* game, const FVector& location, float radius)
{
	UObject* pylon = SpawnNavigationPoint(game, obj.Pylon, obj.PylonDefault, location, 0);
	if (!pylon) {
		return 0;
	}
	Field<float>(pylon, off.Pylon_ExpansionRadius) = radius;

	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	int count = 0;
	for (UObject* listed = Field<UObject*>(world, off.WorldInfo_PylonList); listed && count < LIST_LIMIT; listed = Field<UObject*>(listed, off.Pylon_NextPylon), count++) {
		if (listed == pylon) {
			return pylon;
		}
	}
	Field<UObject*>(pylon, off.Pylon_NextPylon) = Field<UObject*>(world, off.WorldInfo_PylonList);
	Field<UObject*>(world, off.WorldInfo_PylonList) = pylon;
	return pylon;
}

static void ReleaseObstacles()
{
	if (obstacles.empty()) {
		return;
	}
	uint8_t parms[16] = {};
	for (const ModeObstacle& cut : obstacles) {
		if (UE3::IsValidObject(cut.Obstacle)) {
			UE3::ProcessNativeEvent(cut.Obstacle, obj.UnRegisterObstacle, parms);
		}
	}
	NAVMESH_LOG(LOG_LEVEL_INFO, "Took %u cuts off the navmesh.", (uint32_t)obstacles.size());
	obstacles.clear();
	turrets_seen.clear();
}

void OnLevelUnload()
{
	ReleaseObstacles();
}

bool InModeCut(const FVector& point, float inset)
{
	for (const ModeObstacle& cut : obstacles) {
		if (point.Z < cut.Floor - OBSTACLE_HEADROOM || point.Z > cut.Floor + OBSTACLE_SPOT_HEIGHT) {
			continue;
		}
		float dx = point.X - cut.Centre.X;
		float dy = point.Y - cut.Centre.Y;
		float x = dx * cosf(cut.Yaw) + dy * sinf(cut.Yaw);
		float y = dy * cosf(cut.Yaw) - dx * sinf(cut.Yaw);
		if (fabsf(x) <= cut.HalfX - inset && fabsf(y) <= cut.HalfY - inset) {
			return true;
		}
	}
	return false;
}

void DetachPylons(UObject* world, const std::vector<UObject*>& pylons)
{
	ReleaseObstacles();
	for (UObject* pylon : pylons) {
		void* mesh = Field<void*>(pylon, PYLON_NAVMESH_PTR);
		if (mesh) {
			flush_dynamic_edges(mesh, 0);
		}
		if ((uintptr_t)(*(void***)pylon)[PYLON_REMOVE_FROM_OCTREE_SLOT] == pylon_remove_address) {
			((tPylonOctree)pylon_remove_address)(pylon, 0);
		}
		Field<void*>(pylon, PYLON_NAVMESH_PTR) = 0;
		Field<void*>(pylon, off.Pylon_ObstacleMesh) = 0;
		UObject** link = &Field<UObject*>(world, off.WorldInfo_PylonList);
		for (int count = 0; *link && count < LIST_LIMIT; count++) {
			if (*link == pylon) {
				*link = Field<UObject*>(pylon, off.Pylon_NextPylon);
				break;
			}
			link = &Field<UObject*>(*link, off.Pylon_NextPylon);
		}
	}
}

void AddToOctree(UObject* pylon)
{
	if ((uintptr_t)(*(void***)pylon)[PYLON_ADD_TO_OCTREE_SLOT] == pylon_add_address) {
		((tPylonOctree)pylon_add_address)(pylon, 0);
	}
}

int32_t DynamicEdgeCount(UObject* pylon)
{
	uint8_t* mesh = pylon ? Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR) : 0;
	return mesh ? Field<int32_t>(mesh, MESH_DYNAMIC_EDGE_SLOTS) - Field<int32_t>(mesh, MESH_DYNAMIC_EDGE_FREE) : 0;
}

// Leaves the Pylon unlinked when an engine assert in the linker would end the game.
bool Link(UObject* pylon)
{
	void* mesh = Field<void*>(pylon, PYLON_NAVMESH_PTR);
	if (!mesh) {
		return false;
	}
	__try {
		rebuild_dynamic_edges(mesh, 0);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static int CollectDynamicEdges(uint8_t* mesh, uint8_t** edges, int capacity)
{
	int count = 0;
	__try {
		uint8_t* slots = *(uint8_t**)(mesh + MESH_DYNAMIC_EDGE_DATA);
		int32_t total = *(int32_t*)(mesh + MESH_DYNAMIC_EDGE_SLOTS);
		const uint32_t* heapBits = *(const uint32_t**)(mesh + MESH_DYNAMIC_EDGE_BITS_HEAP);
		const uint32_t* bits = heapBits ? heapBits : (const uint32_t*)(mesh + MESH_DYNAMIC_EDGE_BITS);
		for (int32_t i = 0; i < total && count < capacity; i++) {
			if (!((bits[i >> 5] >> (i & 31)) & 1)) {
				continue;
			}
			uint8_t* edge = *(uint8_t**)(slots + i * DYNAMIC_EDGE_SLOT_SIZE + DYNAMIC_EDGE_SLOT_EDGE);
			if ((uintptr_t)edge < 0x10000 || edge == (uint8_t*)-1) {
				continue;
			}
			if (*(uintptr_t*)edge == cross_edge_vtable && *(uint8_t**)(edge + EDGE_MESH) == mesh) {
				edges[count++] = edge;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
	return count;
}

int32_t WidenDynamicEdges(UObject* world, UObject* patch)
{
	static uint8_t* edges[DYNAMIC_EDGE_LIMIT];
	int32_t wide = 0;
	int listed = 0;
	for (UObject* pylon = Field<UObject*>(world, off.WorldInfo_PylonList); pylon && listed < LIST_LIMIT; pylon = Field<UObject*>(pylon, off.Pylon_NextPylon), listed++) {
		uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
		int32_t expected = DynamicEdgeCount(pylon);
		if (!mesh || expected <= 0) {
			continue;
		}
		int count = CollectDynamicEdges(mesh, edges, DYNAMIC_EDGE_LIMIT);
		if (count != expected) {
			NAVMESH_LOG(LOG_LEVEL_WARN, "%s has %d dynamic links but %d were found; their widths stay as the engine set them."
				, UE3::ObjectName(pylon), expected, count);
			continue;
		}
		const uint8_t* verts = Field<uint8_t*>(mesh, NavCache::MESH_VERTS);
		int32_t vertCount = Field<int32_t>(mesh, NavCache::MESH_VERTS + 4);
		std::vector<FVector> ends(count * 2);
		bool readable = verts != 0;
		for (int i = 0; i < count && readable; i++) {
			const uint16_t* indices = Field<uint16_t*>(edges[i], EDGE_VERTS);
			int32_t indexCount = Field<int32_t>(edges[i], EDGE_VERTS + 4);
			readable = indices && indexCount >= 2 && indices[0] < vertCount && indices[1] < vertCount;
			if (readable) {
				ends[i * 2] = *(const FVector*)(verts + indices[0] * NavCache::VERT_SIZE);
				ends[i * 2 + 1] = *(const FVector*)(verts + indices[1] * NavCache::VERT_SIZE);
			}
		}
		if (!readable) {
			NAVMESH_LOG(LOG_LEVEL_WARN, "A dynamic link of %s has no readable ends; the widths stay as the engine set them.", UE3::ObjectName(pylon));
			continue;
		}
		std::vector<int> parent(count);
		for (int i = 0; i < count; i++) {
			parent[i] = i;
		}
		auto root = [&](int i) {
			while (parent[i] != i) {
				parent[i] = parent[parent[i]];
				i = parent[i];
			}
			return i;
		};
		for (int i = 0; i < count; i++) {
			for (int j = i + 1; j < count; j++) {
				bool touch = false;
				for (int a = 0; a < 2 && !touch; a++) {
					for (int b = 0; b < 2 && !touch; b++) {
						const FVector& p = ends[i * 2 + a];
						const FVector& q = ends[j * 2 + b];
						touch = fabsf(p.X - q.X) <= EDGE_JOIN_DISTANCE && fabsf(p.Y - q.Y) <= EDGE_JOIN_DISTANCE && fabsf(p.Z - q.Z) <= EDGE_JOIN_HEIGHT;
					}
				}
				if (touch) {
					parent[root(i)] = root(j);
				}
			}
		}
		std::vector<float> run(count, 0.0f);
		for (int i = 0; i < count; i++) {
			run[root(i)] += Field<float>(edges[i], EDGE_LENGTH);
		}
		for (int i = 0; i < count; i++) {
			float& effective = Field<float>(edges[i], EDGE_EFFECTIVE);
			effective = fmaxf(effective, fmaxf(run[root(i)], SEAM_LINK_WIDTH));
			if (pylon == patch && effective >= MIN_EDGE_WIDTH) {
				wide++;
			}
		}
	}
	return wide;
}

static bool EdgeEnds(uint8_t* mesh, uint8_t* edge, FVector& a, FVector& b)
{
	const uint8_t* verts = Field<uint8_t*>(mesh, NavCache::MESH_VERTS);
	int32_t vertCount = Field<int32_t>(mesh, NavCache::MESH_VERTS + 4);
	const uint16_t* indices = Field<uint16_t*>(edge, EDGE_VERTS);
	int32_t indexCount = Field<int32_t>(edge, EDGE_VERTS + 4);
	if (!verts || !indices || indexCount < 2 || indices[0] >= vertCount || indices[1] >= vertCount) {
		return false;
	}
	a = *(const FVector*)(verts + indices[0] * NavCache::VERT_SIZE);
	b = *(const FVector*)(verts + indices[1] * NavCache::VERT_SIZE);
	return true;
}

static void CountLinks(UObject* world, int32_t& total, int32_t& narrow, bool report)
{
	static uint8_t* edges[DYNAMIC_EDGE_LIMIT];
	total = 0;
	narrow = 0;
	int listed = 0;
	int reported = 0;
	for (UObject* pylon = Field<UObject*>(world, off.WorldInfo_PylonList); pylon && listed < LIST_LIMIT; pylon = Field<UObject*>(pylon, off.Pylon_NextPylon), listed++) {
		uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
		if (!mesh || DynamicEdgeCount(pylon) <= 0) {
			continue;
		}
		int count = CollectDynamicEdges(mesh, edges, DYNAMIC_EDGE_LIMIT);
		for (int i = 0; i < count; i++) {
			total++;
			if (Field<float>(edges[i], EDGE_EFFECTIVE) >= MIN_EDGE_WIDTH) {
				continue;
			}
			narrow++;
			FVector a;
			FVector b;
			if (!report || reported >= NARROW_LINK_SAMPLES || !EdgeEnds(mesh, edges[i], a, b)) {
				continue;
			}
			reported++;
			float nearestPlane = 1.0e9f;
			float nearestHeight = 0.0f;
			for (int j = 0; j < count; j++) {
				FVector c;
				FVector d;
				if (j == i || !EdgeEnds(mesh, edges[j], c, d)) {
					continue;
				}
				const FVector* mine[2] = { &a, &b };
				const FVector* other[2] = { &c, &d };
				for (int m = 0; m < 2; m++) {
					for (int o = 0; o < 2; o++) {
						float plane = sqrtf((mine[m]->X - other[o]->X) * (mine[m]->X - other[o]->X) + (mine[m]->Y - other[o]->Y) * (mine[m]->Y - other[o]->Y));
						if (plane < nearestPlane) {
							nearestPlane = plane;
							nearestHeight = fabsf(mine[m]->Z - other[o]->Z);
						}
					}
				}
			}
			NAVMESH_LOG(LOG_LEVEL_INFO, "Narrow link of %s: %.0f wide (%.0f own) from (%.0f %.0f %.0f) to (%.0f %.0f %.0f); the nearest other end is %.1f away in the plane and %.1f in height."
				, UE3::ObjectName(pylon), Field<float>(edges[i], EDGE_EFFECTIVE), Field<float>(edges[i], EDGE_LENGTH), a.X, a.Y, a.Z, b.X, b.Y, b.Z, nearestPlane, nearestHeight);
		}
	}
}

static void DumpLinks(UObject* world)
{
	static uint8_t* edges[DYNAMIC_EDGE_LIMIT];
	int listed = 0;
	int32_t dumped = 0;
	for (UObject* pylon = Field<UObject*>(world, off.WorldInfo_PylonList); pylon && listed < LIST_LIMIT; pylon = Field<UObject*>(pylon, off.Pylon_NextPylon), listed++) {
		uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
		if (!mesh || DynamicEdgeCount(pylon) <= 0) {
			continue;
		}
		int count = CollectDynamicEdges(mesh, edges, DYNAMIC_EDGE_LIMIT);
		for (int i = 0; i < count; i++) {
			FVector a;
			FVector b;
			if (!EdgeEnds(mesh, edges[i], a, b)) {
				continue;
			}
			dumped++;
			NAVMESH_LOG(LOG_LEVEL_INFO, "Link dump: %s (%.1f %.1f %.1f) (%.1f %.1f %.1f) %.1f wide", UE3::ObjectName(pylon), a.X, a.Y, a.Z, b.X, b.Y, b.Z
				, Field<float>(edges[i], EDGE_EFFECTIVE));
		}
	}
	NAVMESH_LOG(LOG_LEVEL_INFO, "Link dump: %d links.", dumped);
}

static std::wstring CachePath(const std::string& map)
{
	std::wstring name;
	for (char c : map) {
		name += (wchar_t)(unsigned char)c;
	}
	name += L".navcache";
	return Platform::FilePath(Platform::FILE_NAVCACHE, name.c_str(), false);
}

struct Reader {
	const uint8_t* Data;
	size_t Size;
	size_t Offset;
	bool Failed;

	bool Get(void* out, size_t size)
	{
		if (Failed || Offset + size > Size) {
			Failed = true;
			return false;
		}
		memcpy(out, Data + Offset, size);
		Offset += size;
		return true;
	}
	const uint8_t* Blob(uint32_t& size)
	{
		if (!Get(&size, sizeof(size)) || Offset + size > Size) {
			Failed = true;
			return 0;
		}
		const uint8_t* start = Data + Offset;
		Offset += size;
		return start;
	}
};

struct CacheEntry {
	uint32_t Kind;
	std::string Path;
	uint32_t Index;
	FVector Location;
	float Radius;
	const uint8_t* NavMesh;
	uint32_t NavMeshSize;
	const uint8_t* ObstacleMesh;
	uint32_t ObstacleMeshSize;
};

static int32_t CompleteBorderSegments(uint8_t* mesh)
{
	int32_t& count = Field<int32_t>(mesh, MESH_BORDER_SEGMENTS + 4);
	if (count > 0) {
		return count;
	}
	__try {
		uint8_t* polys = Field<uint8_t*>(mesh, NavCache::MESH_POLYS);
		int32_t polyCount = Field<int32_t>(mesh, NavCache::MESH_POLYS + 4);
		for (int32_t i = 0; i < polyCount; i++) {
			poly_border_segments(polys + i * NavCache::POLY_SIZE, mesh + MESH_BORDER_SEGMENTS);
		}
		return count;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

bool LoadMeshes(UObject* pylon, const uint8_t* nav, uint32_t nav_size, const uint8_t* obstacle, uint32_t obstacle_size)
{
	UObject* navMesh = NavCache::LoadMesh(pylon, nav, nav_size);
	UObject* obstacleMesh = NavCache::LoadMesh(pylon, obstacle, obstacle_size);
	const char* problem = NavCache::MeshProblem(navMesh);
	problem = problem ? problem : NavCache::MeshProblem(obstacleMesh);
	if (problem) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "The navmesh of %s is not loaded: %s.", UE3::ObjectName(pylon), problem);
	}
	if (!navMesh || (obstacle_size && !obstacleMesh) || problem) {
		return false;
	}
	Field<UObject*>(pylon, PYLON_NAVMESH_PTR) = navMesh;
	Field<UObject*>(pylon, off.Pylon_ObstacleMesh) = obstacleMesh;
	pylon_post_load(pylon, 0);
	if (CompleteBorderSegments((uint8_t*)navMesh) < 0) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "The border sides of %s could not be listed; it links only toward its first neighbour.", UE3::ObjectName(pylon));
	}
	return true;
}

static uint8_t* FirstSlot(UObject* link)
{
	UE3::TArray<uint8_t>& slots = Field<UE3::TArray<uint8_t> >(link, off.CoverLink_Slots);
	return slots.Num > 0 ? slots.Data : 0;
}

static void SetSlotBit(uint8_t* slot, UObject* property, bool value)
{
	UE3::SetBoolProperty((UObject*)slot, property, value);
}

// The engine keeps raw pointers to a CoverLink, so it is disabled, not destroyed.
void DisableCover(UObject* link)
{
	if (cover_ready && link) {
		UE3::SetBoolProperty(link, obj.CoverLink_bDisabled, true);
	}
}

UObject* AddCover(UObject* game, CoverSpot& spot)
{
	spot.Link = 0;
	if (!cover_ready) {
		return 0;
	}
	UE3::FRotator rotation = { 0, spot.Yaw, 0 };
	UObject* link = SpawnNavigationPoint(game, obj.CoverLink, obj.CoverLinkDefault, spot.Location, &rotation);
	uint8_t* slot = link ? FirstSlot(link) : 0;
	// The AI finds cover only through the navigation octree.
	bool addable = link && (uintptr_t)(*(void***)link)[PYLON_ADD_TO_OCTREE_SLOT] == navigation_point_add_address;
	if (!slot || !addable) {
		DisableCover(link);
		return 0;
	}
	Field<uint8_t>(slot, off.Slot_CoverType) = spot.CoverType;
	Field<FVector>(slot, off.Slot_LocationOffset) = { 0.0f, 0.0f, 0.0f };
	Field<UE3::FRotator>(slot, off.Slot_RotationOffset) = { 0, 0, 0 };
	SetSlotBit(slot, obj.Slot_bLeanLeft, (spot.Flags & COVER_LEAN_LEFT) != 0);
	SetSlotBit(slot, obj.Slot_bLeanRight, (spot.Flags & COVER_LEAN_RIGHT) != 0);
	SetSlotBit(slot, obj.Slot_bCanPopUp, (spot.Flags & COVER_POPUP) != 0);
	SetSlotBit(slot, obj.Slot_bEnabled, true);
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	bool listed = false;
	int count = 0;
	for (UObject* listedLink = Field<UObject*>(world, off.WorldInfo_CoverList); listedLink && count < LIST_LIMIT && !listed; listedLink = Field<UObject*>(listedLink, off.CoverLink_NextCoverLink), count++) {
		listed = listedLink == link;
	}
	if (!listed) {
		Field<UObject*>(link, off.CoverLink_NextCoverLink) = Field<UObject*>(world, off.WorldInfo_CoverList);
		Field<UObject*>(world, off.WorldInfo_CoverList) = link;
	}
	((tPylonOctree)navigation_point_add_address)(link, 0);
	spot.Link = link;
	return link;
}

static std::wstring CoverPath(const std::string& map)
{
	std::wstring path = CachePath(map);
	return path.empty() ? path : path.substr(0, path.size() - wcslen(L".navcache")) + L".navcover";
}

static FVector MeshVertex(uint8_t* mesh, int32_t index)
{
	const FVector& v = *(const FVector*)(Field<uint8_t*>(mesh, NavCache::MESH_VERTS) + index * NavCache::VERT_SIZE);
	const float* m = (const float*)(mesh + MESH_LOCAL_TO_WORLD);
	return { v.X * m[0] + v.Y * m[4] + v.Z * m[8] + m[12], v.X * m[1] + v.Y * m[5] + v.Z * m[9] + m[13], v.X * m[2] + v.Y * m[6] + v.Z * m[10] + m[14] };
}

static uint8_t* PolyAt(uint8_t* mesh, const FVector& point)
{
	uint8_t* polys = Field<uint8_t*>(mesh, NavCache::MESH_POLYS);
	int32_t count = Field<int32_t>(mesh, NavCache::MESH_POLYS + 4);
	for (int32_t i = 0; i < count; i++) {
		uint8_t* poly = polys + i * NavCache::POLY_SIZE;
		const uint16_t* indices = Field<uint16_t*>(poly, NavCache::POLY_VERTS);
		int32_t n = Field<int32_t>(poly, NavCache::POLY_VERTS + 4);
		if (n < 3 || n > 32) {
			continue;
		}
		FVector pts[32];
		float low = 1e9f;
		float high = -1e9f;
		for (int32_t k = 0; k < n; k++) {
			pts[k] = MeshVertex(mesh, indices[k]);
			low = pts[k].Z < low ? pts[k].Z : low;
			high = pts[k].Z > high ? pts[k].Z : high;
		}
		if (point.Z < low - LINK_FLOOR_WINDOW || point.Z > high + LINK_FLOOR_WINDOW) {
			continue;
		}
		bool inside = false;
		for (int32_t k = 0, j = n - 1; k < n; j = k++) {
			if ((pts[k].Y > point.Y) != (pts[j].Y > point.Y) && point.X < pts[k].X + (point.Y - pts[k].Y) * (pts[j].X - pts[k].X) / (pts[j].Y - pts[k].Y)) {
				inside = !inside;
			}
		}
		if (inside) {
			return poly;
		}
	}
	return 0;
}

static uint8_t* PolyUnder(UObject* world, const FVector& point, uint8_t** mesh_out)
{
	int listed = 0;
	__try {
		for (UObject* pylon = Field<UObject*>(world, off.WorldInfo_PylonList); pylon && listed < LIST_LIMIT; pylon = Field<UObject*>(pylon, off.Pylon_NextPylon), listed++) {
			uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
			uint8_t* poly = mesh ? PolyAt(mesh, point) : 0;
			if (poly) {
				*mesh_out = mesh;
				return poly;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
	return 0;
}

std::vector<JumpLink>& Links()
{
	return links;
}

static bool FloorUnder(UObject* world, const FVector& point, float& floor)
{
	float best = LINK_FLOOR_WINDOW;
	bool found = false;
	int listed = 0;
	__try {
		for (UObject* pylon = Field<UObject*>(world, off.WorldInfo_PylonList); pylon && listed < LIST_LIMIT; pylon = Field<UObject*>(pylon, off.Pylon_NextPylon), listed++) {
			uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
			uint8_t* polys = mesh ? Field<uint8_t*>(mesh, NavCache::MESH_POLYS) : 0;
			int32_t count = mesh ? Field<int32_t>(mesh, NavCache::MESH_POLYS + 4) : 0;
			for (int32_t i = 0; i < count; i++) {
				uint8_t* poly = polys + i * NavCache::POLY_SIZE;
				const uint16_t* indices = Field<uint16_t*>(poly, NavCache::POLY_VERTS);
				int32_t n = Field<int32_t>(poly, NavCache::POLY_VERTS + 4);
				if (n < 3 || n > 32) {
					continue;
				}
				FVector pts[32];
				float height = 0.0f;
				for (int32_t k = 0; k < n; k++) {
					pts[k] = MeshVertex(mesh, indices[k]);
					height += pts[k].Z / n;
				}
				if (fabsf(height - point.Z) >= best) {
					continue;
				}
				bool inside = false;
				for (int32_t k = 0, j = n - 1; k < n; j = k++) {
					if ((pts[k].Y > point.Y) != (pts[j].Y > point.Y) && point.X < pts[k].X + (point.Y - pts[k].Y) * (pts[j].X - pts[k].X) / (pts[j].Y - pts[k].Y)) {
						inside = !inside;
					}
				}
				if (inside) {
					best = fabsf(height - point.Z);
					floor = height;
					found = true;
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	return found;
}

static bool PolyOnFloor(uint8_t* poly, float floor)
{
	uint8_t* mesh = Field<uint8_t*>(poly, 0);
	const uint16_t* indices = Field<uint16_t*>(poly, NavCache::POLY_VERTS);
	int32_t n = Field<int32_t>(poly, NavCache::POLY_VERTS + 4);
	if (!mesh || n < 3) {
		return false;
	}
	float low = 1e9f;
	float high = -1e9f;
	for (int32_t k = 0; k < n; k++) {
		float z = MeshVertex(mesh, indices[k]).Z;
		low = z < low ? z : low;
		high = z > high ? z : high;
	}
	return low <= floor + OBSTACLE_FLOOR_WINDOW && high >= floor - OBSTACLE_FLOOR_WINDOW;
}

static float FitBox(const FVector& extent, float yaw, float& half_x, float& half_y)
{
	float c = fabsf(cosf(yaw));
	float s = fabsf(sinf(yaw));
	float det = c * c - s * s;
	if (fabsf(det) >= 0.2f) {
		half_x = (extent.X * c - extent.Y * s) / det;
		half_y = (extent.Y * c - extent.X * s) / det;
		if (half_x > 1.0f && half_y > 1.0f) {
			return yaw;
		}
	}
	half_x = extent.X;
	half_y = extent.Y;
	return 0.0f;
}

static bool IsTopLevelPoly(uint8_t* poly)
{
	uint8_t* mesh = Field<uint8_t*>(poly, 0);
	UObject* pylon = mesh ? Field<UObject*>(mesh, UE3::Offset::ObjectOuter) : 0;
	return pylon && UE3::IsA(pylon, obj.Pylon) && Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR) == mesh;
}

static void SplitCutMeshes(const std::vector<void*>& meshes)
{
	for (void* mesh : meshes) {
		if (update_obstacles && mesh) {
			update_obstacles(mesh, 0);
		}
	}
}

static void HideComponents(UObject* actor)
{
	if (!obj.SetComponentHidden) {
		return;
	}
	const PtrArray& components = Field<PtrArray>(actor, off.Actor_Components);
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.SetComponentHidden) + 16, 0);
	for (int32_t k = 0; k < components.Num; k++) {
		UObject* component = ((UObject**)components.Data)[k];
		if (component && UE3::IsA(component, obj.PrimitiveComponent)) {
			std::fill(parms.begin(), parms.end(), (uint8_t)0);
			Field<uint32_t>(parms.data(), off.SetComponentHidden_NewHidden) = 1;
			UE3::ProcessNativeEvent(component, obj.SetComponentHidden, parms.data());
		}
	}
}

static bool CutAround(UObject* game, UObject* world, UObject* actor, UObject* component, bool turned, float margin, const std::vector<FVector>& objectives, std::vector<void*>& meshes, int32_t& polys_cut, const char*& reason)
{
	reason = 0;
	FVector centre = Field<FVector>(component, off.Component_Bounds);
	FVector extent = Field<FVector>(component, off.Component_Bounds + sizeof(FVector));
	float halfX = extent.X;
	float halfY = extent.Y;
	float yaw = turned ? FitBox(extent, Field<UE3::FRotator>(actor, off.Actor_Rotation).Yaw * UNREAL_YAW_TO_RADIANS, halfX, halfY) : 0.0f;
	float bottom = centre.Z - extent.Z;
	float floor = 0.0f;
	bool onMesh = false;
	const float samples[5][2] = { { 0.0f, 0.0f }, { 0.5f, 0.5f }, { -0.5f, 0.5f }, { -0.5f, -0.5f }, { 0.5f, -0.5f } };
	for (const auto& at : samples) {
		FVector point = { centre.X + at[0] * extent.X, centre.Y + at[1] * extent.Y, bottom };
		if (FloorUnder(world, point, floor)) {
			onMesh = true;
			break;
		}
	}
	if (!onMesh) {
		reason = "no navmesh under it";
	}
	else if (bottom - floor > OBSTACLE_HEADROOM) {
		reason = "room to walk under it";
	}
	else if (centre.Z + extent.Z - floor < OBSTACLE_STEP) {
		reason = "low enough to step over";
	}
	else if (2.0f * halfX > OBSTACLE_SIZE_LIMIT || 2.0f * halfY > OBSTACLE_SIZE_LIMIT) {
		reason = "too large for a box";
	}
	if (reason) {
		return false;
	}
	halfX += margin;
	halfY += margin;
	ModeObstacle cut = {};
	cut.Mesh = actor;
	cut.Centre = centre;
	cut.HalfX = halfX;
	cut.HalfY = halfY;
	cut.Yaw = yaw;
	cut.Floor = floor;
	const float corners[4][2] = { { 1.0f, 1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
	for (int k = 0; k < 4; k++) {
		float x = corners[k][0] * halfX;
		float y = corners[k][1] * halfY;
		cut.Corners[k] = { centre.X + x * cosf(yaw) - y * sinf(yaw), centre.Y + x * sinf(yaw) + y * cosf(yaw), floor + OBSTACLE_PLANE_HEIGHT };
	}
	for (const FVector& at : objectives) {
		float dx = at.X - centre.X;
		float dy = at.Y - centre.Y;
		float x = dx * cosf(yaw) + dy * sinf(yaw);
		float y = dy * cosf(yaw) - dx * sinf(yaw);
		if (fabsf(x) <= halfX && fabsf(y) <= halfY && at.Z >= floor - OBSTACLE_HEADROOM && at.Z <= floor + OBSTACLE_SPOT_HEIGHT) {
			obstacles.push_back(cut);
			NAVMESH_LOG(LOG_LEVEL_INFO, "An objective stands on %s at (%.0f %.0f %.0f), %.0f by %.0f, so the navmesh around it keeps its polys and goal spots stay off it."
				, UE3::ObjectName(actor), centre.X, centre.Y, centre.Z, 2.0f * halfX, 2.0f * halfY);
			return true;
		}
	}
	float reach = sqrtf(halfX * halfX + halfY * halfY);
	FVector queryCentre = { centre.X, centre.Y, floor + OBSTACLE_PLANE_HEIGHT };
	FVector queryExtent = { reach, reach, OBSTACLE_FLOOR_WINDOW };
	PtrArray found = {};
	intersecting_polys(&queryCentre, &queryExtent, &found, 1, 1);
	std::vector<void*> onFloor;
	for (int32_t k = 0; k < found.Num; k++) {
		uint8_t* poly = ((uint8_t**)found.Data)[k];
		if (poly && IsTopLevelPoly(poly) && PolyOnFloor(poly, floor)) {
			onFloor.push_back(poly);
		}
	}
	int32_t nearby = found.Num;
	UE3::FreeArray(&found);
	if (onFloor.empty()) {
		reason = "no poly of its floor under it";
		return false;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.Spawn) + 16, 0);
	Field<UObject*>(parms.data(), off.Spawn_SpawnClass) = obj.NavMeshObstacle;
	Field<FVector>(parms.data(), off.Spawn_SpawnLocation) = queryCentre;
	Field<uint32_t>(parms.data(), off.Spawn_bNoCollisionFail) = 1;
	UE3::ProcessNativeEvent(game, obj.Spawn, parms.data());
	cut.Obstacle = Field<UObject*>(parms.data(), off.Spawn_ReturnValue);
	if (!cut.Obstacle) {
		return false;
	}
	HideComponents(cut.Obstacle);
	obstacles.push_back(cut);
	PtrArray shape = { obstacles.back().Corners, 4, 4 };
	PtrArray polys = { onFloor.data(), (int32_t)onFloor.size(), (int32_t)onFloor.size() };
	register_obstacle((uint8_t*)cut.Obstacle + OBSTACLE_INTERFACE, 0, &shape, &polys);
	int32_t taken = 0;
	for (void* poly : onFloor) {
		taken += Field<int16_t>(poly, POLY_OBSTACLE_COUNT) > 0 ? 1 : 0;
		void* mesh = Field<void*>(poly, 0);
		if (std::find(meshes.begin(), meshes.end(), mesh) == meshes.end()) {
			meshes.push_back(mesh);
		}
	}
	polys_cut += taken;
	NAVMESH_LOG(LOG_LEVEL_INFO, "The module cut the navmesh around %s at (%.0f %.0f %.0f), %.0f by %.0f at yaw %.0f, floor %.0f. The engine took %d of %d floor polys and %d polys near it."
		, UE3::ObjectName(actor), centre.X, centre.Y, centre.Z, 2.0f * halfX, 2.0f * halfY, yaw / UNREAL_YAW_TO_RADIANS * 360.0f / 65536.0f
		, floor, taken, (int32_t)onFloor.size(), nearby);
	return true;
}

void CutModeMeshes(UObject* game)
{
	UObject* world = game ? Field<UObject*>(game, off.Actor_WorldInfo) : 0;
	obstacles.clear();
	if (!register_obstacle || !world) {
		return;
	}
	turrets_seen.clear();
	int32_t meshes = 0;
	int32_t skipped = 0;
	int32_t polysCut = 0;
	std::vector<void*> cutMeshes;
	std::vector<FVector> objectives;
	for (int32_t i = 0; i < UE3::ObjectCount(); i++) {
		UObject* object = UE3::ObjectAt(i);
		const char* className = object ? UE3::ObjectName(UE3::ObjectClass(object)) : 0;
		if (className && strcmp(className, OBJECTIVE_CLASS) == 0 && Field<UObject*>(object, off.Actor_WorldInfo) == world) {
			objectives.push_back(Field<FVector>(object, off.Actor_Location));
		}
	}
	for (int32_t i = 0; i < UE3::ObjectCount() && obstacles.size() < (size_t)OBSTACLE_LIMIT; i++) {
		UObject* mesh = UE3::ObjectAt(i);
		if (!mesh || !UE3::IsA(mesh, obj.ModeMesh) || Field<UObject*>(mesh, off.Actor_WorldInfo) != world || UE3::GetBoolProperty(mesh, obj.bDeleteMe)
			|| !UE3::GetBoolProperty(mesh, obj.bBlockActors) || !UE3::GetBoolProperty(mesh, obj.bCollideActors)) {
			continue;
		}
		UObject* component = Field<UObject*>(mesh, off.MeshActor_StaticMeshComponent);
		if (!component) {
			continue;
		}
		meshes++;
		const char* reason = 0;
		if (CutAround(game, world, mesh, component, true, OBSTACLE_MARGIN, objectives, cutMeshes, polysCut, reason)) {
			continue;
		}
		if (!reason) {
			NAVMESH_LOG(LOG_LEVEL_WARN, "%s", "The engine refused to spawn a NavMeshObstacle, the meshes of the game mode are not cut.");
			break;
		}
		skipped++;
		NAVMESH_LOG(LOG_LEVEL_DEBUG, "%s at (%.0f %.0f %.0f) is not cut: %s.", UE3::ObjectName(mesh)
			, Field<FVector>(component, off.Component_Bounds).X, Field<FVector>(component, off.Component_Bounds).Y, Field<FVector>(component, off.Component_Bounds).Z, reason);
	}
	SplitCutMeshes(cutMeshes);
	NAVMESH_LOG(LOG_LEVEL_INFO, "Mode meshes: %d block pawns, %u cut on %d polys, %d not cut.", meshes, (uint32_t)obstacles.size(), polysCut, skipped);
}

void CutTurrets(UObject* game)
{
	UObject* world = game ? Field<UObject*>(game, off.Actor_WorldInfo) : 0;
	if (!register_obstacle || !turrets_ready || !world || obstacles.size() >= (size_t)OBSTACLE_LIMIT) {
		return;
	}
	int listed = 0;
	std::vector<void*> cutMeshes;
	for (UObject* pawn = Field<UObject*>(world, off.WorldInfo_PawnList); pawn && listed < LIST_LIMIT; pawn = Field<UObject*>(pawn, off.Pawn_NextPawn), listed++) {
		const char* className = UE3::ObjectName(UE3::ObjectClass(pawn));
		if (!className || strcmp(className, TURRET_CLASS) != 0 || !UE3::GetBoolProperty(pawn, obj.bBlockActors)) {
			continue;
		}
		UObject* component = Field<UObject*>(pawn, off.Actor_CollisionComponent);
		if (!component || std::find(turrets_seen.begin(), turrets_seen.end(), pawn) != turrets_seen.end()) {
			continue;
		}
		turrets_seen.push_back(pawn);
		int32_t polysCut = 0;
		const char* reason = 0;
		if (!CutAround(game, world, pawn, component, false, TURRET_MARGIN, std::vector<FVector>(), cutMeshes, polysCut, reason)) {
			NAVMESH_LOG(LOG_LEVEL_DEBUG, "%s at (%.0f %.0f %.0f) is not cut: %s.", UE3::ObjectName(pawn)
				, Field<FVector>(pawn, off.Actor_Location).X, Field<FVector>(pawn, off.Actor_Location).Y, Field<FVector>(pawn, off.Actor_Location).Z
				, reason ? reason : "the engine refused the obstacle");
		}
	}
	SplitCutMeshes(cutMeshes);
}

static void WidenNewEdges(uint8_t* mesh, uint8_t** before, int before_count, uint8_t** after, int& added)
{
	int count = CollectDynamicEdges(mesh, after, DYNAMIC_EDGE_LIMIT);
	for (int i = 0; i < count; i++) {
		bool known = false;
		for (int j = 0; j < before_count && !known; j++) {
			known = after[i] == before[j];
		}
		if (!known) {
			*(float*)(after[i] + EDGE_EFFECTIVE) = LINK_WIDTH;
			added++;
		}
	}
}

const char* ApplyLink(UObject* world, const JumpLink& link)
{
	static uint8_t* beforeA[DYNAMIC_EDGE_LIMIT];
	static uint8_t* beforeB[DYNAMIC_EDGE_LIMIT];
	static uint8_t* after[DYNAMIC_EDGE_LIMIT];
	if (!add_cross_edge) {
		return "the engine's edge function was not found";
	}
	uint8_t* meshA = 0;
	uint8_t* meshB = 0;
	uint8_t* polyA = PolyUnder(world, link.From, &meshA);
	uint8_t* polyB = PolyUnder(world, link.To, &meshB);
	if (!polyA) {
		return "no navmesh poly under the start";
	}
	if (!polyB) {
		return "no navmesh poly under the end";
	}
	if (polyA == polyB) {
		return "both ends lie on one poly";
	}
	float dx = link.To.X - link.From.X;
	float dy = link.To.Y - link.From.Y;
	float flat = sqrtf(dx * dx + dy * dy);
	if (flat < 1.0f) {
		return "the ends lie on one vertical";
	}
	float px = -dy / flat * LINK_WIDTH * 0.5f;
	float py = dx / flat * LINK_WIDTH * 0.5f;
	FVector v1 = { link.From.X + px, link.From.Y + py, link.From.Z };
	FVector v2 = { link.From.X - px, link.From.Y - py, link.From.Z };
	void* pair[2] = { polyA, polyB };
	PtrArray polys = { pair, 2, 2 };
	int countA = CollectDynamicEdges(meshA, beforeA, DYNAMIC_EDGE_LIMIT);
	int countB = meshB == meshA ? 0 : CollectDynamicEdges(meshB, beforeB, DYNAMIC_EDGE_LIMIT);
	__try {
		add_cross_edge(&v1, &v2, &polys, 0, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "the engine refused the edge";
	}
	int added = 0;
	WidenNewEdges(meshA, beforeA, countA, after, added);
	if (meshB != meshA) {
		WidenNewEdges(meshB, beforeB, countB, after, added);
	}
	NAVMESH_LOG(LOG_LEVEL_DEBUG, "Link from (%.0f %.0f %.0f) to (%.0f %.0f %.0f): %d edges added.", link.From.X, link.From.Y, link.From.Z, link.To.X, link.To.Y, link.To.Z, added);
	return 0;
}

void ApplyLinks(UObject* world)
{
	uint32_t made = 0;
	for (const JumpLink& link : links) {
		const char* problem = ApplyLink(world, link);
		if (problem) {
			NAVMESH_LOG(LOG_LEVEL_WARN, "Link from (%.0f %.0f %.0f) to (%.0f %.0f %.0f): %s.", link.From.X, link.From.Y, link.From.Z, link.To.X, link.To.Y, link.To.Z, problem);
		}
		else {
			made++;
		}
	}
	if (!links.empty()) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "%u of %u links have their edges.", made, (uint32_t)links.size());
	}
	uint32_t seamsMade = 0;
	for (const JumpLink& seam : seams) {
		const char* problem = ApplyLink(world, seam);
		if (problem) {
			NAVMESH_LOG(LOG_LEVEL_DEBUG, "Seam link from (%.0f %.0f %.0f) to (%.0f %.0f %.0f): %s.", seam.From.X, seam.From.Y, seam.From.Z, seam.To.X, seam.To.Y, seam.To.Z, problem);
		}
		else {
			seamsMade++;
		}
	}
	if (!seams.empty()) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "%u of %u seam links have their edges.", seamsMade, (uint32_t)seams.size());
	}
}

static std::wstring LinkPath(const std::string& map, const wchar_t* extension = L".navlinks")
{
	std::wstring path = CachePath(map);
	return path.empty() ? path : path.substr(0, path.size() - wcslen(L".navcache")) + extension;
}

static bool ReadLinkFile(const std::wstring& path, std::vector<JumpLink>& out, bool& good)
{
	FILE* in = 0;
	if (path.empty() || _wfopen_s(&in, path.c_str(), L"rb") != 0 || !in) {
		return false;
	}
	uint32_t header[3] = { 0, 0, 0 };
	good = fread(header, sizeof(header), 1, in) == 1 && header[0] == LINK_MAGIC && header[1] == LINK_VERSION && header[2] < 10000;
	for (uint32_t i = 0; good && i < header[2]; i++) {
		JumpLink link;
		good = fread(&link.From, sizeof(FVector), 1, in) == 1 && fread(&link.To, sizeof(FVector), 1, in) == 1;
		if (good) {
			out.push_back(link);
		}
	}
	fclose(in);
	return true;
}

static void LoadLinks(const std::string& map)
{
	bool good = true;
	if (ReadLinkFile(LinkPath(map), links, good)) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "Loaded %u links for %s%s.", (uint32_t)links.size(), map.c_str(), good ? "" : " (the file is cut short)");
	}
	if (ReadLinkFile(LinkPath(map, L".navseams"), seams, good)) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "Loaded %u seam links for %s%s.", (uint32_t)seams.size(), map.c_str(), good ? "" : " (the file is cut short)");
	}
}

bool ReadCover(const std::string& map, std::vector<CoverSpot>& spots)
{
	std::wstring path = CoverPath(map);
	FILE* in = 0;
	if (path.empty() || _wfopen_s(&in, path.c_str(), L"rb") != 0 || !in) {
		return false;
	}
	std::vector<uint8_t> file;
	uint8_t chunk[65536];
	size_t got;
	while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		file.insert(file.end(), chunk, chunk + got);
	}
	fclose(in);
	Reader reader = { file.data(), file.size(), 0, false };
	uint32_t header[3] = {};
	reader.Get(header, sizeof(header));
	if (reader.Failed || header[0] != COVER_MAGIC || header[1] != COVER_VERSION) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "The cover file of %s is not a version %u cover file.", map.c_str(), COVER_VERSION);
		return false;
	}
	for (uint32_t i = 0; i < header[2] && !reader.Failed; i++) {
		CoverSpot spot = {};
		uint8_t tail[4] = {};
		reader.Get(&spot.Location, sizeof(FVector));
		reader.Get(&spot.Yaw, sizeof(spot.Yaw));
		reader.Get(tail, sizeof(tail));
		if (reader.Failed) {
			break;
		}
		spot.CoverType = tail[0];
		spot.Flags = tail[1];
		spots.push_back(spot);
	}
	return true;
}

static void LoadCover(UObject* game, const std::string& map)
{
	std::vector<CoverSpot> spots;
	if (!ReadCover(map, spots)) {
		return;
	}
	uint32_t added = 0;
	for (CoverSpot& spot : spots) {
		added += AddCover(game, spot) ? 1 : 0;
	}
	NAVMESH_LOG(LOG_LEVEL_INFO, "Loaded %u cover spots for %s, %u added as cover.", (uint32_t)spots.size(), map.c_str(), added);
}

static bool LoadCache(UObject* game, UObject* world, const std::string& map)
{
	DWORD start = GetTickCount();
	std::wstring path = CachePath(map);
	FILE* in = 0;
	if (path.empty() || _wfopen_s(&in, path.c_str(), L"rb") != 0 || !in) {
		NAVMESH_LOG(LOG_LEVEL_INFO, "No navmesh cache for %s.", map.c_str());
		return false;
	}
	std::vector<uint8_t> file;
	uint8_t chunk[65536];
	size_t got;
	while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		file.insert(file.end(), chunk, chunk + got);
	}
	fclose(in);

	Reader reader = { file.data(), file.size(), 0, false };
	uint32_t header[3] = {};
	reader.Get(header, sizeof(header));
	if (reader.Failed || header[0] != CACHE_MAGIC || header[1] != CACHE_VERSION) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "The cache for %s is not a version %u navmesh cache.", map.c_str(), CACHE_VERSION);
		return false;
	}
	std::vector<CacheEntry> entries(header[2]);
	uint32_t newCount = 0;
	for (CacheEntry& entry : entries) {
		reader.Get(&entry.Kind, sizeof(entry.Kind));
		if (entry.Kind == CACHE_EXISTING_PYLON) {
			uint32_t length = 0;
			const uint8_t* text = reader.Blob(length);
			if (text) {
				entry.Path.assign((const char*)text, length);
			}
		}
		else {
			reader.Get(&entry.Index, sizeof(entry.Index));
			reader.Get(&entry.Location, sizeof(entry.Location));
			reader.Get(&entry.Radius, sizeof(entry.Radius));
			newCount = entry.Index + 1 > newCount ? entry.Index + 1 : newCount;
		}
		entry.NavMesh = reader.Blob(entry.NavMeshSize);
		entry.ObstacleMesh = reader.Blob(entry.ObstacleMeshSize);
	}
	if (reader.Failed || newCount > (uint32_t)LIST_LIMIT) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "The cache for %s is cut short.", map.c_str());
		return false;
	}

	spawned.assign(newCount, 0);
	for (const CacheEntry& entry : entries) {
		if (entry.Kind == CACHE_NEW_PYLON) {
			spawned[entry.Index] = SpawnPylonAt(game, entry.Location, entry.Radius);
		}
	}
	if (std::find(spawned.begin(), spawned.end(), (UObject*)0) != spawned.end()) {
		NAVMESH_LOG(LOG_LEVEL_ERROR, "Not every Pylon of the cache for %s could be spawned, it is not loaded.", map.c_str());
		DetachPylons(world, std::vector<UObject*>(spawned.begin(), std::remove(spawned.begin(), spawned.end(), (UObject*)0)));
		spawned.clear();
		return false;
	}
	NavCache::SetNewPylons(spawned);
	NavCache::ResetPaths();

	uint32_t loaded = 0;
	uint32_t failed = 0;
	std::vector<UObject*> added;
	for (const CacheEntry& entry : entries) {
		UObject* pylon = entry.Kind == CACHE_NEW_PYLON ? spawned[entry.Index] : NavCache::FindActor(entry.Path);
		if (!pylon || !UE3::IsA(pylon, obj.Pylon)) {
			NAVMESH_LOG(LOG_LEVEL_WARN, "The cache names %s, but this map has no Pylon for it.", entry.Path.c_str());
			failed++;
			continue;
		}
		if (!LoadMeshes(pylon, entry.NavMesh, entry.NavMeshSize, entry.ObstacleMesh, entry.ObstacleMeshSize)) {
			if (entry.Kind == CACHE_NEW_PYLON) {
				DetachPylons(world, std::vector<UObject*>{ pylon });
			}
			failed++;
			continue;
		}
		loaded++;
		if (entry.Kind != CACHE_NEW_PYLON) {
			continue;
		}
		AddToOctree(pylon);
		added.push_back(pylon);
	}
	for (UObject* pylon : added) {
		if (!Link(pylon)) {
			NAVMESH_LOG(LOG_LEVEL_ERROR, "The engine failed to link %s to the Pylons around it.", UE3::ObjectName(pylon));
		}
	}
	WidenDynamicEdges(world, 0);
	int32_t sides = 0;
	for (UObject* pylon : added) {
		uint8_t* mesh = Field<uint8_t*>(pylon, PYLON_NAVMESH_PTR);
		sides += mesh ? Field<int32_t>(mesh, MESH_BORDER_SEGMENTS + 4) : 0;
	}
	NAVMESH_LOG(LOG_LEVEL_INFO, "Loaded the cache for %s in %u ms: %u Pylons (%u added, %d border sides), %u failed."
		, map.c_str(), GetTickCount() - start, loaded, (uint32_t)added.size(), sides, failed);
	int32_t links = 0;
	int32_t narrow = 0;
	CountLinks(world, links, narrow, true);
	NAVMESH_LOG(LOG_LEVEL_INFO, "After widening: %d dynamic links, %d narrower than a bot.", links, narrow);
	return true;
}

struct StairPlate {
	const char* Map;
	const char* Mesh;
	FVector Origin;
};
static const StairPlate STAIR_PLATES[] = {
	{ "Floodgate", "SM_Floor", { -3388.9f, -4558.0f, 652.7f } },
	{ "Floodgate", "SM_Floor", { -3607.6f, -4557.5f, 519.6f } },
	{ "Floodgate", "SM_Floor", { -3388.9f, -5286.0f, 652.7f } },
	{ "Floodgate", "SM_Floor", { -3607.6f, -5285.5f, 519.6f } },
	{ "Floodgate", "SM_Floor", { -3103.0f, -5238.4f, 652.0f } },
	{ "Floodgate", "SM_Floor", { -2884.3f, -5238.9f, 519.0f } },
};
static const float STAIR_PLATE_MATCH = 4.0f;
static const float STAIR_DROP = 15.0f;

static UE3::FRotator RotatorOf(const float* matrix)
{
	const float units = 32768.0f / 3.14159265f;
	float pitch = atan2f(matrix[2], sqrtf(matrix[0] * matrix[0] + matrix[1] * matrix[1]));
	float yaw = atan2f(matrix[1], matrix[0]);
	float sideX = -sinf(yaw);
	float sideY = cosf(yaw);
	float roll = atan2f(matrix[8] * sideX + matrix[9] * sideY, matrix[4] * sideX + matrix[5] * sideY);
	return { (int32_t)lroundf(pitch * units), (int32_t)lroundf(yaw * units), (int32_t)lroundf(roll * units) };
}

void LowerStairs(UObject* game)
{
	UObject* world = ready && game ? Field<UObject*>(game, off.Actor_WorldInfo) : 0;
	if (!world) {
		return;
	}
	std::string map = MapName(world);
	int32_t listed = 0;
	for (const StairPlate& plate : STAIR_PLATES) {
		listed += _stricmp(plate.Map, map.c_str()) == 0 ? 1 : 0;
	}
	if (listed == 0 || !obj.ReattachComponent) {
		return;
	}
	int32_t lowered = 0;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(obj.ReattachComponent) + 16, 0);
	for (int32_t i = 0; i < UE3::ObjectCount(); i++) {
		UObject* component = UE3::ObjectAt(i);
		if (!component || !UE3::IsA(component, obj.StaticMeshComponent)) {
			continue;
		}
		UObject* owner = Field<UObject*>(component, off.Component_Owner);
		UObject* mesh = Field<UObject*>(component, off.StaticMeshComponent_StaticMesh);
		if (!owner || !mesh || Field<UObject*>(owner, off.Actor_WorldInfo) != world) {
			continue;
		}
		const float* matrix = &Field<float>(component, off.Component_LocalToWorld);
		for (const StairPlate& plate : STAIR_PLATES) {
			if (_stricmp(plate.Map, map.c_str()) != 0 || strcmp(UE3::ObjectName(mesh), plate.Mesh) != 0
				|| fabsf(matrix[12] - plate.Origin.X) > STAIR_PLATE_MATCH || fabsf(matrix[13] - plate.Origin.Y) > STAIR_PLATE_MATCH || fabsf(matrix[14] - plate.Origin.Z) > STAIR_PLATE_MATCH) {
				continue;
			}
			FVector from = { matrix[12], matrix[13], matrix[14] };
			FVector& translation = Field<FVector>(component, off.Component_Translation);
			if (UE3::IsA(owner, obj.MeshCollection)) {
				const FVector& base = Field<FVector>(owner, off.Actor_Location);
				translation = { from.X - base.X, from.Y - base.Y, from.Z - STAIR_DROP - base.Z };
				Field<UE3::FRotator>(component, off.Component_Rotation) = RotatorOf(matrix);
			}
			else {
				translation.X -= STAIR_DROP * matrix[2];
				translation.Y -= STAIR_DROP * matrix[6];
				translation.Z -= STAIR_DROP * matrix[10];
			}
			std::fill(parms.begin(), parms.end(), (uint8_t)0);
			Field<UObject*>(parms.data(), off.ReattachComponent_ComponentToReattach) = component;
			UE3::ProcessNativeEvent(owner, obj.ReattachComponent, parms.data());
			lowered++;
			NAVMESH_LOG(LOG_LEVEL_INFO, "Lowered a stair plate of %s from (%.0f %.0f %.1f) to (%.0f %.0f %.1f).", UE3::ObjectName(owner), from.X, from.Y, from.Z, matrix[12], matrix[13], matrix[14]);
			break;
		}
	}
	if (lowered != listed) {
		NAVMESH_LOG(LOG_LEVEL_WARN, "Lowered %d of the %d stair plates listed for %s.", lowered, listed, map.c_str());
	}
}

void OnMatchStart(UObject* game)
{
	spawned.clear();
	links.clear();
	seams.clear();
	RaisePathVisits();
	if (!ready) {
		return;
	}
	UObject* world = Field<UObject*>(game, off.Actor_WorldInfo);
	if (!world) {
		return;
	}
	std::string map = MapName(world);
	if (_stricmp(map.c_str(), "FoxEntry") == 0) {
		return;
	}
	LoadCache(game, world, map);
	LoadCover(game, map);
	LoadLinks(map);
	ApplyLinks(world);
	int32_t dynamic = 0;
	int32_t narrow = 0;
	CountLinks(world, dynamic, narrow, false);
	NAVMESH_LOG(LOG_LEVEL_INFO, "After the cover and the links: %d dynamic links, %d narrower than a bot.", dynamic, narrow);
}

}
