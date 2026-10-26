// SPDX-License-Identifier: GPL-3.0-only
// Inventory vocabulary copied from public sc_inventory.h for build 4.10.197.2878.
using System;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    public static partial class GameAbi
    {
        public const string InventoryName = "game.inventory";
        public const uint InventoryVersion1_0 = 0x00010000u;
        public const uint InventoryVersion1_1 = 0x00010001u;
        public const uint InventoryMaxPorts = 128;
        public const uint InventorySnapshotSize1_0 = 3088;
        public const uint InventoryFlagTruncated = 1;
        public const uint InventoryFlagLimited = 2;
    }

    public enum ScItemType : uint
    {
        Unknown = 0xFFFFFFFFu,
        AIModule = 0,
        AirTrafficController = 1,
        AmmoBox = 2,
        AmmoCrate = 3,
        Armor = 4,
        Audio = 5,
        Avionics = 6,
        AttachedPart = 7,
        Battery = 8,
        BoatController = 9,
        BodyArmor = 10,
        Bomb = 11,
        BombLauncher = 12,
        Bottle = 13,
        Button = 14,
        CapacitorAssignmentController = 15,
        Cargo = 16,
        CargoGrid = 17,
        Char_Accessory_Eyes = 18,
        Char_Accessory_Head = 19,
        Char_Armor_Arms = 20,
        Char_Armor_Feet = 21,
        Char_Armor_Helmet = 22,
        Char_Armor_Legs = 23,
        Char_Armor_Torso = 24,
        Char_Armor_Undersuit = 25,
        Char_Armor_Backpack = 26,
        Char_Body = 27,
        Char_Clothing_Feet = 28,
        Char_Clothing_Hands = 29,
        Char_Clothing_Hat = 30,
        Char_Clothing_Mask = 31,
        Char_Clothing_Jumpsuit = 32,
        Char_Clothing_Legs = 33,
        Char_Clothing_Torso_0 = 34,
        Char_Clothing_Torso_1 = 35,
        Char_Clothing_Torso_2 = 36,
        Char_Clothing_Undershirt = 37,
        Char_Clothing_Backpack = 38,
        Char_Flair = 39,
        Char_Hair_Color = 40,
        Char_Hair_Prop = 41,
        Char_Head = 42,
        Char_Head_Beard = 43,
        Char_Head_Eyebrow = 44,
        Char_Head_Eyelash = 45,
        Char_Head_Eyes = 46,
        Char_Head_Hair = 47,
        Char_Head_Stubble = 48,
        Char_Head_Piercings = 49,
        Char_Lens = 50,
        Char_Skin_Color = 51,
        Cloth = 52,
        CommsController = 53,
        Container = 54,
        ControlPanel = 55,
        Cooler = 56,
        CoolerController = 57,
        Crafter = 58,
        Creature_Body = 59,
        Creature_Hair = 60,
        Creature_Harvest = 61,
        Currency = 62,
        Debris = 63,
        Decal = 64,
        Display = 65,
        DockingAnimator = 66,
        DockingCollar = 67,
        DockingController = 68,
        Door = 69,
        DoorController = 70,
        Drink = 71,
        Elevator = 72,
        EMP = 73,
        EnergyController = 74,
        ExternalFuelTank = 75,
        Flair_Cockpit = 76,
        Flair_Floor = 77,
        Flair_Surface = 78,
        Flair_Wall = 79,
        FlightController = 80,
        Food = 81,
        FPS_AttachmentBarrel = 82,
        FPS_AttachmentBottom = 83,
        FPS_AttachmentOptics = 84,
        FPS_Consumable = 85,
        FPS_Cooler = 86,
        FPS_Deployable = 87,
        FPS_Magazine = 88,
        FPS_PowerGen = 89,
        FPS_Radar = 90,
        FPS_Scanner = 91,
        FPS_Throwable = 92,
        FPS_WeaponMelee = 93,
        FPS_WeaponShouldered = 94,
        FPS_WeaponSidearm = 95,
        FPS_WeaponStocked = 96,
        FPS_WeaponUtility = 97,
        FuelController = 98,
        FuelIntake = 99,
        FuelNozzle = 100,
        FuelTank = 101,
        Gadget = 102,
        GravityGenerator = 103,
        Grenade = 104,
        GroundVehicleMissileLauncher = 105,
        Hangar = 106,
        HangarExpansion = 107,
        HangarStock = 108,
        Interior = 109,
        InventoryContainer = 110,
        JumpDrive = 111,
        LandingSystem = 112,
        LandingGear = 113,
        LifeSupportGenerator = 114,
        LifeSupportTank = 115,
        LifeSupportVent = 116,
        Light = 117,
        LightController = 118,
        Lightgroup = 119,
        LiveSupport = 120,
        MaelstromPart = 121,
        Magazine = 122,
        MainEngine = 123,
        MainThruster = 124,
        ManneuverThruster = 125,
        MiningController = 126,
        MiningModifier = 127,
        Misc = 128,
        Missile = 129,
        MissileController = 130,
        MissileLauncher = 131,
        MobiGlas = 132,
        Module = 133,
        MultiLight = 134,
        NOITEM_Player = 135,
        NOITEM_Vehicle = 136,
        Paints = 137,
        PersistentHab = 138,
        PersonalInnerThought = 139,
        Ping = 140,
        Player = 141,
        PowerPlant = 142,
        QuantumDrive = 143,
        QuantumInterdictionGenerator = 144,
        QuantumFuelTank = 145,
        Radar = 146,
        Relay = 147,
        RemoteConnection = 148,
        RemovableChip = 149,
        RemovableBlade = 150,
        Room = 151,
        SalvageController = 152,
        SalvageFieldEmitter = 153,
        SalvageFieldSupporter = 154,
        SalvageFillerStation = 155,
        SalvageHead = 156,
        SalvageInternalStorage = 157,
        SalvageModifier = 158,
        Scanner = 159,
        Seat = 160,
        SeatAccess = 161,
        SeatDashboard = 162,
        SelfDestruct = 163,
        Sensor = 164,
        Shield = 165,
        ShieldController = 166,
        Ship = 167,
        ShopDisplay = 168,
        SpaceMine = 169,
        SpaceMineLauncher = 170,
        StatusScreen = 171,
        Suit = 172,
        TargetSelector = 173,
        Thumbnail = 174,
        ToolArm = 175,
        TowingBeam = 176,
        TractorBeam = 177,
        Transponder = 178,
        Turret = 179,
        TurretBase = 180,
        Usable = 181,
        UtilityTurret = 182,
        Visor = 183,
        WeaponAttachment = 184,
        WeaponController = 185,
        WeaponDefensive = 186,
        WeaponGun = 187,
        WeaponMining = 188,
        WeaponPersonal = 189,
        WeaponRegenPool = 190,
        WheeledController = 191,
    }

    /// <summary>Build-scoped categories, not per-class item names. No service lookup or game read.</summary>
    public static class InventoryItemTypes
    {
        public const uint Count = 192;
        public const string VerifiedBuild = "4.10.197.2878";
        public static ScItemType FromRaw(ulong raw, bool valid = true) =>
            valid && raw < Count ? (ScItemType)raw : ScItemType.Unknown;
        public static string Name(ulong raw) => raw < Count ? ((ScItemType)raw).ToString() : "UNDEFINED";
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ScInventoryPort
    {
        public uint size, valid;
        public ulong item_class_id, entity_id;
        public readonly ScItemType ItemType => InventoryItemTypes.FromRaw(item_class_id, valid != 0);
        public readonly string ItemTypeName => InventoryItemTypes.Name((uint)ItemType);
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScInventorySnapshot
    {
        public uint size, count;
        public ulong generation;
        public fixed byte ports[128 * 24];
        public uint flags, total_seen;
        public readonly bool HasDetails => size >= 3096;
        public ScInventoryPort Port(int index)
        {
            if (index < 0 || (uint)index >= count || index >= 128) throw new ArgumentOutOfRangeException(nameof(index));
            fixed (byte* p = ports) return ((ScInventoryPort*)p)[index];
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScInventoryV1
    {
        public uint size, _pad;
        public delegate* unmanaged[Cdecl]<void*, ScInventorySnapshot*, ScoResult> snapshot;
    }

    /// <summary>Read-only caller-owned snapshots. Presence and category names do not imply
    /// game.inventory.read is enabled. Game thread only; not run in game.</summary>
    public sealed unsafe class Inventory
    {
        private Plugin? _plugin;
        private ScInventoryV1* _table;
        private uint _snapshotSize = 3096;
        public bool IsOpen => _table != null;

        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _plugin = null;
            _table = null;
            _snapshotSize = 3096;
            var api = plugin.Api;
            if (api == null || plugin.Self == 0 || api->size < ApiOffset.query_service + (uint)sizeof(nint) || api->query_service == null)
                return ScoResult.Unavailable;
            ScoResult r = plugin.Query(GameAbi.InventoryName, GameAbi.InventoryVersion1_1, out ServiceRef<ScInventoryV1> service);
            if (r == ScoResult.NotFound || r == ScoResult.Unavailable)
            {
                r = plugin.Query(GameAbi.InventoryName, GameAbi.InventoryVersion1_0, out service);
                _snapshotSize = GameAbi.InventorySnapshotSize1_0;
            }
            if (r != ScoResult.Ok) return r;
            if (!service.Covers(8) || service.Table->snapshot == null) return ScoResult.Unavailable;
            _plugin = plugin;
            _table = service.Table;
            return ScoResult.Ok;
        }

        public ScoResult Snapshot(out ScInventorySnapshot snapshot)
        {
            snapshot = default;
            snapshot.size = _snapshotSize;
            var api = _plugin == null ? null : _plugin.Api;
            if (api == null || _plugin!.Self == 0 || api->size < 16 + (uint)sizeof(nint) || api->has == null ||
                !_plugin.Has("game.inventory.read") || !new ServiceRef<ScInventoryV1>(_table).Covers(8) || _table->snapshot == null)
                return ScoResult.Unavailable;
            ScInventorySnapshot value = default;
            value.size = _snapshotSize;
            value.count = GameAbi.InventoryMaxPorts;
            ScoResult r = _table->snapshot(_plugin.SelfPtr, &value);
            if (r != ScoResult.Ok) return r;
            if (value.size < _snapshotSize || value.count > GameAbi.InventoryMaxPorts) return ScoResult.Failed;
            value.size = _snapshotSize;
            if (_snapshotSize == GameAbi.InventorySnapshotSize1_0) { value.flags = 0; value.total_seen = 0; }
            snapshot = value;
            return ScoResult.Ok;
        }
    }
}
