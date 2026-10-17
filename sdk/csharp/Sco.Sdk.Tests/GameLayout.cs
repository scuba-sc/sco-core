// GameLayout.cs: the Star Citizen extension's part of the layout test: the types, constants and
// names of sco_datacore.h and the sc_*.h game services, pinned by tests/abi_datacore.c and
// tests/abi_game_*.c. Part of sco-sdk-game-sc; the core SDK's test runs without it.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Sco.Sdk.Game;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static partial class Layout
    {
        static partial void AddGame(Dictionary<string, Type> types, Dictionary<string, long> constants, Dictionary<string, string> strings)
        {
            types.Add("sco_dc_type", typeof(ScoDcType));
            types.Add("sco_dc_value", typeof(ScoDcValue));
            types.Add("sco_dc_report", typeof(ScoDcReport));
            types.Add("sco_dc_applied", typeof(ScoDcApplied));
            types.Add("sco_datacore_v1", typeof(ScoDatacoreV1));
            types.Add("sc_actors_v1", typeof(ScActorsV1));
            types.Add("sc_actor_state", typeof(ScActorState));
            types.Add("sc_world_hit_flag", typeof(ScWorldHitFlag));
            types.Add("sc_world_hit", typeof(ScWorldHit));
            types.Add("sc_world_v1", typeof(ScWorldV1));
            types.Add("sc_game_player_spawned", typeof(ScGamePlayerSpawned));
            types.Add("sc_game_player_died", typeof(ScGamePlayerDied));
            types.Add("sc_game_actor_died", typeof(ScGameActorDied));
            types.Add("sc_game_zone_changed", typeof(ScGameZoneChanged));
            types.Add("sc_game_vehicle_seat", typeof(ScGameVehicleSeat));
            types.Add("sc_game_mission_changed", typeof(ScGameMissionChanged));
            types.Add("sc_game_loadout_respawn_completed", typeof(ScGameLoadoutRespawnCompleted));
            types.Add("sc_entities_v1", typeof(ScEntitiesV1));
            types.Add("sc_vehicle_seat_flag", typeof(ScVehicleSeatFlag));
            types.Add("sc_vehicle_seat", typeof(ScVehicleSeat));
            types.Add("sc_vehicles_v1", typeof(ScVehiclesV1));
            types.Add("sc_creative_flag", typeof(ScCreativeFlag));
            types.Add("sc_creative_v1", typeof(ScCreativeV1));
            types.Add("sc_hangar_lift_op", typeof(ScHangarLiftOp));
            types.Add("sc_hangar_v1", typeof(ScHangarV1));
            types.Add("sc_reputation_checks", typeof(ScReputationChecks));
            types.Add("sc_reputation_v1", typeof(ScReputationV1));
            constants.Add("SCO_DATACORE_VERSION_1_0", Abi.DataCoreVersion1_0);
            constants.Add("SCO_DATACORE_VERSION_1_1", Abi.DataCoreVersion1_1);
            constants.Add("SCO_DC_OPEN", Abi.DcOpen);
            constants.Add("SCO_DC_LOADED", Abi.DcLoaded);
            constants.Add("SCO_DC_NON_ATOMIC", Abi.DcNonAtomic);
            constants.Add("SCO_DC_QUEUED", Abi.DcQueued);
            constants.Add("SCO_DC_APPLIED", Abi.DcApplied);
            constants.Add("SCO_DC_SKIPPED", Abi.DcSkipped);
            constants.Add("SCO_DC_REFUSED", Abi.DcRefused);
            constants.Add("SCO_DC_OP_PATCH", Abi.DcOpPatch);
            constants.Add("SCO_DC_MAX_OPS", Abi.DcMaxOps);
            constants.Add("SCO_DC_MAX_PATCHES", Abi.DcMaxPatches);
            constants.Add("SCO_DC_BOOL", (long)ScoDcType.Bool);
            constants.Add("SCO_DC_INT", (long)ScoDcType.Int);
            constants.Add("SCO_DC_UINT", (long)ScoDcType.UInt);
            constants.Add("SCO_DC_FLOAT", (long)ScoDcType.Float);
            constants.Add("SCO_DC_STRING", (long)ScoDcType.String);
            constants.Add("SCO_DC_GUID", (long)ScoDcType.Guid);
            constants.Add("SCO_DC_ENUM", (long)ScoDcType.Enum);
            constants.Add("SCO_DC_NULL", (long)ScoDcType.Null);
            constants.Add("SCO_DC_INSTANCE", (long)ScoDcType.Instance);
            constants.Add("SCO_DC_REF", (long)ScoDcType.Ref);
            constants.Add("SCO_DC_TYPE_FORCE32", (long)ScoDcType.Force32);
            constants.Add("SC_ACTORS_VERSION_1_0", GameAbi.ActorsVersion1_0);
            constants.Add("SC_ACTORS_VERSION_1_1", GameAbi.ActorsVersion1_1);
            constants.Add("SC_ACTOR_ALIVE", (long)ScActorState.Alive);
            constants.Add("SC_ACTOR_INCAPACITATED", (long)ScActorState.Incapacitated);
            constants.Add("SC_ACTOR_DEAD", (long)ScActorState.Dead);
            constants.Add("SC_ACTOR_STATE_FORCE32", (long)ScActorState.Force32);
            constants.Add("SC_WORLD_VERSION_1_0", GameAbi.WorldVersion1_0);
            constants.Add("SC_WORLD_VERSION_1_1", GameAbi.WorldVersion1_1);
            constants.Add("SC_WORLD_RAY_MAX_DISTANCE", GameAbi.WorldRayMaxDistance);
            constants.Add("SC_WORLD_HIT_ENTITY", (long)ScWorldHitFlag.Entity);
            constants.Add("SC_WORLD_HIT_NORMAL", (long)ScWorldHitFlag.Normal);
            constants.Add("SC_WORLD_HIT_FLAG_FORCE32", (long)ScWorldHitFlag.Force32);
            constants.Add("SC_ACTORS_MAX_NPCS", GameAbi.ActorsMaxNpcs);
            constants.Add("SC_ENTITIES_VERSION_1_0", GameAbi.EntitiesVersion1_0);
            constants.Add("SC_ENTITIES_VERSION_1_1", GameAbi.EntitiesVersion1_1);
            constants.Add("SC_ENTITIES_MAX_OWNED", GameAbi.EntitiesMaxOwned);
            constants.Add("SC_ENTITIES_MAX_WATCHES", GameAbi.EntitiesMaxWatches);
            constants.Add("SC_ENTITIES_MAX_TYPE_LEN", GameAbi.EntitiesMaxTypeLen);
            constants.Add("SC_ENTITY_STREAMED_IN", GameAbi.EntityStreamedIn);
            constants.Add("SC_ENTITY_STREAMED_OUT", GameAbi.EntityStreamedOut);
            constants.Add("SC_VEHICLES_SERVICE_VERSION", GameAbi.VehiclesVersion1_0);
            constants.Add("SC_VEHICLE_SEAT_NAME_MAX", GameAbi.VehicleSeatNameMax);
            constants.Add("SC_SEAT_USABLE", (long)ScVehicleSeatFlag.Usable);
            constants.Add("SC_SEAT_USABLE_KNOWN", (long)ScVehicleSeatFlag.UsableKnown);
            constants.Add("SC_SEAT_OCCUPIED", (long)ScVehicleSeatFlag.Occupied);
            constants.Add("SC_SEAT_PILOT", (long)ScVehicleSeatFlag.Pilot);
            constants.Add("SC_SEAT_FLAG_FORCE32", (long)ScVehicleSeatFlag.Force32);
            constants.Add("SC_CREATIVE_SERVICE_VERSION", GameAbi.CreativeVersion1_0);
            constants.Add("SC_CREATIVE_FLY_SPEED_MAX", GameAbi.CreativeFlySpeedMax);
            constants.Add("SC_CREATIVE_GOD_MODE", (long)ScCreativeFlag.GodMode);
            constants.Add("SC_CREATIVE_FLY", (long)ScCreativeFlag.Fly);
            constants.Add("SC_CREATIVE_INFINITE_AMMO", (long)ScCreativeFlag.InfiniteAmmo);
            constants.Add("SC_CREATIVE_INFINITE_SHIP_AMMO", (long)ScCreativeFlag.InfiniteShipAmmo);
            constants.Add("SC_CREATIVE_FLAG_FORCE32", (long)ScCreativeFlag.Force32);
            constants.Add("SC_HANGAR_SERVICE_VERSION", GameAbi.HangarVersion1_0);
            constants.Add("SC_HANGAR_LIFT_OPEN", (long)ScHangarLiftOp.Open);
            constants.Add("SC_HANGAR_LIFT_CLOSE", (long)ScHangarLiftOp.Close);
            constants.Add("SC_HANGAR_LIFT_OP_FORCE32", (long)ScHangarLiftOp.Force32);
            constants.Add("SC_REPUTATION_SERVICE_VERSION", GameAbi.ReputationVersion1_0);
            strings.Add("SCO_DATACORE_NAME", Abi.DataCoreName);
            strings.Add("SCO_DC_APPLIED_EVENT", Abi.DataCoreAppliedEvent);
            strings.Add("SC_ACTORS_NAME", GameAbi.ActorsName);
            strings.Add("SC_WORLD_NAME", GameAbi.WorldName);
            strings.Add("SC_GAME_EVENT_PLAYER_SPAWNED", GameAbi.EventPlayerSpawned);
            strings.Add("SC_GAME_EVENT_PLAYER_DIED", GameAbi.EventPlayerDied);
            strings.Add("SC_GAME_EVENT_ACTOR_DIED", GameAbi.EventActorDied);
            strings.Add("SC_GAME_EVENT_ZONE_CHANGED", GameAbi.EventZoneChanged);
            strings.Add("SC_GAME_EVENT_VEHICLE_BOARDED", GameAbi.EventVehicleBoarded);
            strings.Add("SC_GAME_EVENT_VEHICLE_EXITED", GameAbi.EventVehicleExited);
            strings.Add("SC_ENTITIES_NAME", GameAbi.EntitiesName);
            strings.Add("SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED", GameAbi.EventLoadoutRespawnCompleted);
            strings.Add("SC_VEHICLES_SERVICE_NAME", GameAbi.VehiclesName);
            strings.Add("SC_CREATIVE_SERVICE_NAME", GameAbi.CreativeName);
            strings.Add("SC_HANGAR_SERVICE_NAME", GameAbi.HangarName);
            strings.Add("SC_REPUTATION_SERVICE_NAME", GameAbi.ReputationName);
        }

        static partial void GameSizeOf(Type t, ref int size)
        {
            if (t == typeof(ScoDcValue)) size = Unsafe.SizeOf<ScoDcValue>();
            if (t == typeof(ScoDcReport)) size = Unsafe.SizeOf<ScoDcReport>();
            if (t == typeof(ScoDcApplied)) size = Unsafe.SizeOf<ScoDcApplied>();
            if (t == typeof(ScoDatacoreV1)) size = Unsafe.SizeOf<ScoDatacoreV1>();
            if (t == typeof(ScActorsV1)) size = Unsafe.SizeOf<ScActorsV1>();
            if (t == typeof(ScWorldHit)) size = Unsafe.SizeOf<ScWorldHit>();
            if (t == typeof(ScWorldV1)) size = Unsafe.SizeOf<ScWorldV1>();
            if (t == typeof(ScGamePlayerSpawned)) size = Unsafe.SizeOf<ScGamePlayerSpawned>();
            if (t == typeof(ScGamePlayerDied)) size = Unsafe.SizeOf<ScGamePlayerDied>();
            if (t == typeof(ScGameActorDied)) size = Unsafe.SizeOf<ScGameActorDied>();
            if (t == typeof(ScGameZoneChanged)) size = Unsafe.SizeOf<ScGameZoneChanged>();
            if (t == typeof(ScGameVehicleSeat)) size = Unsafe.SizeOf<ScGameVehicleSeat>();
            if (t == typeof(ScGameMissionChanged)) size = Unsafe.SizeOf<ScGameMissionChanged>();
            if (t == typeof(ScGameLoadoutRespawnCompleted)) size = Unsafe.SizeOf<ScGameLoadoutRespawnCompleted>();
            if (t == typeof(ScEntitiesV1)) size = Unsafe.SizeOf<ScEntitiesV1>();
            if (t == typeof(ScVehicleSeat)) size = Unsafe.SizeOf<ScVehicleSeat>();
            if (t == typeof(ScVehiclesV1)) size = Unsafe.SizeOf<ScVehiclesV1>();
            if (t == typeof(ScCreativeV1)) size = Unsafe.SizeOf<ScCreativeV1>();
            if (t == typeof(ScHangarV1)) size = Unsafe.SizeOf<ScHangarV1>();
            if (t == typeof(ScReputationChecks)) size = Unsafe.SizeOf<ScReputationChecks>();
            if (t == typeof(ScReputationV1)) size = Unsafe.SizeOf<ScReputationV1>();
        }
    }
}
