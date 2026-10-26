// GameFakeHost.cs: the Star Citizen extension's part of the fake-host test: stand-ins for game.actors,
// game.entities, game.world, game.creative, game.hangar, game.reputation and game.gamerules, and the tests of their C# wrappers and of the game.*
// events. Part of sco-sdk-game-sc; FakeHost.cs runs without it.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sco.Sdk.Game;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe partial class FakeHost
    {
        private static bool _inventoryCapability = true;
        static partial void GameHas(string name, ref bool ready)
        {
            if (name == "game.inventory.read") ready = _inventoryCapability;
        }

        private static ScInventoryV1* _inventory;
        private static bool _inventoryAvailable = true, _inventoryNull;
        private static uint _inventoryVersion = GameAbi.InventoryVersion1_1, _inventoryQueriedVersion;
        private static ulong _inventoryRaw = 22;
        private static uint _inventoryValid = 1;
        private static ScoResult _inventoryResult = ScoResult.Ok;
        private static int _inventoryCalls;
        private static bool _inventoryBadSize, _inventoryBadCount;
        private static Inventory? _inventoryAfterUnload;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult InvSnapshot(void* self, ScInventorySnapshot* value)
        {
            ++_inventoryCalls;
            if (self != _self || value == null || value->size < 3088 || value->count != 128) return ScoResult.BadArg;
            if (_inventoryResult != ScoResult.Ok) return _inventoryResult;
            value->count = 1;
            value->generation = 7;
            var port = (ScInventoryPort*)value->ports;
            *port = new ScInventoryPort { size = 24, valid = _inventoryValid, item_class_id = _inventoryRaw, entity_id = ulong.MaxValue };
            if (value->size >= 3096) { value->flags = 3; value->total_seen = 129; }
            if (_inventoryBadSize) value->size = 0;
            if (_inventoryBadCount) value->count = 129;
            return ScoResult.Ok;
        }

        private static void InventoryTests(Plugin p)
        {
            Expect(sizeof(ScInventoryPort) == 24 && sizeof(ScInventorySnapshot) == 3096 && sizeof(ScInventoryV1) == 16, "inventory native sizes");
            Expect(Marshal.OffsetOf<ScInventoryPort>(nameof(ScInventoryPort.item_class_id)).ToInt32() == 8 &&
                   Marshal.OffsetOf<ScInventoryPort>(nameof(ScInventoryPort.entity_id)).ToInt32() == 16 &&
                   Marshal.OffsetOf<ScInventorySnapshot>(nameof(ScInventorySnapshot.ports)).ToInt32() == 16 &&
                   Marshal.OffsetOf<ScInventorySnapshot>(nameof(ScInventorySnapshot.flags)).ToInt32() == 3088 &&
                   Marshal.OffsetOf<ScInventorySnapshot>(nameof(ScInventorySnapshot.total_seen)).ToInt32() == 3092,
                   "inventory frozen prefix and 1.1 tail offsets");
            var inventory = new Inventory();
            Expect(inventory.Open(p) == ScoResult.Ok && _inventoryQueriedVersion == GameAbi.InventoryVersion1_1, "inventory queries exact 1.1");
            for (uint raw = 0; raw < InventoryItemTypes.Count; ++raw)
            {
                _inventoryRaw = raw;
                Expect(inventory.Snapshot(out ScInventorySnapshot snap) == ScoResult.Ok && snap.count == 1 && snap.HasDetails && snap.flags == 3 && snap.total_seen == 129,
                       $"inventory snapshot {raw}");
                var port = snap.Port(0);
                Expect(port.item_class_id == raw && (uint)port.ItemType == raw && port.ItemTypeName == ((ScItemType)raw).ToString() && port.entity_id == ulong.MaxValue,
                       $"inventory mapped category {raw}");
            }
            foreach (ulong raw in new ulong[] { 192, 999, uint.MaxValue, 0x100000016, 1UL << 63, ulong.MaxValue })
            {
                _inventoryRaw = raw;
                Expect(inventory.Snapshot(out ScInventorySnapshot snap) == ScoResult.Ok && snap.Port(0).item_class_id == raw &&
                       snap.Port(0).ItemType == ScItemType.Unknown && snap.Port(0).ItemTypeName == "UNDEFINED", "inventory preserves unknown uint64");
            }
            _inventoryValid = 0;
            foreach (ulong raw in new ulong[] { 0, 22 })
            {
                _inventoryRaw = raw;
                Expect(inventory.Snapshot(out ScInventorySnapshot invalid) == ScoResult.Ok && invalid.Port(0).item_class_id == raw &&
                       invalid.Port(0).ItemType == ScItemType.Unknown && invalid.Port(0).ItemTypeName == "UNDEFINED",
                       "invalid port maps unknown without dropping raw, including AIModule zero");
            }
            _inventoryValid = 1;
            _inventoryVersion = GameAbi.InventoryVersion1_0;
            Expect(inventory.Open(p) == ScoResult.Ok && _inventoryQueriedVersion == GameAbi.InventoryVersion1_0 &&
                   inventory.Snapshot(out ScInventorySnapshot old) == ScoResult.Ok && old.size == 3088 && !old.HasDetails && old.flags == 0 && old.total_seen == 0,
                   "inventory 1.0 prefix fallback has no tail");
            _inventoryVersion = GameAbi.InventoryVersion1_1;
            Expect(inventory.Open(p) == ScoResult.Ok, "inventory reopens 1.1");
            int calls = _inventoryCalls;
            for (uint size = 0; size < 16; ++size)
            {
                _inventory->size = size;
                Expect(inventory.Snapshot(out _) == ScoResult.Unavailable && _inventoryCalls == calls, "inventory incomplete member never called");
                Expect(new Inventory().Open(p) == (size < 4 ? ScoResult.BadArg : ScoResult.Unavailable), "inventory incomplete table refuses open");
            }
            _inventory->size = 16; _inventory->snapshot = null;
            Expect(inventory.Snapshot(out _) == ScoResult.Unavailable && new Inventory().Open(p) == ScoResult.Unavailable && _inventoryCalls == calls,
                   "inventory null method never called");
            _inventory->snapshot = &InvSnapshot;
            _inventoryCapability = false;
            Expect(inventory.Snapshot(out _) == ScoResult.Unavailable && _inventoryCalls == calls, "inventory capability off never called");
            _inventoryCapability = true;
            _inventoryResult = ScoResult.Unavailable;
            Expect(inventory.Snapshot(out ScInventorySnapshot missing) == ScoResult.Unavailable && missing.count == 0, "inventory unavailable leaves no snapshot");
            _inventoryResult = ScoResult.Ok;
            _inventoryBadSize = true;
            Expect(inventory.Snapshot(out ScInventorySnapshot shortValue) == ScoResult.Failed && shortValue.count == 0, "inventory malformed snapshot size discarded");
            _inventoryBadSize = false; _inventoryBadCount = true;
            Expect(inventory.Snapshot(out ScInventorySnapshot longValue) == ScoResult.Failed && longValue.count == 0, "inventory over-capacity snapshot discarded");
            _inventoryBadCount = false;
            var api = p.Api;
            uint rootSize = api->size;
            calls = _inventoryCalls;
            api->size = 23;
            Expect(inventory.Snapshot(out _) == ScoResult.Unavailable && _inventoryCalls == calls, "inventory partial root has never called");
            api->size = rootSize;
            var has = api->has;
            api->has = null;
            Expect(inventory.Snapshot(out _) == ScoResult.Unavailable && _inventoryCalls == calls, "inventory null root has never called");
            api->has = has;
            api->size = ApiOffset.query_service + 7;
            Expect(new Inventory().Open(p) == ScoResult.Unavailable, "inventory partial root query never called");
            api->size = rootSize;
            var query = api->query_service;
            api->query_service = null;
            Expect(new Inventory().Open(p) == ScoResult.Unavailable, "inventory null root query never called");
            api->query_service = query;
            _inventoryNull = true;
            Expect(new Inventory().Open(p) == ScoResult.BadArg && _inventoryCalls == calls, "inventory null service table rejected");
            _inventoryNull = false;
            _inventoryAvailable = false;
            Expect(inventory.Open(p) == ScoResult.NotFound && !inventory.IsOpen, "inventory missing table clears borrowed state");
            _inventoryAvailable = true;
            Expect(inventory.Open(p) == ScoResult.Ok, "inventory restores provider");
            _inventoryAfterUnload = inventory;
        }

        static partial void GameAfterUnload()
        {
            int calls = _inventoryCalls;
            Expect(_inventoryAfterUnload!.Snapshot(out _) == ScoResult.Unavailable && _inventoryCalls == calls, "inventory never calls after consumer unload");
        }

        private static ScActorsV1* _actors;  // a stand-in game.actors
        private static ScEntitiesV1* _entities;  // a stand-in game.entities
        private static ScWorldV1* _world;    // a stand-in game.world
        private static ScCreativeV1* _creative;  // a stand-in game.creative
        private static uint _creativeFlags;      // its toggles
        private static float _creativeSpeed;
        private static bool _actorEntityServicesMissing;
        private static ScHangarV1* _hangar;      // a stand-in game.hangar
        private static uint _hangarLastOp;       // the last op a lift request handed over
        private static ScReputationV1* _reputation;  // a stand-in game.reputation
        private static ScGamerulesV1* _gamerules;    // a stand-in game.gamerules
        private static uint _gamerulesValid = 1;     // what the stand-in snapshot reports
        private static uint _gamerulesVersion = GameAbi.GameRulesVersion1_1;
        private static bool _gamerulesQueryNull;
        private static uint _gamerulesListCalls, _gamerulesListBad;
        private static ScoResult _gamerulesListResult = ScoResult.Ok;
        private static uint _freeCamValue = 1;   // what the stand-in free-camera flag reads
        private static int _freeCamCalls;

        static partial void GameQueryService(string name, uint minVersion, void** o, ref bool found)
        {
            if (name == GameAbi.InventoryName)
            {
                _inventoryQueriedVersion = minVersion;
                if (_inventoryAvailable && minVersion <= _inventoryVersion) { *o = _inventoryNull ? null : _inventory; found = true; }
                return;
            }
            if (_actorEntityServicesMissing && (name == GameAbi.ActorsName || name == GameAbi.EntitiesName)) return;
            if (name == GameAbi.ActorsName) { *o = _actors; found = true; return; }
            if (name == GameAbi.EntitiesName) { *o = _entities; found = true; return; }
            if (name == GameAbi.WorldName) { *o = _world; found = true; return; }
            if (name == GameAbi.CreativeName) { *o = _creative; found = true; return; }
            if (name == GameAbi.HangarName) { *o = _hangar; found = true; return; }
            if (name == GameAbi.ReputationName) { *o = _reputation; found = true; return; }
            if (name == GameAbi.GameRulesName)
            {
                if (minVersion <= _gamerulesVersion) { *o = _gamerulesQueryNull ? null : _gamerules; found = true; }
                return;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult HgLiftOf(void* self, ulong area, ulong* lift)
        {
            if (self != _self || area == 0 || lift == null) return ScoResult.BadArg;
            if (area != 21) return ScoResult.NotFound;
            *lift = 31;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult HgLiftRequest(void* self, ulong lift, uint op)
        {
            if (self != _self || lift != 31 || (op != 1 && op != 2)) return ScoResult.BadArg;
            _hangarLastOp = op;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult HgAtcCancel(void* self, ulong atc) => self == _self && atc == 41 ? ScoResult.Ok : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult HgLastError(void* self, byte* o, uint* io) => Bytes(self == _self ? "no lift" : "hangar-other", o, io);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RepChecks(void* self, ScReputationChecks* c)
        {
            if (self != _self || c == null || c->size < (uint)sizeof(ScReputationChecks)) return ScoResult.BadArg;
            c->sites = 3;
            c->intact = 2;
            c->services = 1;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RepLastError(void* self, byte* o, uint* io) => Bytes(self == _self ? "rep-why" : "rep-other", o, io);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult GrRead(void* self, ScGamerulesState* o)
        {
            if (self != _self || o == null || o->size < (uint)sizeof(ScGamerulesState)) return ScoResult.BadArg;
            o->valid = _gamerulesValid;
            o->generation = 7;
            o->enforce_group_qt = 1;
            o->quantum_travel_allowed = 0;
            o->quantum_boost_allowed = 1;
            o->travel_target_threshold_angle = 45f;
            o->boost_group_distance_threshold = 2500f;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult GrList(void* self, uint offset, ScGamerulesParamInfo* o,
            uint stride, uint capacity, uint* count, uint* total)
        {
            ++_gamerulesListCalls;
            if (self != _self || offset > 3 || capacity > 64 || count == null || total == null || count == total ||
                (capacity == 0 ? o != null : o == null || stride != (uint)sizeof(ScGamerulesParamInfo))) return ScoResult.BadArg;
            uint n = Math.Min(capacity, 3 - offset);
            for (uint i = 0; i < n; ++i)
            {
                if (o[i].size != (uint)sizeof(ScGamerulesParamInfo)) return ScoResult.BadArg;
                o[i].type = offset + i == 2 ? GameAbi.GameRulesParamFloat : GameAbi.GameRulesParamInt;
                string name = "synthetic_" + (offset + i), group = offset + i == 2 ? "" : "test";
                for (int j = 0; j < 48; ++j) o[i].name[j] = j < name.Length ? (byte)name[j] : (byte)0;
                for (int j = 0; j < 32; ++j) o[i].group[j] = j < group.Length ? (byte)group[j] : (byte)0;
            }
            *count = _gamerulesListBad == 1 ? capacity + 1 : n;
            *total = _gamerulesListBad == 5 ? 0u : 3u;
            if (n != 0 && _gamerulesListBad == 2) new Span<byte>(o[0].name, 48).Fill((byte)'x');
            if (n != 0 && _gamerulesListBad == 3) o[0].size = 0;
            if (n != 0 && _gamerulesListBad == 4) { o[0].name[0] = 0xFF; o[0].name[1] = 0; }
            if (n != 0 && _gamerulesListBad == 6) new Span<byte>(o[0].group, 32).Fill((byte)'x');
            return _gamerulesListResult;
        }

        private static void GameRulesListTests(GameRules rules)
        {
            Expect(new GameRules().ListParams(0, 1, out var absent, out uint total) == ScoResult.Unavailable && absent.Length == 0 && total == 0,
                "unopened GameRules enumeration is unavailable with empty outputs");
            Expect(rules.ListParams(0, 0, out var page, out total) == ScoResult.Ok && page.Length == 0 && total == 3,
                "GameRules count-only passes NULL with initialized counts");
            Expect(rules.ListParams(0, 2, out page, out total) == ScoResult.Ok && page.Length == 2 && total == 3 &&
                page[0] == new GameRulesParameter(GameAbi.GameRulesParamInt, "synthetic_0", "test"),
                "GameRules copies a bounded definition page at native stride88");
            var first = page;
            Expect(rules.ListParams(2, 64, out page, out total) == ScoResult.Ok && page.Length == 1 && total == 3 &&
                page[0] == new GameRulesParameter(GameAbi.GameRulesParamFloat, "synthetic_2", "") && first[0].Name == "synthetic_0",
                "partial final page and empty group preserve earlier owned strings");
            Expect(rules.ListParams(3, 1, out page, out total) == ScoResult.Ok && page.Length == 0 && total == 3,
                "offset equal total is a complete empty page");
            Expect(rules.ListParams(4, 1, out page, out total) == ScoResult.BadArg && page.Length == 0 && total == 0,
                "invalid offset result is preserved with empty outputs");
            uint calls = _gamerulesListCalls;
            Expect(rules.ListParams(0, 65, out page, out total) == ScoResult.BadArg && _gamerulesListCalls == calls,
                "oversized capacity is refused without a provider call");
            for (uint bad = 1; bad <= 6; ++bad)
            {
                _gamerulesListBad = bad;
                Expect(rules.ListParams(bad == 5 ? 1u : 0u, 2, out page, out total) == ScoResult.Failed && page.Length == 0 && total == 0,
                    "malformed count/string/size/UTF8/total refuses without exposing partial records");
            }
            _gamerulesListBad = 0;
            _gamerulesListResult = ScoResult.WrongThread;
            Expect(rules.ListParams(0, 2, out page, out total) == ScoResult.WrongThread && page.Length == 0 && total == 0,
                "provider failure is preserved and scratch output is discarded");
            _gamerulesListResult = ScoResult.Ok;
            _gamerulesVersion = GameAbi.GameRulesVersion1_0;
            calls = _gamerulesListCalls;
            Expect(rules.ListParams(0, 1, out page, out total) == ScoResult.NotFound && _gamerulesListCalls == calls && rules.Read(out _) == ScoResult.Ok,
                "old service never calls a 1.1 tail and keeps Read working");
            _gamerulesVersion = GameAbi.GameRulesVersion1_1;
            _gamerulesQueryNull = true;
            Expect(rules.ListParams(0, 1, out page, out total) == ScoResult.BadArg && _gamerulesListCalls == calls,
                "Query refuses a successful NULL table as BadArg without a tail call");
            _gamerulesQueryNull = false;
            Expect(rules.ListParams(0, 1, out page, out total) == ScoResult.Ok, "enumeration recovers after refused provider cases");
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult WorldFreeCam(uint* set)
        {
            ++_freeCamCalls;
            *set = _freeCamValue;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActLocal(ulong* actor, ulong* entity)
        {
            *actor = 5;
            *entity = 6;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActSpawn(void* self, byte* cls, ulong zone, double* pos, ulong* id)
        {
            if (self != _self || S(cls) != "Npc" || zone != 9 || pos[2] != 3.0) return ScoResult.BadArg;
            *id = 77;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActDespawn(void* self, ulong id) => self == _self && id == 77 ? ScoResult.Ok : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActLastError(void* self, byte* o, uint* io)
        {
            byte[] why = System.Text.Encoding.UTF8.GetBytes((self == null ? "read" : "not yours") + "\0");
            uint cap = *io;
            *io = (uint)why.Length;
            if (cap < why.Length) return ScoResult.TooMany;
            for (int i = 0; i < why.Length; ++i) o[i] = why[i];
            return ScoResult.Ok;
        }

        private static ScoResult Bytes(string s, byte* o, uint* io)
        {
            byte[] b = System.Text.Encoding.UTF8.GetBytes(s + "\0");
            uint cap = *io;
            *io = (uint)b.Length;
            if (cap < b.Length) return ScoResult.TooMany;
            for (int i = 0; i < b.Length; ++i) o[i] = b[i];
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int EntAlive(ulong id) => id == 5 ? 1 : 0;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntClassOf(ulong id, byte* o, uint* io) => id == 5 ? Bytes("AEGS_Avenger_Titan", o, io) : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntGet(ulong id, double* pos, double* rot, ulong* zone)
        {
            if (id != 5) return ScoResult.NotFound;
            pos[0] = 1; pos[1] = 2; pos[2] = 3;
            rot[0] = 0; rot[1] = 0; rot[2] = 0; rot[3] = 1;
            *zone = 9;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntSet(void* self, ulong id, ulong zone, double* pos, double* rot) =>
            self == _self && id == 5 && zone == 9 && pos[2] == 3.0 && rot[3] == 1.0 ? ScoResult.Ok : ScoResult.Failed;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntSpawn(void* self, byte* cls, ulong zone, double* pos, double* rot, ulong* id)
        {
            if (self != _self || S(cls) != "DRAK_Cutlass_Black" || zone != 9 || pos[2] != 3.0 || rot[3] != 1.0) return ScoResult.BadArg;
            *id = 88;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntDespawn(void* self, ulong id) => self == _self && id == 88 ? ScoResult.Ok : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntKeep(void* self, ulong id) => self == _self && id == 89 ? ScoResult.Ok : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntWatch(void* self, uint what, byte* type, delegate* unmanaged[Cdecl]<void*, uint, ulong, byte*, void> fn, void* ctx, ulong* id)
        {
            string t = S(type);
            if (self != _self || t.Length == 0 || t == "*" || what == 0) return ScoResult.BadArg;
            *id = 3;
            byte[] cls = System.Text.Encoding.UTF8.GetBytes("AEGS_Avenger_Titan\0");
            fixed (byte* c = cls) fn(ctx, what, 42, c);   // the host would call this later, on the game thread
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntUnwatch(void* self, ulong id) => self == _self && id == 3 ? ScoResult.Ok : ScoResult.NotFound;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntQuery(ulong zone, double* pos, double radius, byte* filter, ulong* ids, uint max, uint* count, uint* more)
        {
            if (zone != 9 || radius != 50.0 || S(filter) != "AEGS_*") return ScoResult.BadArg;
            uint n = 0;
            for (ulong v = 7; v <= 9; ++v)
                if (n < max) ids[n++] = v; else ++*more;
            *count = n;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult EntLastError(void* self, byte* o, uint* io) => Bytes(self == null ? "ent-read" : "ent-own", o, io);

        private static int _watchHits;
        private static ulong _watchId;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void WatchFn(void* ctx, uint what, ulong id, byte* cls)
        {
            if (ctx == _self && what == GameAbi.EntityStreamedIn && id == 42 && S(cls) == "AEGS_Avenger_Titan") ++_watchHits;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActHealth(ulong id, float* cur, float* max)
        {
            *cur = 0;
            *max = 0;
            if (id != 6) return ScoResult.NotFound;
            *cur = 87.5f;
            return ScoResult.Ok;
        }

        private static int _actorStateCalls;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActState(ulong id, uint* state)
        {
            ++_actorStateCalls;
            *state = 0;
            return ScoResult.Unavailable;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult WorldRaycast(ulong zone, double* from, double* dir, double max, ScWorldHit* hit)
        {
            if (hit == null || hit->size < (uint)sizeof(ScWorldHit) || zone == 0) return ScoResult.BadArg;
            if (zone != 9) return ScoResult.NotFound;
            hit->pos[0] = from[0] + dir[0] * max;
            hit->pos[1] = from[1] + dir[1] * max;
            hit->pos[2] = from[2] + dir[2] * max;
            hit->distance = max;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult WorldCamera(double* pos, double* rot, ulong* zone)
        {
            pos[0] = 1; pos[1] = 2; pos[2] = 3;
            rot[0] = 0; rot[1] = 0; rot[2] = 0; rot[3] = 1;
            *zone = 9;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult WorldLastError(void* self, byte* o, uint* io)
        {
            byte[] why = System.Text.Encoding.UTF8.GetBytes("no hit\0");
            uint cap = *io;
            *io = (uint)why.Length;
            if (cap < why.Length) return ScoResult.TooMany;
            for (int i = 0; i < why.Length; ++i) o[i] = why[i];
            return ScoResult.Ok;
        }

        private static ScoResult CrToggle(void* self, int on, uint bit)
        {
            if (self == null) return ScoResult.BadArg;
            _creativeFlags = on != 0 ? _creativeFlags | bit : _creativeFlags & ~bit;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrGod(void* self, int on) => CrToggle(self, on, 0x1);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrFly(void* self, int on) => CrToggle(self, on, 0x2);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrSpeed(void* self, float mps)
        {
            if (!(mps > 0 && mps <= GameAbi.CreativeFlySpeedMax)) return ScoResult.BadArg;
            _creativeSpeed = mps;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrAmmo(void* self, int on) => CrToggle(self, on, 0x4);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrShipAmmo(void* self, int on) => CrToggle(self, on, 0x8);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrState(void* self, uint* flags)
        {
            *flags = _creativeFlags;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult CrLastError(void* self, byte* o, uint* io)
        {
            byte[] why = System.Text.Encoding.UTF8.GetBytes("speed must be above 0\0");
            uint cap = *io;
            *io = (uint)why.Length;
            if (cap < why.Length) return ScoResult.TooMany;
            for (int i = 0; i < why.Length; ++i) o[i] = why[i];
            return ScoResult.Ok;
        }

        static partial void GameSetUp()
        {
            _inventory = (ScInventoryV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScInventoryV1));
            _inventory->size = (uint)sizeof(ScInventoryV1);
            _inventory->snapshot = &InvSnapshot;
            _actors = (ScActorsV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScActorsV1));
            _actors->size = (uint)sizeof(ScActorsV1);
            _actors->local_player = &ActLocal;
            _actors->spawn_npc = &ActSpawn;
            _actors->despawn = &ActDespawn;
            _actors->last_error = &ActLastError;
            _entities = (ScEntitiesV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScEntitiesV1));
            _entities->size = (uint)sizeof(ScEntitiesV1);
            _entities->alive = &EntAlive;
            _entities->class_of = &EntClassOf;
            _entities->get_transform = &EntGet;
            _entities->set_transform = &EntSet;
            _entities->spawn = &EntSpawn;
            _entities->despawn = &EntDespawn;
            _entities->watch = &EntWatch;
            _entities->unwatch = &EntUnwatch;
            _entities->query_radius = &EntQuery;
            _entities->last_error = &EntLastError;
            _entities->keep = &EntKeep;
            _actors->health = &ActHealth;
            _actors->state = &ActState;
            _world = (ScWorldV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScWorldV1));
            _world->size = (uint)sizeof(ScWorldV1);
            _world->raycast = &WorldRaycast;
            _world->camera = &WorldCamera;
            _world->last_error = &WorldLastError;
            _world->free_camera_flag = &WorldFreeCam;
            _hangar = (ScHangarV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScHangarV1));
            _hangar->size = (uint)sizeof(ScHangarV1);
            _hangar->lift_of_landing_area = &HgLiftOf;
            _hangar->lift_request = &HgLiftRequest;
            _hangar->atc_cancel_request = &HgAtcCancel;
            _hangar->last_error = &HgLastError;
            _reputation = (ScReputationV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScReputationV1));
            _reputation->size = (uint)sizeof(ScReputationV1);
            _reputation->checks = &RepChecks;
            _reputation->last_error = &RepLastError;
            _gamerules = (ScGamerulesV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScGamerulesV1));
            _gamerules->size = (uint)sizeof(ScGamerulesV1);
            _gamerules->read = &GrRead;
            _gamerules->list_params = &GrList;
            _creative = (ScCreativeV1*)NativeMemory.AllocZeroed((nuint)sizeof(ScCreativeV1));
            _creative->size = (uint)sizeof(ScCreativeV1);
            _creative->set_god_mode = &CrGod;
            _creative->set_fly_mode = &CrFly;
            _creative->set_fly_speed = &CrSpeed;
            _creative->set_infinite_ammo = &CrAmmo;
            _creative->set_infinite_ship_ammo = &CrShipAmmo;
            _creative->state = &CrState;
            _creative->last_error = &CrLastError;
        }

        // Every callback is checked with a partially covered pointer, its exact extent,
        // and a fully covered null pointer. Keep the backing allocation valid while its
        // advertised size changes so a failure is observable without an access violation.
        private static void CheckGameFields<T>(T* table, (string Field, Func<bool> Unavailable, Func<bool> Available)[] cases)
            where T : unmanaged
        {
            T saved = *table;
            try
            {
                foreach (var test in cases)
                {
                    uint offset = (uint)Marshal.OffsetOf<T>(test.Field).ToInt32();
                    *(uint*)table = offset + (uint)sizeof(nint) - 1;
                    Expect(test.Unavailable(), $"{typeof(T).Name}.{test.Field} rejects a partial pointer");
                    *(uint*)table = offset + (uint)sizeof(nint);
                    Expect(test.Available(), $"{typeof(T).Name}.{test.Field} accepts its exact extent");
                    *table = saved;
                    *(nint*)((byte*)table + offset) = 0;
                    Expect(test.Unavailable(), $"{typeof(T).Name}.{test.Field} rejects a null pointer");
                    *table = saved;
                }
            }
            finally { *table = saved; }
        }

        private static void MalformedActorEntityTests(Plugin p)
        {
            var actors = new Actors();
            var entities = new Entities();
            double[] pos = { 1, 2, 3 }, rot = { 0, 0, 0, 1 };
            var actorCases = new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScActorsV1.local_player),
                    () => actors.LocalPlayer(out ulong a, out ulong e) == ScoResult.Unavailable && a == 0 && e == 0,
                    () => actors.LocalPlayer(out ulong a, out ulong e) == ScoResult.Ok && a == 5 && e == 6),
                (nameof(ScActorsV1.spawn_npc),
                    () => actors.SpawnNpc("Npc", 9, pos, out ulong id) == ScoResult.Unavailable && id == 0,
                    () => actors.SpawnNpc("Npc", 9, pos, out ulong id) == ScoResult.Ok && id == 77),
                (nameof(ScActorsV1.despawn),
                    () => actors.Despawn(77) == ScoResult.Unavailable,
                    () => actors.Despawn(77) == ScoResult.Ok),
                (nameof(ScActorsV1.last_error),
                    () => actors.LastError() == "" && actors.LastReadError() == "",
                    () => actors.LastError() == "not yours" && actors.LastReadError() == "read"),
                (nameof(ScActorsV1.health),
                    () => actors.Health(6, out float cur, out float max) == ScoResult.Unavailable && cur == 0 && max == 0,
                    () => actors.Health(6, out float cur, out float max) == ScoResult.Ok && cur == 87.5f && max == 0),
                (nameof(ScActorsV1.state),
                    () => { int calls = _actorStateCalls; return actors.State(6, out ScActorState state) == ScoResult.Unavailable && state == ScActorState.Alive && _actorStateCalls == calls; },
                    () => { int calls = _actorStateCalls; return actors.State(6, out _) == ScoResult.Unavailable && _actorStateCalls == calls + 1; }),
            };
            var entityCases = new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScEntitiesV1.alive), () => !entities.Alive(5), () => entities.Alive(5)),
                (nameof(ScEntitiesV1.class_of),
                    () => entities.ClassOf(5, out string cls) == ScoResult.Unavailable && cls == "",
                    () => entities.ClassOf(5, out string cls) == ScoResult.Ok && cls != ""),
                (nameof(ScEntitiesV1.get_transform),
                    () => entities.GetTransform(5, pos, rot, out ulong zone) == ScoResult.Unavailable && zone == 0 && pos[2] == 3 && rot[3] == 1,
                    () => entities.GetTransform(5, pos, rot, out ulong zone) == ScoResult.Ok && zone == 9),
                (nameof(ScEntitiesV1.set_transform),
                    () => entities.SetTransform(5, 9, pos, rot) == ScoResult.Unavailable,
                    () => entities.SetTransform(5, 9, pos, rot) == ScoResult.Ok),
                (nameof(ScEntitiesV1.spawn),
                    () => entities.Spawn("DRAK_Cutlass_Black", 9, pos, rot, out ulong id) == ScoResult.Unavailable && id == 0,
                    () => entities.Spawn("DRAK_Cutlass_Black", 9, pos, rot, out ulong id) == ScoResult.Ok && id == 88),
                (nameof(ScEntitiesV1.despawn), () => entities.Despawn(88) == ScoResult.Unavailable, () => entities.Despawn(88) == ScoResult.Ok),
                (nameof(ScEntitiesV1.watch),
                    () => entities.Watch(GameAbi.EntityStreamedIn, "AEGS_*", &WatchFn, _self, out ulong id) == ScoResult.Unavailable && id == 0,
                    () => entities.Watch(GameAbi.EntityStreamedIn, "AEGS_*", &WatchFn, _self, out ulong id) == ScoResult.Ok && id == 3),
                (nameof(ScEntitiesV1.unwatch), () => entities.Unwatch(3) == ScoResult.Unavailable, () => entities.Unwatch(3) == ScoResult.Ok),
                (nameof(ScEntitiesV1.query_radius),
                    () => entities.QueryRadius(9, pos, 50, "AEGS_*", new ulong[2], out uint count, out uint more) == ScoResult.Unavailable && count == 0 && more == 0,
                    () => entities.QueryRadius(9, pos, 50, "AEGS_*", new ulong[2], out uint count, out uint more) == ScoResult.Ok && count == 2 && more == 1),
                (nameof(ScEntitiesV1.last_error),
                    () => entities.LastError() == "" && entities.LastReadError() == "",
                    () => entities.LastError() == "ent-own" && entities.LastReadError() == "ent-read"),
                (nameof(ScEntitiesV1.keep), () => entities.Keep(89) == ScoResult.Unavailable, () => entities.Keep(89) == ScoResult.Ok),
            };
            Expect(actors.Open(p) == ScoResult.Ok && entities.Open(p) == ScoResult.Ok, "malformed-table fixtures open");
            CheckGameFields(_actors, actorCases);
            CheckGameFields(_entities, entityCases);

            ScActorsV1* savedActors = _actors;
            ScEntitiesV1* savedEntities = _entities;
            uint actorSize = _actors->size, entitySize = _entities->size;
            try
            {
                foreach (uint size in new uint[] { 0, 3, 4, 39 })
                {
                    Expect(actors.Open(p) == ScoResult.Ok, "actors reopen fixture starts open");
                    _actors->size = size;
                    Expect(actors.Open(p) == (size < 4 ? ScoResult.BadArg : ScoResult.Unavailable) && !actors.IsOpen,
                        $"actors rejects size {size} and closes");
                    _actors->size = actorSize;
                    foreach (var test in actorCases) Expect(test.Unavailable(), $"actors failed reopen disables {test.Field}");
                }
                foreach (uint size in new uint[] { 0, 3, 4, 87 })
                {
                    Expect(entities.Open(p) == ScoResult.Ok, "entities reopen fixture starts open");
                    _entities->size = size;
                    Expect(entities.Open(p) == (size < 4 ? ScoResult.BadArg : ScoResult.Unavailable) && !entities.IsOpen,
                        $"entities rejects size {size} and closes");
                    _entities->size = entitySize;
                    foreach (var test in entityCases) Expect(test.Unavailable(), $"entities failed reopen disables {test.Field}");
                }
                Expect(actors.Open(p) == ScoResult.Ok && entities.Open(p) == ScoResult.Ok, "null-table fixtures start open");
                _actors = null;
                _entities = null;
                Expect(actors.Open(p) == ScoResult.BadArg && !actors.IsOpen, "actors rejects an Ok query with a null table and closes");
                Expect(entities.Open(p) == ScoResult.BadArg && !entities.IsOpen, "entities rejects an Ok query with a null table and closes");
                _actors = savedActors;
                _entities = savedEntities;
                foreach (var test in actorCases) Expect(test.Unavailable(), $"actors null-table reopen disables {test.Field}");
                foreach (var test in entityCases) Expect(test.Unavailable(), $"entities null-table reopen disables {test.Field}");

                Expect(actors.Open(p) == ScoResult.Ok && entities.Open(p) == ScoResult.Ok, "missing-service fixtures start open");
                _actorEntityServicesMissing = true;
                Expect(actors.Open(p) == ScoResult.NotFound && !actors.IsOpen, "actors closes after a failed service query");
                Expect(entities.Open(p) == ScoResult.NotFound && !entities.IsOpen, "entities closes after a failed service query");
                _actorEntityServicesMissing = false;
                foreach (var test in actorCases) Expect(test.Unavailable(), $"actors missing-service reopen disables {test.Field}");
                foreach (var test in entityCases) Expect(test.Unavailable(), $"entities missing-service reopen disables {test.Field}");

                // Actual short allocations exercise Open without a full struct behind the prefix.
                foreach (uint size in new uint[] { 4, 39, 87 })
                {
                    void* prefix = NativeMemory.AllocZeroed(size);
                    try
                    {
                        *(uint*)prefix = size;
                        _actors = (ScActorsV1*)prefix;
                        _entities = (ScEntitiesV1*)prefix;
                        if (size < 40) Expect(actors.Open(p) == ScoResult.Unavailable && !actors.IsOpen, "actors rejects a short allocation");
                        Expect(entities.Open(p) == ScoResult.Unavailable && !entities.IsOpen, "entities rejects a short allocation");
                    }
                    finally { _actors = savedActors; _entities = savedEntities; NativeMemory.Free(prefix); }
                }
                Expect(actors.Open(p) == ScoResult.Ok && actors.LocalPlayer(out _, out _) == ScoResult.Ok,
                    "actors recovers after a failed reopen");
                Expect(entities.Open(p) == ScoResult.Ok && entities.Alive(5), "entities recovers after a failed reopen");
            }
            finally
            {
                _actorEntityServicesMissing = false;
                _actors = savedActors;
                _entities = savedEntities;
                _actors->size = actorSize;
                _entities->size = entitySize;
            }
        }

        static partial void GameTests(Plugin p)
        {
            InventoryTests(p);
            var actors = new Actors();
            Expect(actors.Open(p) == ScoResult.Ok && actors.IsOpen, "game.actors opens");
            Expect(actors.LocalPlayer(out ulong actorId, out ulong entityId) == ScoResult.Ok && actorId == 5 && entityId == 6, "Actors.LocalPlayer");
            Expect(actors.SpawnNpc("Npc", 9, new double[] { 1, 2, 3 }, out ulong npc) == ScoResult.Ok && npc == 77, "Actors.SpawnNpc");
            Expect(actors.SpawnNpc("Npc", 9, new double[] { 1, 2 }, out npc) == ScoResult.BadArg && npc == 0, "SpawnNpc wants 3 coordinates");
            Expect(actors.Despawn(77) == ScoResult.Ok && actors.Despawn(78) == ScoResult.NotFound, "Actors.Despawn");
            Expect(actors.LastError() == "not yours" && actors.LastReadError() == "read", "Actors.LastError, LastReadError");
            Expect(actors.Health(6, out float hp, out float hpMax) == ScoResult.Ok && hp == 87.5f && hpMax == 0f, "Actors.Health (1.1)");
            Expect(actors.Health(7, out hp, out _) == ScoResult.NotFound && hp == 0f, "Health of an unknown id");
            Expect(actors.State(6, out ScActorState st) == ScoResult.Unavailable && st == ScActorState.Alive, "Actors.State is unavailable");
            uint savedSize = _actors->size;
            _actors->size = 40;   // a 1.0 table: the 1.1 functions are past its end
            Expect(actors.Health(6, out _, out _) == ScoResult.Unavailable && actors.State(6, out _) == ScoResult.Unavailable,
                   "a 1.0 table answers Unavailable for the 1.1 functions");
            _actors->size = savedSize;

            var creative = new Creative();
            Expect(creative.Open(p) == ScoResult.Ok && creative.IsOpen, "game.creative opens");
            Expect(creative.SetGodMode(true) == ScoResult.Ok && creative.SetFlyMode(true) == ScoResult.Ok &&
                   creative.SetInfiniteAmmo(true) == ScoResult.Ok && creative.SetInfiniteShipAmmo(true) == ScoResult.Ok,
                   "Creative toggles on");
            Expect(creative.State(out ScCreativeFlag cf) == ScoResult.Ok &&
                   cf == (ScCreativeFlag.GodMode | ScCreativeFlag.Fly | ScCreativeFlag.InfiniteAmmo | ScCreativeFlag.InfiniteShipAmmo),
                   "Creative.State reads the toggles back");
            Expect(creative.SetFlyMode(false) == ScoResult.Ok && creative.SetInfiniteAmmo(false) == ScoResult.Ok &&
                   creative.State(out cf) == ScoResult.Ok && cf == (ScCreativeFlag.GodMode | ScCreativeFlag.InfiniteShipAmmo),
                   "Creative toggles off");
            Expect(creative.SetFlySpeed(250f) == ScoResult.Ok && _creativeSpeed == 250f, "Creative.SetFlySpeed");
            Expect(creative.SetFlySpeed(0f) == ScoResult.BadArg && creative.LastError() == "speed must be above 0", "Creative.LastError");
            Expect(new Creative().SetGodMode(true) == ScoResult.Unavailable, "a Creative that isn't open answers Unavailable");

            var world = new World();
            Expect(world.Open(p) == ScoResult.Ok && world.IsOpen, "game.world opens");
            Expect(world.Raycast(9, new double[] { 0, 0, 1 }, new double[] { 0, 0, -1 }, 10, out WorldHit hit) == ScoResult.Ok
                   && hit.Z == -9 && hit.Distance == 10 && hit.EntityId == 0 && hit.Flags == 0, "World.Raycast");
            Expect(world.Raycast(8, new double[] { 0, 0, 1 }, new double[] { 0, 0, -1 }, 10, out hit) == ScoResult.NotFound && hit == default,
                   "Raycast of an unknown zone");
            Expect(world.Raycast(0, new double[] { 0, 0, 1 }, new double[] { 0, 0, -1 }, 10, out _) == ScoResult.BadArg, "Raycast zone 0");
            Expect(world.Raycast(9, new double[] { 0, 0 }, new double[] { 0, 0, -1 }, 10, out _) == ScoResult.BadArg, "Raycast wants 3 coordinates");
            Expect(world.Camera(out WorldCamera cam) == ScoResult.Ok && cam.X == 1 && cam.Z == 3 && cam.Qw == 1 && cam.ZoneId == 9, "World.Camera");
            Expect(world.LastError() == "no hit", "World.LastError");
            Expect(world.TryGetFreeCameraFlag(out bool freeCam) == ScoResult.Ok && freeCam, "World.TryGetFreeCameraFlag (1.1)");
            _freeCamValue = 0;
            Expect(world.TryGetFreeCameraFlag(out freeCam) == ScoResult.Ok && !freeCam, "TryGetFreeCameraFlag reads a zero flag as false");
            _freeCamValue = 1;
            uint worldSize = _world->size;
            int freeCamCalls = _freeCamCalls;
            _world->size = 32;   // a 1.0 table: free_camera_flag is past its end
            Expect(world.Open(p) == ScoResult.Ok && world.Camera(out _) == ScoResult.Ok, "a 1.0 world table still opens and answers its 1.0 calls");
            Expect(world.TryGetFreeCameraFlag(out freeCam) == ScoResult.Unavailable && !freeCam && _freeCamCalls == freeCamCalls,
                   "a 1.0 world table answers Unavailable without calling free_camera_flag");
            _world->size = worldSize;
            Expect(new World().TryGetFreeCameraFlag(out freeCam) == ScoResult.Unavailable && !freeCam, "a World that isn't open answers Unavailable");

            var hangar = new Hangar();
            Expect(new Hangar().LiftRequest(31, ScHangarLiftOp.Open) == ScoResult.Unavailable && new Hangar().LastError() == "",
                   "a Hangar that isn't open answers Unavailable");
            Expect(hangar.Open(p) == ScoResult.Ok && hangar.IsOpen, "game.hangar opens");
            Expect(hangar.LiftOfLandingArea(21, out ulong lift) == ScoResult.Ok && lift == 31, "Hangar.LiftOfLandingArea");
            Expect(hangar.LiftOfLandingArea(22, out lift) == ScoResult.NotFound && lift == 0, "LiftOfLandingArea without a lift");
            Expect(hangar.LiftRequest(31, ScHangarLiftOp.Open) == ScoResult.Ok && _hangarLastOp == 1 &&
                   hangar.LiftRequest(31, ScHangarLiftOp.Close) == ScoResult.Ok && _hangarLastOp == 2, "Hangar.LiftRequest passes the op");
            Expect(hangar.LiftRequest(31, (ScHangarLiftOp)7) == ScoResult.BadArg, "LiftRequest of an unknown op is the host's BadArg");
            Expect(hangar.AtcCancelRequest(41) == ScoResult.Ok && hangar.AtcCancelRequest(42) == ScoResult.NotFound, "Hangar.AtcCancelRequest");
            Expect(hangar.LastError() == "no lift", "Hangar.LastError passes this plugin's handle");
            uint hangarSize = _hangar->size;
            _hangar->size = 39;   // shorter than the 1.0 table
            Expect(hangar.Open(p) == ScoResult.Unavailable && !hangar.IsOpen, "hangar rejects a short table and closes");
            _hangar->size = hangarSize;
            Expect(hangar.AtcCancelRequest(41) == ScoResult.Unavailable, "a failed hangar reopen disables its calls");
            Expect(hangar.Open(p) == ScoResult.Ok, "hangar recovers after a failed reopen");

            var gr = new GameRules();
            Expect(sizeof(ScGamerulesV1) == 24 && sizeof(ScGamerulesState) == 56 && sizeof(ScGamerulesParamInfo) == 88 &&
                   Marshal.OffsetOf<ScGamerulesV1>(nameof(ScGamerulesV1.read)).ToInt32() == 8 &&
                   Marshal.OffsetOf<ScGamerulesV1>(nameof(ScGamerulesV1.list_params)).ToInt32() == 16 &&
                   Marshal.OffsetOf<ScGamerulesParamInfo>(nameof(ScGamerulesParamInfo.name)).ToInt32() == 8 &&
                   Marshal.OffsetOf<ScGamerulesParamInfo>(nameof(ScGamerulesParamInfo.group)).ToInt32() == 56 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.generation)).ToInt32() == 8 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.enforce_group_qt)).ToInt32() == 16 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.quantum_travel_allowed)).ToInt32() == 20 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.quantum_boost_allowed)).ToInt32() == 24 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.travel_target_threshold_angle)).ToInt32() == 28 &&
                   Marshal.OffsetOf<ScGamerulesState>(nameof(ScGamerulesState.boost_group_distance_threshold)).ToInt32() == 48 &&
                   GameAbi.GameRulesVersion1_0 == 0x00010000u && GameAbi.GameRulesName == "game.gamerules",
                   "game.gamerules layout matches tests/abi_game_gamerules.c");
            Expect(new GameRules().Read(out ScGamerulesState g) == ScoResult.Unavailable && g.valid == 0 && g.size == (uint)sizeof(ScGamerulesState),
                   "a GameRules that isn't open answers Unavailable with a zeroed, sized struct");
            Expect(gr.Open(p) == ScoResult.Ok && gr.IsOpen, "game.gamerules opens");
            Expect(gr.Read(out g) == ScoResult.Ok && g.valid == 1 && g.generation == 7 && g.enforce_group_qt == 1 &&
                   g.quantum_travel_allowed == 0 && g.quantum_boost_allowed == 1 &&
                   g.travel_target_threshold_angle == 45f && g.boost_group_distance_threshold == 2500f,
                   "GameRules.Read passes this plugin's handle and returns the snapshot");
            _gamerulesValid = 0;
            Expect(gr.Read(out g) == ScoResult.Ok && g.valid == 0, "GameRules.Read reports valid == 0 before a parameter set arrives");
            _gamerulesValid = 1;
            uint grSize = _gamerules->size;
            _gamerules->size = 15;   // shorter than the 1.0 table
            Expect(gr.Open(p) == ScoResult.Unavailable && !gr.IsOpen, "gamerules rejects a short table and closes");
            _gamerules->size = grSize;
            Expect(gr.Read(out g) == ScoResult.Unavailable && g.valid == 0, "a failed gamerules reopen disables Read");
            Expect(gr.Open(p) == ScoResult.Ok, "gamerules recovers after a failed reopen");
            GameRulesListTests(gr);
            var rep = new Reputation();
            Expect(new Reputation().Checks(out ScReputationChecks rc) == ScoResult.Unavailable && rc.size == (uint)sizeof(ScReputationChecks) && rc.sites == 0,
                   "a Reputation that isn't open answers Unavailable with a zeroed, sized struct");
            Expect(rep.Open(p) == ScoResult.Ok && rep.IsOpen, "game.reputation opens");
            Expect(rep.Checks(out rc) == ScoResult.Ok && rc.sites == 3 && rc.intact == 2 && rc.services == 1, "Reputation.Checks");
            Expect(rep.LastError() == "rep-why", "Reputation.LastError passes this plugin's handle");
            uint repSize = _reputation->size;
            _reputation->size = 23;   // shorter than the 1.0 table
            Expect(rep.Open(p) == ScoResult.Unavailable && !rep.IsOpen, "reputation rejects a short table and closes");
            _reputation->size = repSize;
            Expect(rep.Checks(out rc) == ScoResult.Unavailable && rc.sites == 0, "a failed reputation reopen disables Checks");
            Expect(rep.Open(p) == ScoResult.Ok, "reputation recovers after a failed reopen");

            ScGamePlayerDied died = default;
            int diedCount = 0;
            Subscription ds = p.OnPlayerDied(e => { died = e; ++diedCount; });
            Expect(ds.Result == ScoResult.Ok, "OnPlayerDied subscribes");
            var payload = new ScGamePlayerDied { size = (uint)sizeof(ScGamePlayerDied), entity_id = 0x8000000000000016ul };
            DispatchPayload(GameAbi.EventPlayerDied, &payload);
            Expect(diedCount == 1 && died.entity_id == 0x8000000000000016ul && died.killer_id == 0, "game.player.died reaches OnPlayerDied");
            var shortPayload = new ScGamePlayerDied { size = 8, entity_id = 5 };   // shorter than this SDK's struct: dropped
            DispatchPayload(GameAbi.EventPlayerDied, &shortPayload);
            Expect(diedCount == 1, "a payload shorter than the struct is dropped");
            ds.Dispose();

            var ent = new Entities();
            Expect(ent.Open(p) == ScoResult.Ok && ent.IsOpen, "game.entities opens");
            Expect(ent.Alive(5) && !ent.Alive(6), "Entities.Alive");
            Expect(ent.ClassOf(5, out string entClass) == ScoResult.Ok && entClass == "AEGS_Avenger_Titan" && ent.ClassOf(6, out entClass) == ScoResult.NotFound && entClass == "",
                   "Entities.ClassOf (size handshake)");
            double[] ePos = new double[3], eRot = new double[4];
            Expect(ent.GetTransform(5, ePos, eRot, out ulong eZone) == ScoResult.Ok && ePos[2] == 3.0 && eRot[3] == 1.0 && eZone == 9, "Entities.GetTransform");
            Expect(ent.GetTransform(5, new double[2], eRot, out eZone) == ScoResult.BadArg && eZone == 0, "GetTransform wants 3 and 4 doubles");
            Expect(ent.SetTransform(5, 9, ePos, eRot) == ScoResult.Ok && ent.SetTransform(6, 9, ePos, eRot) == ScoResult.Failed, "Entities.SetTransform");
            Expect(ent.Spawn("DRAK_Cutlass_Black", 9, ePos, eRot, out ulong eId) == ScoResult.Ok && eId == 88, "Entities.Spawn");
            Expect(ent.Spawn("DRAK_Cutlass_Black", 9, new double[] { 1, 2 }, eRot, out eId) == ScoResult.BadArg && eId == 0, "Spawn wants 3 and 4 doubles");
            Expect(ent.Despawn(88) == ScoResult.Ok && ent.Despawn(89) == ScoResult.NotFound, "Entities.Despawn");
            Expect(ent.Keep(89) == ScoResult.Ok && ent.Keep(88) == ScoResult.NotFound, "Entities.Keep (1.1)");
            Expect(ent.Watch(GameAbi.EntityStreamedIn, "AEGS_*", &WatchFn, _self, out _watchId) == ScoResult.Ok && _watchId == 3 && _watchHits == 1,
                   "Entities.Watch calls fn(ctx, what, id, class)");
            Expect(ent.Watch(GameAbi.EntityStreamedIn, "*", &WatchFn, _self, out ulong badWatch) == ScoResult.BadArg && badWatch == 0, "Watch refuses a lone '*'");
            Expect(ent.Unwatch(3) == ScoResult.Ok && ent.Unwatch(4) == ScoResult.NotFound, "Entities.Unwatch");
            ulong[] ids = new ulong[2];
            Expect(ent.QueryRadius(9, ePos, 50.0, "AEGS_*", ids, out uint eCount, out uint eMore) == ScoResult.Ok && eCount == 2 && eMore == 1 && ids[0] == 7 && ids[1] == 8,
                   "Entities.QueryRadius (count, more)");
            Expect(ent.LastError() == "ent-own" && ent.LastReadError() == "ent-read", "Entities.LastError, LastReadError");
            Expect(ent.Watch(GameAbi.EntityStreamedIn, "AEGS_*", null, _self, out badWatch) == ScoResult.BadArg && badWatch == 0,
                "Watch rejects a null consumer callback");
            CheckGameFields(_world, new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScWorldV1.raycast),
                    () => world.Raycast(9, new double[] { 0, 0, 1 }, new double[] { 0, 0, -1 }, 10, out WorldHit h) == ScoResult.Unavailable && h == default,
                    () => world.Raycast(9, new double[] { 0, 0, 1 }, new double[] { 0, 0, -1 }, 10, out _) == ScoResult.Ok),
                (nameof(ScWorldV1.camera), () => world.Camera(out WorldCamera cam) == ScoResult.Unavailable && cam == default,
                    () => world.Camera(out _) == ScoResult.Ok),
                (nameof(ScWorldV1.last_error), () => world.LastError() == "", () => world.LastError() == "no hit"),
                (nameof(ScWorldV1.free_camera_flag),
                    () => world.TryGetFreeCameraFlag(out bool fcOff) == ScoResult.Unavailable && !fcOff,
                    () => world.TryGetFreeCameraFlag(out bool fcOn) == ScoResult.Ok && fcOn),
            });
            CheckGameFields(_hangar, new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScHangarV1.lift_of_landing_area),
                    () => hangar.LiftOfLandingArea(21, out ulong l0) == ScoResult.Unavailable && l0 == 0,
                    () => hangar.LiftOfLandingArea(21, out ulong l1) == ScoResult.Ok && l1 == 31),
                (nameof(ScHangarV1.lift_request),
                    () => hangar.LiftRequest(31, ScHangarLiftOp.Open) == ScoResult.Unavailable,
                    () => hangar.LiftRequest(31, ScHangarLiftOp.Open) == ScoResult.Ok),
                (nameof(ScHangarV1.atc_cancel_request),
                    () => hangar.AtcCancelRequest(41) == ScoResult.Unavailable,
                    () => hangar.AtcCancelRequest(41) == ScoResult.Ok),
                (nameof(ScHangarV1.last_error), () => hangar.LastError() == "", () => hangar.LastError() == "no lift"),
            });
            CheckGameFields(_reputation, new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScReputationV1.checks),
                    () => rep.Checks(out ScReputationChecks c0) == ScoResult.Unavailable && c0.sites == 0,
                    () => rep.Checks(out ScReputationChecks c1) == ScoResult.Ok && c1.sites == 3),
                (nameof(ScReputationV1.last_error), () => rep.LastError() == "", () => rep.LastError() == "rep-why"),
            });
            CheckGameFields(_gamerules, new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScGamerulesV1.read),
                    () => gr.Read(out ScGamerulesState g0) == ScoResult.Unavailable && g0.valid == 0 && g0.size == (uint)sizeof(ScGamerulesState),
                    () => gr.Read(out ScGamerulesState g1) == ScoResult.Ok && g1.valid == 1 && g1.generation == 7),
                (nameof(ScGamerulesV1.list_params),
                    () => { ScoResult result = gr.ListParams(0, 1, out var page0, out uint total0);
                        return result == (_gamerules->size < 4 ? ScoResult.BadArg : ScoResult.Unavailable) && page0.Length == 0 && total0 == 0; },
                    () => gr.ListParams(0, 1, out var page1, out uint total1) == ScoResult.Ok && page1.Length == 1 && total1 == 3),
            });
            CheckGameFields(_creative, new (string Field, Func<bool> Unavailable, Func<bool> Available)[]
            {
                (nameof(ScCreativeV1.set_god_mode), () => creative.SetGodMode(true) == ScoResult.Unavailable, () => creative.SetGodMode(true) == ScoResult.Ok),
                (nameof(ScCreativeV1.set_fly_mode), () => creative.SetFlyMode(true) == ScoResult.Unavailable, () => creative.SetFlyMode(true) == ScoResult.Ok),
                (nameof(ScCreativeV1.set_fly_speed), () => creative.SetFlySpeed(250) == ScoResult.Unavailable, () => creative.SetFlySpeed(250) == ScoResult.Ok),
                (nameof(ScCreativeV1.set_infinite_ammo), () => creative.SetInfiniteAmmo(true) == ScoResult.Unavailable, () => creative.SetInfiniteAmmo(true) == ScoResult.Ok),
                (nameof(ScCreativeV1.set_infinite_ship_ammo), () => creative.SetInfiniteShipAmmo(true) == ScoResult.Unavailable, () => creative.SetInfiniteShipAmmo(true) == ScoResult.Ok),
                (nameof(ScCreativeV1.state), () => creative.State(out ScCreativeFlag f) == ScoResult.Unavailable && f == 0, () => creative.State(out _) == ScoResult.Ok),
                (nameof(ScCreativeV1.last_error), () => creative.LastError() == "", () => creative.LastError() == "speed must be above 0"),
            });
            MalformedActorEntityTests(p);
        }
    }
}
