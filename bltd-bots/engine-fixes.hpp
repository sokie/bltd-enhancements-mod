#pragma once
#include <stdint.h>

namespace EngineFixes {

// Fixes a null cover slot read that crashes the game once AI teammates use cover.
bool Install(uintptr_t title_base);
bool InstallMovieCloseFix(uintptr_t title_base);
bool InstallMovieExitFix(uintptr_t title_base);
bool InstallMovieVolumeFix(uintptr_t title_base);
void RefreshMovieVolume(void* controller);

}
