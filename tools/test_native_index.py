import hashlib
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import native_index  # noqa: E402

REPO = native_index.ROOT

SIGS = r'''
#include "sig_rows.h"
namespace sco::game {
namespace {
constexpr rows::RipSpec kFrom{ "demo.not_a_row_spec", 0x26, 3, 7 };
constexpr const char* kHelpers[] = { "demo.alpha", "demo.beta" };
constexpr const char* kSelf[] = { "demo.gamma", "demo.alpha" };
constexpr Capability kCaps[] = {
    { "demo.helpers", kHelpers, std::size(kHelpers) },
    { "demo.gamma",   kSelf,    std::size(kSelf) },
};
}  // namespace
#define DEMO_N(k) { "demo.numbered." #k, nullptr, 0, 0, Resolve<k>, {} }
extern const SigDef kDemoSignatures[] = {
    { "demo.alpha", "48 8B 0D ?? ?? ?? ??", 0, 0, nullptr, {} },   // { "demo.commented_out", ...
    /* { "demo.block_commented", nullptr, 0, 0, nullptr, {} }, */
    { "demo.beta",  nullptr, 0, 0, ResolveFn<kFrom>, { "demo.alpha" } },
    { "demo.gamma", nullptr, 0, 0, ResolveGamma, {} },
    DEMO_N(1), DEMO_N(2),
    { "demo.idle",  nullptr, 0, 0, ResolveIdle, {} },
};
}  // namespace sco::game
'''

REGISTRATION = '''
SigRegistrationResult RegisterGameSignatures(SignatureRegistry& registry) {
    const SignatureTable kGameSignatureTables[] = {
        {kDemoSignatures, kDemoSignatureCount},
    };
    return registry.RegisterTables(kGameSignatureTables, std::size(kGameSignatureTables));
}
'''


def capability(name, rows, default="ON", level="sigcheck_resolved", spike=None):
    return {"name": name, "default": default, "evidence_level": level, "sig_rows": rows, "spike_ref": spike}


def registry(*caps):
    return {"services": [{"id": "demo", "capabilities": list(caps)}]}


class Fixture:
    def __init__(self, sigs=SIGS, registration=REGISTRATION, reg=None):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "src/game").mkdir(parents=True)
        (self.root / "sdk").mkdir()
        (self.root / "src/game/demo_sigs.cpp").write_text(sigs, encoding="utf-8")
        (self.root / "src/game/signatures.cpp").write_text(registration, encoding="utf-8")
        (self.root / "sdk/status_registry.json").write_text(json.dumps(reg or registry()), encoding="utf-8")

    def build(self):
        return native_index.build(self.root)

    def close(self):
        self.tmp.cleanup()


