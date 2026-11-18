// SPDX-License-Identifier: GPL-3.0-only
//! The install folder of a launcher: where things live, what is installed, and the only two
//! changes the reference launcher makes to it (flip a plugin's `disabled` marker, install a
//! verified plugin zip).
//!
//! * [`layout`] finds the launcher folder and names every path under it.
//! * [`plugins`] lists plugin folders and enables, disables and installs them.
//! * [`extract`] reads the members of a zip that [`scl_plugins::verify`] already accepted.
//! * [`status`] gathers and renders the read-only status report.
//!
//! This crate never starts a process, never touches the network and never loads plugin code.
//! Not run in game.

pub mod extract;
pub mod layout;
pub mod plugins;
pub mod status;

pub use layout::Layout;
