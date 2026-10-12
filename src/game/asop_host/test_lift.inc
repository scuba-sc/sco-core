// SPDX-License-Identifier: GPL-3.0-only
namespace lift_fixture {
using DWORD=uint32_t;
constexpr int32_t kOpenIdle=3,kClosedIdle=7;
constexpr DWORD kLiftOpenFallbackMs=4000,kLiftWatchMs=90000;
struct LiftWatch { uint64_t manager=0; DWORD spawnedAt=0; bool opened=false; int32_t lastState=-2; };
std::vector<LiftWatch> g_liftWatches;
int32_t state=-1; int calls=0; bool available=true;
using LiftRequestFn=void(*)(uint64_t);
void Open(uint64_t) { ++calls; }
LiftRequestFn g_liftOpen=Open;
void Log(const char*,...) {}
int32_t LiftStateOf(uint64_t) { return state; }
void LogLiftState(const char*,uint64_t,int32_t s,int32_t& last) { last=s; }
bool CallLift(LiftRequestFn fn,uint64_t manager) { if(!available) return false; fn(manager);return true; }
#include "lift_watch.inc"
void TestLift() {
    g_liftWatches.push_back({1,100,false,-2}); state=kClosedIdle;
    RunLiftWatches(4100); assert(calls==0); // exact timeout boundary
    RunLiftWatches(4101); assert(calls==1 && g_liftWatches[0].opened);
    RunLiftWatches(5000); assert(calls==1); // exactly once
    state=kOpenIdle; RunLiftWatches(5001); assert(g_liftWatches.empty());
    g_liftWatches.push_back({2,100,false,-2}); state=-1;
    RunLiftWatches(5000); assert(calls==1); // unknown not interpreted as ClosedIdle
    RunLiftWatches(90101); assert(g_liftWatches.empty());
    g_liftWatches.push_back({3,100,false,-2}); state=kClosedIdle; available=false;
    RunLiftWatches(5000); assert(calls==1 && g_liftWatches[0].opened); // fault not retried
    RunLiftWatches(90101); assert(g_liftWatches.empty());
}
}
