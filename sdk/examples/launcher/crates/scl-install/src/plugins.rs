// SPDX-License-Identifier: GPL-3.0-only
//! Plugin folders: list them, switch them with the `disabled` marker, install one from a zip.
//!
//! The rules are those of the sco-core host (`docs/plugins.md`): a plugin folder is a folder of
//! `<data>/plugins/` that holds a `plugin.ini`, its id is the folder name, a file or folder named
//! `disabled` inside it keeps it from loading. Nothing here loads or runs plugin code.

use crate::extract::read_members;
use crate::layout::Layout;
use scl_config::plugin_ini::parse_plugin_ini;
use scl_contract::{files, plugin_api};
use scl_plugins::verify::{verify_archive, Report};
use std::fs;
use std::path::Path;

/// One folder of `<data>/plugins/` that has a `plugin.ini`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginEntry {
    /// The folder name, which is the plugin id.
    pub id: String,
    pub name: String,
    pub version: String,
    /// `native`, `lua`, `data`, or empty when the manifest names none.
    pub kind: String,
    /// False when the `disabled` marker is present.
    pub enabled: bool,
}

/// True when `id` is a legal plugin id: 1 to 31 characters of `a-z`, `0-9`, `_`, and not reserved.
pub fn valid_id(id: &str) -> bool {
    (1..=31).contains(&id.len())
        && id
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_')
        && !files::RESERVED_IDS.contains(&id)
}

/// Lists the plugin folders in byte order of the folder name. A missing plugin folder is an
/// empty list. Symlinks are skipped, as the host skips them.
pub fn list(layout: &Layout) -> Vec<PluginEntry> {
    let Ok(rd) = fs::read_dir(&layout.plugins) else {
        return Vec::new();
    };
    let mut names: Vec<String> = rd
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false))
        .filter_map(|e| e.file_name().into_string().ok())
        .filter(|n| !n.starts_with('.'))
        .collect();
    names.sort();
    let mut out = Vec::new();
    for id in names {
        let dir = layout.plugin_dir(&id);
        let Ok(bytes) = fs::read(dir.join(files::PLUGIN_MANIFEST)) else {
            continue;
        };
        let p = parse_plugin_ini(&id, &bytes[..bytes.len().min(16 * 1024)]);
        out.push(PluginEntry {
            enabled: !dir.join(files::PLUGIN_DISABLED_MARKER).exists(),
            id,
            name: p.name,
            version: p.version,
            kind: p.kind,
        });
    }
    out
}

/// Turns a plugin on or off by removing or creating the `disabled` marker. Returns whether the
/// state changed (false when it already was as asked).
pub fn set_enabled(layout: &Layout, id: &str, enabled: bool) -> Result<bool, String> {
    if !valid_id(id) {
        return Err(format!("'{id}' is not a plugin id"));
    }
    let dir = layout.plugin_dir(id);
    if !dir.join(files::PLUGIN_MANIFEST).is_file() {
        return Err(format!("no plugin '{id}' in {}", layout.plugins.display()));
    }
    let marker = dir.join(files::PLUGIN_DISABLED_MARKER);
    let is_disabled = marker.exists();
    match (enabled, is_disabled) {
        (true, true) => {
            let r = if marker.is_dir() {
                fs::remove_dir(&marker)
            } else {
                fs::remove_file(&marker)
            };
            r.map_err(|e| format!("cannot remove {}: {e}", marker.display()))?;
            Ok(true)
        }
        (false, false) => {
            fs::write(&marker, b"")
                .map_err(|e| format!("cannot write {}: {e}", marker.display()))?;
            Ok(true)
        }
        _ => Ok(false),
    }
}

/// What an install did.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Installed {
    pub id: String,
    pub name: String,
    pub version: String,
    pub kind: String,
    pub files: usize,
    pub bytes: u64,
    /// The zip's SHA-256, as the verifier computed it.
    pub archive_sha256: String,
    /// An older copy was replaced.
    pub replaced: bool,
    /// The new copy starts disabled (the old one was, or `start_disabled` was set).
    pub disabled: bool,
}

