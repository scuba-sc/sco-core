// GPL-3.0-only. Synthetic game-pack provider, no game/network/real IDs.
#include "../src/game/services/world_control.h"
#include <scosdk/game/world_control.hpp>
#include <sco/runtime.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
using sco::game::services::WorldControl;
using sco::game::services::WorldControlProvider;
namespace {
int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int a, b;
auto* A = reinterpret_cast<sco_plugin*>(&a);
auto* B = reinterpret_cast<sco_plugin*>(&b);
bool thread = true, alive = true, fail_begin = false, fail_step = false, fail_restore = false;
int begins = 0, steps = 0, restores = 0, rays = 0;
struct State { double pos; int mode, input; } state{12.0, 7, 9}, saved{};
bool on_thread() { return thread; }
bool live(const sco_plugin* p) { return alive && (p == A || p == B); }
bool owned(const sco_plugin* p, uint64_t id) { return p == A && id >= 100 && id < 132; }
sco_result begin() { ++begins; if (fail_begin) return SCO_NOT_FOUND; saved=state; state.mode=1; state.input=0; return SCO_OK; }
sco_result step(const sc_world_camera_step* s) { ++steps; state.pos += s->translation[0]; return fail_step ? SCO_FAILED : SCO_OK; }
sco_result restore() { ++restores; if (fail_restore) return SCO_FAILED; state=saved; return SCO_OK; }
sco_result ray(uint64_t zone, const double*, const double* dir, double dist,
    const uint64_t* ids, uint32_t n, sc_world_hit* hit) {
    ++rays; CHECK(zone == 8); CHECK(dir[0] == 1); CHECK(dist == 20000);
    // Simulated preview at distance 2, ground at 5: own preview filtered.
    bool skip = false; for (uint32_t i=0; i<n; ++i) skip |= ids[i] == 100;
    hit->distance = skip ? 5 : 2; hit->pos[0] = hit->distance;
    hit->flags = SC_WORLD_HIT_ENTITY | SC_WORLD_HIT_NORMAL; hit->entity_id=999;
    hit->normal[2] = 1; return SCO_OK;
}
WorldControlProvider provider() { return {on_thread, live, owned, ray, begin, step, restore}; }
WorldControl* ctrl;
void owner_released(const void* owner) { CHECK(ctrl->ReleaseOwner(owner)==SCO_OK); }
sco_result ray_table(sco_plugin* self, uint64_t z, const double* f, const double* d, double m, const uint64_t* ids, uint32_t n, sc_world_hit* hit) { return ctrl->Ray(self,z,f,d,m,ids,n,hit); }
sco_result acquire_table(sco_plugin* self, uint64_t* token) { return ctrl->Acquire(self,token); }
sco_result step_table(sco_plugin* self, uint64_t token, const sc_world_camera_step* s) { return ctrl->Step(self,token,s); }
sco_result release_table(sco_plugin* self, uint64_t token) { return ctrl->Release(self,token); }
sc_world_control_v1 table{sizeof(table),0,ray_table,acquire_table,step_table,release_table};
const void* offered = &table;
sco_result query_result = SCO_OK;
sco_result query(const char* name, uint32_t version, const void** out) {
    CHECK(std::strcmp(name, SC_WORLD_CONTROL_NAME)==0); CHECK(version==SC_WORLD_CONTROL_VERSION_1_0);
    *out = offered; return query_result;
}
}
int main() {
    CHECK(SC_WORLD_CONTROL_STEP_METRES == 10.0 && SC_WORLD_CONTROL_STEP_RADIANS == 0.25);
    WorldControl c(provider(), true); ctrl = &c;
    sco_api api{}; api.size=sizeof(api); api.query_service=query;
    sco::sdk::game::WorldControl w;
    CHECK(w.Open(&api,A)==SCO_OK);
    const double from[3]{}, dir[3]{2,0,0}; uint64_t ids[33]{};
    for (unsigned i=0; i<33; ++i) ids[i]=100+i;
    sc_world_hit hit{};
    CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,1,hit)==SCO_OK);
    CHECK(hit.distance==5 && hit.flags==0 && hit.entity_id==0 && hit.normal[2]==0);
    CHECK(w.RaycastFiltered(8,from,dir,1e9,nullptr,0,hit)==SCO_OK && hit.distance==2);
    CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,32,hit)==SCO_OK);
    const int r0=rays;
    CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,33,hit)==SCO_BAD_ARG);
    CHECK(w.RaycastFiltered(8,from,dir,1e9,nullptr,1,hit)==SCO_BAD_ARG);
    ids[1]=100; CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,2,hit)==SCO_BAD_ARG); ids[1]=101;
    ids[0]=999; CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,1,hit)==SCO_FAILED && hit.distance==0); ids[0]=100;
    CHECK(c.Ray(B,8,from,dir,1e9,ids,1,&hit)==SCO_FAILED);
    double bad[3]{std::numeric_limits<double>::infinity(),0,0};
    CHECK(w.RaycastFiltered(8,bad,dir,1e9,ids,1,hit)==SCO_BAD_ARG);
    CHECK(w.RaycastFiltered(8,from,from,1e9,ids,1,hit)==SCO_BAD_ARG);
    CHECK(w.RaycastFiltered(0,from,dir,1e9,ids,1,hit)==SCO_BAD_ARG);
    CHECK(rays==r0);
    struct Extended { sc_world_hit hit; uint64_t tail; } ext{}; ext.hit.size=sizeof(ext); ext.tail=12345;
    CHECK(c.Ray(A,8,from,dir,1e9,ids,1,&ext.hit)==SCO_OK && ext.tail==12345 && ext.hit.size==sizeof(ext));
    // Physically short output, no zeroing past supplied extent.
    auto short_hit=std::make_unique<unsigned char[]>(4); uint32_t small=4; std::memcpy(short_hit.get(),&small,4);
    CHECK(c.Ray(A,8,from,dir,1e9,ids,1,reinterpret_cast<sc_world_hit*>(short_hit.get()))==SCO_BAD_ARG);
    uint64_t lease=9, other=9;
    thread=false; CHECK(w.CameraAcquire(lease)==SCO_WRONG_THREAD && lease==0);
    CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,1,hit)==SCO_WRONG_THREAD);
    thread=true; alive=false; CHECK(w.CameraAcquire(lease)==SCO_BAD_ARG); alive=true;
    fail_begin=true; CHECK(w.CameraAcquire(lease)==SCO_NOT_FOUND && lease==0 && state.mode==7); fail_begin=false;
    CHECK(w.CameraAcquire(lease)==SCO_OK && lease!=0 && state.mode==1);
    CHECK(c.Acquire(B,&other)==SCO_FAILED && other==0);
    CHECK(w.CameraAcquire(other)==SCO_OK && other==lease);
    sc_world_camera_step input{}; input.size=sizeof(input); input.rotation_xyzw[3]=1; input.translation[0]=2;
    CHECK(c.Step(B,lease,&input)==SCO_FAILED);
    CHECK(c.Release(B,lease)==SCO_FAILED);
    CHECK(w.CameraStep(lease,input)==SCO_OK);
    CHECK(state.pos==14.0);
    input.translation[0]=10.1; CHECK(w.CameraStep(lease,input)==SCO_BAD_ARG);
    input.translation[0]=2; input.rotation_xyzw[3]=0; CHECK(w.CameraStep(lease,input)==SCO_BAD_ARG);
    input.rotation_xyzw[0]=std::sin(.2); input.rotation_xyzw[3]=std::cos(.2); CHECK(w.CameraStep(lease,input)==SCO_BAD_ARG);
    input.rotation_xyzw[0]=0; input.rotation_xyzw[3]=1;
    input._pad=1; CHECK(w.CameraStep(lease,input)==SCO_BAD_ARG); input._pad=0;
    input.translation[0]=std::numeric_limits<double>::quiet_NaN();
    CHECK(w.CameraStep(lease,input)==SCO_BAD_ARG); input.translation[0]=2;
    auto short_step=std::make_unique<unsigned char[]>(4); std::memcpy(short_step.get(),&small,4);
    CHECK(c.Step(A,lease,reinterpret_cast<sc_world_camera_step*>(short_step.get()))==SCO_BAD_ARG);
    input.translation[0]=10; CHECK(w.CameraStep(lease,input)==SCO_OK); input.translation[0]=2;
    thread=false; CHECK(w.CameraRelease(lease)==SCO_WRONG_THREAD && state.mode==1); thread=true;
    CHECK(c.ReleaseOwner(B)==SCO_OK && state.mode==1);
    const int before=restores;
    CHECK(c.ReleaseOwner(A)==SCO_OK && state.pos==12.0 && state.mode==7 && state.input==9);
    CHECK(c.ReleaseOwner(A)==SCO_OK && restores==before+1);
    CHECK(w.CameraRelease(lease)==SCO_NOT_FOUND);
    CHECK(w.CameraAcquire(other)==SCO_OK && other!=lease);
    CHECK(w.CameraStep(lease,input)==SCO_NOT_FOUND);
    CHECK(w.CameraRelease(other)==SCO_OK);
    WorldControl unavailable({on_thread,live},true); CHECK(unavailable.Acquire(A,&lease)==SCO_UNAVAILABLE && lease==0);
    WorldControl nohook(provider(),false); CHECK(nohook.Acquire(A,&lease)==SCO_UNAVAILABLE);
    auto missing=provider(); missing.restore=nullptr; WorldControl norestore(missing,true);
    CHECK(norestore.Acquire(A,&lease)==SCO_UNAVAILABLE);
    CHECK(w.CameraAcquire(lease)==SCO_OK); CHECK(c.Stop()==SCO_OK && state.mode==7);
    CHECK(w.CameraAcquire(other)==SCO_UNAVAILABLE); CHECK(c.Stop()==SCO_OK);
    CHECK(c.Start(true)==SCO_OK); CHECK(w.CameraAcquire(other)==SCO_OK);
    CHECK(c.Start(true)==SCO_FAILED); CHECK(w.CameraRelease(other)==SCO_OK);
    // Provider step can fail AFTER a synthetic mutation: compensate and latch.
    WorldControl fault(provider(),true); CHECK(fault.Acquire(A,&lease)==SCO_OK);
    fail_step=true; CHECK(fault.Step(A,lease,&input)==SCO_FAILED && state.mode==7 && state.pos==12.0); fail_step=false;
    CHECK(fault.Acquire(B,&other)==SCO_UNAVAILABLE && other==0);
    WorldControl restore_fault(provider(),true); CHECK(restore_fault.Acquire(A,&lease)==SCO_OK);
    fail_restore=true; CHECK(restore_fault.ReleaseOwner(A)==SCO_FAILED && state.mode==1);
    CHECK(restore_fault.Acquire(B,&other)==SCO_UNAVAILABLE);
    fail_restore=false; CHECK(restore_fault.ReleaseOwner(A)==SCO_OK && state.mode==7);
    CHECK(restore_fault.Acquire(A,&other)==SCO_UNAVAILABLE);
    CHECK(restore_fault.Start(true)==SCO_FAILED);
    // Real neutral-runtime owner release hook reaches the synthetic camera
    // provider. This is host-hook wiring evidence, not Windows loader/SEH proof.
    WorldControl unloading(provider(),false); ctrl=&unloading;
    sco::SetGameThread();
    CHECK(sco::AddReleaseHook(owner_released)==sco::Result::Ok);
    CHECK(unloading.Start(true)==SCO_OK);
    CHECK(unloading.Acquire(B,&lease)==SCO_OK);
    CHECK(unloading.Step(B,lease,&input)==SCO_OK && state.pos==14.0);
    CHECK(sco::Release(B)==sco::Result::Ok && state.pos==12.0 && state.mode==7 && state.input==9);
    CHECK(sco::Release(B)==sco::Result::BadArg); // unload twice cannot restore again
    CHECK(sco::RemoveReleaseHook(owner_released)==sco::Result::Ok);
    // Each field independently physically truncated, then full-sized null.
    const size_t offsets[]{8,16,24,32};
    for (const auto offset: offsets) {
        auto bytes=std::make_unique<unsigned char[]>(offset+7);
        std::memcpy(bytes.get(),&table,offset+7); const uint32_t n=static_cast<uint32_t>(offset+7);
        std::memcpy(bytes.get(),&n,4); offered=bytes.get(); CHECK(w.Open(&api,A)==SCO_OK);
        if(offset==8) CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,1,hit)==SCO_UNAVAILABLE);
        if(offset==16) CHECK(w.CameraAcquire(lease)==SCO_UNAVAILABLE && lease==0);
        if(offset==24) CHECK(w.CameraStep(1,input)==SCO_UNAVAILABLE);
        if(offset==32) CHECK(w.CameraRelease(1)==SCO_UNAVAILABLE);
    }
    sc_world_control_v1 nulls{}; nulls.size=sizeof(nulls); offered=&nulls; CHECK(w.Open(&api,A)==SCO_OK);
    CHECK(w.RaycastFiltered(8,from,dir,1e9,ids,1,hit)==SCO_UNAVAILABLE);
    CHECK(w.CameraAcquire(lease)==SCO_UNAVAILABLE && lease==0);
    CHECK(w.CameraStep(1,input)==SCO_UNAVAILABLE); CHECK(w.CameraRelease(1)==SCO_UNAVAILABLE);
    query_result=SCO_NOT_FOUND; CHECK(w.Open(&api,A)==SCO_NOT_FOUND); CHECK(w.CameraAcquire(lease)==SCO_UNAVAILABLE);
    query_result=SCO_OK; offered=nullptr; CHECK(w.Open(&api,A)==SCO_UNAVAILABLE);
    api.query_service=nullptr; CHECK(w.Open(&api,A)==SCO_UNAVAILABLE);
    std::printf("world_control synthetic: %d passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
