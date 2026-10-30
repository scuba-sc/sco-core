// Signature rows for sc-offline's world features: build mode (build.cpp), missions (missions.cpp),
// the console and quantum cvars (cvars.cpp) and the quantum boost hooks (quantum.cpp). Each
// resolver is the scan sc-offline ran itself, moved byte for byte; where that scan took the first
// match, the row insists on the count found in 4.10.196.36804, so a game patch that adds or loses
// a site shows up as a failed row instead of a silent change.
//
// Two kinds of check are new, both read from that build: the entity vtable rows (build.cpp checked
// a live entity's slots at run time; the rows find the vtable those checks describe, and the
// product compares slot pointers with them) and build.camera_fields (build.cpp read the camera at
// fixed offsets nothing checked).
#include "sco/game/world.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sig_rows.h"
#include <cstring>
#include <iterator>

namespace sco::game {

namespace {

using namespace world;
using rows::InText;
using rows::TextAt;
using rows::CheckedRip;

const uint8_t* Offset(const Image& img, const Section& section, const uint8_t* p, size_t before, size_t after, size_t n) {
    if (before > static_cast<size_t>(PTRDIFF_MAX) || after > static_cast<size_t>(PTRDIFF_MAX)) return nullptr;
    const auto* begin = rows::RangeAt(section, p, -static_cast<ptrdiff_t>(before), 0);
    const auto* at = rows::RangeAt(section, begin, static_cast<ptrdiff_t>(after), n);
    return rows::InImage(img, at, n) ? at : nullptr;
}

const uint8_t* RdataString(const Image& img, const char* value) {
    return rows::InImage(img, img.rdata.base, img.rdata.size) ? FindCString(img.rdata, value) : nullptr;
}

SigResult Missing()          { return { SigState::Missing, nullptr, 0, nullptr }; }
SigResult Ambiguous(int n)   { return { SigState::Ambiguous, nullptr, n, nullptr }; }

// Every `r0 r1 r2 <rel32>` instruction in .text whose RIP target is `target` and for which
// `ok(insn)` holds, in address order (the memchr walk sc-offline ran). Up to max kept; returns
// the total.
template <class Ok>
int RipRefs(const Image& img, uint8_t r1, uint8_t r2, const uint8_t* target, size_t lead, size_t tail,
            uint8_t** out, int max, Ok ok) {
    if (!target || !rows::InImage(img, target, 1) || !rows::InImage(img, img.text.base, img.text.size)) return 0;
    const Section& text = img.text;
    if (!text.base || tail < 7 || lead > text.size || tail > text.size - lead) return 0;
    const size_t last = text.size - tail;
    int n = 0;
    for (size_t off = lead; off <= last; ++off) {
        uint8_t* p = text.base + off;
        if (p[0] != 0x48 || p[1] != r1 || p[2] != r2) continue;
        if (CheckedRip(img, p, 3, 7) != target || !ok(p)) continue;
        if (n < max) out[n] = p;
        ++n;
    }
    return n;
}

bool InImage(const Image& img, const uint8_t* p) { return rows::InImage(img, p, 1); }

// ---- build: free camera (build.cpp RegisteredHandler) -----------------------------------------

// The console handler registered with `name`: lea r8, [handler] at -0xF from lea rdx, [name].
SigResult RegisteredHandler(const Image& img, const char* name) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* str = RdataString(img, name);
    if (!str) return SigFail("the command name isn't in .rdata");
    uint8_t* sites[4] = {};
    const int n = RipRefs(img, 0x8D, 0x15, str, 0xF, 7, sites, 4, [](const uint8_t* p) { return BytesMatch(p - 0xF, "4C 8D 05"); });
    if (n == 0) return Missing();
    if (n > 1) return Ambiguous(n);
    const uint8_t* h = CheckedRip(img, TextAt(img, sites[0], -0xF, 7), 3, 7);
    if (!InText(img, h, 0x41)) return SigFail("handler outside .text");
    return SigOk(h);
}

bool LoadsString(const Image& img, const uint8_t* at, const char* s) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return false;
    const uint8_t* str = RdataString(img, s);
    const uintptr_t start = reinterpret_cast<uintptr_t>(img.text.base);
    const uintptr_t pos = reinterpret_cast<uintptr_t>(at);
    return str && start && pos >= start && rows::InText(img, at, 7) && BytesMatch(at, "48 8D 0D") && CheckedRip(img, at, 3, 7) == str;
}

