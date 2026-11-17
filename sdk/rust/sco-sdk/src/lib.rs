//! Experimental, unsafe Rust declarations for `sco_api.h` ABI 1.1.
//! This is not a safe wrapper. Read the SDK's C contract before using it.
#![deny(unsafe_op_in_unsafe_fn)]
use core::ffi::{c_char, c_int, c_void};

#[cfg(not(target_pointer_width = "64"))]
compile_error!("SCO plugin ABI v1 requires 64-bit pointers");

pub const API_MAJOR: u16 = 1;
pub const API_MINOR: u16 = 1;
pub type ResultCode = c_int;
pub const OK: ResultCode = 0;
pub const UNAVAILABLE: ResultCode = 1;
pub const NOT_FOUND: ResultCode = 2;
pub const BAD_ARG: ResultCode = 3;
pub const CRASHED: ResultCode = 4;
pub const WRONG_THREAD: ResultCode = 5;
pub const TOO_MANY: ResultCode = 6;
pub const FAILED: ResultCode = 7;
pub type LogLevel = c_int;
pub const LOG_INFO: LogLevel = 0;
pub const LOG_WARN: LogLevel = 1;
pub const LOG_ERROR: LogLevel = 2;
pub const ARG_INT: u32 = 0;
pub const ARG_FLOAT: u32 = 1;
pub const ARG_STRING: u32 = 2;
pub const ARG_BOOL: u32 = 3;

#[repr(C)]
pub struct Plugin { _private: [u8; 0] }
#[repr(C)]
#[derive(Clone, Copy)]
pub union ArgValue { pub i: i64, pub f: f64, pub s: *const c_char }
#[repr(C)]
pub struct Arg { pub kind: u32, pub pad: u32, pub value: ArgValue }
#[repr(C)]
pub struct ArgDef { pub name: *const c_char, pub kind: u32, pub pad: u32, pub help: *const c_char }
pub type TaskFn = unsafe extern "C" fn(*mut c_void);
pub type EventFn = unsafe extern "C" fn(*const c_char, *const c_void, *mut c_void);
pub type CommandFn = unsafe extern "C" fn(*const Arg, u32, *mut c_void, *mut c_char, u32) -> ResultCode;
pub type InvokeDone = unsafe extern "C" fn(ResultCode, *const c_char, *mut c_void);
pub type RawFn = unsafe extern "C" fn(*const c_void, u32, *mut c_void, *mut u32, *mut c_void) -> ResultCode;
#[repr(C)]
pub struct Command {
    pub size: u32, pub pad: u32, pub name: *const c_char, pub title: *const c_char,
    pub help: *const c_char, pub capability: *const c_char, pub args: *const ArgDef,
    pub nargs: u32, pub arg_def_size: u32, pub callback: Option<CommandFn>, pub ctx: *mut c_void,
}
#[repr(C)]
pub struct Api {
    pub size: u32, pub major: u16, pub minor: u16,
    pub host_version: Option<unsafe extern "C" fn() -> *const c_char>,
    pub has: Option<unsafe extern "C" fn(*const c_char) -> c_int>,
    pub run_on_game_thread: Option<unsafe extern "C" fn(*mut Plugin, Option<TaskFn>, *mut c_void) -> ResultCode>,
    pub subscribe: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, Option<EventFn>, *mut c_void) -> ResultCode>,
    pub unsubscribe: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, Option<EventFn>) -> ResultCode>,
    pub status: Option<unsafe extern "C" fn(*mut Plugin, *const c_char)>,
    pub log: Option<unsafe extern "C" fn(*mut Plugin, LogLevel, *const c_char)>,
    pub register_command: Option<unsafe extern "C" fn(*mut Plugin, *const Command) -> ResultCode>,
    pub invoke: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, *const Arg, u32, Option<InvokeDone>, *mut c_void) -> ResultCode>,
    pub list_commands: Option<unsafe extern "C" fn(*mut *const Command, u32) -> u32>,
    pub provide_service: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, u32, *const c_void) -> ResultCode>,
    pub query_service: Option<unsafe extern "C" fn(*const c_char, u32, *mut *const c_void) -> ResultCode>,
    pub release_service: Option<unsafe extern "C" fn(*mut Plugin, *const c_char) -> ResultCode>,
    pub invoke_raw: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, *const c_void, u32, *mut c_void, *mut u32) -> ResultCode>,
    pub register_raw: Option<unsafe extern "C" fn(*mut Plugin, *const c_char, *const c_char, Option<RawFn>, *mut c_void) -> ResultCode>,
}
#[repr(C)]
pub struct PluginInfo { pub size: u32, pub api_major: u16, pub api_minor: u16, pub name: *const c_char, pub version: *const c_char, pub author: *const c_char }

