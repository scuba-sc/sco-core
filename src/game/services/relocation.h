#pragma once
#include "sco/runtime.h"
namespace sco::game::services {
Result StartRelocation();
void StopRelocation();
void ReleaseRelocationOwner(const void* owner);
}
