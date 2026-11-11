//! Minimal experimental plugin using the frozen C ABI. No Rust panic may cross it.
use core::{ffi::{c_char, c_void}, mem::{offset_of, size_of}, ptr};
use sco_sdk_raw::{self as sco, Api, Command, Plugin, PluginInfo};

// These immutable strings and the table live for the life of the library.
struct StaticInfo(PluginInfo);
unsafe impl Sync for StaticInfo {}
static INFO: StaticInfo = StaticInfo(PluginInfo {
    size: size_of::<PluginInfo>() as u32, api_major: sco::API_MAJOR, api_minor: sco::API_MINOR,
    name: b"rust_hello\0".as_ptr().cast(), version: b"0.1.0\0".as_ptr().cast(),
    author: b"sco SDK example\0".as_ptr().cast(),
});
unsafe extern "C" fn ping(_args: *const sco::Arg, nargs: u32, _ctx: *mut c_void, reply: *mut c_char, reply_size: u32) -> sco::ResultCode {
    if nargs != 0 || reply.is_null() || reply_size < 5 { return sco::BAD_ARG; }
    unsafe { ptr::copy_nonoverlapping(b"pong\0".as_ptr(), reply.cast::<u8>(), 5) };
    sco::OK
}
#[no_mangle]
pub extern "C" fn sco_plugin_query() -> *const PluginInfo { &INFO.0 }
#[no_mangle]
pub unsafe extern "C" fn sco_plugin_load(api: *const Api, me: *mut Plugin) -> sco::ResultCode {
    if api.is_null() || me.is_null() { return sco::BAD_ARG; }
    // The ABI guarantees a readable header. Check complete member extents before
    // loading any pointer; never construct &Api for a truncated host table.
    if !unsafe { sco::covers(api, offset_of!(Api, register_command), size_of::<Option<unsafe extern "C" fn(*mut Plugin, *const Command) -> sco::ResultCode>>()) } { return sco::FAILED; }
    let major = unsafe { ptr::read(ptr::addr_of!((*api).major)) };
    if major != sco::API_MAJOR { return sco::FAILED; }
    let register = unsafe { ptr::read(ptr::addr_of!((*api).register_command)) };
    let Some(register) = register else { return sco::FAILED; };
    let cmd = Command { size: size_of::<Command>() as u32, pad: 0,
        name: b"rust_hello.ping\0".as_ptr().cast(), title: b"Ping\0".as_ptr().cast(),
        help: b"Replies pong\0".as_ptr().cast(), capability: ptr::null(), args: ptr::null(),
        nargs: 0, arg_def_size: size_of::<sco::ArgDef>() as u32, callback: Some(ping), ctx: ptr::null_mut() };
    unsafe { register(me, &cmd) }
}
#[no_mangle]
pub extern "C" fn sco_plugin_unload() {}

