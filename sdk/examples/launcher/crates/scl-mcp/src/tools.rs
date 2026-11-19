// SPDX-License-Identifier: GPL-3.0-only
//! The four tool definitions and the server instructions. Descriptions are plain statements of what
//! each read-only tool returns; `tests/run.rs` checks the schemas the server publishes.

/// One tool: name, title, description and the two JSON schemas.
pub struct ToolDef {
    pub name: &'static str,
    pub title: &'static str,
    pub description: &'static str,
    pub input_schema: &'static str,
    pub output_schema: &'static str,
}

/// The `instructions` text of the `initialize` reply.
pub const INSTRUCTIONS: &str = r#"This server gives a read-only view of one launcher folder: its status, plugins, logs and settings. It never starts or stops the game, installs, updates, or changes settings or plugins. Paths under the user's profile are shown as %USERPROFILE%. The game's own log is not served. To write plugins, use the sco-core SDK MCP server; to test plugins out of game, use a synthetic sandbox. Nothing here has been run in game."#;

/// The four read-only tools, in the order `tools/list` returns them.
pub const TOOLS: [ToolDef; 4] = [
    ToolDef {
        name: r#"get_status"#,
        title: r#"Launcher status"#,
        description: r#"Returns the state of this launcher folder: the launcher version, the folder, whether the settings file and the data folder exist, the plugins setting, how many plugins are enabled and installed, and which capability switches are on. These are the read-only checks behind the launcher's `status` command; the tool takes no arguments and never changes anything or uses the network. Paths are redacted. Capability switches have not been run in game."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"launcher_version":{"type":"string","description":"Version of this launcher."},
"folder":{"type":"string","description":"The launcher folder with the user profile redacted."},
"settings_found":{"type":"boolean","description":"Whether the settings file exists."},
"data_found":{"type":"boolean","description":"Whether the data folder exists."},
"plugins_setting_on":{"type":"boolean","description":"The plugins setting in the settings file."},
"plugins_enabled":{"type":"integer","minimum":0,"description":"Plugin folders without a disabled marker."},
"plugins_total":{"type":"integer","minimum":0,"description":"Plugin folders with a plugin.ini."},
"capabilities_on":{"type":"array","items":{"type":"string"},"description":"Ini keys of the capability switches that are on."},
"capabilities_total":{"type":"integer","minimum":0,"description":"How many capability switches the launcher has."}},
"required":["launcher_version","folder","settings_found","data_found","plugins_setting_on","plugins_enabled","plugins_total","capabilities_on","capabilities_total"],
"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"list_plugins"#,
        title: r#"Installed plugins"#,
        description: r#"Lists the plugin folders under data\plugins that have a plugin.ini, sorted by folder name and capped at 256. Each entry has the folder, whether it is enabled (no disabled file), the plugin.ini fields id, name, version, author, api, kind, entry and description (empty when absent, each shortened to 256 bytes) and warnings such as an id that does not match the folder or a missing id, name or version. The tool takes no arguments and is read-only: it does not load or run any plugin."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"count":{"type":"integer","minimum":0,"description":"Number of plugins returned."},
"truncated":{"type":"boolean","description":"True when more than 256 plugin folders exist and the list was cut."},
"plugins":{"type":"array","description":"Plugin folders sorted by folder name.","items":{"type":"object","properties":{
"folder":{"type":"string","description":"Folder name under data\\plugins."},
"enabled":{"type":"boolean","description":"False when the folder has a disabled file."},
"id":{"type":"string","description":"plugin.ini id; empty when absent."},
"name":{"type":"string","description":"plugin.ini name; empty when absent."},
"version":{"type":"string","description":"plugin.ini version; empty when absent."},
"author":{"type":"string","description":"plugin.ini author; empty when absent."},
"api":{"type":"string","description":"plugin.ini api; empty when absent."},
"kind":{"type":"string","description":"plugin.ini kind; empty when absent."},
"entry":{"type":"string","description":"plugin.ini entry; empty when absent."},
"description":{"type":"string","description":"plugin.ini description; empty when absent."},
"warnings":{"type":"array","description":"Problems found in this plugin's plugin.ini.","items":{"type":"string"}}},
"required":["folder","enabled","id","name","version","author","api","kind","entry","description","warnings"],"additionalProperties":false}}},
"required":["count","truncated","plugins"],"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"read_log"#,
        title: r#"Read a log"#,
        description: r#"Returns the last lines of one log: the host log data\mod.log, the launcher's data\launcher.log or its previous run launcher.log.1. It scans at most the last 2 MiB of the file, can keep only the lines containing a case-insensitive ASCII text, and returns at most 500 lines and 64 KiB of text, dropping the oldest first. Before filtering and returning it learns every handle, nickname, character name and identifier that the scanned part of the file and earlier calls in this session announce, and that the login lines anywhere in the first 64 MiB of every log announce, then redacts paths under the user's profile (%USERPROFILE%) and Users and home paths (<user>), the account name, the values of identity keys (nickname, handle, character and account names, ids; as <redacted>) and e-mail addresses, and replaces invalid UTF-8. Free text can still hold a name that no key or login line announced, and obfuscated addresses or paths are not caught, so read a log before you share it. The tool is read-only: it never writes, rotates or deletes a log."#,
        input_schema: r#"{"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous"],"description":"Which log: mod (data\\mod.log), launcher (data\\launcher.log) or launcher_previous (data\\launcher.log.1)."},
"lines":{"type":"integer","minimum":1,"maximum":500,"default":100,"description":"How many of the last matching lines to return."},
"contains":{"type":"string","minLength":1,"maxLength":200,"description":"Keep only lines containing this text, compared case-insensitively for ASCII letters, before the last lines are taken."}},
"required":["log"],"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous"],"description":"The log that was read."},
"path":{"type":"string","description":"The file's path with the user profile redacted; empty when unknown."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"lines":{"type":"array","description":"The returned lines, oldest first, without line endings.","items":{"type":"string"}},
"matched_lines":{"type":"integer","minimum":0,"description":"Lines that matched in the scanned part of the file, before the last N were taken."},
"truncated":{"type":"boolean","description":"True when older content was not scanned or more lines matched than were returned."},
"identity_scan_complete":{"type":"boolean","description":"False when a log was longer than 64 MiB (or could not be read), so login lines past that point were not learned from."},
"notice":{"type":"string","description":"Empty, or what was not scanned and what that means for redaction."}},
"required":["log","path","exists","lines","matched_lines","truncated","identity_scan_complete","notice"],"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"get_settings"#,
        title: r#"Launcher settings"#,
        description: r#"Returns the settings in the launcher's ini file next to the launcher as section, key and value entries in file order, skipping comments and blank lines. Values are redacted for the user profile and shortened to 1 KiB, and at most 1000 entries are returned. The tool takes no arguments and is read-only: it never writes or migrates the file."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"path":{"type":"string","description":"The ini file's path with the user profile redacted."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"truncated":{"type":"boolean","description":"True when the file has more settings than were returned."},
"settings":{"type":"array","description":"Settings in file order.","items":{"type":"object","properties":{
"section":{"type":"string","description":"The [section] the key is under; empty before any section."},
"key":{"type":"string","description":"The setting's key."},
"value":{"type":"string","description":"The setting's value as written, redacted."}},
"required":["section","key","value"],"additionalProperties":false}}},
"required":["path","exists","truncated","settings"],"additionalProperties":false}"#,
    },
];