class ParserTests(unittest.TestCase):
    def build(self, **kw):
        fx = Fixture(**kw)
        self.addCleanup(fx.close)
        return fx.build()

    def test_rows_are_table_elements_with_macros_expanded(self):
        ids = [e["id"] for e in self.build()["entries"]]
        self.assertEqual(ids, ["demo.alpha", "demo.beta", "demo.gamma", "demo.numbered.1", "demo.numbered.2", "demo.idle"])

    def test_tiers(self):
        reg = registry(
            capability("demo.promoted", ["demo.helpers"]),
            capability("demo.held", ["demo.alpha", "demo.gamma"], default="OFF", level="off_pending_probe", spike="B9"),
            capability("demo.blocked", ["demo.numbered.1"], default="none", level="blocked"),
        )
        entries = {e["id"]: e for e in self.build(reg=reg)["entries"]}
        self.assertEqual(entries["demo.alpha"]["tier"], 1)
        self.assertEqual(entries["demo.alpha"]["capability"], ["demo.promoted"])
        self.assertEqual(entries["demo.beta"]["tier"], 1)
        self.assertEqual(entries["demo.gamma"]["tier"], 3)
        self.assertEqual(entries["demo.gamma"]["capability"], ["demo.held"])
        self.assertEqual(entries["demo.numbered.1"]["tier"], 4)
        self.assertNotIn("capability", entries["demo.numbered.1"])
        self.assertEqual(entries["demo.idle"]["tier"], 2)
        self.assertNotIn("capability", entries["demo.idle"])

    def test_row_name_wins_over_same_named_group(self):
        entries = {e["id"]: e for e in self.build(reg=registry(capability("demo.cap", ["demo.gamma"])))["entries"]}
        self.assertEqual(entries["demo.gamma"]["tier"], 1)
        self.assertEqual(entries["demo.alpha"]["tier"], 2)

    def test_unknown_sig_row_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "neither a row nor a group"):
            self.build(reg=registry(capability("demo.cap", ["demo.missing"])))

    def test_unregistered_table_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "not registered"):
            self.build(registration=REGISTRATION.replace("{kDemoSignatures, kDemoSignatureCount},", ""))

    def test_duplicate_registered_table_is_refused(self):
        entry = "{kDemoSignatures, kDemoSignatureCount},"
        with self.assertRaisesRegex(native_index.NativeIndexError, "registered twice"):
            self.build(registration=REGISTRATION.replace(entry, entry + entry))

    def test_unknown_registered_table_is_refused(self):
        entry = "{kDemoSignatures, kDemoSignatureCount},"
        with self.assertRaisesRegex(native_index.NativeIndexError, "which no .* defines"):
            self.build(registration=REGISTRATION.replace(entry, entry + "{kOtherSignatures, kOtherSignatureCount},"))

    def test_wrong_count_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "requires count"):
            self.build(registration=REGISTRATION.replace("kDemoSignatureCount", "kOtherSignatureCount"))

    def test_different_or_conditional_submission_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "submit .* atomically"):
            self.build(registration=REGISTRATION.replace("return registry.RegisterTables", "if (ready) return registry.RegisterTables"))
        with self.assertRaisesRegex(native_index.NativeIndexError, "submit .* atomically"):
            self.build(registration=REGISTRATION.replace("std::size(kGameSignatureTables)", "1"))

    def test_missing_authoritative_array_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "authoritative"):
            self.build(registration=REGISTRATION.replace("kGameSignatureTables[]", "kOtherTables[]"))

    def test_commented_out_table_does_not_count(self):
        entry = "{kDemoSignatures, kDemoSignatureCount},"
        with self.assertRaisesRegex(native_index.NativeIndexError, "not registered"):
            self.build(registration=REGISTRATION.replace(entry, "/* " + entry + " */"))

    def test_unknown_element_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "not a row initializer"):
            self.build(sigs=SIGS.replace("DEMO_N(1),", "OTHER_MACRO(1),"))

    def test_duplicate_row_is_refused(self):
        with self.assertRaisesRegex(native_index.NativeIndexError, "defined in"):
            self.build(sigs=SIGS.replace('"demo.idle"', '"demo.beta"'))

    def test_collision_is_refused(self):
        original = native_index.symbol_hash
        native_index.symbol_hash = lambda row_id: "_0x0000000000000000"
        self.addCleanup(setattr, native_index, "symbol_hash", original)
        with self.assertRaisesRegex(native_index.NativeIndexError, "hash collision"):
            self.build()


class RepositoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.index = native_index.build(REPO)
        cls.committed = (REPO / native_index.INDEX).read_text(encoding="utf-8")

    def test_committed_index_is_current(self):
        self.assertEqual(native_index.render(self.index), self.committed)

    def test_hashes_are_unique_and_well_formed(self):
        hashes = [e["hash"] for e in self.index["entries"]]
        self.assertEqual(len(hashes), len(set(hashes)), "hash collision in sdk/native_index.json")
        for e in self.index["entries"]:
            self.assertRegex(e["hash"], r"^_0x[0-9A-F]{16}$")
            digest = hashlib.sha256(("sco-core:native:" + e["id"]).encode()).hexdigest()[:16].upper()
            self.assertEqual(e["hash"], "_0x" + digest)

    def test_entries_carry_only_the_allowed_fields(self):
        for e in self.index["entries"]:
            self.assertLessEqual(set(e), set(native_index.ENTRY_KEYS), e["id"])
            self.assertEqual(e["source"], "src/game/%s_sigs.cpp" % e["subsystem"])
            self.assertEqual("capability" in e, e["tier"] in (1, 3), e["id"])

    def test_no_byte_pattern_or_address_leaks(self):
        hex_runs = re.findall(r"0x[0-9a-fA-F]{6,}", self.committed)
        self.assertEqual(len(hex_runs), self.index["row_count"])
        self.assertNotRegex(self.committed, r"\b[0-9A-F]{2}( [0-9A-F?]{2}){3,}\b")

    def test_every_row_and_subsystem_is_indexed(self):
        self.assertEqual(self.index["row_count"], len(self.index["entries"]))
        stems = {p.name[:-len("_sigs.cpp")] for p in (REPO / "src/game").glob("*_sigs.cpp")}
        self.assertEqual({e["subsystem"] for e in self.index["entries"]}, stems)


if __name__ == "__main__":
    unittest.main()
