// SPDX-License-Identifier: GPL-3.0-only
//! The `mcp` command entry (`run_with`) and the server over a scratch launcher folder (`run_in`).
//! Synthetic data only; nothing here runs in game.

use std::io::Cursor;
use std::path::PathBuf;

use scl_contract::REFERENCE_PRODUCT;
use scl_install::Layout;
use scl_mcp::json::parse;
use scl_mcp::{run_in, run_with, Io, Platform, PluginFolder};

struct Mini;

impl Platform for Mini {
    fn version(&self) -> String {
        "1.2.3-test".to_string()
    }
    fn product(&self) -> String {
        "Test launcher".to_string()
    }
    fn plugins(&mut self) -> Vec<PluginFolder> {
        vec![PluginFolder {
            folder: b"demo".to_vec(),
            ini_text: b"id = demo\nname = Demo\nversion = 1\n".to_vec(),
            disabled: false,
        }]
    }
}

struct Outcome {
    code: i32,
    stdout: String,
    stderr: String,
}

fn run(args: &[&str], input: &str) -> Outcome {
    let args: Vec<String> = args.iter().map(|a| a.to_string()).collect();
    let mut stdin = Cursor::new(input.as_bytes().to_vec());
    let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
    let code = {
        let mut io = Io {
            stdin: &mut stdin,
            stdout: &mut stdout,
            stderr: &mut stderr,
        };
        run_with(&args, &mut io, &mut || Box::new(Mini))
    };
    Outcome {
        code,
        stdout: String::from_utf8(stdout).unwrap(),
        stderr: String::from_utf8(stderr).unwrap(),
    }
}

fn run_real(layout: &Layout, input: &str) -> Outcome {
    let mut stdin = Cursor::new(input.as_bytes().to_vec());
    let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
    let code = {
        let mut io = Io {
            stdin: &mut stdin,
            stdout: &mut stdout,
            stderr: &mut stderr,
        };
        run_in(layout, &[], &mut io)
    };
    Outcome {
        code,
        stdout: String::from_utf8(stdout).unwrap(),
        stderr: String::from_utf8(stderr).unwrap(),
    }
}

const PING: &str = r#"{"jsonrpc":"2.0","id":1,"method":"ping"}"#;

fn call(name: &str, args: &str) -> String {
    format!(
        "{{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{{\"name\":\"{name}\",\"arguments\":{args}}}}}\n"
    )
}

#[test]
fn answers_one_line_per_request_and_keeps_stdout_for_the_protocol() {
    let o = run(
        &[],
        &format!(
            "{PING}\n{{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}}\n{PING}\n"
        ),
    );
    assert_eq!(o.code, 0);
    assert_eq!(
        o.stdout,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n"
    );
    assert_eq!(
        o.stderr,
        "Test launcher mcp: read-only server on stdio, 1.2.3-test\n"
    );
}

#[test]
fn unknown_argument_prints_usage_and_fails() {
    let o = run(&["--game-log"], "");
    assert_eq!(o.code, 1);
    assert!(o.stderr.starts_with("usage: sco-reference-launcher mcp"));
    assert!(o.stderr.contains("unknown argument '--game-log'"));
    assert!(o.stdout.is_empty());
}

#[test]
fn help_goes_to_stdout() {
    for flag in ["--help", "-h", "/?"] {
        let o = run(&[flag], "");
        assert_eq!(o.code, 0, "{flag}");
        assert!(o.stdout.starts_with("usage: sco-reference-launcher mcp"));
        assert!(o.stderr.is_empty());
    }
}

#[test]
fn lists_exactly_four_read_only_tools() {
    let o = run(
        &[],
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n",
    );
    let r = parse(o.stdout.trim_end().as_bytes());
    assert!(r.ok, "{}", r.error);
    let v = r.value;
    let tools = v.find(b"result").unwrap().find(b"tools").unwrap();
    let names: Vec<String> = tools
        .items()
        .iter()
        .map(|t| String::from_utf8_lossy(t.find(b"name").unwrap().as_bytes()).into_owned())
        .collect();
    assert_eq!(
        names,
        ["get_status", "list_plugins", "read_log", "get_settings"]
    );
}

#[test]
fn the_game_log_is_not_a_choice() {
    let o = run(&[], &call("read_log", r#"{"log":"game"}"#));
    assert!(o.stdout.contains("-32602"), "{}", o.stdout);
}

#[test]
fn list_plugins_over_the_fake_platform() {
    let o = run(&[], &call("list_plugins", "{}"));
    assert!(o.stdout.contains("\"count\":1"), "{}", o.stdout);
    assert!(o.stdout.contains("\"folder\":\"demo\""));
}

fn scratch(name: &str) -> PathBuf {
    let p = std::env::temp_dir().join(format!("scl-mcp-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&p);
    std::fs::create_dir_all(&p).unwrap();
    p
}

#[test]
fn real_folder_status_plugins_log_and_settings() {
    let root = scratch("real");
    let layout = Layout::new(REFERENCE_PRODUCT, &root);
    let plug = layout.plugin_dir("pilot");
    std::fs::create_dir_all(&plug).unwrap();
    std::fs::write(
        plug.join("plugin.ini"),
        "id = pilot\nname = Pilot\nversion = 1.0.0\napi = 1.0\nkind = data\n",
    )
    .unwrap();
    std::fs::write(plug.join("disabled"), "").unwrap();
    std::fs::write(&layout.ini, "plugins = on\nexp_audio_native = off\n").unwrap();
    std::fs::write(&layout.mod_log, "one\nneedle two\nthree\n").unwrap();

    let input = format!(
        "{}{}{}{}",
        call("get_status", "{}"),
        call("list_plugins", "{}"),
        call("read_log", r#"{"log":"mod","contains":"NEEDLE"}"#),
        call("get_settings", "{}")
    );
    let o = run_real(&layout, &input);
    assert_eq!(o.code, 0);
    let lines: Vec<&str> = o.stdout.lines().collect();
    assert_eq!(lines.len(), 4, "{}", o.stdout);
    assert!(lines[0].contains("\"plugins_total\":1"));
    assert!(lines[0].contains("\"plugins_enabled\":0"));
    assert!(lines[0].contains("\"settings_found\":true"));
    assert!(lines[0].contains("\"capabilities_total\":6"));
    assert!(!lines[0].contains("exp_audio_native\""), "{}", lines[0]);
    assert!(lines[0].contains("exp_loadout_async"));
    assert!(lines[1].contains("\"enabled\":false"));
    assert!(lines[2].contains("needle two"));
    assert!(!lines[2].contains("three"));
    assert!(lines[3].contains("exp_audio_native"));

    // The server wrote nothing.
    assert!(!layout.data.join("launcher.log").exists());
    assert!(!layout.storage.exists());
    let _ = std::fs::remove_dir_all(&root);
}
