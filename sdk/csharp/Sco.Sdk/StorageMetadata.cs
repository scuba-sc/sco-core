// SPDX-License-Identifier: GPL-3.0-only
// Separate read-only sco.storage.metadata 2.0 service; existing Storage is unchanged.
using System;
using System.Runtime.InteropServices;
using System.Text;
using Sco.Sdk.Interop;

namespace Sco.Sdk
{
    public readonly record struct StorageOwnerInfo(uint Flags, ulong SessionGeneration, ulong TableCount, ulong CursorCount);
    public readonly record struct StorageTableInfo(uint Flags, ulong ColumnCount);
    public readonly record struct StorageQuotaInfo(uint Flags, ulong ConfiguredBytes, ulong EffectiveBytes, ulong AllocatedBytes, ulong FreeBytes);

    /// <summary>Copied metadata for this plugin's already opened database. Never opens a database.</summary>
    public sealed unsafe class StorageMetadata
    {
        public const string ServiceName = "sco.storage.metadata";
        public const uint ServiceVersion = 0x00020000;
        public const uint OwnerOpened = 1;
        private const uint MaxNameBuffer = (16u << 20) + 1;
        private static readonly UTF8Encoding StrictUtf8 = new(false, true);
        private Plugin? _plugin;
        private ScoStorageV2* _t;
        public bool IsOpen => _t != null;
        private void* Self => _plugin == null ? null : _plugin.SelfPtr;
        private bool Covers(uint offset) => _t != null && (ulong)_t->size >= (ulong)offset + (uint)sizeof(nint);

        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _t = null; _plugin = null;
            ScoApi* api = plugin.Api;
            if (api == null || plugin.SelfPtr == null) return ScoResult.BadArg;
            if (!plugin.ApiCovers(ApiOffset.query_service) || api->query_service == null) return ScoResult.Unavailable;
            void* table = null;
            ScoResult r;
            fixed (byte* name = Utf8.Z(ServiceName)) r = api->query_service(name, ServiceVersion, &table);
            if (r == ScoResult.NotFound || (r == ScoResult.Ok && table == null)) return ScoResult.Unavailable;
            if (r != ScoResult.Ok) return r;
            _t = (ScoStorageV2*)table; _plugin = plugin;
            return ScoResult.Ok;
        }

        public ScoResult OwnerInfo(out StorageOwnerInfo value)
        {
            value = default;
            if (!Covers(8) || _t->owner_info == null) return ScoResult.Unavailable;
            ScoStorageOwnerInfo copy = new() { size = (uint)sizeof(ScoStorageOwnerInfo) };
            ScoResult r = _t->owner_info(Self, &copy);
            if (r != ScoResult.Ok) return r;
            if (copy.size != sizeof(ScoStorageOwnerInfo) || copy.flags != OwnerOpened || copy.session_generation == 0) return ScoResult.Failed;
            value = new(copy.flags, copy.session_generation, copy.table_count, copy.cursor_count);
            return ScoResult.Ok;
        }

        private static ScoResult Name(string? name, out byte[]? bytes)
        {
            bytes = null;
            if (name == null) return ScoResult.Ok;
            if (name.IndexOf('\0') >= 0) return ScoResult.BadArg;
            try
            {
                int count = StrictUtf8.GetByteCount(name);
                if (count >= MaxNameBuffer) return ScoResult.BadArg;
                bytes = new byte[count + 1];
                StrictUtf8.GetBytes(name.AsSpan(), bytes.AsSpan(0, count));
                return ScoResult.Ok;
            }
            catch (EncoderFallbackException) { return ScoResult.BadArg; }
            catch (OutOfMemoryException) { return ScoResult.TooMany; }
        }

