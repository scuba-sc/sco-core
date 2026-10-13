// Creative.cs: the game service "game.creative" 1.0 (sc_creative.h) for C# plugins.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    /// <summary>The names, versions and limits sc_creative.h defines.</summary>
    public static partial class GameAbi
    {
        public const string CreativeName = "game.creative";       // SC_CREATIVE_SERVICE_NAME
        public const uint CreativeVersion1_0 = 0x00010000u;      // SC_CREATIVE_SERVICE_VERSION
        public const int CreativeFlySpeedMax = 10000;            // SC_CREATIVE_FLY_SPEED_MAX, m/s
    }

    /// <summary>sc_creative_flag: the toggles State reports.</summary>
    [Flags]
    public enum ScCreativeFlag
    {
        GodMode = 0x1,
        Fly = 0x2,
        InfiniteAmmo = 0x4,
        InfiniteShipAmmo = 0x8,
        Force32 = 0x7fffffff,
    }

    /// <summary>sc_creative_v1, the table of the game service "game.creative": 64 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScCreativeV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<void*, int, ScoResult> set_god_mode;
        public delegate* unmanaged[Cdecl]<void*, int, ScoResult> set_fly_mode;
        public delegate* unmanaged[Cdecl]<void*, float, ScoResult> set_fly_speed;
        public delegate* unmanaged[Cdecl]<void*, int, ScoResult> set_infinite_ammo;
        public delegate* unmanaged[Cdecl]<void*, int, ScoResult> set_infinite_ship_ammo;
        public delegate* unmanaged[Cdecl]<void*, uint*, ScoResult> state;
        public delegate* unmanaged[Cdecl]<void*, byte*, uint*, ScoResult> last_error;
    }

    /// <summary>
    /// The game service "game.creative" (sc_creative.h): god mode, fly and its speed, infinite
    /// ammo, for the local player. Game thread only. Open once: the game pack publishes it before
    /// plugins load and withdraws it after they unload. Every call answers a ScoResult;
    /// <see cref="LastError"/> has the reason for the last failure. A toggle you switch on goes off
    /// when your plugin unloads. Check the capabilities game.creative.god_mode / .fly / .ammo /
    /// .ship_ammo with Plugin.Has.
    /// </summary>
    public sealed unsafe class Creative
    {
        private Plugin? _plugin;
        private ScCreativeV1* _t;

        public bool IsOpen => _t != null;

        /// <summary>Queries game.creative 1.0. NotFound without the game pack or its services.</summary>
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            ScoResult r = plugin.Query(GameAbi.CreativeName, GameAbi.CreativeVersion1_0, out ServiceRef<ScCreativeV1> s);
            if (r != ScoResult.Ok) return r;
            if (!s.Covers(56)) return ScoResult.Unavailable;   // last_error, the last 1.0 function
            _plugin = plugin;
            _t = s.Table;
            return ScoResult.Ok;
        }

        private void* Self => _plugin == null ? null : _plugin.SelfPtr;

        public ScoResult SetGodMode(bool on) => _t == null || _t->size < (uint)((byte*)&_t->set_god_mode - (byte*)_t + sizeof(nint)) || _t->set_god_mode == null ? ScoResult.Unavailable : _t->set_god_mode(Self, on ? 1 : 0);
        public ScoResult SetFlyMode(bool on) => _t == null || _t->size < (uint)((byte*)&_t->set_fly_mode - (byte*)_t + sizeof(nint)) || _t->set_fly_mode == null ? ScoResult.Unavailable : _t->set_fly_mode(Self, on ? 1 : 0);

        /// <summary>Metres per second, above 0 and at most <see cref="GameAbi.CreativeFlySpeedMax"/>.</summary>
        public ScoResult SetFlySpeed(float mps) => _t == null || _t->size < (uint)((byte*)&_t->set_fly_speed - (byte*)_t + sizeof(nint)) || _t->set_fly_speed == null ? ScoResult.Unavailable : _t->set_fly_speed(Self, mps);
        public ScoResult SetInfiniteAmmo(bool on) => _t == null || _t->size < (uint)((byte*)&_t->set_infinite_ammo - (byte*)_t + sizeof(nint)) || _t->set_infinite_ammo == null ? ScoResult.Unavailable : _t->set_infinite_ammo(Self, on ? 1 : 0);
        public ScoResult SetInfiniteShipAmmo(bool on) => _t == null || _t->size < (uint)((byte*)&_t->set_infinite_ship_ammo - (byte*)_t + sizeof(nint)) || _t->set_infinite_ship_ammo == null ? ScoResult.Unavailable : _t->set_infinite_ship_ammo(Self, on ? 1 : 0);

        /// <summary>The toggles that are on, whoever switched them on.</summary>
        public ScoResult State(out ScCreativeFlag flags)
        {
            uint f = 0;
            ScoResult r = _t == null || _t->size < (uint)((byte*)&_t->state - (byte*)_t + sizeof(nint)) || _t->state == null ? ScoResult.Unavailable : _t->state(Self, &f);
            flags = (ScCreativeFlag)f;
            return r;
        }

        /// <summary>The reason for the last failed call ("" if none).</summary>
        public string LastError()
        {
            if (_t == null || _t->size < (uint)((byte*)&_t->last_error - (byte*)_t + sizeof(nint)) || _t->last_error == null) return "";
            return Storage.Sized((buf, size) => _t->last_error(Self, buf, size), out byte[] b) == ScoResult.Ok ? Utf8.FromBytes(b) : "";
        }
    }
}
