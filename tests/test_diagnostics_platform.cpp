// SPDX-License-Identifier: GPL-3.0-only
// Standalone no-game fixture; compile with the matching diagnostics_platform TU.
#include "../src/ipc/diagnostics_platform.h"
#include <array>
// Fixture operations and checks must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/acl.h>
#include <membership.h>
#endif
#endif
namespace p = sco::diagnostics::platform;

int ChildLease(bool expectBusy) {
    p::Mapping peer;
    if (!p::Open(peer) || peer.owner || peer.bytes != 262144 ||
        static_cast<unsigned char*>(peer.base)[262143] != 0x72) return 2;
    p::Close(peer);
    p::Lease child;
    const bool acquired = p::Acquire(child);
    if (acquired) p::Release(child);
    return acquired != expectBusy ? 0 : 1;
}
void ProbeChild(bool busy) {
#ifdef _WIN32
    char executable[MAX_PATH];
    assert(GetModuleFileNameA(nullptr, executable, MAX_PATH));
    std::string cmd = std::string("\"") + executable + "\" " + (busy ? "busy" : "free");
    STARTUPINFOA startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    assert(CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
    assert(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD exitCode = 1; assert(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    assert(exitCode == 0);
#else
    const pid_t child = fork(); assert(child >= 0);
    if (!child) _exit(ChildLease(busy));
    int status = 0; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif
}
int main(int argc, char** argv) {
    if (argc == 2) return ChildLease(std::strcmp(argv[1], "busy") == 0);
    std::array<unsigned char, 32> a{}, b{};
    assert(p::Random(a.data(), a.size())); assert(p::Random(b.data(), b.size()));
    assert(a != b); assert(!p::Random(nullptr, 1)); assert(p::Random(nullptr, 0));
    std::array<unsigned char, 1024> large{}; assert(p::Random(large.data(), large.size()));
    const uint64_t started = p::NowMs(); assert(started != 0);
    p::Mapping absent; assert(!p::Open(absent));
    p::Mapping host, duplicate, peer;
    assert(p::Create(host)); assert(host.owner && host.base && host.bytes == 262144);
    assert(host.name == p::MappingName()); assert(!p::Create(duplicate));
    assert(p::Open(peer)); assert(!peer.owner && peer.bytes == 262144);
    assert(!p::Create(host)); assert(!p::Open(peer)); // Existing live handles remain intact.
    static_cast<unsigned char*>(host.base)[262143] = 0x72;
    assert(static_cast<unsigned char*>(peer.base)[262143] == 0x72);
    p::Close(peer);
    assert(p::Open(peer)); // Closing a peer must never unlink the host mapping.
    p::Lease lease, second;
    assert(p::Acquire(lease)); assert(!p::Acquire(second));
    bool threadAcquired = true;
    std::thread duplicateThread([&] { p::Lease contender; threadAcquired = p::Acquire(contender);
                                     if (threadAcquired) p::Release(contender); });
    duplicateThread.join(); assert(!threadAcquired);
    ProbeChild(true);
    std::thread releaseThread([&] { p::Release(lease); }); releaseThread.join();
    ProbeChild(false);
    assert(p::Acquire(second)); p::Release(second);
    p::Close(peer); p::Close(host); p::Close(host);
    assert(!p::Open(absent));
#ifndef _WIN32
    const std::string name = p::MappingName();
    int fd = open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600); assert(fd >= 0);
    assert(ftruncate(fd, 262143) == 0); assert(!p::Open(absent));
    assert(ftruncate(fd, 262145) == 0); assert(!p::Open(absent));
    assert(ftruncate(fd, 262144) == 0); assert(fchmod(fd, 0644) == 0);
    assert(!p::Open(absent)); close(fd); assert(unlink(name.c_str()) == 0);
#endif
#ifndef _WIN32
    const char* scratch = std::getenv("SCO_DIAGNOSTICS_TEST_TMPDIR");
    char resolved[4096]; assert(realpath(scratch ? scratch : ".", resolved));
    std::string templatePath = std::string(resolved) + "/diagnostics-credential-XXXXXX";
    char* created = mkdtemp(templatePath.data()); assert(created);
    const std::string dir(created), path = dir + "/session.json";
    const std::string payload = "{\"credential_version\":1,\"synthetic\":true}";
    std::string read = "poison";
    assert(!p::ReadCredential(path, read) && read.empty());
    assert(p::WriteCredential(path, payload));
    assert(p::ReadCredential(path, read) && read == payload);
    struct stat st{}; assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(stat(dir.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    assert(!p::WriteCredential(path, "different"));
    assert(p::ReadCredential(path, read) && read == payload);
    assert(!p::WriteCredential("relative/session.json", payload));
    assert(!p::WriteCredential(dir + "/../session.json", payload));
    assert(!p::WriteCredential(dir + "/./session.json", payload));
    assert(!p::WriteCredential(path + std::string("\0extra", 6), payload));
    assert(!p::WriteCredential(dir + "/empty", ""));
    assert(!p::WriteCredential(dir + "/oversize", std::string(65537, 'x')));
    assert(chmod(path.c_str(), 0644) == 0);
    read = "poison"; assert(!p::ReadCredential(path, read) && read.empty());
    p::RemoveCredential(path); assert(access(path.c_str(), F_OK) == 0);
    assert(chmod(path.c_str(), 0600) == 0);
    assert(chmod(dir.c_str(), 0755) == 0);
    assert(!p::ReadCredential(path, read)); assert(!p::WriteCredential(dir + "/new", payload));
    assert(chmod(dir.c_str(), 0700) == 0);
    const std::string alias = dir + "/alias";
    assert(link(path.c_str(), alias.c_str()) == 0);
    assert(!p::ReadCredential(path, read)); p::RemoveCredential(path);
    assert(access(path.c_str(), F_OK) == 0); assert(unlink(alias.c_str()) == 0);
    assert(symlink(path.c_str(), alias.c_str()) == 0);
    assert(!p::ReadCredential(alias, read)); assert(!p::WriteCredential(alias, payload));
    p::RemoveCredential(alias); assert(lstat(alias.c_str(), &st) == 0 && S_ISLNK(st.st_mode));
    assert(unlink(alias.c_str()) == 0);
    const std::string linkedDir = dir + "-link";
    assert(symlink(dir.c_str(), linkedDir.c_str()) == 0);
    assert(!p::ReadCredential(linkedDir + "/session.json", read));
    assert(!p::WriteCredential(linkedDir + "/new", payload));
    p::RemoveCredential(linkedDir + "/session.json"); assert(access(path.c_str(), F_OK) == 0);
    assert(unlink(linkedDir.c_str()) == 0);
    assert(mkfifo(alias.c_str(), 0600) == 0); assert(!p::ReadCredential(alias, read));
    p::RemoveCredential(alias); assert(lstat(alias.c_str(), &st) == 0 && S_ISFIFO(st.st_mode));
    assert(unlink(alias.c_str()) == 0);
#ifdef __APPLE__
    int aclFd = open(path.c_str(), O_RDONLY); assert(aclFd >= 0);
    acl_t broadAcl = acl_init(1); assert(broadAcl);
    acl_entry_t entry; assert(acl_create_entry(&broadAcl, &entry) == 0);
    assert(acl_set_tag_type(entry, ACL_EXTENDED_ALLOW) == 0);
    uuid_t group; assert(mbr_gid_to_uuid(getgid(), group) == 0);
    assert(acl_set_qualifier(entry, group) == 0);
    acl_permset_t permissions; assert(acl_get_permset(entry, &permissions) == 0);
    assert(acl_add_perm(permissions, ACL_READ_DATA) == 0);
    assert(acl_set_fd_np(aclFd, broadAcl, ACL_TYPE_EXTENDED) == 0); acl_free(broadAcl);
    assert(fstat(aclFd, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(!p::ReadCredential(path, read)); p::RemoveCredential(path);
    assert(access(path.c_str(), F_OK) == 0);
    acl_t emptyAcl = acl_init(0); assert(emptyAcl);
    assert(acl_set_fd_np(aclFd, emptyAcl, ACL_TYPE_EXTENDED) == 0); acl_free(emptyAcl); close(aclFd);
    assert(p::ReadCredential(path, read) && read == payload);
#endif
    int credentialFd = open(path.c_str(), O_WRONLY); assert(credentialFd >= 0);
    assert(ftruncate(credentialFd, 65537) == 0); close(credentialFd);
    assert(!p::ReadCredential(path, read) && read.empty());
    p::RemoveCredential(path); assert(access(path.c_str(), F_OK) != 0);
    assert(p::WriteCredential(path, payload)); p::RemoveCredential(path);
    assert(rmdir(dir.c_str()) == 0);
#else
    // Synthetic owner-only fixture from the current token, not a distinct-user
    // or administrator-enforced privilege-boundary proof.
    HANDLE token = nullptr; assert(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD bytes = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> tokenInfo(bytes);
    assert(GetTokenInformation(token, TokenUser, tokenInfo.data(), bytes, &bytes)); CloseHandle(token);
    LPWSTR sid = nullptr;
    assert(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenInfo.data())->User.Sid, &sid));
    const std::wstring ownSid(sid); LocalFree(sid);
    const std::wstring ownSddl = L"O:" + ownSid + L"D:P(A;;FA;;;" + ownSid + L")";
    const std::wstring broadSddl = L"O:" + ownSid + L"D:P(A;;FA;;;" + ownSid + L")(A;ID;GR;;;WD)";
    PSECURITY_DESCRIPTOR ownSd = nullptr, broadSd = nullptr;
    assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(ownSddl.c_str(), SDDL_REVISION_1, &ownSd, nullptr));
    assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(broadSddl.c_str(), SDDL_REVISION_1, &broadSd, nullptr));
    SECURITY_ATTRIBUTES ownSa{sizeof(SECURITY_ATTRIBUTES), ownSd, FALSE};
    SECURITY_ATTRIBUTES broadSa{sizeof(SECURITY_ATTRIBUTES), broadSd, FALSE};
    HANDLE hostile = CreateFileMappingW(INVALID_HANDLE_VALUE, &broadSa, PAGE_READWRITE, 0, 262144,
                                       L"Local\\SCO_sco.diagnostics");
    assert(hostile && GetLastError() != ERROR_ALREADY_EXISTS);
    assert(!p::Open(absent)); assert(!p::Create(duplicate)); CloseHandle(hostile);
    hostile = CreateFileMappingW(INVALID_HANDLE_VALUE, &ownSa, PAGE_READWRITE, 0, 266240,
                                 L"Local\\SCO_sco.diagnostics");
    assert(hostile && GetLastError() != ERROR_ALREADY_EXISTS);
    assert(!p::Open(absent)); CloseHandle(hostile);
    hostile = CreateMutexW(&broadSa, FALSE, L"Local\\SCO_sco.diagnostics.peer-lease");
    assert(hostile); assert(!p::Acquire(second)); CloseHandle(hostile);
    wchar_t temp[MAX_PATH]; assert(GetTempPathW(MAX_PATH, temp));
    const std::wstring dir = std::wstring(temp) + L"sco-diagnostics-fixture-" + std::to_wstring(GetCurrentProcessId());
    assert(CreateDirectoryW(dir.c_str(), &ownSa));
    const std::wstring widePath = dir + L"\\session.json";
    const auto utf8 = [](const std::wstring& value) {
        const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        assert(n > 0); std::string result(n, '\0');
        assert(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                  result.data(), n, nullptr, nullptr) == n);
        return result;
    };
    const std::string path = utf8(widePath), payload = "{\"credential_version\":1,\"synthetic\":true}";
    std::string read = "poison";
    assert(!p::ReadCredential(path, read) && read.empty());
    assert(p::WriteCredential(path, payload)); assert(p::ReadCredential(path, read) && read == payload);
    assert(!p::WriteCredential(path, "different")); assert(p::ReadCredential(path, read) && read == payload);
    assert(!p::WriteCredential("relative\\session.json", payload));
    assert(!p::WriteCredential(utf8(dir) + "\\..\\session.json", payload));
    assert(!p::WriteCredential(path + ":stream", payload));
    assert(!p::WriteCredential(path + std::string("\0extra", 6), payload));
    assert(!p::WriteCredential(utf8(dir) + "\\oversize", std::string(65537, 'x')));
    const auto setDacl = [](const std::wstring& name, PSECURITY_DESCRIPTOR descriptor, bool directory) {
        HANDLE file = CreateFileW(name.c_str(), WRITE_DAC | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
        assert(file != INVALID_HANDLE_VALUE);
        PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        assert(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present);
        assert(SetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                               nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS);
        CloseHandle(file);
    };
    setDacl(widePath, broadSd, false);
    assert(!p::ReadCredential(path, read) && read.empty()); p::RemoveCredential(path);
    assert(GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES);
    setDacl(widePath, ownSd, false);
    setDacl(dir, broadSd, true);
    assert(!p::ReadCredential(path, read)); assert(!p::WriteCredential(utf8(dir) + "\\new", payload));
    setDacl(dir, ownSd, true);
    const std::wstring alias = dir + L"\\alias";
    assert(CreateHardLinkW(alias.c_str(), widePath.c_str(), nullptr));
    assert(!p::ReadCredential(path, read)); p::RemoveCredential(path);
    assert(GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(alias.c_str()));
    // Symlink fixture requires Developer Mode or SeCreateSymbolicLinkPrivilege.
    const DWORD symlinkFlags = 0x2; // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    if (CreateSymbolicLinkW(alias.c_str(), widePath.c_str(), symlinkFlags)) {
        assert(!p::ReadCredential(utf8(alias), read)); p::RemoveCredential(utf8(alias));
        assert(GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES); assert(DeleteFileW(alias.c_str()));
    } else {
        std::cout << "Windows leaf-reparse fixture SKIPPED (symlink privilege/Developer Mode unavailable)\n";
    }
    const std::wstring linkedDir = dir + L"-link";
    if (CreateSymbolicLinkW(linkedDir.c_str(), dir.c_str(), symlinkFlags | SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        assert(!p::ReadCredential(utf8(linkedDir + L"\\session.json"), read));
        assert(!p::WriteCredential(utf8(linkedDir + L"\\new"), payload));
        p::RemoveCredential(utf8(linkedDir + L"\\session.json"));
        assert(GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES); assert(RemoveDirectoryW(linkedDir.c_str()));
    } else {
        std::cout << "Windows ancestor-reparse fixture SKIPPED (symlink privilege/Developer Mode unavailable)\n";
    }
    p::RemoveCredential(path); assert(!p::ReadCredential(path, read));
    assert(RemoveDirectoryW(dir.c_str())); LocalFree(ownSd); LocalFree(broadSd);
#endif
    assert(p::NowMs() >= started);
    std::cout << "diagnostics platform mapping/random/lease/credential fixture passed\n";
}
