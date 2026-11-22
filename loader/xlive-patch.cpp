#include "log.hpp"
#include "xlive-patch.hpp"
#include <winsock2.h>
#include <Windows.h>
#include <stdint.h>
#include <string.h>

#define XLIVE_LOG(level, format, ...) Log::Write(level, "[Bots] " format, __VA_ARGS__)

namespace XlivePatch {

static const uint8_t CHECK_GATE[] = { 0x74, 0x14, 0xFF, 0x75, 0x14, 0x8B, 0xCE, 0xFF, 0x75, 0x10, 0x53, 0xE8 };
static const uint8_t JMP_SHORT = 0xEB;

static int PatchGates(uint8_t* base, const IMAGE_NT_HEADERS* nt)
{
	int patched = 0;
	const IMAGE_SECTION_HEADER* section = (const IMAGE_SECTION_HEADER*)
		((const uint8_t*)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
		size_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
		if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !section->SizeOfRawData || size < sizeof(CHECK_GATE)) {
			continue;
		}
		uint8_t* code = base + section->VirtualAddress;
		size_t at = 0;
		while (at + sizeof(CHECK_GATE) <= size) {
			uint8_t* hit = (uint8_t*)memchr(code + at, CHECK_GATE[0], size - sizeof(CHECK_GATE) + 1 - at);
			if (!hit) {
				break;
			}
			at = hit - code;
			if (memcmp(hit, CHECK_GATE, sizeof(CHECK_GATE)) == 0) {
				DWORD protection = 0;
				if (VirtualProtect(hit, 1, PAGE_EXECUTE_READWRITE, &protection)) {
					*hit = JMP_SHORT;
					VirtualProtect(hit, 1, protection, &protection);
					FlushInstructionCache(GetCurrentProcess(), hit, 1);
					XLIVE_LOG(LOG_LEVEL_INFO, "Patched xlive.dll at xlive.dll+0x%x.", (uint32_t)(section->VirtualAddress + at));
					patched++;
				}
				else {
					XLIVE_LOG(LOG_LEVEL_WARN, "The xlive.dll site at xlive.dll+0x%x is not writable (error %u).", (uint32_t)(section->VirtualAddress + at), GetLastError());
				}
			}
			at++;
		}
	}
	return patched;
}

void Install()
{
	HMODULE module = GetModuleHandleW(L"xlive.dll");
	if (!module) {
		XLIVE_LOG(LOG_LEVEL_WARN, "%s", "xlive.dll is not loaded, its memory check stays on.");
		return;
	}
	uint8_t* base = (uint8_t*)module;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return;
	}
	const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return;
	}
	if (!PatchGates(base, nt)) {
		XLIVE_LOG(LOG_LEVEL_INFO, "%s", "xlive.dll has no memory check of this shape, nothing to turn off.");
	}
}

}
