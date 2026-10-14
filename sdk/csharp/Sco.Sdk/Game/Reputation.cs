// Reputation.cs: the game pack's service "game.reputation" 1.0 (sc_reputation.h): a read-only check of
// the game's reputation-service check sites. Sco.Sdk.Game holds the game pack's services
// (docs/game-services.md).
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    public static partial class GameAbi
    {
        public const string ReputationName = "game.reputation";
        public const uint ReputationVersion1_0 = 0x00010000u;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ScReputationChecks
    {
        public uint size;
        public uint sites;
        public uint intact;
        public uint services;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScReputationV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<void*, ScReputationChecks*, ScoResult> checks;
        public delegate* unmanaged[Cdecl]<void*, byte*, uint*, ScoResult> last_error;
    }

    /// <summary>
    /// The game service "game.reputation" (docs/game/reputation.md). Published by the Star Citizen
    /// game pack: Open answers NotFound on a host without it. Game thread only (a command, a tick or
    /// a RunOnGameThread task); from another thread every call answers WrongThread. Read-only:
    /// nothing here reads or changes a player's standing, and nothing writes game memory.
    /// Capability: game.reputation.checks, OFF by default (Checks then answers Unavailable without
    /// touching the game).
    /// </summary>
    public sealed unsafe class Reputation
    {
        private Plugin? _plugin;
        private ScReputationV1* _t;

        public bool IsOpen => _t != null;

        /// <summary>Queries game.reputation 1.0. NotFound without the game pack's services;
        /// Unavailable on a 1.0 host or a table shorter than 1.0's.</summary>
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null;
            _plugin = null;
            ScoResult r = plugin.Query(GameAbi.ReputationName, GameAbi.ReputationVersion1_0, out ServiceRef<ScReputationV1> s);
            if (r != ScoResult.Ok) return r;
            if (!s.Covers(16)) return ScoResult.Unavailable;   // last_error, the last 1.0 function
            _plugin = plugin;
            _t = s.Table;
            return ScoResult.Ok;
        }

        private void* Self => _plugin == null ? null : _plugin.SelfPtr;
        private bool Covers(uint offset) => new ServiceRef<ScReputationV1>(_t).Covers(offset);

        /// <summary>Fills checks from the game's reputation-service check sites. checks is zeroed
        /// (size set) before the call, so it stays zero apart from size when the call doesn't reach
        /// the game. game.reputation.checks.</summary>
        public ScoResult Checks(out ScReputationChecks checks)
        {
            ScReputationChecks c = default;
            c.size = (uint)sizeof(ScReputationChecks);
            ScoResult r = !Covers(8) || _t->checks == null ? ScoResult.Unavailable : _t->checks(Self, &c);
            checks = c;
            return r;
        }

        /// <summary>The reason for this plugin's last failed call ("" if none).</summary>
        public string LastError()
        {
            if (!Covers(16) || _t->last_error == null) return "";
            nint t = (nint)_t;
            nint self = (nint)Self;
            return Storage.Sized((buf, size) => ((ScReputationV1*)t)->last_error((void*)self, buf, size), out byte[] b) == ScoResult.Ok
                ? Utf8.FromBytes(b) : "";
        }
    }
}
