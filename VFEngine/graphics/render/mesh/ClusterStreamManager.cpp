#include "ClusterStreamManager.hpp"
#include "../gpudriven/ClusterBuffer.hpp"
#include "../gpudriven/MeshletBuffer.hpp"
#include "../../core/Device.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ClusterDAGTypes.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <chrono>

namespace render::mesh
{
    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    ClusterStreamManager::ClusterStreamManager(core::Device& device, gpudriven::ClusterBuffer& clusterBuffer)
        : device(device)
        , clusterBuffer(clusterBuffer)
    {
    }

    ClusterStreamManager::~ClusterStreamManager()
    {
        // Wait for all pending async reads to complete
        std::lock_guard<std::mutex> lock(pendingReadsMutex);
        for (auto& future : pendingReads)
        {
            if (future.valid())
            {
                future.wait();
            }
        }
    }

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    void ClusterStreamManager::requestMesh(const std::string& meshPath)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it != meshStates.end())
        {
            it->second.referenceCount++;
            return;
        }

        ClusterMeshState state;
        state.referenceCount = 1;
        state.headerParsed = false;
        meshStates[meshPath] = std::move(state);
    }

    void ClusterStreamManager::releaseMesh(const std::string& meshPath)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it == meshStates.end())
        {
            return;
        }

        if (--it->second.referenceCount == 0)
        {
            // Evict all cluster data for this mesh
            clusterBuffer.evictMesh(meshPath);

            // Update memory tracking
            for (const auto& [key, submeshState] : it->second.submeshStates)
            {
                if (submeshState.gpuMemoryUsed > 0)
                {
                    currentGPUMemoryUsed -= submeshState.gpuMemoryUsed;
                }
            }

            meshStates.erase(it);
        }
    }

    // =========================================================================
    // Mesh Stream Management
    // =========================================================================

    void ClusterStreamManager::openMeshStream(const std::string& meshPath)
    {
        // Note: meshStatesMutex must be held by caller
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || it->second.headerParsed)
        {
            return;
        }

        auto& state = it->second;

        state.handle = resource::MeshStreamResource::openStream(meshPath);
        if (!state.handle)
        {
            loggerError("ClusterStreamManager: Failed to open stream for {}", meshPath);
            return;
        }

        // Check if mesh has cluster DAG data
        if (!state.handle->hasClusterDAGData())
        {
            loggerWarning("ClusterStreamManager: Mesh {} has no cluster DAG data", meshPath);
            state.headerParsed = true;
            return;
        }

        state.headerParsed = true;

        // Reserve space in ClusterBuffer
        const auto& header = state.handle->getHeader();
        auto* alloc = clusterBuffer.reserveClusters(meshPath, header);
        if (!alloc)
        {
            loggerError("ClusterStreamManager: Failed to reserve cluster space for {}", meshPath);
            return;
        }

        loggerWarning("ClusterStreamManager: Reserved cluster space for {} ({} submeshes with DAG)",
                    meshPath, header.numSubmeshes);

        // Schedule initial cluster loading
        scheduleInitialClusters(meshPath);
    }

    void ClusterStreamManager::scheduleInitialClusters(const std::string& meshPath)
    {
        // Note: meshStatesMutex must be held by caller
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || !it->second.handle)
        {
            return;
        }

        const auto& header = it->second.handle->getHeader();
        auto& meshState = it->second;

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& submeshInfo = header.submeshes[subIdx];

            // Skip submeshes without cluster DAG data
            if (!submeshInfo.hasClusterDAGData)
            {
                continue;
            }

            std::string submeshKey = makeSubmeshKey(submeshInfo.name, subIdx);

            // Initialize submesh state
            ClusterSubmeshState& submeshState = meshState.submeshStates[submeshKey];
            submeshState.state = gpudriven::ClusterStreamState::NotRequested;
            submeshState.lastVisibleFrame = 0;
            submeshState.lastScreenError = FLT_MAX;
            submeshState.gpuMemoryUsed = 0;
            submeshState.inQueue = true;

            // Create streaming request with high priority (initial load)
            ClusterStreamingRequest request;
            request.meshPath = meshPath;
            request.submeshName = submeshInfo.name;
            request.submeshIndex = subIdx;
            request.priority = 1000.0f;  // High priority for initial load
            request.screenError = 0.0f;
            request.worldCenter = glm::vec3(submeshInfo.clusterDAGInfo.boundingSphere);
            request.boundingRadius = submeshInfo.clusterDAGInfo.boundingSphere.w;
            request.lastVisibleFrame = currentFrame;

            {
                std::lock_guard<std::mutex> queueLock(queueMutex);
                streamingQueue.push(request);
            }

            // Update state
            clusterBuffer.setStreamState(meshPath, submeshInfo.name, subIdx,
                                         gpudriven::ClusterStreamState::Queued);
            submeshState.state = gpudriven::ClusterStreamState::Queued;
        }
    }

    // =========================================================================
    // Frame Update
    // =========================================================================

    void ClusterStreamManager::update(const glm::vec3& cameraPos, uint64_t frameIndex)
    {
        currentFrame = frameIndex;
        bytesStreamedThisFrame = 0;

        // Step 1: Open pending mesh streams
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            for (auto& [path, state] : meshStates)
            {
                if (!state.headerParsed && state.referenceCount > 0)
                {
                    openMeshStream(path);
                }
            }
        }

        // Step 2: Process completed async reads -> queue uploads
        processPendingReads();

        // Step 3: Process pending GPU uploads (main thread)
        processPendingUploads();

        // Step 4: Update priorities based on camera position
        updatePriorities(cameraPos);

        // Step 5: Process streaming queue -> start new async reads
        processStreamingQueue();
    }

    // =========================================================================
    // Streaming Pipeline
    // =========================================================================

    void ClusterStreamManager::processStreamingQueue()
    {
        std::lock_guard<std::mutex> readLock(pendingReadsMutex);
        std::lock_guard<std::mutex> queueLock(queueMutex);

        while (pendingReads.size() < maxPendingReads &&
               !streamingQueue.empty() &&
               bytesStreamedThisFrame < maxBytesPerFrame)
        {
            ClusterStreamingRequest request = streamingQueue.top();
            streamingQueue.pop();

            // Check current state - skip if already loaded or loading
            auto currentState = clusterBuffer.getStreamState(
                request.meshPath, request.submeshName, request.submeshIndex);

            if (currentState != gpudriven::ClusterStreamState::Queued &&
                currentState != gpudriven::ClusterStreamState::Evicted)
            {
                // Already streaming, ready, or in another state
                continue;
            }

            // Update state to streaming
            clusterBuffer.setStreamState(request.meshPath, request.submeshName,
                                         request.submeshIndex, gpudriven::ClusterStreamState::Streaming);

            // Update internal tracking
            {
                std::lock_guard<std::mutex> meshLock(meshStatesMutex);
                auto meshIt = meshStates.find(request.meshPath);
                if (meshIt != meshStates.end())
                {
                    std::string key = makeSubmeshKey(request.submeshName, request.submeshIndex);
                    auto& submeshState = meshIt->second.submeshStates[key];
                    submeshState.state = gpudriven::ClusterStreamState::Streaming;
                    submeshState.inQueue = false;
                }
            }

            // Start async read
            auto future = asyncReadClusterDAG(request.meshPath, request.submeshName, request.submeshIndex);
            pendingReads.push_back(std::move(future));
        }
    }

    void ClusterStreamManager::processPendingReads()
    {
        std::lock_guard<std::mutex> lock(pendingReadsMutex);

        auto it = pendingReads.begin();
        while (it != pendingReads.end())
        {
            // Check if ready without blocking
            auto status = it->wait_for(std::chrono::milliseconds(0));
            if (status == std::future_status::ready)
            {
                ClusterStreamingResult result = it->get();
                it = pendingReads.erase(it);

                // Handle the completed read
                handleCompletedRead(std::move(result));
            }
            else
            {
                ++it;
            }
        }
    }

    void ClusterStreamManager::handleCompletedRead(ClusterStreamingResult&& result)
    {
        if (!result.success || !result.dagData)
        {
            loggerError("ClusterStreamManager: Failed to read cluster DAG for {}:{}#{}",
                       result.meshPath, result.submeshName, result.submeshIndex);

            // Reset state
            clusterBuffer.setStreamState(result.meshPath, result.submeshName,
                                         result.submeshIndex, gpudriven::ClusterStreamState::NotRequested);
            return;
        }

        // Calculate memory required
        size_t memoryRequired = result.dagData->clusters.size() * sizeof(gpudriven::GPUCluster);
        memoryRequired += sizeof(gpudriven::GPUClusterDAGHeader);

        // Check memory budget and evict if necessary
        evictLRUClustersIfNeeded(memoryRequired);

        // Queue for GPU upload
        ClusterPendingUpload upload;
        upload.meshPath = std::move(result.meshPath);
        upload.submeshName = std::move(result.submeshName);
        upload.submeshIndex = result.submeshIndex;
        upload.dagData = std::move(result.dagData);

        // Get base meshlet offset from meshlet buffer allocation
        upload.baseMeshletOffset = 0;
        if (meshletBuffer)
        {
            const auto* alloc = meshletBuffer->getAllocation(
                upload.meshPath, upload.submeshName, upload.submeshIndex);
            if (alloc && alloc->lods[0].isAllocated)
            {
                upload.baseMeshletOffset = alloc->lods[0].meshletOffset;
            }
        }

        // Update state to uploading
        clusterBuffer.setStreamState(upload.meshPath, upload.submeshName,
                                     upload.submeshIndex, gpudriven::ClusterStreamState::Uploading);

        std::lock_guard<std::mutex> uploadLock(uploadMutex);
        pendingUploads.push_back(std::move(upload));
    }

    void ClusterStreamManager::processPendingUploads()
    {
        std::lock_guard<std::mutex> lock(uploadMutex);

        auto it = pendingUploads.begin();
        while (it != pendingUploads.end())
        {
            // Check bandwidth budget
            if (bytesStreamedThisFrame >= maxBytesPerFrame)
            {
                break;
            }

            // Check upload count limit
            if (std::distance(pendingUploads.begin(), it) >= static_cast<ptrdiff_t>(maxPendingUploads))
            {
                break;
            }

            auto& upload = *it;

            // Calculate memory for this upload
            size_t memoryUsed = upload.dagData->clusters.size() * sizeof(gpudriven::GPUCluster);
            memoryUsed += sizeof(gpudriven::GPUClusterDAGHeader);

            // Perform GPU upload
            bool uploaded = clusterBuffer.uploadClusterDAG(
                upload.meshPath,
                upload.submeshName,
                upload.submeshIndex,
                *upload.dagData,
                upload.baseMeshletOffset
            );

            if (uploaded)
            {
                bytesStreamedThisFrame += memoryUsed;
                currentGPUMemoryUsed += memoryUsed;

                // Update internal state
                {
                    std::lock_guard<std::mutex> meshLock(meshStatesMutex);
                    auto meshIt = meshStates.find(upload.meshPath);
                    if (meshIt != meshStates.end())
                    {
                        std::string key = makeSubmeshKey(upload.submeshName, upload.submeshIndex);
                        auto& submeshState = meshIt->second.submeshStates[key];
                        submeshState.state = gpudriven::ClusterStreamState::Ready;
                        submeshState.gpuMemoryUsed = memoryUsed;
                    }
                }

                loggerWarning("ClusterStreamManager: Uploaded cluster DAG for {}:{}#{} ({} KB)",
                           upload.meshPath, upload.submeshName, upload.submeshIndex, memoryUsed / 1024);
            }
            else
            {
                loggerError("ClusterStreamManager: Failed to upload cluster DAG for {}:{}#{}",
                           upload.meshPath, upload.submeshName, upload.submeshIndex);

                clusterBuffer.setStreamState(upload.meshPath, upload.submeshName,
                                             upload.submeshIndex, gpudriven::ClusterStreamState::NotRequested);
            }

            it = pendingUploads.erase(it);
        }
    }

    // =========================================================================
    // Async I/O
    // =========================================================================

    std::future<ClusterStreamingResult> ClusterStreamManager::asyncReadClusterDAG(
        const std::string& meshPath,
        const std::string& submeshName,
        uint32_t submeshIndex)
    {
        return std::async(std::launch::async,
            [this, meshPath, submeshName, submeshIndex]()
            {
                ClusterStreamingResult result;
                result.meshPath = meshPath;
                result.submeshName = submeshName;
                result.submeshIndex = submeshIndex;
                result.success = false;

                std::lock_guard<std::mutex> lock(meshStatesMutex);
                auto it = meshStates.find(meshPath);
                if (it != meshStates.end() && it->second.handle)
                {
                    result.dagData = std::make_unique<resource::ClusterDAGData>();
                    result.success = it->second.handle->readClusterDAG(
                        submeshIndex, *result.dagData);
                }

                return result;
            });
    }

    // =========================================================================
    // Priority Management
    // =========================================================================

    void ClusterStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        // Rebuild priority queue with updated priorities
        std::lock_guard<std::mutex> queueLock(queueMutex);

        if (streamingQueue.empty())
        {
            return;
        }

        // Extract all requests
        std::vector<ClusterStreamingRequest> requests;
        while (!streamingQueue.empty())
        {
            requests.push_back(streamingQueue.top());
            streamingQueue.pop();
        }

        // Recalculate priorities and reinsert
        for (auto& request : requests)
        {
            request.priority = calculatePriority(request, cameraPos);
            streamingQueue.push(request);
        }
    }

    float ClusterStreamManager::calculatePriority(const ClusterStreamingRequest& request,
                                                   const glm::vec3& cameraPos) const
    {
        // Screen error is primary factor (lower = finer detail = higher priority)
        float screenErrorPriority = request.screenError;

        // Distance factor (closer = higher priority)
        float distance = glm::length(request.worldCenter - cameraPos);
        float distanceFactor = 1.0f + distance * 0.01f;

        // Screen coverage (larger = higher priority)
        float screenCoverage = request.boundingRadius / std::max(1.0f, distance);
        float coverageFactor = 1.0f / (1.0f + screenCoverage * 10.0f);

        // Recency bonus (recently visible = higher priority)
        uint64_t framesSinceVisible = currentFrame - request.lastVisibleFrame;
        float recencyFactor = 1.0f + std::min(static_cast<float>(framesSinceVisible) * 0.01f, 1.0f);

        // Combined priority (lower = higher priority)
        return screenErrorPriority * distanceFactor * coverageFactor * recencyFactor;
    }

    // =========================================================================
    // Memory Budget & Eviction
    // =========================================================================

    void ClusterStreamManager::evictLRUClustersIfNeeded(size_t requiredBytes)
    {
        if (currentGPUMemoryUsed + requiredBytes <= maxGPUMemoryBudget)
        {
            return;  // No eviction needed
        }

        size_t bytesToFree = (currentGPUMemoryUsed + requiredBytes) - maxGPUMemoryBudget;
        bytesToFree = std::max(bytesToFree, maxGPUMemoryBudget / 10);  // Free at least 10%

        auto candidates = getLRUCandidates();

        for (const auto& [meshPath, submeshKey] : candidates)
        {
            if (bytesToFree <= 0)
            {
                break;
            }

            std::lock_guard<std::mutex> lock(meshStatesMutex);

            auto meshIt = meshStates.find(meshPath);
            if (meshIt == meshStates.end())
            {
                continue;
            }

            auto& submeshState = meshIt->second.submeshStates[submeshKey];
            if (submeshState.state != gpudriven::ClusterStreamState::Ready)
            {
                continue;
            }

            // Don't evict recently visible clusters (last 60 frames ~ 1 second at 60fps)
            if (currentFrame - submeshState.lastVisibleFrame < 60)
            {
                continue;
            }

            // Parse submesh key
            std::string submeshName;
            uint32_t submeshIdx;
            if (!parseSubmeshKey(submeshKey, submeshName, submeshIdx))
            {
                continue;
            }

            // Evict from GPU
            if (clusterBuffer.evictClusterDAG(meshPath, submeshName, submeshIdx))
            {
                size_t freedBytes = submeshState.gpuMemoryUsed;
                currentGPUMemoryUsed -= freedBytes;
                bytesToFree -= std::min(bytesToFree, freedBytes);

                submeshState.state = gpudriven::ClusterStreamState::Evicted;
                submeshState.gpuMemoryUsed = 0;

                loggerWarning("ClusterStreamManager: Evicted {}:{} (freed {} KB)",
                           meshPath, submeshKey, freedBytes / 1024);
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> ClusterStreamManager::getLRUCandidates() const
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        // Build list of (lastVisibleFrame, meshPath, submeshKey)
        std::vector<std::tuple<uint64_t, std::string, std::string>> scored;

        for (const auto& [meshPath, meshState] : meshStates)
        {
            for (const auto& [submeshKey, submeshState] : meshState.submeshStates)
            {
                if (submeshState.state == gpudriven::ClusterStreamState::Ready)
                {
                    scored.emplace_back(submeshState.lastVisibleFrame, meshPath, submeshKey);
                }
            }
        }

        // Sort by frame (oldest first)
        std::sort(scored.begin(), scored.end());

        // Convert to result format
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(scored.size());
        for (const auto& [frame, mesh, key] : scored)
        {
            result.emplace_back(mesh, key);
        }

        return result;
    }

    // =========================================================================
    // Priority Requests
    // =========================================================================

    void ClusterStreamManager::requestStreamingUnit(const std::string& meshPath,
                                                     const std::string& submeshName,
                                                     uint32_t submeshIndex,
                                                     float screenError,
                                                     const glm::vec3& worldCenter,
                                                     float boundingRadius)
    {
        // Check if already ready or in queue
        auto currentState = clusterBuffer.getStreamState(meshPath, submeshName, submeshIndex);
        if (currentState == gpudriven::ClusterStreamState::Ready ||
            currentState == gpudriven::ClusterStreamState::Streaming ||
            currentState == gpudriven::ClusterStreamState::Uploading)
        {
            return;
        }

        // Check internal queue state
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto meshIt = meshStates.find(meshPath);
            if (meshIt != meshStates.end())
            {
                std::string key = makeSubmeshKey(submeshName, submeshIndex);
                auto subIt = meshIt->second.submeshStates.find(key);
                if (subIt != meshIt->second.submeshStates.end() && subIt->second.inQueue)
                {
                    return;  // Already in queue
                }
            }
        }

        // Create streaming request
        ClusterStreamingRequest request;
        request.meshPath = meshPath;
        request.submeshName = submeshName;
        request.submeshIndex = submeshIndex;
        request.priority = screenError;  // Will be recalculated in updatePriorities
        request.screenError = screenError;
        request.worldCenter = worldCenter;
        request.boundingRadius = boundingRadius;
        request.lastVisibleFrame = currentFrame;

        {
            std::lock_guard<std::mutex> queueLock(queueMutex);
            streamingQueue.push(request);
        }

        // Update state
        clusterBuffer.setStreamState(meshPath, submeshName, submeshIndex,
                                     gpudriven::ClusterStreamState::Queued);

        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto meshIt = meshStates.find(meshPath);
            if (meshIt != meshStates.end())
            {
                std::string key = makeSubmeshKey(submeshName, submeshIndex);
                auto& submeshState = meshIt->second.submeshStates[key];
                submeshState.state = gpudriven::ClusterStreamState::Queued;
                submeshState.inQueue = true;
            }
        }
    }

    void ClusterStreamManager::markVisible(const std::string& meshPath,
                                            const std::string& submeshName,
                                            uint32_t submeshIndex,
                                            float screenError)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto meshIt = meshStates.find(meshPath);
        if (meshIt == meshStates.end())
        {
            return;
        }

        std::string key = makeSubmeshKey(submeshName, submeshIndex);
        auto& submeshState = meshIt->second.submeshStates[key];
        submeshState.lastVisibleFrame = currentFrame;
        submeshState.lastScreenError = screenError;
    }

    // =========================================================================
    // Query State
    // =========================================================================

    bool ClusterStreamManager::isDAGReady(const std::string& meshPath,
                                           const std::string& submeshName,
                                           uint32_t submeshIndex) const
    {
        return clusterBuffer.getStreamState(meshPath, submeshName, submeshIndex) ==
               gpudriven::ClusterStreamState::Ready;
    }

    bool ClusterStreamManager::hasClusterData(const std::string& meshPath) const
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it == meshStates.end())
        {
            return false;
        }

        return it->second.headerParsed && it->second.handle && it->second.handle->hasClusterDAGData();
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    uint32_t ClusterStreamManager::getPendingReadCount() const
    {
        std::lock_guard<std::mutex> lock(pendingReadsMutex);
        return static_cast<uint32_t>(pendingReads.size());
    }

    uint32_t ClusterStreamManager::getQueuedRequestCount() const
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        return static_cast<uint32_t>(streamingQueue.size());
    }

    uint32_t ClusterStreamManager::getPendingUploadCount() const
    {
        std::lock_guard<std::mutex> lock(uploadMutex);
        return static_cast<uint32_t>(pendingUploads.size());
    }

    // =========================================================================
    // Utility Methods
    // =========================================================================

    bool ClusterStreamManager::parseSubmeshKey(const std::string& key,
                                                std::string& outSubmeshName,
                                                uint32_t& outSubmeshIndex)
    {
        size_t hashPos = key.find('#');
        if (hashPos == std::string::npos)
        {
            return false;
        }

        outSubmeshName = key.substr(0, hashPos);
        try
        {
            outSubmeshIndex = static_cast<uint32_t>(std::stoul(key.substr(hashPos + 1)));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

} // namespace render::mesh
