// SPDX-License-Identifier: GPL-3.0-only
#include "simulation.h"
#include <algorithm>
#include <limits>
#include <utility>
namespace sco::game::asop_selection::simulation {
namespace {
constexpr size_t kMaxClasses=2048,kMaxHistory=8192,kMaxPins=4096,kMaxOperations=4096;
std::string Fold(const std::string& s) {
    std::string out=s;for(char& c:out)if(c>='A' && c<='Z')c=static_cast<char>(c+'a'-'A');return out;
}
bool Valid(const std::string& s) {
    if(s.empty() || s.size()>63)return false;
    for(char c:s)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return true;
}
}
Model::Model(size_t generation_budget):budget_(generation_budget<=kMaxGenerationBudget?generation_budget:0) {}
Status Model::SetPolicy(Policy policy,bool admitted) {
    if(stopping_ || stopped_)return Status::Unavailable;
    if(policy!=Policy::Off && policy!=Policy::Game && policy!=Policy::Ships)return Status::BadArg;
    policy_=policy;admitted_=admitted;return Status::Ok;
}
Status Model::Request(uint64_t revision,const std::vector<std::string>& classes) {
    if(stopping_ || stopped_)return Status::Unavailable;
    if(!revision || classes.size()>kMaxClasses)return Status::BadArg;
    if(revision<=requested_revision_)return Status::Stale;
    try {
        std::map<std::string,bool> seen;
        for(const auto& name:classes)if(!Valid(name) || !seen.emplace(Fold(name),true).second)return Status::BadArg;
        auto copy=classes;requested_=std::move(copy);requested_revision_=revision;staged_.clear();staged_known_=false;staged_revision_=0;
        return Status::Ok;
    } catch(...) {return Status::Failed;}
}
Status Model::Stage(const std::vector<std::string>& catalogue,bool fail_preparation) {
    if(stopping_ || stopped_ || !requested_revision_)return Status::Unavailable;
    staged_.clear();staged_revision_=0;staged_known_=false;
    if(fail_preparation)return Status::Failed;
    try {
        std::map<std::string,bool> available;
        if(catalogue.size()>kMaxHistory)return Status::TooMany;
        for(const auto& name:catalogue){if(!Valid(name))return Status::BadArg;available.emplace(Fold(name),true);if(available.size()>kMaxClasses)return Status::TooMany;}
        auto ids=identities_;uint64_t next=next_id_;std::vector<Row> rows;rows.reserve(requested_.size());
        for(const auto& name:requested_) {
            auto key=Fold(name);if(!available.count(key))return Status::NotFound;
            auto it=ids.find(key);
            if(it==ids.end()) {
                if(ids.size()>=kMaxHistory || next==std::numeric_limits<uint64_t>::max())return Status::TooMany;
                it=ids.emplace(std::move(key),++next).first;
            }
            rows.push_back({it->second,name});
        }
        identities_=std::move(ids);next_id_=next;staged_=std::move(rows);staged_revision_=requested_revision_;staged_known_=true;
        return Status::Ok;
    } catch(...) {return Status::Failed;}
}
bool Model::Pinned(uint64_t id) const {for(const auto& p:pins_)if(p.second==id)return true;return false;}
const Model::Generation* Model::FindGeneration(uint64_t id) const {
    for(const auto& g:generations_)if(g.id==id)return &g;
    return nullptr;
}
void Model::Collect() {
    generations_.erase(std::remove_if(generations_.begin(),generations_.end(),[&](const Generation& g){return g.id!=current_ && !Pinned(g.id);}),generations_.end());
}
Status Model::Publish() {
    if(stopping_ || stopped_ || policy_!=Policy::Ships || !admitted_ || !staged_known_)return Status::Unavailable;
    if(staged_revision_!=requested_revision_)return Status::Stale;
    if(next_generation_==std::numeric_limits<uint64_t>::max())return Status::TooMany;
    size_t retained=0;for(const auto& g:generations_)if(Pinned(g.id))++retained;
    if(retained>=budget_)return Status::TooMany;
    try {
        Generation next;next.id=next_generation_+1;next.revision=staged_revision_;next.rows=staged_;
        generations_.push_back(std::move(next));current_=++next_generation_;Collect();
        staged_known_=false;staged_revision_=0;staged_.clear();return Status::Ok;
    } catch(...) {return Status::Failed;}
}
Status Model::Observe(uint64_t generation,uint64_t scope) {
    if(stopping_ || stopped_ || policy_!=Policy::Ships || !admitted_)return Status::Unavailable;
    if(!scope)return Status::BadArg;
    if(!generation || generation!=current_)return Status::NotFound;
    const auto* g=FindGeneration(generation);if(!g)return Status::NotFound;
    observed_generation_=g->id;observed_revision_=g->revision;observed_scope_=scope;observed_count_=g->rows.size();return Status::Ok;
}
Status Model::Route(Status original,const std::vector<Row>& game,std::vector<Row>& out) const {
    if(stopping_ || stopped_)return Status::Unavailable;
    if(policy_==Policy::Game || policy_==Policy::Off) {
        if(original!=Status::Ok)return original;
        try{out=game;return Status::Ok;}catch(...){return Status::Failed;}
    }
    if(!admitted_)return Status::Unavailable;
    const auto* g=FindGeneration(current_);if(!g)return Status::Unavailable;
    try{out=g->rows;return Status::Ok;}catch(...){return Status::Failed;}
}
Status Model::PinCurrent(uint64_t& pin) {
    if(stopping_ || stopped_ || policy_!=Policy::Ships || !admitted_ || !FindGeneration(current_))return Status::Unavailable;
    if(pins_.size()>=kMaxPins || next_pin_==std::numeric_limits<uint64_t>::max())return Status::TooMany;
    try{pins_.emplace(next_pin_+1,current_);pin=++next_pin_;return Status::Ok;}catch(...){return Status::Failed;}
}
Status Model::Inspect(uint64_t pin,std::vector<Row>& out) const {
    auto it=pins_.find(pin);if(it==pins_.end())return Status::NotFound;
    const auto* g=FindGeneration(it->second);if(!g)return Status::NotFound;
    try{out=g->rows;return Status::Ok;}catch(...){return Status::Failed;}
}
Status Model::Fence(uint64_t pin) {
    if(!pins_.erase(pin))return Status::NotFound;
    Collect();return Status::Ok;
}
Status Model::Begin(uint64_t class_id,uint64_t& operation) {
    if(stopping_ || stopped_ || policy_!=Policy::Ships || !admitted_)return Status::Unavailable;
    const auto* g=FindGeneration(current_);if(!g)return Status::Unavailable;
    auto it=std::find_if(g->rows.begin(),g->rows.end(),[&](const Row& row){return row.id==class_id;});
    if(it==g->rows.end())return Status::NotFound;
    if(operations_.size()>=kMaxOperations || next_operation_==std::numeric_limits<uint64_t>::max())return Status::TooMany;
    try{operations_.emplace(next_operation_+1,it->name);operation=++next_operation_;return Status::Ok;}catch(...){return Status::Failed;}
}
Status Model::Historical(uint64_t operation,std::string& out) const {
    auto it=operations_.find(operation);if(it==operations_.end())return Status::NotFound;
    try{out=it->second;return Status::Ok;}catch(...){return Status::Failed;}
}
Status Model::Finish(uint64_t operation) {return operations_.erase(operation)?Status::Ok:Status::NotFound;}
Status Model::Stop() {
    if(stopped_)return Status::Ok;
    stopping_=true;admitted_=false;staged_.clear();staged_known_=false;staged_revision_=0;
    if(!pins_.empty() || !operations_.empty())return Status::Busy;
    generations_.clear();current_=0;identities_.clear();stopped_=true;return Status::Ok;
}
Snapshot Model::State() const {
    Snapshot out;out.requested_known=requested_revision_!=0;out.requested_revision=requested_revision_;out.requested_count=requested_.size();
    out.policy=policy_;out.admitted=admitted_;out.stopping=stopping_;out.stopped=stopped_;
    out.staged_known=staged_known_;out.staged_revision=staged_revision_;out.staged_count=staged_.size();
    if(const auto* g=FindGeneration(current_)){out.published_known=true;out.published_generation=g->id;out.published_revision=g->revision;out.published_count=g->rows.size();}
    out.custom_routed=out.published_known && policy_==Policy::Ships && admitted_ && !stopping_ && !stopped_;
    out.observed_known=observed_generation_!=0;out.observed_generation=observed_generation_;out.observed_revision=observed_revision_;out.observed_scope=observed_scope_;out.observed_count=observed_count_;
    out.retained_generations=generations_.size();out.pins=pins_.size();out.operations=operations_.size();return out;
}
}
