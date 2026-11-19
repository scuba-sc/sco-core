// SPDX-License-Identifier: GPL-3.0-only
//! `sco-reference-launcher`: see the `scl_cli` library for the commands.

use scl_cli::{run, Host, Io};
use scl_contract::REFERENCE_PRODUCT;
use std::io::{stderr, stdin, stdout};
use std::path::PathBuf;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."));
    let env_root = std::env::var(REFERENCE_PRODUCT.env_name("ROOT")).ok();
    let host = Host {
        exe_dir: &exe_dir,
        env_root: env_root.as_deref(),
    };
    let (mut i, mut o, mut e) = (stdin(), stdout(), stderr());
    let mut io = Io {
        stdin: &mut i,
        stdout: &mut o,
        stderr: &mut e,
    };
    let code = run(&REFERENCE_PRODUCT, &host, &args, &mut io);
    std::process::exit(code);
}
