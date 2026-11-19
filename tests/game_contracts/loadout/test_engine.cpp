// SPDX-License-Identifier: GPL-3.0-only
// Actual Windows adapter over synthetic native objects, signatures and spawn
// boundary. No game image, process or network is used.
#include "game/services/loadout.h"
#include "game/services/spawn.h"
#include "sco/host.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include "sco/game/reads.h"
#include "sco/game/host.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL engine %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
using Table = std::array<uintptr_t, 300>;
struct Object { Table* table; alignas(8) unsigned char fields[0x600]{}; };
Table playerTable{}, itemTable{}, containerTable{}, portTable{}, resolverTable{}, classTable{};
Object player{&playerTable}, item{&itemTable}, container{&containerTable}, port{&portTable}, resolver{&resolverTable}, itemClass{&classTable};
uintptr_t nativeSystem[2]{};
uintptr_t portVector[1]{};
uint64_t portEntity = 0;
bool admitted = true, streamed = false, attachAllowed = true, duplicatePort = false;
unsigned spawned = 0, attached = 0, removed = 0;
uint64_t Id(uintptr_t entity) { return entity == reinterpret_cast<uintptr_t>(&player) ? 1 : 2; }
uint64_t* GetId(uintptr_t entity, uint64_t* out) { *out = Id(entity); return out; }
uintptr_t GetClass(uintptr_t) { return reinterpret_cast<uintptr_t>(&itemClass); }
const char* ClassName(uintptr_t) { return "synthetic_helmet"; }
const uint16_t* TypeId(uintptr_t, void* out, const char* name) {
    *static_cast<uint16_t*>(out) = std::strcmp(name, "IItemPortContainer") == 0 ? 1 : 2;
    return static_cast<const uint16_t*>(out);
}
uint64_t* Component(uintptr_t entity, uint64_t* out, const uint16_t* type) {
    *out = entity == reinterpret_cast<uintptr_t>(&player) && *type == 1 ? reinterpret_cast<uintptr_t>(&container) :
           entity == reinterpret_cast<uintptr_t>(&item) && *type == 2 ? reinterpret_cast<uintptr_t>(&item) : 0;
    return out;
}
char lastPortName[64]{};
bool KnownPort(const char* name) {
    static const char* const names[] = {"Armor_Helmet", "Clothing_Torso_0", "Clothing_Torso_1", "Clothing_Torso2",
        "Clothing_Legs", "Clothing_Feet", "Clothing_Hands", "Hat_ItemPort", "Hair_ItemPort", "Hair_Color",
        "Eyebrow_ItemPort", "Head_Accessory_ItemPort", "Head_Horn_ItemPort", "Jewellery_ItemPort"};
    for (const char* known : names) if (std::strcmp(name, known) == 0) return true;
    return false;
}
uintptr_t FindPort(uintptr_t root, const char* name) {
    CHECK(root == reinterpret_cast<uintptr_t>(&container));
    std::snprintf(lastPortName, sizeof(lastPortName), "%s", name);
    return KnownPort(name) ? reinterpret_cast<uintptr_t>(&port) : 0;
}
uint64_t* Handle(uintptr_t p, uint64_t* out) {
    CHECK(p == reinterpret_cast<uintptr_t>(&port)); *out = portEntity; return out;
}
uintptr_t Nested(uintptr_t p) { CHECK(p == reinterpret_cast<uintptr_t>(&port)); return duplicatePort ? reinterpret_cast<uintptr_t>(&container) : 0; }
bool Attach(uintptr_t p, uint64_t id, uint64_t behavior, uint32_t reason, uint64_t source) {
    CHECK(p == reinterpret_cast<uintptr_t>(&port) && id == 2 && behavior == 1 && reason == 0 && source == 0);
    ++attached;
    if (attachAllowed) portEntity = reinterpret_cast<uintptr_t>(&item);
    return attachAllowed;
}
sco::SigResult System(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(nativeSystem); }
sco::SigResult Find(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(reinterpret_cast<void*>(&FindPort)); }
sco::SigResult GetHandle(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(reinterpret_cast<void*>(&Handle)); }
sco::SigResult GetNested(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(reinterpret_cast<void*>(&Nested)); }
sco::SigResult Attacher(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(reinterpret_cast<void*>(&Attach)); }
sco::SigResult Present(const sco::Image&, const sco::SigResolveContext&) { return sco::SigOk(&player); }
const sco::SigDef rows[]{
    {"teleport.entity_system", nullptr, 0, 0, System, {}},
    {"loadout.port_by_name", nullptr, 0, 0, Find, {}},
    {"loadout.port_handle", nullptr, 0, 0, GetHandle, {}},
    {"loadout.port_container", nullptr, 0, 0, GetNested, {}},
    {"loadout.attach_entity", nullptr, 0, 0, Attacher, {}},
    {"spawn.landing_helper", nullptr, 0, 0, Present, {}},
    {"entities.class_site", nullptr, 0, 0, Present, {}},
};
}
namespace sco::game::host { bool FreshAdmissionAllowed() noexcept { return admitted; } }
namespace sco::game::reads {
bool LocalPlayer(uintptr_t& actor, uintptr_t& entity) { actor = entity = reinterpret_cast<uintptr_t>(&player); return true; }
uintptr_t EntityFromId(uint64_t id) {
    return id == 1 ? reinterpret_cast<uintptr_t>(&player) : id == 2 && streamed ? reinterpret_cast<uintptr_t>(&item) : 0;
}
uintptr_t EntityZone(uintptr_t entity) { CHECK(entity == reinterpret_cast<uintptr_t>(&player)); return 11; }
uint64_t ZoneId(uintptr_t zone) { CHECK(zone == 11); return 12; }
void EntityLocalPos(uintptr_t entity, double out[3]) { CHECK(entity == reinterpret_cast<uintptr_t>(&player)); out[0] = out[1] = out[2] = 0; }
}
namespace sco::game::services {
bool EntityRemovalReady() { return true; }
void RemoveEntityLater(uint64_t id) { CHECK(id == 2); ++removed; }
const char* SpawnEntityInZone(const char* name, uint64_t zone, const double pos[3], uint64_t& id, const double rot[4]) {
    (void)rot;
    CHECK(zone == 12 && pos[0] == 0);
    if (std::strcmp(name, "synthetic_missing") == 0) return "unknown entity class";
    CHECK(std::strcmp(name, "synthetic_helmet") == 0);
    ++spawned; id = 2; return nullptr;
}
}
int main() {
    using namespace sco::game::services;
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"synthetic native loadout"});
    sco_plugin* owner = sco::host::NewPlugin("engine_loadout");
    playerTable[1] = itemTable[1] = reinterpret_cast<uintptr_t>(&GetId);
    playerTable[0x390 / 8] = itemTable[0x390 / 8] = reinterpret_cast<uintptr_t>(&Component);
    itemTable[0x20 / 8] = reinterpret_cast<uintptr_t>(&GetClass);
    classTable[0x18 / 8] = reinterpret_cast<uintptr_t>(&ClassName);
    resolverTable[0x10 / 8] = reinterpret_cast<uintptr_t>(&TypeId);
    containerTable[0x6f8 / 8] = reinterpret_cast<uintptr_t>(&FindPort);
    portTable[0x48 / 8] = reinterpret_cast<uintptr_t>(&Nested);
    portTable[0x68 / 8] = reinterpret_cast<uintptr_t>(&Handle);
    portTable[0x90 / 8] = reinterpret_cast<uintptr_t>(&Attach);
    nativeSystem[1] = reinterpret_cast<uintptr_t>(&resolver);
    portVector[0] = reinterpret_cast<uintptr_t>(&port);
    *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(&container) + 0x88) = reinterpret_cast<uintptr_t>(portVector);
    *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(&container) + 0x90) = reinterpret_cast<uintptr_t>(portVector + 1);
    auto registry = std::make_shared<sco::SignatureRegistry>();
    const sco::SignatureTable signatureTable{rows, std::size(rows)};
    CHECK(registry->RegisterTables(&signatureTable, 1).status == sco::SigRegistryStatus::Ok);
    CHECK(registry->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(registry->Resolve(sco::Image{}) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(registry, nullptr));
    SetEnvironmentVariableA("SCO_LOADOUT_ASYNC", nullptr);
    CHECK(ProductionLoadoutBackend() == nullptr);
    SetEnvironmentVariableA("SCO_LOADOUT_OUTFIT", nullptr);
    SetEnvironmentVariableA("SCO_LOADOUT_ASYNC", "1");
    // Async route on, outfit switch off: outfit stays off.
    const LoadoutAsyncBackend* backend = ProductionLoadoutBackend(); CHECK(backend && !backend->outfit);
    CHECK(StartLoadout(nullptr, backend) == sco::Result::Ok);
    CHECK(api->has("game.loadout.async") && !api->has("game.loadout.outfit"));
    CHECK(StopLoadout() == sco::Result::Ok);
    // Any value other than 1 is off. The outfit switch alone, without the async route, gives no backend.
    SetEnvironmentVariableA("SCO_LOADOUT_OUTFIT", "0");
    backend = ProductionLoadoutBackend(); CHECK(backend && !backend->outfit);
    SetEnvironmentVariableA("SCO_LOADOUT_ASYNC", nullptr);
    SetEnvironmentVariableA("SCO_LOADOUT_OUTFIT", "1");
    CHECK(ProductionLoadoutBackend() == nullptr);
    // Both switches: outfit on.
    SetEnvironmentVariableA("SCO_LOADOUT_ASYNC", "1");
    backend = ProductionLoadoutBackend(); CHECK(backend && backend->outfit);
    CHECK(StartLoadout(nullptr, backend) == sco::Result::Ok);
    CHECK(api->has("game.loadout.async") && api->has("game.loadout.outfit") && !api->has("game.loadout.appearance"));
    const void* table = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &table) == SCO_OK);
    const auto* service = static_cast<const sc_loadout_v1_2*>(table);
    sc_loadout_item requested{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"};
    sc_loadout_request request{sizeof(request), 1, sizeof(requested), 0, &requested};
    sc_loadout_job_report report{}; report.size = sizeof(report);
    uint64_t job = 0;
    CHECK(service->submit(owner, &request, &job) == SCO_OK);
    TickLoadout(1); CHECK(spawned == 1 && attached == 0);
    TickLoadout(2); CHECK(attached == 0);
    CHECK(service->status(owner, job, &report) == SCO_OK && !report.report.applied);
    streamed = true;
    TickLoadout(3); CHECK(attached == 1);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.state == SC_LOADOUT_JOB_PENDING);
    TickLoadout(4);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.state == SC_LOADOUT_JOB_COMPLETE && report.report.applied == 1);
    CHECK(service->cancel(owner, job) == SCO_OK && removed == 0);
    CHECK(service->submit(owner, &request, &job) == SCO_OK);
    TickLoadout(5);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.report.applied == 1 && spawned == 1);
    CHECK(service->cancel(owner, job) == SCO_OK);
    portEntity = 0; streamed = false;
    CHECK(service->submit(owner, &request, &job) == SCO_OK);
    TickLoadout(6);
    CHECK(service->cancel(owner, job) == SCO_OK && removed == 1);
    CHECK(service->submit(owner, &request, &job) == SCO_OK);
    TickLoadout(7); admitted = false; TickLoadout(8);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.state == SC_LOADOUT_JOB_FAILED && removed == 2);
    CHECK(service->cancel(owner, job) == SCO_OK); admitted = true;
    requested.slot = SC_LOADOUT_SLOT_VISOR;
    CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(9);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.report.slots[0].status == SC_LOADOUT_UNSUPPORTED);
    CHECK(service->cancel(owner, job) == SCO_OK);
    requested = {SC_LOADOUT_SLOT_HEAD, 0, "synthetic_missing"};
    CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(10);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.report.slots[0].status == SC_LOADOUT_ITEM_MISSING);
    CHECK(service->cancel(owner, job) == SCO_OK);
    requested = {SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"};
    portEntity = 0; streamed = true; attachAllowed = false;
    const unsigned beforeAttach = attached;
    CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(11); TickLoadout(12);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.report.slots[0].status == SC_LOADOUT_FAILED);
    CHECK(attached == beforeAttach + 1 && report.report.applied == 0);
    CHECK(service->cancel(owner, job) == SCO_OK);
    // 1.3 outfit slots: each assigned slot looks up its own item-port name; unassigned values
    // (and the hair, hair colour, eyebrow, body, head, eyes and teeth ports, which have no slot) never reach the port search.
    attachAllowed = true; streamed = false; portEntity = 0;
    struct OutfitCase { uint32_t slot; const char* port; };
    const OutfitCase outfitCases[] = {
        {SC_LOADOUT_SLOT_CLOTHING_TORSO_0, "Clothing_Torso_0"}, {SC_LOADOUT_SLOT_CLOTHING_TORSO_1, "Clothing_Torso_1"},
        {SC_LOADOUT_SLOT_CLOTHING_TORSO_2, "Clothing_Torso2"}, {SC_LOADOUT_SLOT_CLOTHING_LEGS, "Clothing_Legs"},
        {SC_LOADOUT_SLOT_CLOTHING_FEET, "Clothing_Feet"}, {SC_LOADOUT_SLOT_CLOTHING_HANDS, "Clothing_Hands"},
        {SC_LOADOUT_SLOT_HAT, "Hat_ItemPort"},
        {SC_LOADOUT_SLOT_HEAD_ACCESSORY, "Head_Accessory_ItemPort"}, {SC_LOADOUT_SLOT_HEAD_HORN, "Head_Horn_ItemPort"},
        {SC_LOADOUT_SLOT_JEWELLERY, "Jewellery_ItemPort"}};
    uint32_t tick = 20;
    for (const auto& c : outfitCases) {
        requested = {c.slot, 0, "synthetic_helmet"};
        lastPortName[0] = 0;
        const unsigned spawnedBefore = spawned;
        CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(tick++);
        CHECK(std::strcmp(lastPortName, c.port) == 0 && spawned == spawnedBefore + 1);
        CHECK(service->cancel(owner, job) == SCO_OK);
    }
    for (uint32_t slot : {uint32_t(SC_LOADOUT_SLOT_JEWELLERY) + 1, 0u, 99u}) {
        requested = {slot, 0, "synthetic_helmet"};
        lastPortName[0] = 0;
        const unsigned spawnedBefore = spawned;
        CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(tick++);
        CHECK(service->status(owner, job, &report) == SCO_OK && report.report.slots[0].status == SC_LOADOUT_UNSUPPORTED);
        CHECK(!lastPortName[0] && spawned == spawnedBefore);
        CHECK(service->cancel(owner, job) == SCO_OK);
    }
    // Full outfit loop on one port: spawn, delayed stream, exact attach, readback.
    requested = {SC_LOADOUT_SLOT_HAT, 0, "synthetic_helmet"};
    portEntity = 0; streamed = false;
    const unsigned attachedBeforeHat = attached;
    CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(tick++);
    streamed = true; TickLoadout(tick++); TickLoadout(tick++);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.state == SC_LOADOUT_JOB_COMPLETE &&
          report.report.applied == 1 && attached == attachedBeforeHat + 1);
    CHECK(service->cancel(owner, job) == SCO_OK);
    requested = {SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"};
    portEntity = 0;
    // A changed live vtable is drift, not permission to call an arbitrary target.
    attachAllowed = true; streamed = false;
    CHECK(service->submit(owner, &request, &job) == SCO_OK); TickLoadout(tick++);
    streamed = true;
    portTable[0x90 / 8] = reinterpret_cast<uintptr_t>(&FindPort);
    TickLoadout(tick++);
    CHECK(service->status(owner, job, &report) == SCO_OK && report.state == SC_LOADOUT_JOB_FAILED);
    CHECK(attached == attachedBeforeHat + 1 && !api->has("game.loadout.async") && !api->has("game.loadout.outfit"));
    StopLoadout(); SetEnvironmentVariableA("SCO_LOADOUT_ASYNC", nullptr); SetEnvironmentVariableA("SCO_LOADOUT_OUTFIT", nullptr);
    CHECK(sco::ClearHostSignatureRegistry(registry, nullptr));
    std::puts("loadout engine: real adapter spawn, pending, attach, readback, cancel and admission checks passed");
}
