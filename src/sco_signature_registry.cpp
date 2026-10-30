#include "sco/signature_registry.h"
#include <array>
#include <atomic>
#include <limits>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sco {
namespace {
struct BudgetExceeded : std::bad_alloc {};
class Budget final : public std::pmr::memory_resource {
public:
    explicit Budget(size_t limit) : limit_(limit) {}
    size_t used() const noexcept { return used_.load(std::memory_order_relaxed); }
    size_t limit() const noexcept { return limit_; }
private:
    void* do_allocate(size_t bytes, size_t alignment) override {
        size_t old = used_.load(std::memory_order_relaxed);
        for (;;) {
            if (bytes > std::numeric_limits<size_t>::max() - old || (limit_ && bytes > limit_ - old))
                throw BudgetExceeded();
            if (used_.compare_exchange_weak(old, old + bytes, std::memory_order_relaxed)) break;
        }
        try { return std::pmr::new_delete_resource()->allocate(bytes, alignment); }
        catch (...) { used_.fetch_sub(bytes, std::memory_order_relaxed); throw; }
    }
    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
        used_.fetch_sub(bytes, std::memory_order_relaxed);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    const size_t limit_;
    std::atomic<size_t> used_{0};
};
template<class T> struct BudgetAllocator {
    using value_type = T;
    std::shared_ptr<Budget> budget;
    explicit BudgetAllocator(std::shared_ptr<Budget> b) noexcept : budget(std::move(b)) {}
    template<class U> BudgetAllocator(const BudgetAllocator<U>& a) noexcept : budget(a.budget) {}
    T* allocate(size_t n) {
        if (n > std::numeric_limits<size_t>::max() / sizeof(T)) throw std::bad_array_new_length();
        return static_cast<T*>(budget->allocate(n * sizeof(T), alignof(T)));
    }
    void deallocate(T* p, size_t n) noexcept { budget->deallocate(p, n * sizeof(T), alignof(T)); }
    template<class U> bool operator==(const BudgetAllocator<U>& a) const noexcept { return budget == a.budget; }
};
struct Hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};
struct Equal {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};
constexpr size_t unknown = std::numeric_limits<size_t>::max();
struct Definition {
    explicit Definition(std::pmr::memory_resource* r) : id(r), pattern(r), needs(r) {}
    Definition(Definition&&) noexcept = default;
    Definition& operator=(Definition&&) noexcept = default;
    Definition(const Definition&) = delete;
    Definition& operator=(const Definition&) = delete;
    std::pmr::string id, pattern;
    bool hasPattern = false;
    int ripDisp = 0, ripSize = 0;
    SigResolver resolve = nullptr;
    std::pmr::vector<std::pmr::string> needs;
    std::array<size_t, kMaxSigNeeds> deps{};
};
struct Graph {
    explicit Graph(std::shared_ptr<Budget> b) : budget(std::move(b)), rows(budget.get()), index(budget.get()) {}
    // Keep the resource alive until all of its containers have been destroyed.
    std::shared_ptr<Budget> budget;
    std::pmr::vector<Definition> rows;
    std::pmr::unordered_map<std::pmr::string, size_t, Hash, Equal> index;
    bool frozen = false;
};
}

struct SignatureSnapshot::Impl {
    explicit Impl(std::shared_ptr<const Graph> g)
        : graph(std::move(g)), results(graph->budget.get()), reasons(graph->budget.get()) {
        results.resize(graph->rows.size());
        reasons.reserve(graph->rows.size());
        for (size_t i = 0; i < graph->rows.size(); ++i) reasons.emplace_back();
    }
    void Set(size_t i, SigResult r) {
        if (r.why) { reasons[i] = r.why; r.why = reasons[i].c_str(); }
        results[i] = r;
    }
    std::shared_ptr<const Graph> graph;
    std::pmr::vector<SigResult> results;
    std::pmr::vector<std::pmr::string> reasons;
};
struct SignatureRegistry::Impl {
    explicit Impl(size_t bytes) : budget(std::make_shared<Budget>(bytes)),
        graph(std::allocate_shared<Graph>(BudgetAllocator<Graph>(budget), budget)) {}
    std::shared_ptr<Budget> budget;
    std::shared_ptr<Graph> graph;
    std::atomic<std::shared_ptr<const SignatureSnapshot>> published;
    bool busy = false;
    bool hostSealed = false;
};

