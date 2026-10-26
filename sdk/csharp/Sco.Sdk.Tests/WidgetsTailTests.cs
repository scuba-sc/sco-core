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
        private static uint _version = Abi.WidgetsVersion1_2, _features = 255, _capsCount = 10;
        private static uint _stride, _size, _severity, _first, _visible;
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
            uint[] data = { _capsCount, _features, 64, 256, 4096, 256, 16, 255, 256, 63 };
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
            return ScoResult.Ok;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Set(void* self, byte* id, byte* title, ScoWidgetNode* nodes, uint count, uint stride,
            delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void> callback, void* context)
        {
            ScoResult r = Capture(nodes, count, stride);
            if (r == ScoResult.Ok) { _callback = callback; _context = context; }
            return r;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Update(void* self, byte* id, ScoWidgetNode* nodes, uint count, uint stride) => Capture(nodes, count, stride);
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Remove(void* self, byte* id) => _result;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RemoveNodes(void* self, byte* id, uint* ids, uint count)
        { ++_calls; return Environment.CurrentManagedThreadId != _thread ? ScoResult.WrongThread : _result; }
        private static void Send(uint id, uint kind, int row, int index = 0, uint size = 40, void* context = null)
        {
            ScoWidgetEvent11 ev = new() { @base = new() { size = size, node_id = id, kind = kind, index = index }, row = row };
            fixed (byte* panel = Utf8.Z("test.tails")) _callback(panel, &ev.@base, context == null ? _context : context);
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
            Check(w.Open(owner) == ScoResult.Ok && w.Version == Abi.WidgetsVersion1_2, "negotiate 1.2");
            Check(w.Capabilities(out var caps) == ScoResult.Ok && caps.Features == 255 && caps.MaxTag == 63 && caps.MaxNodes == 256, "copied capabilities");
            var events = new List<WidgetEvent>();
            WidgetNode list = new() { Id = 2, Kind = WidgetKind.List, Label = "Rows", Index = -1,
                Items = new[] { "α", "β", "γ" }, Tooltip = "Choose é", Severity = 2, FirstRow = 1, VisibleRows = 2, Tag = "Rows.primary-1" };
            Check(w.SetPanel("test.tails", "Tails", new[] { list }, (_, ev) => events.Add(ev)) == ScoResult.Ok &&
                _stride == 128 && _size == 128 && _tooltip == "Choose é" && _tag == list.Tag && _severity == 2 && _first == 1 && _visible == 2 && _item == "α", "1.2 marshalled tail and UTF-8");
            Send(2, 6, 1, 1); Send(2, Abi.WidgetEventPage, 2);
            Check(events.Count == 2 && events[0].Row == 1 && events[1].Kind == WidgetKind.Page && events[1].Row == 2, "copied LIST row and PAGE");
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
            _features = 255; _capsCount = 11;
            Check(w.Capabilities(out caps) == ScoResult.BadArg && caps == default, "invalid output count");
            _capsCount = 8; Check(w.Capabilities(out _) == ScoResult.BadArg, "short capabilities result"); _capsCount = 10;
            ScoResult wrong = ScoResult.Ok;
            var thread = new System.Threading.Thread(() => wrong = w.UpdateNodes("test.tails", new[] { replacement }));
            thread.Start(); thread.Join(); Check(wrong == ScoResult.WrongThread, "thread result propagated");
            for (uint size = 4; size < 48; ++size)
            {
                ScoWidgetsV1* shorter = (ScoWidgetsV1*)NativeMemory.AllocZeroed(size); shorter->size = size;
                if (size >= 32) shorter->update_nodes = &Update;
                if (size >= 40) shorter->remove_nodes = &RemoveNodes;
                _table = shorter; w.Open(owner);
                Check(w.Capabilities(out _) == ScoResult.Unavailable && w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable && w.RemoveNodes("test.tails", new uint[] { 4 }) == ScoResult.Unavailable, "physically short table " + size);
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
            PluginExports.UnloadPlugin(owner);
            Check(!w.IsOpen && Registry.Count == 0 && w.UpdateNodes("test.tails", new[] { replacement }) == ScoResult.Unavailable, "unload cleans all retained roots");
            Check(PluginExports.LoadPlugin(owner, &api, (void*)1, "test") == ScoResult.Ok && !w.IsOpen, "reload invalidates wrapper");
            PluginExports.UnloadPlugin(owner); _table = null;
            Console.WriteLine($"widgets tails: {_checks} checks, {_failed} failed");
            return _failed;
        }
    }
}
