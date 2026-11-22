#include <winsock2.h>
#include <Windows.h>
#include "engine-fixes.hpp"
#include "../loader/hook.hpp"
#include <string.h>

namespace EngineFixes {

static const uint32_t RVA_COVER_SLOT_READ = 0x009A7EE3;
static const uint8_t COVER_SLOT_READ_BYTES[] = { 0x8B, 0x88, 0xA8, 0x02, 0x00, 0x00 };
static const uint32_t RVA_COVER_SLOT_CONTINUE = 0x009A7EE9;
static const uint32_t RVA_COVER_SLOT_MISSING = 0x009A7EFF;

static uint32_t cover_slot_continue = 0;
static uint32_t cover_slot_missing = 0;

static __declspec(naked) void CoverSlotNullCheck()
{
	__asm {
		test eax, eax
		jz missing
		mov ecx, dword ptr [eax + 0x2A8]
		jmp dword ptr [cover_slot_continue]
	missing:
		jmp dword ptr [cover_slot_missing]
	}
}

static const uint32_t RVA_MOVIE_CLOSE = 0x002B3C60;
static const uint8_t MOVIE_CLOSE_BYTES[] = { 0x6A, 0xFF, 0x68, 0x30, 0x80, 0x06, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t RVA_FLUSH_RENDERING_COMMANDS = 0x001EEC70;
static const uint32_t RVA_IS_GARBAGE_COLLECTING = 0x0111AB90;

typedef void (__stdcall *tMovieClose)(void* decoder);
static tMovieClose movie_close_original = 0;
static void (*flush_rendering_commands)() = 0;
static const volatile uint32_t* is_garbage_collecting = 0;

// Close queues a render command that holds the decoder with no fence, so garbage collection can free the decoder first.
static void __stdcall MovieCloseHook(void* decoder)
{
	movie_close_original(decoder);
	if (*is_garbage_collecting) {
		flush_rendering_commands();
	}
}

static const uint32_t RVA_MOVIE_VOLUME_EVENT = 0x001357B0;
static const uint8_t MOVIE_VOLUME_EVENT_BYTES[] = { 0x51, 0x8B, 0x15, 0x98, 0x3D, 0x5A, 0x01, 0xD9, 0xEE };
static const uint32_t RVA_IS_IN_GAME_THREAD = 0x001D8410;

typedef float (__stdcall *tMovieVolumeEvent)(void* controller);
static tMovieVolumeEvent movie_volume_original = 0;
static uint32_t (*is_in_game_thread)() = 0;
static volatile float movie_volume = 1.0f;

// ProcessEvent keeps its call depth in a global, so the render thread must not run the volume event.
static float __stdcall MovieVolumeHook(void* controller)
{
	if (is_in_game_thread()) {
		movie_volume = movie_volume_original(controller);
	}
	return movie_volume;
}

static const uint32_t RVA_ENGINE_EXIT = 0x009508B0;
static const uint8_t ENGINE_EXIT_BYTES[] = { 0x6A, 0xFF, 0x68, 0x60, 0xEF, 0x07, 0x01, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint32_t RVA_FULL_SCREEN_MOVIE = 0x011BD668;
static const uint32_t MOVIE_STOP_SLOT = 7;

typedef void (__stdcall *tEngineExit)(void* loop);
typedef void (__thiscall *tMovieStop)(void* player, float delay, uint32_t wait, uint32_t force);
static tEngineExit engine_exit_original = 0;
static void* const* full_screen_movie = 0;

// The game frees the textures of the loading movie before it stops the movie.
static void __stdcall EngineExitHook(void* loop)
{
	void* player = *full_screen_movie;
	if (player) {
		(*(tMovieStop**)player)[MOVIE_STOP_SLOT](player, 0.0f, 0, 1);
		flush_rendering_commands();
	}
	engine_exit_original(loop);
}

bool Install(uintptr_t title_base)
{
	uint8_t* site = (uint8_t*)(title_base + RVA_COVER_SLOT_READ);
	if (memcmp(site, COVER_SLOT_READ_BYTES, sizeof(COVER_SLOT_READ_BYTES)) != 0) {
		return false;
	}
	cover_slot_continue = (uint32_t)(title_base + RVA_COVER_SLOT_CONTINUE);
	cover_slot_missing = (uint32_t)(title_base + RVA_COVER_SLOT_MISSING);
	PatchWithJump((DWORD)site, (void*)CoverSlotNullCheck);
	NopFill((uint32_t)site + 5, sizeof(COVER_SLOT_READ_BYTES) - 5);
	return true;
}

bool InstallMovieVolumeFix(uintptr_t title_base)
{
	BYTE* event = (BYTE*)(title_base + RVA_MOVIE_VOLUME_EVENT);
	if (memcmp(event, MOVIE_VOLUME_EVENT_BYTES, sizeof(MOVIE_VOLUME_EVENT_BYTES)) != 0) {
		return false;
	}
	is_in_game_thread = (uint32_t (*)())(title_base + RVA_IS_IN_GAME_THREAD);
	movie_volume_original = (tMovieVolumeEvent)DetourClassFunc(event, (BYTE*)MovieVolumeHook, sizeof(MOVIE_VOLUME_EVENT_BYTES));
	return movie_volume_original != 0;
}

void RefreshMovieVolume(void* controller)
{
	if (movie_volume_original) {
		MovieVolumeHook(controller);
	}
}

bool InstallMovieCloseFix(uintptr_t title_base)
{
	BYTE* close = (BYTE*)(title_base + RVA_MOVIE_CLOSE);
	if (memcmp(close, MOVIE_CLOSE_BYTES, sizeof(MOVIE_CLOSE_BYTES)) != 0) {
		return false;
	}
	flush_rendering_commands = (void (*)())(title_base + RVA_FLUSH_RENDERING_COMMANDS);
	is_garbage_collecting = (const volatile uint32_t*)(title_base + RVA_IS_GARBAGE_COLLECTING);
	movie_close_original = (tMovieClose)DetourClassFunc(close, (BYTE*)MovieCloseHook, sizeof(MOVIE_CLOSE_BYTES));
	return movie_close_original != 0;
}

bool InstallMovieExitFix(uintptr_t title_base)
{
	BYTE* engine_exit = (BYTE*)(title_base + RVA_ENGINE_EXIT);
	if (memcmp(engine_exit, ENGINE_EXIT_BYTES, sizeof(ENGINE_EXIT_BYTES)) != 0) {
		return false;
	}
	flush_rendering_commands = (void (*)())(title_base + RVA_FLUSH_RENDERING_COMMANDS);
	full_screen_movie = (void* const*)(title_base + RVA_FULL_SCREEN_MOVIE);
	engine_exit_original = (tEngineExit)DetourClassFunc(engine_exit, (BYTE*)EngineExitHook, sizeof(ENGINE_EXIT_BYTES));
	return engine_exit_original != 0;
}

}
