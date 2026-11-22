#include "hook.hpp"
#include <string.h>

static void WriteCode(void* address, const void* bytes, size_t size)
{
	DWORD protection = 0;
	VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &protection);
	memcpy(address, bytes, size);
	VirtualProtect(address, size, protection, &protection);
	FlushInstructionCache(GetCurrentProcess(), address, size);
}

static void WriteJump(uint8_t* at, const void* target)
{
	uint8_t jump[5] = { 0xE9 };
	*(int32_t*)(jump + 1) = (int32_t)((const uint8_t*)target - (at + 5));
	WriteCode(at, jump, sizeof(jump));
}

void* DetourClassFunc(BYTE* src, const BYTE* dst, const int len)
{
	if (len < 8) {
		return 0;
	}
	uint8_t* trampoline = (uint8_t*)VirtualAlloc(0, len + 8, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!trampoline) {
		return 0;
	}
	trampoline[0] = 0x58;
	trampoline[1] = 0x59;
	trampoline[2] = 0x50;
	memcpy(trampoline + 3, src, len);
	WriteJump(trampoline + 3 + len, src + len);
	uint8_t* patch = new uint8_t[len];
	memset(patch, 0x90, len);
	patch[0] = 0x58;
	patch[1] = 0x51;
	patch[2] = 0x50;
	patch[3] = 0xE9;
	*(int32_t*)(patch + 4) = (int32_t)(dst - (src + 8));
	WriteCode(src, patch, len);
	delete[] patch;
	return trampoline;
}

void PatchWithJump(DWORD instruction_addr, void* new_function_ptr)
{
	WriteJump((uint8_t*)instruction_addr, new_function_ptr);
}

void NopFill(uint32_t address, int length)
{
	uint8_t* nops = new uint8_t[length];
	memset(nops, 0x90, length);
	WriteCode((void*)address, nops, length);
	delete[] nops;
}
