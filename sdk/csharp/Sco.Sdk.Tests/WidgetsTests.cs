// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe class WidgetsTests
    {
        private sealed class Owner : Plugin { }
        private static ScoWidgetsV1* _table = null;
        private static int _checks, _failed;
        private static string S(byte* p) => Marshal.PtrToStringUTF8((nint)p) ?? "";
        private static void Expect(bool ok, string what)
        {
            ++_checks;
            if (!ok) { ++_failed; Console.WriteLine("FAIL widgets: " + what); }
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Query(byte* name, uint version, void** result)
        {
            *result = null;
            if (S(name) != Abi.WidgetsName || _table == null) return ScoResult.NotFound;
            *result = _table;
            return version == Abi.WidgetsVersion1_0 ? ScoResult.Ok : ScoResult.Unavailable;
        }
        private static string _label = "";
        private static ScoResult _mutation = ScoResult.Ok;
        private static int _thread, _invokes, _logs;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void Log(void* self, ScoLogLevel level, byte* text) { ++_logs; }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Invoke(void* self, byte* name, ScoArg* args, uint count,
            delegate* unmanaged[Cdecl]<ScoResult, byte*, void*, void> done, void* ctx)
        { ++_invokes; if (done != null) { byte zero = 0; done(ScoResult.Ok, &zero, ctx); } return ScoResult.Ok; }
        private static delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void> _fn;
        private static void* _ctx;
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Set(void* self, byte* id, byte* title, ScoWidgetNode* nodes, uint count, uint stride,
            delegate* unmanaged[Cdecl]<byte*, ScoWidgetEvent*, void*, void> fn, void* ctx)
        {
            if (Environment.CurrentManagedThreadId != _thread) return ScoResult.WrongThread;
            if (_mutation != ScoResult.Ok) return _mutation;
            if (self == null || count == 0 || stride != 80 || nodes[0].size != 80) return ScoResult.BadArg;
            _label = S(nodes[0].label);
            _fn = fn == null ? _fn : fn; _ctx = ctx; // keep the trampoline to inject late/stale events
            return ScoResult.Ok;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Remove(void* self, byte* id) => Environment.CurrentManagedThreadId != _thread ? ScoResult.WrongThread : _mutation;
        private static void Send(ScoWidgetEvent ev, void* ctx, string panel = "test.events")
        { fixed (byte* id = Utf8.Z(panel)) _fn(id, &ev, ctx); }
        private sealed class Receiver
        { public void Event(string panel, WidgetEvent ev) { } }
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static WeakReference Root(Widgets w, WidgetNode[] nodes)
        {
            var receiver = new Receiver();
            Expect(w.SetPanel("test.life", "Lifetime", nodes, receiver.Event) == ScoResult.Ok, "lifetime set");
            return new WeakReference(receiver);
        }
        private static void Collect() { GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); }
        public static int Run()
        {
            _thread = Environment.CurrentManagedThreadId;
            foreach ((_, string pin) in WidgetsPins.Lines) Expect(Layout.Check(pin) == null, "layout " + pin);
            ScoApi api = new() { size = (uint)sizeof(ScoApi), major = 1, minor = 1, query_service = &Query, invoke = &Invoke, log = &Log };
            var p = new Owner();
            Expect(PluginExports.LoadPlugin(p, &api, (void*)1, "test") == ScoResult.Ok, "owner load");
            var w = new Widgets();
            for (uint size = 96; size < 104; ++size)
            {
                api.size = size;
                Expect(w.Open(p) == ScoResult.Unavailable, "query API extent " + size);
            }
            api.size = 128; api.query_service = null;
            Expect(w.Open(p) == ScoResult.Unavailable, "null query API");
            api.query_service = &Query;
            Expect(w.Open(p) == ScoResult.NotFound && !w.IsOpen, "missing service");
            ScoWidgetsV1 table = new() { size = 24, set_panel = &Set, remove_panel = &Remove };
            _table = &table;
            Expect(w.Open(p) == ScoResult.Ok, "open widgets");
            WidgetNode[] nodes = { new() { Id = 1, Kind = WidgetKind.Label, Label = "héllo" } };
            Expect(w.SetPanel("test.main", "Example", nodes) == ScoResult.Ok && _label == "héllo", "copied UTF-8 label");
            Expect(w.RemovePanel("test.main") == ScoResult.Ok, "remove panel");
            Expect(w.SetPanel("test.main", "Example", Array.Empty<WidgetNode>()) == ScoResult.BadArg, "empty nodes");
            Expect(w.SetPanel("test.main", "Example", new WidgetNode[257]) == ScoResult.TooMany, "node cap");
            Expect(w.SetPanel("test..bad", "Example", nodes) == ScoResult.BadArg, "ID syntax");
            Expect(w.SetPanel("other.main", "Example", nodes) == ScoResult.BadArg, "owner namespace");
            Expect(w.SetPanel("test.main", "", nodes) == ScoResult.BadArg, "empty title");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Label = "x\0y" } }) == ScoResult.BadArg, "embedded NUL");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Label = "\ud800" } }) == ScoResult.BadArg, "invalid UTF-16");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Label = new string('é', 128) } }) == ScoResult.TooMany, "UTF-8 byte cap");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0], nodes[0] }) == ScoResult.BadArg, "duplicate ID");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Parent = 1 } }) == ScoResult.BadArg, "bad parent");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Value = 1 } }) == ScoResult.BadArg, "unused field");
            Expect(w.SetPanel("test.main", "Example", new[] { nodes[0] with { Kind = WidgetKind.Button, Label = "" } }) == ScoResult.BadArg, "accessible label");
            // Real shortened allocations end inside each method; no full-sized backing object hides a read.
            for (uint size = 4; size < 24; ++size)
            {
                ScoWidgetsV1* shortTable = (ScoWidgetsV1*)NativeMemory.AllocZeroed(size);
                shortTable->size = size;
                if (size >= 16) shortTable->set_panel = &Set;
                _table = shortTable;
                Expect(w.Open(p) == ScoResult.Ok, "short open " + size);
                Expect(w.SetPanel("test.main", "Example", nodes) == (size < 16 ? ScoResult.Unavailable : ScoResult.Ok), "set extent " + size);
                Expect(w.RemovePanel("test.main") == ScoResult.Unavailable, "remove extent " + size);
                NativeMemory.Free(shortTable);
            }
            _table = &table; w.Open(p);
            table.set_panel = null; Expect(w.SetPanel("test.main", "Example", nodes) == ScoResult.Unavailable, "null set"); table.set_panel = &Set;
            table.remove_panel = null; Expect(w.RemovePanel("test.main") == ScoResult.Unavailable, "null remove"); table.remove_panel = &Remove;
            _table = null; Expect(w.Open(p) == ScoResult.NotFound && !w.IsOpen && w.RemovePanel("test.main") == ScoResult.Unavailable, "failed reopen clears table");
            _table = &table; w.Open(p);
            WidgetNode[] all = {
                new() { Id = 1, Kind = WidgetKind.Column }, new() { Id = 2, Kind = WidgetKind.Row, Parent = 1 },
                new() { Id = 3, Kind = WidgetKind.Label, Parent = 2, Label = "Status" },
                new() { Id = 4, Kind = WidgetKind.Button, Parent = 2, Label = "Ping" },
                new() { Id = 5, Kind = WidgetKind.Toggle, Parent = 1, Label = "Toggle", Value = 1 },
                new() { Id = 6, Kind = WidgetKind.Slider, Parent = 1, Label = "Slider", Value = 5, Minimum = 0, Maximum = 10 },
                new() { Id = 7, Kind = WidgetKind.Combo, Parent = 1, Label = "Combo", Index = 0, Items = new[] { "α", "β" } },
                new() { Id = 8, Kind = WidgetKind.List, Parent = 1, Label = "List", Index = -1, Items = new[] { "one" } },
                new() { Id = 9, Kind = WidgetKind.TextInput, Parent = 1, Label = "Text", Text = "initial" },
                new() { Id = 10, Kind = WidgetKind.Table, Parent = 1, Columns = 2, Items = new[] { "a", "b" } }
            };
            var events = new System.Collections.Generic.List<WidgetEvent>();
            Expect(w.SetPanel("test.events", "Events", all, (_, e) => events.Add(e)) == ScoResult.Ok, "all widgets and layouts");
            all[6].Items![0] = "changed"; // caller mutation must not alter callback bounds/data
            void* typedCtx = _ctx;
            Send(new() { size = 32, node_id = 4, kind = 2 }, typedCtx);
            Send(new() { size = 32, node_id = 5, kind = 3, value = 0 }, typedCtx);
            Send(new() { size = 32, node_id = 6, kind = 4, value = 7.5 }, typedCtx);
            Send(new() { size = 32, node_id = 7, kind = 5, index = 1 }, typedCtx);
            Send(new() { size = 32, node_id = 8, kind = 6, index = -1 }, typedCtx);
            fixed (byte* text = Utf8.Z("copied é")) Send(new() { size = 32, node_id = 9, kind = 7, text = text }, typedCtx);
            Expect(events.Count == 6 && events[1].Value == 0 && events[2].Value == 7.5 && events[3].Index == 1 && events[4].Index == -1 && events[5].Text == "copied é", "six typed payloads copied");
            Send(new() { size = 31, node_id = 4, kind = 2 }, typedCtx);
            Send(new() { size = 32, node_id = 6, kind = 4, value = double.NaN }, typedCtx);
            Send(new() { size = 32, node_id = 6, kind = 4, value = 11 }, typedCtx);
            Send(new() { size = 32, node_id = 7, kind = 5, index = 2 }, typedCtx);
            Send(new() { size = 32, node_id = 9, kind = 7 }, typedCtx);
            Send(new() { size = 32, node_id = 4, kind = 3 }, typedCtx);
            Send(new() { size = 32, node_id = 3, kind = 1 }, typedCtx);
            Send(new() { size = 32, node_id = 4, kind = 2 }, typedCtx, "test.wrong");
            Expect(events.Count == 6, "invalid events dropped");
            Expect(w.RemovePanel("test.events") == ScoResult.Ok, "remove typed panel");
            Send(new() { size = 32, node_id = 4, kind = 2 }, typedCtx);
            Expect(events.Count == 6, "stale callback dropped");
            all[0] = all[0] with { Disabled = true };
            Expect(w.SetPanel("test.events", "Events", all, (_, e) => events.Add(e)) == ScoResult.Ok, "disabled layout");
            Send(new() { size = 32, node_id = 4, kind = 2 }, _ctx);
            Expect(events.Count == 6, "disabled ancestor suppresses events");
            Expect(w.RemovePanel("test.events") == ScoResult.Ok, "remove disabled");
            int delivered = 0;
            WidgetNode[] button = { new() { Id = 2, Kind = WidgetKind.Button, Label = "Ping" } };
            Expect(w.SetPanel("test.events", "Events", button, (panel, e) => { if (panel == "test.events" && e.NodeId == 2 && e.Kind == WidgetKind.Button) ++delivered; }) == ScoResult.Ok, "root callback");
            ScoWidgetEvent ev = new() { size = 32, node_id = 2, kind = 2 };
            fixed (byte* panel = Utf8.Z("test.events")) _fn(panel, &ev, _ctx);
            Expect(delivered == 1, "typed button callback");
            void* oldCtx = _ctx;
            WeakReference weak = Root(w, button); Collect(); Expect(weak.IsAlive, "callback strongly rooted");
            _mutation = ScoResult.TooMany;
            Expect(w.SetPanel("test.life", "Lifetime", button, (_, _) => { }) == ScoResult.TooMany, "failed replacement");
            _mutation = ScoResult.NotFound;
            Expect(w.RemovePanel("test.life") == ScoResult.NotFound, "failed removal");
            Collect(); Expect(weak.IsAlive, "failed calls preserve root");
            _mutation = ScoResult.Ok; Expect(w.RemovePanel("test.life") == ScoResult.Ok, "successful removal");
            Collect(); Expect(!weak.IsAlive, "removed callback collected");
            Expect(w.SetPanel("test.events", "Events", button, (_, _) => { ++delivered; p.Invoke("test.command", out _); w.RemovePanel("test.events"); }) == ScoResult.Ok, "reentrant remove set");
            void* removing = _ctx;
            Send(ev, removing); Send(ev, removing);
            Expect(delivered == 2 && _invokes == 1, "invoke/remove reentrancy drops tail");
            Expect(w.SetPanel("test.events", "Events", button, (_, _) => { ++delivered; w.SetPanel("test.events", "Events", button); }) == ScoResult.Ok, "reentrant replace set");
            void* replacing = _ctx;
            Send(ev, replacing); Send(ev, replacing);
            Expect(delivered == 3, "replacement drops stale tail");
            Expect(w.SetPanel("test.events", "Events", button, (_, _) => throw new InvalidOperationException("test")) == ScoResult.Ok, "exception callback set");
            Send(ev, _ctx); Expect(_logs == 1, "callback exception stays managed");
            ScoResult wrong = ScoResult.Ok;
            var thread = new System.Threading.Thread(() => wrong = w.SetPanel("test.events", "Events", button));
            thread.Start(); thread.Join(); Expect(wrong == ScoResult.WrongThread, "host thread affinity propagated");
            _mutation = ScoResult.Unavailable; Expect(w.RemovePanel("test.events") == ScoResult.Unavailable, "host unavailability propagated"); _mutation = ScoResult.Ok;
            Expect(w.SetPanel("test.events", "Events", button, (_, _) => ++delivered) == ScoResult.Ok, "unload pending callback");
            oldCtx = _ctx;
            PluginExports.UnloadPlugin(p);
            Send(ev, oldCtx);
            Expect(delivered == 3 && Registry.Count == 0 && !w.IsOpen, "late callback after unload");
            Expect(PluginExports.LoadPlugin(p, &api, (void*)1, "test") == ScoResult.Ok && !w.IsOpen, "reload invalidates old wrapper");
            PluginExports.UnloadPlugin(p);
            _table = &table;
            var example = new CsWidgets.Demo();
            Expect(PluginExports.LoadPlugin(example, &api, (void*)2, "cs_widgets") == ScoResult.Ok, "archive example load");
            Send(new() { size = 32, node_id = 4, kind = 2 }, _ctx, "cs_widgets.main");
            Expect(example.Events == 1, "archive example event");
            PluginExports.UnloadPlugin(example);
            Expect(Registry.Count == 0, "archive example unload");
            _table = null;
            var absent = new CsWidgets.Demo();
            Expect(PluginExports.LoadPlugin(absent, &api, (void*)2, "cs_widgets") == ScoResult.Ok, "archive example without service");
            PluginExports.UnloadPlugin(absent);
            Console.WriteLine($"widgets: {_checks} checks, {_failed} failed");
            return _failed;
        }
    }
}
