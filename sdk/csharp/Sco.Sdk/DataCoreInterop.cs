// DataCoreInterop.cs: the C ABI of sco_datacore.h 1.1 (the host service "sco.datacore"), as blittable
// C# structs. Part of the Star Citizen extension (sco-sdk-game-sc): DataCore is the game's data
// database, so the game-neutral core SDK leaves it out. Pinned by tests/abi_datacore.c.
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System.Runtime.InteropServices;

namespace Sco.Sdk
{
    /// <summary>sco_dc_type (sco_datacore.h).</summary>
    public enum ScoDcType : int
    {
        Bool = 0,
        Int = 1,
        UInt = 2,
        Float = 3,
        String = 4,
        Guid = 5,
        Enum = 6,
        Null = 7,
        Instance = 8,
        Ref = 9,
        Force32 = 0x7fffffff,
    }

    public static partial class Abi
    {
        public const string DataCoreName = "sco.datacore";        // SCO_DATACORE_NAME
        public const uint DataCoreVersion1_0 = 0x00010000u;
        public const uint DataCoreVersion1_1 = 0x00010001u;
        public const string DataCoreAppliedEvent = "datacore.applied"; // SCO_DC_APPLIED_EVENT
        public const uint DcOpen = 1u;             // state()
        public const uint DcLoaded = 2u;
        public const uint DcNonAtomic = 0x1u;      // begin() flags
        public const uint DcQueued = 1u;           // sco_dc_report.state
        public const uint DcApplied = 2u;
        public const uint DcSkipped = 3u;
        public const uint DcRefused = 4u;
        public const uint DcOpPatch = 0xFFFFFFFFu; // sco_dc_report.op_index of the patch's own entry
        public const uint DcMaxOps = 65536u;
        public const uint DcMaxPatches = 64u;
    }
}

namespace Sco.Sdk.Interop
{
    /// <summary>sco_dc_value: 40 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoDcValue
    {
        public uint size;
        public ScoDcType type;
        public long i;
        public ulong u;
        public double f;
        public byte* s;
    }

    /// <summary>sco_dc_report: 204 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoDcReport
    {
        public uint size;
        public uint state;
        public uint op_index;
        public fixed byte reason[192];
    }

    /// <summary>sco_dc_applied, the data of the event datacore.applied: 16 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ScoDcApplied
    {
        public uint size;
        public uint applied;
        public uint skipped;
        public uint refused;
    }

    /// <summary>sco_datacore_v1, the table of the host service "sco.datacore": 88 bytes.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoDatacoreV1
    {
        public uint size;
        public uint _pad;
        public delegate* unmanaged[Cdecl]<uint> state;
        public delegate* unmanaged[Cdecl]<void*, uint, ulong*, ScoResult> begin;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, ScoDcValue*, ScoResult> set;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, byte*, ulong*, ScoResult> add_instance;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, ulong, ScoResult> set_pointer;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, ScoDcValue*, ScoResult> append;
        public delegate* unmanaged[Cdecl]<ulong, byte*, byte*, byte*, byte*, byte*, ulong*, ScoResult> add_record;
        public delegate* unmanaged[Cdecl]<ulong, ScoResult> commit;
        public delegate* unmanaged[Cdecl]<ulong, ScoResult> discard;
        public delegate* unmanaged[Cdecl]<ulong, uint, ScoDcReport*, ScoResult> report;
    }
}
