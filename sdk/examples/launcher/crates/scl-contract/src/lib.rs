// SPDX-License-Identifier: GPL-3.0-only
//! The launcher-to-host contract, with nothing product specific in it.
//!
//! Everything a product chooses (its name, file names, environment prefix, default packs and update
//! source) lives in one [`ProductConfig`]. Everything the sco-core host kit fixes (the capability
//! switch names, the plugin folder rules, the settings key format) is a constant here. The
//! contract is written out in `docs/launcher-contract.md`.
//!
//! Not run in game. These are source-level constants checked by unit tests only.

/// What one product decides. Pass it to every function that builds a name or a path.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ProductConfig {
    /// Shown to the player, for example in `status`.
    pub name: &'static str,
    /// Lower case words and dashes. Prefix of the named events: `Local\<slug>-stop-<pid>`.
    pub slug: &'static str,
    /// Upper case words and underscores. Prefix of the product's environment variables.
    pub env_prefix: &'static str,
    /// Settings file next to the launcher, for example `my-launcher.ini`.
    pub ini_file: &'static str,
    /// Data folder next to the launcher. Holds `plugins/`, `storage/`, logs.
    pub data_dir: &'static str,
    /// Plugin folder names a fresh install is expected to carry (informational, shown by `status`).
    pub default_packs: &'static [&'static str],
    /// Where a product's own updater would look. The reference launcher never contacts it.
    pub update_source: &'static str,
    /// Version text of the launcher.
    pub version: &'static str,
}

/// The product the reference launcher ships as. Replace it, or build your own `ProductConfig`.
pub const REFERENCE_PRODUCT: ProductConfig = ProductConfig {
    name: "SCO reference launcher",
    slug: "sco-reference",
    env_prefix: "SCO_REF",
    ini_file: "reference-launcher.ini",
    data_dir: "data",
    default_packs: &[],
    update_source: "",
    version: env!("CARGO_PKG_VERSION"),
};

impl ProductConfig {
    /// `<env_prefix>_<suffix>`: a variable of this product.
    pub fn env_name(&self, suffix: &str) -> String {
        format!("{}_{suffix}", self.env_prefix)
    }

    /// `Local\<slug>-stop-<pid>`. Launcher to mod: run the exit work now.
    pub fn stop_event_name(&self, pid: u32) -> String {
        stop_channel::event_name(self.slug, stop_channel::STOP_KIND, pid)
    }

    /// `Local\<slug>-stopped-<pid>`. Mod to launcher: the exit work has run.
    pub fn stopped_event_name(&self, pid: u32) -> String {
        stop_channel::event_name(self.slug, stop_channel::STOPPED_KIND, pid)
    }

    /// `Local\<slug>-ready-<launcher pid>`. A launcher and its own helper process: the helper
    /// signals when its work is done. A product detail; the host kit never opens it.
    pub fn ready_event_name(&self, launcher_pid: u32) -> String {
        stop_channel::event_name(self.slug, stop_channel::READY_KIND, launcher_pid)
    }

    /// The three variables of the launcher-owned block, in order (see [`env_block`]).
    pub fn env_names(&self) -> [String; 3] {
        [
            self.env_name(env_block::PLUGINS),
            self.env_name(env_block::DATA),
            self.env_name(env_block::MOD_LOG),
        ]
    }
}

/// The product-owned part of the game environment. The host kit reads the capability switches
/// ([`capability_env`]); how a product passes the plugin switch, the data root and the log path
/// to its own `dinput8.dll` is the product's choice. The reference launcher uses these three.
pub mod env_block {
    /// `on` or `off`: load plugins from the plugin folder.
    pub const PLUGINS: &str = "PLUGINS";
    /// Absolute path of the data folder.
    pub const DATA: &str = "DATA";
    /// Absolute path of the host log file.
    pub const MOD_LOG: &str = "MOD_LOG";
}

