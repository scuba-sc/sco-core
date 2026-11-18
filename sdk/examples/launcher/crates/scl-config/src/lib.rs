// SPDX-License-Identifier: GPL-3.0-only
//! Settings of a launcher: the product ini (read, edit in place), the capability switches, the
//! environment block the game process gets, and the `plugin.ini` reader.
//!
//! Synthetic evidence only. Not run in game.

pub mod capabilities;
pub mod cfmt;
pub mod config;
pub mod env;
pub mod ini_edit;
pub mod plugin_ini;

pub use capabilities::Capabilities;
pub use config::{parse_on_off, Config};
pub use env::{apply_capabilities, game_env, render, CapabilityApply, EnvVar};
