// GameFakeHost.cs: the Star Citizen extension's part of the fake-host test: stand-ins for game.actors,
// game.entities, game.world and game.creative, and the tests of their C# wrappers and of the game.*
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
        private static ScActorsV1* _actors;  // a stand-in game.actors
        private static ScEntitiesV1* _entities;  // a stand-in game.entities
        private static ScWorldV1* _world;    // a stand-in game.world
        private static ScCreativeV1* _creative;  // a stand-in game.creative
        private static uint _creativeFlags;      // its toggles
        private static float _creativeSpeed;
        private static bool _actorEntityServicesMissing;

        static partial void GameQueryService(string name, void** o, ref bool found)
        {
            if (_actorEntityServicesMissing && (name == GameAbi.ActorsName || name == GameAbi.EntitiesName)) return;
            if (name == GameAbi.ActorsName) { *o = _actors; found = true; return; }
            if (name == GameAbi.EntitiesName) { *o = _entities; found = true; return; }
            if (name == GameAbi.WorldName) { *o = _world; found = true; return; }
            if (name == GameAbi.CreativeName) { *o = _creative; found = true; return; }
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
            MalformedActorEntityTests(p);
        }
    }
}
