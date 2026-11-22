#include "loader.hpp"
#include "log.hpp"
#include "version.h"
#include "xlive-patch.hpp"
#include "../bltd-bots/bots.hpp"
#include "../bltd-bots/engine-fixes.hpp"
#include "../bltd-bots/maps.hpp"
#include "../bltd-bots/netversion.hpp"

#define ENTRY_LOG(level, format, ...) Log::Write(level, "[Bots] " format, __VA_ARGS__)

static HMODULE this_module = 0;

// Runs under the loader lock, so it only writes code and opens the log.
static void Install()
{
	Loader::SetModule(this_module);
	Log::Open(Loader::LogPath());
	ENTRY_LOG(LOG_LEVEL_INFO, "bltd-bots " BLTD_VERSION_TEXT " (" BLTD_BUILD_STAMP ") loaded from %ls.", Loader::ModulePath().c_str());
	HMODULE title = GetModuleHandleW(0);
	if (!Loader::TitleMatches(title)) {
		ENTRY_LOG(LOG_LEVEL_ERROR, "%s", "The game is not BLTD.exe 1.0 (Steam), the bots are not installed.");
		return;
	}
	XlivePatch::Install();
	if (!EngineFixes::Install((uintptr_t)title)) {
		ENTRY_LOG(LOG_LEVEL_ERROR, "%s", "The cover slot code does not match, the bots are not installed.");
		return;
	}
	if (!EngineFixes::InstallMovieCloseFix((uintptr_t)title)) {
		ENTRY_LOG(LOG_LEVEL_WARN, "%s", "The movie close code does not match, the fix for that crash is off.");
	}
	if (!EngineFixes::InstallMovieExitFix((uintptr_t)title)) {
		ENTRY_LOG(LOG_LEVEL_WARN, "%s", "The engine exit code does not match, the fix for that crash is off.");
	}
	if (!EngineFixes::InstallMovieVolumeFix((uintptr_t)title)) {
		ENTRY_LOG(LOG_LEVEL_WARN, "%s", "The movie volume code does not match, the fix for that crash is off.");
	}
	if (Bots::Install((uintptr_t)title)) {
		Bots::Maps::Install((uintptr_t)title);
		Bots::NetVersion::Install((uintptr_t)title);
	}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		this_module = module;
		DisableThreadLibraryCalls(module);
		Install();
	}
	else if (reason == DLL_PROCESS_DETACH) {
		Bots::Maps::RemoveMenuEntries();
		Bots::Maps::StampShaderCache();
	}
	return TRUE;
}
