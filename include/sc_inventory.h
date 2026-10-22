/* Read-only view over the local player's item ports: what is attached where. No writes,
 * no pointers, no actions cross this ABI. See docs/game/inventory.md. */
#ifndef SC_INVENTORY_H
#define SC_INVENTORY_H
#include "sco_api.h"
#include <stddef.h>
#include <stdint.h>
#define SC_INVENTORY_SERVICE_NAME "game.inventory"
#define SC_INVENTORY_SERVICE_VERSION_1_0 0x00010000u
#define SC_INVENTORY_SERVICE_VERSION_1_1 0x00010001u /* + flags and total_seen after ports[] */
#define SC_INVENTORY_SERVICE_VERSION SC_INVENTORY_SERVICE_VERSION_1_1
#define SC_INVENTORY_MAX_PORTS 128u
/* snapshot.flags bits (1.1; see sc_inventory_snapshot). */
#define SC_INVENTORY_FLAG_TRUNCATED 0x1u /* ports exist that were not written: total_seen > count */
#define SC_INVENTORY_FLAG_LIMITED   0x2u /* the walk's own limits (depth, container budget, a cycle or an
                                            unreadable nested container) cut the tree: total_seen is a lower bound */
/* One item attached to one port of the queried entity, as of the snapshot. item_class_id holds
 * the item type id, an ordinal of the game's item-type enumeration (a coarse category,
 * build-scoped): sc_item_type and sc_inventory_item_type_name below name it. Per-class item
 * names are not yet established (docs/game/inventory.md). */
typedef struct sc_inventory_port {
    uint32_t size;
    uint32_t valid;            /* 1 = port exists and holds an item; else fields below are 0 */
    uint64_t item_class_id;    /* item type id: ordinal of the game's item-type enumeration */
    uint64_t entity_id;        /* the item's entity id, session-scoped (game.entities keys) */
} sc_inventory_port;

