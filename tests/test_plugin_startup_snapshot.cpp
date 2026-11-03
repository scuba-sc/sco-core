// Synthetic contract and file-publication tests; no game process or native plugin execution.
#include "../src/app/plugin_startup_snapshot.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace P = sco::plugins;
namespace PS = sco::app::detail::plugin_startup;

static int checks = 0;
static int failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static P::Plugin Plugin(const char* id, P::State state, P::Kind kind = P::Kind::Native) {
    P::Plugin plugin;
    plugin.folder = id;
    plugin.manifestOk = true;
    plugin.manifest.id = id;
    plugin.manifest.version = "1.2.3+test";
    plugin.manifest.kind = kind;
    plugin.state = state;
    plugin.reason = "raw plugin text with a private path";
    return plugin;
}

static std::string ReadWholeFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void TestProjection() {
    std::vector<P::Plugin> list;
    list.push_back(Plugin("off_one", P::State::Off));
    list.push_back(Plugin("disabled_one", P::State::Disabled, P::Kind::Lua));
    list.push_back(Plugin("refused_one", P::State::Refused, P::Kind::Data));
    list.push_back(Plugin("ready_one", P::State::Ready, P::Kind::Builtin));
    list.push_back(Plugin("loaded_one", P::State::Loaded));
    list.push_back(Plugin("crashed_one", P::State::Crashed));
    list.push_back(Plugin("unloaded_one", P::State::Unloaded));
    list.push_back(Plugin("faulted_one", P::State::Faulted));
    list.push_back(Plugin("unknown_10", P::State::Ready));
    list.push_back(Plugin("unknown_9", P::State::Ready));
    P::Plugin invalid;
    invalid.folder = "../../private/player";
    invalid.manifestOk = false;
    invalid.state = P::State::Refused;
    invalid.reason = "plugin.ini: private path and free-form error";
    list.push_back(std::move(invalid));

    const std::string session = "0123456789abcdef0123456789abcdef";
    const std::string started = "2026-11-03T08:00:00Z";
    const std::string completed = "2026-11-03T08:00:01Z";
    PS::Record record = PS::Project(list, true, session, started, completed);
    std::string json;
    CHECK(record.state == PS::RecordState::Complete && record.pluginCount == list.size() && record.loadedCount == 1);
    CHECK(PS::Serialize(record, json));
    CHECK(json.find("{\"schema_version\":1,\"record_state\":\"complete\"") == 0);
    CHECK(json.find("\"plugins_enabled_at_start\":true,\"plugin_count\":11,\"loaded_count\":1,\"plugins\":[") != std::string::npos);
    CHECK(json.find("{\"id\":\"off_one\",\"manifest_valid\":true,\"version\":\"1.2.3+test\",\"kind\":\"native\",\"state\":\"off\",\"reason_code\":\"plugins_off\",\"restart_required\":false}") != std::string::npos);
    CHECK(json.find("\"kind\":\"lua\",\"state\":\"disabled\",\"reason_code\":\"plugin_disabled\"") != std::string::npos &&
          json.find("\"kind\":\"data\",\"state\":\"refused\",\"reason_code\":\"plugin_refused\"") != std::string::npos &&
          json.find("\"kind\":\"builtin\",\"state\":\"ready\",\"reason_code\":null") != std::string::npos);
    CHECK(json.find("\"state\":\"crashed\",\"reason_code\":\"plugin_crashed\"") != std::string::npos &&
          json.find("\"state\":\"loaded\",\"reason_code\":null") != std::string::npos &&
          json.find("\"state\":\"unloaded\",\"reason_code\":null") != std::string::npos &&
          json.find("\"reason_code\":\"provider_fault_restart_required\",\"restart_required\":true") != std::string::npos);
    CHECK(json.find("\"id\":\"unknown_11\",\"manifest_valid\":false,\"version\":null,\"kind\":\"unknown\",\"state\":\"refused\",\"reason_code\":\"invalid_manifest\"") != std::string::npos);
    CHECK(json.find("private") == std::string::npos && json.find("../../") == std::string::npos && json.size() <= PS::kMaxBytes);

    auto invalidOff = list;
    invalidOff.back().state = P::State::Off;
    PS::Record stateConsistent = PS::Project(invalidOff, true, session, started, completed);
    CHECK(stateConsistent.plugins && stateConsistent.plugins->back().reasonCode == "plugins_off" &&
          PS::Serialize(stateConsistent, json));
    stateConsistent.plugins->back().reasonCode = "invalid_manifest";
    CHECK(!PS::Serialize(stateConsistent, json));

    PS::Record starting = PS::StartingRecord(session, started, false);
    CHECK(PS::Serialize(starting, json) && json.find("\"record_state\":\"starting\"") != std::string::npos &&
          json.find("\"completed_at_utc\":null") != std::string::npos && json.ends_with("\"plugins\":null}"));
    PS::Record failed = PS::FailedRecord(list, true, session, started, completed);
    CHECK(PS::Serialize(failed, json) && json.find("\"record_state\":\"failed\"") != std::string::npos &&
          json.ends_with("\"plugins\":null}"));

    auto unsupported = list;
    unsupported[0].manifest.version = "release candidate";
    PS::Record incomplete = PS::Project(unsupported, true, session, started, completed);
    CHECK(incomplete.state == PS::RecordState::Incomplete && !incomplete.plugins &&
          PS::Serialize(incomplete, json) && json.find("\"record_state\":\"incomplete\"") != std::string::npos &&
          json.ends_with("\"plugins\":null}"));
    auto overLimit = list;
    overLimit.resize(PS::kMaxRows, Plugin("within_limit", P::State::Loaded));
    PS::Record atLimit = PS::Project(overLimit, true, session, started, completed);
    CHECK(atLimit.state == PS::RecordState::Complete && atLimit.pluginCount == PS::kMaxRows &&
          PS::Serialize(atLimit, json) && json.size() <= PS::kMaxBytes);
    overLimit = list;
    overLimit.resize(PS::kMaxRows + 1, Plugin("over_limit", P::State::Loaded));
    incomplete = PS::Project(overLimit, true, session, started, completed);
    CHECK(incomplete.state == PS::RecordState::Incomplete && incomplete.pluginCount == PS::kMaxRows + 1 &&
          !incomplete.plugins && PS::Serialize(incomplete, json));

    record.loadedCount = 2;
    CHECK(!PS::Serialize(record, json));
    record = PS::Project(list, true, session, started, completed);
    record.sessionId[0] = 'G';
    CHECK(!PS::Serialize(record, json));
}

