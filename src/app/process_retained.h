// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
#include "sco/app.h"

namespace sco::app::detail {
// Game host composition only (game::host::Composition::ProcessRetained). Starts the NORMAL, non-owned
// runtime with native game services allowed. It refuses before any exposure unless the platform carries a
// gameReadersQuiesced callback, no shutdownOwner and no image (the host already installed the registry),
// and the runtime is not owned. The public StartDetailed keeps refusing gameServices for everyone else.
// A runtime started here stops by closure only: Stop() closes admission, dispatches game.exit once and
// retains plugins, services, registry and backing; it returns Ok for "closed", never "retired", and a
// restart is refused.
StartResult StartProcessRetained(const Platform& platform);
}
