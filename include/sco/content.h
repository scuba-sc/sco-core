#pragma once
// Host-only lifecycle for sco.content. GPL-3.0-only; not a plugin SDK header.
#include "sco_content.h"
#include "sco/plugins.h"
#include "sco/runtime.h"
namespace sco::content {
// All operations game-thread-only. Start publishes an empty table before Discover
// so requires=sco.content resolves. Snapshot once, after ContentIndex::Build and
// before ANY plugin code. Refused packs are marked and removed from the index.
Result Start();
Result Snapshot(std::vector<plugins::Plugin>& list, plugins::ContentIndex& index);
void Stop();
const sco_content_v1* Table();
}
