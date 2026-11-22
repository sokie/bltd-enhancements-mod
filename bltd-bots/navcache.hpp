#pragma once
#include "ue3.hpp"
#include <stdint.h>
#include <string>
#include <vector>

namespace NavCache {

static const int32_t MESH_VERTS = 0x3C;
static const int32_t MESH_POLYS = 0x90;
static const int32_t VERT_SIZE = 0x24;
static const int32_t POLY_SIZE = 0xB0;
static const int32_t POLY_VERTS = 0x08;

bool Init(uintptr_t title_base);
void SetNewPylons(const std::vector<UE3::UObject*>& pylons);
std::string ObjectPath(UE3::UObject* object);
UE3::UObject* FindActor(const std::string& path);
void ResetPaths();
UE3::UObject* LoadMesh(UE3::UObject* pylon, const uint8_t* data, size_t size);
const char* MeshProblem(UE3::UObject* mesh);

}
