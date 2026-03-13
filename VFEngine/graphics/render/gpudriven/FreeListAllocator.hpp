#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <mutex>

namespace render::gpudriven {

    class FreeListAllocator {
    public:
        static constexpr uint32_t ALLOCATION_FAILED = UINT32_MAX;

        FreeListAllocator() = default;

        void reset(uint32_t maxCapacity) {
            std::lock_guard<std::mutex> lock(mtx);
            capacity = maxCapacity;
            usedCount = 0;
            reservedCount = 0;
            freeList.clear();
        }

        uint32_t allocate(uint32_t count) {
            if (count == 0) return 0;
            std::lock_guard<std::mutex> lock(mtx);

            // First try to find a free block that fits
            for (auto it = freeList.begin(); it != freeList.end(); ++it) {
                if (it->size >= count) {
                    uint32_t offset = it->offset;
                    if (it->size == count) {
                        freeList.erase(it);
                    } else {
                        it->offset += count;
                        it->size -= count;
                    }
                    return offset;
                }
            }

            // No free block found, allocate from end if space available
            if (usedCount + reservedCount + count <= capacity) {
                uint32_t offset = usedCount + reservedCount;
                reservedCount += count;
                return offset;
            }

            return ALLOCATION_FAILED;
        }

        void free(uint32_t offset, uint32_t count) {
            if (count == 0) return;
            std::lock_guard<std::mutex> lock(mtx);
            freeList.push_back({offset, count});
            defragment();
        }

        void markUsed(uint32_t count) {
            std::lock_guard<std::mutex> lock(mtx);
            if (reservedCount >= count) {
                reservedCount -= count;
                usedCount += count;
            }
        }

        uint32_t getUsedCount() const { std::lock_guard<std::mutex> lock(mtx); return usedCount; }
        uint32_t getReservedCount() const { std::lock_guard<std::mutex> lock(mtx); return reservedCount; }
        uint32_t getCapacity() const { std::lock_guard<std::mutex> lock(mtx); return capacity; }

        uint32_t getFreeBlockCount() const { std::lock_guard<std::mutex> lock(mtx); return static_cast<uint32_t>(freeList.size()); }

        float getFragmentationPercent() const {
            std::lock_guard<std::mutex> lock(mtx);
            if (freeList.empty()) return 0.0f;
            uint32_t totalFree = 0;
            uint32_t largestFree = 0;
            for (const auto& block : freeList) {
                totalFree += block.size;
                if (block.size > largestFree) largestFree = block.size;
            }
            if (totalFree == 0) return 0.0f;
            // Fragmentation = 1 - (largest free block / total free space)
            return (1.0f - static_cast<float>(largestFree) / static_cast<float>(totalFree)) * 100.0f;
        }

    private:
        struct FreeBlock {
            uint32_t offset;
            uint32_t size;
        };

        std::vector<FreeBlock> freeList;
        mutable std::mutex mtx;
        uint32_t capacity = 0;
        uint32_t usedCount = 0;
        uint32_t reservedCount = 0;

        void defragment() {
            if (freeList.size() < 2) return;

            std::sort(freeList.begin(), freeList.end(),
                [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

            std::vector<FreeBlock> merged;
            merged.push_back(freeList[0]);

            for (size_t i = 1; i < freeList.size(); ++i) {
                FreeBlock& last = merged.back();
                const FreeBlock& current = freeList[i];

                if (last.offset + last.size == current.offset) {
                    last.size += current.size;
                } else {
                    merged.push_back(current);
                }
            }

            freeList = std::move(merged);
        }
    };

}
