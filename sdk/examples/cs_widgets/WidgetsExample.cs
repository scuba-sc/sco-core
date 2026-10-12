// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Runtime.InteropServices;
using Sco.Sdk;
using Sco.Sdk.Interop;

namespace CsWidgets
{
    // Synthetic data only; no renderer, GPU or game dependencies.
    public sealed class Demo : Plugin
    {
        private readonly Widgets _widgets = new();
        public int Events { get; private set; }
        public override ScoResult OnLoad()
        {
            ScoResult r = _widgets.Open(this);
            if (r is ScoResult.NotFound or ScoResult.Unavailable) return ScoResult.Ok;
            if (r != ScoResult.Ok) return r;
            WidgetNode[] nodes = {
                new() { Id = 1, Kind = WidgetKind.Column },
                new() { Id = 2, Kind = WidgetKind.Row, Parent = 1 },
                new() { Id = 3, Kind = WidgetKind.Label, Parent = 2, Label = "Example status" },
                new() { Id = 4, Kind = WidgetKind.Button, Parent = 2, Label = "Ping" },
                new() { Id = 5, Kind = WidgetKind.Toggle, Parent = 1, Label = "Enabled", Value = 1 },
                new() { Id = 6, Kind = WidgetKind.Slider, Parent = 1, Label = "Level", Value = 3, Maximum = 10 },
                new() { Id = 7, Kind = WidgetKind.Combo, Parent = 1, Label = "Mode", Index = 0, Items = new[] { "A", "B" } },
                new() { Id = 8, Kind = WidgetKind.List, Parent = 1, Label = "Entries", Index = -1, Items = new[] { "One" } },
                new() { Id = 9, Kind = WidgetKind.TextInput, Parent = 1, Label = "Name", Text = "Example" },
                new() { Id = 10, Kind = WidgetKind.Table, Parent = 1, Columns = 2, Items = new[] { "Name", "Value" }, Disabled = true }
            };
            r = _widgets.SetPanel("cs_widgets.main", "C# widgets", nodes, (_, e) =>
            {
                ++Events;
                if (e.Kind == WidgetKind.Button) Invoke("example.ping", out _);
                Info($"widgets event {e.Kind} node {e.NodeId}");
            });
            return r == ScoResult.Unavailable ? ScoResult.Ok : r;
        }
        public override void OnUnload() { if (_widgets.IsOpen) _widgets.RemovePanel("cs_widgets.main"); }
    }
    public static unsafe class Exports
    {
        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_query")]
        public static ScoPluginInfo* Query() => PluginExports.Query("cs_widgets", "1.0.0", "sco SDK example");
        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_load")]
        public static ScoResult Load(ScoApi* api, void* self) => PluginExports.Load<Demo>(api, self, "cs_widgets");
        [UnmanagedCallersOnly(EntryPoint = "sco_plugin_unload")]
        public static void Unload() => PluginExports.Unload();
    }
}
