// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Private no-socket OS support, not an SDK service or plugin API.
namespace sco::diagnostics::platform {
struct Mapping {
    void* base = nullptr;
    uint64_t bytes = 0;
    uintptr_t handle = 0;
    bool owner = false;
    std::string name;
};
struct Lease { uintptr_t handle = 0; };
bool Random(void* buffer, size_t bytes);
std::string MappingName();
bool Create(Mapping& mapping);
bool Open(Mapping& mapping);
void Close(Mapping& mapping);
// Nonwaiting singleton. A lease is noncopyable by convention, held to peer exit.
bool Acquire(Lease& lease);
void Release(Lease& lease);
// Absolute operator-selected path, private immediate directory, no symlinks or
// reparses. Existing credentials are never overwritten. Caller additionally
// excludes checkout/plugin roots and validates the closed JSON credential schema.
bool WriteCredential(const std::string& path, const std::string& json);
bool ReadCredential(const std::string& path, std::string& json);
void RemoveCredential(const std::string& path);
uint64_t NowMs();
} // namespace sco::diagnostics::platform
