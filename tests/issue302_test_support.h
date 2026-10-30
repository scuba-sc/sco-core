#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace issue302 {
// Two committed pages with one made inaccessible; no sanitizer runtime required.
class GuardPages {
public:
    explicit GuardPages(bool guardFirst = false) {
#ifdef _WIN32
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page = info.dwPageSize;
        data = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD oldProtect = 0;
        if (!data || !VirtualProtect(data + (guardFirst ? 0 : page), page, PAGE_NOACCESS, &oldProtect))
            std::abort();
#else
        const long size = sysconf(_SC_PAGESIZE);
        if (size <= 0) std::abort();
        page = static_cast<size_t>(size);
        data = static_cast<uint8_t*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (data == MAP_FAILED || mprotect(data + (guardFirst ? 0 : page), page, PROT_NONE))
            std::abort();
#endif
    }
    ~GuardPages() {
#ifdef _WIN32
        VirtualFree(data, 0, MEM_RELEASE);
#else
        munmap(data, page * 2);
#endif
    }
    GuardPages(const GuardPages&) = delete;
    GuardPages& operator=(const GuardPages&) = delete;
    uint8_t* data = nullptr;
    size_t page = 0;
};
} // namespace issue302
