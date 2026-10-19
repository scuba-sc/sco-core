// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_container_contents.h>
namespace sco::sdk::game {
class ContainerContents {
    const sc_container_contents_v1* table_;
    sco_plugin* owner_;
public:
    ContainerContents(const sc_container_contents_v1* table, sco_plugin* owner)
        : table_(table), owner_(owner) {}
    sco_result snapshot(uint64_t container, sc_container_snapshot& out) const noexcept {
        if (!table_ || table_->size < offsetof(sc_container_contents_v1, snapshot) + sizeof(table_->snapshot)
            || !table_->snapshot) return SCO_UNAVAILABLE;
        return table_->snapshot(owner_, container, &out);
    }
};
}
