// Capability registry: a fixed table of names, each ready or not with a reason.
#include "sco/caps.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include <cstdio>
#include <cstring>
#include <mutex>

namespace sco::caps {

namespace {

struct Cap {
    char name[kMaxNameLen + 1];
    bool ready;
    char reason[kMaxReasonLen + 1];
};

std::mutex g_lock;
Cap        g_caps[kMaxCaps];
size_t     g_count = 0;

// Segments of [a-z0-9_] joined by '.', 1..kMaxNameLen characters.
bool ValidName(const char* n) {
    if (!n || !*n || strnlen(n, kMaxNameLen + 1) > kMaxNameLen) return false;
    char prev = '.';
    for (const char* p = n; *p; ++p) {
        const char c = *p;
        if (c == '.') {
            if (prev == '.') return false;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
        prev = c;
    }
    return prev != '.';
}

Cap* Find(const char* name) {
    for (size_t i = 0; i < g_count; ++i)
        if (strcmp(g_caps[i].name, name) == 0) return &g_caps[i];
    return nullptr;
}

}  // namespace

Result Set(const char* name, bool ready, const char* reason) {
    if (!ValidName(name)) return Result::BadArg;
    std::lock_guard<std::mutex> hold(g_lock);
    Cap* c = Find(name);
    if (!c) {
        if (g_count == kMaxCaps) return Result::TooMany;
        c = &g_caps[g_count++];
        snprintf(c->name, sizeof(c->name), "%s", name);
    }
    c->ready = ready;
    snprintf(c->reason, sizeof(c->reason), "%s", reason ? reason : "");
    return Result::Ok;
}

namespace {

struct SignatureUpdate {
    const char* name;
    const char* const* ids;
    size_t count;
    const SignatureRegistry* owner;
    const SignatureSnapshot* expected;
    Result result = Result::Unavailable;
};

// Called under the host ownership lock. Never re-enter host composition here.
void SetFromCurrentSnapshot(void* opaque) noexcept {
    auto& request = *static_cast<SignatureUpdate*>(opaque);
    try {
        const auto snapshot = request.owner->Snapshot();
        if (!snapshot || (request.expected && snapshot.get() != request.expected)) return;
        char why[kMaxReasonLen + 1] = "";
        bool ready = true;
        for (size_t i = 0; i < request.count && ready; ++i) {
            const SigResult* r = snapshot->Lookup(request.ids[i]);
            if (!r) {
                snprintf(why, sizeof(why), "unknown signature %s", request.ids[i]);
                ready = false;
            } else if (r->state != SigState::Ok) {
                snprintf(why, sizeof(why), "needs %s (%s)", request.ids[i], SigStateName(r->state));
                ready = false;
            }
        }
        request.result = Set(request.name, ready, ready ? nullptr : why);
    } catch (...) {
        request.result = Result::Unavailable;
    }
}

// Only change capability state when the ownership lock confirms no active host.
void SetWithoutHost(void* opaque) noexcept {
    auto& request = *static_cast<SignatureUpdate*>(opaque);
    try {
        const Result result = Set(request.name, false, "signature snapshot unavailable");
        request.result = result == Result::Ok ? Result::Unavailable : result;
    } catch (...) {
        request.result = Result::Unavailable;
    }
}

bool ValidSignatureInput(const char* name, const char* const* ids, size_t n) {
    if (!ids || n == 0) return false;
    for (size_t i = 0; i < n; ++i) if (!ids[i]) return false;
    return ValidName(name);
}

} // namespace

Result SetFromSignatures(const char* name, const char* const* sigIds, size_t n,
                         const SignatureSnapshot& signatures) {
    if (!ValidSignatureInput(name, sigIds, n)) return Result::BadArg;
    const auto owner = AcquireHostSignatureRegistry();
    if (!owner) return Result::Unavailable;
    SignatureUpdate request{name, sigIds, n, owner.get(), &signatures};
    if (!WithHostSignatureRegistry(owner, SetFromCurrentSnapshot, &request)) return Result::Unavailable;
    return request.result;
}

Result SetFromSignatures(const char* name, const char* const* sigIds, size_t n) {
    if (!ValidSignatureInput(name, sigIds, n)) return Result::BadArg;
    const auto owner = AcquireHostSignatureRegistry();
    SignatureUpdate request{name, sigIds, n, owner.get(), nullptr};
    if (owner) {
        if (!WithHostSignatureRegistry(owner, SetFromCurrentSnapshot, &request)) return Result::Unavailable;
    } else if (!WithNoHostSignatureRegistry(SetWithoutHost, &request)) {
        return Result::Unavailable;
    }
    return request.result;
}

void InvalidateAll(const char* reason) noexcept {
    std::lock_guard<std::mutex> hold(g_lock);
    for (size_t i = 0; i < g_count; ++i) {
        g_caps[i].ready = false;
        snprintf(g_caps[i].reason, sizeof(g_caps[i].reason), "%s", reason ? reason : "");
    }
}

bool Has(const char* name) {
    if (!name) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    const Cap* c = Find(name);
    return c && c->ready;
}

size_t List(Entry* out, size_t max) {
    std::lock_guard<std::mutex> hold(g_lock);
    if (out)
        for (size_t i = 0; i < g_count && i < max; ++i) {
            memcpy(out[i].name, g_caps[i].name, sizeof(out[i].name));
            out[i].ready = g_caps[i].ready;
            memcpy(out[i].reason, g_caps[i].reason, sizeof(out[i].reason));
        }
    return g_count;
}

}  // namespace sco::caps
