// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "state.h"
#include "sco/game/reads.h"
#include "sco/game/services.h"
#include "sco/hook.h"
#include "sco/scan.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
namespace sco::game::asop_host {
using sco::Log;
using sco::Rel32;
using reads::ZoneId;
using reads::ZoneParent;
constexpr uint64_t kPtrMask=0xFFFFFFFFFFFFull;
template<class T> T Rd(uintptr_t p) { return *reinterpret_cast<const T*>(p); }
template<class R,class... A> R VCall(uintptr_t obj,size_t off,A... a) {
    using Fn=R(__fastcall*)(uintptr_t,A...);
    return reinterpret_cast<Fn>(Rd<uintptr_t>(Rd<uintptr_t>(obj)+off))(obj,a...);
}
inline bool GetLocalPlayer(uintptr_t& actor,uintptr_t& entity) { return reads::LocalPlayer(actor,entity); }
bool HookFunction(uint8_t* target,size_t stolen,void* detour,void** original);
uintptr_t EntitySystem();
uint64_t EntityIdOfHandle(const void* handle);
uint64_t EntityIdOfComponent(uintptr_t component);
int FleetShipIndexOfUrnId(const uint64_t id[2]);
const char* FleetShipClass(int index);
bool AsopEnabled();
bool Fleet_UseGameList();
bool AsopCapabilityRows(const char* capability,char* why,size_t n);
bool Fleet_RetrieveActive();
uint64_t Fleet_RetrievedShipOwner(uint64_t vehicle);
void Fleet_OnPadVehicle(uint64_t atc,uint64_t pad,uint64_t vehicle);
} // namespace sco::game::asop_host
