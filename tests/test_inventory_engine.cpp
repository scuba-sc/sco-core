// SPDX-License-Identifier: GPL-3.0-only
// game.inventory 1.1's engine side (src/game/services/inventory_engine.cpp) against SYNTHETIC game
// objects: fake vtables in test memory laid out at the offsets inventory_map.h names, never the
// game. Covers the guards (wrong vtable entries, bad bounds, bad pointers, dead handles), the
// missing-container case, nested containers, a cycle, the 128-port cap and generation. Windows only
// (SEH). Nothing here is in-game evidence.
#include "../src/game/inventory_map.h"
#include "../src/game/services/inventory_engine.h"
#include <sc_inventory.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <windows.h>

using namespace sco::game::inventory;
namespace map = sco::game::inventory_map;

namespace {

int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

constexpr uint64_t kGenBits = 0x0ABC000000000000ull;   // handle generation bits above the pointer
constexpr size_t kVtSlots = 0x1000 / 8;

// ---- synthetic objects ---------------------------------------------------------------------

struct Entity { uintptr_t* vt; uint64_t id; uint64_t handleOfContainer; };
struct Item { uintptr_t* vt; uint32_t type; uint32_t pad; uintptr_t descriptor; uint8_t descBytes[16]; };
struct Port { uintptr_t* vt; bool valid; uint64_t itemHandle; Item* item; uintptr_t container; };
struct Container {
    uintptr_t* vt;
    uint8_t pad[0x80];
    uintptr_t begin, end;   // at +0x88, +0x90 as in the game's layout
    std::vector<uintptr_t> storage;
};
static_assert(offsetof(Container, begin) == map::kContainerPortsBegin && offsetof(Container, end) == map::kContainerPortsEnd, "layout");

bool deadHandle = false;
uint16_t typeIdCache = 5;

// Entity slot: component by type. Writes the handle into *out and returns out (like the game).
const uint64_t* __fastcall EntityComponent(Entity* e, uint64_t* out, uint16_t* type) {
    if (*type != 5 || !e->handleOfContainer) return nullptr;
    *out = e->handleOfContainer;
    return out;
}
const uint64_t* __fastcall EntityId(Entity* e, uint8_t* tmp) { (void)tmp; return &e->id; }
bool __fastcall HandleValid(const uint64_t* h) { return !deadHandle && (*h & 0xFFFFFFFFFFFFull) != 0 && (*h >> 48) == (kGenBits >> 48); }
uintptr_t __fastcall ContainerGetPorts(Container*, void*, void*, bool) { return 0; }   // never called, only compared
bool __fastcall PortIsValid(Port* p) { return p->valid; }
const uint64_t* __fastcall PortItemHandle(Port* p, uint64_t* tmp) { *tmp = p->itemHandle; return tmp; }
uintptr_t __fastcall PortGetItem(Port* p) { return reinterpret_cast<uintptr_t>(p->item); }
uintptr_t __fastcall PortGetContainer(Port* p) { return p->container; }
uintptr_t __fastcall ItemDescriptor(Item* i) { return i->descriptor; }

EngineRows Rows() {
    EngineRows r;
    r.getItemPorts = reinterpret_cast<uintptr_t>(&ContainerGetPorts);
    r.portIsValid = reinterpret_cast<uintptr_t>(&PortIsValid);
    r.portItemHandle = reinterpret_cast<uintptr_t>(&PortItemHandle);
    r.portGetItem = reinterpret_cast<uintptr_t>(&PortGetItem);
    r.portGetContainer = reinterpret_cast<uintptr_t>(&PortGetContainer);
    r.handleValid = reinterpret_cast<uintptr_t>(&HandleValid);
    r.typeIdCache = reinterpret_cast<uintptr_t>(&typeIdCache);
    return r;
}

uintptr_t* MakeVt(std::vector<uintptr_t*>& keep) {
    auto* vt = new uintptr_t[kVtSlots]();
    keep.push_back(vt);
    return vt;
}

// A little world. Everything is owned here and freed with it.
struct World {
    std::vector<uintptr_t*> vts;
    std::vector<Entity*> entities;
    std::vector<Item*> items;
    std::vector<Port*> ports;
    std::vector<Container*> containers;
    uintptr_t* entityVt = nullptr;
    uintptr_t* itemVt = nullptr;
    uintptr_t* portVt = nullptr;
    uintptr_t* containerVt = nullptr;
    uint64_t nextId = 5000;

