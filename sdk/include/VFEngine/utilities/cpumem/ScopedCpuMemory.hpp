#pragma once
#include "CpuMemoryExport.hpp"
#include "CpuMemorySnapshot.hpp" // CategoryId

namespace memory
{
    // RAII guard for a transient CPU allocation: adds `bytes` to the category on
    // construction and subtracts the same amount on destruction, so the category's
    // current + peak usage rise and fall with the lifetime of the scratch buffer.
    // Move-only; the moved-from guard releases nothing.
    //
    // Usage (resolve the id once, reuse on the hot path):
    //   static const memory::CategoryId cat =
    //       memory::CpuMemoryManager::instance().registerCategory(
    //           memory::categories::ImportTextureDecode, memory::CategoryKind::Transient);
    //   memory::ScopedCpuMemory guard(cat, decodedBytes);
    struct VF_CPUMEMORY_API ScopedCpuMemory
    {
        ScopedCpuMemory(CategoryId id, uint64_t bytes);
        ~ScopedCpuMemory();

        ScopedCpuMemory(ScopedCpuMemory&& other) noexcept;

        ScopedCpuMemory(const ScopedCpuMemory&) = delete;
        ScopedCpuMemory& operator=(const ScopedCpuMemory&) = delete;
        ScopedCpuMemory& operator=(ScopedCpuMemory&&) = delete;

    private:
        CategoryId id_;
        uint64_t bytes_;
    };
}
