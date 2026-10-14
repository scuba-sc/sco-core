// Hangar.cs: the game pack's service "game.hangar" 1.0 (sc_hangar.h), layout only.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
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
}
