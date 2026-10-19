// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_entities.h>
namespace sco::game::services {
// Backend provides full copied registry or fails TOO_MANY; never claims an
// observed subset is complete. Identities/generations are host-issued, nonzero
// and never pointer encodings. Native adapter absent until independently grounded.
struct EntityLookupBackend {
    void* ctx = nullptr;
    bool (*on_thread)() = nullptr;
    sco_result (*classes)(void*, sc_entity_class_page*) = nullptr;
    sco_result (*name)(void*, const char*, uint64_t*) = nullptr;
    sco_result (*component)(void*, uint64_t, uint32_t, sc_entity_component_record*) = nullptr;
};
class EntityLookup {
public:
    explicit EntityLookup(EntityLookupBackend backend = {}) : backend_(backend) {}
    sco_result Classes(uint64_t, uint32_t, sc_entity_class_page*);
    sco_result Class(const char*, sc_entity_class_record*);
    sco_result Name(const char*, uint64_t*);
    sco_result Component(uint64_t, uint32_t, sc_entity_component_record*);
    void Stop() { stopped_ = true; }
private:
    sco_result Snapshot(sc_entity_class_page&);
    sco_result Admission() const;
    EntityLookupBackend backend_;
    bool busy_ = false, stopped_ = false;
};
}