SignatureSnapshot::SignatureSnapshot(Token, std::shared_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
size_t SignatureSnapshot::Count() const noexcept { return impl_->results.size(); }
const char* SignatureSnapshot::IdAt(size_t i) const noexcept {
    return i < impl_->graph->rows.size() ? impl_->graph->rows[i].id.c_str() : nullptr;
}
const SigResult* SignatureSnapshot::ResultAt(size_t i) const noexcept {
    return i < impl_->results.size() ? &impl_->results[i] : nullptr;
}
const SigResult* SignatureSnapshot::Lookup(const char* id) const noexcept {
    if (!id) return nullptr;
    const auto it = impl_->graph->index.find(id);
    return it == impl_->graph->index.end() ? nullptr : &impl_->results[it->second];
}
uint8_t* SigResolveContext::Sig(const char* id) const noexcept { return lookup_(state_, id); }

const char* SigRegistryStatusName(SigRegistryStatus s) noexcept {
    switch (s) {
    case SigRegistryStatus::Ok: return "OK";
    case SigRegistryStatus::InvalidArgument: return "INVALID_ARGUMENT";
    case SigRegistryStatus::DuplicateId: return "DUPLICATE_ID";
    case SigRegistryStatus::Frozen: return "FROZEN";
    case SigRegistryStatus::NotFrozen: return "NOT_FROZEN";
    case SigRegistryStatus::OutOfMemory: return "OUT_OF_MEMORY";
    case SigRegistryStatus::BudgetExceeded: return "BUDGET_EXCEEDED";
    case SigRegistryStatus::TooLarge: return "TOO_LARGE";
    case SigRegistryStatus::ResolverException: return "RESOLVER_EXCEPTION";
    case SigRegistryStatus::Busy: return "BUSY";
    case SigRegistryStatus::NotResolved: return "NOT_RESOLVED";
    case SigRegistryStatus::HostSealed: return "HOST_SEALED";
    }
    return "?";
}
SignatureRegistry::SignatureRegistry(size_t budgetBytes) : impl_(std::make_unique<Impl>(budgetBytes)) {}
SignatureRegistry::~SignatureRegistry() = default;
size_t SignatureRegistry::UsedBytes() const noexcept { return impl_->budget->used(); }
size_t SignatureRegistry::BudgetBytes() const noexcept { return impl_->budget->limit(); }

SigRegistrationResult SignatureRegistry::RegisterTables(const SignatureTable* tables, size_t count) noexcept {
    if (impl_->busy) return {SigRegistryStatus::Busy, 0, 0};
    if (impl_->graph->frozen) return {SigRegistryStatus::Frozen, 0, 0};
    if (!tables && count) return {SigRegistryStatus::InvalidArgument, 0, 0};
    size_t t = 0, r = 0;
    auto& graph = *impl_->graph;
    size_t committed = graph.rows.size();
    try {
        std::pmr::vector<Definition> staged(graph.budget.get());
        std::pmr::unordered_set<std::pmr::string, Hash, Equal> stagedIds(graph.budget.get());
        for (t = 0; t < count; ++t) {
            if (!tables[t].rows && tables[t].count) return {SigRegistryStatus::InvalidArgument, t, 0};
            for (r = 0; r < tables[t].count; ++r) {
                const auto& def = tables[t].rows[r];
                if (!def.id) return {SigRegistryStatus::InvalidArgument, t, r};
                if (graph.index.find(def.id) != graph.index.end() || stagedIds.find(def.id) != stagedIds.end())
                    return {SigRegistryStatus::DuplicateId, t, r};
                Definition row(graph.budget.get());
                row.id = def.id;
                row.hasPattern = def.pattern != nullptr;
                if (def.pattern) row.pattern = def.pattern;
                row.ripDisp = def.ripDisp; row.ripSize = def.ripSize; row.resolve = def.resolve;
                for (const char* need : def.needs) { if (!need) break; row.needs.emplace_back(need); }
                stagedIds.emplace(row.id);
                staged.push_back(std::move(row));
            }
        }
        t = r = kSignatureNoIndex;
        if (staged.size() > graph.rows.max_size() - committed) return {SigRegistryStatus::TooLarge, t, r};
        graph.rows.reserve(committed + staged.size());
        graph.index.reserve(committed + staged.size());
        for (auto& row : staged) {
            auto [it, inserted] = graph.index.emplace(row.id, graph.rows.size());
            if (!inserted) throw std::logic_error("duplicate after staging");
            try { graph.rows.push_back(std::move(row)); }
            catch (...) { graph.index.erase(it); throw; }
        }
        return {SigRegistryStatus::Ok, 0, 0};
    } catch (...) {
        // Logical contents are atomic even if a container kept extra capacity.
        while (graph.rows.size() > committed) {
            graph.index.erase(graph.rows.back().id);
            graph.rows.pop_back();
        }
        try { throw; }
        catch (const BudgetExceeded&) { return {SigRegistryStatus::BudgetExceeded, t, r}; }
        catch (const std::bad_alloc&) { return {SigRegistryStatus::OutOfMemory, t, r}; }
        catch (const std::length_error&) { return {SigRegistryStatus::TooLarge, t, r}; }
        catch (...) { return {SigRegistryStatus::InvalidArgument, t, r}; }
    }
}

SigRegistryStatus SignatureRegistry::Freeze() noexcept {
    if (impl_->busy) return SigRegistryStatus::Busy;
    auto& graph = *impl_->graph;
    if (graph.frozen) return SigRegistryStatus::Frozen;
    for (auto& row : graph.rows) {
        for (size_t i = 0; i < row.needs.size(); ++i) {
            const auto it = graph.index.find(row.needs[i]);
            row.deps[i] = it == graph.index.end() ? unknown : it->second;
        }
    }
    graph.frozen = true;
    return SigRegistryStatus::Ok;
}

SigRegistryStatus SignatureRegistry::Resolve(const Image& image) noexcept {
    if (impl_->busy) return SigRegistryStatus::Busy;
    auto graph = impl_->graph;
    if (!graph->frozen) return SigRegistryStatus::NotFrozen;
    if (impl_->hostSealed) return SigRegistryStatus::HostSealed;
    struct BusyGuard { bool& busy; explicit BusyGuard(bool& b) : busy(b) { busy = true; } ~BusyGuard() { busy = false; } } guard(impl_->busy);
    try {
        auto result = std::allocate_shared<SignatureSnapshot::Impl>(BudgetAllocator<SignatureSnapshot::Impl>(impl_->budget), graph);
        struct State { const Graph* graph; SignatureSnapshot::Impl* result; } state{graph.get(), result.get()};
        auto lookup = +[](const void* p, const char* id) noexcept -> uint8_t* {
            if (!id) return nullptr;
            const auto& s = *static_cast<const State*>(p);
            const auto it = s.graph->index.find(id);
            if (it == s.graph->index.end()) return nullptr;
            const auto& r = s.result->results[it->second];
            return r.state == SigState::Ok ? r.at : nullptr;
        };
        SigResolveContext ctx(&state, lookup);
        struct Frame { size_t row, next; };
        std::pmr::vector<Frame> stack(impl_->budget.get());
        std::pmr::vector<uint8_t> visiting(impl_->budget.get());
        visiting.resize(graph->rows.size(), 0);
        for (size_t root = 0; root < graph->rows.size(); ++root) {
            if (result->results[root].state != SigState::NotRun) continue;
            visiting[root] = 1; stack.push_back({root, 0});
            while (!stack.empty()) {
                const size_t i = stack.back().row;
                const auto& row = graph->rows[i];
                if (stack.back().next < row.needs.size()) {
                    size_t dep = row.deps[stack.back().next];
                    if (dep == unknown) {
                        result->Set(i, SigFail("needs an unknown signature"));
                        visiting[i] = 0; stack.pop_back(); continue;
                    }
                    if (result->results[dep].state == SigState::NotRun) {
                        if (visiting[dep]) result->Set(dep, SigFail("circular dependency"));
                        else { visiting[dep] = 1; stack.push_back({dep, 0}); continue; }
                    }
                    if (result->results[dep].state != SigState::Ok) {
                        result->Set(i, {SigState::Blocked, nullptr, 0, graph->rows[dep].id.c_str()});
                        visiting[i] = 0; stack.pop_back(); continue;
                    }
                    ++stack.back().next;
                    continue;
                }
                SigResult r;
                if (row.resolve) r = row.resolve(image, ctx);
                else if (row.hasPattern) {
                    r = SigPattern(image.text, row.pattern.c_str());
                    if (r.state == SigState::Ok && row.ripDisp > 0)
                        r.at = RipTarget(r.at, static_cast<size_t>(row.ripDisp), static_cast<size_t>(row.ripSize));
                } else r = SigFail("row has neither a pattern nor a resolver");
                if (r.state == SigState::Ok && !r.at) r = SigFail("resolver returned no address");
                result->Set(i, r);
                visiting[i] = 0; stack.pop_back();
            }
        }
        auto snapshot = std::allocate_shared<SignatureSnapshot>(BudgetAllocator<SignatureSnapshot>(impl_->budget), SignatureSnapshot::Token{}, std::move(result));
        std::shared_ptr<const SignatureSnapshot> published = std::move(snapshot);
        impl_->published.store(std::move(published), std::memory_order_release);
        return SigRegistryStatus::Ok;
    } catch (const BudgetExceeded&) { return SigRegistryStatus::BudgetExceeded; }
    catch (const std::bad_alloc&) { return SigRegistryStatus::OutOfMemory; }
    catch (const std::length_error&) { return SigRegistryStatus::TooLarge; }
    catch (...) { return SigRegistryStatus::ResolverException; }
}
SigRegistryStatus SignatureRegistry::SealForHost() noexcept {
    if (impl_->busy) return SigRegistryStatus::Busy;
    if (!impl_->graph->frozen) return SigRegistryStatus::NotFrozen;
    if (!impl_->published.load(std::memory_order_acquire)) return SigRegistryStatus::NotResolved;
    impl_->hostSealed = true;
    return SigRegistryStatus::Ok;
}
std::shared_ptr<const SignatureSnapshot> SignatureRegistry::Snapshot() const noexcept {
    return impl_->published.load(std::memory_order_acquire);
}
} // namespace sco
