// SPDX-License-Identifier: GPL-3.0-only
//! The command line of the SDK reference launcher.
//!
//! ```text
//! status
//! plugins list | enable <id> | disable <id> | install-from-zip <zip> [--replace] [--disabled]
//! capabilities [env | enable <key> | disable <key>]
//! mcp
//! help
//! ```
//!
//! What it does not do, on purpose: start the game, inject anything, change the PC (firewall,
//! hosts file, registry, anti-cheat files), reach the network, or update itself. Those are product
//! duties; see `docs/building-a-launcher.md`. Not run in game.
//!
//! The whole command line is [`run`]: it takes the product, the arguments, the folder facts and
//! the three streams, so tests drive it without a process.

use scl_config::{env as cenv, ini_edit, Capabilities, Config};
use scl_contract::{exit_code, ProductConfig};
use scl_install::{plugins, status, Layout};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

/// The three streams.
pub struct Io<'a> {
    pub stdin: &'a mut dyn Read,
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
}

/// What the process knows about where it runs.
pub struct Host<'a> {
    /// The folder of the executable.
    pub exe_dir: &'a Path,
    /// The value of `<env_prefix>_ROOT`, when set.
    pub env_root: Option<&'a str>,
}

/// The usage text for `product`.
pub fn usage(product: &ProductConfig) -> String {
    let exe = "sco-reference-launcher";
    format!(
        "{name} {version}\n\
\n\
usage: {exe} [--root <folder>] <command>\n\
\n\
  status                         what the folder holds and what the settings say; read-only\n\
  plugins list                   plugin folders and whether each is enabled\n\
  plugins enable <id>            remove the plugin's disabled marker\n\
  plugins disable <id>           create the plugin's disabled marker\n\
  plugins install-from-zip <zip> [--replace] [--disabled]\n\
                                 verify a plugin zip, then install it into the plugin folder\n\
  capabilities                   the capability switches and their state\n\
  capabilities env               the environment block a host would get\n\
  capabilities enable <key>      set an exp_* key to on in the settings file\n\
  capabilities disable <key>     set an exp_* key to off in the settings file\n\
  mcp                            read-only MCP server on stdin and stdout for AI agents\n\
  help                           show this\n\
\n\
  --root <folder>   the launcher folder (else {prefix}_ROOT, else the folder of this program)\n\
\n\
This launcher does not start the game, inject, change the PC, use the network or update itself.\n\
Nothing here has been run in game.\n\
exit codes: 0 ok, 1 error, 2 wrong command line\n",
        name = product.name,
        version = product.version,
        prefix = product.env_prefix,
    )
}

fn say(io: &mut Io<'_>, text: &str) {
    let _ = io.stdout.write_all(text.as_bytes());
}

fn fail(io: &mut Io<'_>, text: &str) -> i32 {
    let _ = writeln!(io.stderr, "[!] {text}");
    exit_code::ERROR
}

fn usage_error(io: &mut Io<'_>, product: &ProductConfig, text: &str) -> i32 {
    let _ = write!(io.stderr, "{}[!] {text}\n", usage(product));
    exit_code::USAGE
}

/// Runs one command line. `args` excludes the program name.
pub fn run(product: &ProductConfig, host: &Host<'_>, args: &[String], io: &mut Io<'_>) -> i32 {
    // The one global option.
    let mut rest: Vec<&str> = Vec::new();
    let mut root: Option<PathBuf> = None;
    let mut i = 0;
    while i < args.len() {
        if args[i] == "--root" {
            match args.get(i + 1) {
                Some(v) => root = Some(PathBuf::from(v)),
                None => return usage_error(io, product, "--root needs a folder"),
            }
            i += 2;
        } else {
            rest.push(args[i].as_str());
            i += 1;
        }
    }
    let layout = Layout::discover(*product, root.as_deref(), host.env_root, host.exe_dir);

    let Some((&cmd, tail)) = rest.split_first() else {
        say(io, &usage(product));
        return exit_code::OK;
    };
    match cmd.to_ascii_lowercase().as_str() {
        "help" | "--help" | "-h" | "/?" => {
            say(io, &usage(product));
            exit_code::OK
        }
        "status" => {
            if let Some(extra) = tail.first() {
                return usage_error(io, product, &format!("unknown argument '{extra}'"));
            }
            say(io, &status::render(&status::collect(&layout)));
            exit_code::OK
        }
        "plugins" => cmd_plugins(product, &layout, tail, io),
        "capabilities" => cmd_capabilities(product, &layout, tail, io),
        "mcp" => {
            let rest: Vec<String> = tail.iter().map(|s| s.to_string()).collect();
            let mut mio = scl_mcp::Io {
                stdin: &mut *io.stdin,
                stdout: &mut *io.stdout,
                stderr: &mut *io.stderr,
            };
            scl_mcp::run_in(&layout, &rest, &mut mio)
        }
        other => usage_error(io, product, &format!("unknown command '{other}'")),
    }
}

