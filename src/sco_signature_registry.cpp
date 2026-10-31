#include "sco/signature_registry.h"
#include <array>
#include <atomic>
#include <limits>
#include <memory_resource>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstring>
#include <type_traits>
#include <utility>

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
// These storage primitives deliberately have allocation-free constructors and
// moves. MSVC's checked STL containers allocate iterator proxies even in noexcept
// constructors/moves; a rejecting budget resource must never reach those paths.
class Text {
public:
    explicit Text(Budget* budget) noexcept : budget_(budget) {}
    Text(Budget* budget, const char* text) : Text(budget) { *this = text; }
    Text(Text&& other) noexcept : budget_(other.budget_), data_(std::exchange(other.data_, nullptr)), bytes_(std::exchange(other.bytes_, 0)) {}
    Text& operator=(Text&& other) noexcept {
        if (this != &other) { clear(); budget_ = other.budget_; data_ = std::exchange(other.data_, nullptr); bytes_ = std::exchange(other.bytes_, 0); }
        return *this;
    }
    ~Text() { clear(); }
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;
    Text& operator=(const char* text) {
        const size_t length = std::strlen(text);
        if (length == std::numeric_limits<size_t>::max()) throw std::length_error("signature text");
        const size_t bytes = length + 1;
        auto* copy = static_cast<char*>(budget_->allocate(bytes, alignof(char)));
        std::memcpy(copy, text, bytes);
        clear(); data_ = copy; bytes_ = bytes;
        return *this;
    }
    const char* c_str() const noexcept { return data_ ? data_ : ""; }
    operator std::string_view() const noexcept { return {c_str(), bytes_ ? bytes_ - 1 : 0}; }
private:
    void clear() noexcept { if (data_) budget_->deallocate(data_, bytes_, alignof(char)); data_ = nullptr; bytes_ = 0; }
    Budget* budget_;
    char* data_ = nullptr;
    size_t bytes_ = 0;
};
template<class T> class Storage {
    static_assert(std::is_nothrow_move_constructible_v<T>);
public:
    explicit Storage(Budget* budget) noexcept : budget_(budget) {}
    ~Storage() { while (size_) pop_back(); if (data_) budget_->deallocate(data_, capacity_ * sizeof(T), alignof(T)); }
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&& other) noexcept : budget_(other.budget_), data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)), capacity_(std::exchange(other.capacity_, 0)) {}
    Storage& operator=(Storage&&) = delete;
    size_t size() const noexcept { return size_; }
    size_t max_size() const noexcept { return std::numeric_limits<size_t>::max() / sizeof(T); }
    bool empty() const noexcept { return size_ == 0; }
    T& operator[](size_t i) noexcept { return data_[i]; }
    const T& operator[](size_t i) const noexcept { return data_[i]; }
    T& back() noexcept { return data_[size_ - 1]; }
    T* begin() noexcept { return data_; }
    T* end() noexcept { return size_ ? data_ + size_ : data_; }
    void reserve(size_t n) {
        if (n <= capacity_) return;
        if (n > max_size()) throw std::length_error("signature storage");
        auto* next = static_cast<T*>(budget_->allocate(n * sizeof(T), alignof(T)));
        for (size_t i = 0; i < size_; ++i) { new (next + i) T(std::move(data_[i])); data_[i].~T(); }
        if (data_) budget_->deallocate(data_, capacity_ * sizeof(T), alignof(T));
        data_ = next; capacity_ = n;
    }
    template<class... Args> void emplace_back(Args&&... args) {
        if (size_ == capacity_) {
            if (size_ == max_size()) throw std::length_error("signature storage");
            reserve(capacity_ > max_size() / 2 ? max_size() : (capacity_ ? capacity_ * 2 : 1));
        }
        new (data_ + size_) T(std::forward<Args>(args)...); ++size_;
    }
    void push_back(T value) { emplace_back(std::move(value)); }
    void pop_back() noexcept { data_[--size_].~T(); }
    void resize(size_t n) { reserve(n); while (size_ < n) emplace_back(); while (size_ > n) pop_back(); }
    void swap(Storage& other) noexcept { std::swap(data_, other.data_); std::swap(size_, other.size_); std::swap(capacity_, other.capacity_); }
