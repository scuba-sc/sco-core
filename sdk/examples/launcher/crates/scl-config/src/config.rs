// SPDX-License-Identifier: GPL-3.0-only
//! The product ini: `key = value` lines. A line that starts with `;` or `#` is a comment, a
//! `[section]` line is skipped, the last value of a key wins, and keys are matched without regard
//! to ASCII case. Values may be wrapped in double quotes.
//!
//! The reference launcher reads two kinds of key: `plugins` (on or off, default on) and the
//! `exp_*` capability switches. Any other key is kept in [`Config::other`] so a product can read its
//! own settings from the same file.

use crate::capabilities::Capabilities;
use std::path::Path;

/// What the ini says.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Config {
    /// Load plugins from the plugin folder. Default on.
    pub plugins: bool,
    /// The capability switches, with the release defaults for any key the file leaves out.
    pub capabilities: Capabilities,
    /// Keys this crate does not know, in file order, lower case key and trimmed value.
    pub other: Vec<(String, String)>,
    /// One line per refused line or value, for `status` to print.
    pub warnings: Vec<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            plugins: true,
            capabilities: Capabilities::default(),
            other: Vec::new(),
            warnings: Vec::new(),
        }
    }
}

/// Space, tab, CR, LF and double quote, from both ends.
pub fn trim(s: &str) -> &str {
    s.trim_matches(|c| matches!(c, ' ' | '\t' | '\r' | '\n' | '"'))
}

/// 1, on, yes, true or 0, off, no, false, without regard to case.
pub fn parse_on_off(v: &str) -> Option<bool> {
    const ON: [&str; 4] = ["1", "on", "yes", "true"];
    const OFF: [&str; 4] = ["0", "off", "no", "false"];
    if ON.iter().any(|w| v.eq_ignore_ascii_case(w)) {
        Some(true)
    } else if OFF.iter().any(|w| v.eq_ignore_ascii_case(w)) {
        Some(false)
    } else {
        None
    }
}

impl Config {
    /// Parses the bytes of an ini file. Never fails: a bad line becomes a warning.
    pub fn parse(bytes: &[u8]) -> Config {
        let bytes = bytes.strip_prefix(b"\xEF\xBB\xBF").unwrap_or(bytes);
        let text = String::from_utf8_lossy(bytes);
        let mut c = Config::default();
        for (n, raw) in text.lines().enumerate() {
            let line = trim(raw);
            if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
                continue;
            }
            if line.starts_with('[') {
                continue;
            }
            let Some((k, v)) = line.split_once('=') else {
                c.warnings.push(format!("line {}: no '=' (ignored)", n + 1));
                continue;
            };
            let key = trim(k).to_ascii_lowercase();
            let value = trim(v);
            if key == "plugins" {
                match parse_on_off(value) {
                    Some(b) => c.plugins = b,
                    None => c
                        .warnings
                        .push(format!("line {}: plugins = '{value}' is not on or off", n + 1)),
                }
            } else if let Some(sw) = Capabilities::switch_for_key(&key) {
                match parse_on_off(value) {
                    Some(b) => c.capabilities.set(sw, b),
                    None => c.warnings.push(format!(
                        "line {}: {key} = '{value}' is not on or off",
                        n + 1
                    )),
                }
            } else if key.starts_with("exp_") {
                c.warnings
                    .push(format!("line {}: unknown capability key '{key}'", n + 1));
            } else {
                c.other.push((key, value.to_string()));
            }
        }
        c
    }

    /// Reads the file. `None` when it does not exist or cannot be read; the defaults then apply.
    pub fn read(path: &Path) -> Option<Config> {
        std::fs::read(path).ok().map(|b| Config::parse(&b))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use scl_contract::capability_env::SWITCHES;

    #[test]
    fn defaults_are_five_on_and_the_diagnostic_off() {
        let c = Config::parse(b"");
        assert!(c.plugins);
        for s in &SWITCHES {
            assert_eq!(c.capabilities.is_on(s), !s.diagnostic_only, "{}", s.env);
        }
    }

    #[test]
    fn keys_values_comments_and_last_wins() {
        let c = Config::parse(
            b"\xEF\xBB\xBF; note\r\n[section]\r\nPlugins = off\r\nexp_cvars_native = 0\r\nexp_cvars_native = no\r\nexp_entities_diag = \"on\"\r\nname = x\r\n",
        );
        assert!(!c.plugins);
        let cv = Capabilities::switch_for_key("EXP_CVARS_NATIVE").unwrap();
        assert!(!c.capabilities.is_on(cv));
        let d = Capabilities::switch_for_key("exp_entities_diag").unwrap();
        assert!(c.capabilities.is_on(d));
        assert_eq!(c.other, vec![("name".to_string(), "x".to_string())]);
        assert!(c.warnings.is_empty());
    }

    #[test]
    fn bad_lines_warn_and_change_nothing() {
        let c = Config::parse(b"plugins = maybe\nno equals\nexp_unknown_thing = 1\n");
        assert!(c.plugins);
        assert_eq!(c.warnings.len(), 3);
    }
}