/// Checks the *complete* extent of a member, not just its starting offset.
///
/// # Safety
/// `api` must be non-null, properly aligned and point to at least the four-byte
/// size field, whose value truthfully describes the allocated host table. Do not
/// form a reference to `Api` unless its entire extent is valid.
pub unsafe fn covers(api: *const Api, offset: usize, member_size: usize) -> bool {
    let size = unsafe { core::ptr::read(api.cast::<u32>()) } as usize;
    offset.checked_add(member_size).is_some_and(|end| end <= size)
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};
    #[test] fn layouts_match_c_abi_v1_pins() {
        assert_eq!((API_MAJOR, API_MINOR), (1, 1));
        assert_eq!([OK, UNAVAILABLE, NOT_FOUND, BAD_ARG, CRASHED, WRONG_THREAD, TOO_MANY, FAILED], [0,1,2,3,4,5,6,7]);
        assert_eq!([LOG_INFO, LOG_WARN, LOG_ERROR], [0,1,2]);
        assert_eq!([ARG_INT, ARG_FLOAT, ARG_STRING, ARG_BOOL], [0,1,2,3]);
        assert_eq!((size_of::<Arg>(), align_of::<Arg>(), offset_of!(Arg, kind), offset_of!(Arg, pad), offset_of!(Arg, value)), (16, 8, 0, 4, 8));
        assert_eq!((size_of::<ArgDef>(), align_of::<ArgDef>(), offset_of!(ArgDef, name), offset_of!(ArgDef, kind), offset_of!(ArgDef, pad), offset_of!(ArgDef, help)), (24, 8, 0, 8, 12, 16));
        assert_eq!((size_of::<Command>(), align_of::<Command>()), (72, 8));
        assert_eq!([offset_of!(Command, size), offset_of!(Command, pad), offset_of!(Command, name), offset_of!(Command, title), offset_of!(Command, help), offset_of!(Command, capability), offset_of!(Command, args), offset_of!(Command, nargs), offset_of!(Command, arg_def_size), offset_of!(Command, callback), offset_of!(Command, ctx)], [0,4,8,16,24,32,40,48,52,56,64]);
        assert_eq!((size_of::<PluginInfo>(), align_of::<PluginInfo>()), (32, 8));
        assert_eq!([offset_of!(PluginInfo, size), offset_of!(PluginInfo, api_major), offset_of!(PluginInfo, api_minor), offset_of!(PluginInfo, name), offset_of!(PluginInfo, version), offset_of!(PluginInfo, author)], [0,4,6,8,16,24]);
        assert_eq!((size_of::<Api>(), align_of::<Api>(), offset_of!(Api, size), offset_of!(Api, major), offset_of!(Api, minor)), (128, 8, 0, 4, 6));
        assert_eq!([offset_of!(Api, host_version), offset_of!(Api, has), offset_of!(Api, run_on_game_thread), offset_of!(Api, subscribe), offset_of!(Api, unsubscribe), offset_of!(Api, status), offset_of!(Api, log), offset_of!(Api, register_command), offset_of!(Api, invoke), offset_of!(Api, list_commands), offset_of!(Api, provide_service), offset_of!(Api, query_service), offset_of!(Api, release_service), offset_of!(Api, invoke_raw), offset_of!(Api, register_raw)], [8,16,24,32,40,48,56,64,72,80,88,96,104,112,120]);
    }
    #[test] fn truncated_allocation_is_not_read_past_size() {
        // Only a size field exists, rather than a full allocation with a poisoned tail.
        let size: u32 = 4;
        let api = (&size as *const u32).cast::<Api>();
        assert!(unsafe { covers(api, 0, 4) });
        assert!(!unsafe { covers(api, 0, 8) });
        assert!(!unsafe { covers(api, 64, 8) });
        assert!(!unsafe { covers(api, usize::MAX, 2) });
    }
}
