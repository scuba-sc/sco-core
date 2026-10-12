// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Text;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk
{
    /// <summary>One node of a complete preorder tree. Unused properties must be zero/null;
    /// Index may also be -1. Inputs are copied for the duration of SetPanel.</summary>
    public readonly record struct WidgetNode
    {
        public uint Id { get; init; }
        public WidgetKind Kind { get; init; }
        public uint Parent { get; init; }
        public bool Disabled { get; init; }
        public int Index { get; init; }
        public string? Label { get; init; }
        public string? Text { get; init; }
        public double Value { get; init; }
        public double Minimum { get; init; }
        public double Maximum { get; init; }
        public string[]? Items { get; init; }
        public uint Columns { get; init; }
    }

    /// <summary>Copied typed event; unused fields are zero/null. Text remains valid after return.</summary>
    public readonly record struct WidgetEvent(uint NodeId, WidgetKind Kind, int Index, double Value, string? Text);

    public abstract unsafe partial class Plugin
    {
        internal readonly Dictionary<string, nint> WidgetCallbacks = new();
        internal readonly object WidgetLock = new();
        internal uint WidgetEpoch;
        partial void ReleaseWidgets()
        {
            lock (WidgetLock) { WidgetCallbacks.Clear(); ++WidgetEpoch; }
        }
    }

    /// <summary>Renderer-neutral retained panels. All mutations and callbacks are game-thread
    /// only; the host is the authority for thread checks. Open again after owner reload.</summary>
    public sealed unsafe class Widgets
    {
        private Plugin? _plugin;
        private ScoWidgetsV1* _t;
        private uint _epoch;
        public bool IsOpen => _t != null && _plugin != null && _plugin.IsLoaded && _epoch == _plugin.WidgetEpoch;
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null;
            _plugin = null;
            if (!plugin.IsLoaded) return ScoResult.BadArg;
            ScoApi* api = plugin.Api;
            if (api == null || api->size < ApiOffset.query_service + 8 || api->query_service == null) return ScoResult.Unavailable;
            ScoResult r = plugin.Query(Abi.WidgetsName, Abi.WidgetsVersion1_0, out ServiceRef<ScoWidgetsV1> s);
            if (r != ScoResult.Ok) return r;
            _plugin = plugin;
            _epoch = plugin.WidgetEpoch;
            _t = s.Table;
            return ScoResult.Ok;
        }
        private static readonly UTF8Encoding StrictUtf8 = new(false, true);
        private sealed class InvalidInput : Exception
        {
            public InvalidInput(ScoResult result) { Result = result; }
            public ScoResult Result { get; }
        }
        private static void Require(bool condition, ScoResult result = ScoResult.BadArg)
        {
            if (!condition) throw new InvalidInput(result);
        }
        private static void CheckText(string? text, int max, bool nullable, ScoResult tooLong)
        {
            if (text == null) { Require(nullable); return; }
            Require(text.Length <= max, tooLong);
            Require(!text.Contains('\0'));
            Require(StrictUtf8.GetByteCount(text) <= max, tooLong);
        }
        private static void CheckId(string? id, Plugin plugin)
        {
            Require(id != null && id.Length > 0 && id.Length <= Abi.WidgetsMaxId);
            Require(id!.StartsWith(plugin.Id + ".", StringComparison.Ordinal));
            bool segment = false;
            foreach (char c in id)
            {
                if (c == '.') { Require(segment); segment = false; }
                else { Require(c is >= 'a' and <= 'z' or >= '0' and <= '9' or '_'); segment = true; }
            }
            Require(segment);
        }
        private static WidgetNode[] CopyNodes(ReadOnlySpan<WidgetNode> input)
        {
            Require(input.Length > 0);
            Require(input.Length <= Abi.WidgetsMaxNodes, ScoResult.TooMany);
            var nodes = input.ToArray();
            var ids = new HashSet<uint>();
            var parents = new List<uint>();
            for (int i = 0; i < nodes.Length; ++i)
            {
                WidgetNode n = nodes[i];
                Require(n.Id != 0 && ids.Add(n.Id) && n.Kind is >= WidgetKind.Label and <= WidgetKind.Column);
                if (n.Parent == 0) parents.Clear();
                else
                {
                    int parent = parents.LastIndexOf(n.Parent);
                    Require(parent >= 0);
                    parents.RemoveRange(parent + 1, parents.Count - parent - 1);
                }
                if (n.Kind is WidgetKind.Row or WidgetKind.Column) parents.Add(n.Id);
                bool numeric = n.Kind is WidgetKind.Toggle or WidgetKind.Slider;
                bool selection = n.Kind is WidgetKind.Combo or WidgetKind.List;
                bool items = selection || n.Kind == WidgetKind.Table;
                Require(double.IsFinite(n.Value) && double.IsFinite(n.Minimum) && double.IsFinite(n.Maximum));
                Require(numeric || n.Value == 0);
                Require(n.Kind == WidgetKind.Slider || (n.Minimum == 0 && n.Maximum == 0));
                Require(selection || n.Index is 0 or -1);
                Require(n.Kind == WidgetKind.TextInput || n.Text == null);
                Require(items || n.Items == null);
                Require(n.Kind == WidgetKind.Table || n.Columns == 0);
                Require(n.Kind != WidgetKind.Toggle || n.Value is 0 or 1);
                Require(n.Kind != WidgetKind.Slider || (n.Minimum <= n.Maximum && n.Value >= n.Minimum && n.Value <= n.Maximum));
                int count = n.Items?.Length ?? 0;
                Require(count <= Abi.WidgetsMaxItems && n.Columns <= Abi.WidgetsMaxColumns, ScoResult.TooMany);
                Require(!selection || (n.Index >= -1 && n.Index < count));
                Require(n.Kind != WidgetKind.Table || (n.Columns > 0 && count % n.Columns == 0));
                CheckText(n.Label, 255, true, ScoResult.TooMany);
                Require(n.Kind is < WidgetKind.Button or > WidgetKind.TextInput || !string.IsNullOrEmpty(n.Label));
                CheckText(n.Text, 255, true, ScoResult.TooMany);
                if (n.Items != null)
                {
                    var copy = (string[])n.Items.Clone();
                    foreach (string text in copy) CheckText(text, 255, false, ScoResult.TooMany);
                    n = n with { Items = copy };
                }
                nodes[i] = n;
            }
            return nodes;
        }
        public ScoResult SetPanel(string id, string title, ReadOnlySpan<WidgetNode> nodes, Action<string, WidgetEvent>? callback = null)
        {
            if (!IsOpen) return ScoResult.Unavailable;
            if (_t->size < 16 || _t->set_panel == null) return ScoResult.Unavailable;
            var arena = new NativeArena();
            nint context = 0;
            lock (_plugin!.WidgetLock)
            try
            {
                CheckId(id, _plugin!);
                CheckText(title, 63, false, ScoResult.BadArg);
                Require(title.Length > 0);
                WidgetNode[] copied = CopyNodes(nodes);
                nodes = copied;
                if (callback != null)
                {
                    Require(_plugin.WidgetCallbacks.ContainsKey(id) || _plugin.WidgetCallbacks.Count < Abi.WidgetsMaxPanels, ScoResult.TooMany);
                    _plugin.WidgetCallbacks.EnsureCapacity(_plugin.WidgetCallbacks.Count + 1);
                    context = Registry.Add(new CallbackEntry(_plugin, id, copied, callback));
                }
                var native = (ScoWidgetNode*)arena.Alloc((nuint)(nodes.Length * sizeof(ScoWidgetNode)));
                for (int i = 0; i < nodes.Length; ++i)
                {
                    WidgetNode n = nodes[i];
                    native[i] = new ScoWidgetNode { size = (uint)sizeof(ScoWidgetNode), kind = (uint)n.Kind,
                        id = n.Id, parent = n.Parent, flags = n.Disabled ? Abi.WidgetDisabled : 0,
                        index = n.Index, label = arena.Add(n.Label), text = arena.Add(n.Text),
                        value = n.Value, minimum = n.Minimum, maximum = n.Maximum, columns = n.Columns };
                    if (n.Items != null && n.Items.Length > 0)
                    {
                        native[i].item_count = (uint)n.Items.Length;
                        native[i].items = (byte**)arena.Alloc((nuint)n.Items.Length * (nuint)sizeof(byte*));
                        for (int j = 0; j < n.Items.Length; ++j) native[i].items[j] = arena.Add(n.Items[j]);
                    }
                }
                ScoResult result = _t->set_panel(_plugin.SelfPtr, arena.Add(id), arena.Add(title), native,
                    (uint)nodes.Length, (uint)sizeof(ScoWidgetNode), callback == null ? null : &EventTrampoline, (void*)context);
                if (result == ScoResult.Ok)
                {
                    if (_plugin.WidgetCallbacks.Remove(id, out nint old)) Registry.Remove(old);
                    if (context != 0) _plugin.WidgetCallbacks[id] = context;
                    context = 0; // ownership transferred to the owner's registry
                }
                return result;
            }
            catch (InvalidInput e) { return e.Result; }
            catch (EncoderFallbackException) { return ScoResult.BadArg; }
            catch (OutOfMemoryException) { return ScoResult.TooMany; }
            finally { if (context != 0) Registry.Remove(context); arena.FreeAll(); }
        }
        private sealed class CallbackEntry : Entry
        {
            public CallbackEntry(Plugin p, string id, WidgetNode[] nodes, Action<string, WidgetEvent> fn) : base(p)
            { Id = id; Nodes = nodes; Fn = fn; }
            public string Id { get; }
            public WidgetNode[] Nodes { get; }
            public Action<string, WidgetEvent> Fn { get; }
        }
        private static string ReadBounded(byte* p, int max)
        {
            Require(p != null);
            int n = 0;
            while (n <= max && p[n] != 0) ++n;
            Require(n <= max);
            return StrictUtf8.GetString(new ReadOnlySpan<byte>(p, n));
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void EventTrampoline(byte* panel, ScoWidgetEvent* ev, void* ctx)
        {
            try
            {
                CallbackEntry? entry = Registry.Get<CallbackEntry>(ctx);
                if (entry == null || !entry.Owner.IsLoaded || ev == null || ev->size < sizeof(ScoWidgetEvent)) return;
                if (ReadBounded(panel, 63) != entry.Id) return;
                WidgetNode node = default;
                bool enabled = true;
                foreach (WidgetNode n in entry.Nodes) if (n.Id == ev->node_id) { node = n; break; }
                if (node.Id == 0 || (uint)node.Kind != ev->kind) return;
                for (WidgetNode n = node; n.Id != 0;)
                {
                    if (n.Disabled) enabled = false;
                    uint parent = n.Parent;
                    n = default;
                    if (parent != 0) foreach (WidgetNode candidate in entry.Nodes) if (candidate.Id == parent) { n = candidate; break; }
                }
                if (!enabled || !double.IsFinite(ev->value)) return;
                bool numeric = node.Kind is WidgetKind.Toggle or WidgetKind.Slider;
                bool selection = node.Kind is WidgetKind.Combo or WidgetKind.List;
                if ((!numeric && ev->value != 0) || (!selection && ev->index != 0) ||
                    (node.Kind != WidgetKind.TextInput && ev->text != null && ReadBounded(ev->text, 255).Length != 0)) return;
                string? text = null;
                switch (node.Kind)
                {
                    case WidgetKind.Button: break;
                    case WidgetKind.Toggle: if (ev->value is not (0 or 1)) return; break;
                    case WidgetKind.Slider: if (ev->value < node.Minimum || ev->value > node.Maximum) return; break;
                    case WidgetKind.Combo:
                    case WidgetKind.List: if (ev->index < -1 || ev->index >= (node.Items?.Length ?? 0)) return; break;
                    case WidgetKind.TextInput: text = ReadBounded(ev->text, 255); break;
                    default: return;
                }
                try { entry.Fn(entry.Id, new WidgetEvent(node.Id, node.Kind, ev->index, ev->value, text)); }
                catch (Exception ex) { entry.Owner.ReportException("widget " + entry.Id, ex); }
            }
            catch { /* neither decoding nor callbacks may escape into native code */ }
        }
        public ScoResult RemovePanel(string id)
        {
            if (!IsOpen || _t->size < 24 || _t->remove_panel == null) return ScoResult.Unavailable;
            lock (_plugin!.WidgetLock)
            try
            {
                CheckId(id, _plugin);
                ScoResult result;
                fixed (byte* p = Utf8.Z(id)) result = _t->remove_panel(_plugin.SelfPtr, p);
                if (result == ScoResult.Ok && _plugin.WidgetCallbacks.Remove(id, out nint old)) Registry.Remove(old);
                return result;
            }
            catch (InvalidInput e) { return e.Result; }
        }
    }
}