/// Options of [`install_from_zip`].
#[derive(Debug, Clone, Copy, Default)]
pub struct InstallOptions {
    /// Replace an installed plugin of the same id. Its `disabled` choice is kept.
    pub replace: bool,
    /// Install the plugin switched off.
    pub start_disabled: bool,
}

/// Checks the facts of a verified report that the verifier leaves to the product: the id rules and
/// the plugin API the launcher's host kit speaks.
pub fn check_report(report: &Report) -> Result<(), String> {
    let id = &report.plugin.id;
    if !valid_id(id) {
        return Err(format!(
            "plugin id '{id}' is not allowed (reserved, or not [a-z0-9_] with 1 to 31 characters)"
        ));
    }
    if *id != report.plugin.folder {
        return Err(format!(
            "id '{id}' does not match folder '{}'",
            report.plugin.folder
        ));
    }
    let m = &report.manifest;
    if m.api_major != plugin_api::MAJOR || m.api_minor > plugin_api::MINOR {
        return Err(format!(
            "built for plugin api {}, this launcher's host kit speaks {}.{}",
            m.api,
            plugin_api::MAJOR,
            plugin_api::MINOR
        ));
    }
    Ok(())
}

/// Verifies `zip` (structure, names, sizes, hashes, manifest), checks it against this launcher,
/// then writes the plugin folder: into a staging folder first, then one rename. Never extracts
/// anything the verifier did not report, never runs plugin code, never touches the network.
pub fn install_from_zip(
    layout: &Layout,
    zip: &Path,
    opts: InstallOptions,
) -> Result<Installed, String> {
    let report = verify_archive(zip).map_err(|e| format!("{}: {}", e.code.as_str(), e.message))?;
    check_report(&report)?;
    let bytes = fs::read(zip).map_err(|e| format!("cannot read {}: {e}", zip.display()))?;
    let members = read_members(&bytes, &report)?;
    let id = report.plugin.id.clone();
    let target = layout.plugin_dir(&id);
    let exists = target.exists();
    if exists && !opts.replace {
        return Err(format!(
            "plugin '{id}' is already installed (use --replace)"
        ));
    }
    let was_disabled = exists && target.join(files::PLUGIN_DISABLED_MARKER).exists();

    fs::create_dir_all(&layout.plugins)
        .map_err(|e| format!("cannot create {}: {e}", layout.plugins.display()))?;
    let staging = layout.plugins.join(format!(".install-{id}"));
    if staging.exists() {
        fs::remove_dir_all(&staging)
            .map_err(|e| format!("cannot clear {}: {e}", staging.display()))?;
    }
    let prefix = format!("{}/", report.plugin.folder);
    let mut total = 0u64;
    let mut write_all = || -> Result<(), String> {
        for m in &members {
            let rel = m
                .path
                .strip_prefix(&prefix)
                .ok_or_else(|| format!("member '{}' is outside the plugin folder", m.path))?;
            if rel.is_empty() || rel.split('/').any(|s| s.is_empty() || s == "." || s == "..") {
                return Err(format!("unsafe member name '{}'", m.path));
            }
            let dest = rel.split('/').fold(staging.clone(), |p, s| p.join(s));
            if let Some(parent) = dest.parent() {
                fs::create_dir_all(parent).map_err(|e| format!("{}: {e}", parent.display()))?;
            }
            fs::write(&dest, &m.data).map_err(|e| format!("{}: {e}", dest.display()))?;
            total += m.data.len() as u64;
        }
        Ok(())
    };
    if let Err(e) = write_all() {
        let _ = fs::remove_dir_all(&staging);
        return Err(e);
    }
    let disabled = was_disabled || opts.start_disabled;
    if disabled {
        if let Err(e) = fs::write(staging.join(files::PLUGIN_DISABLED_MARKER), b"") {
            let _ = fs::remove_dir_all(&staging);
            return Err(format!("cannot write the disabled marker: {e}"));
        }
    }
    let old = layout.plugins.join(format!(".replaced-{id}"));
    if exists {
        let _ = fs::remove_dir_all(&old);
        if let Err(e) = fs::rename(&target, &old) {
            let _ = fs::remove_dir_all(&staging);
            return Err(format!("cannot move the old copy aside: {e}"));
        }
    }
    if let Err(e) = fs::rename(&staging, &target) {
        if exists {
            let _ = fs::rename(&old, &target);
        }
        let _ = fs::remove_dir_all(&staging);
        return Err(format!("cannot put the plugin in place: {e}"));
    }
    if exists {
        let _ = fs::remove_dir_all(&old);
    }
    Ok(Installed {
        id,
        name: report.manifest.name.clone(),
        version: report.manifest.version.clone(),
        kind: report.manifest.kind.as_str().to_string(),
        files: members.len(),
        bytes: total,
        archive_sha256: report.archive.sha256.clone(),
        replaced: exists,
        disabled,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use scl_contract::REFERENCE_PRODUCT;
    use std::path::PathBuf;

    fn scratch(name: &str) -> PathBuf {
        let p = std::env::temp_dir().join(format!("scl-install-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(&p).unwrap();
        p
    }

    fn corpus(name: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/zip-corpus/cases")
            .join(format!("{name}.zip"))
    }

    #[test]
    fn ids() {
        assert!(valid_id("pilot_2"));
        assert!(!valid_id("Pilot"));
        assert!(!valid_id("sco"));
        assert!(!valid_id(""));
        assert!(!valid_id(&"a".repeat(32)));
    }

    #[test]
    fn install_list_disable_enable_replace() {
        let root = scratch("flow");
        let l = Layout::new(REFERENCE_PRODUCT, &root);
        assert!(list(&l).is_empty());
        let got = install_from_zip(&l, &corpus("good-minimal"), InstallOptions::default()).unwrap();
        assert!(!got.replaced && !got.disabled);
        let rows = list(&l);
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].id, got.id);
        assert!(rows[0].enabled);
        // A second install needs --replace.
        assert!(install_from_zip(&l, &corpus("good-minimal"), InstallOptions::default()).is_err());
        assert_eq!(set_enabled(&l, &got.id, false), Ok(true));
        assert_eq!(set_enabled(&l, &got.id, false), Ok(false));
        assert!(!list(&l)[0].enabled);
        // Replace keeps the disabled choice.
        let again = install_from_zip(
            &l,
            &corpus("good-minimal"),
            InstallOptions {
                replace: true,
                start_disabled: false,
            },
        )
        .unwrap();
        assert!(again.replaced && again.disabled);
        assert_eq!(set_enabled(&l, &got.id, true), Ok(true));
        assert!(list(&l)[0].enabled);
        // No staging leftovers.
        let leftovers: Vec<_> = fs::read_dir(&l.plugins)
            .unwrap()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_name().to_string_lossy().starts_with('.'))
            .collect();
        assert!(leftovers.is_empty());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn hostile_zip_installs_nothing() {
        let root = scratch("bad");
        let l = Layout::new(REFERENCE_PRODUCT, &root);
        let r = install_from_zip(&l, &corpus("bad-collision-case"), InstallOptions::default());
        assert!(r.is_err());
        assert!(list(&l).is_empty());
        assert!(!l.plugins.exists() || fs::read_dir(&l.plugins).unwrap().next().is_none());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn set_enabled_refuses_unknown_and_bad_ids() {
        let root = scratch("nope");
        let l = Layout::new(REFERENCE_PRODUCT, &root);
        assert!(set_enabled(&l, "missing", true).is_err());
        assert!(set_enabled(&l, "../x", true).is_err());
        let _ = fs::remove_dir_all(&root);
    }
}