/* 192-member item category enumeration (EItemType) verified on Star Citizen 4.10.197.2878 (F-4.10.197.2878-014). Ordinals are build-scoped. */
typedef enum sc_item_type {
    SC_ITEM_TYPE_UNKNOWN = 0xFFFFFFFFu,
    SC_ITEM_TYPE_AI_MODULE = 0,
    SC_ITEM_TYPE_AIR_TRAFFIC_CONTROLLER = 1,
    SC_ITEM_TYPE_AMMO_BOX = 2,
    SC_ITEM_TYPE_AMMO_CRATE = 3,
    SC_ITEM_TYPE_ARMOR = 4,
    SC_ITEM_TYPE_AUDIO = 5,
    SC_ITEM_TYPE_AVIONICS = 6,
    SC_ITEM_TYPE_ATTACHED_PART = 7,
    SC_ITEM_TYPE_BATTERY = 8,
    SC_ITEM_TYPE_BOAT_CONTROLLER = 9,
    SC_ITEM_TYPE_BODY_ARMOR = 10,
    SC_ITEM_TYPE_BOMB = 11,
    SC_ITEM_TYPE_BOMB_LAUNCHER = 12,
    SC_ITEM_TYPE_BOTTLE = 13,
    SC_ITEM_TYPE_BUTTON = 14,
    SC_ITEM_TYPE_CAPACITOR_ASSIGNMENT_CONTROLLER = 15,
    SC_ITEM_TYPE_CARGO = 16,
    SC_ITEM_TYPE_CARGO_GRID = 17,
    SC_ITEM_TYPE_CHAR_ACCESSORY_EYES = 18,
    SC_ITEM_TYPE_CHAR_ACCESSORY_HEAD = 19,
    SC_ITEM_TYPE_CHAR_ARMOR_ARMS = 20,
    SC_ITEM_TYPE_CHAR_ARMOR_FEET = 21,
    SC_ITEM_TYPE_CHAR_ARMOR_HELMET = 22,
    SC_ITEM_TYPE_CHAR_ARMOR_LEGS = 23,
    SC_ITEM_TYPE_CHAR_ARMOR_TORSO = 24,
    SC_ITEM_TYPE_CHAR_ARMOR_UNDERSUIT = 25,
    SC_ITEM_TYPE_CHAR_ARMOR_BACKPACK = 26,
    SC_ITEM_TYPE_CHAR_BODY = 27,
    SC_ITEM_TYPE_CHAR_CLOTHING_FEET = 28,
    SC_ITEM_TYPE_CHAR_CLOTHING_HANDS = 29,
    SC_ITEM_TYPE_CHAR_CLOTHING_HAT = 30,
    SC_ITEM_TYPE_CHAR_CLOTHING_MASK = 31,
    SC_ITEM_TYPE_CHAR_CLOTHING_JUMPSUIT = 32,
    SC_ITEM_TYPE_CHAR_CLOTHING_LEGS = 33,
    SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_0 = 34,
    SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_1 = 35,
    SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_2 = 36,
    SC_ITEM_TYPE_CHAR_CLOTHING_UNDERSHIRT = 37,
    SC_ITEM_TYPE_CHAR_CLOTHING_BACKPACK = 38,
    SC_ITEM_TYPE_CHAR_FLAIR = 39,
    SC_ITEM_TYPE_CHAR_HAIR_COLOR = 40,
    SC_ITEM_TYPE_CHAR_HAIR_PROP = 41,
    SC_ITEM_TYPE_CHAR_HEAD = 42,
    SC_ITEM_TYPE_CHAR_HEAD_BEARD = 43,
    SC_ITEM_TYPE_CHAR_HEAD_EYEBROW = 44,
    SC_ITEM_TYPE_CHAR_HEAD_EYELASH = 45,
    SC_ITEM_TYPE_CHAR_HEAD_EYES = 46,
    SC_ITEM_TYPE_CHAR_HEAD_HAIR = 47,
    SC_ITEM_TYPE_CHAR_HEAD_STUBBLE = 48,
    SC_ITEM_TYPE_CHAR_HEAD_PIERCINGS = 49,
    SC_ITEM_TYPE_CHAR_LENS = 50,
    SC_ITEM_TYPE_CHAR_SKIN_COLOR = 51,
    SC_ITEM_TYPE_CLOTH = 52,
    SC_ITEM_TYPE_COMMS_CONTROLLER = 53,
    SC_ITEM_TYPE_CONTAINER = 54,
    SC_ITEM_TYPE_CONTROL_PANEL = 55,
    SC_ITEM_TYPE_COOLER = 56,
    SC_ITEM_TYPE_COOLER_CONTROLLER = 57,
    SC_ITEM_TYPE_CRAFTER = 58,
    SC_ITEM_TYPE_CREATURE_BODY = 59,
    SC_ITEM_TYPE_CREATURE_HAIR = 60,
    SC_ITEM_TYPE_CREATURE_HARVEST = 61,
    SC_ITEM_TYPE_CURRENCY = 62,
    SC_ITEM_TYPE_DEBRIS = 63,
    SC_ITEM_TYPE_DECAL = 64,
    SC_ITEM_TYPE_DISPLAY = 65,
    SC_ITEM_TYPE_DOCKING_ANIMATOR = 66,
    SC_ITEM_TYPE_DOCKING_COLLAR = 67,
    SC_ITEM_TYPE_DOCKING_CONTROLLER = 68,
    SC_ITEM_TYPE_DOOR = 69,
    SC_ITEM_TYPE_DOOR_CONTROLLER = 70,
    SC_ITEM_TYPE_DRINK = 71,
    SC_ITEM_TYPE_ELEVATOR = 72,
    SC_ITEM_TYPE_EMP = 73,
    SC_ITEM_TYPE_ENERGY_CONTROLLER = 74,
    SC_ITEM_TYPE_EXTERNAL_FUEL_TANK = 75,
    SC_ITEM_TYPE_FLAIR_COCKPIT = 76,
    SC_ITEM_TYPE_FLAIR_FLOOR = 77,
    SC_ITEM_TYPE_FLAIR_SURFACE = 78,
    SC_ITEM_TYPE_FLAIR_WALL = 79,
    SC_ITEM_TYPE_FLIGHT_CONTROLLER = 80,
    SC_ITEM_TYPE_FOOD = 81,
    SC_ITEM_TYPE_FPS_ATTACHMENT_BARREL = 82,
    SC_ITEM_TYPE_FPS_ATTACHMENT_BOTTOM = 83,
    SC_ITEM_TYPE_FPS_ATTACHMENT_OPTICS = 84,
    SC_ITEM_TYPE_FPS_CONSUMABLE = 85,
    SC_ITEM_TYPE_FPS_COOLER = 86,
    SC_ITEM_TYPE_FPS_DEPLOYABLE = 87,
    SC_ITEM_TYPE_FPS_MAGAZINE = 88,
    SC_ITEM_TYPE_FPS_POWER_GEN = 89,
    SC_ITEM_TYPE_FPS_RADAR = 90,
    SC_ITEM_TYPE_FPS_SCANNER = 91,
    SC_ITEM_TYPE_FPS_THROWABLE = 92,
    SC_ITEM_TYPE_FPS_WEAPON_MELEE = 93,
    SC_ITEM_TYPE_FPS_WEAPON_SHOULDERED = 94,
    SC_ITEM_TYPE_FPS_WEAPON_SIDEARM = 95,
    SC_ITEM_TYPE_FPS_WEAPON_STOCKED = 96,
    SC_ITEM_TYPE_FPS_WEAPON_UTILITY = 97,
    SC_ITEM_TYPE_FUEL_CONTROLLER = 98,
    SC_ITEM_TYPE_FUEL_INTAKE = 99,
    SC_ITEM_TYPE_FUEL_NOZZLE = 100,
    SC_ITEM_TYPE_FUEL_TANK = 101,
    SC_ITEM_TYPE_GADGET = 102,
    SC_ITEM_TYPE_GRAVITY_GENERATOR = 103,
    SC_ITEM_TYPE_GRENADE = 104,
    SC_ITEM_TYPE_GROUND_VEHICLE_MISSILE_LAUNCHER = 105,
    SC_ITEM_TYPE_HANGAR = 106,
    SC_ITEM_TYPE_HANGAR_EXPANSION = 107,
    SC_ITEM_TYPE_HANGAR_STOCK = 108,
    SC_ITEM_TYPE_INTERIOR = 109,
    SC_ITEM_TYPE_INVENTORY_CONTAINER = 110,
    SC_ITEM_TYPE_JUMP_DRIVE = 111,
    SC_ITEM_TYPE_LANDING_SYSTEM = 112,
    SC_ITEM_TYPE_LANDING_GEAR = 113,
    SC_ITEM_TYPE_LIFE_SUPPORT_GENERATOR = 114,
    SC_ITEM_TYPE_LIFE_SUPPORT_TANK = 115,
    SC_ITEM_TYPE_LIFE_SUPPORT_VENT = 116,
    SC_ITEM_TYPE_LIGHT = 117,
    SC_ITEM_TYPE_LIGHT_CONTROLLER = 118,
    SC_ITEM_TYPE_LIGHTGROUP = 119,
    SC_ITEM_TYPE_LIVE_SUPPORT = 120,
    SC_ITEM_TYPE_MAELSTROM_PART = 121,
    SC_ITEM_TYPE_MAGAZINE = 122,
    SC_ITEM_TYPE_MAIN_ENGINE = 123,
    SC_ITEM_TYPE_MAIN_THRUSTER = 124,
    SC_ITEM_TYPE_MANNEUVER_THRUSTER = 125,
    SC_ITEM_TYPE_MINING_CONTROLLER = 126,
    SC_ITEM_TYPE_MINING_MODIFIER = 127,
    SC_ITEM_TYPE_MISC = 128,
    SC_ITEM_TYPE_MISSILE = 129,
    SC_ITEM_TYPE_MISSILE_CONTROLLER = 130,
    SC_ITEM_TYPE_MISSILE_LAUNCHER = 131,
    SC_ITEM_TYPE_MOBI_GLAS = 132,
    SC_ITEM_TYPE_MODULE = 133,
    SC_ITEM_TYPE_MULTI_LIGHT = 134,
    SC_ITEM_TYPE_NOITEM_PLAYER = 135,
    SC_ITEM_TYPE_NOITEM_VEHICLE = 136,
    SC_ITEM_TYPE_PAINTS = 137,
    SC_ITEM_TYPE_PERSISTENT_HAB = 138,
    SC_ITEM_TYPE_PERSONAL_INNER_THOUGHT = 139,
    SC_ITEM_TYPE_PING = 140,
    SC_ITEM_TYPE_PLAYER = 141,
    SC_ITEM_TYPE_POWER_PLANT = 142,
    SC_ITEM_TYPE_QUANTUM_DRIVE = 143,
    SC_ITEM_TYPE_QUANTUM_INTERDICTION_GENERATOR = 144,
    SC_ITEM_TYPE_QUANTUM_FUEL_TANK = 145,
    SC_ITEM_TYPE_RADAR = 146,
    SC_ITEM_TYPE_RELAY = 147,
    SC_ITEM_TYPE_REMOTE_CONNECTION = 148,
    SC_ITEM_TYPE_REMOVABLE_CHIP = 149,
    SC_ITEM_TYPE_REMOVABLE_BLADE = 150,
    SC_ITEM_TYPE_ROOM = 151,
    SC_ITEM_TYPE_SALVAGE_CONTROLLER = 152,
    SC_ITEM_TYPE_SALVAGE_FIELD_EMITTER = 153,
    SC_ITEM_TYPE_SALVAGE_FIELD_SUPPORTER = 154,
    SC_ITEM_TYPE_SALVAGE_FILLER_STATION = 155,
    SC_ITEM_TYPE_SALVAGE_HEAD = 156,
    SC_ITEM_TYPE_SALVAGE_INTERNAL_STORAGE = 157,
    SC_ITEM_TYPE_SALVAGE_MODIFIER = 158,
    SC_ITEM_TYPE_SCANNER = 159,
    SC_ITEM_TYPE_SEAT = 160,
    SC_ITEM_TYPE_SEAT_ACCESS = 161,
    SC_ITEM_TYPE_SEAT_DASHBOARD = 162,
    SC_ITEM_TYPE_SELF_DESTRUCT = 163,
    SC_ITEM_TYPE_SENSOR = 164,
    SC_ITEM_TYPE_SHIELD = 165,
    SC_ITEM_TYPE_SHIELD_CONTROLLER = 166,
    SC_ITEM_TYPE_SHIP = 167,
    SC_ITEM_TYPE_SHOP_DISPLAY = 168,
    SC_ITEM_TYPE_SPACE_MINE = 169,
    SC_ITEM_TYPE_SPACE_MINE_LAUNCHER = 170,
    SC_ITEM_TYPE_STATUS_SCREEN = 171,
    SC_ITEM_TYPE_SUIT = 172,
    SC_ITEM_TYPE_TARGET_SELECTOR = 173,
    SC_ITEM_TYPE_THUMBNAIL = 174,
    SC_ITEM_TYPE_TOOL_ARM = 175,
    SC_ITEM_TYPE_TOWING_BEAM = 176,
    SC_ITEM_TYPE_TRACTOR_BEAM = 177,
    SC_ITEM_TYPE_TRANSPONDER = 178,
    SC_ITEM_TYPE_TURRET = 179,
    SC_ITEM_TYPE_TURRET_BASE = 180,
    SC_ITEM_TYPE_USABLE = 181,
    SC_ITEM_TYPE_UTILITY_TURRET = 182,
    SC_ITEM_TYPE_VISOR = 183,
    SC_ITEM_TYPE_WEAPON_ATTACHMENT = 184,
    SC_ITEM_TYPE_WEAPON_CONTROLLER = 185,
    SC_ITEM_TYPE_WEAPON_DEFENSIVE = 186,
    SC_ITEM_TYPE_WEAPON_GUN = 187,
    SC_ITEM_TYPE_WEAPON_MINING = 188,
    SC_ITEM_TYPE_WEAPON_PERSONAL = 189,
    SC_ITEM_TYPE_WEAPON_REGEN_POOL = 190,
    SC_ITEM_TYPE_WHEELED_CONTROLLER = 191,
    SC_ITEM_TYPE_FORCE32 = 0x7fffffff
} sc_item_type;