SigResult ResolveFreeCamOn(const Image& img) {
    SigResult r = RegisteredHandler(img, "FreeCamEnable");
    if (r.state != SigState::Ok) return r;
    if (!LoadsString(img, TextAt(img, r.at, 0x23, 7), "Enabling free cam")) return SigFail("\"Enabling free cam\" not loaded at +0x23");
    return r;
}

SigResult ResolveFreeCamOff(const Image& img) {
    SigResult r = RegisteredHandler(img, "FreeCamDisable");
    if (r.state != SigState::Ok) return r;
    if (!LoadsString(img, TextAt(img, r.at, 0x11, 7), "Disabling free cam")) return SigFail("\"Disabling free cam\" not loaded at +0x11");
    if (!BytesMatch(r.at, "48 83 EC 28 80 3D ?? ?? ?? ?? 00")) return SigFail("layout changed at +0x000");
    return r;
}

constexpr rows::RipSpec kFreeCamFlag{ "build.free_cam_off", 4, 2, 7 };   // cmp byte [rip+X], 0

// ---- build: ground ray (build.cpp ResolveGroundRay / GroundRaySite) ---------------------------

SigResult ResolveRayTag(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* tag = RdataString(img, "PlanetRayIntersection");
    return tag ? SigOk(tag) : SigFail("\"PlanetRayIntersection\" isn't in .rdata");
}

bool GroundRaySite(const Image& img, const uint8_t* L) {
    const uint8_t* begin = Offset(img, img.text, L, 0xBF, 0, 0xBF + 0xF9);
    if (!begin || !BytesMatch(L - 0xBF, "48 8B 3D") || !BytesMatch(L - 0x42, "48 8B 98 C0 01 00 00")
        || !BytesMatch(L + 0x07, "C7 85 9C 00 00 00 01 01 00 00") || !BytesMatch(L + 0x26, "48 C7 85 A0 00 00 00 0F 02 00 00")
        || !BytesMatch(L + 0x80, "FF 50 30 41 B8 1E 00 00 00") || !BytesMatch(L + 0x9A, "FF D3") || !BytesMatch(L + 0xF2, "33 D2 E8"))
        return false;
    const uint8_t* release = CheckedRip(img, TextAt(img, L, 0xF4, 5), 1, 5);
    return InText(img, release, 0x14) && BytesMatch(release, "48 8B D1 48 8B 0D ?? ?? ?? ?? 48 8B 01 48 FF A0 28 02 00 00")
        && CheckedRip(img, TextAt(img, release, 3, 7), 3, 7) && CheckedRip(img, TextAt(img, release, 3, 7), 3, 7) == CheckedRip(img, TextAt(img, L, -0xBF, 7), 3, 7);
}

SigResult ResolveGroundRay(const Image& img) {
    uint8_t* sites[4] = {};
    const int n = RipRefs(img, 0x8D, 0x05, Sig("build.ray_tag"), 0, 7, sites, 4,
                          [&img](const uint8_t* p) { return GroundRaySite(img, p); });
    if (n == 0) return SigFail("no planet ray cast loads the tag with the expected layout");
    if (n > 1) return Ambiguous(n);
    return SigOk(sites[0]);
}

SigResult ResolvePhysWorld(const Image& img) {   // mov rdi, [phys world] at -0xBF
    const uint8_t* source = Offset(img, img.text, Sig("build.ground_ray"), 0xBF, 0, 7);
    const uint8_t* g = CheckedRip(img, source, 3, 7);
    return g && InImage(img, g) ? SigOk(g) : SigFail("RIP target outside the image");
}
constexpr rows::RipSpec kReleaseGrid{ "build.ground_ray", 0xF4, 1, 5 };                     // call release grid

// ---- build: entity vtable (build.cpp EntitySlotsOk / RaySlotsOk) ------------------------------

