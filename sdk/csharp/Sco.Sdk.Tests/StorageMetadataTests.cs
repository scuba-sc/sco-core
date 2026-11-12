// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Tests
{
    internal static unsafe class StorageMetadataTests
    {
        private sealed class Owner : Plugin { }
        private static ScoStorageV2* _table;
        private static ScoResult _queryResult = ScoResult.Ok, _result = ScoResult.Ok;
        private static int _mode, _calls, _checks, _failed;
        private static byte[] _name = Encoding.UTF8.GetBytes("visits");
        private static void Check(bool ok, string why)
        { ++_checks; if (!ok) { ++_failed; Console.WriteLine("FAIL storage metadata: " + why); } }
        private static void At<T>(string field, int offset) where T : unmanaged => Check(Marshal.OffsetOf<T>(field).ToInt32() == offset, typeof(T).Name + "." + field);

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Query(byte* name, uint version, void** output)
        {
            Check(Marshal.PtrToStringUTF8((nint)name) == StorageMetadata.ServiceName && version == 0x00020000, "service identity");
            *output = _table; return _queryResult;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Info(void* self, ScoStorageOwnerInfo* output)
        {
            ++_calls; Check(self == (void*)1 && output->size == 32, "owner arguments and exact record capacity");
            *output = new() { size = 32, flags = 1, session_generation = 7, table_count = 3, cursor_count = 2 };
            if (_mode == 1) output->size = 31;
            if (_mode == 2) output->size = 33;
            if (_mode == 3) output->flags = 8;
            if (_mode == 4) output->session_generation = 0;
            return _result;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Next(void* self, byte* after, byte* output, uint* size, ScoStorageTableInfo* info)
        {
            ++_calls; Check(self == (void*)1 && info->size == 16, "table arguments and exact record capacity");
            *info = new() { size = 16, column_count = 4 };
            if (_result != ScoResult.Ok) return _result;
            if (_mode == 10) return ScoResult.TooMany;
            if (_mode == 11) { *size = (16u << 20) + 2; return ScoResult.TooMany; }
            uint needed = (uint)_name.Length + 1;
            if (*size < needed) { *size = needed; return ScoResult.TooMany; }
            _name.AsSpan().CopyTo(new Span<byte>(output, _name.Length)); output[_name.Length] = 0; *size = needed;
            if (_mode == 1) info->size = 15;
            if (_mode == 2) info->size = 17;
            if (_mode == 3) info->flags = 1;
            if (_mode == 5) *size = 0;
            if (_mode == 6) *size = needed + 100000;
            if (_mode == 7) output[needed - 1] = 1;
            if (_mode == 8) output[0] = 0;
            if (_mode == 9) { output[0] = 0xc3; output[1] = 0; *size = 2; }
            return ScoResult.Ok;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Rows(void* self, byte* table, ulong* count)
        {
            ++_calls; Check(self == (void*)1 && table != null, "row arguments"); *count = 42; return _result;
        }
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        private static ScoResult Quota(void* self, ScoStorageQuotaInfo* output)
        {
            ++_calls; Check(self == (void*)1 && output->size == 40, "quota arguments and exact record capacity");
            *output = new() { size = 40, configured_bytes = 10001, effective_bytes = 12288, allocated_bytes = 4096, free_bytes = 8192 };
            if (_mode == 1) output->size = 39;
            if (_mode == 2) output->size = 41;
            if (_mode == 3) output->flags = 1;
            if (_mode == 4) output->allocated_bytes = 12289;
            if (_mode == 5) output->free_bytes = 0;
            return _result;
        }

        public static int Run()
        {
            Check(sizeof(ScoStorageV2) == 40 && sizeof(ScoStorageOwnerInfo) == 32 && sizeof(ScoStorageTableInfo) == 16 && sizeof(ScoStorageQuotaInfo) == 40, "native record and table sizes");
            At<ScoStorageV2>("size", 0); At<ScoStorageV2>("_pad", 4);
            At<ScoStorageV2>("owner_info", 8); At<ScoStorageV2>("next_table", 16); At<ScoStorageV2>("row_count", 24); At<ScoStorageV2>("quota_info", 32);
            At<ScoStorageOwnerInfo>("size", 0); At<ScoStorageOwnerInfo>("flags", 4); At<ScoStorageOwnerInfo>("session_generation", 8); At<ScoStorageOwnerInfo>("table_count", 16); At<ScoStorageOwnerInfo>("cursor_count", 24);
            At<ScoStorageTableInfo>("size", 0); At<ScoStorageTableInfo>("flags", 4); At<ScoStorageTableInfo>("column_count", 8);
            At<ScoStorageQuotaInfo>("size", 0); At<ScoStorageQuotaInfo>("flags", 4); At<ScoStorageQuotaInfo>("configured_bytes", 8); At<ScoStorageQuotaInfo>("effective_bytes", 16); At<ScoStorageQuotaInfo>("allocated_bytes", 24); At<ScoStorageQuotaInfo>("free_bytes", 32);
            ScoApi api = new() { size = (uint)sizeof(ScoApi), major = 1, minor = 1, query_service = &Query };
            ScoStorageV2 table = new() { size = 40, owner_info = &Info, next_table = &Next, row_count = &Rows, quota_info = &Quota };
            _table = &table;
            var plugin = new Owner();
            Check(PluginExports.LoadPlugin(plugin, &api, (void*)1, "test") == ScoResult.Ok, "load owner");
            var metadata = new StorageMetadata();
            Check(metadata.OwnerInfo(out _) == ScoResult.Unavailable, "unopened wrapper");
            Check(metadata.Open(plugin) == ScoResult.Ok && metadata.IsOpen, "open metadata");
            Check(metadata.OwnerInfo(out StorageOwnerInfo oi) == ScoResult.Ok && oi == new StorageOwnerInfo(1, 7, 3, 2), "owner copied values");
            Check(metadata.NextTable(null, out string name, out StorageTableInfo ti) == ScoResult.Ok && name == "visits" && ti.ColumnCount == 4, "table copied values");
            Check(metadata.RowCount("visits", out ulong rows) == ScoResult.Ok && rows == 42, "row value");
            Check(metadata.QuotaInfo(out StorageQuotaInfo qi) == ScoResult.Ok && qi == new StorageQuotaInfo(0, 10001, 12288, 4096, 8192), "quota copied values");
            _name = Encoding.UTF8.GetBytes(new string('z', 900) + "é航");
            Check(metadata.NextTable("visits", out name, out ti) == ScoResult.Ok && name.EndsWith("é航") && name.Length == 902, "growing UTF-8 reply");
            _name = Array.Empty<byte>(); Check(metadata.NextTable(null, out name, out ti) == ScoResult.Ok && name == "", "empty name");
            _name = Encoding.UTF8.GetBytes("visits"); Check(metadata.NextTable("visits", out name, out ti) == ScoResult.Failed && name == "" && ti == default, "nonadvancing name");
            Check(oi.TableCount == 3 && qi.FreeBytes == 8192, "prior copies unchanged");
            foreach (string bad in new[] { "a\0b", "\ud800", "\udc00" })
            {
                _calls = 0;
                Check(metadata.NextTable(bad, out _, out _) == ScoResult.BadArg && metadata.RowCount(bad, out _) == ScoResult.BadArg && _calls == 0, "invalid input rejected before provider");
            }
            Check(metadata.RowCount(null!, out _) == ScoResult.BadArg, "null row table");
            for (_mode = 1; _mode <= 4; ++_mode) Check(metadata.OwnerInfo(out oi) == ScoResult.Failed && oi == default, "malformed owner " + _mode);
            foreach (int bad in new[] { 1, 2, 3, 5, 6, 7, 8, 9, 10, 11 })
            {
                _mode = bad;
                Check(metadata.NextTable(null, out name, out ti) == (bad >= 10 ? ScoResult.TooMany : ScoResult.Failed) && name == "" && ti == default, "malformed table " + bad);
            }
            for (_mode = 1; _mode <= 5; ++_mode) Check(metadata.QuotaInfo(out qi) == ScoResult.Failed && qi == default, "malformed quota " + _mode);
            _mode = 0;
            foreach (ScoResult error in new[] { ScoResult.BadArg, ScoResult.NotFound, ScoResult.Unavailable, ScoResult.WrongThread })
            {
                _result = error;
                Check(metadata.OwnerInfo(out oi) == error && oi == default && metadata.NextTable(null, out name, out ti) == error && name == "" && ti == default &&
                      metadata.RowCount("visits", out rows) == error && rows == 0 && metadata.QuotaInfo(out qi) == error && qi == default, "host failure leaves copied outputs empty");
            }
            _result = ScoResult.Ok;
            for (uint size = 4; size <= 40; ++size)
            {
                void* memory = NativeMemory.AllocZeroed(size); *(uint*)memory = size; _table = (ScoStorageV2*)memory;
                Check(metadata.Open(plugin) == ScoResult.Ok, "open physically short table"); _calls = 0;
                Check(metadata.OwnerInfo(out _) == ScoResult.Unavailable && metadata.NextTable(null, out _, out _) == ScoResult.Unavailable &&
                      metadata.RowCount("visits", out _) == ScoResult.Unavailable && metadata.QuotaInfo(out _) == ScoResult.Unavailable && _calls == 0, "short/null slots " + size);
                if (size >= 16) _table->owner_info = &Info;
                if (size >= 24) _table->next_table = &Next;
                if (size >= 32) _table->row_count = &Rows;
                if (size >= 40) _table->quota_info = &Quota;
                Check(metadata.OwnerInfo(out _) == (size >= 16 ? ScoResult.Ok : ScoResult.Unavailable) &&
                      metadata.NextTable(null, out _, out _) == (size >= 24 ? ScoResult.Ok : ScoResult.Unavailable) &&
                      metadata.RowCount("visits", out _) == (size >= 32 ? ScoResult.Ok : ScoResult.Unavailable) &&
                      metadata.QuotaInfo(out _) == (size >= 40 ? ScoResult.Ok : ScoResult.Unavailable), "only fully present slots used " + size);
                NativeMemory.Free(memory);
            }
            _table = &table;
            for (uint size = ApiOffset.query_service; size < ApiOffset.query_service + 8; ++size)
            {
                api.size = size;
                Check(metadata.Open(plugin) == ScoResult.Unavailable && !metadata.IsOpen, "old/partial API " + size);
            }
            api.size = (uint)sizeof(ScoApi); api.query_service = null;
            Check(metadata.Open(plugin) == ScoResult.Unavailable, "null query slot"); api.query_service = &Query;
            _table = null; Check(metadata.Open(plugin) == ScoResult.Unavailable, "null provider");
            _table = &table; _queryResult = ScoResult.NotFound; Check(metadata.Open(plugin) == ScoResult.Unavailable, "absent service");
            _queryResult = ScoResult.Unavailable; Check(metadata.Open(plugin) == ScoResult.Unavailable, "wrong service major");
            _queryResult = ScoResult.Ok;
            PluginExports.UnloadPlugin(plugin); _table = null;
            Console.WriteLine($"storage metadata: {_checks} checks, {_failed} failed");
            return _failed;
        }
    }
}