#define SC_ITEM_TYPE_COUNT_197 192u

static inline const char* sc_inventory_item_type_name(uint32_t type) {
    switch (type) {
        case SC_ITEM_TYPE_AI_MODULE: return "AIModule";
        case SC_ITEM_TYPE_AIR_TRAFFIC_CONTROLLER: return "AirTrafficController";
        case SC_ITEM_TYPE_AMMO_BOX: return "AmmoBox";
        case SC_ITEM_TYPE_AMMO_CRATE: return "AmmoCrate";
        case SC_ITEM_TYPE_ARMOR: return "Armor";
        case SC_ITEM_TYPE_AUDIO: return "Audio";
        case SC_ITEM_TYPE_AVIONICS: return "Avionics";
        case SC_ITEM_TYPE_ATTACHED_PART: return "AttachedPart";
        case SC_ITEM_TYPE_BATTERY: return "Battery";
        case SC_ITEM_TYPE_BOAT_CONTROLLER: return "BoatController";
        case SC_ITEM_TYPE_BODY_ARMOR: return "BodyArmor";
        case SC_ITEM_TYPE_BOMB: return "Bomb";
        case SC_ITEM_TYPE_BOMB_LAUNCHER: return "BombLauncher";
        case SC_ITEM_TYPE_BOTTLE: return "Bottle";
        case SC_ITEM_TYPE_BUTTON: return "Button";
        case SC_ITEM_TYPE_CAPACITOR_ASSIGNMENT_CONTROLLER: return "CapacitorAssignmentController";
        case SC_ITEM_TYPE_CARGO: return "Cargo";
        case SC_ITEM_TYPE_CARGO_GRID: return "CargoGrid";
        case SC_ITEM_TYPE_CHAR_ACCESSORY_EYES: return "Char_Accessory_Eyes";
        case SC_ITEM_TYPE_CHAR_ACCESSORY_HEAD: return "Char_Accessory_Head";
        case SC_ITEM_TYPE_CHAR_ARMOR_ARMS: return "Char_Armor_Arms";
        case SC_ITEM_TYPE_CHAR_ARMOR_FEET: return "Char_Armor_Feet";
        case SC_ITEM_TYPE_CHAR_ARMOR_HELMET: return "Char_Armor_Helmet";
        case SC_ITEM_TYPE_CHAR_ARMOR_LEGS: return "Char_Armor_Legs";
        case SC_ITEM_TYPE_CHAR_ARMOR_TORSO: return "Char_Armor_Torso";
        case SC_ITEM_TYPE_CHAR_ARMOR_UNDERSUIT: return "Char_Armor_Undersuit";
        case SC_ITEM_TYPE_CHAR_ARMOR_BACKPACK: return "Char_Armor_Backpack";
        case SC_ITEM_TYPE_CHAR_BODY: return "Char_Body";
        case SC_ITEM_TYPE_CHAR_CLOTHING_FEET: return "Char_Clothing_Feet";
        case SC_ITEM_TYPE_CHAR_CLOTHING_HANDS: return "Char_Clothing_Hands";
        case SC_ITEM_TYPE_CHAR_CLOTHING_HAT: return "Char_Clothing_Hat";
        case SC_ITEM_TYPE_CHAR_CLOTHING_MASK: return "Char_Clothing_Mask";
        case SC_ITEM_TYPE_CHAR_CLOTHING_JUMPSUIT: return "Char_Clothing_Jumpsuit";
        case SC_ITEM_TYPE_CHAR_CLOTHING_LEGS: return "Char_Clothing_Legs";
        case SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_0: return "Char_Clothing_Torso_0";
        case SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_1: return "Char_Clothing_Torso_1";
        case SC_ITEM_TYPE_CHAR_CLOTHING_TORSO_2: return "Char_Clothing_Torso_2";
        case SC_ITEM_TYPE_CHAR_CLOTHING_UNDERSHIRT: return "Char_Clothing_Undershirt";
        case SC_ITEM_TYPE_CHAR_CLOTHING_BACKPACK: return "Char_Clothing_Backpack";
        case SC_ITEM_TYPE_CHAR_FLAIR: return "Char_Flair";
        case SC_ITEM_TYPE_CHAR_HAIR_COLOR: return "Char_Hair_Color";
        case SC_ITEM_TYPE_CHAR_HAIR_PROP: return "Char_Hair_Prop";
        case SC_ITEM_TYPE_CHAR_HEAD: return "Char_Head";
        case SC_ITEM_TYPE_CHAR_HEAD_BEARD: return "Char_Head_Beard";
        case SC_ITEM_TYPE_CHAR_HEAD_EYEBROW: return "Char_Head_Eyebrow";
        case SC_ITEM_TYPE_CHAR_HEAD_EYELASH: return "Char_Head_Eyelash";
        case SC_ITEM_TYPE_CHAR_HEAD_EYES: return "Char_Head_Eyes";
        case SC_ITEM_TYPE_CHAR_HEAD_HAIR: return "Char_Head_Hair";
        case SC_ITEM_TYPE_CHAR_HEAD_STUBBLE: return "Char_Head_Stubble";
        case SC_ITEM_TYPE_CHAR_HEAD_PIERCINGS: return "Char_Head_Piercings";
        case SC_ITEM_TYPE_CHAR_LENS: return "Char_Lens";
        case SC_ITEM_TYPE_CHAR_SKIN_COLOR: return "Char_Skin_Color";
        case SC_ITEM_TYPE_CLOTH: return "Cloth";
        case SC_ITEM_TYPE_COMMS_CONTROLLER: return "CommsController";
        case SC_ITEM_TYPE_CONTAINER: return "Container";
        case SC_ITEM_TYPE_CONTROL_PANEL: return "ControlPanel";
        case SC_ITEM_TYPE_COOLER: return "Cooler";
        case SC_ITEM_TYPE_COOLER_CONTROLLER: return "CoolerController";
        case SC_ITEM_TYPE_CRAFTER: return "Crafter";
        case SC_ITEM_TYPE_CREATURE_BODY: return "Creature_Body";
        case SC_ITEM_TYPE_CREATURE_HAIR: return "Creature_Hair";
        case SC_ITEM_TYPE_CREATURE_HARVEST: return "Creature_Harvest";
        case SC_ITEM_TYPE_CURRENCY: return "Currency";
        case SC_ITEM_TYPE_DEBRIS: return "Debris";
        case SC_ITEM_TYPE_DECAL: return "Decal";
        case SC_ITEM_TYPE_DISPLAY: return "Display";
        case SC_ITEM_TYPE_DOCKING_ANIMATOR: return "DockingAnimator";
        case SC_ITEM_TYPE_DOCKING_COLLAR: return "DockingCollar";
        case SC_ITEM_TYPE_DOCKING_CONTROLLER: return "DockingController";
        case SC_ITEM_TYPE_DOOR: return "Door";
        case SC_ITEM_TYPE_DOOR_CONTROLLER: return "DoorController";
        case SC_ITEM_TYPE_DRINK: return "Drink";
        case SC_ITEM_TYPE_ELEVATOR: return "Elevator";
        case SC_ITEM_TYPE_EMP: return "EMP";
        case SC_ITEM_TYPE_ENERGY_CONTROLLER: return "EnergyController";
        case SC_ITEM_TYPE_EXTERNAL_FUEL_TANK: return "ExternalFuelTank";
        case SC_ITEM_TYPE_FLAIR_COCKPIT: return "Flair_Cockpit";
        case SC_ITEM_TYPE_FLAIR_FLOOR: return "Flair_Floor";
        case SC_ITEM_TYPE_FLAIR_SURFACE: return "Flair_Surface";
        case SC_ITEM_TYPE_FLAIR_WALL: return "Flair_Wall";
        case SC_ITEM_TYPE_FLIGHT_CONTROLLER: return "FlightController";
        case SC_ITEM_TYPE_FOOD: return "Food";
        case SC_ITEM_TYPE_FPS_ATTACHMENT_BARREL: return "FPS_AttachmentBarrel";
        case SC_ITEM_TYPE_FPS_ATTACHMENT_BOTTOM: return "FPS_AttachmentBottom";
        case SC_ITEM_TYPE_FPS_ATTACHMENT_OPTICS: return "FPS_AttachmentOptics";
        case SC_ITEM_TYPE_FPS_CONSUMABLE: return "FPS_Consumable";
        case SC_ITEM_TYPE_FPS_COOLER: return "FPS_Cooler";
        case SC_ITEM_TYPE_FPS_DEPLOYABLE: return "FPS_Deployable";
        case SC_ITEM_TYPE_FPS_MAGAZINE: return "FPS_Magazine";
        case SC_ITEM_TYPE_FPS_POWER_GEN: return "FPS_PowerGen";
        case SC_ITEM_TYPE_FPS_RADAR: return "FPS_Radar";
        case SC_ITEM_TYPE_FPS_SCANNER: return "FPS_Scanner";
        case SC_ITEM_TYPE_FPS_THROWABLE: return "FPS_Throwable";
        case SC_ITEM_TYPE_FPS_WEAPON_MELEE: return "FPS_WeaponMelee";
        case SC_ITEM_TYPE_FPS_WEAPON_SHOULDERED: return "FPS_WeaponShouldered";
        case SC_ITEM_TYPE_FPS_WEAPON_SIDEARM: return "FPS_WeaponSidearm";
        case SC_ITEM_TYPE_FPS_WEAPON_STOCKED: return "FPS_WeaponStocked";
        case SC_ITEM_TYPE_FPS_WEAPON_UTILITY: return "FPS_WeaponUtility";
        case SC_ITEM_TYPE_FUEL_CONTROLLER: return "FuelController";
        case SC_ITEM_TYPE_FUEL_INTAKE: return "FuelIntake";
        case SC_ITEM_TYPE_FUEL_NOZZLE: return "FuelNozzle";
        case SC_ITEM_TYPE_FUEL_TANK: return "FuelTank";
        case SC_ITEM_TYPE_GADGET: return "Gadget";
        case SC_ITEM_TYPE_GRAVITY_GENERATOR: return "GravityGenerator";
        case SC_ITEM_TYPE_GRENADE: return "Grenade";
        case SC_ITEM_TYPE_GROUND_VEHICLE_MISSILE_LAUNCHER: return "GroundVehicleMissileLauncher";
        case SC_ITEM_TYPE_HANGAR: return "Hangar";
        case SC_ITEM_TYPE_HANGAR_EXPANSION: return "HangarExpansion";
        case SC_ITEM_TYPE_HANGAR_STOCK: return "HangarStock";
        case SC_ITEM_TYPE_INTERIOR: return "Interior";
        case SC_ITEM_TYPE_INVENTORY_CONTAINER: return "InventoryContainer";
        case SC_ITEM_TYPE_JUMP_DRIVE: return "JumpDrive";
        case SC_ITEM_TYPE_LANDING_SYSTEM: return "LandingSystem";
        case SC_ITEM_TYPE_LANDING_GEAR: return "LandingGear";
        case SC_ITEM_TYPE_LIFE_SUPPORT_GENERATOR: return "LifeSupportGenerator";
        case SC_ITEM_TYPE_LIFE_SUPPORT_TANK: return "LifeSupportTank";
        case SC_ITEM_TYPE_LIFE_SUPPORT_VENT: return "LifeSupportVent";
        case SC_ITEM_TYPE_LIGHT: return "Light";
        case SC_ITEM_TYPE_LIGHT_CONTROLLER: return "LightController";
        case SC_ITEM_TYPE_LIGHTGROUP: return "Lightgroup";
        case SC_ITEM_TYPE_LIVE_SUPPORT: return "LiveSupport";
        case SC_ITEM_TYPE_MAELSTROM_PART: return "MaelstromPart";
        case SC_ITEM_TYPE_MAGAZINE: return "Magazine";
        case SC_ITEM_TYPE_MAIN_ENGINE: return "MainEngine";
        case SC_ITEM_TYPE_MAIN_THRUSTER: return "MainThruster";
        case SC_ITEM_TYPE_MANNEUVER_THRUSTER: return "ManneuverThruster";
        case SC_ITEM_TYPE_MINING_CONTROLLER: return "MiningController";
        case SC_ITEM_TYPE_MINING_MODIFIER: return "MiningModifier";
        case SC_ITEM_TYPE_MISC: return "Misc";
        case SC_ITEM_TYPE_MISSILE: return "Missile";
        case SC_ITEM_TYPE_MISSILE_CONTROLLER: return "MissileController";
        case SC_ITEM_TYPE_MISSILE_LAUNCHER: return "MissileLauncher";
        case SC_ITEM_TYPE_MOBI_GLAS: return "MobiGlas";
        case SC_ITEM_TYPE_MODULE: return "Module";
        case SC_ITEM_TYPE_MULTI_LIGHT: return "MultiLight";
        case SC_ITEM_TYPE_NOITEM_PLAYER: return "NOITEM_Player";
        case SC_ITEM_TYPE_NOITEM_VEHICLE: return "NOITEM_Vehicle";
        case SC_ITEM_TYPE_PAINTS: return "Paints";
        case SC_ITEM_TYPE_PERSISTENT_HAB: return "PersistentHab";
        case SC_ITEM_TYPE_PERSONAL_INNER_THOUGHT: return "PersonalInnerThought";
        case SC_ITEM_TYPE_PING: return "Ping";
        case SC_ITEM_TYPE_PLAYER: return "Player";
        case SC_ITEM_TYPE_POWER_PLANT: return "PowerPlant";
        case SC_ITEM_TYPE_QUANTUM_DRIVE: return "QuantumDrive";
        case SC_ITEM_TYPE_QUANTUM_INTERDICTION_GENERATOR: return "QuantumInterdictionGenerator";
        case SC_ITEM_TYPE_QUANTUM_FUEL_TANK: return "QuantumFuelTank";
        case SC_ITEM_TYPE_RADAR: return "Radar";
        case SC_ITEM_TYPE_RELAY: return "Relay";
        case SC_ITEM_TYPE_REMOTE_CONNECTION: return "RemoteConnection";
        case SC_ITEM_TYPE_REMOVABLE_CHIP: return "RemovableChip";
        case SC_ITEM_TYPE_REMOVABLE_BLADE: return "RemovableBlade";
        case SC_ITEM_TYPE_ROOM: return "Room";
        case SC_ITEM_TYPE_SALVAGE_CONTROLLER: return "SalvageController";
        case SC_ITEM_TYPE_SALVAGE_FIELD_EMITTER: return "SalvageFieldEmitter";
        case SC_ITEM_TYPE_SALVAGE_FIELD_SUPPORTER: return "SalvageFieldSupporter";
        case SC_ITEM_TYPE_SALVAGE_FILLER_STATION: return "SalvageFillerStation";
        case SC_ITEM_TYPE_SALVAGE_HEAD: return "SalvageHead";
        case SC_ITEM_TYPE_SALVAGE_INTERNAL_STORAGE: return "SalvageInternalStorage";
        case SC_ITEM_TYPE_SALVAGE_MODIFIER: return "SalvageModifier";
        case SC_ITEM_TYPE_SCANNER: return "Scanner";
        case SC_ITEM_TYPE_SEAT: return "Seat";
        case SC_ITEM_TYPE_SEAT_ACCESS: return "SeatAccess";
        case SC_ITEM_TYPE_SEAT_DASHBOARD: return "SeatDashboard";
        case SC_ITEM_TYPE_SELF_DESTRUCT: return "SelfDestruct";
        case SC_ITEM_TYPE_SENSOR: return "Sensor";
        case SC_ITEM_TYPE_SHIELD: return "Shield";
        case SC_ITEM_TYPE_SHIELD_CONTROLLER: return "ShieldController";
        case SC_ITEM_TYPE_SHIP: return "Ship";
        case SC_ITEM_TYPE_SHOP_DISPLAY: return "ShopDisplay";
        case SC_ITEM_TYPE_SPACE_MINE: return "SpaceMine";
        case SC_ITEM_TYPE_SPACE_MINE_LAUNCHER: return "SpaceMineLauncher";
        case SC_ITEM_TYPE_STATUS_SCREEN: return "StatusScreen";
        case SC_ITEM_TYPE_SUIT: return "Suit";
        case SC_ITEM_TYPE_TARGET_SELECTOR: return "TargetSelector";
        case SC_ITEM_TYPE_THUMBNAIL: return "Thumbnail";
        case SC_ITEM_TYPE_TOOL_ARM: return "ToolArm";
        case SC_ITEM_TYPE_TOWING_BEAM: return "TowingBeam";
        case SC_ITEM_TYPE_TRACTOR_BEAM: return "TractorBeam";
        case SC_ITEM_TYPE_TRANSPONDER: return "Transponder";
        case SC_ITEM_TYPE_TURRET: return "Turret";
        case SC_ITEM_TYPE_TURRET_BASE: return "TurretBase";
        case SC_ITEM_TYPE_USABLE: return "Usable";
        case SC_ITEM_TYPE_UTILITY_TURRET: return "UtilityTurret";
        case SC_ITEM_TYPE_VISOR: return "Visor";
        case SC_ITEM_TYPE_WEAPON_ATTACHMENT: return "WeaponAttachment";
        case SC_ITEM_TYPE_WEAPON_CONTROLLER: return "WeaponController";
        case SC_ITEM_TYPE_WEAPON_DEFENSIVE: return "WeaponDefensive";
        case SC_ITEM_TYPE_WEAPON_GUN: return "WeaponGun";
        case SC_ITEM_TYPE_WEAPON_MINING: return "WeaponMining";
        case SC_ITEM_TYPE_WEAPON_PERSONAL: return "WeaponPersonal";
        case SC_ITEM_TYPE_WEAPON_REGEN_POOL: return "WeaponRegenPool";
        case SC_ITEM_TYPE_WHEELED_CONTROLLER: return "WheeledController";
        default: return "UNDEFINED";
    }
}

