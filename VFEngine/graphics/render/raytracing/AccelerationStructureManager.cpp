#include "AccelerationStructureManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../gpudriven/scene/MergedMeshBuffer.hpp"
#include "print/Log.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::raytracing
{
    AccelerationStructureManager::AccelerationStructureManager(core::Device& device)
        : device(device)
    {
    }

    AccelerationStructureManager::~AccelerationStructureManager()
    {
        cleanup();
    }

    std::string AccelerationStructureManager::makeSubmeshKey(const std::string& meshPath,
                                                              const std::string& submeshName,
                                                              uint32_t submeshIndex)
    {
        return meshPath + "|" + submeshName + "|" + std::to_string(submeshIndex);
    }

    uint64_t AccelerationStructureManager::makeGeometryOffsetKey(uint32_t vertexOffset, uint32_t indexOffset)
    {
        return (static_cast<uint64_t>(vertexOffset) << 32) | static_cast<uint64_t>(indexOffset);
    }

    void AccelerationStructureManager::init()
    {
        if (initialized) return;

        if (!device.isRayQuerySupported())
        {
            vfLogWarning("AccelerationStructureManager: Ray query not supported, skipping init");
            return;
        }

        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
        vfLogInfo("AccelerationStructureManager: Initialized (per-submesh BLAS)");
    }

    void AccelerationStructureManager::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // GPU is idle — deferred deletion queue will be flushed by RenderManager

        // Destroy all mesh BLAS entries
        for (auto& [key, entry] : blasCache)
        {
            destroyBLASEntryImmediate(entry);
        }
        blasCache.clear();
        pendingBLASBuilds.clear();
        geometryOffsetToSubmeshKey.clear();

        // Destroy all terrain BLAS entries
        for (auto& [key, entry] : terrainBlasCache)
        {
            destroyBLASEntryImmediate(entry);
        }
        terrainBlasCache.clear();
        pendingTerrainBLASBuilds.clear();
        terrainOffsetToTileKey.clear();

        // Drop any in-flight compaction state (the source structures were just destroyed above).
        pendingCompactions.clear();
        freeCompactionSlots.clear();
        compactionFrameCounter = 0;
        forceNextTlasRebuild = false;
        if (compactionQueryPool)
        {
            vkDevice.destroyQueryPool(compactionQueryPool);
            compactionQueryPool = nullptr;
        }

        // Destroy TLAS
        if (tlas)
        {
            vkDevice.destroyAccelerationStructureKHR(tlas);
            tlas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, tlasBuffer, tlasAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceAllocation, device.getMemoryManager());

        // Destroy staging buffers
        for (auto& staging : instanceStagingBuffers)
        {
            core::BufferUtilities::destroyBuffer(vkDevice, staging.buffer, staging.allocation, device.getMemoryManager());
            staging.capacity = 0;
        }

        // Destroy scratch pool
        blasScratchPool.cleanup(device);

        // Destroy descriptor resources
        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (tlasDescriptorLayout)
        {
            vkDevice.destroyDescriptorSetLayout(tlasDescriptorLayout);
            tlasDescriptorLayout = nullptr;
        }

        memoryBudget = {};
        currentInstanceCount = 0;
        instanceBufferCapacity = 0;
        tlasScratchSize = 0;
        tlasBuilt = false;
        initialized = false;
    }

    void AccelerationStructureManager::destroyBLASEntry(BLASEntry& entry)
    {
        if (!entry.blas && !entry.buffer) return;

        // Decrement budget synchronously (safe — budget is an estimate, not GPU-critical)
        if (entry.buffer)
        {
            memoryBudget.blasTotalBytes -= entry.size;
            memoryBudget.blasCount--;
        }

        auto* dq = deletionQueue;
        if (!dq)
        {
            dq = core::RenderManager::getGlobalDeletionQueue();
            if (!dq)
                vfLogWarning("AccelerationStructureManager: No deletion queue available, using immediate destroy");
        }

        if (dq)
        {
            vk::AccelerationStructureKHR blas = entry.blas;
            vk::Buffer buffer = entry.buffer;
            core::VulkanAllocation allocation = entry.allocation;
            core::VulkanMemoryManager* memMgr = &device.getMemoryManager();

            dq->queueCustom([blas, buffer, allocation, memMgr](vk::Device dev) mutable {
                if (blas) dev.destroyAccelerationStructureKHR(blas);
                if (buffer) core::BufferUtilities::destroyBuffer(dev, buffer, allocation, *memMgr);
            });
        }
        else
        {
            destroyBLASEntryImmediate(entry);
        }

        entry.blas = nullptr;
        entry.buffer = nullptr;
        entry.allocation = {};
    }

    void AccelerationStructureManager::destroyBLASEntryImmediate(BLASEntry& entry)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        if (entry.blas)
        {
            vkDevice.destroyAccelerationStructureKHR(entry.blas);
            entry.blas = nullptr;
        }
        if (entry.buffer)
        {
            core::BufferUtilities::destroyBuffer(vkDevice, entry.buffer, entry.allocation, device.getMemoryManager());
            memoryBudget.blasTotalBytes -= entry.size;
            memoryBudget.blasCount--;
        }
    }

    void AccelerationStructureManager::deferTLASDestruction(vk::AccelerationStructureKHR oldTlas,
                                                             vk::Buffer oldBuffer, core::VulkanAllocation oldAlloc)
    {
        auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();
        if (dq)
        {
            core::VulkanMemoryManager* memMgr = &device.getMemoryManager();
            dq->queueCustom([oldTlas, oldBuffer, oldAlloc, memMgr](vk::Device dev) mutable {
                if (oldTlas) dev.destroyAccelerationStructureKHR(oldTlas);
                if (oldBuffer) core::BufferUtilities::destroyBuffer(dev, oldBuffer, oldAlloc, *memMgr);
            });
        }
        else
        {
            vk::Device vkDevice = device.getLogicalDevice();
            if (oldTlas) vkDevice.destroyAccelerationStructureKHR(oldTlas);
            if (oldBuffer) core::BufferUtilities::destroyBuffer(vkDevice, oldBuffer, oldAlloc, device.getMemoryManager());
        }
    }

    void AccelerationStructureManager::notifyMeshReady(const std::string& meshPath,
                                                        const std::string& submeshName,
                                                        uint32_t submeshIndex,
                                                        const gpudriven::SubmeshLocation& submeshLoc)
    {
        if (!initialized) return;

        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);

        std::lock_guard<std::mutex> lock(pendingMutex);

        // Skip if already built
        if (blasCache.count(key) && blasCache[key].deviceAddress != 0) return;

        const auto& lod0 = submeshLoc.lods[0];
        if (lod0.vertexCount == 0 || lod0.indexCount == 0) return;

        PendingBLAS pending;
        pending.key = key;
        pending.vertexOffset = lod0.vertexOffset;
        pending.vertexCount = lod0.vertexCount;
        pending.indexOffset = lod0.indexOffset;
        pending.indexCount = lod0.indexCount;
        pendingBLASBuilds.push_back(std::move(pending));

        // Register reverse mapping
        uint64_t offsetKey = makeGeometryOffsetKey(lod0.vertexOffset, lod0.indexOffset);
        geometryOffsetToSubmeshKey[offsetKey] = key;
    }

    void AccelerationStructureManager::notifyMeshRemoved(const std::string& meshPath,
                                                          const std::string& submeshName,
                                                          uint32_t submeshIndex)
    {
        if (!initialized) return;

        std::lock_guard<std::mutex> lock(pendingMutex);

        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = blasCache.find(key);
        if (it == blasCache.end()) return;

        // Remove reverse mapping
        uint64_t offsetKey = makeGeometryOffsetKey(it->second.lod0VertexOffset, it->second.lod0IndexOffset);
        geometryOffsetToSubmeshKey.erase(offsetKey);

        invalidatePendingCompaction(it->second.blas);
        destroyBLASEntry(it->second);
        blasCache.erase(it);

        // Also remove from pending builds
        pendingBLASBuilds.erase(
            std::remove_if(pendingBLASBuilds.begin(), pendingBLASBuilds.end(),
                [&key](const PendingBLAS& p) { return p.key == key; }),
            pendingBLASBuilds.end());
    }

    void AccelerationStructureManager::buildPendingBLAS(vk::CommandBuffer cmd,
                                                         vk::Buffer vertexBuffer, uint32_t vertexStride,
                                                         vk::Buffer indexBuffer)
    {
        if (!initialized) return;

        // Swap pending list under lock so streaming thread can continue queuing
        std::vector<PendingBLAS> localPending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingBLASBuilds.empty()) return;
            localPending = std::move(pendingBLASBuilds);
            pendingBLASBuilds.clear();
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Keys of BLAS actually (re)built this dispatch — queued for compaction after the
        // build-complete barrier below.
        std::vector<std::string> builtKeys;
        builtKeys.reserve(localPending.size());

        vk::DeviceAddress vertexBufferAddress = vkDevice.getBufferAddress({vertexBuffer});
        vk::DeviceAddress indexBufferAddress = vkDevice.getBufferAddress({indexBuffer});

        // Pre-calculate max scratch size to avoid reallocation during command recording (VK-1182)
        vk::DeviceSize maxScratchSize = 0;
        for (const auto& pending : localPending)
        {
            uint32_t triCount = pending.indexCount / 3;
            if (triCount == 0) continue;

            vk::AccelerationStructureGeometryTrianglesDataKHR triData{};
            triData.vertexFormat = vk::Format::eR32G32B32Sfloat;
            triData.vertexData.deviceAddress = vertexBufferAddress +
                static_cast<vk::DeviceSize>(pending.vertexOffset) * vertexStride;
            triData.vertexStride = vertexStride;
            triData.maxVertex = pending.vertexCount - 1;
            triData.indexType = vk::IndexType::eUint32;
            triData.indexData.deviceAddress = indexBufferAddress +
                static_cast<vk::DeviceSize>(pending.indexOffset) * sizeof(uint32_t);

            vk::AccelerationStructureGeometryKHR geom{};
            geom.geometryType = vk::GeometryTypeKHR::eTriangles;
            geom.geometry.triangles = triData;
            geom.flags = vk::GeometryFlagBitsKHR::eOpaque;

            vk::AccelerationStructureBuildGeometryInfoKHR bi{};
            bi.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            bi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                       vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            bi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            bi.geometryCount = 1;
            bi.pGeometries = &geom;

            vk::AccelerationStructureBuildSizesInfoKHR si{};
            vkDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, &bi, &triCount, &si);
            maxScratchSize = std::max(maxScratchSize, si.buildScratchSize);
        }
        if (maxScratchSize > 0)
            blasScratchPool.acquire(maxScratchSize, device);

        for (const auto& pending : localPending)
        {
            uint32_t triangleCount = pending.indexCount / 3;
            if (triangleCount == 0) continue;

            // Configure triangle geometry with offsets into merged buffers
            vk::AccelerationStructureGeometryTrianglesDataKHR triangleData{};
            triangleData.vertexFormat = vk::Format::eR32G32B32Sfloat;
            triangleData.vertexData.deviceAddress = vertexBufferAddress +
                static_cast<vk::DeviceSize>(pending.vertexOffset) * vertexStride;
            triangleData.vertexStride = vertexStride;
            triangleData.maxVertex = pending.vertexCount - 1;
            triangleData.indexType = vk::IndexType::eUint32;
            triangleData.indexData.deviceAddress = indexBufferAddress +
                static_cast<vk::DeviceSize>(pending.indexOffset) * sizeof(uint32_t);

            vk::AccelerationStructureGeometryKHR geometry{};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.geometry.triangles = triangleData;
            geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                              vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            // Query size requirements
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
            vkDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                &buildInfo, &triangleCount, &sizeInfo);

            // Destroy old entry if exists
            auto it = blasCache.find(pending.key);
            if (it != blasCache.end() && it->second.blas)
            {
                // A pending compaction may still reference this old BLAS — invalidate it
                // so processPendingCompactions doesn't double-free the structure we destroy here.
                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    invalidatePendingCompaction(it->second.blas);
                }
                destroyBLASEntry(it->second);
            }

            // Create BLAS buffer
            BLASEntry entry{};
            entry.size = sizeInfo.accelerationStructureSize;
            entry.lod0VertexOffset = pending.vertexOffset;
            entry.lod0IndexOffset = pending.indexOffset;
            entry.lod0VertexCount = pending.vertexCount;
            entry.lod0IndexCount = pending.indexCount;

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, entry.buffer, entry.allocation, device.getMemoryManager());
            }

            // Create acceleration structure
            vk::AccelerationStructureCreateInfoKHR createInfo{};
            createInfo.buffer = entry.buffer;
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            entry.blas = vkDevice.createAccelerationStructureKHR(createInfo);

            // Acquire scratch memory
            vk::DeviceAddress scratchAddress = blasScratchPool.acquire(sizeInfo.buildScratchSize, device);

            // Record build
            buildInfo.dstAccelerationStructure = entry.blas;
            buildInfo.scratchData.deviceAddress = scratchAddress;

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
            rangeInfo.primitiveCount = triangleCount;
            const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
            cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

            // Barrier between individual BLAS builds — required because they share scratch memory
            // (Vulkan spec: scratch memory must not be accessed by builds not separated by a barrier)
            {
                vk::MemoryBarrier buildBarrier{
                    vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                    vk::AccessFlagBits::eAccelerationStructureWriteKHR |
                    vk::AccessFlagBits::eAccelerationStructureReadKHR
                };
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                    vk::DependencyFlags{},
                    1, &buildBarrier, 0, nullptr, 0, nullptr);
            }

            // Get device address
            entry.deviceAddress = vkDevice.getAccelerationStructureAddressKHR({entry.blas});

            // Update budget
            memoryBudget.blasTotalBytes += entry.size;
            memoryBudget.blasCount++;

            blasCache[pending.key] = entry;
            builtKeys.push_back(pending.key);
        }

        // Single barrier after all BLAS builds
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        // Queue compaction-size queries for the BLAS just built (after the build-complete
        // barrier — writeAccelerationStructuresPropertiesKHR must observe finished builds).
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            for (const auto& key : builtKeys)
            {
                auto it = blasCache.find(key);
                if (it != blasCache.end() && it->second.blas)
                    queueBlasForCompaction(cmd, key, it->second, /*isTerrain*/ false);
            }
        }

        memoryBudget.scratchPeakBytes = blasScratchPool.getPeakSize();

        vfLogInfo("AccelerationStructureManager: Built {} BLAS entries (total: {})",
                  localPending.size(), memoryBudget.blasCount);
    }

    void AccelerationStructureManager::insertTLASCrossFrameBarrier(vk::CommandBuffer cmd)
    {
        // With core::MAX_FRAMES_IN_FLIGHT=2, the previous frame's RT shadow compute may still
        // be reading the TLAS via ray queries when the current frame rebuilds it.
        // This barrier ensures the previous frame's TLAS reads and builds complete before
        // we overwrite the shared instance buffer, TLAS, and scratch buffer.
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureReadKHR |   // RT shadow compute reads TLAS
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,   // previous TLAS build writes
            vk::AccessFlagBits::eTransferWrite |                   // instance buffer copy
            vk::AccessFlagBits::eAccelerationStructureWriteKHR |   // TLAS build
            vk::AccessFlagBits::eAccelerationStructureReadKHR      // TLAS build reads instance data
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eTransfer |
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);
    }

    // ============================================================
    // BLAS compaction (VK-1430)
    // ============================================================

    void AccelerationStructureManager::ensureCompactionQueryPool()
    {
        if (compactionQueryPool) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::QueryPoolCreateInfo poolInfo{};
        poolInfo.queryType = vk::QueryType::eAccelerationStructureCompactedSizeKHR;
        poolInfo.queryCount = kCompactionQueryCapacity;

        compactionQueryPool = vkDevice.createQueryPool(poolInfo);

        freeCompactionSlots.clear();
        freeCompactionSlots.reserve(kCompactionQueryCapacity);
        // Push in reverse so pop_back() hands out ascending indices first (cosmetic).
        for (uint32_t i = kCompactionQueryCapacity; i-- > 0;)
            freeCompactionSlots.push_back(i);
    }

    void AccelerationStructureManager::queueBlasForCompaction(vk::CommandBuffer cmd,
                                                              const std::string& key,
                                                              const BLASEntry& entry,
                                                              bool isTerrain)
    {
        // Caller holds pendingMutex.
        ensureCompactionQueryPool();

        if (freeCompactionSlots.empty())
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                vfLogWarning("AccelerationStructureManager: compaction query pool exhausted "
                             "({} slots), skipping compaction for some BLAS this frame",
                             kCompactionQueryCapacity);
            }
            return;
        }

        uint32_t slot = freeCompactionSlots.back();
        freeCompactionSlots.pop_back();

        cmd.resetQueryPool(compactionQueryPool, slot, 1);
        cmd.writeAccelerationStructuresPropertiesKHR(
            1, &entry.blas,
            vk::QueryType::eAccelerationStructureCompactedSizeKHR,
            compactionQueryPool, slot);

        PendingCompaction pc{};
        pc.key = key;
        pc.isTerrain = isTerrain;
        pc.stale = false;
        pc.srcAS = entry.blas;
        pc.srcBuffer = entry.buffer;
        pc.srcAlloc = entry.allocation;
        pc.srcSize = entry.size;
        pc.queryIndex = slot;
        pc.frameQueued = compactionFrameCounter;
        pendingCompactions.push_back(std::move(pc));
    }

    void AccelerationStructureManager::invalidatePendingCompaction(vk::AccelerationStructureKHR srcAS)
    {
        // Caller holds pendingMutex. The cache-destroy path owns the structure; mark any
        // matching pending record stale and null its handles so processPendingCompactions
        // only recycles the query slot.
        if (!srcAS) return;
        for (auto& pc : pendingCompactions)
        {
            if (pc.srcAS == srcAS)
            {
                pc.stale = true;
                pc.srcAS = nullptr;
                pc.srcBuffer = nullptr;
                pc.srcAlloc = {};
            }
        }
    }

    void AccelerationStructureManager::processPendingCompactions(vk::CommandBuffer cmd)
    {
        if (!initialized) return;

        std::lock_guard<std::mutex> lock(pendingMutex);

        ++compactionFrameCounter;

        if (pendingCompactions.empty()) return;

        vk::Device vkDevice = device.getLogicalDevice();

        bool anyCopied = false;
        uint32_t processed = 0;

        for (auto it = pendingCompactions.begin(); it != pendingCompactions.end(); )
        {
            if (processed >= kMaxCompactionsPerFrame)
                break;

            PendingCompaction& pc = *it;

            // Not yet safe to read the query (writes from frameQueued may still be in flight).
            if (pc.frameQueued + core::MAX_FRAMES_IN_FLIGHT > compactionFrameCounter)
            {
                ++it;
                continue;
            }

            ++processed;

            // Source was destroyed by an eviction — just recycle the slot.
            if (pc.stale)
            {
                freeCompactionSlots.push_back(pc.queryIndex);
                it = pendingCompactions.erase(it);
                continue;
            }

            // Read compacted size without waiting; if not ready, leave for a later frame.
            vk::DeviceSize compactedSize = 0;
            vk::Result res = vkDevice.getQueryPoolResults(
                compactionQueryPool, pc.queryIndex, 1,
                sizeof(vk::DeviceSize), &compactedSize, sizeof(vk::DeviceSize),
                vk::QueryResultFlagBits::e64);

            if (res == vk::Result::eNotReady)
            {
                --processed; // didn't actually consume a compaction budget slot
                ++it;
                continue;
            }

            // Bad/non-beneficial result — drop the record, keep the original BLAS.
            if (res != vk::Result::eSuccess || compactedSize == 0 || compactedSize >= pc.srcSize)
            {
                freeCompactionSlots.push_back(pc.queryIndex);
                it = pendingCompactions.erase(it);
                continue;
            }

            // Verify the cache still holds this key AND still points at the BLAS we queued
            // (it may have been evicted or rebuilt between queue and now — eviction would have
            // marked us stale, but a rebuild replaces the entry without touching this record).
            auto& cache = pc.isTerrain ? terrainBlasCache : blasCache;
            auto cacheIt = cache.find(pc.key);
            if (cacheIt == cache.end() || cacheIt->second.blas != pc.srcAS)
            {
                // The source is owned elsewhere now (or already freed). Recycle slot only.
                freeCompactionSlots.push_back(pc.queryIndex);
                it = pendingCompactions.erase(it);
                continue;
            }

            // Allocate the compacted acceleration structure (mirrors buildPending*BLAS).
            vk::Buffer newBuffer;
            core::VulkanAllocation newAlloc;
            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = compactedSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, newBuffer, newAlloc, device.getMemoryManager());
            }

            vk::AccelerationStructureCreateInfoKHR createInfo{};
            createInfo.buffer = newBuffer;
            createInfo.size = compactedSize;
            createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            vk::AccelerationStructureKHR newAS = vkDevice.createAccelerationStructureKHR(createInfo);

            vk::CopyAccelerationStructureInfoKHR copyInfo{};
            copyInfo.src = pc.srcAS;
            copyInfo.dst = newAS;
            copyInfo.mode = vk::CopyAccelerationStructureModeKHR::eCompact;
            cmd.copyAccelerationStructureKHR(copyInfo);

            vk::DeviceAddress newAddr = vkDevice.getAccelerationStructureAddressKHR({newAS});

            // Swap the compacted structure into the cache entry.
            cacheIt->second.blas = newAS;
            cacheIt->second.buffer = newBuffer;
            cacheIt->second.allocation = newAlloc;
            cacheIt->second.deviceAddress = newAddr;
            cacheIt->second.size = compactedSize;

            // Budget: replace original size with compacted size (count unchanged).
            memoryBudget.blasTotalBytes -= pc.srcSize;
            memoryBudget.blasTotalBytes += compactedSize;

            // Defer-free the original AS + buffer (mirror destroyBLASEntry).
            {
                auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();
                if (dq)
                {
                    vk::AccelerationStructureKHR oldAS = pc.srcAS;
                    vk::Buffer oldBuf = pc.srcBuffer;
                    core::VulkanAllocation oldAlloc = pc.srcAlloc;
                    core::VulkanMemoryManager* memMgr = &device.getMemoryManager();
                    dq->queueCustom([oldAS, oldBuf, oldAlloc, memMgr](vk::Device dev) mutable {
                        if (oldAS) dev.destroyAccelerationStructureKHR(oldAS);
                        if (oldBuf) core::BufferUtilities::destroyBuffer(dev, oldBuf, oldAlloc, *memMgr);
                    });
                }
                else
                {
                    if (pc.srcAS) vkDevice.destroyAccelerationStructureKHR(pc.srcAS);
                    if (pc.srcBuffer)
                        core::BufferUtilities::destroyBuffer(vkDevice, pc.srcBuffer, pc.srcAlloc, device.getMemoryManager());
                }
            }

            freeCompactionSlots.push_back(pc.queryIndex);
            it = pendingCompactions.erase(it);

            forceNextTlasRebuild = true;
            anyCopied = true;
        }

        // One barrier so the TLAS build that follows reads the compacted BLAS correctly.
        if (anyCopied)
        {
            vk::MemoryBarrier barrier{
                vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                vk::AccessFlagBits::eAccelerationStructureReadKHR
            };
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                vk::DependencyFlags{},
                1, &barrier, 0, nullptr, 0, nullptr);
        }
    }

    void AccelerationStructureManager::buildTLAS(vk::CommandBuffer cmd,
                                                  const std::vector<gpudriven::GPUObjectData>& objects,
                                                  uint32_t objectCount,
                                                  const gpudriven::MergedMeshBuffer& mergedBuffer)
    {
        if (!initialized || objectCount == 0 || blasCache.empty()) return;

        // Deferred deletions handled by DeferredDeletionQueue

        vk::Device vkDevice = device.getLogicalDevice();

        // Build instance array
        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        instances.reserve(objectCount);

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            const auto& obj = objects[i];

            // Skip transparent/additive objects - they don't cast RT shadows
            if (obj.flags & (gpudriven::ObjectFlags::Translucent | gpudriven::ObjectFlags::AdditiveBlend))
                continue;

            // Skip terrain tiles - they have their own BLAS path (VK-1151)
            if (obj.flags & gpudriven::ObjectFlags::TerrainTile)
                continue;

            // Look up BLAS for this object via its LOD 0 vertex/index offsets
            uint32_t vertexOffset = obj.lod0Data.x;
            uint32_t indexOffset = obj.lod0Data.y;
            uint64_t offsetKey = makeGeometryOffsetKey(vertexOffset, indexOffset);

            auto keyIt = geometryOffsetToSubmeshKey.find(offsetKey);
            if (keyIt == geometryOffsetToSubmeshKey.end()) continue;

            auto blasIt = blasCache.find(keyIt->second);
            if (blasIt == blasCache.end() || blasIt->second.deviceAddress == 0) continue;

            // Convert glm::mat4 to VkTransformMatrixKHR (3x4 row-major)
            const glm::mat4& m = obj.modelMatrix;
            vk::TransformMatrixKHR transform{};
            transform.matrix[0][0] = m[0][0]; transform.matrix[0][1] = m[1][0]; transform.matrix[0][2] = m[2][0]; transform.matrix[0][3] = m[3][0];
            transform.matrix[1][0] = m[0][1]; transform.matrix[1][1] = m[1][1]; transform.matrix[1][2] = m[2][1]; transform.matrix[1][3] = m[3][1];
            transform.matrix[2][0] = m[0][2]; transform.matrix[2][1] = m[1][2]; transform.matrix[2][2] = m[2][2]; transform.matrix[2][3] = m[3][2];

            vk::AccelerationStructureInstanceKHR inst{};
            inst.transform = transform;
            inst.instanceCustomIndex = i;
            inst.mask = 0xFF;
            inst.instanceShaderBindingTableRecordOffset = 0;
            inst.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            inst.accelerationStructureReference = blasIt->second.deviceAddress;

            instances.push_back(inst);
        }

        if (instances.empty()) return;

        // Protect shared TLAS/instance/scratch buffers from cross-frame races
        insertTLASCrossFrameBarrier(cmd);

        uint32_t instanceCount = static_cast<uint32_t>(instances.size());
        vk::DeviceSize instanceDataSize = sizeof(vk::AccelerationStructureInstanceKHR) * instanceCount;

        // Ensure buffers are large enough
        ensureInstanceBuffer(instanceDataSize);

        auto& staging = instanceStagingBuffers[currentStagingFrame];
        ensureStagingBuffer(staging, instanceDataSize);

        // Upload instance data via staging
        memcpy(staging.allocation.mappedPtr, instances.data(), instanceDataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.size = instanceDataSize;
        cmd.copyBuffer(staging.buffer, instanceBuffer, 1, &copyRegion);

        // Barrier: transfer -> AS build
        vk::MemoryBarrier copyBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &copyBarrier, 0, nullptr, 0, nullptr);

        // Configure TLAS build
        vk::DeviceAddress instanceAddress = vkDevice.getBufferAddress({instanceBuffer});

        vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceAddress;

        vk::AccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        tlasGeometry.geometry.instances = instancesData;

        // A BLAS compaction this frame replaces a BLAS device address; the TLAS must be
        // rebuilt (not refit) so it picks up the new acceleration structure references.
        bool canUpdate = tlasBuilt && (currentInstanceCount == instanceCount) && !forceNextTlasRebuild;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                          vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        buildInfo.mode = canUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate
                                   : vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &tlasGeometry;

        // Query sizes
        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        vkDevice.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo, &instanceCount, &sizeInfo);

        // Rebuild TLAS structure if needed
        if (!canUpdate)
        {
            if (tlas)
                deferTLASDestruction(tlas, tlasBuffer, tlasAllocation);
            tlas = nullptr;
            tlasBuffer = nullptr;
            tlasAllocation = {};

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, tlasBuffer, tlasAllocation, device.getMemoryManager());
            }

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{};
            tlasCreateInfo.buffer = tlasBuffer;
            tlasCreateInfo.size = sizeInfo.accelerationStructureSize;
            tlasCreateInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            tlas = vkDevice.createAccelerationStructureKHR(tlasCreateInfo);

            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;

            memoryBudget.tlasTotalBytes = sizeInfo.accelerationStructureSize;
        }

        // Ensure TLAS scratch buffer
        vk::DeviceSize requiredScratch = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);
        ensureTlasScratch(requiredScratch);

        vk::DeviceAddress scratchAddress = vkDevice.getBufferAddress({tlasScratchBuffer});

        if (canUpdate)
        {
            buildInfo.srcAccelerationStructure = tlas;
        }
        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

        // Barrier: AS build -> compute/fragment shader reads
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        currentInstanceCount = instanceCount;
        tlasBuilt = true;
        memoryBudget.tlasInstanceCount = instanceCount;

        // The TLAS build above consumed any compaction-driven rebuild request.
        forceNextTlasRebuild = false;

        currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;

        updateDescriptor();
    }

    void AccelerationStructureManager::ensureInstanceBuffer(vk::DeviceSize requiredSize)
    {
        if (instanceBuffer && instanceBufferCapacity >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        if (instanceBuffer)
        {
            auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();
            if (dq)
            {
                vk::Buffer oldBuf = instanceBuffer;
                core::VulkanAllocation oldAlloc = instanceAllocation;
                core::VulkanMemoryManager* memMgr = &device.getMemoryManager();
                dq->queueCustom([oldBuf, oldAlloc, memMgr](vk::Device dev) mutable {
                    core::BufferUtilities::destroyBuffer(dev, oldBuf, oldAlloc, *memMgr);
                });
            }
            else
            {
                core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceAllocation, device.getMemoryManager());
            }
            instanceBuffer = nullptr;
            instanceAllocation = {};
        }

        // Allocate with some headroom to avoid frequent reallocations
        vk::DeviceSize allocSize = std::max(requiredSize, static_cast<vk::DeviceSize>(requiredSize * 1.5));

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = allocSize;
        request.usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                        vk::BufferUsageFlagBits::eShaderDeviceAddress |
                        vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(request, instanceBuffer, instanceAllocation, device.getMemoryManager());

        instanceBufferCapacity = static_cast<uint32_t>(allocSize);
    }

    void AccelerationStructureManager::ensureStagingBuffer(StagingBuffer& staging, vk::DeviceSize requiredSize)
    {
        if (staging.buffer && staging.capacity >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        if (staging.buffer)
        {
            auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();
            if (dq)
            {
                vk::Buffer oldBuf = staging.buffer;
                core::VulkanAllocation oldAlloc = staging.allocation;
                core::VulkanMemoryManager* memMgr = &device.getMemoryManager();
                dq->queueCustom([oldBuf, oldAlloc, memMgr](vk::Device dev) mutable {
                    core::BufferUtilities::destroyBuffer(dev, oldBuf, oldAlloc, *memMgr);
                });
                staging.buffer = nullptr;
                staging.allocation = {};
            }
            else
            {
                core::BufferUtilities::destroyBuffer(vkDevice, staging.buffer, staging.allocation, device.getMemoryManager());
            }
        }

        vk::DeviceSize allocSize = std::max(requiredSize, static_cast<vk::DeviceSize>(requiredSize * 1.5));

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = allocSize;
        request.usage = vk::BufferUsageFlagBits::eTransferSrc;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request, staging.buffer, staging.allocation, device.getMemoryManager());

        staging.capacity = allocSize;
    }

    void AccelerationStructureManager::ensureTlasScratch(vk::DeviceSize requiredSize)
    {
        if (tlasScratchBuffer && tlasScratchSize >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        if (tlasScratchBuffer)
        {
            auto* dq = deletionQueue ? deletionQueue : core::RenderManager::getGlobalDeletionQueue();
            if (dq)
            {
                vk::Buffer oldBuf = tlasScratchBuffer;
                core::VulkanAllocation oldAlloc = tlasScratchAllocation;
                core::VulkanMemoryManager* memMgr = &device.getMemoryManager();
                dq->queueCustom([oldBuf, oldAlloc, memMgr](vk::Device dev) mutable {
                    core::BufferUtilities::destroyBuffer(dev, oldBuf, oldAlloc, *memMgr);
                });
                tlasScratchBuffer = nullptr;
                tlasScratchAllocation = {};
            }
            else
            {
                core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());
            }
        }

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = requiredSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eShaderDeviceAddress;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(request, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());

        tlasScratchSize = requiredSize;
    }

    // ============================================================
    // Terrain BLAS support
    // ============================================================

    void AccelerationStructureManager::notifyTerrainTileReady(const std::string& tileKey,
                                                               uint32_t vertexOffset, uint32_t vertexCount,
                                                               uint32_t indexOffset, uint32_t indexCount)
    {
        if (!initialized) return;
        if (vertexCount == 0 || indexCount == 0) return;

        std::lock_guard<std::mutex> lock(pendingMutex);

        // If tile already has a BLAS (LOD change), destroy old one first
        auto it = terrainBlasCache.find(tileKey);
        if (it != terrainBlasCache.end() && it->second.deviceAddress != 0)
        {
            // Check if geometry actually changed
            if (it->second.lod0VertexOffset == vertexOffset && it->second.lod0IndexOffset == indexOffset)
                return; // Same geometry, skip

            // Remove old reverse mapping
            uint64_t oldKey = makeGeometryOffsetKey(it->second.lod0VertexOffset, it->second.lod0IndexOffset);
            terrainOffsetToTileKey.erase(oldKey);
            invalidatePendingCompaction(it->second.blas);
            destroyBLASEntry(it->second);
            terrainBlasCache.erase(it);
        }

        PendingBLAS pending;
        pending.key = tileKey;
        pending.vertexOffset = vertexOffset;
        pending.vertexCount = vertexCount;
        pending.indexOffset = indexOffset;
        pending.indexCount = indexCount;
        pendingTerrainBLASBuilds.push_back(std::move(pending));

        uint64_t offsetKey = makeGeometryOffsetKey(vertexOffset, indexOffset);
        terrainOffsetToTileKey[offsetKey] = tileKey;
    }

    void AccelerationStructureManager::notifyTerrainTileRemoved(const std::string& tileKey)
    {
        if (!initialized) return;

        std::lock_guard<std::mutex> lock(pendingMutex);

        auto it = terrainBlasCache.find(tileKey);
        if (it == terrainBlasCache.end()) return;

        uint64_t offsetKey = makeGeometryOffsetKey(it->second.lod0VertexOffset, it->second.lod0IndexOffset);
        terrainOffsetToTileKey.erase(offsetKey);
        invalidatePendingCompaction(it->second.blas);
        destroyBLASEntry(it->second);
        terrainBlasCache.erase(it);

        pendingTerrainBLASBuilds.erase(
            std::remove_if(pendingTerrainBLASBuilds.begin(), pendingTerrainBLASBuilds.end(),
                [&tileKey](const PendingBLAS& p) { return p.key == tileKey; }),
            pendingTerrainBLASBuilds.end());
    }

    void AccelerationStructureManager::buildPendingTerrainBLAS(vk::CommandBuffer cmd,
                                                                vk::Buffer terrainVertexBuffer, uint32_t vertexStride,
                                                                vk::Buffer terrainIndexBuffer)
    {
        if (!initialized) return;

        std::vector<PendingBLAS> localPending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingTerrainBLASBuilds.empty()) return;
            localPending = std::move(pendingTerrainBLASBuilds);
            pendingTerrainBLASBuilds.clear();
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Keys of terrain BLAS actually (re)built this dispatch — queued for compaction below.
        std::vector<std::string> builtKeys;
        builtKeys.reserve(localPending.size());

        vk::DeviceAddress vertexBufferAddress = vkDevice.getBufferAddress({terrainVertexBuffer});
        vk::DeviceAddress indexBufferAddress = vkDevice.getBufferAddress({terrainIndexBuffer});

        // Pre-calculate max scratch size to avoid reallocation during command recording (VK-1182)
        vk::DeviceSize maxScratchSize = 0;
        for (const auto& pending : localPending)
        {
            uint32_t triCount = pending.indexCount / 3;
            if (triCount == 0) continue;

            vk::AccelerationStructureGeometryTrianglesDataKHR triData{};
            triData.vertexFormat = vk::Format::eR32G32B32Sfloat;
            triData.vertexData.deviceAddress = vertexBufferAddress +
                static_cast<vk::DeviceSize>(pending.vertexOffset) * vertexStride;
            triData.vertexStride = vertexStride;
            triData.maxVertex = pending.vertexCount - 1;
            triData.indexType = vk::IndexType::eUint32;
            triData.indexData.deviceAddress = indexBufferAddress +
                static_cast<vk::DeviceSize>(pending.indexOffset) * sizeof(uint32_t);

            vk::AccelerationStructureGeometryKHR geom{};
            geom.geometryType = vk::GeometryTypeKHR::eTriangles;
            geom.geometry.triangles = triData;
            geom.flags = vk::GeometryFlagBitsKHR::eOpaque;

            vk::AccelerationStructureBuildGeometryInfoKHR bi{};
            bi.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            bi.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                       vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            bi.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            bi.geometryCount = 1;
            bi.pGeometries = &geom;

            vk::AccelerationStructureBuildSizesInfoKHR si{};
            vkDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, &bi, &triCount, &si);
            maxScratchSize = std::max(maxScratchSize, si.buildScratchSize);
        }
        if (maxScratchSize > 0)
            blasScratchPool.acquire(maxScratchSize, device);

        for (const auto& pending : localPending)
        {
            uint32_t triangleCount = pending.indexCount / 3;
            if (triangleCount == 0) continue;

            vk::AccelerationStructureGeometryTrianglesDataKHR triangleData{};
            triangleData.vertexFormat = vk::Format::eR32G32B32Sfloat;
            triangleData.vertexData.deviceAddress = vertexBufferAddress +
                static_cast<vk::DeviceSize>(pending.vertexOffset) * vertexStride;
            triangleData.vertexStride = vertexStride;
            triangleData.maxVertex = pending.vertexCount - 1;
            triangleData.indexType = vk::IndexType::eUint32;
            triangleData.indexData.deviceAddress = indexBufferAddress +
                static_cast<vk::DeviceSize>(pending.indexOffset) * sizeof(uint32_t);

            vk::AccelerationStructureGeometryKHR geometry{};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.geometry.triangles = triangleData;
            geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                              vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
            vkDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                &buildInfo, &triangleCount, &sizeInfo);

            BLASEntry entry{};
            entry.size = sizeInfo.accelerationStructureSize;
            entry.lod0VertexOffset = pending.vertexOffset;
            entry.lod0IndexOffset = pending.indexOffset;
            entry.lod0VertexCount = pending.vertexCount;
            entry.lod0IndexCount = pending.indexCount;

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, entry.buffer, entry.allocation, device.getMemoryManager());
            }

            vk::AccelerationStructureCreateInfoKHR createInfo{};
            createInfo.buffer = entry.buffer;
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            entry.blas = vkDevice.createAccelerationStructureKHR(createInfo);

            vk::DeviceAddress scratchAddress = blasScratchPool.acquire(sizeInfo.buildScratchSize, device);

            buildInfo.dstAccelerationStructure = entry.blas;
            buildInfo.scratchData.deviceAddress = scratchAddress;

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
            rangeInfo.primitiveCount = triangleCount;
            const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
            cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

            // Barrier between individual BLAS builds — required because they share scratch memory
            {
                vk::MemoryBarrier buildBarrier{
                    vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                    vk::AccessFlagBits::eAccelerationStructureWriteKHR |
                    vk::AccessFlagBits::eAccelerationStructureReadKHR
                };
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                    vk::DependencyFlags{},
                    1, &buildBarrier, 0, nullptr, 0, nullptr);
            }

            entry.deviceAddress = vkDevice.getAccelerationStructureAddressKHR({entry.blas});

            memoryBudget.blasTotalBytes += entry.size;
            memoryBudget.blasCount++;

            // Destroy existing entry if present (avoids leak when tile is rebuilt)
            auto existingIt = terrainBlasCache.find(pending.key);
            if (existingIt != terrainBlasCache.end())
            {
                {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    invalidatePendingCompaction(existingIt->second.blas);
                }
                destroyBLASEntry(existingIt->second);
                terrainBlasCache.erase(existingIt);
            }
            terrainBlasCache[pending.key] = entry;
            builtKeys.push_back(pending.key);
        }

        // Final barrier: BLAS builds → TLAS build reads
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        // Queue compaction-size queries for the terrain BLAS just built (after barrier).
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            for (const auto& key : builtKeys)
            {
                auto it = terrainBlasCache.find(key);
                if (it != terrainBlasCache.end() && it->second.blas)
                    queueBlasForCompaction(cmd, key, it->second, /*isTerrain*/ true);
            }
        }

        memoryBudget.scratchPeakBytes = blasScratchPool.getPeakSize();

        vfLogInfo("AccelerationStructureManager: Built {} terrain BLAS entries (total terrain: {})",
                  localPending.size(), terrainBlasCache.size());
    }

    void AccelerationStructureManager::buildTLASWithTerrain(vk::CommandBuffer cmd,
                                                             const std::vector<gpudriven::GPUObjectData>& objects,
                                                             uint32_t objectCount,
                                                             const gpudriven::MergedMeshBuffer& mergedBuffer,
                                                             const std::vector<gpudriven::TerrainTileGPUData>& terrainTiles,
                                                             uint32_t terrainTileCount)
    {
        if (!initialized || (objectCount == 0 && terrainTileCount == 0)) return;
        if (blasCache.empty() && terrainBlasCache.empty()) return;

        // Deferred deletions handled by DeferredDeletionQueue

        vk::Device vkDevice = device.getLogicalDevice();

        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        instances.reserve(objectCount + terrainTileCount);

        // Mesh object instances (same as buildTLAS)
        for (uint32_t i = 0; i < objectCount; ++i)
        {
            const auto& obj = objects[i];

            if (obj.flags & (gpudriven::ObjectFlags::Translucent | gpudriven::ObjectFlags::AdditiveBlend))
                continue;
            if (obj.flags & gpudriven::ObjectFlags::TerrainTile)
                continue;

            uint32_t vertexOffset = obj.lod0Data.x;
            uint32_t indexOffset = obj.lod0Data.y;
            uint64_t offsetKey = makeGeometryOffsetKey(vertexOffset, indexOffset);

            auto keyIt = geometryOffsetToSubmeshKey.find(offsetKey);
            if (keyIt == geometryOffsetToSubmeshKey.end()) continue;

            auto blasIt = blasCache.find(keyIt->second);
            if (blasIt == blasCache.end() || blasIt->second.deviceAddress == 0) continue;

            const glm::mat4& m = obj.modelMatrix;
            vk::TransformMatrixKHR transform{};
            transform.matrix[0][0] = m[0][0]; transform.matrix[0][1] = m[1][0]; transform.matrix[0][2] = m[2][0]; transform.matrix[0][3] = m[3][0];
            transform.matrix[1][0] = m[0][1]; transform.matrix[1][1] = m[1][1]; transform.matrix[1][2] = m[2][1]; transform.matrix[1][3] = m[3][1];
            transform.matrix[2][0] = m[0][2]; transform.matrix[2][1] = m[1][2]; transform.matrix[2][2] = m[2][2]; transform.matrix[2][3] = m[3][2];

            vk::AccelerationStructureInstanceKHR inst{};
            inst.transform = transform;
            inst.instanceCustomIndex = i;
            inst.mask = 0xFF;
            inst.instanceShaderBindingTableRecordOffset = 0;
            inst.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            inst.accelerationStructureReference = blasIt->second.deviceAddress;
            instances.push_back(inst);
        }

        // Terrain tile instances
        for (uint32_t i = 0; i < terrainTileCount; ++i)
        {
            const auto& tile = terrainTiles[i];

            // Find the best LOD BLAS — use lod0MeshletData.z (baseVertexOffset) as part of key
            // Terrain tiles use coordX/coordZ as unique identifier
            std::string tileKey = std::to_string(tile.coordX) + "_" + std::to_string(tile.coordZ);

            auto blasIt = terrainBlasCache.find(tileKey);
            if (blasIt == terrainBlasCache.end() || blasIt->second.deviceAddress == 0) continue;

            const glm::mat4& m = tile.modelMatrix;
            vk::TransformMatrixKHR transform{};
            transform.matrix[0][0] = m[0][0]; transform.matrix[0][1] = m[1][0]; transform.matrix[0][2] = m[2][0]; transform.matrix[0][3] = m[3][0];
            transform.matrix[1][0] = m[0][1]; transform.matrix[1][1] = m[1][1]; transform.matrix[1][2] = m[2][1]; transform.matrix[1][3] = m[3][1];
            transform.matrix[2][0] = m[0][2]; transform.matrix[2][1] = m[1][2]; transform.matrix[2][2] = m[2][2]; transform.matrix[2][3] = m[3][2];

            vk::AccelerationStructureInstanceKHR inst{};
            inst.transform = transform;
            inst.instanceCustomIndex = objectCount + i;
            inst.mask = 0xFF;
            inst.instanceShaderBindingTableRecordOffset = 0;
            inst.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            inst.accelerationStructureReference = blasIt->second.deviceAddress;
            instances.push_back(inst);
        }

        if (instances.empty()) return;

        // Protect shared TLAS/instance/scratch buffers from cross-frame races
        insertTLASCrossFrameBarrier(cmd);

        // The rest is identical to buildTLAS — upload instances, build/update TLAS
        uint32_t instanceCount = static_cast<uint32_t>(instances.size());
        vk::DeviceSize instanceDataSize = sizeof(vk::AccelerationStructureInstanceKHR) * instanceCount;

        ensureInstanceBuffer(instanceDataSize);

        auto& staging = instanceStagingBuffers[currentStagingFrame];
        ensureStagingBuffer(staging, instanceDataSize);

        memcpy(staging.allocation.mappedPtr, instances.data(), instanceDataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.size = instanceDataSize;
        cmd.copyBuffer(staging.buffer, instanceBuffer, 1, &copyRegion);

        vk::MemoryBarrier copyBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &copyBarrier, 0, nullptr, 0, nullptr);

        vk::DeviceAddress instanceAddress = vkDevice.getBufferAddress({instanceBuffer});

        vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceAddress;

        vk::AccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        tlasGeometry.geometry.instances = instancesData;

        // A BLAS compaction this frame replaces a BLAS device address; the TLAS must be
        // rebuilt (not refit) so it picks up the new acceleration structure references.
        bool canUpdate = tlasBuilt && (currentInstanceCount == instanceCount) && !forceNextTlasRebuild;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                          vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        buildInfo.mode = canUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate
                                   : vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &tlasGeometry;

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        vkDevice.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo, &instanceCount, &sizeInfo);

        if (!canUpdate)
        {
            if (tlas)
                deferTLASDestruction(tlas, tlasBuffer, tlasAllocation);
            tlas = nullptr;
            tlasBuffer = nullptr;
            tlasAllocation = {};

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, tlasBuffer, tlasAllocation, device.getMemoryManager());
            }

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{};
            tlasCreateInfo.buffer = tlasBuffer;
            tlasCreateInfo.size = sizeInfo.accelerationStructureSize;
            tlasCreateInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            tlas = vkDevice.createAccelerationStructureKHR(tlasCreateInfo);

            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            memoryBudget.tlasTotalBytes = sizeInfo.accelerationStructureSize;
        }

        vk::DeviceSize requiredScratch = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);
        ensureTlasScratch(requiredScratch);

        vk::DeviceAddress scratchAddress = vkDevice.getBufferAddress({tlasScratchBuffer});

        if (canUpdate)
        {
            buildInfo.srcAccelerationStructure = tlas;
        }
        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        currentInstanceCount = instanceCount;
        tlasBuilt = true;
        memoryBudget.tlasInstanceCount = instanceCount;

        // The TLAS build above consumed any compaction-driven rebuild request.
        forceNextTlasRebuild = false;

        currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT;

        updateDescriptor();
    }

    void AccelerationStructureManager::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding tlasBinding{};
        tlasBinding.binding = 0;
        tlasBinding.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorBindingFlags bindingFlags = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.bindingCount = 1;
        flagsInfo.pBindingFlags = &bindingFlags;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &tlasBinding;

        tlasDescriptorLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void AccelerationStructureManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eAccelerationStructureKHR;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void AccelerationStructureManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &tlasDescriptorLayout;

        tlasDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void AccelerationStructureManager::updateDescriptor()
    {
        if (!tlas) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::WriteDescriptorSetAccelerationStructureKHR asWrite{};
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &tlas;

        vk::WriteDescriptorSet write{};
        write.pNext = &asWrite;
        write.dstSet = tlasDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }
}
