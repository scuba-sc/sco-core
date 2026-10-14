// Hangar.cs: the game pack's service "game.hangar" 1.0 (sc_hangar.h): the ship lift of a landing
// area, a lift open/close request, and withdrawing an ATC request. Sco.Sdk.Game holds the game
// pack's services (docs/game-services.md).
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    public static partial class GameAbi
    {
        public const string HangarName = "game.hangar";
        public const uint HangarVersion1_0 = 0x00010000u;
    }

    public enum ScHangarLiftOp
    {
        Open = 1,
        Close = 2,
        Force32 = 0x7fffffff,
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScHangarV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<void*, ulong, ulong*, ScoResult> lift_of_landing_area;
        public delegate* unmanaged[Cdecl]<void*, ulong, uint, ScoResult> lift_request;
        public delegate* unmanaged[Cdecl]<void*, ulong, ScoResult> atc_cancel_request;
        public delegate* unmanaged[Cdecl]<void*, byte*, uint*, ScoResult> last_error;
    }

    /// <summary>
    /// The game service "game.hangar" (docs/game/hangar.md). Published by the Star Citizen game
    /// pack: Open answers NotFound on a host without it. Game thread only (a command, a tick or a
    /// RunOnGameThread task); from another thread every call answers WrongThread. Nothing is owned:
    /// a lift request or an ATC cancel is one call into the game and nothing is undone at unload.
    /// Capabilities: game.hangar.lift, game.hangar.atc_cancel; both OFF by default (a call then
    /// answers Unavailable without touching the game).
    /// </summary>
    public sealed unsafe class Hangar
    {
        private Plugin? _plugin;
        private ScHangarV1* _t;

        public bool IsOpen => _t != null;

        /// <summary>Queries game.hangar 1.0. NotFound without the game pack's services;
        /// Unavailable on a 1.0 host or a table shorter than 1.0's.</summary>
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null;
            _plugin = null;
            ScoResult r = plugin.Query(GameAbi.HangarName, GameAbi.HangarVersion1_0, out ServiceRef<ScHangarV1> s);
            if (r != ScoResult.Ok) return r;
            if (!s.Covers(32)) return ScoResult.Unavailable;   // last_error, the last 1.0 function
            _plugin = plugin;
            _t = s.Table;
            return ScoResult.Ok;
        }

        private void* Self => _plugin == null ? null : _plugin.SelfPtr;
        private bool Covers(uint offset) => new ServiceRef<ScHangarV1>(_t).Covers(offset);

        /// <summary>liftId = the ship lift serving the landing area landingAreaId (0 on failure).
        /// NotFound: the landing area isn't streamed in, or no lift goes with it. game.hangar.lift.</summary>
        public ScoResult LiftOfLandingArea(ulong landingAreaId, out ulong liftId)
        {
            ulong l = 0;
            ScoResult r = !Covers(8) || _t->lift_of_landing_area == null ? ScoResult.Unavailable : _t->lift_of_landing_area(Self, landingAreaId, &l);
            liftId = l;
            return r;
        }

        /// <summary>Asks the ship lift liftId to open or close. Ok means the request was handed to
        /// the game, not that the lift has moved. game.hangar.lift.</summary>
        public ScoResult LiftRequest(ulong liftId, ScHangarLiftOp op) =>
            !Covers(16) || _t->lift_request == null ? ScoResult.Unavailable : _t->lift_request(Self, liftId, (uint)op);

        /// <summary>Withdraws the local player's pending request at the air traffic controller atcId.
        /// game.hangar.atc_cancel.</summary>
        public ScoResult AtcCancelRequest(ulong atcId) =>
            !Covers(24) || _t->atc_cancel_request == null ? ScoResult.Unavailable : _t->atc_cancel_request(Self, atcId);

        /// <summary>The reason for this plugin's last failed call ("" if none).</summary>
        public string LastError()
        {
            if (!Covers(32) || _t->last_error == null) return "";
            nint t = (nint)_t;
            nint self = (nint)Self;
            return Storage.Sized((buf, size) => ((ScHangarV1*)t)->last_error((void*)self, buf, size), out byte[] b) == ScoResult.Ok
                ? Utf8.FromBytes(b) : "";
        }
    }
}
