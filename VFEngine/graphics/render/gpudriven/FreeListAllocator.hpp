#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>

namespace render::gpudriven {

    class FreeListAllocator {
    public:
        static constexpr uint32_t ALLOCATION_FAILED = UINT32_MAX;

        FreeListAllocator() = default;

        void reset(uint32_t maxCapacity) {
            capacity = maxCapacity;
            usedCount = 0;
            reservedCount = 0;
            freeList.clear();
        }

        uint32_t allocate(uint32_t count) {
            if (count == 0) return 0;

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
            freeList.push_back({offset, count});
            defragment();
        }

        void markUsed(uint32_t count) {
            if (reservedCount >= count) {
                reservedCount -= count;
                usedCount += count;
            }
        }

        uint32_t getUsedCount() const { return usedCount; }
        uint32_t getReservedCount() const { return reservedCount; }
        uint32_t getCapacity() const { return capacity; }

    private:
        struct FreeBlock {
            uint32_t offset;
            uint32_t size;
        };

        std::vector<FreeBlock> freeList;
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