/// Capability switches the sco-core host reads from its process environment.
///
/// A switch is on only when the variable is exactly `1`. A launcher that turns a switch off removes
/// the variable from the child environment, so a value set in the player's shell cannot leak in.
/// Owner decision 2026-11-17: every capability switch is on by default and is a visible setting the
/// player can turn off. The one diagnostic switch is off by default.
pub mod capability_env {
    pub const LOADOUT_ASYNC: &str = "SCO_LOADOUT_ASYNC";
    pub const INVENTORY_READ: &str = "SCO_INVENTORY_READ";
    pub const ACTOR_STATS_WRITE: &str = "SCO_ACTOR_STATS_WRITE";
    pub const CVARS_NATIVE: &str = "SCO_CVARS_NATIVE";
    pub const AUDIO_NATIVE: &str = "SCO_AUDIO_NATIVE";
    /// Diagnostic: hooks that patch game functions to write log lines. Not a capability.
    pub const ENTITIES_DIAG: &str = "SCO_ENTITIES_DIAG";

    /// The exact value that turns a switch on.
    pub const ON_VALUE: &str = "1";

    /// One launcher-visible capability switch.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct Switch {
        /// Environment variable the sco-core host reads.
        pub env: &'static str,
        /// Key in the product ini: `exp_` and the variable name without `SCO_`, in lower case.
        pub ini_key: &'static str,
        /// What the switch turns on. Not a capability for the diagnostic switch.
        pub capability: &'static str,
        /// Evidence level from the SDK status registry. No switch is `in_game_verified`.
        pub evidence: &'static str,
        /// True for the one diagnostic switch.
        pub diagnostic_only: bool,
        /// Release default: true for every capability switch, false for the diagnostic one.
        pub default_on: bool,
    }

    /// Every switch a launcher shows, in this order.
    pub const SWITCHES: [Switch; 6] = [
        Switch {
            env: LOADOUT_ASYNC,
            ini_key: "exp_loadout_async",
            capability: "game.loadout.async",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: INVENTORY_READ,
            ini_key: "exp_inventory_read",
            capability: "game.inventory.read",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: ACTOR_STATS_WRITE,
            ini_key: "exp_actor_stats_write",
            capability: "game.actor_stats.write",
            evidence: "off_pending_probe",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: CVARS_NATIVE,
            ini_key: "exp_cvars_native",
            capability: "game.cvars.read adapter",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: AUDIO_NATIVE,
            ini_key: "exp_audio_native",
            capability: "audio native adapter attempt",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: ENTITIES_DIAG,
            ini_key: "exp_entities_diag",
            capability: "diagnostic hooks that patch game functions to log, not a capability",
            evidence: "not a capability",
            diagnostic_only: true,
            default_on: false,
        },
    ];

    /// The sentence every switch carries in a UI and in the docs.
    pub const NOT_RUN_IN_GAME: &str = "Not run in game.";
}

/// Launcher process exit codes of the reference launcher.
pub mod exit_code {
    pub const OK: i32 = 0;
    pub const ERROR: i32 = 1;
    /// The command line was wrong.
    pub const USAGE: i32 = 2;
}

/// Names of the stop channel events.
///
/// The mod (the host in the game process) creates two events scoped to its own process, in the
/// session-local namespace with the default security descriptor; the launcher opens them by the
/// game's process id. The stop event exists only in a process where the host is loaded.
pub mod stop_channel {
    pub const NAMESPACE: &str = "Local\\";
    /// Launcher to mod: run the exit work now (manual reset, stays signalled).
    pub const STOP_KIND: &str = "stop";
    /// Mod to launcher: the exit work has run, or there is nothing left to run.
    pub const STOPPED_KIND: &str = "stopped";
    /// Launcher to its own helper process (product detail).
    pub const READY_KIND: &str = "ready";
    /// The event name buffer of the native host is 64 wide characters, terminator included.
    pub const MAX_NAME_CHARS: usize = 63;