// A vtable entry as a pointer into this image: relocated in game, at the PE header's ImageBase
// in a file mapped from disk.
const uint8_t* SlotTarget(const Image& img, const uint8_t* entry) {
    if (!rows::InImage(img, entry, 8)) return nullptr;
    const uintptr_t start = reinterpret_cast<uintptr_t>(img.rdata.base);
    const uintptr_t pos = reinterpret_cast<uintptr_t>(entry);
    if (!start || pos < start || pos - start > img.rdata.size || img.rdata.size - (pos - start) < 8) return nullptr;
    uint64_t q;
    memcpy(&q, entry, 8);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(q));
    if (InText(img, p, 1) && rows::InImage(img, p, 1)) return p;
    if (!rows::InImage(img, img.base, 0x400) || img.base[0] != 'M' || img.base[1] != 'Z') return nullptr;
    int32_t nt;
    memcpy(&nt, img.base + 0x3C, 4);
    if (nt <= 0 || !rows::ImageAt(img, img.base, nt, 0x38) || memcmp(img.base + nt, "PE\0\0", 4) != 0) return nullptr;
    uint64_t base;
    memcpy(&base, img.base + nt + 0x30, 8);
    if (q < base || q - base >= img.size) return nullptr;
    p = img.base + static_cast<size_t>(q - base);
    return InText(img, p, 1) ? p : nullptr;
}

