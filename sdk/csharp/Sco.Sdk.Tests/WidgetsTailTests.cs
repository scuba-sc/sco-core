// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe class WidgetsTailTests
    {
        private sealed class Owner : Plugin { }
        private static ScoWidgetsV1* _table;
        private static uint _version = Abi.WidgetsVersion1_4, _features = 0x2ff, _capsCount = 10;
        private static uint _maxTag = 63;
        private static readonly Dictionary<uint, string?> EventTags = new();
        private static bool _taggedMode;
        private static uint _stride, _size, _severity, _first, _visible, _presentation;
        private static string? _tooltip, _tag, _item;
        private static ScoResult _result = ScoResult.Ok;
        private static int _thread, _calls, _checks, _failed;
        private static delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void> _callback;
        private static void* _context;
        private static string S(byte* p) => Marshal.PtrToStringUTF8((nint)p) ?? "";
        private static void Check(bool value, string message)
        { ++_checks; if (!value) { ++_failed; Console.WriteLine("FAIL widgets tails: " + message); } }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Query(byte* name, uint version, void** result)
        {
            *result = null;
            if (S(name) != Abi.WidgetsName || _table == null) return ScoResult.NotFound;
            if (version > _version) return ScoResult.Unavailable;
            *result = _table; return ScoResult.Ok;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Caps(void* self, uint* values)
        {
            if (Environment.CurrentManagedThreadId != _thread) return ScoResult.WrongThread;
            uint capacity = values[0];
            if (capacity < 2) return ScoResult.BadArg;
            uint[] data = { _capsCount, _features, 64, 256, 4096, 256, 16, 255, 256, _maxTag };
            for (int i = 1; i < Math.Min(capacity, Math.Min(_capsCount, 10)); ++i) values[i] = data[i];
            values[0] = _capsCount;
            return ScoResult.Ok;
        }
        private static ScoResult Capture(ScoWidgetNode* nodes, uint count, uint stride)
        {
            ++_calls;
            if (Environment.CurrentManagedThreadId != _thread) return ScoResult.WrongThread;
            if (_result != ScoResult.Ok) return _result;
            if (nodes == null || count == 0) return ScoResult.BadArg;
            _stride = stride; _size = nodes->size;
            _tooltip = _tag = _item = null; _severity = _first = _visible = 0;
            if (nodes->item_count > 0) _item = S(nodes->items[0]);
            if (stride >= 104)
            {
                var tail = (ScoWidgetNode11*)nodes;
                _tooltip = tail->tooltip == null ? null : S(tail->tooltip);
                _severity = tail->severity; _first = tail->first_row; _visible = tail->visible_rows;
                if (tail->_reserved != 0) return ScoResult.BadArg;
            }
            if (stride >= 128)
            {
                var tail = (ScoWidgetNode12*)nodes;
                _tag = tail->tag == null ? null : S(tail->tag);
                for (int i = 0; i < 3; ++i) if (tail->_reserved[i] != 0) return ScoResult.BadArg;
            }
            if (stride >= 144)
            {
                var tail = (ScoWidgetNode13*)nodes;
                _presentation = tail->presentation;
                for (int i = 0; i < 3; ++i) if (tail->_reserved[i] != 0) return ScoResult.BadArg;
            }
            return ScoResult.Ok;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Set(void* self, byte* id, byte* title, ScoWidgetNode* nodes, uint count, uint stride,
            delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void> callback, void* context)
        {
            ScoResult r = Capture(nodes, count, stride);
            if (r == ScoResult.Ok)
            {
                _callback = callback; _context = context; EventTags.Clear(); _taggedMode = false;
                Remember(nodes, count, stride);
            }
            return r;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Update(void* self, byte* id, ScoWidgetNode* nodes, uint count, uint stride)
        {
            ScoResult result = Capture(nodes, count, stride);
            if (result == ScoResult.Ok) Remember(nodes, count, stride);
            return result;
        }
        private static void Remember(ScoWidgetNode* nodes, uint count, uint stride)
        {
            for (uint i = 0; i < count; ++i)
            {
                var node = (ScoWidgetNode*)((byte*)nodes + i * stride);
                byte* source = stride >= 128 ? ((ScoWidgetNode12*)node)->tag : null;
                string? tag = source == null ? null : S(source);
                EventTags[node->id] = tag;
                _taggedMode |= !string.IsNullOrEmpty(tag);
            }
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Remove(void* self, byte* id) => _result;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RemoveNodes(void* self, byte* id, uint* ids, uint count)
        {
            ++_calls;
            if (Environment.CurrentManagedThreadId != _thread) return ScoResult.WrongThread;
            if (_result == ScoResult.Ok) for (uint i = 0; i < count; ++i) EventTags.Remove(ids[i]);
            return _result;
        }
        private static void Send(uint id, uint kind, int row, int index = 0, uint size = 0, void* context = null, double value = 0)
        {
            if (size == 0) size = _taggedMode ? 48u : 40u;
            EventTags.TryGetValue(id, out string? tag);
            ScoWidgetEvent14 ev = new() { @base = new() { @base = new() { size = size, node_id = id, kind = kind, index = index, value = value }, row = row } };
            fixed (byte* source = Utf8.Z(string.IsNullOrEmpty(tag) ? null : tag))
            fixed (byte* panel = Utf8.Z("test.tails"))
            { ev.tag = source; _callback(panel, &ev.@base.@base, context == null ? _context : context); }
        }
        private static void SendTag(byte* tag, uint size = 48, uint id = 2)
        {
            ScoWidgetEvent14 ev = new() { @base = new() { @base = new() { size = size, node_id = id, kind = 2 }, row = -1 }, tag = tag };
            fixed (byte* panel = Utf8.Z("test.tails")) _callback(panel, &ev.@base.@base, _context);
        }
        public static int Run()
        {
            _thread = Environment.CurrentManagedThreadId;
            ScoApi api = new() { size = (uint)sizeof(ScoApi), major = 1, minor = 1, query_service = &Query };
            ScoWidgetsV1 table = new() { size = 48, set_panel = &Set, remove_panel = &Remove,
                update_nodes = &Update, remove_nodes = &RemoveNodes, capabilities = &Caps };
            _table = &table;
            var owner = new Owner();
            Check(PluginExports.LoadPlugin(owner, &api, (void*)1, "test") == ScoResult.Ok, "load");
            var w = new Widgets();
            Check(w.Open(owner) == ScoResult.Ok && w.Version == Abi.WidgetsVersion1_4, "negotiate 1.4");
            Check(w.Capabilities(out var caps) == ScoResult.Ok && caps.Features == 0x2ff && caps.MaxTag == 63 && caps.MaxNodes == 256, "copied capabilities");
            var events = new List<WidgetEvent>();
            WidgetNode list = new() { Id = 2, Kind = WidgetKind.List, Label = "Rows", Index = -1,
                Items = new[] { "α", "β", "γ" }, Tooltip = "Choose é", Severity = 2, FirstRow = 1, VisibleRows = 2, Tag = "Rows.primary-1" };
            Check(w.SetPanel("test.tails", "Tails", new[] { list }, (_, ev) => events.Add(ev)) == ScoResult.Ok &&
                _stride == 128 && _size == 128 && _tooltip == "Choose é" && _tag == list.Tag && _severity == 2 && _first == 1 && _visible == 2 && _item == "α", "1.4 provider uses 1.2 tagged node tail and UTF-8");
            Send(2, 6, 1, 1); Send(2, Abi.WidgetEventPage, 2);
            Check(events.Count == 2 && events[0].Row == 1 && events[1].Kind == WidgetKind.Page && events[1].Row == 2 && events[0].Tag == list.Tag && events[1].Tag == list.Tag, "copied LIST row, PAGE and canonical tags");
            Send(2, 6, 0, 1); Send(2, Abi.WidgetEventPage, 3); Send(2, Abi.WidgetEventPage, -1);
            Send(2, Abi.WidgetEventPage, 1, 1); Send(2, 6, 0, 0, 32); Send(2, Abi.WidgetEventPage, 0, 0, 39);
            Check(events.Count == 2, "malformed/truncated extended events dropped");
            void* saved = _context;
            ScoWidgetEvent* shortEvent = (ScoWidgetEvent*)NativeMemory.AllocZeroed(32);
            shortEvent->size = 32; shortEvent->node_id = 2; shortEvent->kind = 6;
            fixed (byte* panel = Utf8.Z("test.tails")) _callback(panel, shortEvent, saved);
            NativeMemory.Free(shortEvent);
            Check(events.Count == 2, "physically short extended event");
            list.Items![0] = "caller change";
            WidgetNode column = new() { Id = 1, Kind = WidgetKind.Column, Tag = "root" };
            Check(w.SetPanel("test.tails", "Tails", new[] { column, list with { Parent = 1 }, new WidgetNode { Id = 3, Kind = WidgetKind.Button, Parent = 1, Label = "Keep", Tag = "keep" } }, (_, ev) => events.Add(ev)) == ScoResult.Ok, "tree");
            WidgetNode replacement = new() { Id = 2, Parent = 1, Kind = WidgetKind.Button, Label = "Apply", Tooltip = "Updated", Tag = "apply" };
            Check(w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Ok && _stride == 128 && _tag == "apply", "replace subtree");
            Send(2, 2, -1); Send(2, 6, 0, 0);
            Check(events.Count == 3 && events[2].Kind == WidgetKind.Button, "callback validates new kind");
            int calls = _calls;
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Parent = 0 } }) == ScoResult.BadArg, "cannot move existing subtree");
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Tag = "keep" } }) == ScoResult.BadArg, "tag collision with retained nodes");
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Id = 4, Parent = 3 } }) == ScoResult.BadArg, "leaf parent refused");
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Id = 4 }, replacement with { Id = 5 } }) == ScoResult.BadArg, "multiple roots refused");
            Check(w.RemoveNodes("test.tails", new uint[] { 2, 99 }) == ScoResult.NotFound, "atomic absent removal");
            Check(w.RemoveNodes("test.tails", new uint[] { 1 }) == ScoResult.BadArg, "cannot empty panel");
            Check(_calls == calls, "invalid inputs do not reach native mutations");
            _result = ScoResult.TooMany;
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Kind = WidgetKind.Label, Label = "Failure" } }) == ScoResult.TooMany, "failed update propagated");
            Check(w.RemoveNodes("test.tails", new uint[] { 2 }) == ScoResult.TooMany, "failed remove propagated");
            _result = ScoResult.Ok; Send(2, 2, -1);
            Check(events.Count == 4, "failure preserves callback state");
            Check(w.RemoveNodes("test.tails", new uint[] { 2, 2 }) == ScoResult.Ok, "duplicate removal ids follow native contract");
            Send(2, 2, -1); Check(events.Count == 4, "removed node callbacks dropped");
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Id = 4 } }) == ScoResult.Ok, "append new last child");
            Send(4, 2, -1); Check(events.Count == 5, "inserted node routes callback");
            Check(w.UpdateNodes("test.tails", new[] { replacement with { Id = 5, Parent = 0, Tag = "newroot" } }) == ScoResult.Ok, "append new root");
            Check(w.SetPanel("test.quiet", "No callback", new[] { column }) == ScoResult.Ok && w.UpdateNodes("test.quiet", new[] { column with { Tag = "quiet" } }) == ScoResult.Ok, "panels without delegates retain update state");
            int reentrant = 0;
            Check(w.SetPanel("test.tails", "Reentrant", new[] { column, replacement }, (_, _) =>
            {
                ++reentrant;
                Check(w.UpdateNodes("test.tails", new[] { replacement with { Disabled = true } }) == ScoResult.Ok, "update from callback");
            }) == ScoResult.Ok, "reentrant update setup");
            Send(2, 2, -1); Send(2, 2, -1);
            Check(reentrant == 1, "reentrant update changes callback validation atomically");
            Check(w.UpdateNodes("test.absent", new[] { column }) == ScoResult.NotFound, "untracked panel refused");
            foreach (WidgetNode invalid in new[] { list with { Severity = 4 }, list with { Tooltip = "x\0y" }, list with { Tag = "bad tag" }, list with { Tag = new string('a', 64) }, list with { VisibleRows = 0, FirstRow = 1 }, list with { FirstRow = 3 }, list with { Kind = WidgetKind.Page } })
                Check(w.SetPanel("test.invalid", "Invalid", new[] { invalid }) == ScoResult.BadArg, "invalid tail fields");
            Check(w.SetPanel("test.invalid", "Invalid", new[] { list with { VisibleRows = 257 } }) == ScoResult.TooMany, "paging limit");
            Check(w.SetPanel("test.invalid", "Invalid", new[] { list with { Tooltip = new string('é', 128) } }) == ScoResult.TooMany, "tooltip UTF-8 limit");
            _features = 0;
            Check(w.SetPanel("test.invalid", "Invalid", new[] { list }) == ScoResult.Unavailable && w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable && w.RemoveNodes("test.tails", new uint[] { 4 }) == ScoResult.Unavailable, "missing features explicit refusal");
            _features = 0x2ff; _capsCount = 11;
            Check(w.Capabilities(out caps) == ScoResult.BadArg && caps == default, "invalid output count");
            _capsCount = 8; Check(w.Capabilities(out _) == ScoResult.BadArg, "short capabilities result"); _capsCount = 10;
            ScoResult wrong = ScoResult.Ok;
            var thread = new System.Threading.Thread(() => wrong = w.UpdateNodes("test.tails", new[] { replacement }));
            thread.Start(); thread.Join(); Check(wrong == ScoResult.WrongThread, "thread result propagated");
            for (uint size = 4; size < 48; ++size)
            {
                ScoWidgetsV1* shorter = (ScoWidgetsV1*)NativeMemory.AllocZeroed(size); shorter->size = size;
                if (size >= 16) shorter->set_panel = &Set;
                if (size >= 32) shorter->update_nodes = &Update;
                if (size >= 40) shorter->remove_nodes = &RemoveNodes;
                _table = shorter; w.Open(owner);
                calls = _calls;
                Check(w.Capabilities(out _) == ScoResult.Unavailable && w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable &&
                    w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable && w.RemoveNodes("test.tails", new uint[] { 4 }) == ScoResult.Unavailable && _calls == calls, "physically short table refuses every mutation " + size);
                NativeMemory.Free(shorter);
            }
            _table = &table;
            table.capabilities = null; w.Open(owner);
            Check(w.Capabilities(out _) == ScoResult.Unavailable, "null capabilities"); table.capabilities = &Caps;
            table.update_nodes = null; Check(w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable, "null update"); table.update_nodes = &Update;
            table.remove_nodes = null; Check(w.RemoveNodes("test.tails", new uint[] { 4 }) == ScoResult.Unavailable, "null removal"); table.remove_nodes = &RemoveNodes;
            _version = Abi.WidgetsVersion1_1; _capsCount = 9; w.Open(owner);
            Check(w.Version == Abi.WidgetsVersion1_1 && w.SetPanel("test.old", "Old", new[] { list with { Tag = null } }) == ScoResult.Ok && _stride == 104 && w.SetPanel("test.old", "Old", new[] { list }) == ScoResult.Unavailable, "1.1 fallback without silent tag loss");
            _version = Abi.WidgetsVersion1_0; w.Open(owner);
            Check(w.Version == Abi.WidgetsVersion1_0 && w.SetPanel("test.old", "Old", new[] { column with { Tag = null } }) == ScoResult.Ok && _stride == 80 && w.SetPanel("test.old", "Old", new[] { list }) == ScoResult.Unavailable && w.Capabilities(out _) == ScoResult.Unavailable, "1.0 fallback");
            WidgetNode numeric = new() { Id = 1, Kind = WidgetKind.Slider, Label = "Darkness",
                Minimum = 20, Maximum = 95, Value = 74, Presentation = WidgetNumericPresentation.IntegerPercent };
            _version = Abi.WidgetsVersion1_2; _features = 0x1ff; _capsCount = 10; w.Open(owner);
            Check(w.SetPanel("test.tails", "Numeric", new[] { numeric }) == ScoResult.Unavailable, "1.2 refuses 1.3 tail");
            _version = Abi.WidgetsVersion1_3; _features = 0xff; w.Open(owner);
            Check(w.SetPanel("test.tails", "Numeric", new[] { numeric }) == ScoResult.Unavailable, "missing numeric feature refused");
            _features = 0x1ff; int numericEvents = 0;
            Check(w.SetPanel("test.tails", "Numeric", new[] { numeric }, (_, _) => ++numericEvents) == ScoResult.Ok &&
                _stride == 144 && _size == 144 && _presentation == 2, "1.3 numeric tail");
            Send(1, (uint)WidgetKind.Slider, -1, value: 74.5);
            Send(1, (uint)WidgetKind.Slider, -1, value: 74);
            Check(numericEvents == 1, "fractional integer event refused");
            Check(w.SetPanel("test.invalid", "Numeric", new[] { numeric with { Value = 74.5 } }) == ScoResult.BadArg &&
                  w.SetPanel("test.invalid", "Numeric", new[] { numeric with { Presentation = (WidgetNumericPresentation)99 } }) == ScoResult.BadArg,
                "invalid numeric inputs refused");
            foreach (uint oldVersion in new[] { Abi.WidgetsVersion1_2, Abi.WidgetsVersion1_3 })
            {
                _version = oldVersion; _features = 0x3ff; w.Open(owner); calls = _calls;
                Check(w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable &&
                      w.UpdateNodes("test.tails", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable && _calls == calls,
                    "old version refuses tagged set/update without mutation " + oldVersion);
                Check(w.SetPanel("test.tails", "Untagged", new[] { replacement with { Parent = 0, Tag = null } }) == ScoResult.Ok,
                    "old version keeps untagged route " + oldVersion);
            }
            _version = Abi.WidgetsVersion1_4; w.Open(owner);
            foreach (uint features in new[] { 0x1ffu, 0x27fu })
            {
                _features = features; calls = _calls;
                Check(w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable &&
                      w.UpdateNodes("test.tails", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable && _calls == calls,
                    "both tag features required before mutation " + features);
            }
            _features = 0x3ff; _capsCount = 9; calls = _calls;
            Check(w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable && _calls == calls, "missing max-tag capability refuses tagged set");
            _capsCount = 10; _maxTag = 0;
            Check(w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.Unavailable && _calls == calls, "zero max-tag refuses tagged set");
            _maxTag = 2;
            Check(w.SetPanel("test.tails", "Tagged", new[] { replacement with { Parent = 0 } }) == ScoResult.TooMany && _calls == calls, "provider tag bound honored");
            _maxTag = 63;
            WidgetNode tagged = new() { Id = 2, Parent = 1, Kind = WidgetKind.Button, Label = "Tagged", Tag = "canonical.tag" };
            WidgetNode plain = new() { Id = 3, Parent = 1, Kind = WidgetKind.Button, Label = "Plain" };
            WidgetNode root = new() { Id = 1, Kind = WidgetKind.Column };
            var copiedEvents = new List<WidgetEvent>();
            Check(w.SetPanel("test.tails", "Tagged", new[] { root, tagged, plain }, (_, ev) => copiedEvents.Add(ev)) == ScoResult.Ok, "tagged event setup");
            byte[] tagBytes = Utf8.Z(tagged.Tag)!;
            fixed (byte* tag = tagBytes) SendTag(tag);
            tagBytes[0] = (byte)'x';
            Check(copiedEvents.Count == 1 && copiedEvents[0].Tag == "canonical.tag", "callback copies borrowed tag bytes");
            Send(3, 2, -1);
            Check(copiedEvents.Count == 2 && copiedEvents[1].Tag == null, "untagged target in tagged mode has null tag");
            foreach (uint extent in new uint[] { 32, 40, 41, 42, 43, 44, 45, 46, 47 })
            {
                var shortTag = (ScoWidgetEvent*)NativeMemory.AllocZeroed(extent);
                shortTag->size = extent; shortTag->node_id = 2; shortTag->kind = 2;
                if (extent >= 40) ((ScoWidgetEvent11*)shortTag)->row = -1;
                fixed (byte* panel = Utf8.Z("test.tails")) _callback(panel, shortTag, _context);
                NativeMemory.Free(shortTag);
            }
            Check(copiedEvents.Count == 2, "physically short and partial tagged tails never succeed");
            SendTag(null);
            foreach (byte[] badTag in new[] { Utf8.Z("wrong")!, Utf8.Z("")!, Utf8.Z("bad tag")!, Utf8.Z("é")!, Utf8.Z(new string('a', 64))! })
                fixed (byte* tag = badTag) SendTag(tag);
            byte[] unterminated = new byte[64]; Array.Fill(unterminated, (byte)'a');
            fixed (byte* tag = unterminated) SendTag(tag);
            fixed (byte* tag = Utf8.Z("canonical.tag")) SendTag(tag, id: 3);
            Check(copiedEvents.Count == 2, "missing, malformed, overlong and noncanonical event tags dropped");
            Check(w.UpdateNodes("test.tails", new[] { tagged with { Tag = null } }) == ScoResult.Ok, "clear last tag by update");
            Send(2, 2, -1, size: 40); Send(2, 2, -1);
            Check(copiedEvents.Count == 3 && copiedEvents[2].Tag == null, "tagged mode persists after tag-free update");
            Check(w.SetPanel("test.tails", "Untagged", new[] { root, plain }, (_, ev) => copiedEvents.Add(ev)) == ScoResult.Ok, "tag-free full replacement");
            Send(3, 2, -1, size: 40);
            Check(copiedEvents.Count == 4 && copiedEvents[3].Tag == null, "full replacement resets tagged mode");
            Check(w.SetPanel("test.tails", "Tagged", new[] { root, tagged, plain }, (_, ev) => copiedEvents.Add(ev)) == ScoResult.Ok &&
                w.RemoveNodes("test.tails", new uint[] { 2 }) == ScoResult.Ok, "remove last tagged node");
            Send(3, 2, -1, size: 40); Send(3, 2, -1);
            Check(copiedEvents.Count == 5 && copiedEvents[4].Tag == null, "tagged mode persists after last-tag removal");
            Check(w.SetPanel("test.tails", "Tag-free", new[] { root, plain }, (_, ev) => copiedEvents.Add(ev)) == ScoResult.Ok &&
                w.UpdateNodes("test.tails", new[] { plain with { Tag = "added.by.update" } }) == ScoResult.Ok, "first tag added by subtree update");
            Send(3, 2, -1, size: 40); Send(3, 2, -1);
            Check(copiedEvents.Count == 6 && copiedEvents[5].Tag == "added.by.update", "first tagged update requires full event tail");
            string? reentrantTag = null;
            Check(w.SetPanel("test.tails", "Reentrant tag", new[] { root, tagged }, (_, ev) =>
            {
                Check(w.SetPanel("test.tails", "Replacement", new[] { root, plain }) == ScoResult.Ok, "replace inside tagged callback");
                reentrantTag = ev.Tag;
            }) == ScoResult.Ok, "reentrant tag setup");
            var oldCallback = _callback; void* oldContext = _context;
            Send(2, 2, -1);
            Check(reentrantTag == "canonical.tag", "tag copy survives reentrant replacement");
            ScoWidgetEvent14 stale = new() { @base = new() { @base = new() { size = 48, node_id = 2, kind = 2 }, row = -1 } };
            fixed (byte* panel = Utf8.Z("test.tails")) oldCallback(panel, &stale.@base.@base, oldContext);
            Check(reentrantTag == "canonical.tag", "replaced callback context stays inert");
            PluginExports.UnloadPlugin(owner);
            Check(!w.IsOpen && Registry.Count == 0 && w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable, "unload cleans all retained roots");
            Check(PluginExports.LoadPlugin(owner, &api, (void*)1, "test") == ScoResult.Ok && !w.IsOpen, "reload invalidates wrapper");
            PluginExports.UnloadPlugin(owner); _table = null;
            Console.WriteLine($"widgets tails: {_checks} checks, {_failed} failed");
            return _failed;
        }
    }
}
