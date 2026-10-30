// Compile twice: baseline registry and candidate registry, each in its own process.
#include "sco/signatures.h"
#ifndef SCO_DIFFERENTIAL_BASELINE
#include "sco/signature_registry.h"
#endif
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

struct Spec {
    std::string id;
    std::vector<std::string> needs;
    const char* pattern = nullptr;
    int disp = 0;
    int size = 0;
    int mode = 0; // 0 = pattern, 1 = success, 2 = failure, 3 = null success, 4 = lookup, 5..10 = explicit states
    std::string read;
};
static std::vector<Spec> specs;
static std::vector<int> calls;
static std::array<uint8_t, 256> bytes;

#ifdef SCO_DIFFERENTIAL_BASELINE
#define CONTEXT
#define READ_SIG(id) sco::Sig(id)
#else
#define CONTEXT , const sco::SigResolveContext& ctx
#define READ_SIG(id) ctx.Sig(id)
#endif

template <int I> static sco::SigResult Callback(const sco::Image& img CONTEXT) {
    calls.push_back(I);
    const Spec& row = specs[I];
    if (row.mode == 2) return sco::SigFail("fixture failure");
    if (row.mode == 3) return sco::SigOk(nullptr);
    if (row.mode == 4) {
        uint8_t* p = READ_SIG(row.read.c_str());
        return p ? sco::SigOk(p) : sco::SigFail("context lookup unavailable");
    }
    if (row.mode >= 5 && row.mode <= 10)
        return {static_cast<sco::SigState>(row.mode - 5), img.base + 120 + I, 7, "explicit callback state"};
    return sco::SigOk(img.base + 120 + I);
}
static void CallbackStates() {
    specs = {
        {"state.not_run", {}, nullptr, 0, 0, 5, {}},
        {"state.first_dependent", {"state.not_run"}, nullptr, 0, 0, 1, {}},
        {"state.second_dependent", {"state.not_run"}, nullptr, 0, 0, 1, {}},
        {"state.ok", {}, nullptr, 0, 0, 6, {}},
        {"state.missing", {}, nullptr, 0, 0, 7, {}},
        {"state.ambiguous", {}, nullptr, 0, 0, 8, {}},
        {"state.failed", {}, nullptr, 0, 0, 9, {}},
        {"state.blocked", {}, nullptr, 0, 0, 10, {}},
        {"state.forward_dependent", {"state.forward_not_run"}, nullptr, 0, 0, 1, {}},
        {"state.forward_not_run", {}, nullptr, 0, 0, 5, {}},
        {"state.ok_dependent", {"state.ok"}, nullptr, 0, 0, 1, {}},
        {"state.missing_dependent", {"state.missing"}, nullptr, 0, 0, 1, {}},
        {"state.ambiguous_dependent", {"state.ambiguous"}, nullptr, 0, 0, 1, {}},
        {"state.failed_dependent", {"state.failed"}, nullptr, 0, 0, 1, {}},
        {"state.blocked_dependent", {"state.blocked"}, nullptr, 0, 0, 1, {}}
    };
}
static constexpr sco::SigResolver callbacks[] = {
    Callback<0>, Callback<1>, Callback<2>, Callback<3>, Callback<4>, Callback<5>,
    Callback<6>, Callback<7>, Callback<8>, Callback<9>, Callback<10>, Callback<11>,
    Callback<12>, Callback<13>, Callback<14>, Callback<15>
};

