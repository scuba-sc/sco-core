// SPDX-License-Identifier: GPL-3.0-only
//! Reads the regular files of a plugin zip after `scl_plugins::verify` accepted it.
//!
//! The verifier checks structure, names, sizes, CRCs and hashes without extracting. This reader
//! is a second, small parser (central directory, stored and deflate members) and every file it
//! returns is compared with the SHA-256 the verifier reported, so a disagreement between the two
//! parsers refuses the install instead of writing something unverified.

use flate2::read::DeflateDecoder;
use scl_plugins::sha256;
use scl_plugins::verify::Report;
use std::io::Read;

/// One file of the archive: `path` as in the zip (`<id>/plugin.ini`) and its bytes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Member {
    pub path: String,
    pub data: Vec<u8>,
}

fn le16(b: &[u8], o: usize) -> Option<usize> {
    Some(u16::from_le_bytes(b.get(o..o + 2)?.try_into().ok()?) as usize)
}

fn le32(b: &[u8], o: usize) -> Option<usize> {
    Some(u32::from_le_bytes(b.get(o..o + 4)?.try_into().ok()?) as usize)
}

/// Returns the files named in `report.artifacts`, each checked against its reported size and
/// SHA-256. `zip` is the whole archive in memory (the verifier caps it at 256 MiB).
pub fn read_members(zip: &[u8], report: &Report) -> Result<Vec<Member>, String> {
    // End of central directory: the last 0x06054b50 in the final 64 KiB + 22 bytes.
    let from = zip.len().saturating_sub(22 + 65535);
    let eocd = (from..zip.len().saturating_sub(21))
        .rev()
        .find(|&i| zip[i..i + 4] == [0x50, 0x4b, 0x05, 0x06])
        .ok_or("no end of central directory")?;
    let count = le16(zip, eocd + 10).ok_or("short end record")?;
    let cd_offset = le32(zip, eocd + 16).ok_or("short end record")?;
    let mut pos = cd_offset;
    let mut out = Vec::new();
    for _ in 0..count {
        if zip.get(pos..pos + 4) != Some(&[0x50, 0x4b, 0x01, 0x02]) {
            return Err("bad central directory entry".into());
        }
        let method = le16(zip, pos + 10).ok_or("short entry")?;
        let csize = le32(zip, pos + 20).ok_or("short entry")?;
        let usize_ = le32(zip, pos + 24).ok_or("short entry")?;
        let nlen = le16(zip, pos + 28).ok_or("short entry")?;
        let elen = le16(zip, pos + 30).ok_or("short entry")?;
        let clen = le16(zip, pos + 32).ok_or("short entry")?;
        let local = le32(zip, pos + 42).ok_or("short entry")?;
        let name_bytes = zip.get(pos + 46..pos + 46 + nlen).ok_or("short name")?;
        let name = std::str::from_utf8(name_bytes)
            .map_err(|_| "member name is not UTF-8")?
            .to_string();
        pos += 46 + nlen + elen + clen;
        if name.ends_with('/') {
            continue;
        }
        let Some(art) = report.artifacts.iter().find(|a| a.path == name) else {
            return Err(format!("member '{name}' is not in the verification report"));
        };
        if zip.get(local..local + 4) != Some(&[0x50, 0x4b, 0x03, 0x04]) {
            return Err(format!("bad local header for '{name}'"));
        }
        let lnlen = le16(zip, local + 26).ok_or("short local header")?;
        let lelen = le16(zip, local + 28).ok_or("short local header")?;
        let start = local + 30 + lnlen + lelen;
        let raw = zip
            .get(start..start + csize)
            .ok_or_else(|| format!("'{name}' runs past the end of the archive"))?;
        let data = match method {
            0 => raw.to_vec(),
            8 => {
                let mut d = Vec::with_capacity(usize_);
                DeflateDecoder::new(raw)
                    .take(art.size_bytes + 1)
                    .read_to_end(&mut d)
                    .map_err(|e| format!("'{name}': {e}"))?;
                d
            }
            m => return Err(format!("'{name}': unsupported method {m}")),
        };
        if data.len() as u64 != art.size_bytes || sha256::hex_digest(&data) != art.sha256 {
            return Err(format!("'{name}' does not match the verification report"));
        }
        out.push(Member { path: name, data });
    }
    if out.len() != report.artifacts.len() {
        return Err("the file set differs from the verification report".into());
    }
    Ok(out)
}
