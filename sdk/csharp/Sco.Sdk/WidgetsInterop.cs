// SPDX-License-Identifier: GPL-3.0-only
using System.Runtime.InteropServices;

namespace Sco.Sdk
{
    public enum WidgetKind : uint
    {
        Label = 1, Button = 2, Toggle = 3, Slider = 4, Combo = 5,
        List = 6, TextInput = 7, Table = 8, Row = 9, Column = 10
    }
    public static partial class Abi
    {
        public const string WidgetsName = "sco.widgets";
        public const uint WidgetsVersion1_0 = 0x00010000u;
        public const uint WidgetsMaxPanels = 64, WidgetsMaxNodes = 256, WidgetsMaxTotalNodes = 4096;
        public const uint WidgetsMaxItems = 256, WidgetsMaxColumns = 16, WidgetsMaxText = 255;
        public const uint WidgetsMaxId = 63, WidgetsMaxTitle = 63, WidgetsMaxEvents = 256;
        public const uint WidgetDisabled = 1;
    }
}
namespace Sco.Sdk.Interop
{
    /// <summary>sco_widget_node: 80 bytes, alignment 8 on supported 64-bit targets.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetNode
    {
        public uint size, kind, id, parent, flags;
        public int index;
        public byte* label;
        public byte* text;
        public double value, minimum, maximum;
        public byte** items;
        public uint item_count, columns;
    }
    /// <summary>sco_widget_event: 32 bytes; pointed-to text is borrowed only during callback.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetEvent
    {
        public uint size, node_id, kind;
        public int index;
        public double value;
        public byte* text;
    }
    /// <summary>sco_widgets_v1: 24 bytes. Check the whole member extent before reading it.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetsV1
    {
        public uint size, _pad;
        public delegate* unmanaged[Cdecl]<void*, byte*, byte*, ScoWidgetNode*, uint, uint,
            delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void>, void*, ScoResult> set_panel;
        public delegate* unmanaged[Cdecl]<void*, byte*, ScoResult> remove_panel;
    }
}