        public ScoResult NextTable(string? after, out string name, out StorageTableInfo value)
        {
            name = ""; value = default;
            if (!Covers(16) || _t->next_table == null) return ScoResult.Unavailable;
            ScoResult input = Name(after, out byte[]? cursor);
            if (input != ScoResult.Ok) return input;
            try
            {
                byte[] buffer = new byte[256];
                for (int attempt = 0; attempt < 32; ++attempt)
                {
                    uint size = (uint)buffer.Length;
                    ScoStorageTableInfo copy = new() { size = (uint)sizeof(ScoStorageTableInfo) };
                    ScoResult r;
                    fixed (byte* a = cursor) fixed (byte* b = buffer) r = _t->next_table(Self, a, b, &size, &copy);
                    if (r == ScoResult.Ok)
                    {
                        if (copy.size != sizeof(ScoStorageTableInfo) || copy.flags != 0 || size == 0 || size > buffer.Length ||
                            buffer[size - 1] != 0 || buffer.AsSpan(0, (int)size - 1).IndexOf((byte)0) >= 0) return ScoResult.Failed;
                        ReadOnlySpan<byte> text = buffer.AsSpan(0, (int)size - 1);
                        if (cursor != null && text.SequenceCompareTo(cursor.AsSpan(0, cursor.Length - 1)) <= 0) return ScoResult.Failed;
                        string decoded = StrictUtf8.GetString(text);
                        name = decoded; value = new(copy.flags, copy.column_count);
                        return ScoResult.Ok;
                    }
                    if (r != ScoResult.TooMany) return r;
                    if (size <= buffer.Length || size > MaxNameBuffer) return ScoResult.TooMany;
                    buffer = new byte[size];
                }
                return ScoResult.TooMany;
            }
            catch (DecoderFallbackException) { return ScoResult.Failed; }
            catch (OutOfMemoryException) { return ScoResult.TooMany; }
        }

        public ScoResult RowCount(string table, out ulong value)
        {
            value = 0;
            if (!Covers(24) || _t->row_count == null) return ScoResult.Unavailable;
            ScoResult input = Name(table, out byte[]? bytes);
            if (input != ScoResult.Ok) return input;
            if (bytes == null) return ScoResult.BadArg;
            ulong copy = 0;
            ScoResult r;
            fixed (byte* name = bytes) r = _t->row_count(Self, name, &copy);
            if (r == ScoResult.Ok) value = copy;
            return r;
        }

        public ScoResult QuotaInfo(out StorageQuotaInfo value)
        {
            value = default;
            if (!Covers(32) || _t->quota_info == null) return ScoResult.Unavailable;
            ScoStorageQuotaInfo copy = new() { size = (uint)sizeof(ScoStorageQuotaInfo) };
            ScoResult r = _t->quota_info(Self, &copy);
            if (r != ScoResult.Ok) return r;
            if (copy.size != sizeof(ScoStorageQuotaInfo) || copy.flags != 0 || copy.allocated_bytes > copy.effective_bytes ||
                copy.free_bytes != copy.effective_bytes - copy.allocated_bytes) return ScoResult.Failed;
            value = new(copy.flags, copy.configured_bytes, copy.effective_bytes, copy.allocated_bytes, copy.free_bytes);
            return ScoResult.Ok;
        }
    }
}

namespace Sco.Sdk.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ScoStorageOwnerInfo
    {
        public uint size, flags;
        public ulong session_generation, table_count, cursor_count;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ScoStorageTableInfo
    {
        public uint size, flags;
        public ulong column_count;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ScoStorageQuotaInfo
    {
        public uint size, flags;
        public ulong configured_bytes, effective_bytes, allocated_bytes, free_bytes;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ScoStorageV2
    {
        public uint size, _pad;
        public delegate* unmanaged[Cdecl]<void*, ScoStorageOwnerInfo*, ScoResult> owner_info;
        public delegate* unmanaged[Cdecl]<void*, byte*, byte*, uint*, ScoStorageTableInfo*, ScoResult> next_table;
        public delegate* unmanaged[Cdecl]<void*, byte*, ulong*, ScoResult> row_count;
        public delegate* unmanaged[Cdecl]<void*, ScoStorageQuotaInfo*, ScoResult> quota_info;
    }
}
