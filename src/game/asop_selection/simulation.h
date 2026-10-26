// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Single-threaded synthetic model. No native pointers, game calls or host publication.
// A real provider needs a proven downstream fence; this model's fence is only a test input.
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace sco::game::asop_selection::simulation {
inline constexpr size_t kMaxGenerationBudget=16;
enum class Status { Ok, BadArg, NotFound, Stale, Unavailable, TooMany, Busy, Failed };
enum class Policy { Off, Game, Ships };
struct Row { uint64_t id=0; std::string name; };
struct Snapshot {
    // All fields describe this mock only. Observed is the last explicit mock
    // acknowledgement and may refer to an older generation than published.
    bool requested_known=false,staged_known=false,published_known=false,observed_known=false;
    bool admitted=false,custom_routed=false,stopping=false,stopped=false;
    Policy policy=Policy::Off;
    uint64_t requested_revision=0,staged_revision=0,published_revision=0,published_generation=0,observed_revision=0,observed_generation=0,observed_scope=0;
    size_t requested_count=0,staged_count=0,published_count=0,observed_count=0,retained_generations=0,pins=0,operations=0;
};
class Model {
public:
    // An invalid or zero budget admits no publication.
    explicit Model(size_t generation_budget);
    Model(const Model&)=delete;
    Model& operator=(const Model&)=delete;
    Status SetPolicy(Policy policy,bool admitted);
    Status Request(uint64_t revision,const std::vector<std::string>& classes);
    Status Stage(const std::vector<std::string>& catalogue,bool fail_preparation=false);
    Status Publish();
    // A mock acknowledgement is scoped to one nonzero synthetic observer token.
    Status Observe(uint64_t generation,uint64_t scope);
    // Game/off returns the original status and copies rows only on original success.
    Status Route(Status original,const std::vector<Row>& game,std::vector<Row>& out) const;
    Status PinCurrent(uint64_t& pin);
    Status Inspect(uint64_t pin,std::vector<Row>& out) const;
    Status Fence(uint64_t pin);
    Status Begin(uint64_t class_id,uint64_t& operation);
    Status Historical(uint64_t operation,std::string& out) const;
    Status Finish(uint64_t operation);
    // Stop closes admission immediately and returns Busy while a synthetic downstream
    // fence or previously accepted operation remains. Owner must retain this object.
    Status Stop();
    Snapshot State() const;
private:
    struct Generation {uint64_t id=0,revision=0;std::vector<Row> rows;};
    void Collect();
    const Generation* FindGeneration(uint64_t id) const;
    bool Pinned(uint64_t id) const;
    size_t budget_;Policy policy_=Policy::Off;bool admitted_=false,stopping_=false,stopped_=false;
    uint64_t requested_revision_=0,staged_revision_=0,next_id_=0,next_generation_=0,next_pin_=0,next_operation_=0;
    uint64_t current_=0,observed_generation_=0,observed_revision_=0,observed_scope_=0;size_t observed_count_=0;
    std::vector<std::string> requested_;std::vector<Row> staged_;bool staged_known_=false;
    std::vector<Generation> generations_;std::map<uint64_t,uint64_t> pins_;
    std::map<uint64_t,std::string> operations_;std::map<std::string,uint64_t> identities_;
};
}
