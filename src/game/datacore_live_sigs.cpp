// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
// Locators for the native DataCore owner (constructor table, reset, deleting wrapper, destructor,
// owner slot) and the layout facts they check: F-4.10.197.2878-051 (DataCore owner/loader layout),
// with the value-pool header from F-4.10.197.2878-031. Static resolution on 4.10.197.2878 only;
// not run in game.
#include "sig_rows.h"
#include <cstring>

namespace sco::game {
namespace {
bool ImageRange(const Image& img, const uint8_t* p, size_t n) {
    return rows::InImage(img, p, n);
}
bool SectionRange(const Image& img, const Section& s, const uint8_t* p, size_t n) {
    return ImageRange(img, s.base, s.size) && rows::InRange(s, p, n);
}
const uint8_t* At(const Image& img, const Section& s, const uint8_t* p, size_t off, size_t n) {
    if (off > static_cast<size_t>(PTRDIFF_MAX) || !SectionRange(img, s, p, 0)) return nullptr;
    return rows::RangeAt(s, p, static_cast<ptrdiff_t>(off), n);
}
const uint8_t* Target(const Image& img, const uint8_t* insn, size_t disp, size_t len) {
    return SectionRange(img, img.text, insn, len) ? rows::CheckedRip(img, insn, disp, len) : nullptr;
}
SigResult OwnerVtable(const Image& img) {
    if(!SectionRange(img,img.text,img.text.base,img.text.size)) return SigFail("DataCore text outside image");
    auto found=SigPattern(img.text,"48 8D 05 ?? ?? ?? ?? 48 89 07 B9 58 00 00 00 48 89 AF 30 01 00 00");
    if(found.state!=SigState::Ok) return found;
    auto* table=Target(img,found.at,3,7);
    if(!SectionRange(img,img.rdata,table,0xf0)) return SigFail("DataCore owner table outside .rdata");
    auto* ctor=FunctionStart(img,found.at);
    if(!SectionRange(img,img.text,ctor,22) || !BytesMatch(ctor,"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56")) return SigFail("DataCore constructor identity changed");
    return SigOk(table);
}
const uint8_t* Slot(const Image& img,size_t offset) {
    const auto* table=Sig("datacore_live.owner_vtable");
    const auto* entry=At(img,img.rdata,table,offset,8);
    if(!entry) return nullptr;
    uint64_t value=0; std::memcpy(&value,entry,8);
    auto* direct=reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(value));
    if(SectionRange(img,img.text,direct,1)) return direct;
    if(!ImageRange(img,img.base,0x400) || img.base[0]!='M' || img.base[1]!='Z') return nullptr;
    uint32_t nt=0;std::memcpy(&nt,img.base+0x3c,4);
    if(nt>img.size-0x38 || std::memcmp(img.base+nt,"PE\0\0",4)!=0) return nullptr;
    uint64_t preferred=0;std::memcpy(&preferred,img.base+nt+0x30,8);
    if(value<preferred || value-preferred>=img.size) return nullptr;
    auto* target=img.base+static_cast<size_t>(value-preferred);
    return SectionRange(img,img.text,target,1)?target:nullptr;
}
SigResult Reset(const Image& img) {
    const auto* target=Slot(img,8);
    if(!target || !SectionRange(img,img.text,target,0x47) ||
       !BytesMatch(target,"4C 8B DC 53 56 57 48 81 EC 80 00 00 00") ||
       !BytesMatch(target+0x37,"C7 81 C8 01 00 00 03 00 00 00")) return SigFail("DataCore reset identity changed");
    return SigOk(target);
}
SigResult Deleting(const Image& img) {
    const auto* target=Slot(img,0x20);
    if(!target || !SectionRange(img,img.text,target,0x26) ||
       !BytesMatch(target,"48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 E8 ?? ?? ?? ?? F6 C3 01 74 0D BA D8 03 00 00")) return SigFail("DataCore deleting wrapper changed");
    return SigOk(target);
}
SigResult Destructor(const Image& img) {
    const auto* wrapper=Sig("datacore_live.deleting");
    auto* call=At(img,img.text,wrapper,0xf,5);
    auto* target=Target(img,call,1,5);
    if(!SectionRange(img,img.text,target,0x2e) || !BytesMatch(target,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D 05 ?? ?? ?? ?? 48 8B D9 48 89 01 33 F6 48 8B 89 D0 01 00 00") ||
       Target(img,At(img,img.text,target,0xf,7),3,7)!=Sig("datacore_live.owner_vtable") || !Sig("datacore_live.owner_vtable")) return SigFail("DataCore destructor identity changed");
    return SigOk(target);
}
SigResult OwnerSlot(const Image& img) {
    if(!SectionRange(img,img.text,img.text.base,img.text.size)) return SigFail("DataCore text outside image");
    auto found=SigPattern(img.text,"48 8B 0D ?? ?? ?? ?? 4C 8D 45 C0 48 8D 55 D8 48 8B 01 FF 90 E8 00 00 00 48 8B 45 E0 33 F6");
    if(found.state!=SigState::Ok)return found;
    auto* slot=Target(img,found.at,3,7);
    if(!ImageRange(img,slot,8))return SigFail("DataCore owner slot outside image");
    return SigOk(slot);
}
}
extern const SigDef kDataCoreLiveSignatures[]={
    {"datacore_live.owner_vtable",nullptr,0,0,OwnerVtable,{}},
    {"datacore_live.owner_slot",nullptr,0,0,OwnerSlot,{}},
    {"datacore_live.reset",nullptr,0,0,Reset,{"datacore_live.owner_vtable"}},
    {"datacore_live.deleting",nullptr,0,0,Deleting,{"datacore_live.owner_vtable"}},
    {"datacore_live.destructor",nullptr,0,0,Destructor,{"datacore_live.deleting","datacore_live.owner_vtable"}}
};
extern const size_t kDataCoreLiveSignatureCount=sizeof(kDataCoreLiveSignatures)/sizeof(kDataCoreLiveSignatures[0]);
}
