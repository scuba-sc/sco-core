// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/runtime.h"
#include <sc_container_contents.h>
namespace sco::game::services {
// Host-owned context and callback must outlive service stop. The callback reads
// into service-owned storage only; it never receives a caller's buffer.
struct ContainerContentsBackend {
    void* ctx = nullptr;
    sco_result (*snapshot)(void*, uint64_t, sc_container_snapshot*) = nullptr;
};
Result StartContainerContents(const ContainerContentsBackend& backend = {});
void StopContainerContents();
}
