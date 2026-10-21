// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "../src/game/services/datacore_live_engine.h"
#include "../src/game/services/datacore_live.h"
#include "sco/host.h"
#include "sco/game/pak.h"
#include "sco/hook.h"
#include "sco/vfs.h"
#include "../src/api/internal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#if defined(_WIN32) && defined(_MSC_VER)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
alignas(16) uintptr_t ownerVtable[8]{};
alignas(16) uintptr_t ownerSlot = 0;
alignas(16) uint8_t owner[0x400]{};
alignas(16) uint8_t header[0x78]{};
alignas(16) uint8_t structs[32]{};
alignas(16) uint8_t properties[36]{};
alignas(16) uint8_t records[0x48]{};
alignas(16) uint8_t boolPool[1]{1};
alignas(16) uint8_t int8Pool[1]{0x81};
alignas(16) uint8_t int16Pool[2]{0x34, 0x12};
alignas(16) uint8_t int32Pool[4]{1, 2, 3, 4};
alignas(16) uint8_t int64Pool[8]{1, 2, 3, 4, 5, 6, 7, 8};
alignas(16) uint8_t uint8Pool[1]{9};
alignas(16) uint8_t uint16Pool[2]{10, 11};
alignas(16) uint8_t uint32Pool[4]{12, 13, 14, 15};
alignas(16) uint8_t uint64Pool[8]{16, 17, 18, 19, 20, 21, 22, 23};
alignas(16) uint8_t floatPool[4]{24, 25, 26, 27};
alignas(16) uint8_t doublePool[8]{28, 29, 30, 31, 32, 33, 34, 35};
alignas(16) uint8_t guidPool[16]{36, 37, 38, 39, 40, 41, 42, 43,
                                 44, 45, 46, 47, 48, 49, 50, 51};
alignas(16) uint8_t textBlob[5]{'h', 'e', 'l', 'l', 'o'};

template<class T>
void Put(uint8_t* bytes, size_t offset, T value) {
    std::memcpy(bytes + offset, &value, sizeof(value));
}

