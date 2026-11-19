// SPDX-License-Identifier: GPL-3.0-only
//! The real launcher folder behind the MCP server.
//!
//! Everything here only reads. The status fields come from `scl-install`, the plugin folders, logs
//! and the ini file from files in the launcher folder. Files are opened with `std::fs`, so a running
//! game is not disturbed. No `data` folder is created, no log is written and nothing uses the
//! network. Nothing here was run in game.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use scl_contract::capability_env::SWITCHES;
use scl_install::Layout;

use crate::server::{
    folder_less, IniRead, LogRead, LogScan, Platform, PluginFolder, StatusInfo, MAX_PLUGINS,
};

/// Reads at most this much of a plugin.ini, from its start (id, name and version come first).
const PLUGIN_INI_BYTES: usize = 64 * 1024;
/// Reads at most this much of the ini file.
const SETTINGS_BYTES: usize = 256 * 1024;
/// Folder names looked at before sorting.
const MAX_FOLDER_NAMES: usize = 4096;

/// The platform of one launcher folder.
pub struct HostPlatform {
    layout: Layout,
}

impl HostPlatform {
    pub fn new(layout: Layout) -> Self {
        HostPlatform { layout }
    }

    /// The path of a log, or None. The game's own log is never looked up, let alone opened.
    fn log_path(&self, log: &str) -> Option<PathBuf> {
        let data = &self.layout.data;
        match log {
            "mod" => Some(self.layout.mod_log.clone()),
            "launcher" => Some(data.join("launcher.log")),
            "launcher_previous" => Some(data.join("launcher.log.1")),
            _ => None,
        }
    }
}

/// `Narrow`: a path as UTF-8 bytes.
fn narrow(p: &Path) -> Vec<u8> {
    p.to_string_lossy().into_owned().into_bytes()
}

/// Why a file could not be opened. A missing file or folder is not an error: the file does not exist.
fn open_failure(e: &std::io::Error) -> Option<Vec<u8>> {
    let code = e.raw_os_error().unwrap_or(0);
    if code == 2 || code == 3 {
        return None; // ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND
    }
    Some(format!("the file could not be opened (Windows error {code})").into_bytes())
}

/// `ReadFileRange`: reads at most `max` bytes, the file's end (a log), or with `from_head` its start (a
/// plugin.ini); `skipped_older` then means "more was left".
fn read_range(path: &Path, max: usize, from_head: bool) -> LogRead {
    let mut out = LogRead {
        path: narrow(path),
        ..LogRead::default()
    };
    let mut f = match File::open(path) {
        Ok(f) => f,
        Err(e) => {
            if let Some(text) = open_failure(&e) {
                out.exists = true;
                out.error = text;
            }
            return out;
        }
    };
    out.exists = true;
    let size = match f.metadata() {
        Ok(m) => m.len(),
        Err(e) => {
            out.error = format!(
                "the file size could not be read (Windows error {})",
                e.raw_os_error().unwrap_or(0)
            )
            .into_bytes();
            return out;
        }
    };
    let mut start = 0u64;
    if size > max as u64 {
        if !from_head {
            start = size - max as u64;
        }
        out.skipped_older = true;
    }
    if let Err(e) = f.seek(SeekFrom::Start(start)) {
        out.error = format!(
            "the file could not be read (Windows error {})",
            e.raw_os_error().unwrap_or(0)
        )
        .into_bytes();
        return out;
    }
    let mut bytes = Vec::new();
    // A read error stops the read; what was read so far is kept.
    let _ = f.take(max as u64).read_to_end(&mut bytes);
    out.bytes = bytes;
    out
}

impl Platform for HostPlatform {
    fn version(&self) -> String {
        self.layout.product.version.to_string()
    }

    fn product(&self) -> String {
        self.layout.product.name.to_string()
    }

    fn profile_roots(&self) -> Vec<Vec<u8>> {
                match std::env::var_os("USERPROFILE") {
            Some(p) if !p.is_empty() => vec![p.to_string_lossy().into_owned().into_bytes()],
            _ => Vec::new(),
        }
    }

    fn account_names(&self) -> Vec<Vec<u8>> {
                let mut names = Vec::new();
        if let Some(a) = std::env::var_os("USERNAME") {
            if !a.is_empty() {
                names.push(a.to_string_lossy().into_owned().into_bytes());
            }
        }
        let profile = std::env::var_os("USERPROFILE")
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_default();
        let trimmed = profile.trim_end_matches(['\\', '/']);
        let last = match trimmed.rfind(['\\', '/']) {
            Some(cut) => &trimmed[cut + 1..],
            None => trimmed,
        };
        if !last.is_empty() {
            names.push(last.as_bytes().to_vec());
        }
        names
    }

