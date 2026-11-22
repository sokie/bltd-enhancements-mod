#pragma once
#include <stdint.h>

namespace Bots {
namespace Maps {

bool Install(uintptr_t title_base);
void AddMenuEntries();
void RemoveMenuEntries();
void StampShaderCache();

}
}
