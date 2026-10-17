// Signature rows for game.inventory (F-4.10.197.2878-011..-016): the functions the item-port
// adapter reads through, so a game build that moves or reshapes one fails here and not in a plugin.
//
// inventory.get_item_ports, inventory.port_is_valid, inventory.port_item_handle,
// inventory.handle_valid, inventory.unequip_item: each a unique prologue in .text. The adapter
// never calls get_item_ports (it reads the container's port vector itself); the row is the
// receiver check: the container's vtable entry must be this function.
// inventory.port_get_item, inventory.port_get_container: three sibling accessors share one
// prologue (the pattern matches all three); they differ in the component label each loads, so the
// resolver keeps the one that loads the wanted label and fails unless that is exactly one.
// inventory.container_type_id: the game caches the item-port container's component type id in a u16
// (0xFFFF = not registered yet). The row is that cache, read from inside inventory.unequip_item
// and accepted only when the same code loads the "IItemPortContainer" label to register it.
// inventory.item_type_names: the game's item type id -> name function, a bounded jump table (the
// shape of actor.stat_names). The row follows the table for every entry in inventory_map.h and
// checks the case still loads the expected name, so a reordered item type enum fails here and the
// capability stays off.
#include "inventory_map.h"
#include "sig_rows.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include <cstring>
#include <iterator>