private:
    Budget* budget_;
    T* data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;
};
constexpr size_t unknown = std::numeric_limits<size_t>::max();
class Index {
    struct Entry { std::string_view key; size_t value = unknown; bool occupied = false; };
public:
    explicit Index(Budget* budget) noexcept : budget_(budget), entries_(budget) {}
    size_t find(std::string_view key) const noexcept {
        if (entries_.empty()) return unknown;
        size_t slot = std::hash<std::string_view>{}(key) & (entries_.size() - 1);
        for (size_t n = 0; n < entries_.size(); ++n) {
            const auto& e = entries_[slot];
            if (!e.occupied) return unknown;
            if (e.value != unknown && e.key == key) return e.value;
            slot = (slot + 1) & (entries_.size() - 1);
        }
        return unknown;
    }
    void reserve(size_t count) {
        size_t n = 8;
        while (count > n / 2) { if (n > entries_.max_size() / 2) throw std::length_error("signature index"); n *= 2; }
        if (n <= entries_.size()) return;
        Storage<Entry> next(budget_); next.resize(n);
        entries_.swap(next);
        for (auto& e : next) if (e.value != unknown) insert(e.key, e.value);
    }
    void insert(std::string_view key, size_t value) noexcept {
        size_t slot = std::hash<std::string_view>{}(key) & (entries_.size() - 1);
        while (entries_[slot].value != unknown) slot = (slot + 1) & (entries_.size() - 1);
        entries_[slot] = {key, value, true};
    }
    void erase(std::string_view key) noexcept {
        size_t slot = std::hash<std::string_view>{}(key) & (entries_.size() - 1);
        while (entries_[slot].occupied) {
            auto& e = entries_[slot];
            if (e.value != unknown && e.key == key) { e.key = {}; e.value = unknown; return; }
            slot = (slot + 1) & (entries_.size() - 1);
        }
    }
private:
    Budget* budget_;
    Storage<Entry> entries_;
};
struct Definition {
    explicit Definition(Budget* r) : id(r), pattern(r), needs(r) {}
    Definition(Definition&&) noexcept = default;
    Definition& operator=(Definition&&) = delete;
    Definition(const Definition&) = delete;
    Definition& operator=(const Definition&) = delete;
    Text id, pattern;
    bool hasPattern = false;
    int ripDisp = 0, ripSize = 0;
    SigResolver resolve = nullptr;
    Storage<Text> needs;
    std::array<size_t, kMaxSigNeeds> deps{};
};
struct Graph {
    explicit Graph(std::shared_ptr<Budget> b) : budget(std::move(b)), rows(budget.get()), index(budget.get()) {}
    // Keep the resource alive until all of its containers have been destroyed.
    std::shared_ptr<Budget> budget;
    Storage<Definition> rows;
    Index index;
    bool frozen = false;
};
}

struct SignatureSnapshot::Impl {
    explicit Impl(std::shared_ptr<const Graph> g)
        : graph(std::move(g)), results(graph->budget.get()), reasons(graph->budget.get()) {
        results.resize(graph->rows.size());
        reasons.reserve(graph->rows.size());
        for (size_t i = 0; i < graph->rows.size(); ++i) reasons.emplace_back(graph->budget.get());
    }
    void Set(size_t i, SigResult r) {
        if (r.why) { reasons[i] = r.why; r.why = reasons[i].c_str(); }
        results[i] = r;
    }
    std::shared_ptr<const Graph> graph;
    std::shared_ptr<const void> imageOwner, resolverOwner;
    Storage<SigResult> results;
    Storage<Text> reasons;
};
struct SignatureRegistry::Impl {
    explicit Impl(size_t bytes) : budget(std::make_shared<Budget>(bytes)),
        graph(std::allocate_shared<Graph>(BudgetAllocator<Graph>(budget), budget)) {}
    std::shared_ptr<Budget> budget;
    std::shared_ptr<Graph> graph;
    mutable std::mutex publishedMutex;
    std::shared_ptr<const SignatureSnapshot> published;
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
    return it == unknown ? nullptr : &impl_->results[it];
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
        Storage<Definition> staged(graph.budget.get());
        Index stagedIds(graph.budget.get());
        for (t = 0; t < count; ++t) {
            if (!tables[t].rows && tables[t].count) return {SigRegistryStatus::InvalidArgument, t, 0};
            for (r = 0; r < tables[t].count; ++r) {
                const auto& def = tables[t].rows[r];
                if (!def.id) return {SigRegistryStatus::InvalidArgument, t, r};
                if (graph.index.find(def.id) != unknown || stagedIds.find(def.id) != unknown)
                    return {SigRegistryStatus::DuplicateId, t, r};
                Definition row(graph.budget.get());
                row.id = def.id;
                row.hasPattern = def.pattern != nullptr;
                if (def.pattern) row.pattern = def.pattern;
                row.ripDisp = def.ripDisp; row.ripSize = def.ripSize; row.resolve = def.resolve;
                for (const char* need : def.needs) { if (!need) break; row.needs.emplace_back(graph.budget.get(), need); }
                stagedIds.reserve(staged.size() + 1);
                stagedIds.insert(row.id, staged.size());
                staged.push_back(std::move(row));
            }
        }
        t = r = kSignatureNoIndex;
        if (staged.size() > graph.rows.max_size() - committed) return {SigRegistryStatus::TooLarge, t, r};
        graph.rows.reserve(committed + staged.size());
        graph.index.reserve(committed + staged.size());
        for (auto& row : staged) {
            graph.index.insert(row.id, graph.rows.size());
            graph.rows.push_back(std::move(row));
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
            row.deps[i] = it;
        }
    }
    graph.frozen = true;
    return SigRegistryStatus::Ok;
}

