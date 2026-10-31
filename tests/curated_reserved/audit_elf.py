#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 the sco-core contributors
"""Check a trusted, unstripped Linux fixture; not a general process boundary."""
import argparse
import re
import subprocess
import sys

MODULE_EXPORTS = {
    'curated_module_identity', 'curated_module_host_accessor_found',
    'curated_module_table', 'curated_module_read', 'curated_module_move',
    'curated_module_enqueue_set', 'curated_module_enqueue_add',
    'curated_module_arm_move_gate', 'curated_module_wait_move_gate',
    'curated_module_release_move_gate',
}
HOST_EXPORT = 'reserved_host_mock_table'
SYSTEM_DEFINED_TYPES = {
    'stderr': {'B'}, '__libc_single_threaded': {'B'},
    '__asan_option_detect_stack_use_after_return': {'B'}, '__ubsan_vptr_type_cache': {'B'},
    '_ZSt7nothrow': {'D'}, '_ZTISt12future_error': {'V'},
    '_ZTVNSt6thread6_StateE': {'V'}, '_ZTVSt12future_error': {'V'},
}
SYSTEM_NEEDED = {'libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1', 'libm.so.6',
                 'libpthread.so.0', 'libdl.so.2', 'librt.so.1'}
DYNAMIC_TAGS = {
    'NEEDED', 'SONAME', 'RPATH', 'RUNPATH', 'INIT', 'FINI', 'INIT_ARRAY', 'INIT_ARRAYSZ',
    'FINI_ARRAY', 'FINI_ARRAYSZ', 'PREINIT_ARRAY', 'PREINIT_ARRAYSZ', 'GNU_HASH', 'HASH',
    'STRTAB', 'SYMTAB', 'STRSZ', 'SYMENT', 'DEBUG', 'PLTGOT', 'PLTRELSZ', 'PLTREL',
    'JMPREL', 'RELA', 'RELASZ', 'RELAENT', 'REL', 'RELSZ', 'RELENT', 'RELACOUNT',
    'RELCOUNT', 'FLAGS', 'FLAGS_1', 'BIND_NOW', 'VERNEED', 'VERNEEDNUM', 'VERDEF',
    'VERDEFNUM', 'VERSYM', 'NULL',
}
SYSTEM_IMPORTS = {
    '_ITM_deregisterTMCloneTable', '_ITM_registerTMCloneTable', '__gmon_start__',
    '_ZNSt18condition_variable10notify_allEv', '_ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE',
    '_ZNSt18condition_variableC1Ev', '_ZNSt18condition_variableD1Ev',
    '_ZNSt6chrono3_V212steady_clock3nowEv', '_ZSt20__throw_system_errori', '_ZSt9terminatev',
    '__cxa_atexit', '__cxa_finalize', '__cxa_begin_catch', '__gxx_personality_v0',
    'pthread_cond_clockwait', 'pthread_cond_timedwait', 'pthread_mutex_lock', 'pthread_mutex_unlock',
    '__asan_after_dynamic_init', '__asan_before_dynamic_init', '__asan_handle_no_return',
    '__asan_init', '__asan_memcpy', '__asan_option_detect_stack_use_after_return',
    '__asan_register_elf_globals', '__asan_unregister_elf_globals', '__asan_register_globals',
    '__asan_unregister_globals', '__asan_version_mismatch_check_v8', '__asan_report_store_n',
    '__sanitizer_internal_memcpy', '__sanitizer_internal_memmove', '__sanitizer_internal_memset',
    '__ubsan_handle_add_overflow', '__ubsan_handle_builtin_unreachable', '__ubsan_handle_divrem_overflow',
    '__ubsan_handle_function_type_mismatch', '__ubsan_handle_load_invalid_value',
    '__ubsan_handle_mul_overflow', '__ubsan_handle_nonnull_arg', '__ubsan_handle_out_of_bounds',
    '__ubsan_handle_sub_overflow', '__ubsan_handle_type_mismatch_v1', '__ubsan_handle_pointer_overflow',
    '_Unwind_Resume', '__gcc_personality_v0', '__tsan_func_entry', '__tsan_func_exit',
    '__tsan_init', '__tsan_memcpy',
}
SYSTEM_IMPORT_PATTERN = re.compile(
    r'(?:__asan_(?:report_(?:load|store)(?:1|2|4|8|16)|stack_malloc_[0-9]+)'
    r'|__tsan_(?:read|write)(?:1|2|4|8|16))$')
ALLOWED_DETAIL = ('sco::detail::AuthorityMutex::', 'sco::detail::AuthorityMutex* std::__addressof<',
                  'sco::detail::g_reserved',
                  'sco::detail::g_legacyDirty', 'sco::detail::g_legacyEntries',
                  'sco::detail::g_authorityMutex')
ALLOWED_CONSTANTS = {
    'kMaxArgHelpLen', 'kMaxArgNameLen', 'kMaxCapabilityLen', 'kMaxCommandArgs',
    'kMaxCommands', 'kMaxHelpLen', 'kMaxNameLen', 'kMaxQueuedTasks',
    'kMaxQueuedTasksHard', 'kMaxRawHandlers', 'kMaxReleaseHooks', 'kMaxServiceNameLen',
    'kMaxSubscriptions', 'kMaxTitleLen', 'kReplySize', 'kReservedPrefixes',
}


def run(*command):
    return subprocess.run(command, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True).stdout