fn cmd_plugins(product: &ProductConfig, layout: &Layout, tail: &[&str], io: &mut Io<'_>) -> i32 {
    let Some((&sub, args)) = tail.split_first() else {
        return usage_error(io, product, "plugins needs list, enable, disable or install-from-zip");
    };
    match sub.to_ascii_lowercase().as_str() {
        "list" => {
            if let Some(extra) = args.first() {
                return usage_error(io, product, &format!("unknown argument '{extra}'"));
            }
            let rows = plugins::list(layout);
            if rows.is_empty() {
                say(io, &format!("no plugins in {}\n", layout.plugins.display()));
            }
            for r in rows {
                say(
                    io,
                    &format!(
                        "{:<24} {:<8} {:<6} {:<10} {}\n",
                        r.id,
                        if r.enabled { "enabled" } else { "disabled" },
                        if r.kind.is_empty() { "-" } else { &r.kind },
                        if r.version.is_empty() { "-" } else { &r.version },
                        r.name
                    ),
                );
            }
            exit_code::OK
        }
        "enable" | "disable" => {
            let on = sub.eq_ignore_ascii_case("enable");
            let [id] = args else {
                return usage_error(io, product, &format!("plugins {sub} needs one plugin id"));
            };
            match plugins::set_enabled(layout, id, on) {
                Ok(true) => {
                    say(io, &format!("{id}: {}\n", if on { "enabled" } else { "disabled" }));
                    exit_code::OK
                }
                Ok(false) => {
                    say(
                        io,
                        &format!("{id}: already {}\n", if on { "enabled" } else { "disabled" }),
                    );
                    exit_code::OK
                }
                Err(e) => fail(io, &e),
            }
        }
        "install-from-zip" | "install" => {
            let mut zip: Option<&str> = None;
            let mut opts = plugins::InstallOptions::default();
            for a in args {
                match *a {
                    "--replace" => opts.replace = true,
                    "--disabled" => opts.start_disabled = true,
                    s if s.starts_with("--") => {
                        return usage_error(io, product, &format!("unknown option '{s}'"))
                    }
                    s if zip.is_none() => zip = Some(s),
                    s => return usage_error(io, product, &format!("unexpected argument '{s}'")),
                }
            }
            let Some(zip) = zip else {
                return usage_error(io, product, "plugins install-from-zip needs a zip file");
            };
            match plugins::install_from_zip(layout, Path::new(zip), opts) {
                Ok(r) => {
                    say(
                        io,
                        &format!(
                            "installed {} {} ({}, {} files, {} bytes){}{}\narchive sha256: {}\n",
                            r.id,
                            r.version,
                            r.kind,
                            r.files,
                            r.bytes,
                            if r.replaced { ", replaced the old copy" } else { "" },
                            if r.disabled { ", disabled" } else { "" },
                            r.archive_sha256
                        ),
                    );
                    exit_code::OK
                }
                Err(e) => fail(io, &format!("not installed: {e}")),
            }
        }
        other => usage_error(io, product, &format!("unknown plugins command '{other}'")),
    }
}

fn cmd_capabilities(
    product: &ProductConfig,
    layout: &Layout,
    tail: &[&str],
    io: &mut Io<'_>,
) -> i32 {
    let cfg = Config::read(&layout.ini).unwrap_or_default();
    match tail {
        [] => {
            say(io, &status::render_capabilities(&cfg.capabilities));
            exit_code::OK
        }
        ["env"] => {
            let block = cenv::game_env(
                &layout.product,
                &cfg,
                &layout.data.display().to_string(),
                &layout.mod_log.display().to_string(),
            );
            say(io, &cenv::render(&block));
            exit_code::OK
        }
        [verb @ ("enable" | "disable"), key] => {
            let Some(sw) = Capabilities::switch_for_key(key) else {
                return fail(io, &format!("'{key}' is not a capability key (run `capabilities`)"));
            };
            let value = if *verb == "enable" { "on" } else { "off" };
            if !ini_edit::set_ini_value(&layout.ini, sw.ini_key, value) {
                return fail(io, &format!("cannot write {}", layout.ini.display()));
            }
            say(io, &format!("{} = {value}\n", sw.ini_key));
            exit_code::OK
        }
        _ => usage_error(io, product, "capabilities takes env, enable <key> or disable <key>"),
    }
}
