// SPDX-License-Identifier: GPL-3.0-only
#ifndef _WIN32
#include "diagnostics_platform.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/acl.h>
#else
#include <sys/xattr.h>
#endif
#include <vector>
#include <utility>

namespace sco::diagnostics::platform {
namespace {
constexpr uint64_t kBytes = 262144;
constexpr size_t kCredentialLimit = 65536;
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
bool NoAcl(int fd) {
#ifdef __APPLE__
    acl_t acl = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
    if (!acl) return errno == ENOENT || errno == ENOTSUP;
    // Darwin acl_get_entry returns 0 for an entry (unlike Linux's 1).
    // Any stored extended ACL is rejected instead of guessing its semantics.
    acl_free(acl);
    return false;
#else
    char byte;
    const ssize_t rc = fgetxattr(fd, "system.posix_acl_access", &byte, 1);
    return rc < 0 && (errno == ENODATA || errno == ENOTSUP);
#endif
}
bool PrivateFile(int fd, struct stat& st) {
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
           (st.st_mode & 07777) == 0600 && st.st_nlink == 1 && NoAcl(fd);
}
// Traverse from / using descriptor-relative opens. Never resolve a symlink,
// accept dot components, or trust a path spelling after opening the parent.
bool Components(const std::string& path, std::vector<std::string>& parts) {
    if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos) return false;
    size_t start = 1;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        parts.push_back(part);
        start = end + 1;
    }
    return !parts.empty() && path.back() != '/';
}
int OpenDirectory(const std::vector<std::string>& parts, size_t count, bool privateLast) {
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    for (size_t i = 0; i < count; ++i) {
        int next = openat(fd, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(fd); fd = next;
        if (fd < 0) return -1;
        struct stat st{};
        const bool last = i + 1 == count && privateLast;
        if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) ||
            (last ? (st.st_uid != geteuid() || (st.st_mode & 07777) != 0700 || !NoAcl(fd)) :
                    ((st.st_uid != 0 && st.st_uid != geteuid()) ||
                     ((st.st_mode & 0022) && !(st.st_uid == 0 && (st.st_mode & S_ISVTX)))))) {
            close(fd); return -1;
        }
    }
    return fd;
}
int CredentialParent(const std::string& path, std::string& leaf) {
    std::vector<std::string> parts;
    if (!Components(path, parts) || parts.size() < 2) return -1;
    leaf = parts.back();
    return OpenDirectory(parts, parts.size() - 1, true);
}
bool SameEntry(int dir, const std::string& leaf, const struct stat& opened) {
    struct stat entry{};
    return fstatat(dir, leaf.c_str(), &entry, AT_SYMLINK_NOFOLLOW) == 0 &&
           entry.st_dev == opened.st_dev && entry.st_ino == opened.st_ino && S_ISREG(entry.st_mode);
}
std::string RuntimeRoot() {
#ifdef __APPLE__
    return "/private/tmp";
#else
    return "/tmp";
#endif
}
std::string RuntimeDirectoryName() { return "sco.diagnostics." + std::to_string(geteuid()); }
int RuntimeDirectory(bool create) {
    std::vector<std::string> parts;
    if (!Components(RuntimeRoot(), parts)) return -1;
    Fd parent(OpenDirectory(parts, parts.size(), false));
    if (parent.value < 0) return -1;
    const std::string leaf = RuntimeDirectoryName();
    if (create && mkdirat(parent.value, leaf.c_str(), 0700) != 0 && errno != EEXIST) return -1;
    parts.push_back(leaf);
    return OpenDirectory(parts, parts.size(), true);
}
bool Map(Mapping& out, bool create) {
    if (out.base || out.handle || out.bytes || out.owner || !out.name.empty()) return false;
    std::string name = MappingName();
    if (name.empty()) return false;
    Fd dir(RuntimeDirectory(create));
    if (dir.value < 0) return false;
    const std::string leaf = name.substr(name.rfind('/') + 1);
    Fd fd(openat(dir.value, leaf.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                                       (create ? O_CREAT | O_EXCL : 0), 0600));
    if (fd.value < 0) return false;
    struct stat st{};
    bool valid = (!create || fchmod(fd.value, 0600) == 0) && PrivateFile(fd.value, st);
    if (create) valid = valid && ftruncate(fd.value, kBytes) == 0;
    valid = valid && PrivateFile(fd.value, st) && st.st_size == static_cast<off_t>(kBytes) &&
            SameEntry(dir.value, leaf, st);
    void* base = valid ? mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd.value, 0) : MAP_FAILED;
    if (base == MAP_FAILED) {
        if (create) unlinkat(dir.value, leaf.c_str(), 0);
        return false;
    }
    out.name = std::move(name); out.base = base; out.bytes = kBytes;
    out.handle = static_cast<uintptr_t>(fd.value) + 1; out.owner = create; fd.value = -1;
    return true;
}
} // namespace

