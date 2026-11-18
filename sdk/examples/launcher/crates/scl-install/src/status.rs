// SPDX-License-Identifier: GPL-3.0-only
//! The read-only status report: what the launcher folder holds and what the settings say.
//! Writes nothing, creates nothing, never touches the network.

use crate::layout::Layout;
use crate::plugins;
use scl_config::{Capabilities, Config};
use scl_contract::capability_env::SWITCHES;

/// What `status` reports.
#[derive(Debug, Clone)]
pub struct Status {
    pub product: String,
    pub version: String,
    pub root: String,
    pub ini_found: bool,
    pub data_found: bool,
    pub plugins_setting_on: bool,
    pub plugins_enabled: usize,
    pub plugins_total: usize,
    pub config: Config,
}

/// Reads the folder. A missing ini or data folder is a state, not an error.
pub fn collect(layout: &Layout) -> Status {
    let cfg = Config::read(&layout.ini);
    let rows = plugins::list(layout);
    Status {
        product: layout.product.name.to_string(),
        version: layout.product.version.to_string(),
        root: layout.root.display().to_string(),
        ini_found: cfg.is_some(),
        data_found: layout.data.is_dir(),
        plugins_setting_on: cfg.as_ref().map(|c| c.plugins).unwrap_or(true),
        plugins_enabled: rows.iter().filter(|r| r.enabled).count(),
        plugins_total: rows.len(),
        config: cfg.unwrap_or_default(),
    }
}

/// The text `status` prints.
pub fn render(s: &Status) -> String {
    let yn = |b: bool| if b { "yes" } else { "no" };
    let mut t = String::new();
    t.push_str(&format!("{} {}\n", s.product, s.version));
    t.push_str(&format!("folder:        {}\n", s.root));
    t.push_str(&format!("settings file: {}\n", if s.ini_found { "found" } else { "not found (defaults apply)" }));
    t.push_str(&format!("data folder:   {}\n", yn(s.data_found)));
    t.push_str(&format!(
        "plugins:       setting {}, {} enabled of {} installed\n",
        if s.plugins_setting_on { "on" } else { "off" },
        s.plugins_enabled,
        s.plugins_total
    ));
    let on = SWITCHES
        .iter()
        .filter(|w| s.config.capabilities.is_on(w))
        .count();
    t.push_str(&format!(
        "capabilities:  {on} of {} switches on (see `capabilities`). {}\n",
        SWITCHES.len(),
        scl_contract::capability_env::NOT_RUN_IN_GAME
    ));
    for w in &s.config.warnings {
        t.push_str(&format!("warning: {w}\n"));
    }
    t
}

/// The table `capabilities` prints: one line per switch, ini key, state, variable, evidence.
pub fn render_capabilities(caps: &Capabilities) -> String {
    let mut t = String::new();
    for w in &SWITCHES {
        t.push_str(&format!(
            "{:<22} {:<3}  {}  {}\n",
            w.ini_key,
            if caps.is_on(w) { "on" } else { "off" },
            w.env,
            Capabilities::status_text(w)
        ));
    }
    t
}

#[cfg(test)]
mod tests {
    use super::*;
    use scl_contract::REFERENCE_PRODUCT;

    #[test]
    fn empty_folder_reports_defaults() {
        let l = Layout::new(
            REFERENCE_PRODUCT,
            std::env::temp_dir().join("scl-status-does-not-exist"),
        );
        let s = collect(&l);
        assert!(!s.ini_found && !s.data_found && s.plugins_setting_on);
        assert_eq!(s.plugins_total, 0);
        let text = render(&s);
        assert!(text.contains("5 of 6 switches on"));
        assert!(text.contains("Not run in game."));
    }

    #[test]
    fn capability_table_lists_every_switch() {
        let t = render_capabilities(&Capabilities::default());
        assert_eq!(t.lines().count(), 6);
        assert!(t.contains("exp_entities_diag"));
        assert!(t.lines().last().unwrap().contains(" off "));
    }
}
