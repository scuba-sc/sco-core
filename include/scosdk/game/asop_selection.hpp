// SPDX-License-Identifier: GPL-3.0-only
#ifndef SCOSDK_GAME_ASOP_SELECTION_HPP
#define SCOSDK_GAME_ASOP_SELECTION_HPP
#include "../plugin.hpp"
#include <sc_asop_selection.h>
#define SCOSDK_ASOP_HAS(member) (table_ && table_->size>=offsetof(sc_asop_selection_v1,member)+sizeof(table_->member) && table_->member)
namespace sco::sdk::game {
class AsopSelection {
public:
    sco_result Open(const sco_api* api,sco_plugin* self) noexcept {
        table_=nullptr;self_=self;
        if(!api || !self)return SCO_BAD_ARG;
        if(api->size<offsetof(sco_api,query_service)+sizeof(api->query_service) || !api->query_service)return SCO_UNAVAILABLE;
        const void* raw=nullptr;
        const auto result=api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw);
        if(result!=SCO_OK)return result;
        table_=static_cast<const sc_asop_selection_v1*>(raw);
        return table_?SCO_OK:SCO_UNAVAILABLE;
    }
    sco_result Open(const Plugin& p) noexcept {return Open(p.Api(),p.Self());}
    sco_result State(sc_asop_selection_state& out) const noexcept {
        return SCOSDK_ASOP_HAS(state)?table_->state(self_,&out):SCO_UNAVAILABLE;
    }
    sco_result Refresh() const noexcept {return SCOSDK_ASOP_HAS(refresh)?table_->refresh(self_):SCO_UNAVAILABLE;}
    sco_result Rows(const sc_asop_selection_read& read,sc_asop_selection_row* rows,uint32_t stride,uint32_t capacity,uint32_t& count,uint32_t& more) const noexcept {
        return SCOSDK_ASOP_HAS(rows)?table_->rows(self_,&read,rows,stride,capacity,&count,&more):SCO_UNAVAILABLE;
    }
    sco_result Acquire(uint64_t session,uint64_t& lease) const noexcept {return SCOSDK_ASOP_HAS(acquire)?table_->acquire(self_,session,&lease):SCO_UNAVAILABLE;}
    sco_result Release(uint64_t lease) const noexcept {return SCOSDK_ASOP_HAS(release)?table_->release(self_,lease):SCO_UNAVAILABLE;}
    sco_result Replace(uint64_t lease,const sc_asop_selection_request& request,uint64_t& id) const noexcept {
        return SCOSDK_ASOP_HAS(replace)?table_->replace(self_,lease,&request,&id):SCO_UNAVAILABLE;
    }
    sco_result Status(uint64_t lease,uint64_t id,sc_asop_selection_receipt& receipt) const noexcept {
        return SCOSDK_ASOP_HAS(status)?table_->status(self_,lease,id,&receipt):SCO_UNAVAILABLE;
    }
private:
    const sc_asop_selection_v1* table_=nullptr;
    sco_plugin* self_=nullptr;
};
}
#undef SCOSDK_ASOP_HAS
#endif
