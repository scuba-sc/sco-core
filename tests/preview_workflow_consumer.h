// GPL-3.0-only. Consumer policy uses public SDK tables only.
#pragma once
#include <sc_entities.h>
#include <sc_owned.h>
#include <sc_world_control.h>
#include <cstddef>
// Full member extents and callback checks are required for borrowed tables.
struct Preview {
    sco_plugin* self; const sc_entities_v1* entities; const sc_owned_v1* owned;
    const sc_world_control_v1* world; uint64_t id = 0, lease = 0;
    sco_result begin(uint64_t zone) {
        if (!entities || entities->size < offsetof(sc_entities_v1, spawn)+sizeof(entities->spawn) || !entities->spawn ||
            !world || world->size < offsetof(sc_world_control_v1, camera_acquire)+sizeof(world->camera_acquire) || !world->camera_acquire || world->size < offsetof(sc_world_control_v1, camera_release)+sizeof(world->camera_release) || !world->camera_release) return SCO_UNAVAILABLE;
        sco_result r=world->camera_acquire(self,&lease); if (r != SCO_OK) return r;
        const double p[3]{}, q[4]{0,0,0,1};
        r=entities->spawn(self,"SyntheticPreview",zone,p,q,&id);
        if (r!=SCO_OK) { auto cleanup=world->camera_release(self,lease); if (cleanup==SCO_OK) lease=0; else return cleanup; }
        return r;
    }
    sco_result move(uint64_t zone, sc_world_hit& copy) {
        if (!id || !world || world->size < offsetof(sc_world_control_v1, raycast_filtered)+sizeof(world->raycast_filtered) || !world->raycast_filtered ||
            !entities || entities->size < offsetof(sc_entities_v1, set_transform)+sizeof(entities->set_transform) || !entities->set_transform) return SCO_UNAVAILABLE;
        const double origin[3]{}, direction[3]{1,0,0}, q[4]{0,0,0,1}; copy={}; copy.size=sizeof(copy);
        auto r=world->raycast_filtered(self,zone,origin,direction,100,&id,1,&copy);
        return r==SCO_OK ? entities->set_transform(self,id,zone,copy.pos,q) : r;
    }
    sco_result retain() {
        if (!owned || owned->size < offsetof(sc_owned_v1, retain_build)+sizeof(owned->retain_build) || !owned->retain_build) return SCO_UNAVAILABLE;
        return owned->retain_build(self,id);
    }
    sco_result remove(uint64_t& ticket) {
        if (!owned || owned->size < offsetof(sc_owned_v1, remove_build)+sizeof(owned->remove_build) || !owned->remove_build) return SCO_UNAVAILABLE;
        auto r=owned->remove_build(self,id,&ticket); if(r==SCO_OK) id=0; return r;
    }
    sco_result cancel() {
        sco_result r=SCO_OK;
        if (id) { if (!entities || entities->size < offsetof(sc_entities_v1, despawn)+sizeof(entities->despawn) || !entities->despawn) r=SCO_UNAVAILABLE; else { r=entities->despawn(self,id); if(r==SCO_OK) id=0; } }
        if (lease) { if (!world || world->size < offsetof(sc_world_control_v1, camera_release)+sizeof(world->camera_release) || !world->camera_release) return SCO_UNAVAILABLE; auto c=world->camera_release(self,lease); if(c==SCO_OK) lease=0; if(r==SCO_OK) r=c; }
        return r;
    }
};