static inline sc_item_type sc_inventory_port_item_type(const sc_inventory_port* port) {
    if (!port || !port->valid || port->item_class_id >= SC_ITEM_TYPE_COUNT_197) {
        return SC_ITEM_TYPE_UNKNOWN;
    }
    return (sc_item_type)port->item_class_id;
}

typedef struct sc_inventory_snapshot {
    uint32_t size;
    uint32_t count;                            /* ports filled, <= SC_INVENTORY_MAX_PORTS */
    uint64_t generation;                       /* changes when the port set changes */
    sc_inventory_port ports[SC_INVENTORY_MAX_PORTS];
    /* 1.1 tail: written only when out->size covers it (a 1.0 caller passes size 3088 and never
     * sees it). Ports come from the whole attached tree, depth first, each port followed by the
     * ports of the container attached to it, empty ports included (docs/game/inventory.md). */
    uint32_t flags;                            /* SC_INVENTORY_FLAG_* */
    uint32_t total_seen;                       /* ports the walk found, written or not; >= count */
} sc_inventory_snapshot;
/* sizeof the 1.0 snapshot: the least out->size a snapshot call accepts. */
#define SC_INVENTORY_SNAPSHOT_SIZE_1_0 3088u
typedef struct sc_inventory_v1 {
    uint32_t size;
    uint32_t _pad;
    /* Copies a snapshot of the LOCAL player's item ports into out (caller sets out->size,
     * out->count = capacity of ports[]). TRUNCATED-style contract: fills min(count, ports),
     * sets count to the number written; generation lets a caller detect growth. With
     * out->size >= sizeof(sc_inventory_snapshot) it also sets flags and total_seen. UNAVAILABLE
     * with no bound backend, off the game thread, no local player, no item-port container on the
     * player, or out->size below SC_INVENTORY_SNAPSHOT_SIZE_1_0.
     * Values are the client's view of server state at snapshot time; read-only by contract. */
    sco_result (*snapshot)(sco_plugin* self, sc_inventory_snapshot* out);
} sc_inventory_v1;
static inline sco_result sc_inventory_snapshot_ports(const sc_inventory_v1* t, sco_plugin* self,
                                                     sc_inventory_snapshot* out) {
    return (t && t->size >= offsetof(sc_inventory_v1, snapshot) + sizeof(t->snapshot) && t->snapshot && out)
               ? t->snapshot(self, out) : SCO_UNAVAILABLE;
}
#endif
