// SPDX-License-Identifier: GPL-3.0-only
// Project-authored no-game fixture. Fake receipts are NOT OS-security proof.
#include "ipc/diagnostics_audit.h"
#include "ipc/diagnostics_ipc.h"
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winevt.h>
#include <string>
#include <vector>

std::wstring Wide(const char* s) {
    std::wstring r;
    for (; *s; ++s) { if (*s < 32 || *s > 126) std::abort(); r += wchar_t(*s); }
    return r;
}
#endif
using namespace sco::diagnostics;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (0)
#ifdef _WIN32
// Only synthetic, administrator-provisioned fixture channels are eligible for
// destructive-rights probes. Never accepts Application/System/Security.
int WindowsFixture(int argc, char** argv) {
    CHECK(argc == 5);
    audit::WindowsOptions options{Wide(argv[2]), Wide(argv[3]), 8388608};
    CHECK(options.log.rfind(L"SCOFixture", 0) == 0);
    CHECK(options.source.rfind(L"SCOFixture", 0) == 0);
    CHECK(std::string(argv[4]) == "8388608");
    for (wchar_t c : options.log + options.source)
        CHECK((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_');
    if (std::string(argv[1]) == "--windows-fixture-driver") {
        wchar_t image[32768]{};
        CHECK(GetModuleFileNameW(nullptr, image, 32768));
        std::wstring command = L"\"" + std::wstring(image) + L"\" --windows-fixture " +
            options.log + L" " + options.source + L" 8388608";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        CHECK(CreateProcessW(image, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &process));
        const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 3);
        DWORD code = 3;
        CHECK(wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code));
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        CHECK(code == 0);
        std::cout << "PASS: dedicated no-game effective-identity child fixture\n";
        return 0;
    }
    if (std::string(argv[1]) == "--windows-denied") {
        const auto receipt = audit::OpenWindowsCollector(options)->Verify(GetTickCount64() + 2000);
        CHECK(receipt.status == audit::Status::IdentityDenied);
        std::cout << "PASS: privileged effective identity denied before append\n";
        return 0;
    }
    CHECK(std::string(argv[1]) == "--windows-fixture");
    auto collector = audit::OpenWindowsCollector(options);
    auto verifyDeadline = GetTickCount64() + 2000;
    auto verified = collector->Verify(verifyDeadline);
    CHECK(audit::Protected(verified, verifyDeadline, GetTickCount64()));
    HANDLE log = OpenEventLogW(nullptr, options.log.c_str());
    CHECK(log);
    DWORD before = 0, after = 0;
    CHECK(GetNumberOfEventLogRecords(log, &before));
    ipc::AuditEvent event{0x100, {}, GetTickCount64(), 128, "ok"};
    event.request.fill(0x22);
    audit::Coordinates coordinates; coordinates.session.fill(0x11); coordinates.build = "fixture-host-1";
    auto deadline = GetTickCount64() + 2000;
    auto receipt = collector->Append(event, coordinates, deadline);
    CHECK(audit::Acknowledged(receipt, deadline, GetTickCount64()));
    CHECK(GetNumberOfEventLogRecords(log, &after) && after == before + 1);
    CHECK(!EvtClearLog(nullptr, options.log.c_str(), nullptr, 0) && GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(GetNumberOfEventLogRecords(log, &before) && before == after);
    EVT_HANDLE config = EvtOpenChannelConfig(nullptr, options.log.c_str(), 0);
    CHECK(config);
    EVT_VARIANT retention{}; retention.Type = EvtVarTypeBoolean; retention.BooleanVal = FALSE;
    if (EvtSetChannelConfigProperty(config, EvtChannelLoggingConfigRetention, 0, &retention))
        CHECK(!EvtSaveChannelConfig(config, 0) && GetLastError() == ERROR_ACCESS_DENIED);
    else CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    EvtClose(config);
    const auto registry = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\" + options.log;
    HKEY key = nullptr;
    CHECK(RegOpenKeyExW(HKEY_LOCAL_MACHINE, registry.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_ACCESS_DENIED);
    CHECK(RegOpenKeyExW(HKEY_LOCAL_MACHINE, registry.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS);
    wchar_t configured[16384]{}, expanded[16384]{};
    DWORD type = 0, size = sizeof(configured);
    CHECK(RegQueryValueExW(key, L"File", nullptr, &type, reinterpret_cast<BYTE*>(configured), &size) == ERROR_SUCCESS);
    RegCloseKey(key);
    CHECK((type == REG_SZ || type == REG_EXPAND_SZ) && size <= sizeof(configured) && size >= 2 &&
          configured[size / sizeof(wchar_t) - 1] == 0);
    const auto expandedSize = ExpandEnvironmentStringsW(configured, expanded, 16384);
    CHECK(expandedSize > 0 && expandedSize <= 16384);
    const DWORD fileRights[] = {FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER};
    for (DWORD right : fileRights) {
        HANDLE rawFile = CreateFileW(expanded, right,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(rawFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    CHECK(manager);
    for (DWORD right : {SERVICE_STOP, SERVICE_CHANGE_CONFIG}) {
        SC_HANDLE service = OpenServiceW(manager, L"EventLog", right);
        CHECK(!service && GetLastError() == ERROR_ACCESS_DENIED);
    }
    CloseServiceHandle(manager);
    CHECK(GetNumberOfEventLogRecords(log, &before) && before == after);
    CloseEventLog(log);
    auto expired = collector->Append(event, coordinates, GetTickCount64());
    CHECK(expired.status == audit::Status::Deadline);
    std::cout << "PASS: protected append acknowledged, clear/config/retention denied; synthetic build/session only\n";
    return 0;
}
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1) return WindowsFixture(argc, argv);
#else
    (void)argc; (void)argv;
#endif
    audit::Receipt accepted{audit::Status::Accepted, 100, true, true};
    CHECK(audit::Acknowledged(accepted, 200, 101));
    CHECK(!audit::Acknowledged(accepted, 100, 100));
    CHECK(!audit::Acknowledged(accepted, 200, 200));
    accepted.protectedSink = false;
    CHECK(!audit::Acknowledged(accepted, 200, 101));
    accepted.protectedSink = true; accepted.effectiveIdentity = false;
    CHECK(!audit::Acknowledged(accepted, 200, 101));
    accepted.effectiveIdentity = true; accepted.completedMs = 102;
    CHECK(!audit::Acknowledged(accepted, 200, 101));
    accepted.completedMs = 0;
    CHECK(!audit::Acknowledged(accepted, 200, 101));
    accepted.completedMs = 100; accepted.status = audit::Status::Verified;
    CHECK(audit::Protected(accepted, 200, 101));
    CHECK(!audit::Acknowledged(accepted, 200, 101));
    for (auto status : {audit::Status::Unavailable, audit::Status::IdentityDenied, audit::Status::ProtectionDenied,
                        audit::Status::Deadline, audit::Status::Full, audit::Status::Invalid, audit::Status::Busy}) {
        accepted.status = status;
        CHECK(!audit::Protected(accepted, 200, 101));
        CHECK(!audit::Acknowledged(accepted, 200, 101));
    }
#ifndef _WIN32
    auto collector = audit::OpenWindowsCollector({L"SCOFixtureAudit", L"SCOFixtureSource"});
    CHECK(collector->Verify(200).status == audit::Status::Unavailable);
    CHECK(collector->Append({}, {}, 200).status == audit::Status::Unavailable);
#endif
    std::cout << "PASS: receipt/deadline contract; Windows OS fixture NOT VERIFIED locally\n";
}
