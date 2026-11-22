#pragma once
#include "ue3.hpp"

namespace Bots {
namespace Squad {

bool Resolve();
void Reset();
void Tick(UE3::UObject* game);
bool Placed(UE3::UObject* controller);

}
}
