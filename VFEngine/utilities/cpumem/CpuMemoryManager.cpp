#include "CpuMemoryManager.hpp"
#include "../print/Log.hpp"
#include <algorithm>

namespace memory
{
    CpuMemoryManager& CpuMemoryManager::instance()
    {
        static CpuMemoryManager mgr;
        return mgr;
    }

    CategoryId CpuMemoryManager::registerCategory(std::string_view name, CategoryKind kind)
    {
        if (name.empty())
        {
            vfLogError("CpuMemoryManager: registerCategory called with an empty name");
            return kInvalidCategory;
        }

        std::lock_guard<std::mutex> lock(categoryMutex_);
        std::string key(name);
        auto it = nameToId_.find(key);
        if (it != nameToId_.end())
        {
            Category& existing = categories_[it->second];
            if (existing.kind != kind)
            {
                vfLogWarning("CpuMemoryManager: category '{}' re-registered with a different kind; keeping the original",
                             key);
            }
            return it->second; // idempotent (AC2)
        }

        uint32_t idx = categoryCount_.load(std::memory_order_relaxed);
        if (idx >= kMaxCategories)
        {
            vfLogError("CpuMemoryManager: category limit ({}) reached; '{}' not registered", kMaxCategories, key);
            return kInvalidCategory;
        }

        Category& c = categories_[idx];
        c.name = key;
        c.kind = kind;
        nameToId_.emplace(std::move(key), idx);
        // Release so a reader that acquires categoryCount_ sees the name/kind writes.
        categoryCount_.store(idx + 1, std::memory_order_release);
        return idx;
    }

    CategoryId CpuMemoryManager::resolve(std::string_view name)
    {
        return registerCategory(name, CategoryKind::Other);
    }

    void CpuMemoryManager::bumpPeak(Category& c, int64_t newValue)
    {
        int64_t prev = c.peak.load(std::memory_order_relaxed);
        while (newValue > prev && !c.peak.compare_exchange_weak(prev, newValue, std::memory_order_relaxed))
        {
            // prev reloaded by compare_exchange_weak on failure
        }
    }

    void CpuMemoryManager::addUsage(CategoryId id, uint64_t bytes)
    {
        if (id >= categoryCount_.load(std::memory_order_acquire) || bytes == 0)
            return;

        const int64_t delta = static_cast<int64_t>(bytes);
        Category& c = categories_[id];
        int64_t newVal = c.bytes.fetch_add(delta, std::memory_order_relaxed) + delta;
        runningTotalBytes_.fetch_add(delta, std::memory_order_relaxed);
        bumpPeak(c, newVal);
    }

    void CpuMemoryManager::subUsage(CategoryId id, uint64_t bytes)
    {
        if (id >= categoryCount_.load(std::memory_order_acquire) || bytes == 0)
            return;

        const int64_t want = static_cast<int64_t>(bytes);
        Category& c = categories_[id];
        int64_t cur = c.bytes.load(std::memory_order_relaxed);
        int64_t applied = 0;
        int64_t desired;
        do
        {
            applied = std::min(cur, want); // clamp: never drop below 0
            desired = cur - applied;
        } while (!c.bytes.compare_exchange_weak(cur, desired, std::memory_order_relaxed));

        runningTotalBytes_.fetch_sub(applied, std::memory_order_relaxed);
    }

    void CpuMemoryManager::setUsage(CategoryId id, uint64_t bytes)
    {
        if (id >= categoryCount_.load(std::memory_order_acquire))
            return;

        const int64_t newVal = static_cast<int64_t>(bytes);
        Category& c = categories_[id];
        int64_t old = c.bytes.exchange(newVal, std::memory_order_relaxed);
        runningTotalBytes_.fetch_add(newVal - old, std::memory_order_relaxed);
        bumpPeak(c, newVal);
    }

    uint64_t CpuMemoryManager::usage(CategoryId id) const
    {
        if (id >= categoryCount_.load(std::memory_order_acquire))
            return 0;
        int64_t v = categories_[id].bytes.load(std::memory_order_relaxed);
        return v > 0 ? static_cast<uint64_t>(v) : 0;
    }

