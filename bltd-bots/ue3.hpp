#pragma once
#include <stdint.h>

namespace UE3 {

struct UObject;

struct FName {
	int32_t Index;
	int32_t Number;
};

template <typename T>
struct TArray {
	T* Data;
	int32_t Num;
	int32_t Max;
};

struct FString {
	wchar_t* Data;
	int32_t Num;
	int32_t Max;
};

struct FVector {
	float X;
	float Y;
	float Z;
};

struct FRotator {
	int32_t Pitch;
	int32_t Yaw;
	int32_t Roll;
};

struct FFrame {
	void* VfTable;
	uint32_t bAllowSuppression;
	uint32_t bSuppressEventTag;
	uint32_t bAutoEmitLineTerminator;
	UObject* Node;
	UObject* Object;
	uint8_t* Code;
	uint8_t* Locals;
	FFrame* PreviousFrame;
	void* OutParms;
};

namespace Offset {
	const uint32_t ObjectIndex = 0x04;
	const uint32_t ObjectFlagsHigh = 0x0C;
	const uint32_t ObjectOuter = 0x28;
	const uint32_t ObjectName = 0x2C;
	const uint32_t ObjectClass = 0x34;
	const uint32_t StructScript = 0x54;
	const uint32_t PropertyOffset = 0x64;
	const uint32_t BoolPropertyBitMask = 0x84;
	const uint32_t FunctionNative = 0x94;
	const uint32_t FunctionParmsSize = 0xA2;
}

template <typename T>
inline T& Field(void* object, int32_t offset)
{
	return *(T*)((uint8_t*)object + offset);
}

inline UObject* ObjectClass(UObject* object)
{
	return Field<UObject*>(object, Offset::ObjectClass);
}

bool Init(uintptr_t title_base);
bool HookScriptCalls();

const char* ObjectName(UObject* object);
const char* NameEntryText(int32_t index);
int32_t FindNameIndex(const char* text);
int32_t ObjectCount();
UObject* ObjectAt(int32_t index);
bool IsValidObject(UObject* object);
UObject* FindObject(const char* path);
int32_t FindPropertyOffset(const char* path);
void SetBoolProperty(UObject* object, UObject* property, bool value);
bool GetBoolProperty(UObject* object, UObject* property);
bool DetectSuperField(UObject* child_class, UObject* parent_class);
bool IsA(UObject* object, UObject* class_object);
void ProcessEvent(UObject* object, UObject* function, void* parms);
void ProcessNativeEvent(UObject* object, UObject* function, void* parms);
uint16_t FunctionParmsSize(UObject* function);
void FreeArray(void* array);
void* Malloc(uint32_t size, uint32_t alignment);
bool LoadPackage(const wchar_t* filename);
bool ClearUserModContent();
uint8_t* FindInScript(UObject* function, const void* pattern, size_t length);

typedef bool (*tScriptPreHandler)(UObject* self, FFrame* stack, void* result);
typedef void (*tScriptPostHandler)(UObject* self, FFrame* stack, void* result);
void SetScriptHandlers(tScriptPreHandler pre_handler, tScriptPostHandler post_handler);

}
