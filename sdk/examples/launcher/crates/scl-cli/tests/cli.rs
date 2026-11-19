// SPDX-License-Identifier: GPL-3.0-only
//! The command line end to end over a scratch launcher folder, in process and through the built
//! binary. Synthetic only; the launcher starts no game. Not run in game.

use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use scl_cli::{run, Host, Io};
use scl_contract::REFERENCE_PRODUCT;

struct Out {
    code: i32,
    stdout: String,
    stderr: String,
}

fn scratch(name: &str) -> PathBuf {
    let p = std::env::temp_dir().join(format!("scl-cli-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&p);
    std::fs::create_dir_all(&p).unwrap();
    p
}

fn zip(name: &str) -> String {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/zip-corpus/cases")
        .join(format!("{name}.zip"))
        .display()
        .to_string()
}

fn cli(root: &Path, args: &[&str], input: &str) -> Out {
    let mut full: Vec<String> = vec!["--root".into(), root.display().to_string()];
    full.extend(args.iter().map(|a| a.to_string()));
    let mut stdin = Cursor::new(input.as_bytes().to_vec());
    let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
    let exe_dir = PathBuf::from("unused");
    let host = Host {
        exe_dir: &exe_dir,
        env_root: None,
    };
    let code = {
        let mut io = Io {
            stdin: &mut stdin,
            stdout: &mut stdout,
            stderr: &mut stderr,
        };
        run(&REFERENCE_PRODUCT, &host, &full, &mut io)
    };
    Out {
        code,
        stdout: String::from_utf8(stdout).unwrap(),
        stderr: String::from_utf8(stderr).unwrap(),
    }
}

#[test]
fn help_and_usage_errors() {
    let r = scratch("help");
    let o = cli(&r, &["help"], "");
    assert_eq!(o.code, 0);
    assert!(o.stdout.contains("plugins install-from-zip"));
    assert!(o.stdout.contains("does not start the game"));
    let o = cli(&r, &["frobnicate"], "");
    assert_eq!(o.code, 2);
    assert!(o.stderr.contains("unknown command 'frobnicate'"));
    let o = cli(&r, &["plugins"], "");
    assert_eq!(o.code, 2);
    let o = cli(&r, &["plugins", "enable"], "");
    assert_eq!(o.code, 2);
    let _ = std::fs::remove_dir_all(&r);
}

#[test]
fn status_on_an_empty_folder_writes_nothing() {
    let r = scratch("status");
    let o = cli(&r, &["status"], "");
    assert_eq!(o.code, 0);
    assert!(o.stdout.contains("settings file: not found (defaults apply)"));
    assert!(o.stdout.contains("0 enabled of 0 installed"));
    assert!(o.stdout.contains("5 of 6 switches on"));
    assert!(std::fs::read_dir(&r).unwrap().next().is_none(), "status wrote something");
    let _ = std::fs::remove_dir_all(&r);
}

#[test]
fn plugin_life_cycle() {
    let r = scratch("plugins");
    let o = cli(&r, &["plugins", "list"], "");
    assert_eq!(o.code, 0);
    assert!(o.stdout.starts_with("no plugins in "));

    let o = cli(&r, &["plugins", "install-from-zip", &zip("good-minimal")], "");
    assert_eq!(o.code, 0, "{}{}", o.stdout, o.stderr);
    assert!(o.stdout.contains("archive sha256: "));
    let o = cli(&r, &["plugins", "list"], "");
    assert!(o.stdout.contains("enabled"), "{}", o.stdout);
    let id = o.stdout.split_whitespace().next().unwrap().to_string();

    let o = cli(&r, &["plugins", "install-from-zip", &zip("good-minimal")], "");
    assert_eq!(o.code, 1);
    assert!(o.stderr.contains("already installed"));

    let o = cli(&r, &["plugins", "disable", &id], "");
    assert_eq!(o.code, 0);
    assert!(o.stdout.contains("disabled"));
    let o = cli(&r, &["plugins", "disable", &id], "");
    assert!(o.stdout.contains("already disabled"));
    let o = cli(&r, &["plugins", "list"], "");
    assert!(o.stdout.contains("disabled"));
    let o = cli(&r, &["plugins", "enable", &id], "");
    assert_eq!(o.code, 0);
    let o = cli(&r, &["status"], "");
    assert!(o.stdout.contains("1 enabled of 1 installed"), "{}", o.stdout);

    let o = cli(&r, &["plugins", "install-from-zip", &zip("bad-collision-case")], "");
    assert_eq!(o.code, 1);
    assert!(o.stderr.contains("not installed"));
    let o = cli(&r, &["plugins", "enable", "nosuch"], "");
    assert_eq!(o.code, 1);
    let _ = std::fs::remove_dir_all(&r);
}

#[test]
fn capabilities_table_env_and_switches() {
    let r = scratch("caps");
    let o = cli(&r, &["capabilities"], "");
    assert_eq!(o.code, 0);
    assert_eq!(o.stdout.lines().count(), 6);
    assert!(o.stdout.contains("Not run in game."));

    let o = cli(&r, &["capabilities", "disable", "exp_cvars_native"], "");
    assert_eq!(o.code, 0, "{}", o.stderr);
    let ini = std::fs::read_to_string(r.join(REFERENCE_PRODUCT.ini_file)).unwrap();
    assert!(ini.contains("exp_cvars_native"));
    let o = cli(&r, &["capabilities", "env"], "");
    assert!(o.stdout.contains("SCO_CVARS_NATIVE=<unset>"), "{}", o.stdout);
    assert!(o.stdout.contains("SCO_AUDIO_NATIVE=1"));
    assert!(o.stdout.contains("SCO_ENTITIES_DIAG=<unset>"));
    let o = cli(&r, &["capabilities", "enable", "exp_entities_diag"], "");
    assert_eq!(o.code, 0);
    let o = cli(&r, &["capabilities", "env"], "");
    assert!(o.stdout.contains("SCO_ENTITIES_DIAG=1"));
    let o = cli(&r, &["capabilities", "enable", "nonsense"], "");
    assert_eq!(o.code, 1);
    let _ = std::fs::remove_dir_all(&r);
}

#[test]
fn mcp_command_talks_over_the_streams() {
    let r = scratch("mcp");
    let o = cli(
        &r,
        &["mcp"],
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n",
    );
    assert_eq!(o.code, 0);
    assert_eq!(o.stdout, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n");
    assert!(o.stderr.contains("read-only server on stdio"));
    let _ = std::fs::remove_dir_all(&r);
}

#[test]
fn built_binary_runs_status_and_mcp() {
    let exe = env!("CARGO_BIN_EXE_sco-reference-launcher");
    let r = scratch("bin");
    let out = Command::new(exe)
        .args(["--root", &r.display().to_string(), "status"])
        .output()
        .unwrap();
    assert!(out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).contains("SCO reference launcher"));

    // The root can also come from the environment.
    let out = Command::new(exe)
        .env("SCO_REF_ROOT", r.display().to_string())
        .arg("status")
        .output()
        .unwrap();
    assert!(String::from_utf8_lossy(&out.stdout).contains(&r.display().to_string()));

    let mut child = Command::new(exe)
        .args(["--root", &r.display().to_string(), "mcp"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();
    {
        use std::io::Write;
        child
            .stdin
            .take()
            .unwrap()
            .write_all(b"{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}\n")
            .unwrap();
    }
    let out = child.wait_with_output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    assert!(text.contains("get_status") && text.contains("read_log"), "{text}");
    let _ = std::fs::remove_dir_all(&r);
}