static void TestWriter(const fs::path& out) {
    const fs::path data = out / "plugin-startup-contract";
    std::error_code ec;
    fs::remove_all(data, ec);
    const fs::path target = data / "plugin-startup.json";
    const std::string session = "0123456789abcdef0123456789abcdef";
    std::vector<P::Plugin> list{Plugin("fixture", P::State::Loaded)};
    PS::Writer first, second;
    CHECK(first.Begin(target, true) && first.Active());
    const std::string starting = ReadWholeFile(target);
    CHECK(starting.find("\"record_state\":\"starting\"") != std::string::npos &&
          starting.find("\"completed_at_utc\":null") != std::string::npos && starting.ends_with("\"plugins\":null}"));
    CHECK(!second.Begin(target, false) && !second.Active());
#ifndef _WIN32
    // Probe the held lock from a second process using a fresh open-file description.
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        const int fd = open((data / "plugin-startup.lock").c_str(), O_RDWR);
        const bool acquired = fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
        if (fd >= 0) close(fd);
        _exit(acquired ? 1 : 0);
    }
    if (child > 0) {
        int status = 0;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
#endif
    CHECK(first.Complete(list) && !first.Active());
    const std::string complete = ReadWholeFile(target);
    CHECK(complete.find("\"record_state\":\"complete\"") != std::string::npos &&
          complete.find("\"plugin_count\":1,\"loaded_count\":1") != std::string::npos && complete.size() <= PS::kMaxBytes);

    // A crash-left partial temporary file is never the reader path and cannot tear the last record.
    const fs::path partial = data / ("plugin-startup.json." + session + ".tmp");
    { std::ofstream file(partial, std::ios::binary); file << "{partial"; }
    CHECK(ReadWholeFile(target) == complete && fs::exists(partial));
    CHECK(second.Begin(target, false) && second.Failed(list));
    CHECK(!fs::exists(partial)); // acquired writer cleans only its strict, regular-file temp names
    const std::string failed = ReadWholeFile(target);
    CHECK(failed.find("\"record_state\":\"failed\"") != std::string::npos &&
          failed.find("\"plugins_enabled_at_start\":false") != std::string::npos && failed.ends_with("\"plugins\":null}"));
}

