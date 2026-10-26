// GameRules.cs: the game pack's service "game.gamerules" 1.1 (sc_gamerules.h): read-only snapshots
// and copied static parameter definitions. Sco.Sdk.Game holds the game pack's services
// (docs/game-services.md).
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Runtime.InteropServices;
using System.Text;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    public static partial class GameAbi
    {
        public const string GameRulesName = "game.gamerules";            // SC_GAMERULES_SERVICE_NAME
        public const uint GameRulesVersion1_0 = 0x00010000u;             // SC_GAMERULES_SERVICE_VERSION
        public const uint GameRulesVersion1_1 = 0x00010001u;
        public const uint GameRulesMaxPage = 64;
        public const uint GameRulesParamInt = 1;
        public const uint GameRulesParamFloat = 2;
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

    /// <summary>sc_gamerules_param_info: copied definition metadata, 88 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScGamerulesParamInfo
    {
        public uint size;
        public uint type;
        public fixed byte name[48];
        public fixed byte group[32];
    }

    public readonly record struct GameRulesParameter(uint Type, string Name, string Group);

    /// <summary>sc_gamerules_v1: 24 bytes including the optional 1.1 list_params tail.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScGamerulesV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<void*, ScGamerulesState*, ScoResult> read;
        public delegate* unmanaged[Cdecl]<void*, uint, ScGamerulesParamInfo*, uint, uint, uint*, uint*, ScoResult> list_params;
    }

    /// <summary>
    /// The game service "game.gamerules" (docs/game/gamerules.md). Published by the Star Citizen
    /// game pack: Open answers NotFound on a host without it. Game thread only (a command, a tick or
    /// a RunOnGameThread task). Read-only: the server owns these values; nothing here writes them
    /// and nothing writes game memory. Capability: game.gamerules.read, OFF by default; the default
    /// production backend binds nothing, so Read then answers Unavailable without touching the game.
    /// ListParams separately copies static definitions without a backend or live values.
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

        private static readonly UTF8Encoding ParameterText = new(false, true);
        private static bool CopyParameterText(byte* text, int capacity, out string value)
        {
            value = string.Empty;
            var bytes = new ReadOnlySpan<byte>(text, capacity);
            int end = bytes.IndexOf((byte)0);
            if (end < 0) return false;
            try { value = ParameterText.GetString(bytes[..end]); return true; }
            catch (DecoderFallbackException) { return false; }
        }

        /// <summary>Copies up to 64 static parameter definitions, without values or a backend.
        /// Capacity zero counts only. On failure parameters is empty and total is zero.</summary>
        public ScoResult ListParams(uint offset, uint capacity, out GameRulesParameter[] parameters, out uint total)
        {
            parameters = Array.Empty<GameRulesParameter>();
            total = 0;
            if (capacity > GameAbi.GameRulesMaxPage) return ScoResult.BadArg;
            if (!IsOpen || _plugin == null) return ScoResult.Unavailable;
            ScoResult result = _plugin.Query(GameAbi.GameRulesName, GameAbi.GameRulesVersion1_1,
                out ServiceRef<ScGamerulesV1> service);
            if (result != ScoResult.Ok) return result;
            if (!service.Covers(16) || service.Table->list_params == null) return ScoResult.Unavailable;
            ScGamerulesParamInfo* page = stackalloc ScGamerulesParamInfo[64];
            new Span<ScGamerulesParamInfo>(page, 64).Clear();
            for (int i = 0; i < (int)capacity; ++i) page[i].size = (uint)sizeof(ScGamerulesParamInfo);
            uint count = 0, found = 0;
            result = service.Table->list_params(Self, offset, capacity == 0 ? null : page,
                (uint)sizeof(ScGamerulesParamInfo), capacity, &count, &found);
            if (result != ScoResult.Ok) return result;
            if (offset > found || count != Math.Min(capacity, found - offset)) return ScoResult.Failed;
            var copied = new GameRulesParameter[count];
            for (int i = 0; i < (int)count; ++i)
            {
                if (page[i].size != (uint)sizeof(ScGamerulesParamInfo) ||
                    !CopyParameterText(page[i].name, 48, out string name) ||
                    !CopyParameterText(page[i].group, 32, out string group)) return ScoResult.Failed;
                copied[i] = new GameRulesParameter(page[i].type, name, group);
            }
            parameters = copied;
            total = found;
            return ScoResult.Ok;
        }
    }
}