SigRegistryStatus SignatureRegistry::Resolve(const Image& image) noexcept {
    return ResolveImpl(image, {}, {});
}
SigRegistryStatus SignatureRegistry::Resolve(const Image& image, std::shared_ptr<const void> imageOwner,
                                              std::shared_ptr<const void> resolverOwner) noexcept {
    if (!imageOwner || !resolverOwner || !imageOwner.use_count() || !resolverOwner.use_count())
        return SigRegistryStatus::InvalidArgument;
    return ResolveImpl(image, std::move(imageOwner), std::move(resolverOwner));
}
SigRegistryStatus SignatureRegistry::ResolveImpl(const Image& image, std::shared_ptr<const void> imageOwner,
                                                  std::shared_ptr<const void> resolverOwner) noexcept {
    if (impl_->busy) return SigRegistryStatus::Busy;
    auto graph = impl_->graph;
    if (!graph->frozen) return SigRegistryStatus::NotFrozen;
    if (impl_->hostSealed) return SigRegistryStatus::HostSealed;
    struct BusyGuard { bool& busy; explicit BusyGuard(bool& b) : busy(b) { busy = true; } ~BusyGuard() { busy = false; } } guard(impl_->busy);
    try {
        auto result = std::allocate_shared<SignatureSnapshot::Impl>(BudgetAllocator<SignatureSnapshot::Impl>(impl_->budget), graph);
        result->imageOwner = std::move(imageOwner);
        result->resolverOwner = std::move(resolverOwner);
        struct State { const Graph* graph; SignatureSnapshot::Impl* result; } state{graph.get(), result.get()};
        auto lookup = +[](const void* p, const char* id) noexcept -> uint8_t* {
            if (!id) return nullptr;
            const auto& s = *static_cast<const State*>(p);
            const auto it = s.graph->index.find(id);
            if (it == unknown) return nullptr;
            const auto& r = s.result->results[it];
            return r.state == SigState::Ok ? r.at : nullptr;
        };
        SigResolveContext ctx(&state, lookup);
        // A NotRun callback may be attempted again by a later root or dependent, but
        // each individual parent must inspect the result after its child returns.
        struct Frame { size_t row, next; bool attempted; };
        Storage<Frame> stack(impl_->budget.get());
        Storage<uint8_t> visiting(impl_->budget.get());
        visiting.resize(graph->rows.size());
    const auto imageBase = reinterpret_cast<uintptr_t>(image.base);
    const auto sectionValid = [&](const Section& section) {
        if (!section.base && !section.size) return true;
        const auto at = reinterpret_cast<uintptr_t>(section.base);
        return imageBase && image.size && image.size <= UINTPTR_MAX - imageBase && at >= imageBase &&
            at - imageBase <= image.size && section.size <= image.size - (at - imageBase);
    };
    if (!sectionValid(image.text) || !sectionValid(image.rdata) || !sectionValid(image.pdata)) {
        for (size_t i = 0; i < graph->rows.size(); ++i) result->Set(i, SigFail("declared section outside the image"));
    } else {
        for (size_t root = 0; root < graph->rows.size(); ++root) {
            if (result->results[root].state != SigState::NotRun) continue;
            visiting[root] = 1; stack.push_back({root, 0, false});
            while (!stack.empty()) {
                const size_t i = stack.back().row;
                const auto& row = graph->rows[i];
                if (stack.back().next < row.needs.size()) {
                    size_t dep = row.deps[stack.back().next];
                    if (dep == unknown) {
                        result->Set(i, SigFail("needs an unknown signature"));
                        visiting[i] = 0; stack.pop_back(); continue;
                    }
                    if (result->results[dep].state == SigState::NotRun && !stack.back().attempted) {
                        if (visiting[dep]) result->Set(dep, SigFail("circular dependency"));
                        else {
                            stack.back().attempted = true;
                            visiting[dep] = 1; stack.push_back({dep, 0, false}); continue;
                        }
                    }
                    if (result->results[dep].state != SigState::Ok) {
                        result->Set(i, {SigState::Blocked, nullptr, 0, graph->rows[dep].id.c_str()});
                        visiting[i] = 0; stack.pop_back(); continue;
                    }
                    ++stack.back().next;
                    stack.back().attempted = false;
                    continue;
                }
                SigResult r;
                if (row.resolve) r = row.resolve(image, ctx);
                else if (row.hasPattern) {
                    r = SigPattern(image.text, row.pattern.c_str());
        if (r.state == SigState::Ok && row.ripDisp > 0) {
            const auto source = reinterpret_cast<uintptr_t>(r.at);
            const auto text = reinterpret_cast<uintptr_t>(image.text.base);
            const auto base = reinterpret_cast<uintptr_t>(image.base);
            const auto disp = static_cast<size_t>(row.ripDisp);
            const auto size = row.ripSize > 0 ? static_cast<size_t>(row.ripSize) : 0;
            if (!base || !image.size || image.size > UINTPTR_MAX - base) {
                r = SigFail("image range missing or invalid");
            } else if (!text || image.text.size > UINTPTR_MAX - text || source < text ||
                       source - text > image.text.size || size > image.text.size - (source - text)) {
                r = SigFail("RIP source instruction outside .text");
            } else if (disp > size || size - disp < sizeof(int32_t)) {
                r = SigFail("RIP displacement outside instruction");
            } else {
                const auto next = source + size;
                const int32_t delta = Rel32(r.at + disp);
                const auto magnitude = delta < 0 ? uintptr_t{0} - static_cast<uintptr_t>(delta) : static_cast<uintptr_t>(delta);
                const bool overflow = delta < 0 ? magnitude > next : magnitude > UINTPTR_MAX - next;
                const auto target = delta < 0 ? next - magnitude : next + magnitude;
                if (overflow || target < base || target - base >= image.size)
                    r = SigFail("RIP target outside the image");
                else r.at = reinterpret_cast<uint8_t*>(target);
            }
        }
                } else r = SigFail("row has neither a pattern nor a resolver");
                if (r.state == SigState::Ok && !r.at) r = SigFail("resolver returned no address");
                result->Set(i, r);
                visiting[i] = 0; stack.pop_back();
            }
        }
        }
        auto snapshot = std::allocate_shared<SignatureSnapshot>(BudgetAllocator<SignatureSnapshot>(impl_->budget), SignatureSnapshot::Token{}, std::move(result));
        std::shared_ptr<const SignatureSnapshot> published = std::move(snapshot);
        {
            std::lock_guard<std::mutex> lock(impl_->publishedMutex);
            impl_->published.swap(published);
        }
        return SigRegistryStatus::Ok;
    } catch (const BudgetExceeded&) { return SigRegistryStatus::BudgetExceeded; }
    catch (const std::bad_alloc&) { return SigRegistryStatus::OutOfMemory; }
    catch (const std::length_error&) { return SigRegistryStatus::TooLarge; }
    catch (...) { return SigRegistryStatus::ResolverException; }
}
SigRegistryStatus SignatureRegistry::SealForHost() noexcept {
    if (impl_->busy) return SigRegistryStatus::Busy;
    if (!impl_->graph->frozen) return SigRegistryStatus::NotFrozen;
    if (!Snapshot()) return SigRegistryStatus::NotResolved;
    impl_->hostSealed = true;
    return SigRegistryStatus::Ok;
}
std::shared_ptr<const SignatureSnapshot> SignatureRegistry::Snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->publishedMutex);
    return impl_->published;
}
} // namespace sco
