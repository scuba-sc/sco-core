// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Sco.Sdk.Interop;

namespace Sco.Sdk.Game
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScInventoryLayoutEntry
    {
        public uint size, state, role, flags, raw_item_type, reserved;
        public ulong entity_id;
        public fixed byte port_key[64];
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScInventoryLayoutSnapshot
    {
        public uint size, count, total_seen, flags;
        public ulong source_epoch, observation_generation;
        public fixed byte identity_domain[64];
        public fixed byte layout_key[64];
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ScInventoryV12
    {
        public uint size, pad;
        public delegate* unmanaged[Cdecl]<void*, ScInventorySnapshot*, ScoResult> snapshot;
        public delegate* unmanaged[Cdecl]<void*, ScInventoryLayoutSnapshot*, ScInventoryLayoutEntry*, uint, uint, ScoResult> snapshot_layout;
    }

    public sealed record InventoryLayoutPort(string Key, uint State, uint Role, uint Flags, uint RawItemType, ulong EntityId);
    public sealed record InventoryLayoutObservation(string IdentityDomain, string LayoutKey, uint Flags, uint TotalSeen,
        ulong SourceEpoch, ulong ObservationGeneration, InventoryLayoutPort[] Ports);

    /// <summary>Copied, read-only logical layout. A table is not proof of an admitted producer.</summary>
    public sealed unsafe class InventoryLayout
    {
        private Plugin? _plugin;
        private ScInventoryV12* _table;
        public ScoResult Open(Plugin plugin)
        {
            ArgumentNullException.ThrowIfNull(plugin);
            _plugin = null; _table = null;
            var r = plugin.Query("game.inventory", 0x00010002u, out ServiceRef<ScInventoryV12> service);
            if (r != ScoResult.Ok) return r;
            if (!service.Covers(16) || service.Table->snapshot_layout == null) return ScoResult.Unavailable;
            _plugin = plugin; _table = service.Table;
            return ScoResult.Ok;
        }

        private static string? Key(byte* text, bool domain)
        {
            int i = 0;
            while (i < 64 && text[i] != 0)
            {
                byte c = text[i++];
                if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-' &&
                    !( !domain && c >= 'A' && c <= 'Z')) return null;
            }
            return i == 0 || i == 64 ? null : Encoding.ASCII.GetString(new ReadOnlySpan<byte>(text, i));
        }

        public ScoResult Snapshot(out InventoryLayoutObservation? observation)
        {
            observation = null;
            var api = _plugin == null ? null : _plugin.Api;
            if (api == null || _plugin!.Self == 0 || api->size < 16 + (uint)sizeof(nint) || api->has == null ||
                !_plugin.Has("game.inventory.layout") || !new ServiceRef<ScInventoryV12>(_table).Covers(16) || _table->snapshot_layout == null)
                return ScoResult.Unavailable;
            ScInventoryLayoutSnapshot info = default; info.size = (uint)sizeof(ScInventoryLayoutSnapshot);
            ScInventoryLayoutEntry* entries = stackalloc ScInventoryLayoutEntry[128];
            for (int i = 0; i < 128; ++i) { entries[i] = default; entries[i].size = (uint)sizeof(ScInventoryLayoutEntry); }
            var result = _table->snapshot_layout(_plugin.SelfPtr, &info, entries, (uint)sizeof(ScInventoryLayoutEntry), 128);
            if (result != ScoResult.Ok) return result;
            if (info.size != sizeof(ScInventoryLayoutSnapshot) || info.count > 128 || info.total_seen < info.count ||
                info.source_epoch == 0 || info.observation_generation == 0 || info.flags == 0 || (info.flags & ~7u) != 0 ||
                ((info.flags & 1u) != 0 && (info.flags != 1u || info.count != info.total_seen)) ||
                (((info.flags & 2u) != 0) != (info.total_seen > info.count))) return ScoResult.Failed;
            string? domain = Key(info.identity_domain, true), layout = Key(info.layout_key, false);
            if (domain == null || layout == null) return ScoResult.Failed;
            var ports = new InventoryLayoutPort[info.count];
            var keys = new HashSet<string>(StringComparer.Ordinal);
            for (int i = 0; i < ports.Length; ++i)
            {
                var row = &entries[i];
                string? key = Key(row->port_key, false);
                if (row->size != sizeof(ScInventoryLayoutEntry) || row->state > 2 || row->role > 2 ||
                    (row->flags & ~3u) != 0 || row->reserved != 0 || key == null || !keys.Add(key) ||
                    (row->state != 2 && (row->flags != 0 || row->raw_item_type != 0 || row->entity_id != 0)) ||
                    ((row->flags & 1u) == 0 && row->raw_item_type != 0) ||
                    (((row->flags & 2u) != 0) != (row->entity_id != 0))) return ScoResult.Failed;
                ports[i] = new InventoryLayoutPort(key, row->state, row->role, row->flags, row->raw_item_type, row->entity_id);
            }
            observation = new InventoryLayoutObservation(domain, layout, info.flags, info.total_seen,
                info.source_epoch, info.observation_generation, ports);
            return ScoResult.Ok;
        }
    }
}