    uint64_t CpuMemoryManager::totalTrackedBytes() const
    {
        int64_t v = runningTotalBytes_.load(std::memory_order_relaxed);
        return v > 0 ? static_cast<uint64_t>(v) : 0;
    }

    void CpuMemoryManager::setBudget(uint64_t bytes)
    {
        budgetBytes_.store(bytes, std::memory_order_relaxed);
    }

    uint64_t CpuMemoryManager::budget() const
    {
        return budgetBytes_.load(std::memory_order_relaxed);
    }

    bool CpuMemoryManager::reserve(uint64_t requestId, uint64_t estBytes, uint64_t projectedExternalBytes)
    {
        std::lock_guard<std::mutex> lock(reservationMutex_);
        const uint64_t b = budgetBytes_.load(std::memory_order_relaxed);
        if (b > 0)
        {
            int64_t reservedNow = reservedBytes_.load(std::memory_order_relaxed);
            uint64_t reserved = reservedNow > 0 ? static_cast<uint64_t>(reservedNow) : 0;
            if (projectedExternalBytes + reserved + estBytes > b)
                return false; // gate closed — caller defers (AC10)
        }

        auto [it, inserted] = reservations_.try_emplace(requestId, estBytes);
        if (inserted)
            reservedBytes_.fetch_add(static_cast<int64_t>(estBytes), std::memory_order_relaxed);
        return true;
    }

    void CpuMemoryManager::reserveUnconditional(uint64_t requestId, uint64_t estBytes)
    {
        std::lock_guard<std::mutex> lock(reservationMutex_);
        auto [it, inserted] = reservations_.try_emplace(requestId, estBytes);
        if (inserted)
            reservedBytes_.fetch_add(static_cast<int64_t>(estBytes), std::memory_order_relaxed);
    }

    void CpuMemoryManager::releaseReservation(uint64_t requestId) noexcept
    {
        std::lock_guard<std::mutex> lock(reservationMutex_);
        auto it = reservations_.find(requestId);
        if (it == reservations_.end())
            return;
        reservedBytes_.fetch_sub(static_cast<int64_t>(it->second), std::memory_order_relaxed);
        reservations_.erase(it);
    }

    uint64_t CpuMemoryManager::outstandingReservations() const
    {
        int64_t v = reservedBytes_.load(std::memory_order_relaxed);
        return v > 0 ? static_cast<uint64_t>(v) : 0;
    }

    void CpuMemoryManager::setGateState(GateState state, uint32_t deferredLoadCount)
    {
        gateState_.store(static_cast<uint8_t>(state), std::memory_order_relaxed);
        deferredLoadCount_.store(deferredLoadCount, std::memory_order_relaxed);
    }

    CpuMemorySnapshotData CpuMemoryManager::snapshot() const
    {
        CpuMemorySnapshotData data;
        {
            std::lock_guard<std::mutex> lock(categoryMutex_);
            uint32_t count = categoryCount_.load(std::memory_order_acquire);
            data.categories.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const Category& c = categories_[i];
                int64_t b = c.bytes.load(std::memory_order_relaxed);
                int64_t p = c.peak.load(std::memory_order_relaxed);
                CategoryView view;
                view.name = c.name;
                view.kind = c.kind;
                view.bytes = b > 0 ? static_cast<uint64_t>(b) : 0;
                view.peak = p > 0 ? static_cast<uint64_t>(p) : 0;
                data.categories.push_back(std::move(view));
            }
        }
        data.totalTrackedBytes = totalTrackedBytes();
        data.totalReservedBytes = outstandingReservations();
        data.budgetBytes = budget();
        data.gateState = static_cast<GateState>(gateState_.load(std::memory_order_relaxed));
        data.deferredLoadCount = deferredLoadCount_.load(std::memory_order_relaxed);
        return data;
    }
}
