#include "bots-internal.hpp"
#include "netversion.hpp"
#include "config.hpp"
#include "../loader/hook.hpp"
#include <stdint.h>
#include <string.h>

#define NET_LOG(level, format, ...) Log::Write(level, "[Net] " format, __VA_ARGS__)

namespace Bots {
namespace NetVersion {

static const uint32_t RVA_ENGINE_VERSION = 0x010F08D4;
static const uint32_t RVA_MIN_NET_VERSION = 0x010F08DC;
static const uint32_t RVA_HELLO_VERSION_OPERAND = 0x00870EEA;
static const uint32_t RVA_HELLO_MIN_VERSION_OPERAND = 0x00870EF6;
// UObject::StaticInit asserts GEngineMinNetVersion <= GEngineVersion, so the minimum goes up right after it.
static const uint32_t RVA_STATIC_INIT = 0x000A66D0;
static const int STATIC_INIT_DETOUR_LEN = 8;
static const uint8_t STATIC_INIT_PROLOGUE[STATIC_INIT_DETOUR_LEN] = { 0xA1, 0xDC, 0x08, 0x4F, 0x01, 0x83, 0xEC, 0x10 };
static const int32_t ENGINE_VERSION = 6165;
static const int32_t ENGINE_MIN_NET_VERSION = 5813;
static const int32_t MOD_NET_REVISION = 1;

static int32_t hello_version = ENGINE_VERSION + MOD_NET_REVISION;
static int32_t hello_min_version = ENGINE_MIN_NET_VERSION;
typedef void (__stdcall* tStaticInit)(void* unused_ecx);
static tStaticInit static_init_original = 0;
static int32_t* min_version_global = 0;

static bool PointOperand(uintptr_t operand, const int32_t* target)
{
	DWORD protection;
	if (!VirtualProtect((void*)operand, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &protection)) {
		return false;
	}
	*(uint32_t*)operand = (uint32_t)target;
	VirtualProtect((void*)operand, sizeof(uint32_t), protection, &protection);
	return true;
}

static bool OperandIs(uintptr_t operand, uint32_t expected)
{
	return *((uint8_t*)operand - 1) == 0x68 && *(uint32_t*)operand == expected;
}

static void __stdcall StaticInitHook(void* unused_ecx)
{
	static_init_original(unused_ecx);
	DWORD protection;
	if (VirtualProtect(min_version_global, sizeof(*min_version_global), PAGE_READWRITE, &protection)) {
		*min_version_global = ENGINE_VERSION + MOD_NET_REVISION;
		VirtualProtect(min_version_global, sizeof(*min_version_global), protection, &protection);
	}
}

bool Install(uintptr_t title_base)
{
	NetConfig config;
	LoadNetConfig(config);
	int32_t* version = (int32_t*)(title_base + RVA_ENGINE_VERSION);
	min_version_global = (int32_t*)(title_base + RVA_MIN_NET_VERSION);
	if (*version != ENGINE_VERSION || *min_version_global != ENGINE_MIN_NET_VERSION) {
		NET_LOG(LOG_LEVEL_WARN, "The engine versions are %d and %d, not %d and %d, the version gate stays off.", *version, *min_version_global, ENGINE_VERSION, ENGINE_MIN_NET_VERSION);
		return false;
	}
	if (!config.VersionGate) {
		NET_LOG(LOG_LEVEL_INFO, "%s", "Version gate off: a game without the module can join this host.");
		return true;
	}
	uintptr_t version_operand = title_base + RVA_HELLO_VERSION_OPERAND;
	uintptr_t min_version_operand = title_base + RVA_HELLO_MIN_VERSION_OPERAND;
	uint8_t* static_init = (uint8_t*)(title_base + RVA_STATIC_INIT);
	if (!OperandIs(version_operand, (uint32_t)version) || !OperandIs(min_version_operand, (uint32_t)min_version_global)
		|| memcmp(static_init, STATIC_INIT_PROLOGUE, STATIC_INIT_DETOUR_LEN) != 0) {
		NET_LOG(LOG_LEVEL_WARN, "%s", "The join handshake code does not match, the version gate stays off.");
		return false;
	}
	if (!PointOperand(version_operand, &hello_version) || !PointOperand(min_version_operand, &hello_min_version)) {
		NET_LOG(LOG_LEVEL_WARN, "%s", "The join handshake code cannot be written, the version gate stays off.");
		return false;
	}
	static_init_original = (tStaticInit)DetourClassFunc(static_init, (BYTE*)StaticInitHook, STATIC_INIT_DETOUR_LEN);
	NET_LOG(LOG_LEVEL_INFO, "Version gate on: this host takes clients of version %d and up, this game joins as version %d and takes hosts from version %d.", ENGINE_VERSION + MOD_NET_REVISION, hello_version, hello_min_version);
	return true;
}

}
}
