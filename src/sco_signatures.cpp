#include "sco/signatures.h"
#include "sco/log.h"
#include <cstring>

namespace sco {

namespace {

constexpr size_t kMaxRows = 512;

struct Row { const SigDef* def; SigResult res; uint8_t visiting; };

Row    g_rows[kMaxRows];
size_t g_count = 0;

Row* Find(const char* id) {
    for (size_t i = 0; i < g_count; ++i)
        if (strcmp(g_rows[i].def->id, id) == 0) return &g_rows[i];
    return nullptr;
}

const Image* g_img = nullptr;

void Resolve(Row& r) {
    if (r.res.state != SigState::NotRun) return;
    if (r.visiting) { r.res = SigFail("circular dependency"); return; }
    r.visiting = 1;
    for (const char* need : r.def->needs) {
        if (!need) break;
        Row* dep = Find(need);
        if (!dep) { r.res = SigFail("needs an unknown signature"); r.visiting = 0; return; }
        Resolve(*dep);
        if (dep->res.state != SigState::Ok) {
            r.res = { SigState::Blocked, nullptr, 0, dep->def->id };
            r.visiting = 0;
            return;
        }
    }
    if (r.def->resolve) {
        r.res = r.def->resolve(*g_img);
    } else if (r.def->pattern) {
        r.res = SigPattern(g_img->text, r.def->pattern);
        if (r.res.state == SigState::Ok && r.def->ripDisp > 0) {
            const auto source = reinterpret_cast<uintptr_t>(r.res.at);
            const auto text = reinterpret_cast<uintptr_t>(g_img->text.base);
            const auto base = reinterpret_cast<uintptr_t>(g_img->base);
            const auto disp = static_cast<size_t>(r.def->ripDisp);
            const auto size = r.def->ripSize > 0 ? static_cast<size_t>(r.def->ripSize) : 0;
            if (!base || !g_img->size || g_img->size > UINTPTR_MAX - base) {
                r.res = SigFail("image range missing or invalid");
            } else if (!text || g_img->text.size > UINTPTR_MAX - text || source < text ||
                       source - text > g_img->text.size || size > g_img->text.size - (source - text)) {
                r.res = SigFail("RIP source instruction outside .text");
            } else if (disp > size || size - disp < sizeof(int32_t)) {
                r.res = SigFail("RIP displacement outside instruction");
            } else {
                const auto next = source + size;
                const int32_t delta = Rel32(r.res.at + disp);
                const auto magnitude = delta < 0 ? uintptr_t{0} - static_cast<uintptr_t>(delta) : static_cast<uintptr_t>(delta);
                const bool overflow = delta < 0 ? magnitude > next : magnitude > UINTPTR_MAX - next;
                const auto target = delta < 0 ? next - magnitude : next + magnitude;
                if (overflow || target < base || target - base >= g_img->size)
                    r.res = SigFail("RIP target outside the image");
                else r.res.at = reinterpret_cast<uint8_t*>(target);
            }
        }
    } else {
        r.res = SigFail("row has neither a pattern nor a resolver");
    }
    if (r.res.state == SigState::Ok && !r.res.at) r.res = SigFail("resolver returned no address");
    r.visiting = 0;
}

}  // namespace

const char* SigStateName(SigState s) {
    switch (s) {
    case SigState::NotRun:    return "NOT RUN";
    case SigState::Ok:        return "OK";
    case SigState::Missing:   return "MISSING";
    case SigState::Ambiguous: return "AMBIG";
    case SigState::Failed:    return "FAILED";
    case SigState::Blocked:   return "BLOCKED";
    }
    return "?";
}

SigResult SigPattern(const Section& text, const char* pattern) {
    int n = 0;
    uint8_t* at = FindUniquePattern(text, pattern, n);
    if (n == 0) return { SigState::Missing, nullptr, 0, nullptr };
    if (n > 1)  return { SigState::Ambiguous, nullptr, n, nullptr };
    return { SigState::Ok, at, 1, nullptr };
}

bool RegisterSignatures(const SigDef* rows, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (g_count == kMaxRows) { Log("[core] signature table full at %s", rows[i].id); return false; }
        if (Find(rows[i].id))    { Log("[core] duplicate signature id %s", rows[i].id); return false; }
        g_rows[g_count++] = { &rows[i], {}, 0 };
    }
    return true;
}

void ResolveAll(const Image& img) {
    g_img = &img;
    for (size_t i = 0; i < g_count; ++i) { g_rows[i].res = {}; g_rows[i].visiting = 0; }
    const auto imageBase = reinterpret_cast<uintptr_t>(img.base);
    const auto sectionValid = [&](const Section& section) {
        if (!section.base && !section.size) return true;
        const auto at = reinterpret_cast<uintptr_t>(section.base);
        return imageBase && img.size && img.size <= UINTPTR_MAX - imageBase && at >= imageBase &&
            at - imageBase <= img.size && section.size <= img.size - (at - imageBase);
    };
    if (!sectionValid(img.text) || !sectionValid(img.rdata) || !sectionValid(img.pdata)) {
        for (size_t i = 0; i < g_count; ++i) g_rows[i].res = SigFail("declared section outside the image");
        g_img = nullptr;
        return;
    }
    for (size_t i = 0; i < g_count; ++i) Resolve(g_rows[i]);
    g_img = nullptr;
}

const SigResult* SigLookup(const char* id) {
    const Row* r = Find(id);
    return r ? &r->res : nullptr;
}

uint8_t* Sig(const char* id) {
    const SigResult* r = SigLookup(id);
    return r && r->state == SigState::Ok ? r->at : nullptr;
}

bool SigReady(const char* id) { return Sig(id) != nullptr; }

size_t           SignatureCount()          { return g_count; }
const SigDef*    SignatureDef(size_t i)    { return i < g_count ? g_rows[i].def : nullptr; }
const SigResult& SignatureResult(size_t i) { static const SigResult none{}; return i < g_count ? g_rows[i].res : none; }

void LogSignatureReport(bool verbose) {
    size_t ok = 0;
    for (size_t i = 0; i < g_count; ++i) ok += g_rows[i].res.state == SigState::Ok;
    Log("[core] signatures: %zu/%zu OK", ok, g_count);
    for (size_t i = 0; i < g_count; ++i) {
        const Row& r = g_rows[i];
        switch (r.res.state) {
        case SigState::Ok:
            if (verbose) Log("[core] OK       %s", r.def->id);
            break;
        case SigState::Missing:   Log("[core] MISSING  %s (pattern: 0 matches)", r.def->id); break;
        case SigState::Ambiguous: Log("[core] AMBIG    %s (pattern: %d matches)", r.def->id, r.res.matches); break;
        case SigState::Blocked:   Log("[core] BLOCKED  %s (needs %s)", r.def->id, r.res.why); break;
        case SigState::Failed:    Log("[core] FAILED   %s (%s)", r.def->id, r.res.why ? r.res.why : "no reason"); break;
        case SigState::NotRun:    Log("[core] NOT RUN  %s", r.def->id); break;
        }
    }
}

}  // namespace sco