bool admitted = true;
bool Admitted() { return admitted; }
#if defined(_WIN32) && defined(_MSC_VER)
unsigned lifecycleCalls=0;
void LifecycleBody(uintptr_t) {
    sco::game::datacore_live::LifecycleGate::ReadLease read;
    assert(!sco::game::datacore_live::ProcessLifecycleGate().EnterRead(read));
    ++lifecycleCalls;
}
uintptr_t LoaderBody(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t) {return 1;}
uintptr_t DeletingBody(uintptr_t value,uint32_t) {LifecycleBody(value);return value;}
template<class F> void* Stub(F body) {
    auto* code=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(code);
    std::memset(code,0x90,16);code[16]=0xff;code[17]=0x25;std::memset(code+18,0,4);
    std::memcpy(code+22,&body,sizeof(body));FlushInstructionCache(GetCurrentProcess(),code,30);
    return code;
}
std::shared_ptr<const sco::vfs::Table> EmptyMounts() {return {};}
#endif
void BuildFixture() {
    ownerSlot = reinterpret_cast<uintptr_t>(owner);
    ownerVtable[1] = 0x10000;
    ownerVtable[4] = 0x10008;
    Put(reinterpret_cast<uint8_t*>(owner), 0, reinterpret_cast<uintptr_t>(ownerVtable));
    Put(reinterpret_cast<uint8_t*>(owner), 0x1d0, reinterpret_cast<uintptr_t>(header));
    Put(reinterpret_cast<uint8_t*>(owner), 0x1d8, reinterpret_cast<uintptr_t>(structs));
    Put(reinterpret_cast<uint8_t*>(owner), 0x1e0, reinterpret_cast<uintptr_t>(properties));
    Put(owner, 0x1f0, reinterpret_cast<uintptr_t>(records));
    Put(owner, 0x1c8, uint32_t{2});
    Put(reinterpret_cast<uint8_t*>(owner), 0x200, reinterpret_cast<uintptr_t>(boolPool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x208, reinterpret_cast<uintptr_t>(int8Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x210, reinterpret_cast<uintptr_t>(int16Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x218, reinterpret_cast<uintptr_t>(int32Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x220, reinterpret_cast<uintptr_t>(int64Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x228, reinterpret_cast<uintptr_t>(uint8Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x230, reinterpret_cast<uintptr_t>(uint16Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x238, reinterpret_cast<uintptr_t>(uint32Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x240, reinterpret_cast<uintptr_t>(uint64Pool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x248, reinterpret_cast<uintptr_t>(floatPool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x250, reinterpret_cast<uintptr_t>(doublePool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x258, reinterpret_cast<uintptr_t>(guidPool));
    Put(reinterpret_cast<uint8_t*>(owner), 0x278, reinterpret_cast<uintptr_t>(textBlob));
    Put(header, 0x10, uint32_t{2});
    Put(header, 0x14, uint32_t{3});
    Put(header, 0x20, uint32_t{2});
    const uint32_t countOffsets[]{0x24, 0x28, 0x2c, 0x30, 0x34, 0x38,
                                  0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50};
    for (uint32_t offset : countOffsets) Put(header, offset, uint32_t{1});
    Put(header, 0x70, uint32_t{5});

    // One struct with one own field, no parent (-1).
    Put(structs, 4, uint32_t{0xffffffffu});
    Put(structs, 8, uint16_t{1});
    Put(structs, 10, uint16_t{0});
    Put(structs, 20, int32_t{0});
    Put(structs, 24, uint16_t{2});
    Put(structs, 26, uint16_t{1});
    const uint8_t firstGuid[16]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t secondGuid[16]{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    std::memcpy(records + 0x10, firstGuid, 16);
    Put(records, 0x0c, uint32_t{1});
    std::memcpy(records + 0x24 + 0x10, secondGuid, 16);
    Put(records, 0x24 + 0x0c, uint32_t{1});
}

void TestEngineThroughPublishedService(const char* mode) {
    using namespace sco::game::services::datacore_live;
    using sco::game::datacore_live::ProcessLifecycleGate;

    BuildFixture();
    EngineRows rows{reinterpret_cast<uintptr_t>(&ownerSlot),
                    reinterpret_cast<uintptr_t>(ownerVtable),
                    ownerVtable[1], ownerVtable[4], &ProcessLifecycleGate(), Admitted};
#if defined(_WIN32) && defined(_MSC_VER)
    if(mode && (std::strcmp(mode,"hooks")==0 || std::strcmp(mode,"hook-failure")==0 || std::strcmp(mode,"hook-rollback")==0)) {
        auto* loader=Stub(&LoaderBody);auto* reset=Stub(&LifecycleBody);auto* destructor=Stub(&LifecycleBody);
        auto* deleting=Stub(&DeletingBody);
        alignas(16) uintptr_t pakObject=0;uintptr_t pakPointer=reinterpret_cast<uintptr_t>(&pakObject);
        sco::game::pak::Options options;options.mounts=EmptyMounts;
        assert(sco::game::pak::Enable(options,{loader,&pakPointer})==sco::Result::Ok);
        rows.reset=reinterpret_cast<uintptr_t>(reset);ownerVtable[1]=rows.reset;
        assert(!PrepareEngine(rows,{reset,destructor,deleting},false));
        const size_t before=sco::hook::DetourCount();
        if(std::strcmp(mode,"hook-failure")==0) {
            DWORD old=0;assert(VirtualProtect(destructor,4096,PAGE_EXECUTE_READ,&old));
            // Already owned second target refuses before installing the first.
            void* original=nullptr;assert(sco::hook::InstallDetour(destructor,16,reinterpret_cast<void*>(&LifecycleBody),&original)==sco::hook::Error::None);
            assert(!PrepareEngine(rows,{reset,destructor,deleting},true));
            assert(!sco::hook::IsHooked(reset) && !EnginePrepared());
        } else if(std::strcmp(mode,"hook-rollback")==0) {
            rows.ownerSlot=0; // Binding rejects after all three detours were committed.
            assert(!PrepareEngine(rows,{reset,destructor,deleting},true));
            assert(sco::hook::DetourCount()==before);
            assert(!sco::hook::IsHooked(reset) && !sco::hook::IsHooked(destructor));
            assert(!sco::hook::IsHooked(deleting));
            assert(!EnginePrepared() && !PrepareEngine(rows,{reset,destructor,deleting},true));
        } else {
            assert(PrepareEngine(rows,{reset,destructor,deleting},true) && EnginePrepared());
            assert(sco::hook::DetourCount()==before+3);
            auto invokeReset=reinterpret_cast<void(*)(uintptr_t)>(reset);
            auto invokeDestructor=reinterpret_cast<void(*)(uintptr_t)>(destructor);
            std::thread resetThread;
            {
                sco::game::datacore_live::LifecycleGate::ReadLease read;
                assert(ProcessLifecycleGate().EnterRead(read));
                resetThread=std::thread([&]{invokeReset(ownerSlot);});
                assert(ProcessLifecycleGate().WaitForQueuedWriterForTesting(std::chrono::seconds(5)));
                assert(lifecycleCalls==0); // Original cannot run until copied reads finish.
            }
            resetThread.join();
            invokeDestructor(ownerSlot);
            auto invokeDeleting=reinterpret_cast<uintptr_t(*)(uintptr_t,uint32_t)>(deleting);
            assert(invokeDeleting(ownerSlot,1)==ownerSlot);
            assert(lifecycleCalls==3 && !EngineBackend().ready());
        }
        std::thread disableThread;
        {
            sco::game::datacore_live::LifecycleGate::ReadLease read;
            assert(ProcessLifecycleGate().EnterRead(read));
            disableThread=std::thread([]{sco::game::pak::Disable();});
            assert(ProcessLifecycleGate().WaitForQueuedWriterForTesting(std::chrono::seconds(5)));
            assert(sco::game::pak::Enabled());
        }
        disableThread.join();
        assert(!sco::game::pak::Enabled());
        sco::hook::RemoveAll();ClearEngine();return;
    }
#endif
    assert(ConfigureEngine(rows));
    const Backend backend = EngineBackend();
    assert(backend.verified && backend.write);

    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"datacore-live-engine-test"});
    auto* self = sco::host::NewPlugin("datacore_live_engine_tester");
    assert(Start(&backend) == SCO_OK);
    const void* table = nullptr;
    assert(api->query_service(SC_DATACORE_LIVE_SERVICE_NAME,
                              SC_DATACORE_LIVE_SERVICE_VERSION, &table) == SCO_OK);
    const auto* dc = static_cast<const sc_datacore_live_v1*>(table);

    assert(dc->is_ready(self) == SCO_OK);
    const uint8_t* expected[]{boolPool,int8Pool,int16Pool,int32Pool,int64Pool,uint8Pool,uint16Pool,uint32Pool,uint64Pool,floatPool,doublePool,guidPool,textBlob};
    const size_t widths[]{1,1,2,4,8,1,2,4,8,4,8,16,5};
    for (uint32_t type = 0; type < SC_DC_POOL_COUNT; ++type) {
        sc_datacore_pool_stats stats{}; stats.size=sizeof(stats);
        assert(dc->get_pool_stats(self,type,&stats)==SCO_OK);
        assert(stats.total_bytes==widths[type]);
        uint8_t copy[16]{}; size_t count=0;
        assert(dc->read_pool(self,type,0,copy,widths[type],&count)==SCO_OK);
        assert(count==widths[type] && std::memcmp(copy,expected[type],count)==0);
        std::memset(copy,0xa5,sizeof(copy));
        assert(dc->read_pool(self,type,UINT64_MAX,copy,1,&count)==SCO_BAD_ARG && count==0 && copy[0]==0xa5);
    }
    sc_datacore_live_record_info record{}; record.size=sizeof(record);
    assert(dc->find_record(self,records+0x10,&record)==SCO_OK && record.field_count==3 && record.struct_type==1 && record.pool_offset==0);
    assert(dc->find_record(self,records+0x24+0x10,&record)==SCO_OK && record.pool_offset==0x24);
    uint8_t absent[16]{}; assert(dc->find_record(self,absent,&record)==SCO_NOT_FOUND);
    size_t count=1; SetWritesEnabled(true);
    assert(dc->write_pool(self,SC_DC_POOL_BOOL,0,boolPool,1,&count)==SCO_UNAVAILABLE && count==0);
    admitted=false; assert(dc->is_ready(self)==SCO_UNAVAILABLE); admitted=true;
    if (mode) {
#if defined(_WIN32) && defined(_MSC_VER)
        if (std::strncmp(mode,"fault-",6)==0) {
            void* denied=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
            assert(denied);
            MEMORY_BASIC_INFORMATION page{};
            assert(VirtualQuery(denied,&page,sizeof(page))==sizeof(page) && page.Protect==PAGE_NOACCESS);
            if (std::strcmp(mode,"fault-snapshot")==0) {
                Put(owner,0x1d0,reinterpret_cast<uintptr_t>(denied));
                uintptr_t installed=0; std::memcpy(&installed,owner+0x1d0,sizeof(installed));
                assert(installed==reinterpret_cast<uintptr_t>(denied));
                assert(!backend.ready());
                Put(owner,0x1d0,reinterpret_cast<uintptr_t>(header));
            } else if (std::strcmp(mode,"fault-copy")==0) {
                Put(owner,0x200,reinterpret_cast<uintptr_t>(denied));
                uint8_t copy=0xa5; count=1;
                assert(dc->read_pool(self,SC_DC_POOL_BOOL,0,&copy,1,&count)==SCO_FAILED && copy==0xa5 && count==0);
                Put(owner,0x200,reinterpret_cast<uintptr_t>(boolPool));
            } else {
                assert(std::strcmp(mode,"fault-record")==0);
                Put(owner,0x1f0,reinterpret_cast<uintptr_t>(denied));
                const auto before=record;
                assert(dc->find_record(self,records+0x10,&record)==SCO_FAILED);
                assert(std::memcmp(&record,&before,sizeof(record))==0);
                Put(owner,0x1f0,reinterpret_cast<uintptr_t>(records));
            }
            assert(!backend.ready());
            assert(VirtualFree(denied,0,MEM_RELEASE));
            assert(dc->is_ready(self)!=SCO_OK);
            Stop();
            ClearEngine();
            assert(!ConfigureEngine(rows));
            assert(sco::Release(self)==sco::Result::Ok);
            return;
        }
#endif
        if (std::strcmp(mode,"cycle")==0) Put(structs,20,int32_t{1});
        else { assert(std::strcmp(mode,"duplicate")==0); std::memcpy(records+0x24+0x10,records+0x10,16); }
        assert(dc->find_record(self,records+0x10,&record)==SCO_FAILED);
        assert(dc->is_ready(self)==SCO_UNAVAILABLE);
    }
    Stop();
    assert(sco::Release(self) == sco::Result::Ok);
    ClearEngine();
}
} // namespace

int main(int argc,char** argv) {
    TestEngineThroughPublishedService(argc>1?argv[1]:nullptr);
    std::puts("PASS test_datacore_live_engine: native counted reads through service");
}
