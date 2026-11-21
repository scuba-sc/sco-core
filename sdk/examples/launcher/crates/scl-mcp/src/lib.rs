// SPDX-License-Identifier: GPL-3.0-only
//! The read-only MCP server behind the reference launcher's `mcp` command.
//!
//! JSON-RPC 2.0 over stdio, one message per line, four tools (`get_status`, `list_plugins`,
//! `read_log`, `get_settings`) and no command, console, start or stop, install or settings write.
//!
//! The protocol code never opens a file: a [`Platform`] supplies the status fields, plugin folders,
//! log tails and ini text. [`host::HostPlatform`] reads a real launcher folder through `scl-install`.
//! Redaction is `scl-redact`.
//!
//! Nothing here was run in game; the checks are synthetic.

#![forbid(unsafe_code)]

pub mod json;
mod run;
pub mod server;
pub mod tools;

pub mod host;

pub use run::{run_in, run_with, Io, USAGE};
pub use server::{
    folder_less, serve, IniRead, LogRead, LogScan, Platform, PluginFolder, Server, StatusInfo,
    ToolResult,
};
