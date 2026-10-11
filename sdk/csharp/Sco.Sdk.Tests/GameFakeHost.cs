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

        static partial void GameQueryService(string name, void** o, ref bool found)
        {
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

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ActState(ulong id, uint* state)
        {
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
        }
    }
}