    fn status(&mut self) -> StatusInfo {
        let st = scl_install::status::collect(&self.layout);
        StatusInfo {
            launcher_version: st.version.clone().into_bytes(),
            folder: st.root.clone().into_bytes(),
            settings_found: st.ini_found,
            data_found: st.data_found,
            plugins_setting_on: st.plugins_setting_on,
            plugins_enabled: st.plugins_enabled as i64,
            plugins_total: st.plugins_total as i64,
            capabilities_on: SWITCHES
                .iter()
                .filter(|w| st.config.capabilities.is_on(w))
                .map(|w| w.ini_key.to_string())
                .collect(),
            capabilities_total: SWITCHES.len() as i64,
        }
    }

    fn plugins(&mut self) -> Vec<PluginFolder> {
        // Names first (cheap, up to 4096), sorted the way list_plugins sorts; only the first MAX_PLUGINS + 1 have
        // their plugin.ini read, so memory stays small and the 256 returned are the first 256 by name. One more
        // than MAX_PLUGINS tells list_plugins to set `truncated`.
        let dir = self.layout.plugins.clone();
        let Ok(entries) = std::fs::read_dir(&dir) else {
            return Vec::new();
        };
        let mut names: Vec<(Vec<u8>, PathBuf)> = Vec::new();
        for entry in entries.flatten() {
            let name = entry.file_name();
            let text = name.to_string_lossy();
            // metadata() follows junctions and symlinks.
            let is_dir = std::fs::metadata(entry.path())
                .map(|m| m.is_dir())
                .unwrap_or(false);
            if !is_dir || text.starts_with('.') {
                continue;
            }
            let folder = entry.path();
            if !folder.join("plugin.ini").is_file() || names.len() >= MAX_FOLDER_NAMES {
                continue;
            }
            names.push((text.as_bytes().to_vec(), folder));
        }
        names.sort_by(|a, b| {
            if folder_less(&a.0, &b.0) {
                std::cmp::Ordering::Less
            } else if folder_less(&b.0, &a.0) {
                std::cmp::Ordering::Greater
            } else {
                std::cmp::Ordering::Equal
            }
        });
        names.truncate(MAX_PLUGINS + 1);
        names
            .into_iter()
            .map(|(folder, path)| {
                let ini = read_range(&path.join("plugin.ini"), PLUGIN_INI_BYTES, true);
                PluginFolder {
                    folder,
                    ini_text: ini.bytes,
                    disabled: std::fs::metadata(path.join("disabled")).is_ok(),
                }
            })
            .collect()
    }

    fn read_log(&mut self, log: &str, max_bytes: usize) -> LogRead {
        match self.log_path(log) {
            Some(path) => read_range(&path, max_bytes, false),
            None => LogRead::default(),
        }
    }

    fn scan_log(&mut self, log: &str, max_bytes: u64, sink: &mut dyn FnMut(&[u8])) -> LogScan {
        // Streams a file from its start in 1 MiB chunks to the sink, at most max_bytes.
        let mut out = LogScan::default();
        let Some(path) = self.log_path(log) else {
            return out;
        };
        let mut f = match File::open(&path) {
            Ok(f) => f,
            Err(e) => {
                if let Some(text) = open_failure(&e) {
                    out.exists = true;
                    out.error = text;
                }
                return out;
            }
        };
        out.exists = true;
        match f.metadata() {
            Ok(m) => out.size = m.len(),
            Err(e) => {
                out.error = format!(
                    "the file size could not be read (Windows error {})",
                    e.raw_os_error().unwrap_or(0)
                )
                .into_bytes();
                return out;
            }
        }
        let mut buf = vec![0u8; 1 << 20];
        while out.scanned < max_bytes {
            let want = (buf.len() as u64).min(max_bytes - out.scanned) as usize;
            match f.read(&mut buf[..want]) {
                Ok(0) | Err(_) => break,
                Ok(got) => {
                    sink(&buf[..got]);
                    out.scanned += got as u64;
                }
            }
        }
        out
    }

    fn settings(&mut self) -> IniRead {
        let path = self.layout.ini.clone();
        let f = read_range(&path, SETTINGS_BYTES, false);
        IniRead {
            path: narrow(&path),
            exists: f.exists,
            error: f.error,
            text: f.bytes,
        }
    }
}
