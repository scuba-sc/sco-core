// SPDX-License-Identifier: GPL-3.0-only
#include "synthetic_owned_session.h"
#include "sco/signature_registry.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace sco {
struct SyntheticOwnedSession::Backing {
    explicit Backing(std::vector<Entry> values) : entries(std::move(values)) {
        constexpr size_t width = 18;
        if (entries.size() > std::numeric_limits<size_t>::max() / width)
            throw std::length_error("synthetic image is too large");
        bytes.reserve(entries.size() * width);
        std::vector<std::string> patterns;
        std::vector<SigDef> rows;
        patterns.reserve(entries.size());
        rows.reserve(entries.size());
        constexpr char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto begin = bytes.size();
            bytes.push_back(0xff);
            const uint64_t index = static_cast<uint64_t>(i);
            for (unsigned shift = 0; shift < 64; shift += 4)
                bytes.push_back(static_cast<uint8_t>(0x10 + ((index >> shift) & 0xf)));
            bytes.push_back(0xfe);
            std::string pattern;
            pattern.reserve(width * 3 - 1);
            for (size_t j = begin; j < bytes.size(); ++j) {
                if (j != begin) pattern.push_back(' ');
                pattern.push_back(hex[bytes[j] >> 4]);
                pattern.push_back(hex[bytes[j] & 0xf]);
            }
            patterns.push_back(std::move(pattern));
        }
        for (size_t i = 0; i < entries.size(); ++i)
            rows.push_back({entries[i].key.c_str(), patterns[i].c_str(), 0, 0, nullptr, {}});
        if (!rows.empty()) {
            const SignatureTable table{rows.data(), rows.size()};
            const auto registration = registry.RegisterTables(&table, 1);
            if (registration.status == SigRegistryStatus::OutOfMemory ||
                registration.status == SigRegistryStatus::BudgetExceeded) throw std::bad_alloc();
            if (registration.status != SigRegistryStatus::Ok)
                throw std::invalid_argument("invalid synthetic signature key");
        }
        if (registry.Freeze() != SigRegistryStatus::Ok)
            throw std::runtime_error("synthetic registry freeze failed");
        Image image{};
        image.base = bytes.data();
        image.size = bytes.size();
        image.text = {bytes.data(), bytes.size()};
        // The private synthetic image and registry share this backing's lifetime.
        // Rows use patterns only; no resolver or user callback is invoked.
        if (registry.Resolve(image) != SigRegistryStatus::Ok)
            throw std::runtime_error("synthetic registry resolution failed");
        snapshot = registry.Snapshot();
        if (!snapshot || snapshot->Count() != entries.size())
            throw std::runtime_error("synthetic snapshot unavailable");
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto* result = snapshot->ResultAt(i);
            const char* const id = snapshot->IdAt(i);
            if (!id || entries[i].key != id || !result || result->state != SigState::Ok ||
                result->at != bytes.data() + i * width)
                throw std::runtime_error("synthetic signature did not resolve uniquely");
        }
    }
    const std::vector<Entry> entries;
    std::vector<uint8_t> bytes;
    SignatureRegistry registry;
    std::shared_ptr<const SignatureSnapshot> snapshot;
};
struct SyntheticOwnedSession::Capsule {
    explicit Capsule(std::vector<Entry> entries)
        : backing(std::make_shared<const Backing>(std::move(entries))) {}
    OwnedShutdown owner;
    std::shared_ptr<const Backing> backing;
    std::atomic<Status> state{Status::Active};
    Capsule* next = nullptr;
};

std::atomic<SyntheticOwnedSession::Capsule*>& SyntheticOwnedSession::retained_capsules() noexcept {
    // This is an append-only lifetime root, not a registry or lookup API. The
    // nodes are deliberately never removed: an incomplete worker/read can
    // reference its owner and backing even after the wrapper has been destroyed.
    static std::atomic<Capsule*> root{nullptr};
    return root;
}

