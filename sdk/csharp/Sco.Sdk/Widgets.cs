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
        public string? Tooltip { get; init; }
        public uint Severity { get; init; }
        public uint FirstRow { get; init; }
        public uint VisibleRows { get; init; }
        public string? Tag { get; init; }
        public WidgetNumericPresentation Presentation { get; init; }
        internal bool Extended { get; init; }
    }

    /// <summary>Copied typed event; unused fields are zero/null. Text remains valid after return.</summary>
    public readonly record struct WidgetEvent(uint NodeId, WidgetKind Kind, int Index, double Value, string? Text)
    {
        public int Row { get; init; } = -1;
    }

    /// <summary>Copied provider limits and feature bits. These do not change the SDK's bounded input limits.</summary>
    public readonly record struct WidgetCapabilities(uint Features, uint MaxPanels, uint MaxNodes,
        uint MaxTotalNodes, uint MaxItems, uint MaxColumns, uint MaxText, uint MaxEvents, uint MaxTag);

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
        public uint Version { get; private set; }
        public bool IsOpen => _t != null && _plugin != null && _plugin.IsLoaded && _epoch == _plugin.WidgetEpoch;
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null;
            _plugin = null;
            Version = 0;
            if (!plugin.IsLoaded) return ScoResult.BadArg;
            ScoApi* api = plugin.Api;
            if (api == null || api->size < ApiOffset.query_service + 8 || api->query_service == null) return ScoResult.Unavailable;
            uint version = Abi.WidgetsVersion1_3;
            ScoResult r = plugin.Query(Abi.WidgetsName, version, out ServiceRef<ScoWidgetsV1> s);
            if (r is ScoResult.Unavailable or ScoResult.NotFound)
            {
                version = Abi.WidgetsVersion1_2;
                r = plugin.Query(Abi.WidgetsName, version, out s);
            }
            if (r is ScoResult.Unavailable or ScoResult.NotFound)
            {
                version = Abi.WidgetsVersion1_1;
                r = plugin.Query(Abi.WidgetsName, version, out s);
            }
            if (r is ScoResult.Unavailable or ScoResult.NotFound)
            {
                version = Abi.WidgetsVersion1_0;
                r = plugin.Query(Abi.WidgetsName, version, out s);
            }
            if (r != ScoResult.Ok) return r;
            _plugin = plugin;
            _epoch = plugin.WidgetEpoch;
            _t = s.Table;
            Version = version;
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
            var tags = new HashSet<string>(StringComparer.Ordinal);
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
                Require(n.Presentation is >= WidgetNumericPresentation.Number and <= WidgetNumericPresentation.IntegerPercent);
                Require(n.Kind == WidgetKind.Slider || n.Presentation == WidgetNumericPresentation.Number);
                if (n.Presentation != WidgetNumericPresentation.Number)
                    foreach (double v in new[] { n.Minimum, n.Maximum, n.Value })
                        Require(v >= int.MinValue && v <= int.MaxValue && Math.Truncate(v) == v);
                int count = n.Items?.Length ?? 0;
                Require(count <= Abi.WidgetsMaxItems && n.Columns <= Abi.WidgetsMaxColumns, ScoResult.TooMany);
                Require(!selection || (n.Index >= -1 && n.Index < count));
                Require(n.Kind != WidgetKind.Table || (n.Columns > 0 && count % n.Columns == 0));
                CheckText(n.Label, 255, true, ScoResult.TooMany);
                Require(n.Kind is < WidgetKind.Button or > WidgetKind.TextInput || !string.IsNullOrEmpty(n.Label));
                CheckText(n.Text, 255, true, ScoResult.TooMany);
                CheckText(n.Tooltip, 255, true, ScoResult.TooMany);
                Require(n.Severity <= Abi.WidgetSeverityError);
                Require(n.Kind is WidgetKind.List or WidgetKind.Table || (n.FirstRow == 0 && n.VisibleRows == 0));
                Require(n.VisibleRows != 0 || n.FirstRow == 0);
                Require(n.VisibleRows <= Abi.WidgetsMaxItems, ScoResult.TooMany);
                uint rows = n.Kind == WidgetKind.Table && n.Columns > 0 ? (uint)count / n.Columns : (uint)count;
                Require(n.FirstRow == 0 || n.FirstRow < rows);
                if (!string.IsNullOrEmpty(n.Tag))
                {
                    Require(n.Tag.Length <= Abi.WidgetsMaxTag);
                    foreach (char c in n.Tag) Require(c is >= 'A' and <= 'Z' or >= 'a' and <= 'z' or >= '0' and <= '9' or '.' or '_' or '-');
                    Require(tags.Add(n.Tag));
                }
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
        public ScoResult Capabilities(out WidgetCapabilities capabilities)
        {
            capabilities = default;
            if (!IsOpen || Version < Abi.WidgetsVersion1_1 || _t->size < 48 || _t->capabilities == null)
                return ScoResult.Unavailable;
            uint* values = stackalloc uint[10];
            new Span<uint>(values, 10).Clear();
            values[0] = 10;
            ScoResult result = _t->capabilities(_plugin!.SelfPtr, values);
            if (result != ScoResult.Ok) return result;
            if (values[0] < 9 || values[0] > 10) return ScoResult.BadArg;
            capabilities = new(values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8],
                values[0] == 10 ? values[9] : 0);
            return ScoResult.Ok;
        }
        private uint WireFormat(WidgetNode[] nodes)
        {
            uint version = Abi.WidgetsVersion1_0, features = 0;
            foreach (WidgetNode n in nodes)
            {
                if (n.Tooltip != null) features |= Abi.WidgetsFeatureTooltip;
                if (n.Severity != 0) features |= Abi.WidgetsFeatureSeverity;
                if (n.VisibleRows != 0) features |= Abi.WidgetsFeaturePaging | Abi.WidgetsFeatureEventRow;
                if (!string.IsNullOrEmpty(n.Tag)) { features |= Abi.WidgetsFeatureStableTags; version = Math.Max(version, Abi.WidgetsVersion1_2); }
                if (n.Presentation != WidgetNumericPresentation.Number) { features |= Abi.WidgetsFeatureNumericPresentation; version = Abi.WidgetsVersion1_3; }
            }
            if (features != 0 && version < Abi.WidgetsVersion1_1) version = Abi.WidgetsVersion1_1;
            if (features != 0)
            {
                Require(Version >= version, ScoResult.Unavailable);
                ScoResult r = Capabilities(out WidgetCapabilities caps);
                Require(r == ScoResult.Ok, r);
                Require((caps.Features & features) == features, ScoResult.Unavailable);
                if ((features & Abi.WidgetsFeatureStableTags) != 0)
                    foreach (WidgetNode n in nodes) Require((uint)(n.Tag?.Length ?? 0) <= caps.MaxTag, ScoResult.TooMany);
            }
            return version;
        }
        private static ScoWidgetNode* MarshalNodes(WidgetNode[] nodes, uint version, NativeArena arena, out uint stride)
        {
            stride = version >= Abi.WidgetsVersion1_3 ? (uint)sizeof(ScoWidgetNode13) :
                version >= Abi.WidgetsVersion1_2 ? (uint)sizeof(ScoWidgetNode12) :
                version >= Abi.WidgetsVersion1_1 ? (uint)sizeof(ScoWidgetNode11) : (uint)sizeof(ScoWidgetNode);
            byte* memory = (byte*)arena.Alloc((nuint)(nodes.Length * stride));
            new Span<byte>(memory, checked((int)(nodes.Length * stride))).Clear();
            for (int i = 0; i < nodes.Length; ++i)
            {
                WidgetNode n = nodes[i];
                var native = (ScoWidgetNode*)(memory + i * stride);
                *native = new ScoWidgetNode { size = stride, kind = (uint)n.Kind, id = n.Id, parent = n.Parent,
                    flags = n.Disabled ? Abi.WidgetDisabled : 0, index = n.Index, label = arena.Add(n.Label),
                    text = arena.Add(n.Text), value = n.Value, minimum = n.Minimum, maximum = n.Maximum, columns = n.Columns };
                if (n.Items != null && n.Items.Length > 0)
                {
                    native->item_count = (uint)n.Items.Length;
                    native->items = (byte**)arena.Alloc((nuint)n.Items.Length * (nuint)sizeof(byte*));
                    for (int j = 0; j < n.Items.Length; ++j) native->items[j] = arena.Add(n.Items[j]);
                }
                if (stride >= sizeof(ScoWidgetNode11))
                {
                    var tail = (ScoWidgetNode11*)native;
                    tail->tooltip = arena.Add(n.Tooltip); tail->severity = n.Severity;
                    tail->first_row = n.FirstRow; tail->visible_rows = n.VisibleRows;
                }
                if (stride >= sizeof(ScoWidgetNode12)) ((ScoWidgetNode12*)native)->tag = arena.Add(n.Tag);
                if (stride >= sizeof(ScoWidgetNode13)) ((ScoWidgetNode13*)native)->presentation = (uint)n.Presentation;
                nodes[i] = n with { Extended = stride >= sizeof(ScoWidgetNode11) };
            }
            return (ScoWidgetNode*)memory;
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
                uint version = WireFormat(copied);
                Require(_plugin.WidgetCallbacks.ContainsKey(id) || _plugin.WidgetCallbacks.Count < Abi.WidgetsMaxPanels, ScoResult.TooMany);
                _plugin.WidgetCallbacks.EnsureCapacity(_plugin.WidgetCallbacks.Count + 1);
                ScoWidgetNode* native = MarshalNodes(copied, version, arena, out uint stride);
                context = Registry.Add(new CallbackEntry(_plugin, id, copied, callback));
                ScoResult result = _t->set_panel(_plugin.SelfPtr, arena.Add(id), arena.Add(title), native,
                    (uint)copied.Length, stride, callback == null ? null : &EventTrampoline, (void*)context);
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
            public CallbackEntry(Plugin p, string id, WidgetNode[] nodes, Action<string, WidgetEvent>? fn) : base(p)
            { Id = id; Nodes = nodes; Fn = fn; }
            public string Id { get; }
            public WidgetNode[] Nodes { get; set; }
            public Action<string, WidgetEvent>? Fn { get; }
        }
        private CallbackEntry Panel(string id)
        {
            CheckId(id, _plugin!);
            Require(_plugin!.WidgetCallbacks.TryGetValue(id, out nint context), ScoResult.NotFound);
            CallbackEntry? entry = Registry.Get<CallbackEntry>((void*)context);
            Require(entry != null, ScoResult.NotFound);
            return entry!;
        }
        private void RequireFeature(uint feature)
        {
            ScoResult r = Capabilities(out WidgetCapabilities caps);
            Require(r == ScoResult.Ok, r);
            Require((caps.Features & feature) == feature, ScoResult.Unavailable);
        }
        private static int SubtreeEnd(WidgetNode[] tree, int begin)
        {
            var descendants = new HashSet<uint> { tree[begin].Id };
            int end = begin + 1;
            while (end < tree.Length && descendants.Contains(tree[end].Parent)) descendants.Add(tree[end++].Id);
            return end;
        }
        /// <summary>Atomically replace one subtree, or append a new subtree under its declared parent.
        /// Panels must have been submitted through this owner's typed wrapper.</summary>
        public ScoResult UpdateNodes(string id, ReadOnlySpan<WidgetNode> nodes)
        {
            if (!IsOpen || Version < Abi.WidgetsVersion1_1 || _t->size < 32 || _t->update_nodes == null)
                return ScoResult.Unavailable;
            var arena = new NativeArena();
            lock (_plugin!.WidgetLock)
            try
            {
                RequireFeature(Abi.WidgetsFeatureUpdateNodes);
                CallbackEntry entry = Panel(id);
                Require(nodes.Length > 0);
                Require(nodes.Length <= Abi.WidgetsMaxNodes, ScoResult.TooMany);
                uint parent = nodes[0].Parent;
                WidgetNode[] fragment = nodes.ToArray();
                fragment[0] = fragment[0] with { Parent = 0 };
                for (int i = 1; i < fragment.Length; ++i) Require(fragment[i].Parent != 0);
                fragment = CopyNodes(fragment);
                fragment[0] = fragment[0] with { Parent = parent };
                uint version = WireFormat(fragment);
                ScoWidgetNode* native = MarshalNodes(fragment, version, arena, out uint stride);
                WidgetNode[] tree = entry.Nodes;
                int begin = Array.FindIndex(tree, n => n.Id == fragment[0].Id), end;
                if (begin >= 0)
                {
                    Require(tree[begin].Parent == parent);
                    end = SubtreeEnd(tree, begin);
                }
                else if (parent != 0)
                {
                    int at = Array.FindIndex(tree, n => n.Id == parent);
                    Require(at >= 0 && tree[at].Kind is WidgetKind.Row or WidgetKind.Column);
                    begin = end = SubtreeEnd(tree, at);
                }
                else begin = end = tree.Length;
                int total = tree.Length - (end - begin) + fragment.Length;
                Require(total <= Abi.WidgetsMaxNodes, ScoResult.TooMany);
                var merged = new WidgetNode[total];
                Array.Copy(tree, 0, merged, 0, begin);
                Array.Copy(fragment, 0, merged, begin, fragment.Length);
                Array.Copy(tree, end, merged, begin + fragment.Length, tree.Length - end);
                merged = CopyNodes(merged);
                ScoResult result = _t->update_nodes(_plugin.SelfPtr, arena.Add(id), native, (uint)fragment.Length, stride);
                if (result == ScoResult.Ok) entry.Nodes = merged;
                return result;
            }
            catch (InvalidInput e) { return e.Result; }
            catch (EncoderFallbackException) { return ScoResult.BadArg; }
            catch (OutOfMemoryException) { return ScoResult.TooMany; }
            finally { arena.FreeAll(); }
        }
        /// <summary>Atomically remove the named subtrees. At least one node must remain.</summary>
        public ScoResult RemoveNodes(string id, ReadOnlySpan<uint> ids)
        {
            if (!IsOpen || Version < Abi.WidgetsVersion1_1 || _t->size < 40 || _t->remove_nodes == null)
                return ScoResult.Unavailable;
            lock (_plugin!.WidgetLock)
            try
            {
                RequireFeature(Abi.WidgetsFeatureRemoveNodes);
                CallbackEntry entry = Panel(id);
                Require(ids.Length > 0);
                Require(ids.Length <= Abi.WidgetsMaxNodes, ScoResult.TooMany);
                WidgetNode[] tree = entry.Nodes;
                var drop = new bool[tree.Length];
                foreach (uint node in ids) Require(node != 0);
                foreach (uint node in ids)
                {
                    int begin = Array.FindIndex(tree, n => n.Id == node);
                    Require(begin >= 0, ScoResult.NotFound);
                    int end = SubtreeEnd(tree, begin);
                    for (int i = begin; i < end; ++i) drop[i] = true;
                }
                var kept = new List<WidgetNode>(tree.Length);
                for (int i = 0; i < tree.Length; ++i) if (!drop[i]) kept.Add(tree[i]);
                Require(kept.Count != 0);
                WidgetNode[] merged = kept.ToArray();
                ScoResult result;
                fixed (byte* panel = Utf8.Z(id))
                fixed (uint* values = ids) result = _t->remove_nodes(_plugin.SelfPtr, panel, values, (uint)ids.Length);
                if (result == ScoResult.Ok) entry.Nodes = merged;
                return result;
            }
            catch (InvalidInput e) { return e.Result; }
            catch (OutOfMemoryException) { return ScoResult.TooMany; }
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
                if (entry == null || entry.Fn == null || !entry.Owner.IsLoaded || ev == null || ev->size < sizeof(ScoWidgetEvent)) return;
                if (ReadBounded(panel, 63) != entry.Id) return;
                WidgetNode node = default;
                bool enabled = true;
                foreach (WidgetNode n in entry.Nodes) if (n.Id == ev->node_id) { node = n; break; }
                bool page = ev->kind == Abi.WidgetEventPage;
                if (node.Id == 0 || (!page && (uint)node.Kind != ev->kind)) return;
                bool extended = Array.Exists(entry.Nodes, n => n.Extended);
                if (extended && ev->size < sizeof(ScoWidgetEvent11)) return;
                int row = -1;
                if (ev->size >= sizeof(ScoWidgetEvent11))
                {
                    var tail = (ScoWidgetEvent11*)ev;
                    if (tail->_reserved != 0) return;
                    row = tail->row;
                    if (!page && row != (node.Kind == WidgetKind.List ? ev->index : -1)) return;
                }
                for (WidgetNode n = node; n.Id != 0;)
                {
                    if (n.Disabled) enabled = false;
                    uint parent = n.Parent;
                    n = default;
                    if (parent != 0) foreach (WidgetNode candidate in entry.Nodes) if (candidate.Id == parent) { n = candidate; break; }
                }
                if (!enabled || !double.IsFinite(ev->value)) return;
                if (page)
                {
                    uint rows = node.Kind == WidgetKind.Table && node.Columns > 0 ? (uint)(node.Items?.Length ?? 0) / node.Columns : (uint)(node.Items?.Length ?? 0);
                    if (!extended || node.Kind is not (WidgetKind.List or WidgetKind.Table) || node.VisibleRows == 0 ||
                        row < 0 || (uint)row >= rows || ev->index != 0 || ev->value != 0 || ev->text != null) return;
                    try { entry.Fn(entry.Id, new WidgetEvent(node.Id, WidgetKind.Page, 0, 0, null) { Row = row }); }
                    catch (Exception ex) { entry.Owner.ReportException("widget " + entry.Id, ex); }
                    return;
                }
                bool numeric = node.Kind is WidgetKind.Toggle or WidgetKind.Slider;
                bool selection = node.Kind is WidgetKind.Combo or WidgetKind.List;
                if ((!numeric && ev->value != 0) || (!selection && ev->index != 0) ||
                    (node.Kind != WidgetKind.TextInput && ev->text != null && ReadBounded(ev->text, 255).Length != 0)) return;
                string? text = null;
                switch (node.Kind)
                {
                    case WidgetKind.Button: break;
                    case WidgetKind.Toggle: if (ev->value is not (0 or 1)) return; break;
                    case WidgetKind.Slider:
                        if (ev->value < node.Minimum || ev->value > node.Maximum ||
                            (node.Presentation != WidgetNumericPresentation.Number && Math.Truncate(ev->value) != ev->value)) return;
                        break;
                    case WidgetKind.Combo:
                    case WidgetKind.List: if (ev->index < -1 || ev->index >= (node.Items?.Length ?? 0)) return; break;
                    case WidgetKind.TextInput: text = ReadBounded(ev->text, 255); break;
                    default: return;
                }
                try { entry.Fn(entry.Id, new WidgetEvent(node.Id, node.Kind, ev->index, ev->value, text) { Row = row }); }
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
