// Reputation.cs: the game pack's service "game.reputation" 1.0 (sc_reputation.h), layout only.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
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
}
