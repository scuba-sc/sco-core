// SPDX-License-Identifier: GPL-3.0-only
//! Runs the verifier on the small zip set in `tests/zip-corpus/cases`: three packages that must
//! verify (one per plugin kind) and one hostile package that must be refused. The full corpus
//! (170 zips and 142 manifests) belongs to the product launcher this one was cut from; this set is
//! the part the reference launcher keeps. Synthetic only. Not run in game.

use std::path::PathBuf;

use scl_plugins::manifest::Kind;
use scl_plugins::verify;

fn zip(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/zip-corpus/cases")
        .join(format!("{name}.zip"))
}

#[test]
fn good_packages_verify_with_the_expected_kind() {
    for (name, kind) in [
        ("good-minimal", Kind::Native),
        ("good-lua-plugin", Kind::Lua),
        ("good-data-pack", Kind::Data),
    ] {
        let r = verify::verify_archive(&zip(name))
            .unwrap_or_else(|e| panic!("{name}: {}: {}", e.code.as_str(), e.message));
        assert_eq!(r.manifest.kind, kind, "{name}");
        assert_eq!(r.plugin.id, r.plugin.folder, "{name}");
        assert_eq!(r.archive.sha256.len(), 64);
        assert!(r.file_count >= 1);
    }
}

#[test]
fn hostile_package_is_refused() {
    let e = verify::verify_archive(&zip("bad-collision-case")).expect_err("must be refused");
    assert!(!e.code.as_str().is_empty());
}

#[test]
fn missing_file_is_refused_not_a_panic() {
    assert!(verify::verify_archive(&zip("does-not-exist")).is_err());
}