    World() {
        entityVt = MakeVt(vts);
        entityVt[map::kEntityComponentByType / 8] = reinterpret_cast<uintptr_t>(&EntityComponent);
        entityVt[map::kEntityId / 8] = reinterpret_cast<uintptr_t>(&EntityId);
        itemVt = MakeVt(vts);
        itemVt[map::kItemDescriptor / 8] = reinterpret_cast<uintptr_t>(&ItemDescriptor);
        portVt = MakeVt(vts);
        portVt[map::kPortGetItem / 8] = reinterpret_cast<uintptr_t>(&PortGetItem);
        portVt[map::kPortGetContainer / 8] = reinterpret_cast<uintptr_t>(&PortGetContainer);
        portVt[map::kPortIsValid / 8] = reinterpret_cast<uintptr_t>(&PortIsValid);
        portVt[map::kPortItemHandle / 8] = reinterpret_cast<uintptr_t>(&PortItemHandle);
        containerVt = MakeVt(vts);
        containerVt[map::kContainerGetPorts / 8] = reinterpret_cast<uintptr_t>(&ContainerGetPorts);
    }
    ~World() {
        for (auto* p : vts) delete[] p;
        for (auto* p : entities) delete p;
        for (auto* p : items) delete p;
        for (auto* p : ports) delete p;
        for (auto* p : containers) delete p;
    }
    Entity* NewEntity(Container* c = nullptr) {
        auto* e = new Entity{ entityVt, nextId++, c ? (reinterpret_cast<uintptr_t>(c) | kGenBits) : 0 };
        entities.push_back(e);
        return e;
    }
    Container* NewContainer() {
        auto* c = new Container{};
        c->vt = containerVt;
        containers.push_back(c);
        return c;
    }
    // An item entity of a type, optionally carrying a container of its own.
    Port* AddPort(Container* c, bool valid, uint32_t type = 0, Container* inner = nullptr) {
        auto* p = new Port{ portVt, valid, 0, nullptr, 0 };
        if (valid) {
            Entity* e = NewEntity();
            auto* it = new Item{};
            it->vt = itemVt;
            it->descriptor = reinterpret_cast<uintptr_t>(&it->descBytes[0]) - map::kDescItemType;
            memset(it->descBytes, 0, sizeof(it->descBytes));
            it->type = type;
            memcpy(&it->descBytes[0], &it->type, 4);   // descriptor + 8 is descBytes[0] via the offset above
            items.push_back(it);
            p->item = it;
            p->itemHandle = reinterpret_cast<uintptr_t>(e) | kGenBits;
            p->container = reinterpret_cast<uintptr_t>(inner);
        }
        ports.push_back(p);
        c->storage.push_back(reinterpret_cast<uintptr_t>(p));
        Sync(c);
        return p;
    }
    static void Sync(Container* c) {
        c->begin = reinterpret_cast<uintptr_t>(c->storage.data());
        c->end = c->begin + 8 * c->storage.size();
    }
    uint64_t EntityIdOf(const Port* p) const { return reinterpret_cast<const Entity*>(p->itemHandle & 0xFFFFFFFFFFFFull)->id; }
};

struct Snap {
    sc_inventory_snapshot s;
    explicit Snap(uint32_t capacity = SC_INVENTORY_MAX_PORTS) { memset(&s, 0xAB, sizeof(s)); s.size = sizeof(s); s.count = capacity; }
};

ReadStatus Read(const World&, Entity* e, Generation& g, Snap& out, const char** why = nullptr) {
    return ReadSnapshot(Rows(), reinterpret_cast<uintptr_t>(e), g, &out.s, why);
}

// ---- tests ---------------------------------------------------------------------------------

void TestHappyPathNested() {
    World w;
    Container* root = w.NewContainer();
    Container* backpack = w.NewContainer();
    Port* helmet = w.AddPort(root, true, 22);
    w.AddPort(root, false);
    Port* pack = w.AddPort(root, true, 26, backpack);
    w.AddPort(backpack, true, 85);
    w.AddPort(backpack, false);
    Port* sidearm = w.AddPort(root, true, 95);
    Entity* player = w.NewEntity(root);
    typeIdCache = 5; deadHandle = false;
    Generation g;
    Snap s;
    const char* why = nullptr;
    CHECK(Read(w, player, g, s, &why) == ReadStatus::Ok);
    // Order: helmet, (empty), pack, then the pack's ports, then the sidearm.
    CHECK(s.s.count == 6 && s.s.total_seen == 6 && s.s.flags == 0 && s.s.generation == 1);
    CHECK(s.s.ports[0].valid == 1 && s.s.ports[0].item_class_id == 22 && s.s.ports[0].entity_id == w.EntityIdOf(helmet));
    CHECK(s.s.ports[1].valid == 0 && s.s.ports[1].item_class_id == 0 && s.s.ports[1].entity_id == 0);
    CHECK(s.s.ports[2].item_class_id == 26 && s.s.ports[2].entity_id == w.EntityIdOf(pack));
    CHECK(s.s.ports[3].item_class_id == 85 && s.s.ports[4].valid == 0);
    CHECK(s.s.ports[5].item_class_id == 95 && s.s.ports[5].entity_id == w.EntityIdOf(sidearm));
    // The same world again: the same generation. Then a change: the next one.
    Snap again;
    CHECK(Read(w, player, g, again) == ReadStatus::Ok && again.s.generation == 1);
    w.AddPort(root, true, 24);
    Snap changed;
    CHECK(Read(w, player, g, changed) == ReadStatus::Ok && changed.s.count == 7 && changed.s.generation == 2);
}

void TestNoContainer() {
    World w;
    Entity* none = w.NewEntity(nullptr);   // the component lookup answers nothing
    Generation g;
    Snap s;
    const char* why = nullptr;
    CHECK(Read(w, none, g, s, &why) == ReadStatus::NoContainer && why);
    // The type isn't registered yet.
    Container* c = w.NewContainer();
    Entity* player = w.NewEntity(c);
    typeIdCache = 0xFFFF;
    CHECK(Read(w, player, g, s) == ReadStatus::NoContainer);
    typeIdCache = 5;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok && s.s.count == 0 && s.s.total_seen == 0 && s.s.generation == 1);
    // A dead container handle.
    deadHandle = true;
    CHECK(Read(w, player, g, s) == ReadStatus::NoContainer);
    deadHandle = false;
}

