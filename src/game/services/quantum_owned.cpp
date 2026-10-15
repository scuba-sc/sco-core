// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 the sco-core contributors
#include "quantum_owned.h"
#include <cmath>
namespace sco::game::services {
namespace {
bool Finite01(float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }
bool SameFloat(float a, float b) { return std::isfinite(a) && std::isfinite(b) && a == b; }
}

bool OwnedQuantum::DriveAlive() {
    uintptr_t id = 0;
    return drive_ && ops_.driveIdentity(ops_.ctx, drive_, &id) && id == identity_;
}

void OwnedQuantum::DropDrive() {
    // The drive's own cells (charge, input state) died with it; the shared parameter record did not.
    drive_ = 0; identity_ = 0; pressed_ = false; charge_ = {};
    chargeAnchored_ = false; chargeSince_ = 0;
}

bool OwnedQuantum::Observe(QuantumObservation* out) {
    if (!out || fault_) return false;
    *out = {};
    out->offline = out->antiCheatAbsent = Admit();
    if (!out->offline || !drive_) return true;
    QuantumDriveSample s;
    if (!DriveAlive() || !ops_.driveSample(ops_.ctx, drive_, &s)) { DropDrive(); return true; }
    out->drive = true;
    if (s.state < 0 || s.state > 9 || s.reason < 0 || s.reason > 14 || !Finite01(s.charge)) {
        out->block = SC_QUANTUM_BLOCK_ENGINE;
        return true;
    }
    out->phase = s.state >= 1 && s.state <= 3 ? SC_QUANTUM_CHARGING
               : s.state == 4 || s.state == 5 ? SC_QUANTUM_BOOSTING : SC_QUANTUM_IDLE;
    out->progress = static_cast<uint32_t>(s.charge * static_cast<float>(SC_QUANTUM_PROGRESS_MAX));
    if (out->progress > SC_QUANTUM_PROGRESS_MAX) out->progress = SC_QUANTUM_PROGRESS_MAX;
    return true;
}

OwnedQuantum::Step OwnedQuantum::WriteCVar(uint32_t i) {
    CVarRecord& c = cvars_[i];
    int32_t current = 0;
    if (!ops_.cvarRead(ops_.ctx, i, &current)) return Step::Failed;
    if (!c.saved) { c.saved = true; c.prior = current; }   // first prior, before any write
    if (current == kOwnedCVarValue) return Step::Ok;
    if (!Admit()) return Step::Lost;
    c.written = true; c.wrote = kOwnedCVarValue;           // recorded before the write: a partial write is still ours
    if (!ops_.cvarWrite(ops_.ctx, i, kOwnedCVarValue)) return Step::Failed;
    int32_t back = 0;
    return ops_.cvarRead(ops_.ctx, i, &back) && back == kOwnedCVarValue ? Step::Ok : Step::Failed;
}

OwnedQuantum::Step OwnedQuantum::EnsureParam() {
    if (param_.written && param_.drive == drive_) return Step::Ok;
    uintptr_t cell = 0;
    float current = 0;
    // No grounded cell is "nothing to write": no mutation happened, so nothing is uncertain.
    if (!DriveAlive() || !ops_.paramCell(ops_.ctx, drive_, &cell) || !ops_.cellRead(ops_.ctx, cell, &current) ||
        !std::isfinite(current)) return Step::Ok;
    // The data pack (quantum_drive.toml) already sets 1e9 on the pack's drive: write nothing then.
    if (current >= kDisableChargeOff) return Step::Ok;
    if (!Admit()) return Step::Lost;
    param_ = {true, drive_, cell, current, kDisableChargeOff};   // prior saved before the write
    if (!ops_.cellWrite(ops_.ctx, cell, kDisableChargeOff)) return Step::Failed;
    float back = 0;
    return ops_.cellRead(ops_.ctx, cell, &back) && SameFloat(back, kDisableChargeOff) ? Step::Ok : Step::Failed;
}

OwnedQuantum::Step OwnedQuantum::SyncInput(bool held) {
    if (held && !pressed_) {
        if (!DriveAlive()) { if (drive_) DropDrive(); return Step::Ok; }
        if (!Admit()) return Step::Lost;
        pressed_ = true;   // before the call: an uncertain press must still be released
        return ops_.sendInput(ops_.ctx, drive_, kInputAction, kInputPress, 1.0f) ? Step::Ok : Step::Failed;
    }
    if (!held && pressed_) return ReleaseInput() ? Step::Ok : Step::Failed;
    return Step::Ok;
}

bool OwnedQuantum::ReleaseInput() {
    if (!pressed_) return true;
    if (DriveAlive() && !ops_.sendInput(ops_.ctx, drive_, kInputAction, kInputRelease, 0.0f)) return false;
    pressed_ = false;   // a dead drive took its input state with it
    return true;
}

bool OwnedQuantum::RestoreCVar(uint32_t i) {
    CVarRecord& c = cvars_[i];
    if (!c.saved) return true;
    if (c.written) {
        int32_t current = 0;
        if (ops_.cvarType(ops_.ctx, i) != QuantumCVarType::Int32 || !ops_.cvarRead(ops_.ctx, i, &current)) return false;
        if (current == c.wrote) {   // someone else's later value is not ours to overwrite
            int32_t back = 0;
            if (!ops_.cvarWrite(ops_.ctx, i, c.prior) || !ops_.cvarRead(ops_.ctx, i, &back) || back != c.prior) return false;
        }
    }
    c = {};
    return true;
}

bool OwnedQuantum::RestoreCell(CellRecord& rec, bool requireDrive) {
    if (!rec.written) { rec = {}; return true; }
    if (requireDrive && (!DriveAlive() || rec.drive != drive_)) { rec = {}; return true; }   // cell died with its drive
    float current = 0;
    if (!ops_.cellRead(ops_.ctx, rec.cell, &current)) return false;
    if (SameFloat(current, rec.wrote)) {
        float back = 0;
        if (!ops_.cellWrite(ops_.ctx, rec.cell, rec.prior) || !ops_.cellRead(ops_.ctx, rec.cell, &back) ||
            !SameFloat(back, rec.prior)) return false;
    }
    rec = {};
    return true;
}

bool OwnedQuantum::Release() {
    bool ok = true;
    if (!ReleaseInput()) ok = false;
    if (!RestoreCell(charge_, true)) ok = false;
    if (!RestoreCell(param_, false)) ok = false;
    for (uint32_t i = 0; i < kOwnedCVars; ++i)
        if (!RestoreCVar(i)) ok = false;
    active_ = false;
    chargeAnchored_ = false; chargeSince_ = 0;
    return ok;
}

bool OwnedQuantum::Apply(bool enabled, bool held) {
    if (fault_) return false;
    if (!enabled || !held) return Release();
    if (!Admit()) return Release();
    for (uint32_t i = 0; i < kOwnedCVars; ++i)
        if (ops_.cvarType(ops_.ctx, i) != QuantumCVarType::Int32) return false;   // refused before any read or write
    active_ = true;
    Step r = Step::Ok;
    for (uint32_t i = 0; i < kOwnedCVars && r == Step::Ok; ++i) r = WriteCVar(i);
    if (r == Step::Ok && drive_) r = EnsureParam();
    if (r == Step::Ok) r = SyncInput(true);
    if (r == Step::Ok) return true;
    const bool restored = Release();   // admission lost: restored and refused; otherwise an uncertain write
    return r == Step::Lost && restored;
}

void OwnedQuantum::OnStartUse(uintptr_t drive, uintptr_t user) {
    if (fault_ || !ops_.localUser(ops_.ctx, user)) return;   // not the local player: untouched
    if (!Admit()) return;
    uintptr_t id = 0;
    if (!ops_.driveIdentity(ops_.ctx, drive, &id)) return;
    if (drive_ != drive) {
        if (!ReleaseInput() || !RestoreCell(charge_, true) || !RestoreCell(param_, false)) { fault_ = true; return; }
        DropDrive();
        drive_ = drive;
    }
    identity_ = id;
    if (active_ && EnsureParam() == Step::Failed) fault_ = true;
}

bool OwnedQuantum::KeepFull(uintptr_t drive) {
    if (fault_ || !active_ || !drive_ || drive != drive_ || !DriveAlive()) return false;
    QuantumDriveSample s;
    if (!ops_.driveSample(ops_.ctx, drive_, &s)) return false;
    const uint32_t now = ops_.nowMs(ops_.ctx);
    if (s.state != 3) chargeAnchored_ = false;
    else if (!chargeAnchored_) { chargeAnchored_ = true; chargeSince_ = now; }
    const bool full = s.state == 4 || s.state == 5 ||
                      (s.state == 3 && chargeAnchored_ && now - chargeSince_ > kChargeSettleMs);
    if (!full || !Admit()) return false;
    uintptr_t cell = 0;
    float current = 0;
    if (!ops_.chargeCell(ops_.ctx, drive_, &cell) || !ops_.cellRead(ops_.ctx, cell, &current) || !Finite01(current)) return false;
    if (current == 1.0f) return true;
    if (!charge_.written || charge_.drive != drive_) charge_ = {true, drive_, cell, current, 1.0f};   // first prior, before the write
    float back = 0;
    if (!ops_.cellWrite(ops_.ctx, cell, 1.0f) || !ops_.cellRead(ops_.ctx, cell, &back) || back != 1.0f) {
        fault_ = true;   // surfaces on the next provider callback and latches restart
        return false;
    }
    return true;
}

bool OwnedQuantum::ControlActive() { return active_ && !fault_; }

namespace {
bool ObserveThunk(void* ctx, QuantumObservation* out) { return static_cast<OwnedQuantum*>(ctx)->Observe(out); }
bool ApplyThunk(void* ctx, bool enabled, bool held) { return static_cast<OwnedQuantum*>(ctx)->Apply(enabled, held); }
}

QuantumBackend MakeOwnedQuantumBackend(OwnedQuantum& owner, const QuantumHook* hooks, uint32_t hookCount) {
    QuantumBackend b;
    b.ctx = &owner; b.observe = ObserveThunk; b.apply = ApplyThunk; b.hooks = hooks; b.hookCount = hookCount;
    return b;
}
}
