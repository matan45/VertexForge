#include "ClusterBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/ClusterDAGTypes.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/EditorLogger.hpp"

namespace render::gpudriven
{
    ClusterBuffer::ClusterBuffer(core::Device& device)
        : device(device)
    {
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    ClusterBuffer::~ClusterBuffer()
    {
        cleanup();
    }

    // =========================================================================
    // Initialization
    // =========================================================================

    void ClusterBuffer::init(uint32_t maxClusters, uint32_t maxDAGHeaders,
                             uint32_t maxSelections, uint32_t maxStreamingUnits)
    {
        if (initialized)
        {
            vfLogWarning("ClusterBuffer: Already initialized");
            return;
        }

        maxClusterCount = maxClusters;
        maxDAGHeaderCount = maxDAGHeaders;
        maxSelectionCount = maxSelections;
        maxStreamingUnitCount = maxStreamingUnits;

        // Initialize allocators
        clusterAllocator.reset(maxClusterCount);
        dagHeaderAllocator.reset(maxDAGHeaderCount);
        streamingUnitAllocator.reset(maxStreamingUnitCount);

        createBuffers();

        initialized = true;

        vfLogInfo("ClusterBuffer: Initialized with {} clusters, {} DAG headers, {} selections, {} streaming units",
                  maxClusterCount, maxDAGHeaderCount, maxSelectionCount, maxStreamingUnitCount);
        vfLogInfo("ClusterBuffer: Total buffer size: {} MB",
                  getTotalBufferSize() / (1024 * 1024));
    }

    void ClusterBuffer::cleanup()
    {
        if (!initialized) return;

        flushPendingTransfers();

        allocations.clear();
        allocationKeyToIndex.clear();
        freeAllocationSlots.clear();

        destroyBuffers();

        currentClusterCount = 0;
        currentDAGHeaderCount = 0;
        currentStreamingUnitCount = 0;

        initialized = false;
    }

    void ClusterBuffer::createBuffers()
    {
        auto logicalDevice = device.getLogicalDevice();

        // Create cluster buffer (GPUCluster[])
        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getClusterBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            clusterBuffer,
            clusterBufferMemory
        );

        // Create DAG header buffer (GPUClusterDAGHeader[])
        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getDAGHeaderBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            dagHeaderBuffer,
            dagHeaderBufferMemory
        );