bool Random(void* buffer, size_t bytes) {
    if (!bytes) return true;
    if (!buffer) return false;
    auto* p = static_cast<unsigned char*>(buffer);
    while (bytes) {
        const size_t n = std::min(bytes, size_t{256});
        if (getentropy(p, n) != 0) return false;
        p += n; bytes -= n;
    }
    return true;
}
std::string MappingName() {
    const pid_t session = getsid(0);
    if (session < 0) return {};
    // macOS shm_open fstat hides mode and fchmod/flock are unsupported. Use a
    // verified private file-backed MAP_SHARED fixture rather than waive checks.
    return RuntimeRoot() + "/" + RuntimeDirectoryName() + "/sco.dg." +
           std::to_string(geteuid()) + "." + std::to_string(session) + ".mapping";
}
bool Create(Mapping& out) { return Map(out, true); }
bool Open(Mapping& out) { return Map(out, false); }
void Close(Mapping& out) {
    if (out.base) munmap(out.base, static_cast<size_t>(out.bytes));
    if (out.owner && out.handle && !out.name.empty()) {
        std::string leaf;
        Fd dir(CredentialParent(out.name, leaf));
        struct stat st{};
        const int fd = static_cast<int>(out.handle - 1);
        if (dir.value >= 0 && PrivateFile(fd, st) && SameEntry(dir.value, leaf, st))
            unlinkat(dir.value, leaf.c_str(), 0);
    }
    if (out.handle) close(static_cast<int>(out.handle - 1));
    out = Mapping{};
}
bool Acquire(Lease& out) {
    if (out.handle) return false;
    Fd dir(RuntimeDirectory(true));
    if (dir.value < 0) return false;
    const std::string name = MappingName();
    if (name.empty()) return false;
    const std::string leaf = name.substr(name.rfind('/') + 1) + ".peer";
    Fd file(openat(dir.value, leaf.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
    struct stat st{};
    if (file.value < 0 || !PrivateFile(file.value, st) || !SameEntry(dir.value, leaf, st) ||
        flock(file.value, LOCK_EX | LOCK_NB) != 0) return false;
    // flock is open-file-description backed: distinct opens conflict in the
    // same process too. Do not unlink the file (that would split lock identity).
    out.handle = static_cast<uintptr_t>(file.value) + 1; file.value = -1;
    return true;
}
void Release(Lease& out) {
    if (out.handle) { const int fd = static_cast<int>(out.handle - 1); flock(fd, LOCK_UN); close(fd); }
    out = Lease{};
}
bool WriteCredential(const std::string& path, const std::string& json) {
    if (json.empty() || json.size() > kCredentialLimit) return false;
    std::string leaf;
    Fd dir(CredentialParent(path, leaf));
    if (dir.value < 0) return false;
    unsigned char nonce[16];
    if (!Random(nonce, sizeof(nonce))) return false;
    constexpr char hex[] = "0123456789abcdef";
    std::string temporary = ".sco-credential-";
    for (auto b : nonce) { temporary.push_back(hex[b >> 4]); temporary.push_back(hex[b & 15]); }
    Fd file(openat(dir.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file.value < 0) return false;
    struct stat st{};
    bool good = fchmod(file.value, 0600) == 0 && PrivateFile(file.value, st);
    size_t done = 0;
    while (good && done < json.size()) {
        const ssize_t written = write(file.value, json.data() + done, json.size() - done);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) good = false;
        else done += static_cast<size_t>(written);
    }
    // Exclusive hard-link publication cannot replace a leftover credential.
    // The temporary link is removed before success; readers reject nlink != 1.
    good = good && fsync(file.value) == 0 && PrivateFile(file.value, st) &&
           SameEntry(dir.value, temporary, st) &&
           linkat(dir.value, temporary.c_str(), dir.value, leaf.c_str(), 0) == 0;
    const bool removed = unlinkat(dir.value, temporary.c_str(), 0) == 0;
    return good && removed && fsync(dir.value) == 0;
}
bool ReadCredential(const std::string& path, std::string& json) {
    json.clear();
    std::string leaf;
    Fd dir(CredentialParent(path, leaf));
    if (dir.value < 0) return false;
    Fd file(openat(dir.value, leaf.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    struct stat before{};
    if (file.value < 0 || !PrivateFile(file.value, before) || before.st_size <= 0 ||
        static_cast<uint64_t>(before.st_size) > kCredentialLimit || !SameEntry(dir.value, leaf, before)) return false;
    std::string result(static_cast<size_t>(before.st_size), '\0');
    size_t done = 0;
    while (done < result.size()) {
        const ssize_t got = read(file.value, result.data() + done, result.size() - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        done += static_cast<size_t>(got);
    }
    char trailing;
    ssize_t got;
    do { got = read(file.value, &trailing, 1); } while (got < 0 && errno == EINTR);
    struct stat after{};
    if (got != 0 || !PrivateFile(file.value, after) || before.st_size != after.st_size ||
        !SameEntry(dir.value, leaf, after)) return false;
    json.swap(result);
    return true;
}
void RemoveCredential(const std::string& path) {
    std::string leaf;
    Fd dir(CredentialParent(path, leaf));
    if (dir.value < 0) return;
    Fd file(openat(dir.value, leaf.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    struct stat st{};
    if (file.value >= 0 && PrivateFile(file.value, st) && SameEntry(dir.value, leaf, st))
        unlinkat(dir.value, leaf.c_str(), 0);
}
uint64_t NowMs() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}
} // namespace sco::diagnostics::platform
#endif
