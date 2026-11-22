#pragma once
#include <winsock2.h>
#include <Windows.h>
#include <stdint.h>

// Detours a __thiscall function to a __stdcall hook that gets this as its first parameter. len must cover whole
// instructions and be at least 8.
void* DetourClassFunc(BYTE* src, const BYTE* dst, const int len);
void PatchWithJump(DWORD instruction_addr, void* new_function_ptr);
void NopFill(uint32_t address, int length);
