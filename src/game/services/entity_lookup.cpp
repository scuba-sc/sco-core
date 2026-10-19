// SPDX-License-Identifier: GPL-3.0-only
#include "entity_lookup.h"
#include <algorithm>
#include <cstring>
namespace sco::game::services {
namespace {
bool Text(const char* text) {
    if (!text) return false;
    const auto n = strnlen(text,96);
    if (!n || n==96) return false;
    for (size_t i=0;i<n;++i) if (static_cast<unsigned char>(text[i])<0x21 ||
        static_cast<unsigned char>(text[i])>0x7e) return false;
    return true;
}
struct Busy { bool& flag; explicit Busy(bool& f):flag(f){flag=true;} ~Busy(){flag=false;} };
template<class T> bool Clear(T* out) {
    if (!out || out->size<sizeof(T)) return false;
    const auto size=out->size; std::memset(out,0,sizeof(T)); out->size=size; return true;
}
}
sco_result EntityLookup::Admission() const {
    if (!backend_.on_thread || !backend_.on_thread()) return SCO_WRONG_THREAD;
    if (busy_) return SCO_FAILED;
    return stopped_ ? SCO_UNAVAILABLE : SCO_OK;
}
sco_result EntityLookup::Snapshot(sc_entity_class_page& page) {
    if (!backend_.classes) return SCO_UNAVAILABLE;
    page.size=sizeof(page); page.count=SC_ENTITIES_MAX_CLASSES;
    const auto r=backend_.classes(backend_.ctx,&page);
    if (stopped_) return SCO_UNAVAILABLE;
    if (r!=SCO_OK) return r;
    if (page.size!=sizeof(page) || page._pad || !page.epoch || page.more ||
        page.count>SC_ENTITIES_MAX_CLASSES) return SCO_FAILED;
    for(uint32_t i=0;i<page.count;++i) {
        const auto& c=page.records[i];
        if(c.size!=sizeof(c)||c._pad||c.epoch!=page.epoch||!c.class_id||!Text(c.name)) return SCO_FAILED;
        for(uint32_t j=0;j<i;++j) if(page.records[j].class_id==c.class_id ||
            std::strcmp(page.records[j].name,c.name)==0) return SCO_FAILED;
    }
    std::sort(page.records,page.records+page.count,[](const auto& a,const auto& b){return a.class_id<b.class_id;});
    return SCO_OK;
}
sco_result EntityLookup::Classes(uint64_t epoch,uint32_t offset,sc_entity_class_page* out) {
    if(!out||out->size<sizeof(*out)) return SCO_BAD_ARG;
    const auto cap=out->count; Clear(out);
    auto r=Admission(); if(r!=SCO_OK)return r;
    if(cap>SC_ENTITIES_MAX_CLASSES)return SCO_BAD_ARG;
    Busy hold(busy_); sc_entity_class_page page{};
    r=Snapshot(page); if(r!=SCO_OK)return r;
    if(epoch&&epoch!=page.epoch)return SCO_NOT_FOUND;
    if(offset>page.count)return SCO_BAD_ARG;
    out->epoch=page.epoch; out->count=std::min(cap,page.count-offset);
    out->more=page.count-offset-out->count;
    std::memcpy(out->records,page.records+offset,out->count*sizeof(page.records[0]));
    return SCO_OK;
}
sco_result EntityLookup::Class(const char* name,sc_entity_class_record* out) {
    if(!Clear(out))return SCO_BAD_ARG;
    auto r=Admission(); if(r!=SCO_OK)return r;
    if(!Text(name))return SCO_BAD_ARG;
    char copy[96]{}; std::memcpy(copy,name,std::strlen(name));
    Busy hold(busy_); sc_entity_class_page page{};
    r=Snapshot(page); if(r!=SCO_OK)return r;
    for(uint32_t i=0;i<page.count;++i) if(std::strcmp(copy,page.records[i].name)==0) {
        const auto size=out->size; *out=page.records[i]; out->size=size; return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
sco_result EntityLookup::Name(const char* name,uint64_t* out) {
    if(!out)return SCO_BAD_ARG;
    *out=0; auto r=Admission(); if(r!=SCO_OK)return r;
    if(!Text(name))return SCO_BAD_ARG;
    if(!backend_.name)return SCO_UNAVAILABLE;
    char copy[96]{}; std::memcpy(copy,name,std::strlen(name));
    // Canonical ASCII folding is public policy; backend must refuse ambiguous matches.
    for(auto& c:copy)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    Busy hold(busy_); uint64_t id=0; r=backend_.name(backend_.ctx,copy,&id);
    if(stopped_)return SCO_UNAVAILABLE;
    if(r!=SCO_OK)return r;
    if(!id)return SCO_FAILED;
    *out=id; return SCO_OK;
}
sco_result EntityLookup::Component(uint64_t id,uint32_t kind,sc_entity_component_record* out) {
    if(!Clear(out))return SCO_BAD_ARG;
    auto r=Admission(); if(r!=SCO_OK)return r;
    if(!id||(kind!=SC_ENTITY_COMPONENT_ACTOR&&kind!=SC_ENTITY_COMPONENT_INVENTORY))return SCO_BAD_ARG;
    if(!backend_.component)return SCO_UNAVAILABLE;
    Busy hold(busy_); sc_entity_component_record record{}; record.size=sizeof(record);
    r=backend_.component(backend_.ctx,id,kind,&record);
    if(stopped_)return SCO_UNAVAILABLE;
    if(r!=SCO_OK)return r;
    if(record.size!=sizeof(record)||record.entity_id!=id||record.kind!=kind||!record.generation)return SCO_FAILED;
    const auto size=out->size; *out=record; out->size=size; return SCO_OK;
}
}