namespace sco::game {
namespace {

using rows::InText;

constexpr const char* kGetItemPorts =
    "48 89 6C 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B 91 88 00 00 00 45 33 ED 48 8B 81 90 00 00 00 45 0F B6 F9 49 2B C2 4D 8B";
constexpr const char* kPortIsValid =
    "48 89 5C 24 18 57 48 83 EC 30 48 8D 05 ?? ?? ?? ?? 48 8B F9 48 89 44 24 40 48 8D 99 40 04 00 00 E8 ?? ?? ?? ?? 8B 50 18 39 53 08 75";
constexpr const char* kPortItemHandle =
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8D 05 ?? ?? ?? ?? 48 8B FA 48 89 44 24 40 48 8D 99 40 04 00 00 48 8B F1 E8 ?? ?? ??";
constexpr const char* kHandleValid =
    "48 89 5C 24 08 57 48 83 EC 20 4C 8B 09 48 8B F9 4D 85 C9 0F 84 ?? ?? ?? ?? 4D 8B D1 48 B8 FF FF FF FF FF FF 00 00 4C 23 D0 49 C1 E9";
constexpr const char* kUnequipItem =
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC D8 00 00 00 4C 8B 72 58 4C 8D 81 48 01 00 00 4C 8B E2 4C 8B CA 48 8D";

// The three sibling port accessors (each takes the port's lock, then asks the attached entity for
// one component by label). 96 bytes, rel32 operands wild.
constexpr const char* kPortAccessor =
    "40 53 57 48 83 EC 38 48 8D 05 ?? ?? ?? ?? 48 8B F9 48 89 44 24 50 48 8D 99 40 04 00 00 E8 ?? ?? ?? ?? 8B 50 18 39 53 08 75 05 "
    "FE 43 0C EB 2D BA 01 00 00 00 F0 48 0F C1 13 48 FF C2 48 0F BA E2 15 73 19 4C 8D 0D ?? ?? ?? ?? C6 44 24 20 01 4C 8D 44 24 50 "
    "48 8B CB E8 ?? ?? ?? ?? 48 8B 87 50";
constexpr size_t kAccessorScan = 0x100;   // how far into an accessor its label load is looked for

bool InImage(const Image& img, const uint8_t* p, size_t n) {
    return img.base && img.size && p >= img.base && p <= img.base + img.size &&
           static_cast<size_t>(img.base + img.size - p) >= n;
}

bool NameIs(const Image& img, const uint8_t* s, const char* want) {
    const size_t n = strlen(want) + 1;
    return InImage(img, s, n) && memcmp(s, want, n) == 0;
}

// True when [fn, fn + kAccessorScan) holds a RIP-relative lea of the C string label.
bool LoadsLabel(const Image& img, const uint8_t* fn, const char* label) {
    if (!InText(img, fn, kAccessorScan)) return false;
    for (const uint8_t* p = fn; p + 7 <= fn + kAccessorScan; ++p) {
        if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != 0x8D || (p[2] & 0xC7) != 0x05) continue;
        if (NameIs(img, RipTarget(p, 3, 7), label)) return true;
    }
    return false;
}

SigResult ResolvePortAccessor(const Image& img, const char* label) {
    uint8_t* hits[8];
    const int n = FindPattern(img.text, kPortAccessor, hits, 8);
    if (n <= 0) return { SigState::Missing, nullptr, 0, nullptr };
    if (n > 8) return { SigState::Ambiguous, nullptr, n, nullptr };
    uint8_t* found = nullptr;
    for (int i = 0; i < n; ++i) {
        if (!LoadsLabel(img, hits[i], label)) continue;
        if (found) return SigFail("two port accessors load the same component label");
        found = hits[i];
    }
    return found ? SigOk(found) : SigFail("no port accessor loads the component label");
}

SigResult ResolvePortGetItem(const Image& img) { return ResolvePortAccessor(img, "IAttachableComponent"); }
SigResult ResolvePortGetContainer(const Image& img) { return ResolvePortAccessor(img, "IItemPortContainer"); }

// Inside the UnequipItem implementation: the registration of the container's component type id.
//   mov ecx,0FFFFh; mov rax,[rbx]; mov rdi,[rax+390h]
//   movzx eax, word ptr [cache]; cmp ax,cx; jne ..; mov rcx,[resolver]; lea r8,[label]
constexpr size_t kUnequipTypeCode = 0x2E0;
constexpr const char* kTypeCode =
    "B9 FF FF 00 00 48 8B 03 48 8B B8 90 03 00 00 0F B7 05 ?? ?? ?? ?? 66 3B C1 75 ?? 48 8B 0D ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ??";
constexpr size_t kTypeCodeBytes = 0x29;
constexpr size_t kTypeCacheInsn = 0x0F;   // from the check: movzx eax, word ptr [rip+disp32]
constexpr size_t kTypeLabelInsn = 0x22;   // from the check: lea r8, [rip+disp32]
static_assert(inventory_map::kEntityComponentByType == 0x390, "kTypeCode pins this");

SigResult ResolveContainerTypeId(const Image& img) {
    const uint8_t* impl = Sig("inventory.unequip_item");
    if (!impl) return SigFail("inventory.unequip_item missing");
    const uint8_t* code = impl + kUnequipTypeCode;
    if (!InText(img, code, kTypeCodeBytes) || !BytesMatch(code, kTypeCode)) return SigFail("the container type id registration changed shape");
    if (!NameIs(img, RipTarget(code + kTypeLabelInsn, 3, 7), "IItemPortContainer"))
        return SigFail("the type id registration doesn't load the IItemPortContainer label");
    const uint8_t* cache = RipTarget(code + kTypeCacheInsn, 3, 7);
    if (!InImage(img, cache, 2)) return SigFail("the type id cache is outside the image");
    return SigOk(cache);
}

// The item type id -> name function: cmp ecx,0C0h; ja <undefined>; movsxd rax,ecx; lea r8,[image base];
// mov ecx,[r8+rax*4+table]; add rcx,r8; jmp rcx; every case lea rax,[name]; ret.
constexpr const char* kNameSwitch =
    "81 F9 C0 00 00 00 0F 87 ?? ?? ?? ?? 48 63 C1 4C 8D 05 ?? ?? ?? ?? 41 8B 8C 80 ?? ?? ?? ?? 49 03 C8 FF E1";
constexpr size_t kBaseLea = 0x0F;
constexpr size_t kTableDisp = 0x1A;
constexpr const char* kNameCase = "48 8D 05 ?? ?? ?? ?? C3";
constexpr uint32_t kNameBound = 0xC1;   // ids 0..0C0h

SigResult ResolveItemTypeNames(const Image& img) {
    SigResult r = SigPattern(img.text, kNameSwitch);
    if (r.state != SigState::Ok) return r;
    if (!img.base || !img.size) return SigFail("no image base to follow the name table from");
    if (RipTarget(r.at + kBaseLea, 3, 7) != img.base) return SigFail("the name function's base lea isn't the image base");
    uint32_t tableRva = 0;
    memcpy(&tableRva, r.at + kTableDisp, sizeof(tableRva));
    const uint8_t* table = img.base + tableRva;
    if (!InImage(img, table, 4u * kNameBound)) return SigFail("the name table is outside the image");
    for (const inventory_map::TypeName& e : inventory_map::kTypeNames) {
        if (e.id >= kNameBound) return SigFail("a checked item type id is outside the name function's bound");
        uint32_t caseRva = 0;
        memcpy(&caseRva, table + 4u * e.id, sizeof(caseRva));
        const uint8_t* c = img.base + caseRva;
        if (!InText(img, c, 8) || !BytesMatch(c, kNameCase)) return SigFail("an item type name case changed shape");
        if (!NameIs(img, RipTarget(c, 3, 7), e.name)) return SigFail("an item type id no longer has its expected name");
    }
    return r;
}

}  // namespace

extern const SigDef kInventorySignatures[] = {
    { "inventory.get_item_ports",     kGetItemPorts,   0, 0, nullptr, {} },
    { "inventory.port_is_valid",      kPortIsValid,    0, 0, nullptr, {} },
    { "inventory.port_item_handle",   kPortItemHandle, 0, 0, nullptr, {} },
    { "inventory.handle_valid",       kHandleValid,    0, 0, nullptr, {} },
    { "inventory.unequip_item",       kUnequipItem,    0, 0, nullptr, {} },
    { "inventory.port_get_item",      nullptr, 0, 0, ResolvePortGetItem, {} },
    { "inventory.port_get_container", nullptr, 0, 0, ResolvePortGetContainer, {} },
    { "inventory.container_type_id",  nullptr, 0, 0, ResolveContainerTypeId, { "inventory.unequip_item" } },
    { "inventory.item_type_names",    nullptr, 0, 0, ResolveItemTypeNames, {} },
};
extern const size_t kInventorySignatureCount = std::size(kInventorySignatures);

}  // namespace sco::game
