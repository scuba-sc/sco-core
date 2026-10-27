// SPDX-License-Identifier: GPL-3.0-only
#include <sc_asop_selection.h>
#include <sco/game/asop_selection.h>
#include <scosdk/game/asop_selection.hpp>
#include <sco/host.h>
#include <sco/runtime.h>
#include "../src/api/internal.h"
#include "../src/game/services/spawn.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
namespace S = sco::game::asop_selection;
static int passed=0,failed=0;
#define CHECK(x) do { if(x) ++passed; else {++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);} } while(0)
static std::vector<int> release_order;
static void hook1(const void*) { release_order.push_back(1); }
static void hook2(const void*) { release_order.push_back(2); }
static void hook3(const void*) { release_order.push_back(3); }
static void hook4(const void*) { release_order.push_back(4); }
static void hook5(const void*) { release_order.push_back(5); }
struct MemoryFile {
    S::Document document;
    unsigned reads=0,writes=0;
    S::FileResult read_result=S::FileResult::Ok;
    bool reenter=false;
    const void* release_after_replace=nullptr;
    const void* release_before_commit=nullptr;
    const void* quarantine_before_commit=nullptr;
    S::FileResult replace_result=S::FileResult::Ok;
    sco::Result reentry_result=sco::Result::Ok;
    static S::FileResult Read(void* context,S::Document& out) {
        auto& self=*static_cast<MemoryFile*>(context);++self.reads;
        if(self.reenter) self.reentry_result=S::Stop();
        if(self.read_result!=S::FileResult::Ok) return self.read_result;
        out=self.document;return S::FileResult::Ok;
    }
    static S::FileResult Replace(void* context,const S::Document& expected,std::string_view bytes,S::Document& out,const S::CommitPermit& permit) {
        auto& self=*static_cast<MemoryFile*>(context);++self.writes;
        if(self.document.present!=expected.present||self.document.bytes!=expected.bytes||self.document.identity!=expected.identity)
            return S::FileResult::Conflict;
        if(self.replace_result!=S::FileResult::Ok) return self.replace_result;
        if(self.release_before_commit) { sco::Release(self.release_before_commit); self.release_before_commit=nullptr; }
        if(self.quarantine_before_commit) {
            const void* owner=self.quarantine_before_commit;
            std::thread([owner]{ CHECK(sco::detail::QuarantineOwner(owner)==sco::Result::Ok); }).join();
            self.quarantine_before_commit=nullptr;
        }
        if(!permit.begin(permit.context)) return S::FileResult::Cancelled;
        self.document.present=true;self.document.bytes=bytes;++self.document.identity[0];out=self.document;
        if(self.release_after_replace) { sco::Release(self.release_after_replace); self.release_after_replace=nullptr; }
        return S::FileResult::Ok;
    }
};
int main() {
    sco::SetGameThread();
    const auto* api=sco::host::BuildApi({"ASOP selection synthetic"});
    auto* self=sco::host::NewPlugin("asop_selection_test");
    const void* raw=nullptr;
    CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_NOT_FOUND);
    MemoryFile file;
    file.document.present=true;
    file.document.bytes="# selected ships\r\n Test_Ship_A\nTest_Ship_B # note\n test_ship_a\n";
    S::Options options;
    options.file={&file,MemoryFile::Read,MemoryFile::Replace};
    options.catalogue={"Test_Ship_A","Test_Ship_B"};
    CHECK(S::Start(options)==sco::Result::Ok);
    CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_OK);
    const auto* table=static_cast<const sc_asop_selection_v1*>(raw);
    sco::sdk::game::AsopSelection wrapped;
    CHECK(wrapped.Open(api,self)==SCO_OK);
    sc_asop_selection_state state{};state.size=sizeof(state);
    CHECK(table&&wrapped.State(state)==SCO_OK);
    CHECK((state.flags&SC_ASOP_SELECTION_FILE_KNOWN)!=0);
    CHECK((state.flags&SC_ASOP_SELECTION_FILE_PRESENT)!=0);
    CHECK(state.file_count==2);
    CHECK((state.flags&(SC_ASOP_SELECTION_PUBLISHED_KNOWN|SC_ASOP_SELECTION_OBSERVED_KNOWN))==0);
    CHECK(file.reads==1&&file.writes==0);
    sc_asop_selection_read query{}; query.size=sizeof(query); query.kind=SC_ASOP_SELECTION_ROWS_FILE;
    query.session=state.session; query.revision=state.file_revision;
    sc_asop_selection_row rows[2]{}; rows[0].size=sizeof(rows[0]); rows[1].size=sizeof(rows[1]);
    uint32_t count=77,more=77;
    CHECK(wrapped.Rows(query,rows,sizeof(rows[0]),2,count,more)==SCO_OK && count==2 && more==0);
    CHECK(std::strcmp(rows[0].class_name,"Test_Ship_A")==0 && std::strcmp(rows[1].class_name,"Test_Ship_B")==0);
    sc_asop_selection_row original=rows[0];rows[1].size=1;count=77;more=77;
    CHECK(table->rows(self,&query,rows,sizeof(rows[0]),2,&count,&more)==SCO_BAD_ARG &&
          std::memcmp(&rows[0],&original,sizeof(original))==0 && count==77 && more==77);
    rows[1].size=sizeof(rows[1]);
    CHECK(table->rows(self,&query,nullptr,0,0,&count,&more)==SCO_OK && count==0 && more==2);
    query.kind=SC_ASOP_SELECTION_ROWS_CATALOGUE;query.revision=state.catalogue_generation;
    CHECK(table->rows(self,&query,rows,sizeof(rows[0]),2,&count,&more)==SCO_OK && count==2);
    query.kind=SC_ASOP_SELECTION_ROWS_FILE;query.revision=state.file_revision;


    const uint64_t file_revision=state.file_revision, semantic_revision=state.semantic_revision;
    uint8_t first_digest[32]{};std::memcpy(first_digest,state.file_digest,32);
    file.document.bytes="# changed comment\nTest_Ship_A\nTest_Ship_B\n";
    ++file.document.identity[0];
    CHECK(wrapped.Refresh()==SCO_OK);
    CHECK(table->state(self,&state)==SCO_OK && state.file_revision>file_revision);
    count=77;more=77;
    CHECK(table->rows(self,&query,rows,sizeof(rows[0]),2,&count,&more)==SCO_NOT_FOUND && count==77 && more==77);

    CHECK(state.semantic_revision==semantic_revision && state.file_count==2);
    CHECK(std::memcmp(first_digest,state.file_digest,32)!=0);
    file.document.bytes="Test_Ship_B\n"; ++file.document.identity[0];
    CHECK(table->refresh(self)==SCO_OK);
    CHECK(table->state(self,&state)==SCO_OK && state.semantic_revision>semantic_revision && state.file_count==1);
    file.read_result=S::FileResult::Error;
    CHECK(table->refresh(self)==SCO_FAILED);
    CHECK(table->state(self,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN) &&
          (state.flags&SC_ASOP_SELECTION_FILE_ERROR) && state.reason==SC_ASOP_SELECTION_REASON_IO);
    file.read_result=S::FileResult::Invalid;
    CHECK(table->refresh(self)==SCO_BAD_ARG);
    CHECK(table->state(self,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN) && state.reason==SC_ASOP_SELECTION_REASON_INVALID);
    file.read_result=S::FileResult::Ok;file.reenter=true;
    CHECK(table->refresh(self)==SCO_OK && file.reentry_result==sco::Result::Failed);
    file.reenter=false;
    CHECK(table->state(self,&state)==SCO_OK && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN) && state.file_count==1);
    file.document.bytes="bad class with spaces\n"; ++file.document.identity[0];
    CHECK(table->refresh(self)==SCO_BAD_ARG);
    CHECK(table->state(self,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN) &&
          state.reason==SC_ASOP_SELECTION_REASON_INVALID);


    file.document.bytes="Test_Ship_B\n"; ++file.document.identity[0];
    CHECK(table->refresh(self)==SCO_OK && table->state(self,&state)==SCO_OK);
    auto* other=sco::host::NewPlugin("asop_selection_other");
    uint64_t lease=0, lease_again=0, denied=9;
    CHECK(wrapped.Acquire(state.session,lease)==SCO_OK && lease!=0);
    sc_asop_selection_class chosen[2]{};
    chosen[0].size=sizeof(chosen[0]);std::strcpy(chosen[0].class_name,"Test_Ship_A");
    chosen[1].size=sizeof(chosen[1]);std::strcpy(chosen[1].class_name,"Unresolved_Ship");
    sc_asop_selection_request request{}; request.size=sizeof(request);request.session=state.session;
    request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32);request.client_key=1;request.classes=chosen;request.count=2;request.class_stride=sizeof(chosen[0]);
    uint64_t id=0, replay=0;
    CHECK(wrapped.Replace(lease,request,id)==SCO_OK && id!=0 && file.writes==0);
    CHECK(S::Pump()==sco::Result::Ok && file.writes==1);
    sc_asop_selection_receipt receipt{};receipt.size=sizeof(receipt);
    CHECK(wrapped.Status(lease,id,receipt)==SCO_OK && receipt.phase==SC_ASOP_SELECTION_FILE_COMMITTED);
    CHECK(table->state(self,&state)==SCO_OK && state.file_count==2);
    CHECK(table->replace(self,lease,&request,&replay)==SCO_OK && replay==id && file.writes==1);
    chosen[1].class_name[0]='X';replay=9;
    CHECK(table->replace(self,lease,&request,&replay)==SCO_BAD_ARG && replay==9 && file.writes==1);
    chosen[1].class_name[0]='U';request.client_key=2;request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32);
    uint64_t conflict_id=0;
    CHECK(table->replace(self,lease,&request,&conflict_id)==SCO_OK);
    file.document.bytes="External_Ship\n"; ++file.document.identity[0];
    CHECK(table->refresh(self)==SCO_OK);
    CHECK(S::Pump()==sco::Result::Ok && file.writes==1);
    receipt.size=sizeof(receipt);
    CHECK(table->status(self,lease,conflict_id,&receipt)==SCO_OK && receipt.phase==SC_ASOP_SELECTION_EDIT_CONFLICT);
    CHECK(file.document.bytes=="External_Ship\n");
    CHECK(table->state(self,&state)==SCO_OK && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    request.client_key=3;request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32); uint64_t conflict2=0;
    CHECK(table->replace(self,lease,&request,&conflict2)==SCO_OK);
    file.document.bytes="Other_External_Ship\n"; ++file.document.identity[0];
    CHECK(S::Pump()==sco::Result::Ok && file.writes==2);
    receipt.size=sizeof(receipt);
    CHECK(table->status(self,lease,conflict2,&receipt)==SCO_OK && receipt.phase==SC_ASOP_SELECTION_EDIT_CONFLICT);
    CHECK(file.document.bytes=="Other_External_Ship\n");
    CHECK(table->state(self,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    CHECK(table->refresh(self)==SCO_OK && table->state(self,&state)==SCO_OK);
    request.client_key=4;request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32);uint64_t cancelled=0;
    CHECK(table->replace(self,lease,&request,&cancelled)==SCO_OK);



    CHECK(table->acquire(self,state.session,&lease_again)==SCO_OK && lease_again==lease);
    CHECK(table->acquire(other,state.session,&denied)==SCO_TOO_MANY && denied==9);
    CHECK(sco::Release(self)==sco::Result::Ok);
    CHECK(S::Pump()==sco::Result::Ok && file.writes==2);
    CHECK(table->state(self,&state)==SCO_BAD_ARG);
    CHECK(table->acquire(other,state.session,&denied)==SCO_OK && denied!=0 && denied!=lease);
    uint64_t evicted_id=0, evicted_revision=0;
    for(uint64_t key=1;key<=34;++key) {
        CHECK(table->state(other,&state)==SCO_OK);
        request.session=state.session;request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32);request.client_key=key;
        uint64_t current=0;
        CHECK(table->replace(other,denied,&request,&current)==SCO_OK);
        if(key==1){evicted_id=current;evicted_revision=request.file_revision;}
        CHECK(S::Pump()==sco::Result::Ok);
    }
    request.client_key=1;request.file_revision=evicted_revision;replay=99;
    const unsigned writes_before_replay=file.writes;
    CHECK(table->replace(other,denied,&request,&replay)==SCO_NOT_FOUND && replay==99 && file.writes==writes_before_replay);
    receipt.size=sizeof(receipt);
    CHECK(table->status(other,denied,evicted_id,&receipt)==SCO_NOT_FOUND);

    sco::sdk::game::AsopSelection other_wrapped;
    CHECK(other_wrapped.Open(api,other)==SCO_OK && other_wrapped.Release(denied)==SCO_OK);
    CHECK(table->release(other,denied)==SCO_OK);
    auto* faulty=sco::host::NewPlugin("asop_selection_faulty"); uint64_t fault_lease=0, after_fault=0;
    CHECK(table->acquire(faulty,state.session,&fault_lease)==SCO_OK);
    CHECK(sco::detail::QuarantineOwner(faulty)==sco::Result::Ok);
    CHECK(table->acquire(other,state.session,&after_fault)==SCO_OK && after_fault!=fault_lease);
    CHECK(table->release(other,after_fault)==SCO_OK);
    auto* finishing=sco::host::NewPlugin("asop_selection_finishing"); uint64_t finishing_lease=0, finishing_id=0;
    CHECK(table->state(finishing,&state)==SCO_OK);
    CHECK(table->acquire(finishing,state.session,&finishing_lease)==SCO_OK);
    request.session=state.session; request.file_revision=state.file_revision;
    std::memcpy(request.file_digest,state.file_digest,32);request.client_key=1;
    CHECK(table->replace(finishing,finishing_lease,&request,&finishing_id)==SCO_OK);
    file.release_after_replace=finishing;
    CHECK(S::Pump()==sco::Result::Ok);
    CHECK(table->state(finishing,&state)==SCO_BAD_ARG);
    CHECK(table->state(other,&state)==SCO_OK && state.file_count==2 && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    CHECK(file.document.bytes.find("Unresolved_Ship")!=std::string::npos);
    auto* early=sco::host::NewPlugin("asop_selection_early");uint64_t early_lease=0,early_id=0;
    CHECK(table->acquire(early,state.session,&early_lease)==SCO_OK);
    request.session=state.session;request.file_revision=state.file_revision;
    std::memcpy(request.file_digest,state.file_digest,32);request.client_key=1;
    CHECK(table->replace(early,early_lease,&request,&early_id)==SCO_OK);
    const std::string before_early=file.document.bytes;
    file.release_before_commit=early;
    CHECK(S::Pump()==sco::Result::Ok && file.document.bytes==before_early);
    CHECK(table->state(other,&state)==SCO_OK && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    auto* quarantined=sco::host::NewPlugin("asop_selection_quarantined");uint64_t qlease=0,qid=0;
    CHECK(table->acquire(quarantined,state.session,&qlease)==SCO_OK);
    request.session=state.session;request.file_revision=state.file_revision;
    std::memcpy(request.file_digest,state.file_digest,32);request.client_key=1;
    CHECK(table->replace(quarantined,qlease,&request,&qid)==SCO_OK);
    file.quarantine_before_commit=quarantined;
    CHECK(S::Pump()==sco::Result::Ok && file.document.bytes==before_early);
    CHECK(table->state(other,&state)==SCO_OK && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    uint64_t final_lease=0,none_id=0,unknown_id=0;
    CHECK(table->acquire(other,state.session,&final_lease)==SCO_OK);
    request.session=state.session;request.file_revision=state.file_revision;
    std::memcpy(request.file_digest,state.file_digest,32);request.client_key=1;request.count=0;request.classes=nullptr;
    CHECK(table->replace(other,final_lease,&request,&none_id)==SCO_OK);
    CHECK(S::Pump()==sco::Result::Ok && file.document.present && file.document.bytes=="# ASOP ship selection, one class per line.\n");
    receipt.size=sizeof(receipt);
    CHECK(table->status(other,final_lease,none_id,&receipt)==SCO_OK && receipt.phase==SC_ASOP_SELECTION_FILE_COMMITTED);
    CHECK(table->state(other,&state)==SCO_OK && state.file_count==0 && (state.flags&SC_ASOP_SELECTION_FILE_PRESENT));
    request.file_revision=state.file_revision;std::memcpy(request.file_digest,state.file_digest,32);request.client_key=2;
    CHECK(table->replace(other,final_lease,&request,&unknown_id)==SCO_OK);
    file.replace_result=S::FileResult::OutcomeUnknown;
    CHECK(S::Pump()==sco::Result::Ok);
    receipt.size=sizeof(receipt);
    CHECK(table->status(other,final_lease,unknown_id,&receipt)==SCO_OK && receipt.phase==SC_ASOP_SELECTION_EDIT_UNKNOWN);
    CHECK(table->state(other,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    const auto writes_after_unknown=file.writes;replay=0;
    CHECK(table->replace(other,final_lease,&request,&replay)==SCO_OK && replay==unknown_id && file.writes==writes_after_unknown);
    file.replace_result=S::FileResult::Ok;
    CHECK(table->refresh(other)==SCO_OK && table->state(other,&state)==SCO_OK && (state.flags&SC_ASOP_SELECTION_FILE_KNOWN));
    CHECK(table->release(other,final_lease)==SCO_OK);
    CHECK(sco::Release(other)==sco::Result::Ok);
    CHECK(S::Stop()==sco::Result::Ok);
    CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_NOT_FOUND);
    namespace G=sco::game::services;
    CHECK(G::AddGameReleaseHook(hook1)==sco::Result::Ok && G::AddGameReleaseHook(hook2)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(hook3)==sco::Result::Ok && G::AddGameReleaseHook(hook4)==sco::Result::Ok);
    CHECK(S::Start(options)==sco::Result::Ok);
    CHECK(G::AddGameReleaseHook(hook5)==sco::Result::TooMany);
    auto* observer=sco::host::NewPlugin("asop_release_order");
    CHECK(sco::Release(observer)==sco::Result::Ok && release_order==std::vector<int>({1,2,3,4}));
    CHECK(S::Stop()==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(hook1)==sco::Result::Ok && G::RemoveGameReleaseHook(hook2)==sco::Result::Ok);
    CHECK(G::RemoveGameReleaseHook(hook3)==sco::Result::Ok && G::RemoveGameReleaseHook(hook4)==sco::Result::Ok);
    // An unreadable fixed file remains visible as unknown and can recover on refresh.
    file.read_result=S::FileResult::Error;
    CHECK(S::Start(options)==sco::Result::Ok);
    auto* recovery=sco::host::NewPlugin("asop_selection_recovery");
    raw=nullptr;CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_OK);
    table=static_cast<const sc_asop_selection_v1*>(raw);state={};state.size=sizeof(state);
    CHECK(table->state(recovery,&state)==SCO_OK && !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN) && state.reason==SC_ASOP_SELECTION_REASON_IO);
    file.read_result=S::FileResult::NotFound;
    CHECK(table->refresh(recovery)==SCO_OK && table->state(recovery,&state)==SCO_OK &&
          (state.flags&SC_ASOP_SELECTION_FILE_KNOWN) && !(state.flags&SC_ASOP_SELECTION_FILE_PRESENT));
    CHECK(sco::Release(recovery)==sco::Result::Ok);
    CHECK(S::Stop()==sco::Result::Ok);
    std::printf("asop_selection: %d passed, %d failed\n",passed,failed);
    return failed?1:0;
}