constexpr struct { size_t slot; const char* bytes; const char* why; } kEntitySlots[] = {
    { kEntitySetPositionSlot, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 56 41 57", "slot 0x2b0 changed" },
    { kEntitySetRotationSlot, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70", "slot 0x2c0 changed" },
    { kEntityGetRotationSlot, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F9 41 0F B6 F0", "slot 0x2c8 changed" },
    { kEntityRayProxySlot,    "40 53 48 83 EC 20 48 8B 89 80 02 00 00 48 8B DA 48 8B 01 FF 50 30", "slot 0x208 changed" },
    { kEntitySkipAddSlot,     "48 89 5C 24 08 57 48 83 EC 20 41 0F B6 D8 48 8B FA E8 ?? ?? ?? ?? 48 85 C0 74 12 41 B1 01", "slot 0x430 changed" },
};

SigResult ResolveEntityVtable(const Image& img) {
    const uint8_t* get = Sig("build.entity_get_rotation");
    const uint8_t* vt = nullptr;
    int refs = 0;
    const Section& rd = img.rdata;
    if (!get || !rows::InImage(img, rd.base, rd.size)) return SigFail("vtable outside .rdata");
    for (size_t o = 0; o <= rd.size && rd.size - o >= 8; o += 8)
        if (SlotTarget(img, rd.base + o) == get) { ++refs; vt = rows::RangeAt(rd, rd.base + o, -static_cast<ptrdiff_t>(kEntityGetRotationSlot), kEntitySkipAddSlot + 8); }
    if (refs == 0) return SigFail("no vtable holds build.entity_get_rotation");
    if (refs > 1) return Ambiguous(refs);
    if (!vt) return SigFail("vtable outside .rdata");
    for (const auto& s : kEntitySlots) {
        const uint8_t* f = SlotTarget(img, vt + s.slot);
        if (!f || !InText(img, f, rows::PatternBytes(s.bytes)) || !BytesMatch(f, s.bytes)) return SigFail(s.why);
    }
    return SigOk(vt);
}

template <size_t Slot>
SigResult ResolveEntitySlot(const Image& img) {
    const uint8_t* vt = Sig("build.entity_vtable");
    const uint8_t* f = SlotTarget(img, rows::RangeAt(img.rdata, vt, Slot, 8));
    return f ? SigOk(f) : SigFail("slot doesn't point into .text");
}

// ---- build: the camera build mode aims with (build.cpp Target) --------------------------------

constexpr rows::Check kCameraChecks[] = {
    { 0x000, "C5 FA 10 B0 44 6D 00 00", "camera rotation w isn't at +0x6d44" },          // vmovss xmm6, [rax+rot+0xC]
    { 0x008, "C5 FA 10 B8 38 6D 00 00", "camera rotation x isn't at +0x6d38" },          // vmovss xmm7, [rax+rot]
    { 0x010, "C5 7A 10 80 3C 6D 00 00", "camera rotation y isn't at +0x6d3c" },          // vmovss xmm8, [rax+rot+4]
    { 0x021, "C5 7A 10 88 40 6D 00 00", "camera rotation z isn't at +0x6d40" },          // vmovss xmm9, [rax+rot+8]
    { 0x032, "C5 78 10 90 20 6D 00 00", "camera position x, y aren't at +0x6d20" },      // vmovups xmm10, [rax+pos]
    { 0x043, "C5 7B 10 98 30 6D 00 00", "camera position z isn't at +0x6d30" },          // vmovsd xmm11, [rax+pos+0x10]
};
static_assert(kCameraPosition == 0x6D20 && kCameraRotation == 0x6D38, "kCameraChecks pin these offsets");

SigResult ResolveCameraFields(const Image& img) {
    const uint8_t* at = Sig("teleport.to_camera");
    if (!at) return SigFail("teleport.to_camera missing");
    at = Offset(img, img.text, at, 0, 0x17A, 0x43 + 8);
    if (!at) return SigFail("camera fields outside .text");
    for (const auto& c : kCameraChecks)
        if (!InText(img, at + c.off, 8) || !BytesMatch(at + c.off, c.bytes)) return SigFail(c.why);
    return SigOk(at);
}

// ---- missions (missions.cpp) ------------------------------------------------------------------

SigResult ResolveMissionSettings(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* fmt = RdataString(img, "[EVMissionManager] Spawn Mission Request - Parsed MissionID: %s (%s)");
    if (!fmt) return SigFail("the spawn mission request format isn't in .rdata");
    uint8_t* sites[4] = {};
    const int n = RipRefs(img, 0x8D, 0x0D, fmt, 0x4B, 7, sites, 4,
                          [](const uint8_t* p) { return BytesMatch(p - 0x4B, "48 8B 0D ?? ?? ?? ?? 83 79 0C 00"); });
    if (n == 0) return Missing();
    if (n > 1) return Ambiguous(n);
    const uint8_t* g = CheckedRip(img, TextAt(img, sites[0], -0x4B, 7), 3, 7);
    return InImage(img, g) ? SigOk(g) : SigFail("RIP target outside the image");
}

SigResult ResolveLoadAll(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* name = RdataString(img, "mission_load_all");
    if (!name) return SigFail("mission_load_all isn't in .rdata");
    uint8_t* sites[4] = {};
    const int n = RipRefs(img, 0x8D, 0x15, name, 0, 7, sites, 4, [](const uint8_t*) { return true; });
    if (n == 0) return SigFail("mission_load_all isn't referenced");
    if (n > 1) return Ambiguous(n);
    const uint8_t* lea = sites[0];
    const auto* registration = TextAt(img, lea, -0xC, 7);
    if (!registration || !BytesMatch(registration, "4C 8D 05")) return SigFail("no handler registered with mission_load_all");
    const uint8_t* h = CheckedRip(img, registration, 3, 7);
    if (!InText(img, h, 0x20) || !BytesMatch(h, "40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 50 08 83 F8 02 7C"))
        return SigFail("layout changed at +0x000");
    if (!TextAt(img, h, 0x2E, 16) || !BytesMatch(h + 0x2E, "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 A0 00 00 00")) return SigFail("layout changed at +0x02e");
    return SigOk(h);
}

constexpr rows::RipSpec kSubsumption{ "missions.load_all", 0x2E, 3, 7 };   // mov rcx, [subsumption]

SigResult ResolveFileChange(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* name = RdataString(img, "void __cdecl Subsumption::XmlFileLibrary::OnFileChange(const struct SFileChangeInfo &)");
    if (!name) return SigFail("the OnFileChange label isn't in .rdata");
    uint8_t* sites[4] = {};
    const int n = RipRefs(img, 0x8D, 0x05, name, 0, 7, sites, 4, [](const uint8_t*) { return true; });
    if (n == 0) return SigFail("the OnFileChange label isn't referenced");
    if (n > 1) return Ambiguous(n);
    const uint8_t* lea = sites[0];
    for (size_t back = 0; back < 0x400; ++back) {
        const auto* f = TextAt(img, lea, -static_cast<ptrdiff_t>(back), 20);
        if (!f) continue;
        if (!BytesMatch(f, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24")) continue;
        if (!TextAt(img, f, 0x203, 7) || !BytesMatch(f + 0x203, "48 8B 0D ?? ?? ?? ??")) return SigFail("layout changed at +0x203");
        return SigOk(f);
    }
    return SigFail("no OnFileChange prologue within 0x400 bytes before the label");
}

constexpr rows::RipSpec kXmlSystem{ "missions.file_change", 0x203, 3, 7 };   // mov rcx, [system]

SigResult ResolveCreateMission(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* name = RdataString(img, "dgs.subsumption.mission.create");
    if (!name) return SigFail("dgs.subsumption.mission.create isn't in .rdata");
    uint8_t* sites[8] = {};
    const int n = RipRefs(img, 0x8D, 0x15, name, 0x13, 7, sites, 8, [](const uint8_t* p) {
        return BytesMatch(p - 0x13, "48 8B 59 18") && BytesMatch(p - 0xF, "48 8D 05");
    });
    if (n == 0) return Missing();
    if (n > 1) return Ambiguous(n);
    const uint8_t* h = CheckedRip(img, TextAt(img, sites[0], -0xF, 7), 3, 7);
    if (!InText(img, h, 0x20) || !BytesMatch(h, "48 89 5C 24 08 57 48 83 EC 30 48 8B D9 B9 C0 00 00 00 E8"))
        return SigFail("layout changed at +0x000");
    return SigOk(h);
}

// ---- cvars (cvars.cpp) ------------------------------------------------------------------------

// The console global the "debugGUI_enable 1" calls load (mov rcx, [console]; lea rdx, [str];
// xor r9d, r9d; ...; call [rax+0x130]). Every site must load the same global.
SigResult ResolveConsole(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const uint8_t* str = RdataString(img, "debugGUI_enable 1");
    if (!str) return SigFail("\"debugGUI_enable 1\" isn't in .rdata");
    uint8_t* sites[kConsoleSites + 1] = {};
    const int n = RipRefs(img, 0x8D, 0x15, str, 7, 0x20, sites, kConsoleSites + 1, [](const uint8_t* p) {
        return BytesMatch(p - 7, "48 8B 0D") && BytesMatch(p + 7, "45 33 C9") && BytesMatch(p + 0x19, "FF 90 30 01 00 00");
    });
    if (n == 0) return Missing();
    if (n != kConsoleSites) return SigFail("console calls: the site count changed");
    const uint8_t* g = CheckedRip(img, TextAt(img, sites[0], -7, 7), 3, 7);
    for (int i = 1; i < n; ++i)
        if (CheckedRip(img, TextAt(img, sites[i], -7, 7), 3, 7) != g) return SigFail("console calls load different globals");
    return InImage(img, g) ? SigOk(g) : SigFail("RIP target outside the image");
}

struct CVar { const char* name; uint8_t registerSlot; };
constexpr CVar kCVars[] = {
    { "v_qdrive2.quantumTravelAllowed", 0x40 },
    { "v_qdrive2.quantumBoostAllowed", 0x40 },
    { "v_qdrive2.setting_ignoreBlockedBoost", 0x40 },
    { "v_qdrive2.setting_ignoreBlockedTravel", 0x40 },
    { "v_qdrive.logging", 0x40 },
    { "v_qdrive2.setting_targetLockAngularSpeedThresholdPlayer", 0x48 },
    { "v_qdrive2.setting_targetLockLinearSpeedThresholdPlayer", 0x48 },
};

// The storage a cvar registers: lea rdx, [name], then within 0x20 bytes a call [rax+slot] (or
// mov r10, [rax+slot]), and within 0x20 bytes before it lea r8, [storage].
template <int K>
SigResult ResolveCVar(const Image& img) {
    if (!rows::InImage(img, img.rdata.base, img.rdata.size)) return SigFail("invalid .rdata");
    const CVar& c = kCVars[K];
    const uint8_t* name = RdataString(img, c.name);
    if (!name) return SigFail("the cvar name isn't in .rdata");
    uint8_t* sites[4] = {};
    // The slot goes into a non-const local deliberately: a `const uint8_t` initialized from a
    // constant expression is usable in constant expressions, so clang folds the read, decides the
    // capture is unused and fails at -Werror, while MSVC still demands it. A non-const local is a
    // real odr-use for both, so the capture is required and neither compiler complains.
    uint8_t slot = c.registerSlot;
    const int n = RipRefs(img, 0x8D, 0x15, name, 0x20, 0x30, sites, 4, [slot](const uint8_t* p) {
        for (int f = 7; f <= 0x20; ++f)
            if ((BytesMatch(p + f, "FF 50") && p[f + 2] == slot) || (BytesMatch(p + f, "4C 8B 50") && p[f + 3] == slot))
                return true;
        return false;
    });
    if (n == 0) return SigFail("no registration of the cvar");
    if (n > 1) return Ambiguous(n);
    for (int b = 7; b <= 0x20; ++b) {
        const uint8_t* q = TextAt(img, sites[0], -b, 7);
        if (!q) continue;
        if (BytesMatch(q, "4C 8D 05")) {
            const uint8_t* g = CheckedRip(img, q, 3, 7);
            return InImage(img, g) ? SigOk(g) : SigFail("RIP target outside the image");
        }
    }
    return SigFail("no storage lea before the registration");
}

// ---- capabilities ----------------------------------------------------------------------------

SigResult ResolveReferenceIntTable(const Image& img) {
    const auto* getter=Sig("cvars.reference_int_get");
    const auto* setter=Sig("cvars.reference_int_set");
    if(!getter || !setter || !rows::InImage(img, img.rdata.base, img.rdata.size) || img.rdata.size<0x78) return SigFail("integer methods unavailable");
    uint8_t* found=nullptr;
    for(size_t off=0;off<=img.rdata.size-0x78;off+=sizeof(uintptr_t)) {
        auto* table=img.rdata.base+off;
        if(SlotTarget(img,table+0x10)!=getter || SlotTarget(img,table+0x30)!=setter) continue;
        const auto* f=SlotTarget(img,table+0x58); const auto* n=SlotTarget(img,table+0x70);
        if(!f || !n || !rows::InText(img,f,4) || !rows::InText(img,n,5) ||
           !BytesMatch(f,"8B 41 18 C3") || !BytesMatch(n,"48 8B 41 08 C3")) continue;
        if(found) return SigFail("multiple reference integer tables");
        found=table;
    }
    return found ? SigOk(found) : SigFail("reference integer table not found");
}

template<size_t Slot>
SigResult ResolveReferenceIntMethod(const Image& img) {
    const auto* table=Sig("cvars.reference_int_vtable");
    if(!table) return SigFail("reference integer table unavailable");
    const auto* method=SlotTarget(img,rows::RangeAt(img.rdata, table, Slot, 8));
    return method ? SigOk(method) : SigFail("integer method outside image");
}

constexpr const char* kFreeCam[] = { "build.free_cam_on", "build.free_cam_off", "build.free_cam_flag" };
constexpr const char* kGroundRay[] = {
    "build.ray_tag", "build.ground_ray", "build.phys_world", "build.release_grid",
    "build.entity_vtable", "build.entity_ray_proxy", "build.entity_skip_add",
};
constexpr const char* kEntityMove[] = {
    "build.entity_get_rotation", "build.entity_vtable", "build.entity_set_position", "build.entity_set_rotation",
};
constexpr const char* kCamera[] = { "teleport.to_camera", "build.camera_fields" };
constexpr const char* kMissionStart[] = { "missions.create", "missions.settings" };
constexpr const char* kMissionLoad[] = { "missions.load_all" };
constexpr const char* kMissionScripts[] = { "missions.load_all", "missions.subsumption", "missions.file_change", "missions.xml_system" };
constexpr const char* kConsole[] = { "cvars.console" };
constexpr const char* kReferenceInt[] = {
    "cvars.console", "cvars.lookup", "cvars.reference_int_vtable", "cvars.reference_int_get",
    "cvars.reference_int_set", "cvars.reference_int_flags", "cvars.reference_int_name",
};
constexpr const char* kQuantumCVars[] = {
    "cvars.quantum_travel_allowed", "cvars.quantum_boost_allowed", "cvars.ignore_blocked_boost",
    "cvars.ignore_blocked_travel", "cvars.qdrive_logging", "cvars.target_lock_angular", "cvars.target_lock_linear",
};
constexpr const char* kQuantumBoost[] = {
    "quantum.on_action", "quantum.start_use", "quantum.drive_input", "quantum.effect_update",
    "quantum.charge", "quantum.spline_get_y", "quantum.audio_system", "quantum.handle_valid",
};
static_assert(std::size(kQuantumCVars) == std::size(kCVars));

constexpr Capability kCaps[] = {
    { "build.free_cam",       kFreeCam,        std::size(kFreeCam) },
    { "build.ground_ray",     kGroundRay,      std::size(kGroundRay) },
    { "build.entity_move",    kEntityMove,     std::size(kEntityMove) },
    { "build.camera",         kCamera,         std::size(kCamera) },
    { "missions.start",       kMissionStart,   std::size(kMissionStart) },
    { "missions.load_all",    kMissionLoad,    std::size(kMissionLoad) },
    { "missions.scripts",     kMissionScripts, std::size(kMissionScripts) },
    { "cvars.console",        kConsole,        std::size(kConsole) },
    { "cvars.reference_int",  kReferenceInt,   std::size(kReferenceInt) },
    { "cvars.qdrive_kept_on", kQuantumCVars,   std::size(kQuantumCVars) },
    { "quantum.boost",        kQuantumBoost,   std::size(kQuantumBoost) },
};

}  // namespace

namespace world {

const Capability* Capabilities(size_t& count) {
    count = std::size(kCaps);
    return kCaps;
}

}  // namespace world

extern const SigDef kWorldSignatures[] = {
    { "build.free_cam_on",         nullptr, 0, 0, ResolveFreeCamOn,  {} },
    { "build.free_cam_off",        nullptr, 0, 0, ResolveFreeCamOff, {} },
    { "build.free_cam_flag",       nullptr, 0, 0, rows::ResolveRip<kFreeCamFlag>, { "build.free_cam_off" } },
    { "build.ray_tag",             nullptr, 0, 0, ResolveRayTag,     {} },
    { "build.ground_ray",          nullptr, 0, 0, ResolveGroundRay,  { "build.ray_tag" } },
    { "build.phys_world",          nullptr, 0, 0, ResolvePhysWorld,   { "build.ground_ray" } },
    { "build.release_grid",        nullptr, 0, 0, rows::ResolveRip<kReleaseGrid>, { "build.ground_ray" } },
    { "build.entity_get_rotation", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 48 8B F9 41 0F B6 F0", 0, 0, nullptr, {} },
    { "build.entity_vtable",       nullptr, 0, 0, ResolveEntityVtable, { "build.entity_get_rotation" } },
    { "build.entity_set_position", nullptr, 0, 0, ResolveEntitySlot<kEntitySetPositionSlot>, { "build.entity_vtable" } },
    { "build.entity_set_rotation", nullptr, 0, 0, ResolveEntitySlot<kEntitySetRotationSlot>, { "build.entity_vtable" } },
    { "build.entity_ray_proxy",    nullptr, 0, 0, ResolveEntitySlot<kEntityRayProxySlot>,    { "build.entity_vtable" } },
    { "build.entity_skip_add",     nullptr, 0, 0, ResolveEntitySlot<kEntitySkipAddSlot>,     { "build.entity_vtable" } },
    { "build.camera_fields",       nullptr, 0, 0, ResolveCameraFields, { "teleport.to_camera" } },
    { "missions.settings",         nullptr, 0, 0, ResolveMissionSettings, {} },
    { "missions.load_all",         nullptr, 0, 0, ResolveLoadAll,         {} },
    { "missions.subsumption",      nullptr, 0, 0, rows::ResolveRip<kSubsumption>, { "missions.load_all" } },
    { "missions.file_change",      nullptr, 0, 0, ResolveFileChange,      {} },
    { "missions.xml_system",       nullptr, 0, 0, rows::ResolveRip<kXmlSystem>,   { "missions.file_change" } },
    { "missions.create",           nullptr, 0, 0, ResolveCreateMission,   {} },
    { "cvars.console",                nullptr, 0, 0, ResolveConsole, {} },
    { "cvars.lookup", "48 89 54 24 10 53 48 83 EC 20 83 3D ?? ?? ?? ?? 00 48 8B D9 74 20 48 8D 0D ?? ?? ?? ?? E8", 0, 0, nullptr, {} },
    { "cvars.reference_int_get", "48 8B 41 48 8B 00 C3", 0, 0, nullptr, {} },
    { "cvars.reference_int_set", "48 89 5C 24 08 48 89 74 24 10 57 48 81 EC A0 00 00 00 48 8B 41 48 8B F2 48 8B D9 3B 10 75 09 F7 41 18 00 00 20 00 74 4E", 0, 0, nullptr, {} },
    { "cvars.reference_int_vtable", nullptr, 0, 0, ResolveReferenceIntTable, {"cvars.reference_int_get", "cvars.reference_int_set"} },
    { "cvars.reference_int_flags", nullptr, 0, 0, ResolveReferenceIntMethod<0x58>, {"cvars.reference_int_vtable"} },
    { "cvars.reference_int_name", nullptr, 0, 0, ResolveReferenceIntMethod<0x70>, {"cvars.reference_int_vtable"} },
    { "cvars.quantum_travel_allowed", nullptr, 0, 0, ResolveCVar<0>, {} },
    { "cvars.quantum_boost_allowed",  nullptr, 0, 0, ResolveCVar<1>, {} },
    { "cvars.ignore_blocked_boost",   nullptr, 0, 0, ResolveCVar<2>, {} },
    { "cvars.ignore_blocked_travel",  nullptr, 0, 0, ResolveCVar<3>, {} },
    { "cvars.qdrive_logging",         nullptr, 0, 0, ResolveCVar<4>, {} },
    { "cvars.target_lock_angular",    nullptr, 0, 0, ResolveCVar<5>, {} },
    { "cvars.target_lock_linear",     nullptr, 0, 0, ResolveCVar<6>, {} },
    { "quantum.on_action",     "40 53 41 56 48 83 EC 68 8D 82 2F FE FF FF 45 33 F6 83 F8 77 0F 87", 0, 0, nullptr, {} },
    { "quantum.start_use",     "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 48 48 8B 02 4C 8D B9 40 FE FF FF", 0, 0, nullptr, {} },
    { "quantum.drive_input",   "48 83 EC 28 8B 02 05 2F FE FF FF 83 F8 77 0F 87", 0, 0, nullptr, {} },
    { "quantum.effect_update", "48 8B C4 C5 FA 11 50 18 55 53 56 57 41 57 48 8D A8 B8 FC FF FF 48 81 EC 20 04 00 00", 0, 0, nullptr, {} },
    { "quantum.charge",        "48 8B C4 53 57 48 81 EC B8 00 00 00 48 89 70 E8 41 0F B6 F9 C5 F8 29 70 D8 C5 F8 29 78 C8", 0, 0, nullptr, {} },
    { "quantum.spline_get_y",  "48 89 5C 24 10 55 48 8D 6C 24 A9 48 81 EC 90 00 00 00 48 8B D9 C7 45 F7 00 29 00 00 33 C9", 0, 0, nullptr, {} },
    // mov rcx, [audio system] at +0x2F of the match: the global it reads.
    { "quantum.audio_system",  "83 79 10 00 4C 8D 41 10 75 33 48 8B 41 08 48 B9 FF FF FF FF FF FF 00 00 "
                               "48 8B D0 48 23 D1 74 1D 48 B9 00 00 00 00 00 00 FF 3F 48 85 C1 74 0E 48 8B 0D", 0x32, 0x36, nullptr, {} },
    { "quantum.handle_valid",  "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 19 48 8B F9 48 85 DB 74 ?? "
                               "48 B8 FF FF FF FF FF FF 00 00 48 8B F3 48 23 F0 48 8B CE E8 ?? ?? ?? ?? 48 8B E8 0F B7 40 04 "
                               "66 83 F8 04 74 ?? 48 C1 EB 30 B9 FF 0F 00 00 66 23 D9 66 39 5D 02 75 ?? 66 83 F8 02", 0, 0, nullptr, {} },
};
extern const size_t kWorldSignatureCount = std::size(kWorldSignatures);

}  // namespace sco::game
