// SPDX-License-Identifier: GPL-3.0-only
//! The environment block a launcher sets for the game process.
//!
//! [`game_env`] is the product block (plugins on or off, data root, log file) followed by the
//! capability switches. An empty value means the variable is removed from the child's
//! environment, so a value the player set in their shell does not leak through.
//!
//! This crate only builds the list. Starting the game and passing the list to it is the
//! product's job; the reference launcher never does either.

use crate::capabilities::Capabilities;
use crate::config::Config;
use scl_contract::capability_env::{ON_VALUE, SWITCHES};
use scl_contract::{env_block, ProductConfig};

/// One variable for the game process. An empty `value` means "remove it".
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnvVar {
    pub name: String,
    pub value: String,
}

impl EnvVar {
    pub fn is_unset(&self) -> bool {
        self.value.is_empty()
    }
}

fn var(name: impl Into<String>, value: impl Into<String>) -> EnvVar {
    EnvVar {
        name: name.into(),
        value: value.into(),
    }
}

/// The capability switches: `1` for each one that is on, removed for each one that is off.
pub fn capability_env(caps: &Capabilities) -> Vec<EnvVar> {
    caps.env()
        .into_iter()
        .map(|(name, value)| var(name, value))
        .collect()
}

/// The result of applying the switches to a process that may already carry some of the variables.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CapabilityApply {
    /// `1` for each switch that is on, an empty value (remove it) for each one that is off.
    pub env: Vec<EnvVar>,
    /// The switches that are on: `exp_loadout_async (SCO_LOADOUT_ASYNC=1), ...`.
    pub on_summary: String,
    /// Variables present in the inherited environment although their key is off, comma separated.
    pub removed: String,
}

/// The capability block with the two texts a launcher prints. `inherited` says whether a variable
/// is present in the environment the launcher itself runs in.
pub fn apply_capabilities(
    caps: &Capabilities,
    inherited: &dyn Fn(&str) -> bool,
) -> CapabilityApply {
    let mut on_summary = String::new();
    let mut removed = String::new();
    for s in &SWITCHES {
        if caps.is_on(s) {
            if !on_summary.is_empty() {
                on_summary.push_str(", ");
            }
            on_summary.push_str(&format!("{} ({}={})", s.ini_key, s.env, ON_VALUE));
        } else if inherited(s.env) {
            if !removed.is_empty() {
                removed.push_str(", ");
            }
            removed.push_str(s.env);
        }
    }
    CapabilityApply {
        env: capability_env(caps),
        on_summary,
        removed,
    }
}

/// The product block and then the capability block. `data` is the data folder and `mod_log` the
/// host log path, both absolute.
pub fn game_env(product: &ProductConfig, cfg: &Config, data: &str, mod_log: &str) -> Vec<EnvVar> {
    let mut v = vec![
        var(
            product.env_name(env_block::PLUGINS),
            if cfg.plugins { "on" } else { "off" },
        ),
        var(product.env_name(env_block::DATA), data),
        var(product.env_name(env_block::MOD_LOG), mod_log),
    ];
    v.extend(capability_env(&cfg.capabilities));
    v
}

/// Text of a block: one `NAME=value` line each, `<unset>` for an empty value.
pub fn render(block: &[EnvVar]) -> String {
    block
        .iter()
        .map(|e| {
            format!(
                "{}={}\n",
                e.name,
                if e.is_unset() {
                    "<unset>"
                } else {
                    e.value.as_str()
                }
            )
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use scl_contract::REFERENCE_PRODUCT;

    #[test]
    fn default_block_golden() {
        let v = game_env(&REFERENCE_PRODUCT, &Config::default(), "C:/d", "C:/d/mod.log");
        assert_eq!(
            render(&v),
            "SCO_REF_PLUGINS=on\nSCO_REF_DATA=C:/d\nSCO_REF_MOD_LOG=C:/d/mod.log\n\
             SCO_LOADOUT_ASYNC=1\nSCO_INVENTORY_READ=1\nSCO_ACTOR_STATS_WRITE=1\n\
             SCO_CVARS_NATIVE=1\nSCO_AUDIO_NATIVE=1\nSCO_ENTITIES_DIAG=<unset>\n"
        );
    }

    #[test]
    fn off_switch_removes_an_inherited_value() {
        let cfg = Config::parse(b"exp_audio_native = off\n");
        let a = apply_capabilities(&cfg.capabilities, &|n| n == "SCO_AUDIO_NATIVE");
        assert_eq!(a.removed, "SCO_AUDIO_NATIVE");
        assert!(a.on_summary.starts_with("exp_loadout_async (SCO_LOADOUT_ASYNC=1)"));
        assert!(!a.on_summary.contains("audio"));
    }
}
