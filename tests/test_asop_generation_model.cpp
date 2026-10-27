// SPDX-License-Identifier: GPL-3.0-only
// Synthetic model only: no engine, native pointers, hooks, filesystem, or live publication.
#include "../src/game/asop_selection/simulation.h"
#include <cstdio>
#include <string>
#include <vector>
namespace M=sco::game::asop_selection::simulation;
static int passed=0,failed=0;
#define CHECK(x) do {if(x)++passed;else {++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main() {
    M::Model model(3);
    std::vector<M::Row> result={{77,"GameClass"}};
    CHECK(model.SetPolicy(M::Policy::Game,true)==M::Status::Ok);
    CHECK(model.Route(M::Status::Ok,result,result)==M::Status::Ok && result.size()==1 && result[0].id==77);
    CHECK(model.Route(M::Status::Failed,{},result)==M::Status::Failed && result.size()==1 && result[0].id==77);
    CHECK(model.Request(1,{"A","a"})==M::Status::BadArg && !model.State().requested_known);
    CHECK(model.Request(1,{std::string(64,'X')})==M::Status::BadArg && !model.State().requested_known);
    CHECK(model.Request(1,std::vector<std::string>(2049,"X"))==M::Status::BadArg);
    CHECK(model.Request(1,{"A","B"})==M::Status::Ok);
    CHECK(model.Stage({"A","B","C"})==M::Status::Ok);
    auto state=model.State();
    CHECK(state.requested_known && state.staged_known && !state.published_known && !state.observed_known);
    CHECK(model.Publish()==M::Status::Unavailable);
    CHECK(model.Stage({"A","B","C"},true)==M::Status::Failed && !model.State().staged_known);
    CHECK(model.Stage({"A","B","C"})==M::Status::Ok);
    CHECK(model.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(model.Publish()==M::Status::Ok);
    state=model.State();CHECK(state.published_known && !state.observed_known && state.published_revision==1 && state.published_count==2);
    uint64_t pin1=0,opA=0,denied=9;CHECK(model.PinCurrent(pin1)==M::Status::Ok);
    std::vector<M::Row> rows;CHECK(model.Inspect(pin1,rows)==M::Status::Ok && rows.size()==2);
    const uint64_t idA=rows[0].id,idB=rows[1].id;
    CHECK(idA && idB && idA!=idB && model.Begin(idA,opA)==M::Status::Ok);
    CHECK(model.Observe(state.published_generation,0)==M::Status::BadArg && !model.State().observed_known);
    CHECK(model.Observe(state.published_generation,42)==M::Status::Ok && model.State().observed_revision==1 && model.State().observed_scope==42);
    CHECK(model.Request(2,{"B"})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::Ok && model.Publish()==M::Status::Ok);
    CHECK(model.Request(2,{"C"})==M::Status::Stale && model.State().published_revision==2);
    CHECK(model.SetPolicy(M::Policy::Game,false)==M::Status::Ok);
    CHECK(model.State().published_known && !model.State().custom_routed && model.State().policy==M::Policy::Game);
    std::vector<M::Row> game={{999,"Original"}},routed;
    CHECK(model.Route(M::Status::Ok,game,routed)==M::Status::Ok && routed.size()==1 && routed[0].id==999);
    CHECK(model.Begin(idB,denied)==M::Status::Unavailable);
    CHECK(model.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(model.State().custom_routed);
    CHECK(model.SetPolicy(M::Policy::Ships,false)==M::Status::Ok && !model.State().custom_routed);
    CHECK(model.Route(M::Status::Ok,game,routed)==M::Status::Unavailable && routed.size()==1 && routed[0].id==999);
    CHECK(model.PinCurrent(denied)==M::Status::Unavailable && denied==9);
    CHECK(model.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(model.Begin(idA,denied)==M::Status::NotFound && denied==9);
    std::string historical;CHECK(model.Historical(opA,historical)==M::Status::Ok && historical=="A");
    CHECK(model.Request(3,{"C","B"})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::Ok && model.Publish()==M::Status::Ok);
    CHECK(model.Request(4,{"B","a"})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::Ok && model.Publish()==M::Status::Ok);
    uint64_t pin4=0;CHECK(model.PinCurrent(pin4)==M::Status::Ok && model.Inspect(pin4,rows)==M::Status::Ok);
    CHECK(rows.size()==2 && rows[0].id==idB && rows[1].id==idA && rows[1].name=="a");
    CHECK(model.Inspect(pin1,rows)==M::Status::Ok && rows.size()==2 && rows[0].id==idA && rows[1].id==idB);
    CHECK(model.Request(5,{"Unknown"})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::NotFound);
    state=model.State();CHECK(state.requested_revision==5 && state.published_revision==4 && state.observed_revision==1 && !state.staged_known);
    CHECK(model.Request(6,{})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::Ok && model.Publish()==M::Status::Ok);
    uint64_t pin6=0;CHECK(model.PinCurrent(pin6)==M::Status::Ok && model.Inspect(pin6,rows)==M::Status::Ok && rows.empty());
    CHECK(model.Request(7,{"C"})==M::Status::Ok && model.Stage({"A","B","C"})==M::Status::Ok);
    CHECK(model.Publish()==M::Status::TooMany && model.State().published_revision==6);
    CHECK(model.Fence(pin4)==M::Status::Ok && model.Publish()==M::Status::Ok);
    CHECK(model.Inspect(pin1,rows)==M::Status::Ok && rows[0].id==idA);
    CHECK(model.Observe(999,43)==M::Status::NotFound && model.State().observed_revision==1 && model.State().observed_scope==42);
    CHECK(model.Stop()==M::Status::Busy && model.Inspect(pin1,rows)==M::Status::Ok);
    CHECK(model.State().stopping && !model.State().custom_routed && model.Begin(idB,denied)==M::Status::Unavailable);
    CHECK(model.Fence(pin1)==M::Status::Ok && model.Fence(pin6)==M::Status::Ok);
    CHECK(model.Stop()==M::Status::Busy && model.Historical(opA,historical)==M::Status::Ok);
    CHECK(model.Finish(opA)==M::Status::Ok && model.Stop()==M::Status::Ok);
    CHECK(model.State().stopped && !model.State().published_known && model.PinCurrent(pin1)==M::Status::Unavailable);
    M::Model bounded(2);std::vector<std::string> many;
    for(int i=0;i<2048;++i)many.push_back("Ship_"+std::to_string(i));
    CHECK(bounded.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(bounded.Request(1,many)==M::Status::Ok && bounded.Stage(many)==M::Status::Ok && bounded.Publish()==M::Status::Ok);
    CHECK(bounded.State().published_count==2048);
    CHECK(bounded.Request(2,{std::string("bad\0name",8)})==M::Status::BadArg && bounded.State().published_revision==1);
    many.push_back("Extra");CHECK(bounded.Stage(many)==M::Status::TooMany && bounded.State().published_revision==1);
    CHECK(bounded.Stop()==M::Status::Ok);
    M::Model invalid_budget(M::kMaxGenerationBudget+1);
    CHECK(invalid_budget.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(invalid_budget.Request(1,{"A"})==M::Status::Ok && invalid_budget.Stage({"A"})==M::Status::Ok);
    CHECK(invalid_budget.Publish()==M::Status::TooMany && !invalid_budget.State().published_known);
    CHECK(invalid_budget.Stop()==M::Status::Ok);
    M::Model superseded(2);
    CHECK(superseded.SetPolicy(M::Policy::Ships,true)==M::Status::Ok);
    CHECK(superseded.Request(1,{"A"})==M::Status::Ok && superseded.Stage({"A","B"})==M::Status::Ok);
    CHECK(superseded.Request(2,{"B"})==M::Status::Ok && !superseded.State().staged_known);
    CHECK(superseded.Publish()==M::Status::Unavailable && !superseded.State().published_known);
    CHECK(superseded.Stage({"A","B"})==M::Status::Ok && superseded.Publish()==M::Status::Ok);
    CHECK(superseded.State().published_revision==2 && superseded.Stop()==M::Status::Ok);
    std::printf("ASOP generation simulation: %d passed, %d failed\n",passed,failed);return failed?1:0;
}