def dynamic_defined(nm, path, allow_platform=False):
    result = set()
    for line in run(nm, '-D', '--defined-only', '--format=posix', path).splitlines():
        fields = line.split()
        if len(fields) < 2:
            raise ValueError('unrecognized nm output: ' + line)
        name = fields[0]
        base = name.split('@', 1)[0]
        if '@' in name:
            if (not allow_platform
                    or not re.search(r'@(?:GLIBC|GLIBCXX|CXXABI|GCC)_[0-9.]+$', name)
                    or base not in SYSTEM_DEFINED_TYPES or fields[1] not in SYSTEM_DEFINED_TYPES[base]):
                raise ValueError('unexpected versioned export: ' + name)
            continue  # versioned platform runtime symbol
        if allow_platform and name in SYSTEM_DEFINED_TYPES:
            if fields[1] not in SYSTEM_DEFINED_TYPES[name]:
                raise ValueError('unexpected platform symbol type: ' + name)
        else:
            result.add(name)
    return result


def needed(readelf, path, allowed_runpath):
    output = run(readelf, '-d', path)
    tags = re.findall(r'^\s*0x[0-9a-fA-F]+\s+\(([^)]+)\)', output, re.MULTILINE)
    if not tags or tags[-1] != 'NULL':
        raise ValueError('unrecognized dynamic section: ' + path)
    unexpected = set(tags) - DYNAMIC_TAGS
    if unexpected:
        raise ValueError('unexpected dynamic tags: ' + repr(unexpected))
    paths = re.findall(r'\((?:RPATH|RUNPATH)\).*?\[(.*?)\]', output)
    if paths and (len(paths) != 1 or paths[0] != allowed_runpath):
        raise ValueError('unexpected runtime search path: ' + repr(paths))
    names = re.findall(r'\(NEEDED\).*?\[(.*?)\]', output)
    if not names:
        raise ValueError('no dynamic dependencies parsed: ' + path)
    for name in names:
        if name not in SYSTEM_NEEDED and not re.fullmatch(
                r'lib(?:asan|ubsan|tsan)\.so(?:\.\d+)*|libclang_rt\.(?:asan|tsan)-x86_64\.so', name):
            raise ValueError('unexpected shared dependency: ' + name)
    return names


def check(nm, readelf, host, module, allowed_runpath=''):
    host_exports = dynamic_defined(nm, host, allow_platform=True)
    module_exports = dynamic_defined(nm, module)
    if host_exports != {HOST_EXPORT}:
        raise ValueError('unexpected host exports: ' + repr(host_exports))
    if module_exports != MODULE_EXPORTS:
        raise ValueError('unexpected module exports: ' + repr(module_exports))
    imports = run(nm, '-D', '--undefined-only', '--format=posix', module).splitlines()
    names = [line.split()[0] for line in imports if line.split()]
    if names.count(HOST_EXPORT) != 1:
        raise ValueError('module must import exactly one host accessor')
    for name in names:
        if name == HOST_EXPORT:
            continue
        base, marker, version = name.partition('@')
        if marker and not re.fullmatch(r'(?:GLIBC|GLIBCXX|CXXABI|GCC)_[0-9.]+', version):
            raise ValueError('unexpected import version: ' + name)
        if base not in SYSTEM_IMPORTS and not SYSTEM_IMPORT_PATTERN.fullmatch(base):
            raise ValueError('unexpected module import: ' + name)
    host_symbols = run(nm, '-a', '-C', '--defined-only', host).splitlines()
    module_symbols = run(nm, '-a', '-C', '--defined-only', module).splitlines()
    authority = re.compile(r'\b[tT]\s+sco::reserved::CreateExecutableAuthority\(\)$')
    if sum(bool(authority.search(line)) for line in host_symbols) != 1:
        raise ValueError('host does not contain exactly one authority definition')
    if any(re.search(r'\bsco::(?:reserved|hook|plugins|game)::', line) for line in module_symbols):
        raise ValueError('module contains a prohibited implementation symbol')
    for line in module_symbols:
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and fields[2].startswith('sco_'):
            raise ValueError('unexpected module C symbol: ' + fields[2])
        if len(fields) == 3 and fields[2].startswith('sco::'):
            if fields[2].removeprefix('sco::') not in ALLOWED_CONSTANTS:
                raise ValueError('unexpected module project symbol: ' + fields[2])
    if any(re.search(r'\bsco::(?:hook|plugins|game)::', line) for line in host_symbols):
        raise ValueError('host contains a prohibited implementation symbol')
    for line in host_symbols:
        fields = line.split(maxsplit=2)
        if len(fields) != 3:
            continue
        name = fields[2]
        if name.startswith('sco_'):
            raise ValueError('unexpected host C symbol: ' + name)
        if name.startswith('sco::detail::') and not name.startswith(ALLOWED_DETAIL):
            raise ValueError('unexpected host detail symbol: ' + name)
        if name.startswith('sco::') and not name.startswith(
                ('sco::reserved::', 'sco::ReservedWorkerLifecycle', 'sco::detail::', 'sco::Result ')):
            if name.removeprefix('sco::') not in ALLOWED_CONSTANTS:
                raise ValueError('unexpected host project symbol: ' + name)
    needed(readelf, host, allowed_runpath)
    needed(readelf, module, allowed_runpath)
    print('PASS: fixture ELF exports, imports, authority and dynamic dependencies')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--nm', required=True)
    parser.add_argument('--readelf', required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--allowed-runpath', default='')
    args = parser.parse_args()
    check(args.nm, args.readelf, args.host, args.module, args.allowed_runpath)


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