    /// `Local\<slug>-<kind>-<pid>`.
    pub fn event_name(slug: &str, kind: &str, pid: u32) -> String {
        format!("{NAMESPACE}{slug}-{kind}-{pid}")
    }
}

/// File and folder names the plugin folder rules fix.
pub mod files {
    /// Per-plugin marker that keeps a plugin disabled, in `<data>/plugins/<id>/`. A file or a
    /// folder of this name counts.
    pub const PLUGIN_DISABLED_MARKER: &str = "disabled";
    /// A plugin folder is a folder that holds this file.
    pub const PLUGIN_MANIFEST: &str = "plugin.ini";
    /// Name of the plugins folder inside the data folder.
    pub const PLUGINS_DIR: &str = "plugins";
    /// Where each plugin's key-value database lives, inside the data folder.
    pub const STORAGE_DIR: &str = "storage";
    /// Historical record of one plugin load pass, written by the host when asked.
    pub const PLUGIN_STARTUP: &str = "plugin-startup.json";
    /// Ids a plugin folder may not use.
    pub const RESERVED_IDS: [&str; 4] = ["sco", "host", "menu", "game"];
}

/// The plugin API of the sco-core release this launcher was written against (`SCO_API_MAJOR` and
/// `SCO_API_MINOR` of `sco_api.h`). A package built for another major, or a newer minor, is not
/// installed.
pub mod plugin_api {
    pub const MAJOR: u16 = 1;
    pub const MINOR: u16 = 1;
}

/// How a plugin's `[settings]` values are kept in `data/storage/<id>.db`, table `sco_kv`.
pub mod settings_store {
    /// Key prefix: `sco.settings.<name>`.
    pub const KEY_PREFIX: &str = "sco.settings.";

    /// The `sco_kv` key of a setting.
    pub fn key(name: &str) -> String {
        format!("{KEY_PREFIX}{name}")
    }

    /// The `sco_kv` value: `<type>:<canonical text>`. The database column holds it as a blob.
    pub fn value(setting_type: &str, canonical: &str) -> String {
        format!("{setting_type}:{canonical}")
    }

    /// Splits a stored value at the first colon into type and text.
    pub fn split_value(stored: &str) -> Option<(&str, &str)> {
        stored.split_once(':')
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn event_names_follow_the_slug() {
        let p = REFERENCE_PRODUCT;
        assert_eq!(p.stop_event_name(1234), "Local\\sco-reference-stop-1234");
        assert_eq!(
            p.stopped_event_name(1234),
            "Local\\sco-reference-stopped-1234"
        );
        assert_eq!(p.ready_event_name(7), "Local\\sco-reference-ready-7");
    }

    #[test]
    fn event_names_fit_the_host_buffer() {
        let n = REFERENCE_PRODUCT.stopped_event_name(u32::MAX);
        assert!(n.chars().count() <= stop_channel::MAX_NAME_CHARS);
    }

    #[test]
    fn env_names_use_the_prefix() {
        assert_eq!(
            REFERENCE_PRODUCT.env_names(),
            ["SCO_REF_PLUGINS", "SCO_REF_DATA", "SCO_REF_MOD_LOG"]
        );
    }

    #[test]
    fn switches_follow_the_naming_rule_and_the_defaults() {
        use capability_env::*;
        assert_eq!(ON_VALUE, "1");
        for s in &SWITCHES {
            let want = format!("exp_{}", s.env.strip_prefix("SCO_").unwrap().to_lowercase());
            assert_eq!(s.ini_key, want);
            assert_eq!(s.default_on, !s.diagnostic_only, "{}", s.env);
        }
        assert_eq!(SWITCHES.len(), 6);
    }

    #[test]
    fn settings_keys_round_trip() {
        assert_eq!(settings_store::key("volume"), "sco.settings.volume");
        let v = settings_store::value("int", "5");
        assert_eq!(settings_store::split_value(&v), Some(("int", "5")));
    }
}