static void Image(sco::Image& img, bool changed) {
    bytes.fill(0xCC);
    bytes[16] = changed ? 0x10 : 0x11; bytes[17] = 0x22;
    bytes[32] = bytes[40] = 0x33; bytes[33] = bytes[41] = 0x44;
    bytes[64] = 0x48; bytes[65] = 0x8B; bytes[66] = 0x05; bytes[71] = 0xC3;
    int32_t rel = 100 - (64 + 7); std::memcpy(bytes.data() + 67, &rel, sizeof rel);
    img = {}; img.base = bytes.data(); img.size = static_cast<uint32_t>(bytes.size());
    img.text = {bytes.data(), 128};
}
static void Fixed() {
    specs = {
        {"test.undeclared", {}, nullptr, 0, 0, 4, "test.unique"},
        {"test.forward", {"test.unique"}, nullptr, 0, 0, 4, "test.unique"},
        {"test.unknown_first", {"test.unknown", "test.cycle_a"}, nullptr, 0, 0, 1, {}},
        {"test.cycle_a", {"test.cycle_b"}, nullptr, 0, 0, 1, {}},
        {"test.cycle_b", {"test.cycle_a"}, nullptr, 0, 0, 1, {}},
        {"test.self", {"test.self"}, nullptr, 0, 0, 1, {}},
        {"test.failure", {}, nullptr, 0, 0, 2, {}},
        {"test.blocked", {"test.failure", "test.unique"}, nullptr, 0, 0, 1, {}},
        {"test.null", {}, nullptr, 0, 0, 3, {}},
        {"test.unique", {}, "11 22", 0, 0, 0, {}},
        {"test.ambiguous", {}, "33 44", 0, 0, 0, {}},
        {"test.missing", {}, "FE ED", 0, 0, 0, {}},
        {"test.rip", {}, "48 8B 05 ?? ?? ?? ?? C3", 3, 7, 0, {}},
        {"test.branch", {"test.unique", "test.ambiguous"}, nullptr, 0, 0, 1, {}},
        {"test.no_locator", {}, nullptr, 0, 0, 0, {}},
        {"test.independent", {}, nullptr, 0, 0, 1, {}}
    };
}
static void Generated(unsigned seed) {
    specs.clear(); specs.resize(12);
    seed += 0x9E3779B9u;
    auto next = [&seed] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    for (size_t i = 0; i < specs.size(); ++i) specs[i].id = "generated." + std::to_string(i);
    for (size_t i = 0; i < specs.size(); ++i) {
        auto& row = specs[i]; row.mode = 1 + static_cast<int>(next() % 3);
        const unsigned n = next() % 4;
        for (unsigned j = 0; j < n; ++j) {
            unsigned d = next() % 15;
            row.needs.push_back(d < specs.size() ? specs[d].id : "generated.unknown");
        }
        if (!row.needs.empty() && next() % 2) { row.mode = 4; row.read = row.needs[0]; }
    }
}
static void Chain() {
    specs.clear(); specs.resize(511);
    for (size_t i = 0; i < specs.size(); ++i) specs[i].id = "chain." + std::to_string(i);
    for (size_t i = 0; i < specs.size(); ++i) {
        specs[i].pattern = "11 22";
        if (i + 1 < specs.size()) specs[i].needs.push_back(specs[i + 1].id);
    }
}

static void PrintResult(const char* id, const sco::SigResult* r) {
    if (!r) { std::printf("ROW %s NULL\n", id); return; }
    const auto addr = reinterpret_cast<uintptr_t>(r->at), base = reinterpret_cast<uintptr_t>(bytes.data());
    long long offset = !r->at ? -1 : (addr >= base && addr < base + bytes.size() ? static_cast<long long>(addr - base) : -2);
    std::printf("ROW %s %u %d %lld %s\n", id, static_cast<unsigned>(r->state), r->matches, offset, r->why ? r->why : "-");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "fixed") == 0) Fixed();
    else if (std::strcmp(argv[1], "callback_states") == 0) CallbackStates();
    else if (std::strcmp(argv[1], "chain") == 0) Chain();
    else Generated(static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10)));
    std::vector<sco::SigDef> defs(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        if (s.needs.size() > sco::kMaxSigNeeds || (s.mode && i >= std::size(callbacks))) return 3;
        defs[i] = {s.id.c_str(), s.pattern, s.disp, s.size, s.mode ? callbacks[i] : nullptr, {}};
        for (size_t j = 0; j < s.needs.size(); ++j) defs[i].needs[j] = s.needs[j].c_str();
    }
#ifdef SCO_DIFFERENTIAL_BASELINE
    if (!sco::RegisterSignatures(defs.data(), defs.size())) return 4;
#else
    sco::SignatureRegistry registry;
    const sco::SignatureTable table{defs.data(), defs.size()};
    if (registry.RegisterTables(&table, 1).status != sco::SigRegistryStatus::Ok) return 4;
    if (registry.Freeze() != sco::SigRegistryStatus::Ok) return 5;
#endif
    sco::Image img{};
    for (int pass = 0; pass < 3; ++pass) {
        Image(img, pass == 1); calls.clear();
#ifdef SCO_DIFFERENTIAL_BASELINE
        sco::ResolveAll(img);
        std::printf("PASS %d COUNT %zu\n", pass, sco::SignatureCount());
        for (size_t i = 0; i < sco::SignatureCount(); ++i) {
            if (sco::SigLookup(sco::SignatureDef(i)->id) != &sco::SignatureResult(i)) return 8;
            PrintResult(sco::SignatureDef(i)->id, &sco::SignatureResult(i));
        }
        if (sco::SigLookup("unregistered.id")) return 9;
#else
        if (registry.Resolve(img) != sco::SigRegistryStatus::Ok) return 6;
        auto snap = registry.Snapshot();
        if (!snap) return 7;
        std::printf("PASS %d COUNT %zu\n", pass, snap->Count());
        for (size_t i = 0; i < snap->Count(); ++i) {
            if (snap->Lookup(snap->IdAt(i)) != snap->ResultAt(i)) return 8;
            PrintResult(snap->IdAt(i), snap->ResultAt(i));
        }
        if (snap->Lookup("unregistered.id")) return 9;
#endif
        std::printf("CALLS"); for (int call : calls) std::printf(" %d", call); std::printf("\n");
    }
    return 0;
}
