// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/owned_shutdown.h"
#include <memory>
namespace sco::detail {
// Reserve the process-global slot empty for a private owned session. The slot
// remains reserved on failure; no global snapshot or address is published.
bool ReserveEmptyHostRegistry(const std::shared_ptr<OwnedShutdown>& owner,
                              const OwnedShutdown::Transition& transition) noexcept;
bool HostRegistryReservedFor(const std::shared_ptr<OwnedShutdown>& owner) noexcept;
// Before runtime binding or other side effects only: undo this owner's empty
// reservation if the separate runtime reservation was refused.
void ReleaseUnusedHostRegistryReservation(const std::shared_ptr<OwnedShutdown>& owner) noexcept;
}
