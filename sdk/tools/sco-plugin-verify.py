#!/usr/bin/env python3
"""Statically verify a single-folder SCUBA plugin ZIP without extracting or running it.

    python tools/sco-plugin-verify.py plugin.zip [--checker PATH] [--json]

The SDK's sco-plugin-check executable is used in manifest-stdin mode so plugin.ini follows the
same parser and settings constraints as the SDK checker, without its normal runtime-check path.
Only ordinary ZIP files using stored/deflated entries are accepted. This is an archive-structure
and manifest-consistency check, not a compatibility, provider-behavior, code-safety, or trust check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import unicodedata
import zipfile
import zlib


MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_CENTRAL_DIRECTORY_BYTES = 8 * 1024 * 1024
MAX_ENTRIES = 4096
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_TOTAL_BYTES = 512 * 1024 * 1024
MAX_COMPRESSION_RATIO = 200
MAX_EMPTY_FILE_COMPRESSED_BYTES = 64
MAX_PATH_BYTES = 1024
MAX_SEGMENT_BYTES = 255
MAX_COLLISION_PREFIX_CHARS = 4 * 1024 * 1024
MAX_MANIFEST_BYTES = 16 * 1024
CHUNK_BYTES = 1024 * 1024
SUPPORTED_COMPRESSION = {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}
EOCD = struct.Struct("<4s4H2LH")
EOCD_SIGNATURE = b"PK\x05\x06"
CENTRAL_HEADER = struct.Struct("<4s6H3L5H2L")
CENTRAL_SIGNATURE = b"PK\x01\x02"
WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
WINDOWS_RESERVED.update(f"COM{i}" for i in range(1, 10))
WINDOWS_RESERVED.update(f"LPT{i}" for i in range(1, 10))
WINDOWS_RESERVED.update({"COM¹", "COM²", "COM³", "LPT¹", "LPT²", "LPT³"})


class VerifyError(Exception):
    """An archive failed a documented, static verification rule."""


def _digest_stream(stream):
    digest = hashlib.sha256()
    while True:
        chunk = stream.read(CHUNK_BYTES)
        if not chunk:
            break
        digest.update(chunk)
    return digest.hexdigest()


def _archive_sha256(fp):
    fp.seek(0)
    digest = _digest_stream(fp)
    fp.seek(0)
    return digest


def _preflight_zip(fp, archive_size):
    """Bound ZIP central-directory allocation before zipfile builds ZipInfo objects."""
    if archive_size < EOCD.size:
        raise VerifyError("file is too short to be a ZIP archive")
    tail_size = min(archive_size, EOCD.size + 0xFFFF)
    fp.seek(archive_size - tail_size)
    tail = fp.read(tail_size)
    position = tail.rfind(EOCD_SIGNATURE)
    found = None
    while position >= 0:
        if position + EOCD.size <= len(tail):
            fields = EOCD.unpack_from(tail, position)
            comment_bytes = fields[-1]
            if position + EOCD.size + comment_bytes == len(tail):
                found = (position, fields)
                break
        position = tail.rfind(EOCD_SIGNATURE, 0, position)
    if found is None:
        raise VerifyError("ZIP end-of-central-directory record is missing or malformed")

    tail_position, fields = found
    _, disk_number, directory_disk, disk_entries, total_entries, directory_size, directory_offset, _ = fields
    eocd_offset = archive_size - tail_size + tail_position
    if disk_number or directory_disk or disk_entries != total_entries:
        raise VerifyError("multi-disk ZIP archives are not supported")
    if (disk_entries == 0xFFFF or total_entries == 0xFFFF or directory_size == 0xFFFFFFFF or
            directory_offset == 0xFFFFFFFF):
        raise VerifyError("ZIP64 archives are not supported by this bounded verifier")
    if total_entries > MAX_ENTRIES:
        raise VerifyError(f"archive has {total_entries} entries; limit is {MAX_ENTRIES}")
    if directory_size > MAX_CENTRAL_DIRECTORY_BYTES:
        raise VerifyError(f"central directory exceeds {MAX_CENTRAL_DIRECTORY_BYTES} bytes")
    if directory_offset + directory_size != eocd_offset:
        raise VerifyError("ZIP must have no prepended data and no gap before its central directory")
    fp.seek(directory_offset)
    central = fp.read(directory_size)
    if len(central) != directory_size:
        raise VerifyError("ZIP central directory is truncated")
    cursor = 0
    actual_entries = 0
    while cursor < len(central):
        if cursor + CENTRAL_HEADER.size > len(central):
            raise VerifyError("ZIP central directory record is truncated")
        fields = CENTRAL_HEADER.unpack_from(central, cursor)
        if fields[0] != CENTRAL_SIGNATURE:
            raise VerifyError("ZIP central directory contains an unsupported record")
        compressed_size, uncompressed_size = fields[8], fields[9]
        name_bytes, extra_bytes, comment_bytes = fields[10], fields[11], fields[12]
        disk_start, local_offset = fields[13], fields[16]
        if disk_start:
            raise VerifyError("multi-disk or ZIP64 member records are not supported")
        if compressed_size == 0xFFFFFFFF or uncompressed_size == 0xFFFFFFFF or local_offset == 0xFFFFFFFF:
            raise VerifyError("ZIP64 member records are not supported by this bounded verifier")
        record_end = cursor + CENTRAL_HEADER.size + name_bytes + extra_bytes + comment_bytes
        if record_end > len(central):
            raise VerifyError("ZIP central directory record is truncated")
        actual_entries += 1
        if actual_entries > MAX_ENTRIES:
            raise VerifyError(f"archive has more than {MAX_ENTRIES} central-directory entries")
        cursor = record_end
    if actual_entries != total_entries:
        raise VerifyError("ZIP end-of-central-directory entry count disagrees with central directory")
    return directory_offset, total_entries


def _path_parts(name, is_directory):
    if not isinstance(name, str) or not name:
        raise VerifyError("archive contains an empty path")
    if "\x00" in name or "\\" in name or name.startswith("/"):
        raise VerifyError(f"unsafe archive path: {name!r}")
    if is_directory:
        if not name.endswith("/"):
            raise VerifyError(f"directory entry has no trailing slash: {name!r}")
        body = name[:-1]
    else:
        body = name
    parts = body.split("/")
    if not body or any(part in ("", ".", "..") for part in parts):
        raise VerifyError(f"unsafe archive path: {name!r}")
    try:
        encoded = name.encode("utf-8", "strict")
    except UnicodeEncodeError as exc:
        raise VerifyError(f"archive path is not valid Unicode: {name!r}") from exc
    if len(encoded) > MAX_PATH_BYTES:
        raise VerifyError(f"archive path exceeds {MAX_PATH_BYTES} UTF-8 bytes")
    for part in parts:
        try:
            part_bytes = part.encode("utf-8", "strict")
        except UnicodeEncodeError as exc:
            raise VerifyError(f"archive path is not valid Unicode: {name!r}") from exc
        if len(part_bytes) > MAX_SEGMENT_BYTES:
            raise VerifyError(f"archive path segment exceeds {MAX_SEGMENT_BYTES} UTF-8 bytes: {name!r}")
        if part[-1] in (".", " ") or any(char in part for char in '<>:"|?*'):
            raise VerifyError(f"path is not portable to Windows: {name!r}")
        if any(unicodedata.category(char) in {"Cc", "Cf", "Cs"} for char in part):
            raise VerifyError(f"archive path contains a control character: {name!r}")
        device_name = part.split(".", 1)[0].upper()
        if device_name in WINDOWS_RESERVED:
            raise VerifyError(f"archive path uses a reserved Windows device name: {name!r}")
    return parts


def _entry_kind(info):
    is_directory = info.is_dir()
    mode = (info.external_attr >> 16) & 0xFFFF
    file_type = stat.S_IFMT(mode)
    if file_type == stat.S_IFLNK:
        raise VerifyError(f"symlink entry is not allowed: {info.filename!r}")
    if file_type not in (0, stat.S_IFREG, stat.S_IFDIR):
        raise VerifyError(f"special filesystem entry is not allowed: {info.filename!r}")
    if file_type == stat.S_IFDIR and not is_directory:
        raise VerifyError(f"directory mode disagrees with ZIP path: {info.filename!r}")
    if file_type == stat.S_IFREG and is_directory:
        raise VerifyError(f"file mode disagrees with ZIP path: {info.filename!r}")
    if info.create_system == 0:
        if info.external_attr & 0x400:
            raise VerifyError(f"DOS reparse-point entry is not allowed: {info.filename!r}")
        if info.external_attr & 0x10 and not is_directory:
            raise VerifyError(f"DOS directory flag disagrees with ZIP path: {info.filename!r}")
    return "directory" if is_directory else "file"


def _collision_keys(prefix):
    nfc = unicodedata.normalize("NFC", prefix)
    nfkc = unicodedata.normalize("NFKC", prefix)
    return (nfc.casefold(), nfkc.casefold())


def _collision_prefix_cost(parts):
    """Bound material retained and normalized while checking every path prefix."""
    original_length = 0
    nfc_length = 0
    nfkc_length = 0
    total = 0
    for index, part in enumerate(parts):
        separator = 1 if index else 0
        original_length += len(part) + separator
        nfc_length += len(unicodedata.normalize("NFC", part).casefold()) + separator
        nfkc_length += len(unicodedata.normalize("NFKC", part).casefold()) + separator
        total += original_length + nfc_length + nfkc_length
    return total


def _inspect_entries(infos, central_directory_offset):
    if len(infos) > MAX_ENTRIES:
        raise VerifyError(f"archive has {len(infos)} entries; limit is {MAX_ENTRIES}")
    by_path = {}
    prefix_spelling = [{}, {}]
    header_offsets = set()
    file_count = 0
    total_uncompressed = 0
    collision_prefix_chars = 0
    for info in infos:
        if getattr(info, "orig_filename", info.filename) != info.filename:
            raise VerifyError("archive path contains a NUL byte or was otherwise truncated by ZIP parsing")
        kind = _entry_kind(info)
        parts = _path_parts(info.filename, kind == "directory")
        collision_prefix_chars += _collision_prefix_cost(parts)
        if collision_prefix_chars > MAX_COLLISION_PREFIX_CHARS:
            raise VerifyError(
                f"archive path collision checks exceed {MAX_COLLISION_PREFIX_CHARS:,} aggregate prefix characters"
            )
        path = "/".join(parts)
        previous = by_path.get(path)
        if previous is not None:
            if previous[0] == kind:
                raise VerifyError(f"duplicate archive path: {info.filename!r}")
            raise VerifyError(f"archive path is both a file and directory: {path!r}")
        by_path[path] = (kind, info)

        for index in range(1, len(parts) + 1):
            prefix = "/".join(parts[:index])
            keys = _collision_keys(prefix)
            for key_index, key in enumerate(keys):
                old = prefix_spelling[key_index].get(key)
                if old is not None and old != prefix:
                    raise VerifyError(f"case or Unicode path collision: {old!r} and {prefix!r}")
                prefix_spelling[key_index][key] = prefix

        if info.flag_bits & (0x0001 | 0x0040 | 0x2000):
            raise VerifyError(f"encrypted ZIP entries are not supported: {info.filename!r}")
        if info.compress_type not in SUPPORTED_COMPRESSION:
            raise VerifyError(f"unsupported ZIP compression method for {info.filename!r}")
        if info.header_offset < 0 or info.header_offset >= central_directory_offset:
            raise VerifyError(f"invalid local header offset: {info.filename!r}")
        if info.header_offset in header_offsets:
            raise VerifyError(f"ZIP entries share a local header: {info.filename!r}")
        header_offsets.add(info.header_offset)

        if kind == "directory":
            if info.file_size or info.compress_size or info.CRC:
                raise VerifyError(f"directory entries must be empty: {info.filename!r}")
            continue
        file_count += 1
        if info.file_size > MAX_FILE_BYTES:
            raise VerifyError(f"file exceeds {MAX_FILE_BYTES} uncompressed bytes: {info.filename!r}")
        total_uncompressed += info.file_size
        if total_uncompressed > MAX_TOTAL_BYTES:
            raise VerifyError(f"archive exceeds {MAX_TOTAL_BYTES} total uncompressed bytes")
        if info.file_size and (not info.compress_size or info.file_size > info.compress_size * MAX_COMPRESSION_RATIO):
            raise VerifyError(f"compression ratio exceeds {MAX_COMPRESSION_RATIO}:1: {info.filename!r}")
        if not info.file_size and info.compress_size > MAX_EMPTY_FILE_COMPRESSED_BYTES:
            raise VerifyError(f"empty file has excessive compressed data: {info.filename!r}")

    # A file cannot also be an ancestor directory. Unlisted parent directories are implicit.
    for path, (kind, _) in by_path.items():
        parts = path.split("/")
        for index in range(1, len(parts)):
            parent = "/".join(parts[:index])
            parent_entry = by_path.get(parent)
            if parent_entry is not None and parent_entry[0] != "directory":
                raise VerifyError(f"file is used as a directory: {parent!r}")
    return by_path, file_count, total_uncompressed


def _manifest_metadata(checker, manifest_bytes, folder):
    try:
        completed = subprocess.run(
            [str(checker), "--manifest-stdin", folder],
            input=manifest_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise VerifyError(f"could not run the SDK manifest parser: {exc}") from exc
    stdout = completed.stdout.decode("utf-8", "replace")
    stderr = completed.stderr.decode("utf-8", "replace")
    if completed.returncode:
        detail = (stdout + " " + stderr).strip()
        raise VerifyError("plugin.ini rejected by the SDK manifest parser" + (f": {detail}" if detail else ""))
    metadata = {"requires": []}
    for line in stdout.splitlines():
        key, separator, value = line.partition("\t")
        if not separator:
            raise VerifyError("SDK manifest parser returned malformed metadata")
        if key == "requires":
            metadata["requires"].append(value)
        elif key in {"id", "kind", "api", "entry"} and key not in metadata:
            metadata[key] = value
        else:
            raise VerifyError("SDK manifest parser returned unexpected metadata")
    if set(metadata) != {"id", "kind", "api", "entry", "requires"}:
        raise VerifyError("SDK manifest parser returned incomplete metadata")
    if metadata["id"] != folder:
        raise VerifyError(f"manifest id {metadata['id']!r} does not match archive folder {folder!r}")
    return metadata


def _file_hashes(zip_file, infos, manifest_path):
    artifacts = []
    manifest_bytes = None
    total_read = 0
    for info in infos:
        if info.is_dir():
            continue
        digest = hashlib.sha256()
        data = bytearray() if info.filename == manifest_path else None
        file_read = 0
        try:
            with zip_file.open(info, "r") as member:
                while True:
                    remaining = min(MAX_FILE_BYTES - file_read,
                                    MAX_TOTAL_BYTES - total_read,
                                    info.file_size - file_read)
                    if data is not None:
                        remaining = min(remaining, MAX_MANIFEST_BYTES - file_read)
                    # Ask for at most one byte beyond a limit so a dishonest stream can be
                    # detected without expanding or hashing another full chunk past the bound.
                    chunk = member.read(min(CHUNK_BYTES, max(1, remaining + 1)))
                    if not chunk:
                        break
                    next_file_read = file_read + len(chunk)
                    next_total_read = total_read + len(chunk)
                    if next_file_read > MAX_FILE_BYTES or next_total_read > MAX_TOTAL_BYTES:
                        raise VerifyError("archive expanded beyond configured limits while reading")
                    if next_file_read > info.file_size:
                        raise VerifyError(f"uncompressed size exceeds ZIP metadata: {info.filename!r}")
                    if data is not None and next_file_read > MAX_MANIFEST_BYTES:
                        raise VerifyError(f"{manifest_path!r} exceeds {MAX_MANIFEST_BYTES} bytes")
                    file_read = next_file_read
                    total_read = next_total_read
                    digest.update(chunk)
                    if data is not None:
                        data.extend(chunk)
        except (zipfile.BadZipFile, RuntimeError, EOFError, zlib.error) as exc:
            raise VerifyError(f"could not read ZIP entry {info.filename!r}: {exc}") from exc
        if file_read != info.file_size:
            raise VerifyError(f"uncompressed size disagrees with ZIP metadata: {info.filename!r}")
        if data is not None:
            manifest_bytes = bytes(data)
        artifacts.append({"path": info.filename, "size_bytes": file_read, "sha256": digest.hexdigest()})
    return artifacts, manifest_bytes, total_read


def _default_checker():
    sdk_root = Path(__file__).resolve().parent.parent
    executable = "sco-plugin-check.exe" if os.name == "nt" else "sco-plugin-check"
    candidates = (sdk_root / "bin" / executable, sdk_root / "out" / "bin" / executable,
                 sdk_root / "sdk-out" / "bin" / executable)
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


def verify_archive(archive_path, checker_path=None):
    """Return static ZIP, manifest, dependency and SHA-256 evidence, or raise VerifyError."""
    path = Path(archive_path)
    checker = Path(checker_path) if checker_path else _default_checker()
    if not checker.is_file():
        raise VerifyError(f"SDK manifest parser not found: {checker} (build sco-plugin-check or pass --checker)")
    try:
        input_stat = path.stat()
    except OSError as exc:
        raise VerifyError(f"cannot stat archive {path}: {exc}") from exc
    if not stat.S_ISREG(input_stat.st_mode):
        raise VerifyError(f"archive is not a regular file: {path}")
    try:
        fp = path.open("rb")
    except OSError as exc:
        raise VerifyError(f"cannot open archive {path}: {exc}") from exc
    with fp:
        stat_before = os.fstat(fp.fileno())
        if not stat.S_ISREG(stat_before.st_mode):
            raise VerifyError(f"archive is not a regular file: {path}")
        archive_size = stat_before.st_size
        if archive_size > MAX_ARCHIVE_BYTES:
            raise VerifyError(f"archive exceeds {MAX_ARCHIVE_BYTES} bytes")
        if archive_size <= 0:
            raise VerifyError("archive is empty")
        central_directory_offset, expected_entries = _preflight_zip(fp, archive_size)
        fp.seek(0)
        try:
            with zipfile.ZipFile(fp, "r", allowZip64=False) as zip_file:
                infos = zip_file.infolist()
                if len(infos) != expected_entries:
                    raise VerifyError("ZIP entry count disagrees with end-of-central-directory record")
                by_path, file_count, declared_total = _inspect_entries(infos, central_directory_offset)
                roots = {name.split("/", 1)[0] for name in by_path}
                if len(roots) != 1:
                    raise VerifyError("archive must contain exactly one plugin folder at its root")
                folder = next(iter(roots))
                root_record = by_path.get(folder)
                if root_record is not None and root_record[0] != "directory":
                    raise VerifyError("archive root must be a folder")
                manifest_path = f"{folder}/plugin.ini"
                manifest_record = by_path.get(manifest_path)
                if manifest_record is None or manifest_record[0] != "file":
                    raise VerifyError(f"archive must contain {manifest_path!r}")
                if manifest_record[1].file_size > MAX_MANIFEST_BYTES:
                    raise VerifyError(f"{manifest_path!r} exceeds {MAX_MANIFEST_BYTES} bytes")

                # Validate every member's declared size and compression bounds before hashing
                # archive bytes or opening any member for decompression.
                archive_hash_before = _archive_sha256(fp)
                artifacts, manifest_bytes, actual_total = _file_hashes(zip_file, infos, manifest_path)
                if manifest_bytes is None:
                    raise VerifyError(f"could not read {manifest_path!r}")
                metadata = _manifest_metadata(checker, manifest_bytes, folder)
                if metadata["kind"] != "data":
                    entry_path = f"{folder}/{metadata['entry']}"
                    entry = by_path.get(entry_path)
                    if entry is None or entry[0] != "file":
                        raise VerifyError(f"declared entry is not a regular archive file: {entry_path!r}")

                manifest_artifact = next(item for item in artifacts if item["path"] == manifest_path)
                archive_hash_after = _archive_sha256(fp)
                stat_after = os.fstat(fp.fileno())
                if (archive_hash_before != archive_hash_after or stat_before.st_size != stat_after.st_size or
                        stat_before.st_mtime_ns != stat_after.st_mtime_ns):
                    raise VerifyError("archive changed while it was being verified")
                if actual_total != declared_total:
                    raise VerifyError("total uncompressed size disagrees with ZIP metadata")
                return {
                    "schema_version": 1,
                    "status": "archive-structure-and-manifest-verified",
                    "archive": {"name": path.name, "size_bytes": archive_size, "sha256": archive_hash_before},
                    "plugin": {
                        "id": metadata["id"],
                        "folder": folder,
                        "kind": metadata["kind"],
                        "declared_api": metadata["api"],
                        "entry": metadata["entry"] or None,
                        "requires": metadata["requires"],
                        "manifest": {"path": manifest_path, "sha256": manifest_artifact["sha256"]},
                    },
                    "limits": {
                        "file_count": file_count,
                        "total_uncompressed_bytes": actual_total,
                        "max_archive_bytes": MAX_ARCHIVE_BYTES,
                        "max_central_directory_bytes": MAX_CENTRAL_DIRECTORY_BYTES,
                        "max_entries": MAX_ENTRIES,
                        "max_file_bytes": MAX_FILE_BYTES,
                        "max_total_uncompressed_bytes": MAX_TOTAL_BYTES,
                        "max_compression_ratio": f"{MAX_COMPRESSION_RATIO}:1",
                        "max_empty_file_compressed_bytes": MAX_EMPTY_FILE_COMPRESSED_BYTES,
                        "max_manifest_bytes": MAX_MANIFEST_BYTES,
                        "max_path_bytes": MAX_PATH_BYTES,
                    },
                    "artifacts": sorted(artifacts, key=lambda item: item["path"]),
                    "limitations": [
                        "This checks archive structure, plugin.ini syntax, folder identity and declared entry presence only.",
                        "Declared API version and dependencies are reported, not compared with a host or resolved.",
                        "It does not test runtime compatibility or provider behavior, inspect native-code safety, or establish trust.",
                        "No archive entry is extracted or executed; native and script entries are hashed as opaque bytes.",
                    ],
                }
        except (zipfile.BadZipFile, zipfile.LargeZipFile, OSError, EOFError, RuntimeError, zlib.error,
                UnicodeError, struct.error) as exc:
            raise VerifyError(f"invalid ZIP archive: {exc}") from exc


def _human_report(result):
    plugin = result["plugin"]
    requires = ", ".join(plugin["requires"]) if plugin["requires"] else "none stated"
    lines = [
        "Plugin archive verified (static archive and manifest checks)",
        f"Archive: {result['archive']['name']}",
        f"Archive SHA-256: {result['archive']['sha256']}",
        f"Plugin: {plugin['id']} ({plugin['kind']}; declares API {plugin['declared_api']})",
        f"Stated dependencies: {requires}",
        f"Artifacts ({result['limits']['file_count']} files):",
    ]
    lines.extend(f"  {item['sha256']}  {item['path']}" for item in result["artifacts"])
    lines.extend(("Limitations:", *(f"  {message}" for message in result["limitations"])))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("archive", help="single-plugin ZIP archive")
    parser.add_argument("--checker", help="trusted SDK sco-plugin-check executable; defaults to <SDK>/bin")
    parser.add_argument("--json", action="store_true", help="write machine-readable JSON to stdout")
    args = parser.parse_args(argv)
    try:
        result = verify_archive(args.archive, args.checker)
    except VerifyError as exc:
        print(f"plugin archive refused: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(_human_report(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
