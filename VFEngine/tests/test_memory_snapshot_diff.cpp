#include <doctest.h>

#include <memory/MemorySnapshotCapture.hpp>

#include <string>
#include <vector>

using memory::CapturedCpuCat;
using memory::CategoryKind;
using memory::DeltaKind;
using memory::DiffRow;
using memory::GpuHeapEntry;
using memory::MemorySnapshotCapture;
using memory::VramAssetCategory;
using memory::VramAssetRow;

namespace
{
    template <class Row>
    const DiffRow<Row>* findByKey(const std::vector<DiffRow<Row>>& rows, const std::string& key)
    {
        for (const auto& r : rows)
            if (r.key == key)
                return &r;
        return nullptr;
    }
}

TEST_CASE("memory snapshot diff — per-asset added/removed/changed by key")
{
    MemorySnapshotCapture before;
    before.vramAssets = {
        {"tex/keep.vfImage", VramAssetCategory::Texture, 1000},   // unchanged
        {"tex/grow.vfImage", VramAssetCategory::Texture, 500},    // grows
        {"mesh/gone.vfMesh", VramAssetCategory::Mesh, 2000},      // removed
    };

    MemorySnapshotCapture after;
    after.vramAssets = {
        {"tex/keep.vfImage", VramAssetCategory::Texture, 1000},   // unchanged
        {"tex/grow.vfImage", VramAssetCategory::Texture, 1500},   // +1000
        {"tex/new.vfImage", VramAssetCategory::Texture, 4096},    // added (leak signal)
    };

    const memory::MemorySnapshotDiff d = memory::diff(before, after);

    // after has 3 rows (keep/grow/new) + 1 removed (gone) = 4 delta rows
    CHECK(d.assetDeltas.size() == 4);

    const auto* keep = findByKey(d.assetDeltas, "tex/keep.vfImage");
    REQUIRE(keep != nullptr);
    CHECK(keep->kind == DeltaKind::Unchanged);
    CHECK(keep->byteDelta == 0);

    const auto* grow = findByKey(d.assetDeltas, "tex/grow.vfImage");
    REQUIRE(grow != nullptr);
    CHECK(grow->kind == DeltaKind::Changed);
    CHECK(grow->byteDelta == 1000);
    CHECK(grow->before.bytes == 500);
    CHECK(grow->after.bytes == 1500);

    const auto* added = findByKey(d.assetDeltas, "tex/new.vfImage");
    REQUIRE(added != nullptr);
    CHECK(added->kind == DeltaKind::Added);
    CHECK(added->byteDelta == 4096);
    CHECK(added->before.bytes == 0);            // default-constructed absent side
    CHECK(added->after.category == VramAssetCategory::Texture);

    const auto* removed = findByKey(d.assetDeltas, "mesh/gone.vfMesh");
    REQUIRE(removed != nullptr);
    CHECK(removed->kind == DeltaKind::Removed);
    CHECK(removed->byteDelta == -2000);
    CHECK(removed->before.bytes == 2000);
    CHECK(removed->after.bytes == 0);           // default-constructed absent side
}

TEST_CASE("memory snapshot diff — cpu categories and gpu heaps")
{
    MemorySnapshotCapture before;
    before.cpuCategories = {
        {"Transient", CategoryKind::Transient, 500, 900},
        {"Staging", CategoryKind::Staging, 64, 64},
    };
    before.gpuHeaps = {
        {"Device-Local", 2000, 4000},
        {"Host-Visible", 100, 500},
    };

    MemorySnapshotCapture after;
    after.cpuCategories = {
        {"Transient", CategoryKind::Transient, 1200, 1200}, // +700
        {"Staging", CategoryKind::Staging, 64, 64},         // unchanged
    };
    after.gpuHeaps = {
        {"Device-Local", 3500, 4000}, // +1500
        // Host-Visible removed
    };

    const memory::MemorySnapshotDiff d = memory::diff(before, after);

    const auto* transient = findByKey(d.cpuDeltas, "Transient");
    REQUIRE(transient != nullptr);
    CHECK(transient->kind == DeltaKind::Changed);
    CHECK(transient->byteDelta == 700);

    const auto* staging = findByKey(d.cpuDeltas, "Staging");
    REQUIRE(staging != nullptr);
    CHECK(staging->kind == DeltaKind::Unchanged);

    const auto* device = findByKey(d.heapDeltas, "Device-Local");
    REQUIRE(device != nullptr);
    CHECK(device->kind == DeltaKind::Changed);
    CHECK(device->byteDelta == 1500);

    const auto* host = findByKey(d.heapDeltas, "Host-Visible");
    REQUIRE(host != nullptr);
    CHECK(host->kind == DeltaKind::Removed);
    CHECK(host->byteDelta == -100);
}

TEST_CASE("memory snapshot diff — totals and empty captures")
{
    MemorySnapshotCapture before;
    before.cpuTotalTrackedBytes = 1000;
    before.vramUsageBytes = 8000;

    MemorySnapshotCapture after;
    after.cpuTotalTrackedBytes = 1750;
    after.vramUsageBytes = 6000;
    after.vramAssets = {
        {"tex/a.vfImage", VramAssetCategory::Texture, 10},
        {"tex/b.vfImage", VramAssetCategory::Texture, 20},
    };

    SUBCASE("scalar totals delta signed correctly")
    {
        const memory::MemorySnapshotDiff d = memory::diff(before, after);
        CHECK(d.cpuTotalDelta == 750);
        CHECK(d.vramUsageDelta == -2000); // freed memory is a negative delta
    }

    SUBCASE("empty-before → everything added")
    {
        const memory::MemorySnapshotDiff d = memory::diff(before, after);
        REQUIRE(d.assetDeltas.size() == 2);
        for (const auto& row : d.assetDeltas)
            CHECK(row.kind == DeltaKind::Added);
    }

    SUBCASE("empty-after → everything removed")
    {
        const memory::MemorySnapshotDiff d = memory::diff(after, before); // swap: after's assets vanish
        REQUIRE(d.assetDeltas.size() == 2);
        for (const auto& row : d.assetDeltas)
            CHECK(row.kind == DeltaKind::Removed);
    }
}
