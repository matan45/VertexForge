#pragma once
#include "CpuMemoryExport.hpp"
#include "CpuMemorySnapshot.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace memory
{
    // Process-wide owner of named CPU RAM categories + the asset-load budget.
    //
    // Compiled into CpuMemory.dll so the singleton resolves to ONE instance
    // across every module (the Import/Audio DLLs, Graphics-in-the-exe, the
    // executables, the test runner). A header-only static-inline counterpart
    // (cf. GpuAllocationStats.hpp) would give each DLL its own copy, so the
    // recorders would never reach the gate. Mirrors the ECSRegistry / Threading
    // / AssetDB single-instance pattern.
    //
    // Relationship to resource::AssetLifecycleManager: that manager remains the
    // single source of truth for *decoded asset* bytes and does eviction. This
    // manager owns transient + staging categories and the in-flight reservations,
    // and the gate's budget. The gate denominator is AssetLifecycleManager's
    // decoded total + this manager's reservations (NOT totalTrackedBytes()).
    //
    // Threading: addUsage/subUsage/setUsage and the gate-denominator reads
    // (totalTrackedBytes / outstandingReservations / budget) are lock-free
    // atomics, so recorders on worker threads never nest a CpuMemory lock under
    // the staging-ring or scheduler mutex. Only registerCategory/snapshot (cold)
    // take categoryMutex_; reserve/releaseReservation take their own
    // reservationMutex_. The scheduler drives reserve/release from dispatchPending
    // (reached via submit() or update(), i.e. on whatever thread calls them) while
    // holding the scheduler mutex; reservationMutex_ makes them safe regardless of
    // caller thread, and it is never nested under the staging-ring lock.
#pragma warning(push)
#pragma warning(disable : 4251) // private STL/atomic members touched only inside the DLL
    class VF_CPUMEMORY_API CpuMemoryManager
    {
    public:
        // Default CPU RAM budget — the gate is ACTIVE by default (user requirement
        // "default ram start with 8g"). 8 GiB is large enough that it only ever
        // defers loads under genuine multi-GB pressure, so normal loads are
        // unaffected. The Editor overrides it from persisted settings; a value of
        // 0 disables the gate (pure advisory accounting).
        static constexpr uint64_t kDefaultBudgetBytes = 8ull * 1024 * 1024 * 1024; // 8 GiB

        static CpuMemoryManager& instance();

        CpuMemoryManager(const CpuMemoryManager&) = delete;
        CpuMemoryManager& operator=(const CpuMemoryManager&) = delete;

        // --- named-category registry (AC1, AC2, AC9) ---
        // Idempotent: a known name returns the same id. A differing kind keeps
        // the first kind and warns once. An empty name returns kInvalidCategory.
        CategoryId registerCategory(std::string_view name, CategoryKind kind = CategoryKind::Other);
        // Register-or-get convenience (kind=Other for unknown names).
        CategoryId resolve(std::string_view name);

        // --- usage accounting (AC3, AC4) — lock-free ---
        void addUsage(CategoryId id, uint64_t bytes);
        void subUsage(CategoryId id, uint64_t bytes); // clamps at 0
        void setUsage(CategoryId id, uint64_t bytes); // absolute (staging ring)
        uint64_t usage(CategoryId id) const;          // 0 for unknown id (never throws)
        uint64_t totalTrackedBytes() const;           // CpuMemory's own categories only

        // --- budget + pre-load gate (AC5, AC10) ---
        void setBudget(uint64_t bytes); // 0 = advisory/disabled
        uint64_t budget() const;
        // Records a reservation for requestId iff admitting estBytes would not
        // exceed budget. projectedExternalBytes is the authoritative decoded
        // total owned elsewhere (resource::AssetLifecycleManager). Returns false
        // (no reservation taken) => the caller must defer the load.
        bool reserve(uint64_t requestId, uint64_t estBytes, uint64_t projectedExternalBytes);
        // Always records (Critical loads bypass the gate — deadlock safety).
        void reserveUnconditional(uint64_t requestId, uint64_t estBytes);
        void releaseReservation(uint64_t requestId) noexcept;
        uint64_t outstandingReservations() const;

        // Published by the scheduler each tick so diagnostics show real state.
        void setGateState(GateState state, uint32_t deferredLoadCount);

        // --- diagnostics (AC8) ---
        CpuMemorySnapshotData snapshot() const;

    private:
        CpuMemoryManager() = default;

        static constexpr uint32_t kMaxCategories = 256;

        struct Category
        {
            std::atomic<int64_t> bytes{0};
            std::atomic<int64_t> peak{0};
            std::string name;                       // set once under categoryMutex_
            CategoryKind kind = CategoryKind::Other; // set once under categoryMutex_
        };

        static void bumpPeak(Category& c, int64_t newValue);

        std::array<Category, kMaxCategories> categories_;
        std::atomic<uint32_t> categoryCount_{0};
        std::unordered_map<std::string, CategoryId> nameToId_;
        mutable std::mutex categoryMutex_;

        std::atomic<int64_t> runningTotalBytes_{0}; // sum across categories (transient+staging)
        std::atomic<int64_t> reservedBytes_{0};
        std::atomic<uint64_t> budgetBytes_{kDefaultBudgetBytes};
        std::atomic<uint8_t> gateState_{static_cast<uint8_t>(GateState::Open)};
        std::atomic<uint32_t> deferredLoadCount_{0};

        std::unordered_map<uint64_t, uint64_t> reservations_; // requestId -> reserved bytes
        mutable std::mutex reservationMutex_;
    };
#pragma warning(pop)
}