void TestGuards() {
    World w;
    Container* c = w.NewContainer();
    w.AddPort(c, true, 22);
    Entity* player = w.NewEntity(c);
    typeIdCache = 5; deadHandle = false;
    Generation g;
    Snap s;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);

    // The component isn't the expected container: its vtable entry isn't GetItemPorts.
    w.containerVt[map::kContainerGetPorts / 8] = 0x1234;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    w.containerVt[map::kContainerGetPorts / 8] = reinterpret_cast<uintptr_t>(&ContainerGetPorts);
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);

    // Each port accessor checked: a port whose vtable differs fails the whole snapshot.
    for (size_t off : { map::kPortGetItem, map::kPortGetContainer, map::kPortIsValid, map::kPortItemHandle }) {
        const uintptr_t was = w.portVt[off / 8];
        w.portVt[off / 8] = 0x1234;
        CHECK(Read(w, player, g, s) == ReadStatus::Guard);
        w.portVt[off / 8] = was;
    }
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);

    // Bad vector bounds: reversed, unaligned, odd length, too many.
    const uintptr_t b = c->begin, e = c->end;
    c->begin = e + 8;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    c->begin = b + 1; c->end = e + 1;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    c->begin = b; c->end = e + 4;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    c->begin = b; c->end = b + 8ull * (map::kMaxPortsPerContainer + 1);
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    c->begin = 0; c->end = 0x800;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    // Both zero is an empty vector.
    c->begin = 0; c->end = 0;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok && s.s.count == 0);
    c->begin = b; c->end = e;

    // A vector in unreadable memory: caught, not a crash.
    void* hole = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    CHECK(hole != nullptr);
    c->begin = reinterpret_cast<uintptr_t>(hole);
    c->end = c->begin + 16;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    // A port pointer into unreadable memory.
    c->begin = b; c->end = e;
    c->storage[0] = reinterpret_cast<uintptr_t>(hole);
    World::Sync(c);
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    c->storage[0] = reinterpret_cast<uintptr_t>(w.ports[0]);
    World::Sync(c);
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);
    VirtualFree(hole, 0, MEM_RELEASE);

    // A dead item handle on a valid port, a null item, a missing descriptor.
    deadHandle = true;
    CHECK(Read(w, player, g, s) == ReadStatus::NoContainer);   // the container handle itself is dead first
    deadHandle = false;
    Port* p = w.ports[0];
    Item* item = p->item;
    p->item = nullptr;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    p->item = item;
    const uintptr_t desc = item->descriptor;
    item->descriptor = 0;
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    item->descriptor = desc;
    const uint64_t handle = p->itemHandle;
    p->itemHandle = 0x1234;   // the generation bits don't match: not a live handle
    CHECK(Read(w, player, g, s) == ReadStatus::Guard);
    p->itemHandle = handle;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);

    // Entity problems: null, a garbage pointer (fault), a missing row.
    CHECK(Read(w, nullptr, g, s) == ReadStatus::Guard);
    CHECK(ReadSnapshot(Rows(), 0x20000, g, &s.s, nullptr) == ReadStatus::Fault);
    EngineRows missing = Rows();
    missing.portGetItem = 0;
    CHECK(ReadSnapshot(missing, reinterpret_cast<uintptr_t>(player), g, &s.s, nullptr) == ReadStatus::Guard);
    CHECK(ReadSnapshot(Rows(), reinterpret_cast<uintptr_t>(player), g, nullptr, nullptr) == ReadStatus::Guard);
}

