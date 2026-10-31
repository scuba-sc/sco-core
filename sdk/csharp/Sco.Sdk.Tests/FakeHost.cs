// FakeHost.cs: drives the SDK's wrappers through a stand-in sco_api table, as the host would call
// them: commands, the exception boundary, events, tasks, raw handlers, the service size check, and
// the registry sweep at unload (a late call through an old ctx must find nothing).
//
// Part of the sco SDK. GPL-3.0, like sco-core.
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe partial class FakeHost
    {
        // The Star Citizen extension (GameFakeHost.cs, in sco-sdk-game-sc) adds stand-ins for the game
        // services and the tests of their wrappers. Without it, the core SDK's test runs these alone.
        static partial void GameSetUp();
        static partial void GameTests(Plugin p);
        static partial void GameAfterUnload();
        static partial void GameHas(string name, ref bool ready);
        static partial void GameQueryService(string name, uint minVersion, void** o, ref bool found);

        private static bool GameQuery(string name, uint minVersion, void** o)
        {
            bool found = false;
            GameQueryService(name, minVersion, o, ref found);
            return found;
        }

        // What the stand-in host keeps of each registration (copies, like the real one).
        private sealed class Cmd
        {
            public Cmd(string name, delegate* unmanaged[Cdecl]<ScoArg*, uint, void*, byte*, uint, ScoResult> fn, void* ctx, uint nargs)
            { Name = name; Fn = fn; Ctx = ctx; NArgs = nargs; }
            public readonly string Name;
            public readonly delegate* unmanaged[Cdecl]<ScoArg*, uint, void*, byte*, uint, ScoResult> Fn;
            public readonly void* Ctx;
            public readonly uint NArgs;
        }

        private sealed class Sub
        {
            public Sub(string ev, delegate* unmanaged[Cdecl]<byte*, void*, void*, void> fn, void* ctx) { Event = ev; Fn = fn; Ctx = ctx; }
            public readonly string Event;
            public readonly delegate* unmanaged[Cdecl]<byte*, void*, void*, void> Fn;
            public readonly void* Ctx;
        }

        private sealed class Task
        {
            public Task(delegate* unmanaged[Cdecl]<void*, void> fn, void* ctx) { Fn = fn; Ctx = ctx; }
            public readonly delegate* unmanaged[Cdecl]<void*, void> Fn;
            public readonly void* Ctx;
        }

        private sealed class Raw
        {
            public Raw(string name, delegate* unmanaged[Cdecl]<void*, uint, void*, uint*, void*, ScoResult> fn, void* ctx) { Name = name; Fn = fn; Ctx = ctx; }
            public readonly string Name;
            public readonly delegate* unmanaged[Cdecl]<void*, uint, void*, uint*, void*, ScoResult> Fn;
            public readonly void* Ctx;
        }

        private static readonly List<Cmd> Cmds = new();
        private static readonly List<Sub> Subs = new();
        private static readonly List<Task> Tasks = new();
        private static readonly List<Raw> Raws = new();
        private static readonly List<string> Logs = new();
        private static readonly nint Version = Marshal.StringToCoTaskMemUTF8("fake-host 1.0");   // static, never freed
        private static uint* _smallTable;   // a service table whose size covers only itself
        private static ScoStorageV1* _storageTable;
        private static int _schemaCalls;
        private static int _provideCalls, _releaseCalls, _registerCalls, _invokeCalls;
        private static bool _forwarded, _registerForwarded, _invokeForwarded;
        private static ScoResult _registerResult = ScoResult.Ok;
        private static Raw? _rejectedRaw;
        private static void* _self;
        private static int _failures;

        private static void Expect(bool ok, string what)
        {
            if (ok) return;
            Console.WriteLine("FAIL fake host: " + what);
            ++_failures;
        }

        private static string S(byte* p) => Marshal.PtrToStringUTF8((nint)p) ?? "";

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static byte* HostVersion() => (byte*)Version;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static int Has(byte* cap)
        {
            string name = S(cap);
            bool ready = name == "teleport";
            GameHas(name, ref ready);
            return ready ? 1 : 0;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RunOnGameThread(void* self, delegate* unmanaged[Cdecl]<void*, void> fn, void* ctx)
        {
            Tasks.Add(new Task(fn, ctx));
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Subscribe(void* self, byte* ev, delegate* unmanaged[Cdecl]<byte*, void*, void*, void> fn, void* ctx)
        {
            string e = S(ev);
            foreach (Sub s in Subs) if (s.Event == e && (nint)s.Fn == (nint)fn) return ScoResult.BadArg;   // the host's rule
            Subs.Add(new Sub(e, fn, ctx));
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Unsubscribe(void* self, byte* ev, delegate* unmanaged[Cdecl]<byte*, void*, void*, void> fn)
        {
            string e = S(ev);
            int i = Subs.FindIndex(s => s.Event == e && (nint)s.Fn == (nint)fn);
            if (i < 0) return ScoResult.NotFound;
            Subs.RemoveAt(i);
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void Status(void* self, byte* m) => Logs.Add("status: " + S(m));

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static void Log(void* self, ScoLogLevel level, byte* m) => Logs.Add(level + ": " + S(m));

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RegisterCommand(void* self, ScoCommand* c)
        {
            if (c->size < sizeof(ScoCommand) || c->fn == null || c->arg_def_size != sizeof(ScoArgDef)) return ScoResult.BadArg;
            Cmds.Add(new Cmd(S(c->name), c->fn, c->ctx, c->nargs));
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Invoke(void* self, byte* name, ScoArg* args, uint nargs,
                                        delegate* unmanaged[Cdecl]<ScoResult, byte*, void*, void> done, void* ctx)
        {
            string n = S(name);
            Cmd? c = Cmds.Find(x => x.Name == n);
            byte* reply = stackalloc byte[256];
            reply[0] = 0;
            ScoResult r = c == null ? ScoResult.NotFound : c.Fn(args, nargs, c.Ctx, reply, 256);
            if (done != null) done(r, reply, ctx);
            return r;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static uint ListCommands(ScoCommand** o, uint max) => (uint)Cmds.Count;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ProvideService(void* self, byte* name, uint version, void* table)
        {
            ++_provideCalls;
            _forwarded = self == _self && S(name) == "test.service" && version == 0x10002 && table == _smallTable;
            return ScoResult.TooMany;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult QueryService(byte* name, uint minVersion, void** o)
        {
            *o = null;
            if (S(name) == Abi.StorageName && _storageTable != null)
            {
                *o = _storageTable;
                return ScoResult.Ok;
            }
            if (GameQuery(S(name), minVersion, o)) return ScoResult.Ok;
            if (S(name) != "other.small") return ScoResult.NotFound;
            *o = _smallTable;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult NextTable(void* self, byte* after, byte* name, uint* size, uint* kind)
        {
            ++_schemaCalls;
            if (self != _self) return ScoResult.BadArg;
            if (after != null) return ScoResult.NotFound;
            if (*size < 7) { *size = 7; return ScoResult.TooMany; }
            "visits\0"u8.CopyTo(new Span<byte>(name, 7));
            *size = 7; *kind = Abi.StorageKindTable;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult NextColumn(void* self, byte* table, uint* index, byte* name, uint* nameSize,
                                             byte* type, uint* typeSize, uint* flags)
        {
            ++_schemaCalls;
            if (self != _self) return ScoResult.BadArg;
            if (S(table) != "visits" || *index != 0) return ScoResult.NotFound;
            "place\0"u8.CopyTo(new Span<byte>(name, (int)*nameSize));
            "TEXT\0"u8.CopyTo(new Span<byte>(type, (int)*typeSize));
            *nameSize = 6; *typeSize = 5; *flags = Abi.StorageColumnNotNull; ++*index;
            return ScoResult.Ok;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult ReleaseService(void* self, byte* name)
        {
            ++_releaseCalls;
            _forwarded = self == _self && S(name) == "test.service";
            return ScoResult.NotFound;
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult InvokeRaw(void* self, byte* name, void* i, uint inSize, void* o, uint* outSize)
        {
            ++_invokeCalls;
            _invokeForwarded = self == _self && S(name) == "test.double" && inSize == sizeof(int) &&
                               i != null && new ReadOnlySpan<byte>(i, (int)inSize).SequenceEqual(BitConverter.GetBytes(2)) && o == null && outSize != null && *outSize == 0;
            string n = S(name);
            Raw? r = Raws.Find(x => x.Name == n);
            if (r == null) { if (outSize != null) *outSize = 0; return ScoResult.NotFound; }
            return r.Fn(i, inSize, o, outSize, r.Ctx);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult RegisterRaw(void* self, byte* name, byte* cap, delegate* unmanaged[Cdecl]<void*, uint, void*, uint*, void*, ScoResult> fn, void* ctx)
        {
            ++_registerCalls;
            _registerForwarded = self == _self && S(name) == "test.rejected" && S(cap) == "test.cap" && fn != null && ctx != null;
            var raw = new Raw(S(name), fn, ctx);
            if (_registerResult != ScoResult.Ok) { _rejectedRaw = raw; return _registerResult; }
            Raws.Add(raw);
            return ScoResult.Ok;
        }

        private static ScoApi* MakeApi()
        {
            var a = (ScoApi*)NativeMemory.AllocZeroed((nuint)sizeof(ScoApi));
            a->size = (uint)sizeof(ScoApi);
            a->major = Abi.ApiMajor;
            a->minor = Abi.ApiMinor;
            a->host_version = &HostVersion;
            a->has = &Has;
            a->run_on_game_thread = &RunOnGameThread;
            a->subscribe = &Subscribe;
            a->unsubscribe = &Unsubscribe;
            a->status = &Status;
            a->log = &Log;
            a->register_command = &RegisterCommand;
            a->invoke = &Invoke;
            a->list_commands = &ListCommands;
            a->provide_service = &ProvideService;
            a->query_service = &QueryService;
            a->release_service = &ReleaseService;
            a->invoke_raw = &InvokeRaw;
            a->register_raw = &RegisterRaw;
            return a;
        }

        private static void Dispatch(string ev, uint ms)
        {
            byte[] e = System.Text.Encoding.UTF8.GetBytes(ev + "\0");
            foreach (Sub s in Subs.ToArray())
                if (s.Event == ev)
                    fixed (byte* p = e) s.Fn(p, &ms, s.Ctx);
        }

        // An event whose data is a payload struct, as the game pack dispatches the game.* events.
        private static void DispatchPayload(string ev, void* data)
        {
            byte[] e = System.Text.Encoding.UTF8.GetBytes(ev + "\0");
            foreach (Sub s in Subs.ToArray())
                if (s.Event == ev)
                    fixed (byte* p = e) s.Fn(p, data, s.Ctx);
        }

        private static void RunTasks()
        {
            Task[] now = Tasks.ToArray();
            Tasks.Clear();
            foreach (Task t in now) t.Fn(t.Ctx);
        }

        private static (ScoResult, string) Call(Cmd c, string? arg)
        {
            byte* reply = stackalloc byte[256];
            reply[0] = 0;
            ScoArg a = default;
            byte[] s = System.Text.Encoding.UTF8.GetBytes((arg ?? "") + "\0");
            fixed (byte* sp = s)
            {
                a.type = (uint)ScoArgType.String;
                a.v.s = sp;
                ScoResult r = c.Fn(arg == null ? null : &a, arg == null ? 0u : 1u, c.Ctx, reply, 256);
                return (r, S(reply));
            }
        }

        private sealed class TestPlugin : Plugin
        {
            public int Ticks, Ticks2;
            public Subscription? A, B;

            public override ScoResult OnLoad()
            {
                ScoResult r = new CommandBuilder(this, "test.echo").Title("Echo").Arg("text", ScoArgType.String, "What to echo")
                    .Handle((args, reply) => { reply.Set("echo: " + args.String(0)); return ScoResult.Ok; }).Register();
                if (r != ScoResult.Ok) return r;
                r = new CommandBuilder(this, "test.boom").Handle((args, reply) => throw new InvalidOperationException("boom")).Register();
                if (r != ScoResult.Ok) return r;
                A = Subscribe("tick", data => Ticks += (int)TickMs(data));
                B = Subscribe("tick", () => ++Ticks2);
                if (A.Result != ScoResult.Ok) return A.Result;
                return RegisterRaw<int, int>("test.double", null, (in int x, out int y) => { y = x * 2; return ScoResult.Ok; });
            }
        }

        private sealed class ThrowingPlugin : Plugin
        {
            public override ScoResult OnLoad()
            {
                new CommandBuilder(this, "bad.cmd").Handle((a, r) => ScoResult.Ok).Register();
                throw new InvalidOperationException("no");
            }
        }

        private static ScoResult NoopRaw(ReadOnlySpan<byte> input, Span<byte> output, out uint written)
        { written = 0; return ScoResult.Ok; }

        private static void OptionalRootTests(TestPlugin p, ScoApi* api)
        {
            uint fullSize = api->size;
            uint[] offsets = { ApiOffset.provide_service, ApiOffset.release_service, ApiOffset.register_raw, ApiOffset.invoke_raw };
            int entries = Registry.Count, raws = Raws.Count;
            int calls = _provideCalls + _releaseCalls + _registerCalls + _invokeCalls;
            byte[] output = { 0x5a };
            for (int slot = 0; slot < offsets.Length; ++slot)
            {
                // Every partial pointer extent must be rejected before the field is read.
                for (uint extent = 0; extent < (uint)sizeof(nint); ++extent)
                {
                    api->size = offsets[slot] + extent;
                    uint partialWritten = 42;
                    ScoResult result = slot switch
                    {
                        0 => p.Provide("test.service", 0x10002, _smallTable),
                        1 => p.Release("test.service"),
                        2 => p.RegisterRawBytes("test.refused", null, NoopRaw),
                        _ => p.InvokeRawBytes("test.double", ReadOnlySpan<byte>.Empty, output, out partialWritten),
                    };
                    Expect(result == ScoResult.Unavailable && (slot != 3 || partialWritten == 0),
                           $"partial optional root slot {slot}, extent {extent}");
                }
            }
            api->size = fullSize;
            var provide = api->provide_service; api->provide_service = null;
            Expect(p.Provide("test.service", 0x10002, _smallTable) == ScoResult.Unavailable, "null provide refused");
            api->provide_service = provide;
            var release = api->release_service; api->release_service = null;
            Expect(p.Release("test.service") == ScoResult.Unavailable, "null release refused");
            api->release_service = release;
            var register = api->register_raw; api->register_raw = null;
            Expect(p.RegisterRawBytes("test.refused", null, NoopRaw) == ScoResult.Unavailable, "null register refused");
            api->register_raw = register;
            var invoke = api->invoke_raw; api->invoke_raw = null;
            Expect(p.InvokeRawBytes("test.double", ReadOnlySpan<byte>.Empty, output, out uint written) == ScoResult.Unavailable &&
                   written == 0 && output[0] == 0x5a, "null invoke preserves output and zeroes written");
            api->invoke_raw = invoke;
            Expect(Registry.Count == entries && Raws.Count == raws &&
                   calls == _provideCalls + _releaseCalls + _registerCalls + _invokeCalls && output[0] == 0x5a,
                   "unavailable slots never call host or retain handlers");

            Expect(p.Provide("test.service", 0x10002, _smallTable) == ScoResult.TooMany && _forwarded, "provide forwards arguments and host result");
            Expect(p.Release("test.service") == ScoResult.NotFound && _forwarded, "release forwards arguments and host result");
            _registerResult = ScoResult.Unavailable;
            Expect(p.RegisterRawBytes("test.rejected", "test.cap", NoopRaw) == ScoResult.Unavailable &&
                   _registerForwarded && Registry.Count == entries && Raws.Count == raws, "host rejection forwards arguments and removes registration");
            _registerResult = ScoResult.Ok;
            uint size = 0;
            Expect(_rejectedRaw != null && _rejectedRaw.Fn(null, 0, null, &size, _rejectedRaw.Ctx) == ScoResult.NotFound,
                   "rejected registration callback has no live context");
            Expect(p.RegisterRawBytes("test.forward", null, NoopRaw) == ScoResult.Ok && Registry.Count == entries + 1 &&
                   Raws.Count == raws + 1 && Raws[^1].Name == "test.forward", "register forwards and retains handler on success");
            Expect(p.InvokeRawBytes("test.double", BitConverter.GetBytes(2), Span<byte>.Empty, out written) == ScoResult.TooMany &&
                   written == 4 && _invokeForwarded, "invoke forwards arguments, host result and required size");
        }

        public static int Run()
        {
            _smallTable = (uint*)NativeMemory.AllocZeroed(8);
            *_smallTable = 4;
            ScoApi* api = MakeApi();
            void* self = NativeMemory.AllocZeroed(8);
            _self = self;
            GameSetUp();

            var p = new TestPlugin();
            Expect(PluginExports.LoadPlugin(p, api, self, "test") == ScoResult.Ok, "load");
            Expect(Cmds.Count == 2 && Cmds[0].Name == "test.echo" && Cmds[0].NArgs == 1, "two commands registered");
            Expect(Subs.Count == 1, "one host subscription for two handlers of one event");
            OptionalRootTests(p, api);

            (ScoResult r, string reply) = Call(Cmds[0], "hi");
            Expect(r == ScoResult.Ok && reply == "echo: hi", $"echo answered {r} \"{reply}\"");
            (r, reply) = Call(Cmds[1], null);
            Expect(r == ScoResult.Failed && reply == "test.boom failed: boom", $"a throwing command answered {r} \"{reply}\"");
            Expect(Logs.Exists(l => l.StartsWith("Error: exception in test.boom", StringComparison.Ordinal)), "the exception is logged");

            Expect(p.Invoke("test.echo", out string got, "there") == ScoResult.Ok && got == "echo: there", "Invoke");

            Dispatch("tick", 100);
            Expect(p.Ticks == 100 && p.Ticks2 == 1, "both handlers ran");
            p.B!.Dispose();
            Dispatch("tick", 100);
            Expect(p.Ticks == 200 && p.Ticks2 == 1 && Subs.Count == 1, "a disposed handler doesn't run");
            p.A!.Dispose();
            Expect(Subs.Count == 0, "the last handler's dispose unsubscribes from the host");

            int ran = 0;
            Expect(p.RunOnGameThread(() => ++ran) == ScoResult.Ok, "task queued");
            p.RunOnGameThread(() => throw new InvalidOperationException("task"));
            RunTasks();
            Expect(ran == 1 && Logs.Exists(l => l.Contains("exception in task", StringComparison.Ordinal)), "tasks run; a throwing task is logged");

            Expect(p.InvokeRaw("test.double", 21, out int doubled) == ScoResult.Ok && doubled == 42, "typed raw handler");
            Expect(p.InvokeRawBytes("test.double", BitConverter.GetBytes(1), Span<byte>.Empty, out uint need) == ScoResult.TooMany && need == 4,
                   "raw size handshake");

            Expect(p.Query("other.small", 0x10000, out ServiceRef<ScoStorageV1> small) == ScoResult.Ok && !small.Covers(8),
                   "the size check refuses a function past the provider's size");
            Expect(p.Query("nobody", 0x10000, out ServiceRef<ScoStorageV1> none) == ScoResult.NotFound && !none.IsValid, "query not found");

            ScoStorageV1 storageTable = default;
            storageTable.size = (uint)sizeof(ScoStorageV1);
            storageTable.next_table = &NextTable;
            storageTable.next_column = &NextColumn;
            _storageTable = &storageTable;
            var storage = new Storage();
            Expect(storage.Open(p) == ScoResult.Ok, "storage 1.1 opens");
            Expect(storage.NextTable(null, out string tableName, out uint kind) == ScoResult.Ok &&
                   tableName == "visits" && kind == Abi.StorageKindTable, "storage schema table copied");
            uint columnIndex = 0;
            Expect(storage.NextColumn("visits", ref columnIndex, out string columnName, out string columnType,
                   out uint flags) == ScoResult.Ok && columnIndex == 1 && columnName == "place" &&
                   columnType == "TEXT" && flags == Abi.StorageColumnNotNull, "storage schema column copied");
            Expect(storage.NextColumn("visits", ref columnIndex, out _, out _, out _) == ScoResult.NotFound &&
                   columnIndex == 1, "storage schema end preserves index");
            int schemaCalls = _schemaCalls;
            storageTable.size = 128;
            Expect(storage.NextTable(null, out _, out _) == ScoResult.Unavailable &&
                   storage.NextColumn("visits", ref columnIndex, out _, out _, out _) == ScoResult.Unavailable &&
                   _schemaCalls == schemaCalls, "storage 1.0 extent refuses schema tail before call");
            _storageTable = null;

            GameTests(p);

            Cmd echo = Cmds[0];
            p.RunOnGameThread(() => ++ran);   // still queued at unload: must never run
            PluginExports.UnloadPlugin(p);
            GameAfterUnload();
            Expect(Registry.Count == 0, $"unload swept the registry ({Registry.Count} left)");
            (r, _) = Call(echo, "late");
            Expect(r == ScoResult.NotFound, "a late call through an old ctx finds nothing");
            RunTasks();
            Expect(ran == 1, "a task queued before unload doesn't run after it");

            Cmds.Clear(); Subs.Clear(); Raws.Clear();
            var bad = new ThrowingPlugin();
            Expect(PluginExports.LoadPlugin(bad, api, self, "bad") == ScoResult.Failed, "OnLoad throwing answers Failed");
            Expect(Registry.Count == 0 && !bad.IsLoaded, "a failed load releases what OnLoad registered");
            Expect(Logs.Exists(l => l.StartsWith("Error: exception in OnLoad", StringComparison.Ordinal)), "the OnLoad exception is logged");

            Console.WriteLine($"fake host: {(_failures == 0 ? "OK" : "FAILED")}");
            return _failures;
        }
    }
}
