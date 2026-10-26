// SPDX-License-Identifier: GPL-3.0-only
// Real file coordinator and owner-release path feeding a separate synthetic model.
#include <sc_asop_selection.h>
#include <sco/game/asop_selection.h>
#include <sco/host.h>
#include <sco/runtime.h>
#include "../src/game/asop_selection/simulation.h"
#include <cstdio>
#include <string>
#include <vector>
namespace S=sco::game::asop_selection;
namespace M=sco::game::asop_selection::simulation;
static int failed=0,passed=0;
#define CHECK(x) do {if(x)++passed;else {++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
struct Disk {
    S::Document document;
    static S::FileResult Read(void* ctx,S::Document& out) {out=static_cast<Disk*>(ctx)->document;return S::FileResult::Ok;}
    static S::FileResult Replace(void*,const S::Document&,std::string_view,S::Document&,const S::CommitPermit&) {return S::FileResult::Error;}
};
bool ReadSelection(const sc_asop_selection_v1* table,sco_plugin* owner,sc_asop_selection_state& state,std::vector<std::string>& names) {
    state={};state.size=sizeof(state);
    if(table->state(owner,&state)!=SCO_OK || !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN))return false;
    sc_asop_selection_read read{};read.size=sizeof(read);read.kind=SC_ASOP_SELECTION_ROWS_FILE;read.session=state.session;read.revision=state.file_revision;
    sc_asop_selection_row rows[SC_ASOP_SELECTION_MAX_PAGE]{};for(auto& row:rows)row.size=sizeof(row);
    uint32_t count=0,more=0;if(table->rows(owner,&read,rows,sizeof(rows[0]),SC_ASOP_SELECTION_MAX_PAGE,&count,&more)!=SCO_OK || more)return false;
    names.clear();for(uint32_t i=0;i<count;++i)names.emplace_back(rows[i].class_name);return true;
}
int main() {
    sco::SetGameThread();const auto* api=sco::host::BuildApi({"ASOP synthetic pipeline"});
    auto* editor=sco::host::NewPlugin("selection_editor");auto* observer=sco::host::NewPlugin("selection_observer");
    Disk disk;disk.document.present=true;disk.document.bytes="A\n";
    S::Options options;options.file={&disk,Disk::Read,Disk::Replace};options.catalogue={"A","B"};
    CHECK(S::Start(options)==sco::Result::Ok);
    const void* raw=nullptr;CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_OK);
    const auto* table=static_cast<const sc_asop_selection_v1*>(raw);
    sc_asop_selection_state state{};std::vector<std::string> names;
    CHECK(ReadSelection(table,editor,state,names) && names==std::vector<std::string>({"A"}));
    M::Model model(3);CHECK(model.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(model.Request(state.semantic_revision,names)==M::Status::Ok && model.Stage(options.catalogue)==M::Status::Ok);
    CHECK(!model.State().published_known && !model.State().observed_known);
    CHECK(model.Publish()==M::Status::Ok && !model.State().observed_known);
    const auto old=model.State();uint64_t pin=0;CHECK(model.PinCurrent(pin)==M::Status::Ok);
    CHECK(!(state.flags&(SC_ASOP_SELECTION_PUBLISHED_KNOWN|SC_ASOP_SELECTION_OBSERVED_KNOWN)));
    CHECK(model.Observe(old.published_generation,42)==M::Status::Ok && model.State().observed_scope==42);
    CHECK(sco::Release(editor)==sco::Result::Ok);
    disk.document.bytes="B\n";++disk.document.identity[0];CHECK(table->refresh(observer)==SCO_OK);
    CHECK(ReadSelection(table,observer,state,names) && names==std::vector<std::string>({"B"}));
    CHECK(model.Request(state.semantic_revision,names)==M::Status::Ok && model.Stage(options.catalogue)==M::Status::Ok && model.Publish()==M::Status::Ok);
    CHECK(model.State().published_revision==state.semantic_revision && model.State().observed_revision==old.published_revision);
    std::vector<M::Row> old_rows;CHECK(model.Inspect(pin,old_rows)==M::Status::Ok && old_rows.size()==1 && old_rows[0].name=="A");
    CHECK(!(state.flags&(SC_ASOP_SELECTION_PUBLISHED_KNOWN|SC_ASOP_SELECTION_OBSERVED_KNOWN)));
    disk.document.bytes="Unresolved\n";++disk.document.identity[0];CHECK(table->refresh(observer)==SCO_OK);
    CHECK(ReadSelection(table,observer,state,names));
    CHECK(model.Request(state.semantic_revision,names)==M::Status::Ok && model.Stage(options.catalogue)==M::Status::NotFound);
    CHECK(model.State().requested_revision==state.semantic_revision && model.State().published_revision!=state.semantic_revision);
    CHECK(model.Stop()==M::Status::Busy && model.Fence(pin)==M::Status::Ok && model.Stop()==M::Status::Ok);
    CHECK(sco::Release(observer)==sco::Result::Ok && S::Stop()==sco::Result::Ok);
    std::printf("ASOP coordinator to synthetic generation model: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
