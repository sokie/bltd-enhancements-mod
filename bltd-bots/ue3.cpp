#include <winsock2.h>
#include <Windows.h>
#include "ue3.hpp"
#include "../loader/hook.hpp"
#include <string.h>

namespace UE3 {

static const uint32_t RVA_GNAMES = 0x0119604C;
static const uint32_t RVA_GOBJOBJECTS = 0x0119607C;
static const uint32_t RVA_PROCESS_INTERNAL = 0x00063F00;
static const uint32_t RVA_PROCESS_EVENT = 0x00069490;
static const uint32_t RVA_FARRAY_DESTRUCTOR = 0x00605520;
static const uint32_t RVA_APP_MALLOC = 0x0001AC00;
static const uint32_t RVA_LOAD_PACKAGE = 0x000B0A00;
static const uint32_t RVA_ROOT_PACKAGE = 0x00989590;
static const uint32_t RVA_IS_USER_MOD_CONTENT_LOADED = 0x0006E9F0;
static const int PROCESS_INTERNAL_DETOUR_LEN = 13;
static const uint8_t PROCESS_INTERNAL_PROLOGUE[PROCESS_INTERNAL_DETOUR_LEN] = { 0x6A, 0xFF, 0x68, 0x08, 0x94, 0x02, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t DETOUR_PROLOGUE[] = { 0x58, 0x51, 0x50, 0xE9 };
static const uint8_t APP_MALLOC_PROLOGUE[] = { 0x8B, 0x0D, 0x18, 0x2B, 0x51, 0x01, 0x85, 0xC9 };
static const uint8_t LOAD_PACKAGE_PROLOGUE[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA0, 0xE1, 0x02, 0x01, 0x64, 0xA1 };
static const uint8_t ROOT_PACKAGE_PROLOGUE[] = { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0x74, 0x24, 0x18, 0x85, 0xF6, 0x75, 0x1A };

static const uint32_t NAME_ENTRY_FLAGS = 0x08;
static const uint32_t NAME_ENTRY_TEXT = 0x10;

typedef void (__stdcall *tProcessInternal)(UObject* self, FFrame* stack, void* result);
typedef void (__fastcall *tProcessEvent)(UObject* self, void* unused_edx, UObject* function, void* parms, void* result);
typedef void (__fastcall *tFArrayDestructor)(void* array);
typedef void* (__cdecl *tAppMalloc)(uint32_t size, uint32_t alignment);
typedef UObject* (__cdecl *tLoadPackage)(UObject* outer, const wchar_t* filename, uint32_t load_flags);
typedef void (__fastcall *tRootPackage)(void* self, void* unused_edx, UObject* package);

static TArray<uint8_t*>* names = 0;
static TArray<UObject*>* objects = 0;
static uint8_t* process_internal_address = 0;
static tProcessInternal process_internal_original = 0;
static tProcessEvent process_event = 0;
static tFArrayDestructor farray_destructor = 0;
static tAppMalloc app_malloc = 0;
static tLoadPackage load_package = 0;
static tRootPackage root_package = 0;
static uint32_t* user_mod_content_loaded = 0;
static int32_t super_field_offset = -1;
static tScriptPreHandler script_pre_handler = 0;
static tScriptPostHandler script_post_handler = 0;

bool Init(uintptr_t title_base)
{
	process_internal_address = (uint8_t*)(title_base + RVA_PROCESS_INTERNAL);
	bool prologueKnown = memcmp(process_internal_address, PROCESS_INTERNAL_PROLOGUE, PROCESS_INTERNAL_DETOUR_LEN) == 0
		|| memcmp(process_internal_address, DETOUR_PROLOGUE, sizeof(DETOUR_PROLOGUE)) == 0;
	if (!prologueKnown || memcmp((void*)(title_base + RVA_APP_MALLOC), APP_MALLOC_PROLOGUE, sizeof(APP_MALLOC_PROLOGUE)) != 0) {
		return false;
	}
	names = (TArray<uint8_t*>*)(title_base + RVA_GNAMES);
	objects = (TArray<UObject*>*)(title_base + RVA_GOBJOBJECTS);
	process_event = (tProcessEvent)(title_base + RVA_PROCESS_EVENT);
	farray_destructor = (tFArrayDestructor)(title_base + RVA_FARRAY_DESTRUCTOR);
	app_malloc = (tAppMalloc)(title_base + RVA_APP_MALLOC);
	if (memcmp((void*)(title_base + RVA_LOAD_PACKAGE), LOAD_PACKAGE_PROLOGUE, sizeof(LOAD_PACKAGE_PROLOGUE)) == 0
		&& memcmp((void*)(title_base + RVA_ROOT_PACKAGE), ROOT_PACKAGE_PROLOGUE, sizeof(ROOT_PACKAGE_PROLOGUE)) == 0) {
		load_package = (tLoadPackage)(title_base + RVA_LOAD_PACKAGE);
		root_package = (tRootPackage)(title_base + RVA_ROOT_PACKAGE);
	}
	const uint8_t* is_user_mod_content_loaded = (const uint8_t*)(title_base + RVA_IS_USER_MOD_CONTENT_LOADED);
	if (is_user_mod_content_loaded[0] == 0xA1 && is_user_mod_content_loaded[5] == 0xC3) {
		user_mod_content_loaded = *(uint32_t* const*)(is_user_mod_content_loaded + 1);
	}
	return true;
}

static void __stdcall ProcessInternalHook(UObject* self, FFrame* stack, void* result)
{
	if (script_pre_handler && !script_pre_handler(self, stack, result)) {
		return;
	}
	process_internal_original(self, stack, result);
	if (script_post_handler) {
		script_post_handler(self, stack, result);
	}
}

bool HookScriptCalls()
{
	if (process_internal_original) {
		return true;
	}
	// A hook on top of another detour would copy its jump.
	if (memcmp(process_internal_address, PROCESS_INTERNAL_PROLOGUE, PROCESS_INTERNAL_DETOUR_LEN) != 0) {
		return false;
	}
	process_internal_original = (tProcessInternal)DetourClassFunc(process_internal_address, (BYTE*)ProcessInternalHook, PROCESS_INTERNAL_DETOUR_LEN);
	return process_internal_original != 0;
}

void SetScriptHandlers(tScriptPreHandler pre_handler, tScriptPostHandler post_handler)
{
	script_pre_handler = pre_handler;
	script_post_handler = post_handler;
}

static const char* NameText(const FName& name)
{
	if (!names || name.Index < 0 || name.Index >= names->Num) {
		return "";
	}
	uint8_t* entry = names->Data[name.Index];
	if (!entry || (Field<int32_t>(entry, NAME_ENTRY_FLAGS) & 1)) {
		return "";
	}
	return (const char*)(entry + NAME_ENTRY_TEXT);
}

const char* ObjectName(UObject* object)
{
	if (!object) {
		return "";
	}
	return NameText(Field<FName>(object, Offset::ObjectName));
}

const char* NameEntryText(int32_t index)
{
	return NameText(FName{ index, 0 });
}

int32_t FindNameIndex(const char* text)
{
	if (!names) {
		return -1;
	}
	for (int32_t i = 0; i < names->Num; i++) {
		uint8_t* entry = names->Data[i];
		if (entry && !(Field<int32_t>(entry, NAME_ENTRY_FLAGS) & 1) && _stricmp((const char*)(entry + NAME_ENTRY_TEXT), text) == 0) {
			return i;
		}
	}
	return -1;
}

int32_t ObjectCount()
{
	return objects ? objects->Num : 0;
}

UObject* ObjectAt(int32_t index)
{
	return objects && index >= 0 && index < objects->Num ? objects->Data[index] : 0;
}

static const uint32_t FLAG_UNREACHABLE = 0x2;

bool IsValidObject(UObject* object)
{
	if (!object || !objects) {
		return false;
	}
	// A pointer kept from an unloaded level can point at freed memory.
	__try {
		int32_t index = Field<int32_t>(object, Offset::ObjectIndex);
		return index >= 0 && index < objects->Num && objects->Data[index] == object && (Field<uint32_t>(object, Offset::ObjectFlagsHigh) & FLAG_UNREACHABLE) == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

UObject* FindObject(const char* path)
{
	if (!objects || !names || names->Num == 0 || strcmp(NameText(FName{ 0, 0 }), "None") != 0) {
		return 0;
	}
	const char* segments[8];
	size_t lengths[8];
	int segmentCount = 0;
	for (const char* segment = path; segmentCount < 8; ) {
		const char* dot = strchr(segment, '.');
		segments[segmentCount] = segment;
		lengths[segmentCount] = dot ? (size_t)(dot - segment) : strlen(segment);
		segmentCount++;
		if (!dot) {
			break;
		}
		segment = dot + 1;
	}
	for (int32_t i = 0; i < objects->Num; i++) {
		UObject* candidate = objects->Data[i];
		UObject* object = candidate;
		int segment = segmentCount - 1;
		for (; object && segment >= 0; segment--) {
			FName name = Field<FName>(object, Offset::ObjectName);
			const char* text = NameText(name);
			if (name.Number != 0 || strlen(text) != lengths[segment] || strncmp(text, segments[segment], lengths[segment]) != 0) {
				break;
			}
			object = Field<UObject*>(object, Offset::ObjectOuter);
		}
		if (segment < 0 && !object) {
			return candidate;
		}
	}
	return 0;
}

int32_t FindPropertyOffset(const char* path)
{
	UObject* property = FindObject(path);
	if (!property) {
		return -1;
	}
	return Field<int32_t>(property, Offset::PropertyOffset);
}

void SetBoolProperty(UObject* object, UObject* property, bool value)
{
	uint32_t mask = Field<uint32_t>(property, Offset::BoolPropertyBitMask);
	uint32_t& bits = Field<uint32_t>(object, Field<int32_t>(property, Offset::PropertyOffset));
	bits = value ? (bits | mask) : (bits & ~mask);
}

bool GetBoolProperty(UObject* object, UObject* property)
{
	uint32_t mask = Field<uint32_t>(property, Offset::BoolPropertyBitMask);
	return (Field<uint32_t>(object, Field<int32_t>(property, Offset::PropertyOffset)) & mask) != 0;
}

bool DetectSuperField(UObject* child_class, UObject* parent_class)
{
	const int32_t candidates[] = { 0x3C, 0x44, 0x48 };
	for (int32_t candidate : candidates) {
		if (Field<UObject*>(child_class, candidate) == parent_class) {
			super_field_offset = candidate;
			return true;
		}
	}
	return false;
}

bool IsA(UObject* object, UObject* class_object)
{
	if (!object || !class_object || super_field_offset < 0) {
		return false;
	}
	for (UObject* current = ObjectClass(object); current; current = Field<UObject*>(current, super_field_offset)) {
		if (current == class_object) {
			return true;
		}
	}
	return false;
}

void ProcessEvent(UObject* object, UObject* function, void* parms)
{
	if (Field<uint32_t>(object, Offset::ObjectFlagsHigh) & FLAG_UNREACHABLE) {
		return;
	}
	process_event(object, 0, function, parms, 0);
}

void ProcessNativeEvent(UObject* object, UObject* function, void* parms)
{
	if (Field<uint32_t>(object, Offset::ObjectFlagsHigh) & FLAG_UNREACHABLE) {
		return;
	}
	// ProcessEvent refuses a function with an opcode index, and its Func points at the same exec.
	uint16_t& nativeIndex = Field<uint16_t>(function, Offset::FunctionNative);
	uint16_t savedIndex = nativeIndex;
	nativeIndex = 0;
	process_event(object, 0, function, parms, 0);
	nativeIndex = savedIndex;
}

void FreeArray(void* array)
{
	farray_destructor(array);
}

void* Malloc(uint32_t size, uint32_t alignment)
{
	return app_malloc(size, alignment);
}

bool LoadPackage(const wchar_t* filename)
{
	UObject* package = load_package ? load_package(0, filename, 0) : 0;
	if (!package) {
		return false;
	}
	root_package(0, 0, package);
	return true;
}

bool ClearUserModContent()
{
	if (!user_mod_content_loaded || *user_mod_content_loaded == 0) {
		return false;
	}
	*user_mod_content_loaded = 0;
	return true;
}

uint8_t* FindInScript(UObject* function, const void* pattern, size_t length)
{
	TArray<uint8_t>& script = Field<TArray<uint8_t> >(function, Offset::StructScript);
	uint8_t* found = 0;
	for (int32_t i = 0; i + (int32_t)length <= script.Num; i++) {
		if (memcmp(script.Data + i, pattern, length) == 0) {
			if (found) {
				return 0;
			}
			found = script.Data + i;
		}
	}
	return found;
}

uint16_t FunctionParmsSize(UObject* function)
{
	return Field<uint16_t>(function, Offset::FunctionParmsSize);
}

}
