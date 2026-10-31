// SPDX-License-Identifier: GPL-3.0-only
// Synthetic sized-string schema provider, including changing and malformed replies.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe class StorageSchemaTests
    {
        private sealed class Owner : Plugin { }
        private enum Mode { Normal, Grow, NoProgress, TooLarge, BadZero, BadOver, MissingNul, EarlyNul, BadUtf8, NameBad, IndexBad, IndexJump, HostFailure, FailAfterGrowth, Forever }
        private static ScoStorageV1* _table;
        private static byte[] _name = Array.Empty<byte>(), _type = Array.Empty<byte>();
        private static Mode _mode;
        private static int _calls, _checks, _failed;
        private static bool _arguments;
        private static uint _wanted;
        private static string? _after;
        private static string _inputTable = "schema";
        private static string S(byte* p) => Marshal.PtrToStringUTF8((nint)p) ?? "";
        private static void Check(bool ok, string what)
        { ++_checks; if (!ok) { ++_failed; Console.WriteLine("FAIL storage schema: " + what); } }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Query(byte* name, uint version, void** result)
        {
            *result = _table;
            return S(name) == Abi.StorageName && version == Abi.StorageVersion1_0 && _table != null ? ScoResult.Ok : ScoResult.NotFound;
        }

        private static ScoResult Copy(byte[] source, byte* output, uint* size)
        {
            uint cap = *size, need = (uint)source.Length + 1;
            *size = need;
            if (cap < need) return ScoResult.TooMany;
            source.AsSpan().CopyTo(new Span<byte>(output, source.Length));
            output[source.Length] = 0;
            return ScoResult.Ok;
        }

        private static ScoResult Malformed(byte* output, uint* size)
        {
            uint cap = *size;
            switch (_mode)
            {
                case Mode.NoProgress: return ScoResult.TooMany;
                case Mode.TooLarge: *size = (16u << 20) + 2; return ScoResult.TooMany;
                case Mode.Forever: *size = cap + 1; return ScoResult.TooMany;
                case Mode.BadZero: *size = 0; return ScoResult.Ok;
                case Mode.BadOver: *size = cap + 1; return ScoResult.Ok;
                case Mode.MissingNul: *size = 1; return ScoResult.Ok;
                case Mode.EarlyNul: output[0] = 0; output[1] = 0; *size = 2; return ScoResult.Ok;
                case Mode.BadUtf8: output[0] = 0xc3; output[1] = 0; *size = 2; return ScoResult.Ok;
                case Mode.HostFailure: return ScoResult.WrongThread;
                default: return ScoResult.Ok;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult NextTable(void* self, byte* after, byte* name, uint* size, uint* kind)
        {
            ++_calls;
            _arguments &= self == (void*)1 && (after == null ? _after == null : S(after) == _after);
            *kind = 23;
            if (_mode == Mode.FailAfterGrowth)
            {
                if (_calls == 1) { *size = 300; return ScoResult.TooMany; }
                return ScoResult.WrongThread;
            }
            if (_mode != Mode.Normal && _mode != Mode.Grow) return Malformed(name, size);
            if (_mode == Mode.Grow && _calls == 1) { *size = 300; return ScoResult.TooMany; }
            return Copy(_name, name, size);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult NextColumn(void* self, byte* table, uint* index, byte* name, uint* ns,
                                             byte* type, uint* ts, uint* flags)
        {
            ++_calls;
            _arguments &= self == (void*)1 && table != null && S(table) == _inputTable && *index == _wanted;
            *index = 999; *flags = 31; // Even broken providers cannot leak these on failure.
            if (_mode == Mode.FailAfterGrowth)
            {
                if (_calls == 1) { *ns = 300; return ScoResult.TooMany; }
                return ScoResult.WrongThread;
            }
            if (_mode == Mode.Grow && _calls == 1) { *ns = 300; return ScoResult.TooMany; }
            if (_mode == Mode.IndexBad || _mode == Mode.IndexJump)
            {
                ScoResult r = Copy(_name, name, ns);
                if (r != ScoResult.Ok) return r;
                r = Copy(_type, type, ts);
                *index = _mode == Mode.IndexJump ? _wanted + 2 : _wanted;
                return r;
            }
            if (_mode == Mode.NameBad)
            {
                *ns = 0;
                return Copy(_type, type, ts);
            }
            if (_mode != Mode.Normal && _mode != Mode.Grow)
            {
                ScoResult r = Copy(_name, name, ns);
                return r == ScoResult.Ok ? Malformed(type, ts) : r;
            }
            ScoResult first = Copy(_name, name, ns);
            if (first != ScoResult.Ok) return first;
            ScoResult second = Copy(_type, type, ts);
            if (second == ScoResult.Ok) *index = _wanted + 1;
            return second;
        }

        private static void Reset(Mode mode, string name = "column", string type = "TEXT")
        {
            _mode = mode; _name = Encoding.UTF8.GetBytes(name); _type = Encoding.UTF8.GetBytes(type);
            _calls = 0; _arguments = true; _wanted = 7; _after = "cursor"; _inputTable = "schema";
        }

        public static int Run()
        {
            ScoApi api = new() { size = (uint)sizeof(ScoApi), major = 1, minor = 1, query_service = &Query };
            ScoStorageV1 table = new() { size = (uint)sizeof(ScoStorageV1), next_table = &NextTable, next_column = &NextColumn };
            _table = &table;
            var p = new Owner();
            Check(PluginExports.LoadPlugin(p, &api, (void*)1, "test") == ScoResult.Ok, "owner load");
            var storage = new Storage();
            Check(storage.Open(p) == ScoResult.Ok, "open storage");

            string longName = new string('a', 900) + "é航", longType = new string('b', 1800) + "界";
            Reset(Mode.Grow, longName, longType);
            Check(storage.NextTable("cursor", out string got, out uint kind) == ScoResult.Ok && got == longName &&
                  kind == 23 && _calls == 3 && _arguments, "table grows between retries and decodes UTF-8");
            Reset(Mode.Grow, longName, longType);
            uint index = _wanted;
            Check(storage.NextColumn("schema", ref index, out string name, out string type, out uint flags) == ScoResult.Ok &&
                  index == 8 && name == longName && type == longType && flags == 31 && _calls == 4 && _arguments,
                  "changing schema and sequential growth retry the original index");
            Reset(Mode.Normal, longName, "int"); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Ok &&
                  name == longName && type == "int" && _calls == 2 && _arguments, "long name with short type");
            Reset(Mode.Normal, "id", longType); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Ok &&
                  name == "id" && type == longType && _calls == 2 && _arguments, "short name with long type");
            Reset(Mode.Normal, "名", ""); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Ok &&
                  name == "名" && type == "" && index == 8, "UTF-8 and empty type with one-byte NUL");
            Reset(Mode.Normal); _after = null;
            Check(storage.NextTable(null, out got, out kind) == ScoResult.Ok && got == "column" && _arguments, "null starting cursor");
            Reset(Mode.Normal); _after = "";
            Check(storage.NextTable("", out got, out kind) == ScoResult.Ok && got == "column" && _arguments,
                  "empty cursor remains a non-null empty string");
            Reset(Mode.Grow, longName); _after = "";
            Check(storage.NextTable("", out got, out kind) == ScoResult.Ok && got == longName && _calls == 3 && _arguments,
                  "empty cursor survives growing retries");
            Reset(Mode.HostFailure); _after = "";
            Check(storage.NextTable("", out got, out kind) == ScoResult.WrongThread && got == "" && kind == 0 && _arguments,
                  "empty cursor preserves host failure and outputs");
            Reset(Mode.Normal); _inputTable = ""; index = _wanted;
            Check(storage.NextColumn("", ref index, out name, out type, out flags) == ScoResult.Ok &&
                  name == "column" && type == "TEXT" && index == 8 && _arguments, "empty table passed as non-null empty string");
            Reset(Mode.Normal, longName, longType); _inputTable = ""; index = _wanted;
            Check(storage.NextColumn("", ref index, out name, out type, out flags) == ScoResult.Ok &&
                  name == longName && type == longType && index == 8 && _calls == 3 && _arguments,
                  "empty table survives sequential retries");
            Reset(Mode.HostFailure); _inputTable = ""; index = _wanted;
            Check(storage.NextColumn("", ref index, out name, out type, out flags) == ScoResult.WrongThread &&
                  name == "" && type == "" && flags == 0 && index == _wanted && _arguments,
                  "empty table preserves host failure and index");

            foreach (Mode mode in new[] { Mode.NoProgress, Mode.TooLarge, Mode.BadZero, Mode.BadOver, Mode.MissingNul, Mode.EarlyNul, Mode.BadUtf8, Mode.HostFailure, Mode.FailAfterGrowth, Mode.Forever })
            {
                ScoResult expected = mode == Mode.HostFailure || mode == Mode.FailAfterGrowth ? ScoResult.WrongThread :
                    mode == Mode.BadZero || mode == Mode.BadOver || mode == Mode.MissingNul || mode == Mode.EarlyNul || mode == Mode.BadUtf8 ? ScoResult.Failed : ScoResult.TooMany;
                Reset(mode); index = _wanted;
                Check(storage.NextTable("cursor", out got, out kind) == expected && got == "" && kind == 0 && _arguments && _calls <= 64,
                      "table refusal preserves outputs: " + mode);
                Reset(mode); index = _wanted;
                Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == expected &&
                      name == "" && type == "" && flags == 0 && index == _wanted && _arguments && _calls <= 64,
                      "column refusal preserves outputs and index: " + mode);
            }

            Reset(Mode.IndexBad); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Failed &&
                  name == "" && type == "" && flags == 0 && index == _wanted, "non-advancing index cannot commit valid fields");
            Reset(Mode.IndexJump); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Failed &&
                  name == "" && type == "" && flags == 0 && index == _wanted, "jumping index cannot commit valid fields");
            Reset(Mode.Normal); _wanted = uint.MaxValue; index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Failed &&
                  name == "" && type == "" && flags == 0 && index == uint.MaxValue, "wrapped index cannot commit valid fields");
            Reset(Mode.NameBad); index = _wanted;
            Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Failed &&
                  name == "" && type == "" && flags == 0 && index == _wanted, "invalid name length cannot commit a valid type");

            Reset(Mode.Normal, new string('z', 16 << 20));
            Check(storage.NextTable("cursor", out got, out kind) == ScoResult.Ok && got.Length == (16 << 20) &&
                  got[0] == 'z' && got[^1] == 'z' && kind == 23 && _calls == 2, "16 MiB payload plus NUL fits");

            for (uint size = 128; size < 144; ++size)
            {
                var shortTable = (ScoStorageV1*)NativeMemory.AllocZeroed(size);
                shortTable->size = size;
                _table = shortTable;
                Check(storage.Open(p) == ScoResult.Ok, "open physically short storage table");
                Reset(Mode.Normal); index = _wanted;
                if (size < 136)
                    Check(storage.NextTable("cursor", out got, out kind) == ScoResult.Unavailable && _calls == 0,
                          "physically truncated table slot " + size);
                Check(storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Unavailable &&
                      index == _wanted && _calls == 0, "physically truncated column slot " + size);
                NativeMemory.Free(shortTable);
            }
            _table = &table;
            Check(storage.Open(p) == ScoResult.Ok, "restore full storage table");
            table.size = 128; Reset(Mode.Normal); index = _wanted;
            Check(storage.NextTable("cursor", out got, out kind) == ScoResult.Unavailable && got == "" && kind == 0 &&
                  storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Unavailable && index == _wanted && _calls == 0,
                  "1.0 table refuses schema tail");
            table.size = (uint)sizeof(ScoStorageV1); table.next_table = null; table.next_column = null;
            Check(storage.NextTable("cursor", out got, out kind) == ScoResult.Unavailable &&
                  storage.NextColumn("schema", ref index, out name, out type, out flags) == ScoResult.Unavailable && _calls == 0,
                  "null schema members refused");
            PluginExports.UnloadPlugin(p); _table = null;
            Console.WriteLine($"storage schema: {_checks} checks, {_failed} failed");
            return _failed;
        }
    }
}