void TestNestedCycleAndDepth() {
    World w;
    Container* root = w.NewContainer();
    Container* inner = w.NewContainer();
    w.AddPort(root, true, 26, inner);
    w.AddPort(inner, true, 54, root);   // attaches the player's own container again
    Entity* player = w.NewEntity(root);
    typeIdCache = 5; deadHandle = false;
    Generation g;
    Snap s;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);
    CHECK(s.s.count == 2 && s.s.total_seen == 2 && s.s.flags == SC_INVENTORY_FLAG_LIMITED);

    // A chain deeper than the limit.
    World d;
    Container* top = d.NewContainer();
    Container* cur = top;
    for (int i = 0; i < 14; ++i) { Container* n = d.NewContainer(); d.AddPort(cur, true, 54, n); cur = n; }
    Entity* p2 = d.NewEntity(top);
    Snap t;
    CHECK(Read(d, p2, g, t) == ReadStatus::Ok && t.s.count == kMaxDepth + 1 && t.s.flags == SC_INVENTORY_FLAG_LIMITED);

    // A nested container that isn't an item-port container: skipped and flagged, the rest read.
    World m;
    Container* r2 = m.NewContainer();
    Container* wrong = m.NewContainer();
    wrong->vt = m.itemVt;   // some other object's vtable
    m.AddPort(r2, true, 26, wrong);
    m.AddPort(r2, true, 95);
    Entity* p3 = m.NewEntity(r2);
    Snap u;
    CHECK(Read(m, p3, g, u) == ReadStatus::Ok && u.s.count == 2 && u.s.flags == SC_INVENTORY_FLAG_LIMITED);
}

void TestCapAndTruncation() {
    World w;
    Container* c = w.NewContainer();
    for (int i = 0; i < 200; ++i) w.AddPort(c, i % 2 == 0, 85);
    Entity* player = w.NewEntity(c);
    typeIdCache = 5; deadHandle = false;
    Generation g;
    Snap s;
    CHECK(Read(w, player, g, s) == ReadStatus::Ok);
    CHECK(s.s.count == SC_INVENTORY_MAX_PORTS && s.s.total_seen == 200 && s.s.flags == SC_INVENTORY_FLAG_TRUNCATED);
    CHECK(s.s.ports[0].valid == 1 && s.s.ports[1].valid == 0 && s.s.ports[126].valid == 1);
    // Capacity in: a smaller one is honoured.
    Snap few(3);
    CHECK(Read(w, player, g, few) == ReadStatus::Ok && few.s.count == 3 && few.s.total_seen == 200 && few.s.flags == SC_INVENTORY_FLAG_TRUNCATED);
    CHECK(few.s.ports[3].entity_id == 0xABABABABABABABABull);   // nothing written past the capacity
}

}  // namespace

int main() {
    TestHappyPathNested();
    TestNoContainer();
    TestGuards();
    TestNestedCycleAndDepth();
    TestCapAndTruncation();
    std::printf("game.inventory engine (synthetic): %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