#[cfg(test)]
mod tests {
    use super::*;
    use core::sync::atomic::{AtomicBool, Ordering};
    static CALLED: AtomicBool = AtomicBool::new(false);
    unsafe extern "C" fn register(_me: *mut Plugin, cmd: *const Command) -> sco::ResultCode {
        let cmd = unsafe { &*cmd };
        assert_eq!(cmd.nargs, 0);
        let mut reply = [0 as c_char; 5];
        let result = unsafe { cmd.callback.unwrap()(ptr::null(), 0, ptr::null_mut(), reply.as_mut_ptr(), 5) };
        assert_eq!(result, sco::OK);
        assert_eq!(reply, [112, 111, 110, 103, 0]);
        CALLED.store(true, Ordering::SeqCst);
        sco::OK
    }
    #[test]
    fn accepts_full_table_and_refuses_short_header() {
        let mut bytes = [0u64; 16];
        let api = bytes.as_mut_ptr().cast::<Api>();
        unsafe {
            ptr::addr_of_mut!((*api).size).write(size_of::<Api>() as u32);
            ptr::addr_of_mut!((*api).major).write(sco::API_MAJOR);
            ptr::addr_of_mut!((*api).register_command).write(Some(register));
            assert_eq!(sco_plugin_load(api, 1usize as *mut Plugin), sco::OK);
        }
        assert!(CALLED.load(Ordering::SeqCst));
        // A genuine ABI 1.0-sized allocation has no tail, unlike a poisoned full table.
        let mut prefix = [0u64; 11];
        let old = prefix.as_mut_ptr().cast::<Api>();
        unsafe {
            ptr::addr_of_mut!((*old).size).write(88);
            ptr::addr_of_mut!((*old).major).write(sco::API_MAJOR);
            ptr::addr_of_mut!((*old).register_command).write(Some(register));
            assert_eq!(sco_plugin_load(old, 1usize as *mut Plugin), sco::OK);
            ptr::addr_of_mut!((*old).register_command).write(None);
            assert_eq!(sco_plugin_load(old, 1usize as *mut Plugin), sco::FAILED);
            ptr::addr_of_mut!((*old).major).write(2);
            assert_eq!(sco_plugin_load(old, 1usize as *mut Plugin), sco::FAILED);
        }
        let short: u32 = 4;
        assert_eq!(unsafe { sco_plugin_load((&short as *const u32).cast(), 1usize as *mut Plugin) }, sco::FAILED);
        assert_eq!(unsafe { sco_plugin_load(ptr::null(), 1usize as *mut Plugin) }, sco::BAD_ARG);
    }
    #[test]
    fn register_member_extent_and_refusals_fail_closed() {
        use core::sync::atomic::{AtomicUsize, Ordering};
        static CALLS: AtomicUsize = AtomicUsize::new(0);
        unsafe extern "C" fn count(_me: *mut Plugin, _cmd: *const Command) -> sco::ResultCode {
            CALLS.fetch_add(1, Ordering::SeqCst);
            sco::OK
        }
        let mut bytes = [0u64; 9];
        let api = bytes.as_mut_ptr().cast::<Api>();
        let me = 1usize as *mut Plugin;
        unsafe {
            ptr::addr_of_mut!((*api).major).write(sco::API_MAJOR);
            ptr::addr_of_mut!((*api).register_command).write(Some(count));
            // The callback starts at 64 but is not fully present until byte 72.
            ptr::addr_of_mut!((*api).size).write(71);
            assert_eq!(sco_plugin_load(api, me), sco::FAILED);
            assert_eq!(CALLS.load(Ordering::SeqCst), 0);
            ptr::addr_of_mut!((*api).size).write(72);
            assert_eq!(sco_plugin_load(api, me), sco::OK);
            assert_eq!(CALLS.load(Ordering::SeqCst), 1);
            ptr::addr_of_mut!((*api).major).write(2);
            assert_eq!(sco_plugin_load(api, me), sco::FAILED);
            ptr::addr_of_mut!((*api).major).write(sco::API_MAJOR);
            assert_eq!(sco_plugin_load(api, ptr::null_mut()), sco::BAD_ARG);
            ptr::addr_of_mut!((*api).register_command).write(None);
            assert_eq!(sco_plugin_load(api, me), sco::FAILED);
            assert_eq!(CALLS.load(Ordering::SeqCst), 1);
        }
    }
    #[test]
    fn ping_checks_reply_capacity_and_argument_count() {
        let mut out = [0 as c_char; 5];
        assert_eq!(unsafe { ping(ptr::null(), 1, ptr::null_mut(), out.as_mut_ptr(), 5) }, sco::BAD_ARG);
        assert_eq!(unsafe { ping(ptr::null(), 0, ptr::null_mut(), out.as_mut_ptr(), 4) }, sco::BAD_ARG);
        assert_eq!(unsafe { ping(ptr::null(), 0, ptr::null_mut(), ptr::null_mut(), 5) }, sco::BAD_ARG);
    }
}