        // Create cluster selection buffer (GPUClusterSelection[])
        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getClusterSelectionBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            clusterSelectionBuffer,
            clusterSelectionBufferMemory
        );

        // Create streaming unit buffer (GPUClusterStreamingUnit[])
        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getStreamingUnitBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            streamingUnitBuffer,
            streamingUnitBufferMemory
        );
    }

    void ClusterBuffer::destroyBuffers()
    {
        auto logicalDevice = device.getLogicalDevice();

        if (clusterBuffer)
        {
            logicalDevice.destroyBuffer(clusterBuffer);
            clusterBuffer = nullptr;
        }
        if (clusterBufferMemory)
        {
            logicalDevice.freeMemory(clusterBufferMemory);
            clusterBufferMemory = nullptr;
        }

        if (dagHeaderBuffer)
        {
            logicalDevice.destroyBuffer(dagHeaderBuffer);
            dagHeaderBuffer = nullptr;
        }
        if (dagHeaderBufferMemory)
        {
            logicalDevice.freeMemory(dagHeaderBufferMemory);
            dagHeaderBufferMemory = nullptr;
        }

        if (clusterSelectionBuffer)
        {
            logicalDevice.destroyBuffer(clusterSelectionBuffer);
            clusterSelectionBuffer = nullptr;
        }
        if (clusterSelectionBufferMemory)
        {
            logicalDevice.freeMemory(clusterSelectionBufferMemory);
            clusterSelectionBufferMemory = nullptr;
        }

        if (streamingUnitBuffer)
        {
            logicalDevice.destroyBuffer(streamingUnitBuffer);
            streamingUnitBuffer = nullptr;
        }
        if (streamingUnitBufferMemory)
        {
            logicalDevice.freeMemory(streamingUnitBufferMemory);
            streamingUnitBufferMemory = nullptr;
        }
    }

    // =========================================================================
    // Reservation
    // =========================================================================

    ClusterDAGAllocation* ClusterBuffer::reserveClusters(const std::string& meshPath,
                                                          const resource::MeshStreamHeader& header)
    {
        if (!initialized)
        {
            vfLogError("ClusterBuffer: Not initialized");
            return nullptr;
        }

        // Track all allocations made during this call for cleanup on failure
        std::vector<std::string> allocatedKeys;

        for (uint32_t submeshIdx = 0; submeshIdx < header.numSubmeshes; ++submeshIdx)
        {
            const auto& submeshInfo = header.submeshes[submeshIdx];

            if (!submeshInfo.hasClusterDAGData)
            {
                continue;
            }

            std::string key = makeAllocationKey(meshPath, submeshInfo.name, submeshIdx);

            // Skip if already allocated
            if (allocationKeyToIndex.find(key) != allocationKeyToIndex.end())
            {
                continue;
            }

            ClusterDAGAllocation alloc;
            alloc.meshPath = meshPath;
            alloc.submeshName = submeshInfo.name;
            alloc.submeshIndex = submeshIdx;

            // Cache metadata from file info
            alloc.leafClusterCount = submeshInfo.clusterDAGInfo.leafClusterCount;
            alloc.maxDepth = submeshInfo.clusterDAGInfo.maxDepth;
            alloc.maxGeometricError = submeshInfo.clusterDAGInfo.maxGeometricError;
            alloc.boundingSphere = submeshInfo.clusterDAGInfo.boundingSphere;

            // Allocate space for this submesh's cluster DAG
            if (!allocateClusterSpace(alloc, submeshInfo.clusterDAGInfo, key))
            {
                // Rollback: free all previously allocated keys in this call
                for (const auto& prevKey : allocatedKeys)
                {
                    auto it = allocationKeyToIndex.find(prevKey);
                    if (it != allocationKeyToIndex.end())
                    {
                        size_t allocIndex = it->second;
                        auto& prevAlloc = allocations[allocIndex];
                        freeClusterSpace(prevAlloc);
                        prevAlloc = ClusterDAGAllocation{};
                        freeAllocationSlots.push_back(allocIndex);
                        allocationKeyToIndex.erase(it);
                    }
                }
                return nullptr;
            }

            // Store allocation
            size_t allocIndex;
            if (!freeAllocationSlots.empty())
            {
                allocIndex = freeAllocationSlots.back();
                freeAllocationSlots.pop_back();
                allocations[allocIndex] = std::move(alloc);
            }
            else
            {
                allocIndex = allocations.size();
                allocations.push_back(std::move(alloc));
            }
            allocationKeyToIndex[key] = allocIndex;
            allocatedKeys.push_back(key);
        }

        // Return pointer to first allocation for this mesh
        for (uint32_t submeshIdx = 0; submeshIdx < header.numSubmeshes; ++submeshIdx)
        {
            const auto& submeshInfo = header.submeshes[submeshIdx];
            std::string key = makeAllocationKey(meshPath, submeshInfo.name, submeshIdx);
            auto it = allocationKeyToIndex.find(key);
            if (it != allocationKeyToIndex.end())
            {
                return &allocations[it->second];
            }
        }

        return nullptr;
    }

    bool ClusterBuffer::allocateClusterSpace(ClusterDAGAllocation& alloc,
                                              const resource::ClusterDAGFileInfo& dagInfo,
                                              const std::string& debugKey)
    {
        if (dagInfo.clusterCount == 0)
        {
            alloc.isAllocated = false;
            return true;
        }

        // Allocate cluster space
        uint32_t clusterOffset = clusterAllocator.allocate(dagInfo.clusterCount);
        if (clusterOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vfLogError("ClusterBuffer: Failed to allocate {} clusters for {}",
                       dagInfo.clusterCount, debugKey);
            return false;
        }

        // Allocate DAG header slot
        uint32_t dagHeaderIndex = dagHeaderAllocator.allocate(1);
        if (dagHeaderIndex == FreeListAllocator::ALLOCATION_FAILED)
        {
            clusterAllocator.free(clusterOffset, dagInfo.clusterCount);
            vfLogError("ClusterBuffer: Failed to allocate DAG header for {}", debugKey);
            return false;
        }

        alloc.clusterOffset = clusterOffset;
        alloc.clusterCount = dagInfo.clusterCount;
        alloc.dagHeaderIndex = dagHeaderIndex;
        alloc.isAllocated = true;

        return true;
    }

    void ClusterBuffer::freeClusterSpace(ClusterDAGAllocation& alloc)
    {
        if (!alloc.isAllocated) return;

        if (alloc.clusterCount > 0)
        {
            clusterAllocator.free(alloc.clusterOffset, alloc.clusterCount);
        }
        dagHeaderAllocator.free(alloc.dagHeaderIndex, 1);

        alloc.isAllocated = false;
        alloc.clusterOffset = 0;
        alloc.clusterCount = 0;
        alloc.dagHeaderIndex = 0;
    }

    // =========================================================================
    // Upload
    // =========================================================================

    bool ClusterBuffer::uploadClusterDAG(const std::string& meshPath,
                                          const std::string& submeshName,
                                          uint32_t submeshIndex,
                                          const resource::ClusterDAGData& dagData,
                                          uint32_t baseMeshletOffset)
    {
        if (!initialized)
        {
            vfLogError("ClusterBuffer: Not initialized");
            return false;
        }

        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it == allocationKeyToIndex.end())
        {
            vfLogError("ClusterBuffer: No allocation found for {}", key);
            return false;
        }

        auto& alloc = allocations[it->second];

        if (!alloc.isAllocated)
        {
            vfLogError("ClusterBuffer: Allocation not ready for {}", key);
            return false;
        }

        if (dagData.clusters.size() != alloc.clusterCount)
        {
            vfLogError("ClusterBuffer: Cluster count mismatch for {} (expected {}, got {})",
                       key, alloc.clusterCount, dagData.clusters.size());
            return false;
        }

        // Convert CPU clusters to GPU format
        std::vector<GPUCluster> gpuClusters(alloc.clusterCount);
        for (uint32_t i = 0; i < alloc.clusterCount; ++i)
        {
            gpuClusters[i] = toGPUCluster(dagData.clusters[i]);
            // Offset meshlet references to global buffer
            gpuClusters[i].meshletOffset += baseMeshletOffset;
        }

        // Upload clusters
        uploadClustersAt(alloc.clusterOffset, gpuClusters.data(), alloc.clusterCount);

        // Build and upload DAG header
        GPUClusterDAGHeader gpuHeader{};
        gpuHeader.clusterCount = dagData.header.clusterCount;
        gpuHeader.leafClusterCount = dagData.header.leafClusterCount;
        gpuHeader.maxDepth = dagData.header.maxDepth;
        gpuHeader.clusterOffset = alloc.clusterOffset;
        gpuHeader.maxGeometricError = dagData.header.maxGeometricError;
        gpuHeader.minGeometricError = dagData.header.minGeometricError;
        gpuHeader.rootClusterIndex = 0; // Root is always at index 0 within DAG
        gpuHeader.streamingUnitCount = 0; // TODO: Populate when streaming units are implemented
        gpuHeader.boundingSphere = dagData.header.boundingSphere;

        uploadDAGHeaderAt(alloc.dagHeaderIndex, gpuHeader);

        // Mark as uploading until transfers complete
        alloc.streamState = ClusterStreamState::Uploading;

        // Wait for async transfers to complete before marking ready
        flushPendingTransfers();

        // Mark as used in allocators
        clusterAllocator.markUsed(alloc.clusterCount);
        dagHeaderAllocator.markUsed(1);

        currentClusterCount = clusterAllocator.getUsedCount();
        currentDAGHeaderCount = dagHeaderAllocator.getUsedCount();

        // Now safe to mark as ready
        alloc.streamState = ClusterStreamState::Ready;

        return true;
    }

    // =========================================================================
    // Upload Helpers
    // =========================================================================

    void ClusterBuffer::uploadClustersAt(uint32_t offset, const GPUCluster* data, uint32_t count)
    {
        if (!data || count == 0) return;

        if (offset + count > maxClusterCount)
        {
            vfLogError("ClusterBuffer: Cluster upload exceeds buffer bounds (offset={}, count={}, max={})",
                       offset, count, maxClusterCount);
            return;
        }

        size_t dataSize = static_cast<size_t>(count) * sizeof(GPUCluster);
        size_t dstOffset = static_cast<size_t>(offset) * sizeof(GPUCluster);

        transferManager->copyToBufferAsync(clusterBuffer, data, dataSize, dstOffset);
    }

    void ClusterBuffer::uploadDAGHeaderAt(uint32_t index, const GPUClusterDAGHeader& header)
    {
        if (index >= maxDAGHeaderCount)
        {
            vfLogError("ClusterBuffer: DAG header upload exceeds buffer bounds (index={}, max={})",
                       index, maxDAGHeaderCount);
            return;
        }

        size_t dataSize = sizeof(GPUClusterDAGHeader);
        size_t dstOffset = static_cast<size_t>(index) * sizeof(GPUClusterDAGHeader);

        transferManager->copyToBufferAsync(dagHeaderBuffer, &header, dataSize, dstOffset);
    }

    void ClusterBuffer::uploadStreamingUnitsAt(uint32_t offset, const GPUClusterStreamingUnit* data, uint32_t count)
    {
        if (!data || count == 0) return;

        if (offset + count > maxStreamingUnitCount)
        {
            vfLogError("ClusterBuffer: Streaming unit upload exceeds buffer bounds (offset={}, count={}, max={})",
                       offset, count, maxStreamingUnitCount);
            return;
        }

        size_t dataSize = static_cast<size_t>(count) * sizeof(GPUClusterStreamingUnit);
        size_t dstOffset = static_cast<size_t>(offset) * sizeof(GPUClusterStreamingUnit);

        transferManager->copyToBufferAsync(streamingUnitBuffer, data, dataSize, dstOffset);
    }

    // =========================================================================
    // Eviction
    // =========================================================================

    bool ClusterBuffer::evictClusterDAG(const std::string& meshPath,
                                         const std::string& submeshName,
                                         uint32_t submeshIndex)
    {
        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it == allocationKeyToIndex.end())
        {
            return false;
        }

        size_t allocIndex = it->second;
        auto& alloc = allocations[allocIndex];

        // Safety: ensure any pending uploads complete before eviction
        if (alloc.streamState == ClusterStreamState::Uploading)
        {
            flushPendingTransfers();
        }

        freeClusterSpace(alloc);
        alloc = ClusterDAGAllocation{};
        freeAllocationSlots.push_back(allocIndex);
        allocationKeyToIndex.erase(it);

        return true;
    }

    void ClusterBuffer::evictMesh(const std::string& meshPath)
    {
        std::vector<std::string> keysToRemove;
        bool hasUploadingAllocations = false;

        for (const auto& [key, index] : allocationKeyToIndex)
        {
            if (key.rfind(meshPath + ":", 0) == 0)
            {
                keysToRemove.push_back(key);
                if (allocations[index].streamState == ClusterStreamState::Uploading)
                {
                    hasUploadingAllocations = true;
                }
            }
        }

        // Safety: ensure any pending uploads complete before eviction
        if (hasUploadingAllocations)
        {
            flushPendingTransfers();
        }

        for (const auto& key : keysToRemove)
        {
            auto it = allocationKeyToIndex.find(key);
            if (it != allocationKeyToIndex.end())
            {
                size_t allocIndex = it->second;
                auto& alloc = allocations[allocIndex];
                freeClusterSpace(alloc);
                alloc = ClusterDAGAllocation{};
                freeAllocationSlots.push_back(allocIndex);
                allocationKeyToIndex.erase(it);
            }
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    const ClusterDAGAllocation* ClusterBuffer::getAllocation(const std::string& meshPath,
                                                              const std::string& submeshName,
                                                              uint32_t submeshIndex) const
    {
        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it != allocationKeyToIndex.end())
        {
            return &allocations[it->second];
        }
        return nullptr;
    }

    ClusterDAGInfo ClusterBuffer::getClusterDAGInfo(const ClusterDAGAllocation& alloc) const
    {
        ClusterDAGInfo info{};
        if (alloc.isAllocated)
        {
            info.clusterOffset = alloc.clusterOffset;
            info.clusterCount = alloc.clusterCount;
            info.dagHeaderIndex = alloc.dagHeaderIndex;
            info.leafClusterCount = alloc.leafClusterCount;
            info.maxGeometricError = alloc.maxGeometricError;
        }
        return info;
    }

    bool ClusterBuffer::hasClusterData(const std::string& meshPath) const
    {
        for (const auto& [key, index] : allocationKeyToIndex)
        {
            if (key.rfind(meshPath + ":", 0) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // State Management
    // =========================================================================

    void ClusterBuffer::setStreamState(const std::string& meshPath,
                                        const std::string& submeshName,
                                        uint32_t submeshIndex,
                                        ClusterStreamState state)
    {
        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it != allocationKeyToIndex.end())
        {
            allocations[it->second].streamState = state;
        }
    }

    ClusterStreamState ClusterBuffer::getStreamState(const std::string& meshPath,
                                                      const std::string& submeshName,
                                                      uint32_t submeshIndex) const
    {
        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it != allocationKeyToIndex.end())
        {
            return allocations[it->second].streamState;
        }
        return ClusterStreamState::NotRequested;
    }

    // =========================================================================
    // Transfer Management
    // =========================================================================

    void ClusterBuffer::flushPendingTransfers()
    {
        if (transferManager && transferManager->hasPendingTransfers())
        {
            transferManager->waitAll();
        }
    }

} // namespace render::gpudriven
