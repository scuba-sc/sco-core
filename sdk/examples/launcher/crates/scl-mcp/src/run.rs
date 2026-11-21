// SPDX-License-Identifier: GPL-3.0-only
//! The `mcp` command: argument rules, the stdio loop and the exit code.
//!
//! A Rust program has no hidden writer to stdout, so the caller hands in the three streams, and
//! this code writes to `stdout` only protocol lines (and the help text of `--help`).

use std::io::{Read, Write};

use crate::server::{serve, Platform, Server};

/// Usage text of the `mcp` command.
pub const USAGE: &str = "usage: sco-reference-launcher mcp [--root <folder>]\n\
\n\
Read-only MCP server on stdin and stdout for AI agents: status, plugins, logs, settings.\n\
It never starts the game, installs, updates or writes. The game's own log is not served.\n";

/// The three streams of the process.
pub struct Io<'a> {
    pub stdin: &'a mut dyn Read,
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
}

fn is(a: &str, word: &str) -> bool {
    a.eq_ignore_ascii_case(word)
}

/// Runs `mcp <args>` over the platform that `make_platform` returns. `args` are the arguments after
/// the word `mcp`, with the global `--root` option already removed. Returns the exit code: 0 at the
/// end of input, 1 for a bad argument.
pub fn run_with(
    args: &[String],
    io: &mut Io<'_>,
    make_platform: &mut dyn FnMut() -> Box<dyn Platform>,
) -> i32 {
    for a in args {
        if is(a, "--help") || is(a, "-h") || is(a, "/?") {
            let _ = write!(io.stdout, "{USAGE}");
            let _ = io.stdout.flush();
            return 0;
        }
        let _ = writeln!(io.stderr, "{USAGE}[!] unknown argument '{a}'");
        return 1;
    }
    let platform = make_platform();
    let _ = writeln!(
        io.stderr,
        "{} mcp: read-only server on stdio, {}",
        platform.product(),
        platform.version()
    );
    let mut server = Server::new(platform);
    let Io { stdin, stdout, .. } = io;
    serve(
        &mut server,
        &mut |buf: &mut [u8]| loop {
            match stdin.read(buf) {
                Ok(n) => return n, // end of input is 0; a read error ends the session below
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => return 0,
            }
        },
        &mut |line: &[u8]| {
            let mut out = Vec::with_capacity(line.len() + 1);
            out.extend_from_slice(line);
            out.push(b'\n');
            stdout.write_all(&out).is_ok() && stdout.flush().is_ok() // the client went away: stop
        },
    );
    0
}

/// `mcp <args>` for the launcher folder `layout`.
pub fn run_in(layout: &scl_install::Layout, args: &[String], io: &mut Io<'_>) -> i32 {
    let layout = layout.clone();
    run_with(args, io, &mut || {
        Box::new(crate::host::HostPlatform::new(layout.clone()))
    })
}
