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
        public const uint WidgetsVersion1_1 = 0x00010001u;
        public const uint WidgetsVersion1_2 = 0x00010002u;
        public const uint WidgetsMaxPanels = 64, WidgetsMaxNodes = 256, WidgetsMaxTotalNodes = 4096;
        public const uint WidgetsMaxItems = 256, WidgetsMaxColumns = 16, WidgetsMaxText = 255;
        public const uint WidgetsMaxId = 63, WidgetsMaxTitle = 63, WidgetsMaxEvents = 256;
        public const uint WidgetsFeatureStableTags = 0x80, WidgetsCapMaxTag = 9, WidgetsCaps1_2 = 10, WidgetsMaxTag = 63;
        public const uint WidgetSeverityNone = 0, WidgetSeverityInfo = 1, WidgetSeverityWarning = 2, WidgetSeverityError = 3;
        public const uint WidgetEventPage = 0x100;
        public const uint WidgetsCapCount = 0, WidgetsCapFeatures = 1, WidgetsCapMaxPanels = 2, WidgetsCapMaxNodes = 3;
        public const uint WidgetsCapMaxTotalNodes = 4, WidgetsCapMaxItems = 5, WidgetsCapMaxColumns = 6;
        public const uint WidgetsCapMaxText = 7, WidgetsCapMaxEvents = 8, WidgetsCaps1_1 = 9;
        public const uint WidgetsFeatureUpdateNodes = 0x01, WidgetsFeatureRemoveNodes = 0x02, WidgetsFeatureSeverity = 0x04;
        public const uint WidgetsFeatureTooltip = 0x08, WidgetsFeaturePaging = 0x10, WidgetsFeatureEventRow = 0x20;
        public const uint WidgetsFeatureCoalescedRender = 0x40;
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
    /// <summary>sco_widget_node_1_1: 104 bytes; tooltip UTF-8 &lt;=255, severity/paging per the header.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetNode11
    {
        public ScoWidgetNode @base;
        public byte* tooltip;
        public uint severity, first_row, visible_rows, _reserved;
    }
    /// <summary>sco_widget_node_1_2: 128 bytes; tag is a stable [A-Za-z0-9._-]{1..63}
    /// automation identifier, unique per panel, never rendered.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetNode12
    {
        public ScoWidgetNode11 @base;
        public byte* tag;
        public fixed uint _reserved[3];
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
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetEvent11
    {
        public ScoWidgetEvent @base;
        public int row;
        public uint _reserved;
    }
    /// <summary>sco_widgets_v1: 48 bytes; 24-byte1.0 prefix. Check each member extent before reading it.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScoWidgetsV1
    {
        public uint size, _pad;
        public delegate* unmanaged[Cdecl]<void*, byte*, byte*, ScoWidgetNode*, uint, uint,
            delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void>, void*, ScoResult> set_panel;
        public delegate* unmanaged[Cdecl]<void*, byte*, ScoResult> remove_panel;
        public delegate* unmanaged[Cdecl]<void*, byte*, ScoWidgetNode*, uint, uint, ScoResult> update_nodes;
        public delegate* unmanaged[Cdecl]<void*, byte*, uint*, uint, ScoResult> remove_nodes;
        public delegate* unmanaged[Cdecl]<void*, uint*, ScoResult> capabilities;
    }
}
