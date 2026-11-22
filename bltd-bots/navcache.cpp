#include <winsock2.h>
#include <Windows.h>
#include "../loader/log.hpp"
#include "navcache.hpp"
#include <map>
#include <math.h>
#include <stdio.h>
#include <string.h>

using UE3::UObject;
using UE3::Field;

#define NAVCACHE_LOG(level, format, ...) Log::Write(level, "[NavMesh] " format, __VA_ARGS__)

namespace NavCache {

static const uint32_t RVA_MEMORY_READER = 0x00004510;
static const uint32_t RVA_MEMORY_READER_VTABLE = 0x00CD4AA8;
static const uint32_t RVA_STATIC_CONSTRUCT_OBJECT = 0x000ADEA0;
static const uint32_t RVA_GERROR = 0x01112B04;
static const uint32_t RVA_MESH_SERIALIZE = 0x0040A190;
static const uint8_t MEMORY_ARCHIVE_PROLOGUE[] = { 0x56, 0x8B, 0xF1, 0xC7, 0x06, 0x10, 0x4D, 0x0D, 0x01 };
static const uint8_t STATIC_CONSTRUCT_OBJECT_PROLOGUE[] = { 0x6A, 0xFF, 0x68, 0x43, 0xDE, 0x02, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t ARCHIVE_SERIALIZE_SLOT = 0x04 / 4;
static const uint32_t ARCHIVE_OBJECT_SLOT = 0x18 / 4;
static const uint32_t ARCHIVE_NAME_SLOT = 0x1C / 4;
static const uint32_t ARCHIVE_VTABLE_SLOTS = 32;
static const size_t ARCHIVE_SIZE = 0x100;
static const int32_t ARCHIVE_OFFSET = 0x80;
static const uint32_t MESH_SERIALIZE_SLOT = 0x34 / 4;
static const int32_t MAX_TEXT = 1024;
static const int32_t INDEX_NONE = -1;
static const int32_t KDOP_LEAF_TRIANGLES = 6;
static const int32_t KDOP_MAX_TRIANGLES = 0xFFFF;
static const float ONE_THIRD = 0.33333334f;

enum RefKind : int32_t {
	REF_NONE = 0,
	REF_NEW_PYLON = 1,
	REF_PATH = 2,
};

typedef void* (__fastcall *tMemoryArchive)(void* archive, void* unused_edx, UE3::TArray<uint8_t>* bytes, uint32_t persistent);
typedef void (__fastcall *tArchiveSerialize)(void* archive, void* unused_edx, void* data, int32_t length);
typedef UObject* (__cdecl *tStaticConstructObject)(UObject* object_class, UObject* outer, UE3::FName name, uint64_t flags
	, UObject* object_template, void* error, UObject* subobject_root, void* instance_graph);
typedef void (__fastcall *tMeshSerialize)(UObject* mesh, void* unused_edx, void* archive);

static tMemoryArchive memory_reader = 0;
static tStaticConstructObject static_construct_object = 0;
static void** gerror = 0;
static uintptr_t mesh_serialize = 0;
static void* reader_vtable[ARCHIVE_VTABLE_SLOTS];
static tArchiveSerialize reader_serialize = 0;
static bool archive_failed = false;
static std::vector<UObject*> new_pylons;
static std::map<std::string, UObject*> actor_paths;
static bool actor_paths_built = false;
static std::map<std::string, int32_t> name_indices;

static void Raw(void* archive, void* data, int32_t length)
{
	reader_serialize(archive, 0, data, length);
}

static void Fail(const char* what, const char* detail)
{
	if (!archive_failed) {
		NAVCACHE_LOG(LOG_LEVEL_ERROR, "The navmesh cache %s: %s.", what, detail);
	}
	archive_failed = true;
}

static void AppendName(std::string& out, UObject* object)
{
	const UE3::FName& name = Field<UE3::FName>(object, UE3::Offset::ObjectName);
	out += UE3::NameEntryText(name.Index);
	if (name.Number) {
		char suffix[16];
		sprintf_s(suffix, sizeof(suffix), "_%d", name.Number - 1);
		out += suffix;
	}
}

std::string ObjectPath(UObject* object)
{
	std::vector<UObject*> chain;
	for (UObject* current = object; current && chain.size() < 16; current = Field<UObject*>(current, UE3::Offset::ObjectOuter)) {
		chain.push_back(current);
	}
	std::string path;
	for (size_t i = chain.size(); i-- > 0; ) {
		AppendName(path, chain[i]);
		if (i) {
			path += '.';
		}
	}
	return path;
}

void ResetPaths()
{
	actor_paths.clear();
	actor_paths_built = false;
}

UObject* FindActor(const std::string& path)
{
	if (!actor_paths_built) {
		actor_paths_built = true;
		UObject* actorClass = UE3::FindObject("Engine.Actor");
		int32_t count = UE3::ObjectCount();
		for (int32_t i = 0; i < count; i++) {
			UObject* object = UE3::ObjectAt(i);
			if (object && UE3::IsA(object, actorClass)) {
				actor_paths[ObjectPath(object)] = object;
			}
		}
	}
	auto found = actor_paths.find(path);
	return found == actor_paths.end() ? 0 : found->second;
}

static int32_t NameIndex(const std::string& text)
{
	auto found = name_indices.find(text);
	if (found != name_indices.end()) {
		return found->second;
	}
	int32_t index = UE3::FindNameIndex(text.c_str());
	name_indices[text] = index;
	return index;
}

static void* __fastcall SerializeObject(void* archive, void* unused_edx, UObject** reference)
{
	int32_t kind = REF_NONE;
	*reference = 0;
	Raw(archive, &kind, sizeof(kind));
	if (kind == REF_NEW_PYLON) {
		int32_t index = -1;
		Raw(archive, &index, sizeof(index));
		if (index < 0 || index >= (int32_t)new_pylons.size()) {
			Fail("names a Pylon that was not spawned", "index out of range");
			return archive;
		}
		*reference = new_pylons[index];
	}
	else if (kind == REF_PATH) {
		int32_t length = 0;
		Raw(archive, &length, sizeof(length));
		if (length <= 0 || length > MAX_TEXT) {
			Fail("has a bad object path", "length out of range");
			return archive;
		}
		std::string path((size_t)length, '\0');
		Raw(archive, &path[0], length);
		*reference = FindActor(path);
		if (!*reference) {
			Fail("names an actor this map does not have", path.c_str());
		}
	}
	else if (kind != REF_NONE) {
		Fail("has a bad object reference", "unknown kind");
	}
	return archive;
}

static void* __fastcall SerializeName(void* archive, void* unused_edx, UE3::FName* name)
{
	int32_t length = 0;
	Raw(archive, &length, sizeof(length));
	if (length <= 0 || length > MAX_TEXT) {
		Fail("has a bad name", "length out of range");
		*name = UE3::FName{ 0, 0 };
		return archive;
	}
	std::string text((size_t)length, '\0');
	Raw(archive, &text[0], length);
	int32_t number = 0;
	Raw(archive, &number, sizeof(number));
	int32_t index = NameIndex(text);
	if (index < 0) {
		Fail("has a name the game does not have", text.c_str());
		index = 0;
	}
	*name = UE3::FName{ index, number };
	return archive;
}

bool Init(uintptr_t title_base)
{
	if (memcmp((void*)(title_base + RVA_MEMORY_READER), MEMORY_ARCHIVE_PROLOGUE, sizeof(MEMORY_ARCHIVE_PROLOGUE)) != 0
		|| memcmp((void*)(title_base + RVA_STATIC_CONSTRUCT_OBJECT), STATIC_CONSTRUCT_OBJECT_PROLOGUE, sizeof(STATIC_CONSTRUCT_OBJECT_PROLOGUE)) != 0) {
		return false;
	}
	memory_reader = (tMemoryArchive)(title_base + RVA_MEMORY_READER);
	static_construct_object = (tStaticConstructObject)(title_base + RVA_STATIC_CONSTRUCT_OBJECT);
	gerror = (void**)(title_base + RVA_GERROR);
	mesh_serialize = title_base + RVA_MESH_SERIALIZE;
	memcpy(reader_vtable, (void*)(title_base + RVA_MEMORY_READER_VTABLE), sizeof(reader_vtable));
	reader_serialize = (tArchiveSerialize)reader_vtable[ARCHIVE_SERIALIZE_SLOT];
	reader_vtable[ARCHIVE_OBJECT_SLOT] = (void*)SerializeObject;
	reader_vtable[ARCHIVE_NAME_SLOT] = (void*)SerializeName;
	return true;
}

void SetNewPylons(const std::vector<UObject*>& pylons)
{
	new_pylons = pylons;
}

static bool IsMesh(UObject* mesh)
{
	return mesh && (uintptr_t)(*(void***)mesh)[MESH_SERIALIZE_SLOT] == mesh_serialize;
}

UObject* LoadMesh(UObject* pylon, const uint8_t* data, size_t size)
{
	static UObject* meshClass = 0;
	if (!meshClass) {
		meshClass = UE3::FindObject("Engine.NavigationMeshBase");
	}
	if (!size || !meshClass) {
		return 0;
	}
	UObject* mesh = static_construct_object(meshClass, pylon, UE3::FName{ 0, 0 }, 0, 0, *gerror, 0, 0);
	if (!IsMesh(mesh)) {
		NAVCACHE_LOG(LOG_LEVEL_ERROR, "No NavigationMeshBase could be constructed for %s.", UE3::ObjectName(pylon));
		return 0;
	}
	// The blob starts with the NetIndex of the saved mesh. The shipped mesh still holds that index.
	std::vector<uint8_t> copy(data, data + size);
	if (copy.size() >= sizeof(int32_t)) {
		*(int32_t*)copy.data() = INDEX_NONE;
	}
	alignas(16) uint8_t archive[ARCHIVE_SIZE] = {};
	UE3::TArray<uint8_t> bytes = { copy.data(), (int32_t)size, (int32_t)size };
	memory_reader(archive, 0, &bytes, 1);
	*(void***)archive = reader_vtable;
	archive_failed = false;
	((tMeshSerialize)mesh_serialize)(mesh, 0, archive);
	int32_t read = Field<int32_t>(archive, ARCHIVE_OFFSET);
	if (archive_failed || read != (int32_t)size) {
		NAVCACHE_LOG(LOG_LEVEL_ERROR, "A navmesh of %s did not load: %d of %u bytes read.", UE3::ObjectName(pylon), read, (uint32_t)size);
		return 0;
	}
	return mesh;
}

struct Centroid {
	float Axis[3];
};

// Runs the engine's kDOP split (TkDOPNode::SplitTriangleList). Returns false when the engine would assert.
static bool KdopBuilds(std::vector<Centroid>& triangles)
{
	std::vector<std::pair<int32_t, int32_t>> ranges = { { 0, (int32_t)triangles.size() } };
	while (!ranges.empty()) {
		int32_t start = ranges.back().first;
		int32_t count = ranges.back().second;
		ranges.pop_back();
		if (count < KDOP_LEAF_TRIANGLES) {
			continue;
		}
		int32_t end = start + count;
		int axis = -1;
		float mean = 0.0f;
		float bestVariance = 0.0f;
		for (int a = 0; a < 3; a++) {
			float sum = 0.0f;
			for (int32_t i = start; i < end; i++) {
				sum += triangles[i].Axis[a];
			}
			float axisMean = (1.0f / (float)count) * sum;
			float variance = 0.0f;
			for (int32_t i = start; i < end; i++) {
				float d = triangles[i].Axis[a] - axisMean;
				variance = d * d + variance;
			}
			variance = (1.0f / (float)count) * variance;
			if (bestVariance <= variance) {
				axis = a;
				mean = axisMean;
				bestVariance = variance;
			}
		}
		if (axis < 0) {
			return false;
		}
		int32_t left = start - 1;
		int32_t right = end;
		while (left < right) {
			do {
				if (++left >= (int32_t)triangles.size()) {
					return false;
				}
			} while (triangles[left].Axis[axis] < mean && left < right);
			do {
				right--;
			} while (triangles[right].Axis[axis] >= mean && right > 0 && left < right);
			if (left < right) {
				std::swap(triangles[left], triangles[right]);
			}
		}
		if (left == end || right == start) {
			left = start + count / 2;
		}
		ranges.push_back({ start, left - start });
		ranges.push_back({ left, end - left });
	}
	return true;
}

const char* MeshProblem(UObject* mesh)
{
	if (!mesh) {
		return 0;
	}
	const uint8_t* verts = Field<uint8_t*>(mesh, MESH_VERTS);
	int32_t vertCount = Field<int32_t>(mesh, MESH_VERTS + 4);
	const uint8_t* polys = Field<uint8_t*>(mesh, MESH_POLYS);
	int32_t polyCount = Field<int32_t>(mesh, MESH_POLYS + 4);
	if (vertCount < 0 || polyCount < 0 || (vertCount && !verts) || (polyCount && !polys)) {
		return "its arrays are broken";
	}
	for (int32_t v = 0; v < vertCount; v++) {
		const float* location = (const float*)(verts + v * VERT_SIZE);
		if (!isfinite(location[0]) || !isfinite(location[1]) || !isfinite(location[2])) {
			return "a vertex is not a finite number";
		}
	}
	std::vector<Centroid> triangles;
	for (int32_t p = 0; p < polyCount; p++) {
		const uint8_t* poly = polys + p * POLY_SIZE;
		const uint16_t* indices = Field<uint16_t*>((void*)poly, POLY_VERTS);
		int32_t count = Field<int32_t>((void*)poly, POLY_VERTS + 4);
		if (count < 3 || !indices) {
			return "a poly has fewer than 3 vertices";
		}
		for (int32_t i = 0; i < count; i++) {
			if (indices[i] >= vertCount) {
				return "a poly names a vertex the mesh does not have";
			}
		}
		const float* first = (const float*)(verts + indices[0] * VERT_SIZE);
		for (int32_t i = count - 1; i > 1; i--) {
			const float* a = (const float*)(verts + indices[i] * VERT_SIZE);
			const float* b = (const float*)(verts + indices[i - 1] * VERT_SIZE);
			Centroid centroid;
			for (int k = 0; k < 3; k++) {
				centroid.Axis[k] = ((a[k] + first[k]) + b[k]) * ONE_THIRD;
			}
			triangles.push_back(centroid);
		}
	}
	if ((int32_t)triangles.size() > KDOP_MAX_TRIANGLES) {
		return "it has more triangles than the kDOP tree can index";
	}
	if (!KdopBuilds(triangles)) {
		return "the engine's kDOP build asserts on its polys";
	}
	return 0;
}

}
