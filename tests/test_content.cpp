// GPL-3.0-only. One synthetic G14 closure fixture, no game/network session.
#include "sco/app.h"
#include "sco/content.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/runtime.h"
#include "scosdk/content.hpp"
#include "../src/api/internal.h"
#include "abi_content.c" // C++ layout pins; also compiled separately as C11
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <thread>

namespace fs = std::filesystem;
namespace P = sco::plugins;
static int pass = 0, fail = 0;
static bool readerRead = false;
static void Sink(const char* line) {
    if (std::strstr(line, "content_reader") && std::strstr(line, "content snapshot read")) readerRead = true;
}
#define CHECK(x) do { if (x) ++pass; else { ++fail; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static void Write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
static void Pack(const fs::path& root, const char* id) {
    Write(root / id / "plugin.ini", std::string("id = ") + id + "\nname = Synthetic fixture\nversion = 1.0.0\napi = 1.1\nkind = data\n");
}
static const void* fakeTable;
static sco_result fakeResult = SCO_OK;
static sco_result Query(const char*, uint32_t, const void** out) { *out = fakeTable; return fakeResult; }
static void Guards(sco_plugin* self) {
    sco::sdk::Content c;
    sco_api api{}; api.size = sizeof(api); api.query_service = Query;
    fakeTable = sco::content::Table();
    CHECK(c.Open(&api, self) == SCO_OK);
    // Physically partial root query pointer: guard must precede the load.
    const uint32_t size = offsetof(sco_api, query_service) + sizeof(api.query_service) - 1;
    auto root = std::make_unique<unsigned char[]>(size);
    std::memset(root.get(), 0, size); std::memcpy(root.get(), &size, sizeof(size));
    CHECK(c.Open(reinterpret_cast<const sco_api*>(root.get()), self) == SCO_UNAVAILABLE);
    sco_content_item out{}; out.size = sizeof(out);
    CHECK(c.Item(SCO_CONTENT_LIST, 0, out) == SCO_UNAVAILABLE); // failed reopen clears state
    api.query_service = nullptr;
    CHECK(c.Open(&api, self) == SCO_UNAVAILABLE);
    api.query_service = Query;
    for (const uint32_t extent : {15u, 23u}) {
        auto tail = std::make_unique<unsigned char[]>(extent);
        std::memset(tail.get(), 0, extent); std::memcpy(tail.get(), &extent, sizeof(extent));
        fakeTable = tail.get(); CHECK(c.Open(&api, self) == SCO_OK);
        uint32_t n = 0;
        CHECK(c.Read(1, nullptr, n) == SCO_UNAVAILABLE);
        if (extent == 15) CHECK(c.Item(SCO_CONTENT_LIST, 0, out) == SCO_UNAVAILABLE);
    }
    sco_content_v1 nulls{}; nulls.size = sizeof(nulls); fakeTable = &nulls;
    CHECK(c.Open(&api, self) == SCO_OK);
    uint32_t n = 0;
    CHECK(c.Item(SCO_CONTENT_LIST, 0, out) == SCO_UNAVAILABLE);
    CHECK(c.Read(1, nullptr, n) == SCO_UNAVAILABLE);
    fakeResult = SCO_NOT_FOUND; fakeTable = sco::content::Table();
    CHECK(c.Open(&api, self) == SCO_NOT_FOUND);
    CHECK(c.Read(1, nullptr, n) == SCO_UNAVAILABLE);
    fakeResult = SCO_OK; fakeTable = nullptr;
    CHECK(c.Open(&api, self) == SCO_UNAVAILABLE);
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    sco::SetLogSink(Sink);
    const fs::path base = fs::path(argv[1]) / "content-fixture";
    fs::remove_all(base); fs::create_directories(base);
    const auto root = base / "plugins";
    Pack(root, "a_pack"); Pack(root, "b_pack"); Pack(root, "disabled_pack"); Pack(root, "broken_pack");
    Pack(root, "a_dependent");
    Write(root / "a_dependent/plugin.ini", "id = a_dependent\nname = Synthetic fixture\nversion = 1.0.0\napi = 1.1\nkind = data\nrequires = broken_pack\n");
    Write(root / "a_dependent/lists/a.txt", "dependent must not leak");
    Pack(root, "code_pack");
    Write(root / "code_pack/plugin.ini", "id = code_pack\nname = Synthetic fixture\nversion = 1.0.0\napi = 1.1\nkind = data\nrequires = content_reader\n");
    Write(root / "code_pack/lists/a.txt", "code-dependent must not leak");
    Write(root / "a_pack/lists/shared.txt", std::string("a\0b", 3));
    Write(root / "a_pack/lists/empty.txt", "");
    Write(root / "a_pack/scripts/nested/example.xml", "<synthetic/>");
    Write(root / "b_pack/lists/shared.txt", "second");
    Write(root / "disabled_pack/lists/shared.txt", "disabled");
    Write(root / "disabled_pack/disabled", "");
    Write(root / "broken_pack/lists/a.txt", "must not leak");
    Write(root / "broken_pack/lists/z.txt", std::string(SCO_CONTENT_MAX_FILE_BYTES + 1u, 'x'));
    Write(root / "content_reader/plugin.ini", "id = content_reader\nname = Synthetic fixture\nversion = 1.0.0\napi = 1.1\nkind = native\nentry = reader.module\nrequires = sco.content\n");
    fs::copy_file(argv[2], root / "content_reader/reader.module");
    // Discovery skips links; subsequent link swaps are checked below.
    std::error_code ec;
    fs::create_symlink(root / "b_pack/lists/shared.txt", root / "a_pack/lists/link.txt", ec);
#ifndef _WIN32
    CHECK(!ec);
#endif
    sco::app::Platform pf{}; pf.pluginRoot = root; pf.pluginsEnabled = true;
    CHECK(sco::app::Start(pf));
    const auto api = sco::host::BuildApi({"synthetic"});
    const void* ptr = nullptr;
    CHECK(api->query_service(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0, &ptr) == SCO_OK);
    const auto table = static_cast<const sco_content_v1*>(ptr);
    CHECK(table && table->size == 24);
    auto self = sco::host::NewPlugin("test_reader"); CHECK(self != nullptr);
    bool readerLoaded = false, brokenRefused = false;
    for (const auto& p : sco::app::Plugins()) {
        if (p.folder == "content_reader") readerLoaded = p.state == P::State::Loaded;
        if (p.folder == "broken_pack") brokenRefused = p.state == P::State::Refused;
    }
    CHECK(readerLoaded && readerRead); CHECK(brokenRefused);
    for (const auto& p : sco::app::Plugins())
        if (p.folder == "code_pack" || p.folder == "a_dependent") CHECK(p.state == P::State::Refused);
    CHECK(sco::app::Content().FromPlugin("broken_pack").empty());
    CHECK(sco::app::Content().FromPlugin("a_dependent").empty());
    sco_content_item item{}; item.size = sizeof(item);
    CHECK(table->item(self, SCO_CONTENT_LIST, 0, &item) == SCO_OK);
    CHECK(std::strcmp(item.name, "lists/empty.txt") == 0);
    uint32_t n = 0; CHECK(table->read(self, item.id, nullptr, &n) == SCO_OK && n == 0);
    item.size = sizeof(item);
    CHECK(table->item(self, SCO_CONTENT_LIST, 1, &item) == SCO_OK);
    CHECK(std::strcmp(item.pack, "a_pack") == 0 && std::strcmp(item.name, "lists/shared.txt") == 0);
    const auto oldId = item.id;
    n = 0; CHECK(table->read(self, item.id, nullptr, &n) == SCO_TOO_MANY && n == 3);
    char bytes[8]; std::memset(bytes, 'Q', sizeof(bytes));
    n = 2; CHECK(table->read(self, item.id, bytes, &n) == SCO_TOO_MANY && n == 3 && bytes[0] == 'Q');
    n = sizeof(bytes); CHECK(table->read(self, item.id, bytes, &n) == SCO_OK && n == 3 && std::memcmp(bytes, "a\0b", 3) == 0 && bytes[3] == 'Q');
    Write(root / "a_pack/lists/shared.txt", "modified");
    n = sizeof(bytes); CHECK(table->read(self, item.id, bytes, &n) == SCO_OK && n == 3); // immutable startup copy
    CHECK(table->item(self, SCO_CONTENT_LIST, 2, &item) == SCO_OK && std::strcmp(item.pack, "b_pack") == 0 && item.id != oldId);
    CHECK(table->item(self, SCO_CONTENT_LIST, 3, &item) == SCO_NOT_FOUND);
    CHECK(table->item(self, SCO_CONTENT_SCRIPT, 0, &item) == SCO_OK && std::strcmp(item.name, "scripts/nested/example.xml") == 0);
    const auto before = item;
    CHECK(table->item(self, static_cast<sco_content_kind>(4), 0, &item) == SCO_BAD_ARG && std::memcmp(&before, &item, sizeof(item)) == 0);
    item.size = sizeof(item) - 1;
    CHECK(table->item(self, SCO_CONTENT_LIST, 0, &item) == SCO_BAD_ARG);
    CHECK(table->item(self, SCO_CONTENT_LIST, 0, nullptr) == SCO_BAD_ARG);
    n = 1; CHECK(table->read(self, oldId, nullptr, &n) == SCO_BAD_ARG && n == 1);
    CHECK(table->read(self, oldId, bytes, nullptr) == SCO_BAD_ARG);
    n = sizeof(bytes); CHECK(table->read(self, UINT64_MAX, bytes, &n) == SCO_NOT_FOUND && n == sizeof(bytes));
    CHECK(table->read(nullptr, oldId, bytes, &n) == SCO_BAD_ARG);
    std::thread wrong([&] {
        sco_content_item o{}; o.size = sizeof(o);
        CHECK(table->item(self, SCO_CONTENT_LIST, 0, &o) == SCO_WRONG_THREAD);
        CHECK(table->read(self, oldId, bytes, &n) == SCO_WRONG_THREAD);
    }); wrong.join();
    Guards(self);
    sco::Release(self); CHECK(table->read(self, oldId, bytes, &n) == SCO_BAD_ARG);
    sco::app::Stop(); CHECK(table->read(self, oldId, bytes, &n) == SCO_UNAVAILABLE);
    CHECK(sco::app::Start(pf));
    self = sco::host::NewPlugin("test_reader");
    n = sizeof(bytes); CHECK(table->read(self, oldId, bytes, &n) == SCO_NOT_FOUND);
    sco::Release(self); sco::app::Stop();
    pf.pluginsEnabled = false; CHECK(sco::app::Start(pf));
    self = sco::host::NewPlugin("test_reader"); item.size = sizeof(item);
    CHECK(table->item(self, SCO_CONTENT_LIST, 0, &item) == SCO_NOT_FOUND);
    sco::Release(self); sco::app::Stop();

    // Between index and snapshot, replace regular files and parent dirs with links.
    // Public callbacks still have no paths; host-side snapshot refuses entire pack.
    sco::SetGameThread(); sco::detail::SetRuntimeStopping(false);
    P::Options opts{}; opts.enabled = true;
    auto list = P::Discover(root, opts); P::ContentIndex index; index.Build(list);
    fs::remove(root / "b_pack/lists/shared.txt");
    fs::create_symlink(root / "a_pack/lists/shared.txt", root / "b_pack/lists/shared.txt", ec);
#ifndef _WIN32
    CHECK(!ec);
#endif
    fs::rename(root / "a_pack/scripts/nested", root / "a_pack/scripts/original");
    std::error_code dirEc;
    fs::create_directory_symlink(root / "a_pack/scripts/original", root / "a_pack/scripts/nested", dirEc);
#ifndef _WIN32
    CHECK(!dirEc);
#endif
    CHECK(sco::content::Start() == sco::Result::Ok);
    CHECK(sco::content::Snapshot(list, index) == sco::Result::Ok);
    for (const auto& p : list) if (p.folder == "b_pack" && !ec) CHECK(p.state == P::State::Refused);
    for (const auto& p : list) if (p.folder == "a_pack" && !dirEc) CHECK(p.state == P::State::Refused);
    CHECK(sco::content::Snapshot(list, index) == sco::Result::BadArg);
    sco::content::Stop();
    // Whole-pack file count refusal is existing indexing, retained and exercised.
    const auto quota = base / "quota"; Pack(quota, "too_many");
    for (unsigned i = 0; i <= P::kMaxPackFiles; ++i) Write(quota / "too_many/lists" / (std::to_string(i) + ".txt"), "");
    list = P::Discover(quota, opts); CHECK(index.Build(list) == 0 && list[0].state == P::State::Refused);
    // Exactly 4096 empty items fit; an additional pack refuses atomically.
    fs::remove(quota / "too_many/lists" / (std::to_string(P::kMaxPackFiles) + ".txt"));
    Pack(quota, "z_extra"); Write(quota / "z_extra/lists/a.txt", "");
    list = P::Discover(quota, opts); index.Build(list);
    CHECK(sco::content::Start() == sco::Result::Ok);
    CHECK(sco::content::Snapshot(list, index) == sco::Result::Ok);
    CHECK(list[0].state == P::State::Loaded && list[1].state == P::State::Refused);
    CHECK(index.Size() == SCO_CONTENT_MAX_ITEMS);
    sco::content::Stop();
    const auto totalRoot = base / "total";
    Pack(totalRoot, "first"); Pack(totalRoot, "second");
    for (unsigned i = 0; i < 16; ++i) Write(totalRoot / "first/lists" / (std::to_string(i) + ".txt"), std::string(SCO_CONTENT_MAX_FILE_BYTES, 's'));
    Write(totalRoot / "second/lists/a.txt", "");
    Write(totalRoot / "second/lists/z.txt", "over limit");
    list = P::Discover(totalRoot, opts); index.Build(list);
    CHECK(sco::content::Start() == sco::Result::Ok);
    CHECK(sco::content::Snapshot(list, index) == sco::Result::Ok);
    CHECK(list[0].state == P::State::Loaded && list[1].state == P::State::Refused);
    CHECK(index.FromPlugin("second").empty());
    sco::detail::SetRuntimeStopping(true);
    self = sco::host::NewPlugin("test_reader");
    CHECK(table->read(self, oldId, bytes, &n) == SCO_UNAVAILABLE);
    sco::content::Stop(); sco::Release(self);
    fs::remove_all(base);
    std::printf("test_content: %d passed, %d failed (synthetic; not run in game)\n", pass, fail);
    return fail ? 1 : 0;
}
