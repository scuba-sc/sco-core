// Signature rows for teleport. Moved from sc-offline src/teleport.cpp (ResolveTeleportApi and
// its two slot checks) without changing a byte or an offset.
#include "sco/game/teleport.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstring>

namespace sco::game {

namespace {

bool VerifyEntityPositionSlots(const Image& img) {
    const uint8_t* fmt = FindCString(img.rdata, "Zone: %s, ZonePos:(%f.2,%f.2,%f.2), WorldPos((%f.2,%f.2,%f.2)");
    const uint8_t* lea = fmt ? FindRipLea(img.text, 0x4C, 0x8D, 0x05, fmt) : nullptr;
    if (!lea) return false;
    static const uint8_t callWorld[] = { 0xFF, 0x90, 0x18, 0x03, 0x00, 0x00 };
    static const uint8_t callLocal[] = { 0xFF, 0x90, 0xB8, 0x02, 0x00, 0x00 };
    static const uint8_t callZone[]  = { 0xFF, 0x90, 0xB8, 0x06, 0x00, 0x00 };
    static const uint8_t zoneName[]  = { 0x48, 0x8B, 0x91, 0x18, 0x02, 0x00, 0x00 };
    static const struct { ptrdiff_t off; const uint8_t* bytes; size_t n; } kChecks[] = {
        { -0xFA, callWorld, 6 }, { -0xD3, callWorld, 6 }, { -0xAC, callWorld, 6 },
        { -0x86, callLocal, 6 }, { -0x5F, callLocal, 6 }, { -0x38, callLocal, 6 },
        { -0x19, callZone, 6 },  { -0x10, zoneName, 7 },
    };
    for (const auto& c : kChecks)
        if (memcmp(lea + c.off, c.bytes, c.n) != 0) return false;
    return true;
}

bool VerifyZoneSlots(const Image& img) {
    const uint8_t* fmt = FindCString(img.rdata,
        "Changing reference point to unstreamable parent zone: new zone: %s (%llu) old zone: %s (%llu)");
    const uint8_t* lea = fmt ? FindRipLea(img.text, 0x48, 0x8D, 0x15, fmt) : nullptr;
    return lea && BytesMatch(lea - 0x1D4, "FF 50 60") && BytesMatch(lea - 0x1B7, "FF 50 08")
        && BytesMatch(lea - 0x1AB, "FF 50 08") && BytesMatch(lea - 0x2F, "FF 50 58");
}

SigResult ResolveToCamera(const Image& img, const SigResolveContext&) {
    const uint8_t* name = FindCString(img.rdata, "CmdTeleportToCamera");
    uint8_t* lea = name ? FindRipLea(img.text, 0x48, 0x8D, 0x15, name) : nullptr;
    if (!lea) return SigFail("CmdTeleportToCamera isn't referenced");
    uint8_t* f = lea - 0x158;
    // The camera fields at +0x17A..+0x1BD are read from an object whose layout grows between game
    // builds (all four moved by 8 in 4.10.196: 0x6D3C -> 0x6D44 ...), and nothing here or in
    // sc-offline reads them: the checks pin the instructions, not the field offsets.
    static const struct { size_t off; const char* bytes; const char* why; } kChecks[] = {
        { 0x000, "40 55",                   "layout changed at +0x000" },
        { 0x024, "48 8B 05 ?? ?? ?? ??",    "layout changed at +0x024" },
        { 0x02B, "48 8B 88 E0 00 00 00",    "layout changed at +0x02b" },
        { 0x035, "FF 90 E0 02 00 00",       "layout changed at +0x035" },
        { 0x09F, "E8 ?? ?? ?? ??",          "layout changed at +0x09f" },
        { 0x136, "FF 90 08 0A 00 00",       "layout changed at +0x136" },
        { 0x13F, "48 8B 51 28",             "layout changed at +0x13f" },
        { 0x151, "48 8B 83 08 02 00 00",    "layout changed at +0x151" },
        { 0x17A, "C5 FA 10 B0 ?? ?? 00 00", "layout changed at +0x17a" },   // vmovss xmm6, [rax+camera field]
        { 0x182, "C5 FA 10 B8 ?? ?? 00 00", "layout changed at +0x182" },   // vmovss xmm7, [rax+camera field]
        { 0x1AC, "C5 78 10 90 ?? ?? 00 00", "layout changed at +0x1ac" },   // vmovups xmm10, [rax+camera field]
        { 0x1BD, "C5 7B 10 98 ?? ?? 00 00", "layout changed at +0x1bd" },   // vmovsd xmm11, [rax+camera field]
        { 0x1FF, "FF 90 D8 06 00 00",       "layout changed at +0x1ff" },
        { 0x2B0, "48 8B 0D ?? ?? ?? ??",    "layout changed at +0x2b0" },
        { 0x2BA, "FF 90 20 01 00 00",       "layout changed at +0x2ba" },
        { 0x2C8, "48 8B 91 E0 06 00 00",    "layout changed at +0x2c8" },
        { 0x2E5, "4C 8B 89 98 01 00 00",    "layout changed at +0x2e5" },
        { 0x368, "FF 90 D8 09 00 00",       "layout changed at +0x368" },
        { 0x376, "4C 8B 81 58 01 00 00",    "layout changed at +0x376" },
    };
    for (const auto& c : kChecks)
        if (!BytesMatch(f + c.off, c.bytes)) return SigFail(c.why);
    if (!VerifyEntityPositionSlots(img)) return SigFail("entity position slots not confirmed");
    if (!VerifyZoneSlots(img))           return SigFail("zone parent/id slots not confirmed");
    return SigOk(f);
}

SigResult ResolveClientMgr(const Image&, const SigResolveContext& ctx)    { return SigOk(RipTarget(ctx.Sig("teleport.to_camera") + 0x024, 3, 7)); }
SigResult ResolveHandleFromId(const Image&, const SigResolveContext& ctx) { return SigOk(RipTarget(ctx.Sig("teleport.to_camera") + 0x09F, 1, 5)); }
SigResult ResolveEntitySystem(const Image&, const SigResolveContext& ctx) { return SigOk(RipTarget(ctx.Sig("teleport.to_camera") + 0x2B0, 3, 7)); }

// The game's own teleport call inside CmdTeleportToCamera (4.10.196, bytes read from the image): the actor's
// teleport component (actor vtable slot 0x9D8) is called through its vtable slot 0x158 with this = component and
// rdx = a params block on the stack. The stores pin the block's layout as game.relocation fills it: zone id at
// +0x00, the rotation as doubles x y z w at +0x08..+0x27, the local position at +0x28..+0x3F. A different
// build fails the row, which keeps capability game.relocation off.
SigResult ResolveRelocation(const Image&, const SigResolveContext& ctx) {
    uint8_t* f = ctx.Sig("teleport.to_camera");
    if (!f) return SigFail("source row missing");
    static const struct { size_t off; const char* bytes; const char* why; } kChecks[] = {
        { 0x211, "48 8B 10 48 89 54 24 40", "relocation params zone id store changed at +0x211" },
        { 0x24F, "C5 FB 11 74 24 60",       "relocation params rotation w store changed at +0x24f" },
        { 0x25E, "C5 FB 11 7C 24 48",       "relocation params rotation x store changed at +0x25e" },
        { 0x26D, "C5 7B 11 44 24 50",       "relocation params rotation y store changed at +0x26d" },
        { 0x27C, "C5 7B 11 4C 24 58",       "relocation params rotation z store changed at +0x27c" },
        { 0x29F, "C5 78 11 54 24 68",       "relocation params local position xy store changed at +0x29f" },
        { 0x2A5, "C5 7B 11 5C 24 78",       "relocation params local position z store changed at +0x2a5" },
        { 0x362, "48 8B 03 48 8B CB FF 90 D8 09 00 00 48 8D 54 24 40 48 8B 08 4C 8B 81 58 01 00 00 48 8B C8 41 FF D0",
                 "teleport component call changed at +0x362" },
    };
    for (const auto& c : kChecks)
        if (!BytesMatch(f + c.off, c.bytes)) return SigFail(c.why);
    return SigOk(f + 0x362);
}

}  // namespace

extern const SigDef kTeleportSignatures[] = {
    { "teleport.to_camera",      nullptr, 0, 0, ResolveToCamera,     {} },
    { "teleport.client_mgr",     nullptr, 0, 0, ResolveClientMgr,    { "teleport.to_camera" } },
    { "teleport.handle_from_id", nullptr, 0, 0, ResolveHandleFromId, { "teleport.to_camera" } },
    { "teleport.entity_system",  nullptr, 0, 0, ResolveEntitySystem, { "teleport.to_camera" } },
    { "teleport.relocation",     nullptr, 0, 0, ResolveRelocation,   { "teleport.to_camera" } },
};
extern const size_t kTeleportSignatureCount = sizeof(kTeleportSignatures) / sizeof(kTeleportSignatures[0]);

bool TeleportAddresses(TeleportAddrs& out) {
    uint8_t* mgr = Sig("teleport.client_mgr");
    uint8_t* es  = Sig("teleport.entity_system");
    uint8_t* h   = Sig("teleport.handle_from_id");
    if (!mgr || !es || !h) return false;
    out.clientMgr    = reinterpret_cast<uintptr_t*>(mgr);
    out.entitySystem = reinterpret_cast<uintptr_t*>(es);
    out.handleFromId = h;
    return true;
}

}  // namespace sco::game
