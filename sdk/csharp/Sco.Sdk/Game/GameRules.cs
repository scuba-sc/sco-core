// GameRules.cs: the game pack's service "game.gamerules" 1.0 (sc_gamerules.h): a read-only view of
// the server GameRules parameters the client received. Sco.Sdk.Game holds the game pack's services
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
        public const string GameRulesName = "game.gamerules";            // SC_GAMERULES_SERVICE_NAME
        public const uint GameRulesVersion1_0 = 0x00010000u;             // SC_GAMERULES_SERVICE_VERSION
    }

    /// <summary>sc_gamerules_state: the snapshot, 56 bytes. All zero apart from size when the call
    /// doesn't reach the game.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ScGamerulesState
    {
        public uint size;
        public uint valid;                           // 1 = a parameter set has been received
        public ulong generation;                     // opaque sync generation, not a session id
        public uint enforce_group_qt;                // server gate: group quantum travel enforced
        public uint quantum_travel_allowed;          // server gate: quantum travel allowed
        public uint quantum_boost_allowed;           // server gate: quantum boost allowed
        public float travel_target_threshold_angle;  // degrees; 0 when unset
        public float target_lock_angular_player;
        public float target_lock_angular_ai;
        public float target_lock_linear_player;
        public float target_lock_linear_ai;
        public float boost_group_distance_threshold;
    }

    /// <summary>sc_gamerules_v1, the table of the game service "game.gamerules": 16 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScGamerulesV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<void*, ScGamerulesState*, ScoResult> read;
    }

    /// <summary>
    /// The game service "game.gamerules" (docs/game/gamerules.md). Published by the Star Citizen
    /// game pack: Open answers NotFound on a host without it. Game thread only (a command, a tick or
    /// a RunOnGameThread task). Read-only: the server owns these values; nothing here writes them
    /// and nothing writes game memory. Capability: game.gamerules.read, OFF by default; the default
    /// production backend binds nothing, so Read then answers Unavailable without touching the game.
    /// There is no LastError: the table has no such member. Not run in game.
    /// </summary>
    public sealed unsafe class GameRules
    {
        private Plugin? _plugin;
        private ScGamerulesV1* _t;

        public bool IsOpen => _t != null;

        /// <summary>Queries game.gamerules 1.0. NotFound without the game pack's services;
        /// Unavailable on a 1.0 host or a table shorter than 1.0's.</summary>
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null;
            _plugin = null;
            ScoResult r = plugin.Query(GameAbi.GameRulesName, GameAbi.GameRulesVersion1_0, out ServiceRef<ScGamerulesV1> s);
            if (r != ScoResult.Ok) return r;
            if (!s.Covers(8)) return ScoResult.Unavailable;   // read, the only 1.0 function
            _plugin = plugin;
            _t = s.Table;
            return ScoResult.Ok;
        }

        private void* Self => _plugin == null ? null : _plugin.SelfPtr;
        private bool Covers(uint offset) => new ServiceRef<ScGamerulesV1>(_t).Covers(offset);

        /// <summary>Copies the client's current server GameRules parameter snapshot into state.
        /// state is zeroed (size set) before the call, so it stays zero apart from size when the
        /// call doesn't reach the game. Check state.valid: 0 means no parameter set was received
        /// yet. game.gamerules.read.</summary>
        public ScoResult Read(out ScGamerulesState state)
        {
            ScGamerulesState s = default;
            s.size = (uint)sizeof(ScGamerulesState);
            ScoResult r = !Covers(8) || _t->read == null ? ScoResult.Unavailable : _t->read(Self, &s);
            state = s;
            return r;
        }
    }
}