SyntheticOwnedSession::SyntheticOwnedSession(std::vector<Entry> entries) : capsule_(nullptr) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].key.empty() || entries[i].key.find('\0') != std::string::npos ||
            (i && entries[i - 1].key == entries[i].key))
            throw std::invalid_argument("synthetic session keys must be nonempty and unique");
    }
    auto candidate = std::make_unique<Capsule>(std::move(entries));
    Capsule* const item = candidate.get();
    auto& root = retained_capsules();
    Capsule* old = root.load(std::memory_order_relaxed);
    do { item->next = old; }
    while (!root.compare_exchange_weak(old, item, std::memory_order_release, std::memory_order_relaxed));
    capsule_ = candidate.release();
}

SyntheticOwnedSession::Lease::Lease(Capsule* capsule, OwnedShutdown::Read&& read) noexcept
    : capsule_(capsule), read_(std::move(read)), thread_(std::this_thread::get_id()) {}
SyntheticOwnedSession::Lease::operator bool() const noexcept {
    return capsule_ && static_cast<bool>(read_) && thread_ == std::this_thread::get_id();
}
bool SyntheticOwnedSession::Lease::lookup(std::string_view key, uint64_t& result) const noexcept {
    if (!*this || capsule_->owner.closed()) return false;
    const auto& entries = capsule_->backing->entries;
    const auto it = std::lower_bound(entries.begin(), entries.end(), key,
        [](const Entry& entry, std::string_view name) { return entry.key < name; });
    if (it == entries.end() || it->key != key) return false;
    const auto index = static_cast<size_t>(it - entries.begin());
    const auto* resolved = capsule_->backing->snapshot->ResultAt(index);
    if (!resolved || resolved->state != SigState::Ok) return false;
    result = it->value;
    return true;
}
SyntheticOwnedSession::Lease SyntheticOwnedSession::try_acquire() {
    if (capsule_->state.load(std::memory_order_acquire) != Status::Active) return {};
    auto read = capsule_->owner.try_read();
    if (!read || capsule_->state.load(std::memory_order_acquire) != Status::Active) return {};
    return Lease(capsule_, std::move(read));
}
SyntheticOwnedSession::Status SyntheticOwnedSession::status() const noexcept {
    const auto state = capsule_->state.load(std::memory_order_acquire);
    if (state == Status::Active && capsule_->owner.closed()) return Status::FailedRetained;
    return state;
}
SyntheticOwnedSession::RetireResult SyntheticOwnedSession::retire_until(
    std::chrono::steady_clock::time_point deadline) {
    return retire_impl(deadline
#ifdef SCO_SYNTHETIC_SESSION_TESTING
                       , nullptr, nullptr
#endif
                       );
}
SyntheticOwnedSession::RetireResult SyntheticOwnedSession::retire_impl(
    std::chrono::steady_clock::time_point deadline
#ifdef SCO_SYNTHETIC_SESSION_TESTING
    , void (*before_commit)(void*), void* context
#endif
    ) {
    Status expected = Status::Active;
    if (!capsule_->state.compare_exchange_strong(expected, Status::Quiescing, std::memory_order_acq_rel))
        return RetireResult::Unavailable;
    try {
        if (capsule_->owner.drain_until(deadline)) {
#ifdef SCO_SYNTHETIC_SESSION_TESTING
            if (before_commit) before_commit(context);
#endif
            if (capsule_->owner.commit_retired(deadline)) {
                capsule_->state.store(Status::QuiescedRetained, std::memory_order_release);
                return RetireResult::QuiescedRetained;
            }
        }
    } catch (...) {}
    // A completed internal joiner can be joined even when a fault invalidated
    // retirement. A still-running joiner remains in the retained capsule.
    try { (void)capsule_->owner.reap_completed_joiner(); } catch (...) {}
    capsule_->owner.fault();
    capsule_->state.store(Status::FailedRetained, std::memory_order_release);
    return RetireResult::FailedRetained;
}
#ifdef SCO_SYNTHETIC_SESSION_TESTING
OwnedShutdown& SyntheticOwnedSession::owner_for_test() noexcept { return capsule_->owner; }
std::weak_ptr<const void> SyntheticOwnedSession::backing_for_test() const noexcept { return capsule_->backing; }
SyntheticOwnedSession::RetireResult SyntheticOwnedSession::retire_with_hook_for_test(
    std::chrono::steady_clock::time_point deadline, void (*before_commit)(void*), void* context) {
    return retire_impl(deadline, before_commit, context);
}
#endif
} // namespace sco
