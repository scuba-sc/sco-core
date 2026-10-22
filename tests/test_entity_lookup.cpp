// SPDX-License-Identifier: GPL-3.0-only
#include "../src/game/services/entity_lookup.h"
#include <cstdio>
#include <cstring>
#include <scosdk/game/entities.hpp>
#include <memory>
#include <stdexcept>
using namespace sco::game::services;
namespace {
int passed=0,failed=0,mode=0; bool thread=true;
EntityLookup* current=nullptr;
const void* offered=nullptr;
sco_result Query(const char*,uint32_t,const void** out){*out=offered;return SCO_OK;}
sco_result ClassTable(const char* n,sc_entity_class_record* out){return current->Class(n,out);}
sco_result PageTable(uint64_t e,uint32_t o,sc_entity_class_page* out){return current->Classes(e,o,out);}
sco_result NameTable(const char* n,uint64_t* out){return current->Name(n,out);}
sco_result ComponentTable(uint64_t id,uint32_t k,sc_entity_component_record* out){return current->Component(id,k,out);}
#define CHECK(x) do{if(x)++passed;else{++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
bool OnThread(){return thread;}
sco_result Classes(void*,EntityClassSnapshot* p){
    p->epoch=7;p->records.resize(2);
    for(unsigned i=0;i<2;++i){auto& r=p->records[i];r.size=sizeof(r);r.epoch=7;r.class_id=2-i;std::strcpy(r.name,i?"Alpha":"Beta");}
    if(mode==1)p->records[1].class_id=2;
    if(mode==2)p->records[1].epoch=8;
    if(mode==3)p->epoch=0;
    if(mode==5)current->Stop();
    return SCO_OK;
}
sco_result ManyClasses(void*,EntityClassSnapshot* p){
    if(mode==9)throw std::bad_alloc{};
    if(mode==10)throw std::runtime_error("provider failure");
    p->epoch=17;p->records.resize(130);
    for(unsigned i=0;i<130;++i){auto& r=p->records[i];r.size=sizeof(r);r.epoch=17;r.class_id=130-i;std::snprintf(r.name,sizeof(r.name),"Class%03u",130-i);}
    if(mode==6)p->records[129].class_id=p->records[0].class_id;
    if(mode==7)std::strcpy(p->records[129].name,p->records[0].name);
    if(mode==8)p->records[129].epoch=18;
    return SCO_OK;
}
sco_result Name(void*,const char* name,uint64_t* id){
    if(std::strcmp(name,"duplicate")==0)return SCO_FAILED;
    if(std::strcmp(name,"pilot")!=0)return SCO_NOT_FOUND;
    *id=42;return SCO_OK;
}
sco_result Component(void*,uint64_t id,uint32_t kind,sc_entity_component_record* out){
    if(id!=42)return SCO_NOT_FOUND;
    out->kind=kind;out->entity_id=id;out->generation=mode==4?0:9;return SCO_OK;
}
}
int main(){
    EntityLookup lookup({nullptr,OnThread,Classes,Name,Component});
    sc_entity_class_page page{};page.size=sizeof(page);page.count=1;
    CHECK(lookup.Classes(0,0,&page)==SCO_OK&&page.count==1&&page.more==1&&page.records[0].class_id==1);
    page.count=64;CHECK(lookup.Classes(8,0,&page)==SCO_NOT_FOUND&&page.count==0);
    page.count=0;CHECK(lookup.Classes(7,0,&page)==SCO_OK&&page.more==2);
    sc_entity_class_record record{};record.size=sizeof(record);
    CHECK(lookup.Class("Alpha",&record)==SCO_OK&&record.class_id==1);
    CHECK(lookup.Class("alpha",&record)==SCO_NOT_FOUND&&record.class_id==0);
    for(mode=1;mode<=3;++mode){page.count=64;CHECK(lookup.Classes(0,0,&page)==SCO_FAILED&&page.count==0&&page.epoch==0);}
    mode=0;uint64_t id=9;CHECK(lookup.Name("PILOT",&id)==SCO_OK&&id==42);
    CHECK(lookup.Name("duplicate",&id)==SCO_FAILED&&id==0);
    sc_entity_component_record component{};component.size=sizeof(component);
    CHECK(lookup.Component(42,SC_ENTITY_COMPONENT_ACTOR,&component)==SCO_OK&&component.generation==9);
    CHECK(lookup.Component(42,99,&component)==SCO_BAD_ARG&&component.generation==0);
    mode=4;CHECK(lookup.Component(42,SC_ENTITY_COMPONENT_ACTOR,&component)==SCO_FAILED);mode=0;
    thread=false;CHECK(lookup.Name("pilot",&id)==SCO_WRONG_THREAD&&id==0);thread=true;
    lookup.Stop();CHECK(lookup.Name("pilot",&id)==SCO_UNAVAILABLE);
    EntityLookup unbound({nullptr,OnThread});CHECK(unbound.Name("pilot",&id)==SCO_UNAVAILABLE);
    struct Future{sc_entity_class_record record;uint64_t tail;} future{};
    future.record.size=sizeof(future);future.tail=123;
    EntityLookup fresh({nullptr,OnThread,Classes,Name,Component});
    CHECK(fresh.Class("Alpha",&future.record)==SCO_OK&&future.tail==123&&future.record.size==sizeof(future));
    current=&fresh;
    sc_entities_v1 table{};table.size=sizeof(table);table.list_classes=PageTable;
    table.find_class=ClassTable;table.find_by_name=NameTable;table.component=ComponentTable;
    offered=&table;sco_api api{};api.size=sizeof(api);api.query_service=Query;
    int owner=0;sco::sdk::game::Entities consumer;
    CHECK(consumer.Open(&api,reinterpret_cast<sco_plugin*>(&owner))==SCO_OK);
    CHECK(consumer.FindByName("PILOT",id)==SCO_OK&&id==42);
    CHECK(consumer.FindClass("Alpha",record)==SCO_OK);
    CHECK(consumer.Component(42,SC_ENTITY_COMPONENT_INVENTORY,component)==SCO_OK);
    for(size_t extent:{size_t(96),size_t(104),size_t(112),size_t(120)}){
        auto bytes=std::make_unique<unsigned char[]>(extent+7);
        std::memcpy(bytes.get(),&table,extent+7);uint32_t size=static_cast<uint32_t>(extent+7);
        std::memcpy(bytes.get(),&size,4);offered=bytes.get();
        CHECK(consumer.Open(&api,reinterpret_cast<sco_plugin*>(&owner))==SCO_OK);
        if(extent==96)CHECK(consumer.ListClasses(0,0,page)==SCO_UNAVAILABLE);
        if(extent==104)CHECK(consumer.FindClass("Alpha",record)==SCO_UNAVAILABLE);
        if(extent==112)CHECK(consumer.FindByName("pilot",id)==SCO_UNAVAILABLE&&id==0);
        if(extent==120)CHECK(consumer.Component(42,1,component)==SCO_UNAVAILABLE);
    }
    mode=5;page.count=64;
    CHECK(fresh.Classes(0,0,&page)==SCO_UNAVAILABLE&&page.count==0&&page.epoch==0);
    mode=0;EntityLookup many({nullptr,OnThread,ManyClasses,Name,Component});
    page.count=64;CHECK(many.Classes(0,0,&page)==SCO_OK&&page.count==64&&page.more==66&&page.records[0].class_id==1);
    page.count=64;CHECK(many.Classes(17,64,&page)==SCO_OK&&page.count==64&&page.more==2&&page.records[0].class_id==65);
    page.count=64;CHECK(many.Classes(17,128,&page)==SCO_OK&&page.count==2&&page.more==0&&page.records[1].class_id==130);
    record.size=sizeof(record);CHECK(many.Class("Class130",&record)==SCO_OK&&record.class_id==130);
    page.count=64;CHECK(many.Classes(18,0,&page)==SCO_NOT_FOUND&&page.count==0&&page.epoch==0);
    page.count=64;CHECK(many.Classes(17,130,&page)==SCO_OK&&page.count==0&&page.more==0);
    page.count=64;CHECK(many.Classes(17,131,&page)==SCO_BAD_ARG&&page.count==0);
    for(mode=6;mode<=8;++mode){page.count=64;CHECK(many.Classes(0,0,&page)==SCO_FAILED&&page.count==0&&page.epoch==0);}
    mode=9;page.count=64;
    CHECK(many.Classes(0,0,&page)==SCO_TOO_MANY&&page.count==0&&page.epoch==0);
    CHECK(many.Class("Class130",&record)==SCO_TOO_MANY&&record.class_id==0);
    mode=10;page.count=64;
    CHECK(many.Classes(0,0,&page)==SCO_FAILED&&page.count==0&&page.epoch==0);
    mode=0;page.count=64;
    CHECK(many.Classes(0,0,&page)==SCO_OK&&page.count==64);
    std::printf("entity lookup: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
