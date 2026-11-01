// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Internal host startup entry points. A valid, explicit publication capability
// is required; public Start functions preserve their original behavior.
#include "sco/content.h"
#include "sco/ipc.h"
#include "sco/net/session.h"
#include "sco/settings.h"
#include "sco/storage.h"
#include "sco/surfaces.h"
#include "sco/ui.h"
#include "sco/widgets.h"
#ifndef SCO_KERNEL_ONLY
#include "sco/datacore_service.h"
#include "sco/game/private_session.h"
#endif

namespace sco::detail { class StartupPublish; }
namespace sco::ui::owned { Result Start(const sco::detail::StartupPublish& cap); }
namespace sco::surfaces::owned { Result Start(const sco::detail::StartupPublish& cap); }
namespace sco::widgets::owned { Result Start(const sco::detail::StartupPublish& cap); }
namespace sco::settings::owned { Result Start(const sco::detail::StartupPublish& cap); }
namespace sco::content::owned {
Result Start(const sco::detail::StartupPublish& cap);
Result Snapshot(std::vector<plugins::Plugin>& list, plugins::ContentIndex& index,
                const sco::detail::StartupPublish& cap);
}
namespace sco::storage::owned { Result Start(const Options& opts, const sco::detail::StartupPublish& cap); }
namespace sco::ipc::owned { Result Start(const Options& opts, const sco::detail::StartupPublish& cap); }
namespace sco::net::owned { Result Start(const ServiceOptions& opts, const sco::detail::StartupPublish& cap); }
#ifndef SCO_KERNEL_ONLY
namespace sco::datacore::service::owned { Result Start(const Options& opts, const sco::detail::StartupPublish& cap); }
namespace sco::game::private_session::owned { Result Start(const Options& opts, const sco::detail::StartupPublish& cap); }
#endif
