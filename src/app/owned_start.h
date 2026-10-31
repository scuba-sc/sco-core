// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/app.h"

namespace sco::app::detail {
// Host composition only: the caller must hold the matching startup transition
// until it publishes its own Running state or retains the failed session.
bool StartOwned(const Platform& platform, const OwnedShutdown::Transition& transition);
bool CopyOwnedData(const OwnedShutdown::Transition& transition, const std::shared_ptr<OwnedShutdown>& owner,
                   std::vector<plugins::Plugin>& list, plugins::ContentIndex& index);
void RetainFailedOwnedStartup(const std::shared_ptr<OwnedShutdown>& owner) noexcept;
}
