// SPDX-License-Identifier: GPL-3.0-only
//! The launcher folder and the paths under it.

use scl_contract::{files, ProductConfig};
use std::path::{Path, PathBuf};

/// Every path a launcher uses, built from the launcher folder and the product config.
#[derive(Debug, Clone)]
pub struct Layout {
    pub product: ProductConfig,
    /// The launcher folder.
    pub root: PathBuf,
    /// The product ini inside it.
    pub ini: PathBuf,
    /// The data folder inside it.
    pub data: PathBuf,
    /// `<data>/plugins`
    pub plugins: PathBuf,
    /// `<data>/storage`
    pub storage: PathBuf,
    /// `<data>/mod.log`: the host log the game writes.
    pub mod_log: PathBuf,
}

impl Layout {
    /// Paths under `root`. Nothing is created and nothing has to exist.
    pub fn new(product: ProductConfig, root: impl Into<PathBuf>) -> Layout {
        let root = root.into();
        let data = root.join(product.data_dir);
        Layout {
            product,
            ini: root.join(product.ini_file),
            plugins: data.join(files::PLUGINS_DIR),
            storage: data.join(files::STORAGE_DIR),
            mod_log: data.join("mod.log"),
            data,
            root,
        }
    }

    /// Finds the launcher folder: `explicit` (the `--root` option), else the variable
    /// `<env_prefix>_ROOT`, else `exe_dir`. Pure: the caller passes what it read.
    pub fn discover(
        product: ProductConfig,
        explicit: Option<&Path>,
        env_root: Option<&str>,
        exe_dir: &Path,
    ) -> Layout {
        let root = match (explicit, env_root) {
            (Some(p), _) => p.to_path_buf(),
            (None, Some(e)) if !e.trim().is_empty() => PathBuf::from(e.trim()),
            _ => exe_dir.to_path_buf(),
        };
        Layout::new(product, root)
    }

    /// The folder of plugin `id`.
    pub fn plugin_dir(&self, id: &str) -> PathBuf {
        self.plugins.join(id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use scl_contract::REFERENCE_PRODUCT;

    #[test]
    fn paths_follow_the_product_config() {
        let l = Layout::new(REFERENCE_PRODUCT, "R");
        assert_eq!(l.ini, Path::new("R").join("reference-launcher.ini"));
        assert_eq!(l.plugins, Path::new("R").join("data").join("plugins"));
        assert_eq!(l.plugin_dir("a"), Path::new("R/data/plugins/a"));
    }

    #[test]
    fn discovery_order_is_option_then_env_then_exe() {
        let p = REFERENCE_PRODUCT;
        let exe = Path::new("E");
        assert_eq!(
            Layout::discover(p, Some(Path::new("X")), Some("Y"), exe).root,
            Path::new("X")
        );
        assert_eq!(Layout::discover(p, None, Some("Y"), exe).root, Path::new("Y"));
        assert_eq!(Layout::discover(p, None, Some("  "), exe).root, Path::new("E"));
        assert_eq!(Layout::discover(p, None, None, exe).root, Path::new("E"));
    }
}