static void TestUnsafeEntriesAndReplacementFailure(const fs::path& out) {
    std::error_code ec;
    const fs::path data = out / "plugin-startup-unsafe-entries";
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);
    CHECK(!ec);
    const fs::path target = data / "plugin-startup.json";

#ifndef _WIN32
    // The lock path is opened without following a symlink; an unrelated sentinel stays untouched.
    const fs::path sentinel = data / "sentinel.txt";
    { std::ofstream file(sentinel, std::ios::binary); file << "keep"; }
    fs::create_symlink(sentinel, data / "plugin-startup.lock", ec);
    CHECK(!ec);
    PS::Writer lockSymlink;
    CHECK(!lockSymlink.Begin(target, true) && !fs::exists(target));
    CHECK(ReadWholeFile(sentinel) == "keep");
    fs::remove(data / "plugin-startup.lock", ec);
    CHECK(!ec);

    // A final-path symlink is atomically replaced as a directory entry, never followed.
    fs::create_symlink(sentinel, target, ec);
    CHECK(!ec);
    // Writer accepts only the canonical output filename; target itself is already a symlink.
    PS::Writer finalSymlink;
    CHECK(finalSymlink.Begin(target, true));
    CHECK(fs::is_regular_file(sentinel) && !fs::is_symlink(target));
    CHECK(ReadWholeFile(sentinel) == "keep");
    CHECK(finalSymlink.Complete({}));
    fs::remove(target, ec);
    CHECK(!ec);

    // Temp cleanup skips reparse-like entries and cannot remove or modify their destination.
    const fs::path symlinkTemp = data / "plugin-startup.json.0123456789abcdef0123456789abcdef.tmp";
    fs::create_symlink(sentinel, symlinkTemp, ec);
    CHECK(!ec);
    PS::Writer tempWriter;
    CHECK(tempWriter.Begin(target, true));
    CHECK(fs::is_symlink(symlinkTemp) && ReadWholeFile(sentinel) == "keep");
    CHECK(tempWriter.Complete({}));
    fs::remove(symlinkTemp, ec);
    CHECK(!ec);
#else
    (void)target;
#endif

    // If replacement cannot happen, startup remains independent and no partial temp is exposed.
    const fs::path blocked = out / "plugin-startup-replacement-failure";
    fs::remove_all(blocked, ec);
    fs::create_directories(blocked / "plugin-startup.json", ec);
    CHECK(!ec);
    PS::Writer replacement;
    CHECK(!replacement.Begin(blocked / "plugin-startup.json", true) && !replacement.Active());
    size_t temps = 0;
    for (const auto& item : fs::directory_iterator(blocked, ec)) {
        if (item.path().filename().string().starts_with("plugin-startup.json.")) ++temps;
    }
    CHECK(!ec && temps == 0);
}

int main(int argc, char** argv) {
    if (argc != 2) { std::printf("usage: test_plugin_startup_snapshot <output-dir>\n"); return 2; }
    TestProjection();
    TestWriter(argv[1]);
    TestUnsafeEntriesAndReplacementFailure(argv[1]);
    std::printf("plugin startup snapshot: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
