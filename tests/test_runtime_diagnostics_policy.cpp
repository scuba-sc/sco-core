// SPDX-License-Identifier: GPL-3.0-only
// Synthetic collector/policy logic only; NOT Windows privilege acceptance.
#include "ipc/diagnostics_ipc.h"
#include "sco/app.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#endif
using namespace sco::diagnostics;
namespace di=sco::diagnostics::ipc;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": "<<#x<<'\n';std::abort();}}while(0)
// Test setup, not a relaxation of the production credential policy. Windows
// filesystem::permissions only changes readonly attributes, not the directory
// DACL. Match the current-token protected directory used by the platform test.
static void CreatePrivateFixtureDirectory(const std::filesystem::path& dir) {
#ifdef _WIN32
    HANDLE token = nullptr;
    CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    CHECK(bytes > 0);
    std::vector<unsigned char> tokenInfo(bytes);
    CHECK(GetTokenInformation(token, TokenUser, tokenInfo.data(), bytes, &bytes));
    CHECK(CloseHandle(token));
    LPWSTR sid = nullptr;
    CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenInfo.data())->User.Sid, &sid));
    const std::wstring ownSid(sid);
    LocalFree(sid);
    const std::wstring sddl = L"O:" + ownSid + L"D:P(A;;FA;;;" + ownSid + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const BOOL created = CreateDirectoryW(dir.c_str(), &attributes);
    const DWORD error = created ? ERROR_SUCCESS : GetLastError();
    LocalFree(descriptor);
    if (!created) std::cerr << "private fixture directory failed: " << error << '\n';
    CHECK(created);
#else
    CHECK(std::filesystem::create_directory(dir));
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
#endif
}
#define CHECK_RESULT(expression, expected) do { \
    const auto actualResult = (expression); \
    if (actualResult != (expected)) std::cerr << "Start result=" << static_cast<int>(actualResult) \
        << ", expected=" << static_cast<int>(expected) << '\n'; \
    CHECK(actualResult == (expected)); \
} while (0)

struct Collector final:audit::Collector {
    audit::Status status=audit::Status::Accepted; unsigned events=0;
    std::shared_ptr<std::atomic<OfflineAdmission>> admission; bool revoke=false;
    audit::Receipt Verify(uint64_t) override {return {audit::Status::Verified,di::ClockNow(),true,true};}
    audit::Receipt Append(const di::AuditEvent& e,const audit::Coordinates&,uint64_t deadline) override {
        ++events;if(revoke&&e.opcode==0x100)admission->store(OfflineAdmission::Denied);
        return {status,status==audit::Status::Deadline?deadline:di::ClockNow(),true,true};
    }
};
int main(int argc,char** argv){
    CHECK(argc==3);sco::SetGameThread();sco::app::Platform defaults;
    CHECK(!defaults.runtimeInspection.enabled&&!defaults.runtimeInspection.disclosureConsent);
    SnapshotStore snapshots;Capture capture;di::Host host(snapshots,capture);di::Options o;
    auto collector=std::make_shared<Collector>();o.policy.collector=collector;
    o.policy.admission=collector->admission=std::make_shared<std::atomic<OfflineAdmission>>(OfflineAdmission::Unknown);
    CHECK(host.Start(o,di::ClockNow())==di::Result::Disabled);
    o.enabled=o.policy.enabled=o.policy.disclosureConsent=true;
    for(auto posture:{OfflineAdmission::Unknown,OfflineAdmission::Online,OfflineAdmission::Denied}){
        o.policy.admission->store(posture);CHECK(host.Start(o,di::ClockNow())==di::Result::Disabled);CHECK(!host.Started());
    }
    const auto excludedRoot=std::filesystem::canonical(argv[2]);
    auto credentialRoot=std::filesystem::canonical(argv[1]);
    const auto relative=credentialRoot.lexically_relative(excludedRoot);
    // An in-tree CMake build is inside the excluded plugin/source root. Keep
    // credentials outside it rather than relaxing the host's exclusion policy.
    if(relative.empty()||*relative.begin()!="..")
        credentialRoot=excludedRoot.parent_path();
    // Sibling fallback stays outside the excluded source root on the same volume.
    const auto outside=credentialRoot.lexically_relative(excludedRoot);
    CHECK(!outside.empty() && *outside.begin()=="..");
    di::Id nonce{};CHECK(platform::Random(nonce.data(),nonce.size()));
    const auto dir=credentialRoot/("runtime-policy-"+di::Hex(nonce.data(),nonce.size()));
    CreatePrivateFixtureDirectory(dir);
    // Resolve only the trusted fixture directory (e.g. macOS /tmp alias),
    // not the credential filename: transport symlink refusal stays intact.
    o.credentialPath=(std::filesystem::canonical(dir)/"session.json").string();o.excludedRoots={excludedRoot.string()};o.hostBuild="fixture-host-1";
    o.policy.admission->store(OfflineAdmission::Authorized);
    for(auto status:{audit::Status::Unavailable,audit::Status::Full,audit::Status::Deadline}){
        collector->status=status;CHECK_RESULT(host.Start(o,di::ClockNow()),di::Result::Audit);CHECK(!host.Started());
    }
    collector->status=audit::Status::Accepted;CHECK_RESULT(host.Start(o,di::ClockNow()),di::Result::Ok);
    di::Peer peer;CHECK(peer.Begin(o.credentialPath,di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Ok);CHECK(peer.Challenge(di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Ok);CHECK(peer.Ready(di::ClockNow())==di::Result::Ok);
    collector->revoke=true;CHECK(peer.Send(0x100,"{}",di::ClockNow())==di::Result::Ok);
    CHECK(host.Poll(di::ClockNow())==di::Result::Disabled);CHECK(!host.Started());
    std::string output;CHECK(peer.Receive(di::ClockNow(),output)!=di::Result::Ok&&output.empty());
    CHECK(!std::filesystem::exists(o.credentialPath));peer.Close();host.Stop();std::filesystem::remove(dir);
    std::cout<<"policy defaults, posture, audit failures and pending-read revocation passed (synthetic)\n";
}
