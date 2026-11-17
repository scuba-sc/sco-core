// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#pragma once
// Private. Static storage for objects that own a thread or are shared with one. Under the game host's
// process-retained composition such an object is never destroyed: a joinable std::thread's destructor
// would terminate, a join would deadlock under the loader lock at DLL detach, and a still-running
// thread must not see its shared state destroyed. Every other composition destroys it at exit exactly
// as a namespace-scope object would. The predicate is set once the retained runtime starts exposing
// anything and is never cleared. At ExitProcess the OS has already ended other threads; this makes
// the destructors themselves safe, whether or not closure ran.
#include <atomic>
#include <utility>

namespace sco::detail {
inline std::atomic<bool>& ProcessRetainedForever() noexcept {
    static std::atomic<bool> forever{false};
    return forever;
}
template<class T> struct ExitHolder {
    T* object;
    template<class... A> explicit ExitHolder(A&&... a) : object(new T(std::forward<A>(a)...)) {}
    ExitHolder(const ExitHolder&) = delete;
    ExitHolder& operator=(const ExitHolder&) = delete;
    ~ExitHolder() { if (!ProcessRetainedForever().load(std::memory_order_acquire)) delete object; }
};
// Id keeps two objects of the same type apart (use __LINE__).
template<class T, int Id, class... A> T& Held(A&&... a) {
    static ExitHolder<T> holder(std::forward<A>(a)...);
    return *holder.object;
}
}  // namespace sco::detail
