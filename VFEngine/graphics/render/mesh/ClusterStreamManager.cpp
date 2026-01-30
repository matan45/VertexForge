#include "ClusterStreamManager.hpp"
#include "../gpudriven/ClusterBuffer.hpp"
#include "../gpudriven/MeshletBuffer.hpp"
#include "../gpudriven/MergedMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ClusterDAGTypes.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_set>

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

        --it->second.referenceCount;

        // THREAD SAFETY: Only erase when both refCount is 0 AND no async reads are pending.
        // This prevents use-after-free when async threads are accessing the handle.
        if (it->second.canBeErased())
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

    void ClusterStreamManager::openMeshStreamLocked(const std::string& meshPath, const MeshStatesLock& lock)
    {
        // THREAD SAFETY: Verify lock is actually held (debug assertion)
        assert(lock.owns_lock() && "meshStatesMutex must be held");
        (void)lock;  // Suppress unused parameter warning in release builds

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
        scheduleInitialClustersLocked(meshPath, lock);
    }

    void ClusterStreamManager::scheduleInitialClustersLocked(const std::string& meshPath, const MeshStatesLock& lock)
    {
        // THREAD SAFETY: Verify lock is actually held (debug assertion)
        assert(lock.owns_lock() && "meshStatesMutex must be held");
        (void)lock;  // Suppress unused parameter warning in release builds

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
            MeshStatesLock lock(meshStatesMutex);
            for (auto& [path, state] : meshStates)
            {
                if (!state.headerParsed && state.referenceCount > 0)
                {
                    openMeshStreamLocked(path, lock);
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

            // Update internal tracking and increment pending async counter
            {
                std::lock_guard<std::mutex> meshLock(meshStatesMutex);
                auto meshIt = meshStates.find(request.meshPath);
                if (meshIt != meshStates.end())
                {
                    std::string key = makeSubmeshKey(request.submeshName, request.submeshIndex);
                    auto& submeshState = meshIt->second.submeshStates[key];
                    submeshState.state = gpudriven::ClusterStreamState::Streaming;
                    submeshState.inQueue = false;

                    // THREAD SAFETY: Increment pending async reads to prevent mesh state
                    // from being erased while async operation is in flight
                    meshIt->second.pendingAsyncReads++;
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
        // THREAD SAFETY: Decrement pending async reads counter now that async operation is complete.
        // This must happen regardless of success/failure to maintain correct count.
        // Also check if mesh state should be cleaned up (deferred from releaseMesh).
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto it = meshStates.find(result.meshPath);
            if (it != meshStates.end())
            {
                if (it->second.pendingAsyncReads > 0)
                {
                    it->second.pendingAsyncReads--;
                }

                // Check if mesh was released while async was in flight
                if (it->second.canBeErased())
                {
                    clusterBuffer.evictMesh(result.meshPath);
                    for (const auto& [key, submeshState] : it->second.submeshStates)
                    {
                        if (submeshState.gpuMemoryUsed > 0)
                        {
                            currentGPUMemoryUsed -= submeshState.gpuMemoryUsed;
                        }
                    }
                    meshStates.erase(it);
                    return;  // Mesh was released, don't process result
                }
            }
        }

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

                // VK-300: Mark cluster DAG as ready in MergedMeshBuffer
                // This enables hasRenderableLOD() to return true for DAG-only submeshes
                if (mergedMeshBuffer)
                {
                    mergedMeshBuffer->markClusterDAGReady(upload.meshPath, upload.submeshName, upload.submeshIndex);
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

                // THREAD SAFETY: Minimize lock scope - only hold lock to get handle pointer.
                // The disk I/O operation (readClusterDAG) happens OUTSIDE the lock to avoid
                // blocking the main thread's update() calls for long periods.
                //
                // SAFETY NOTE: MeshStreamHandle is owned by meshStates and only freed when
                // canBeErased() returns true (refCount == 0 AND pendingAsyncReads == 0).
                // processStreamingQueue() increments pendingAsyncReads before starting this
                // async operation, ensuring the handle remains valid until handleCompletedRead()
                // decrements the counter.
                resource::MeshStreamHandle* handle = nullptr;
                {
                    std::lock_guard<std::mutex> lock(meshStatesMutex);
                    auto it = meshStates.find(meshPath);
                    if (it != meshStates.end() && it->second.handle)
                    {
                        handle = it->second.handle.get();
                    }
                }

                // Perform disk I/O outside the lock
                if (handle)
                {
                    result.dagData = std::make_unique<resource::ClusterDAGData>();
                    result.success = handle->readClusterDAG(submeshIndex, *result.dagData);
                }

                return result;
            });
    }

    // =========================================================================
    // Priority Management
    // =========================================================================

    void ClusterStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        // VK-298 Performance Fix: Avoid O(n log n) priority rebuild every frame
        // Only update when:
        // 1. Enough frames have passed since last update, OR
        // 2. Camera has moved significantly

        uint64_t framesSinceUpdate = currentFrame - lastPriorityUpdateFrame;
        float cameraMoved = glm::length(cameraPos - lastCameraPos);

        bool shouldUpdate = false;

        // Time-based throttle: update at most every N frames
        if (framesSinceUpdate >= priorityUpdateFrameInterval)
        {
            shouldUpdate = true;
        }

        // Movement-based trigger: force update if camera moved significantly
        // This ensures responsiveness when user is actively navigating
        if (cameraMoved >= cameraMovementThreshold)
        {
            shouldUpdate = true;
        }

        if (!shouldUpdate)
        {
            return;
        }

        // Track update state
        lastPriorityUpdateFrame = currentFrame;
        lastCameraPos = cameraPos;

        // Rebuild priority queue with updated priorities
        std::lock_guard<std::mutex> queueLock(queueMutex);

        if (streamingQueue.empty())
        {
            return;
        }

        // Extract all requests
        std::vector<ClusterStreamingRequest> requests;
        requests.reserve(streamingQueue.size());  // Avoid reallocs
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
        // Clamp to reasonable range to prevent numerical issues
        float screenErrorPriority = std::clamp(request.screenError, 0.0f, 1000.0f);

        // Distance factor (closer = higher priority)
        // Clamp to prevent numerical instability with very distant objects
        // Max factor of 11.0 corresponds to ~1000 unit distance
        float distance = glm::length(request.worldCenter - cameraPos);
        float distanceFactor = 1.0f + std::min(distance * 0.01f, 10.0f);

        // Screen coverage (larger = higher priority)
        // Already bounded: coverage in [0, boundingRadius], factor in (0, 1]
        float screenCoverage = request.boundingRadius / std::max(1.0f, distance);
        float coverageFactor = 1.0f / (1.0f + screenCoverage * 10.0f);

        // Recency bonus (recently visible = higher priority)
        // Already bounded: factor in [1.0, 2.0]
        uint64_t framesSinceVisible = currentFrame - request.lastVisibleFrame;
        float recencyFactor = 1.0f + std::min(static_cast<float>(framesSinceVisible) * 0.01f, 1.0f);

        // Combined priority (lower = higher priority)
        // With all factors bounded, result is stable and comparable
        return screenErrorPriority * distanceFactor * coverageFactor * recencyFactor;
    }

    // =========================================================================
    // Memory Budget & Eviction
    // =========================================================================

    void ClusterStreamManager::evictLRUClustersIfNeeded(size_t requiredBytes)
    {
        // VK-296: Delegate to enhanced group eviction strategy
        evictWithGroupStrategy(requiredBytes);
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
    // VK-296: Enhanced Eviction with Multi-Factor Priority and Group Strategy
    // =========================================================================

    float ClusterStreamManager::calculateEvictionPriority(const StreamingUnitState& state) const
    {
        // Recency score: frames since last visible (normalized 0-1)
        // Higher framesSinceVisible = higher eviction priority (more likely to evict)
        uint64_t framesSinceVisible = currentFrame - state.lastVisibleFrame;
        float recencyScore = std::min(1.0f,
            static_cast<float>(framesSinceVisible) /
            static_cast<float>(evictionConfig.recencyProtectionFrames * 4));

        // Screen error score: higher error = less detailed = more likely to evict
        // Normalize against typical error range (0 to 100 pixels)
        float errorScore = std::min(1.0f, state.lastScreenError / 100.0f);

        // Memory size score: larger allocations = more valuable to evict
        // Normalize against typical streaming unit size (~100KB)
        float memoryScore = std::min(1.0f,
            static_cast<float>(state.gpuMemoryUsed) / (100.0f * 1024.0f));

        // Combined priority: LOWER value = EVICT FIRST
        // Invert recency: old (high recencyScore) = low priority value = evict first
        // Use error directly: high error = high errorScore = we want to evict, so invert
        // Use memory directly: large = high memoryScore = valuable to evict, so invert
        float priority =
            (1.0f - recencyScore) * evictionConfig.recencyWeight +
            (1.0f - errorScore) * evictionConfig.screenErrorWeight +
            (1.0f - memoryScore) * evictionConfig.memorySizeWeight;

        return priority;  // Range 0-1, lower = evict first
    }

    bool ClusterStreamManager::isProtectedFromEviction(const StreamingUnitState& state) const
    {
        // Recently visible protection
        if (currentFrame - state.lastVisibleFrame < evictionConfig.recencyProtectionFrames)
        {
            return true;
        }

        // Not loaded = nothing to evict
        if (!state.isLoaded)
        {
            return true;
        }

        return false;
    }

    std::vector<EvictionCandidate> ClusterStreamManager::buildEvictionCandidates() const
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        std::vector<EvictionCandidate> candidates;

        for (const auto& [meshPath, meshState] : meshStates)
        {
            for (const auto& [submeshKey, submeshState] : meshState.submeshStates)
            {
                // Only consider ready (loaded) submeshes
                if (submeshState.state != gpudriven::ClusterStreamState::Ready)
                {
                    continue;
                }

                // Check if we have per-unit states
                if (evictionConfig.enableGroupEviction && !submeshState.unitStates.empty())
                {
                    // Build candidates at streaming unit granularity
                    for (const auto& unitState : submeshState.unitStates)
                    {
                        if (!unitState.isLoaded)
                        {
                            continue;
                        }

                        if (isProtectedFromEviction(unitState))
                        {
                            continue;
                        }

                        EvictionCandidate candidate;
                        candidate.priority = calculateEvictionPriority(unitState);
                        candidate.memorySize = unitState.gpuMemoryUsed;
                        candidate.meshPath = meshPath;
                        candidate.submeshKey = submeshKey;
                        candidate.unitIndex = unitState.unitIndex;
                        candidates.push_back(candidate);
                    }
                }
                else
                {
                    // Fall back to whole-submesh eviction
                    // Check recency protection
                    if (currentFrame - submeshState.lastVisibleFrame < evictionConfig.recencyProtectionFrames)
                    {
                        continue;
                    }

                    // Create a temporary unit state for priority calculation
                    StreamingUnitState tempState;
                    tempState.lastVisibleFrame = submeshState.lastVisibleFrame;
                    tempState.lastScreenError = submeshState.lastScreenError;
                    tempState.gpuMemoryUsed = submeshState.gpuMemoryUsed;
                    tempState.isLoaded = true;

                    EvictionCandidate candidate;
                    candidate.priority = calculateEvictionPriority(tempState);
                    candidate.memorySize = submeshState.gpuMemoryUsed;
                    candidate.meshPath = meshPath;
                    candidate.submeshKey = submeshKey;
                    candidate.unitIndex = 0xFFFFFFFF;  // Whole submesh
                    candidates.push_back(candidate);
                }
            }
        }

        return candidates;
    }

    void ClusterStreamManager::filterByDependencies(std::vector<EvictionCandidate>& candidates) const
    {
        if (!evictionConfig.respectDependencies)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(meshStatesMutex);

        // Build set of loaded unit indices per submesh
        std::unordered_map<std::string, std::unordered_set<uint32_t>> loadedUnits;

        for (const auto& [meshPath, meshState] : meshStates)
        {
            for (const auto& [submeshKey, submeshState] : meshState.submeshStates)
            {
                std::string fullKey = meshPath + ":" + submeshKey;
                for (const auto& unitState : submeshState.unitStates)
                {
                    if (unitState.isLoaded)
                    {
                        loadedUnits[fullKey].insert(unitState.unitIndex);
                    }
                }
            }
        }

        // Remove candidates whose children are still loaded
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                [&](const EvictionCandidate& c) {
                    // Whole-submesh eviction doesn't have dependencies to check
                    if (c.unitIndex == 0xFFFFFFFF)
                    {
                        return false;
                    }

                    std::string fullKey = c.meshPath + ":" + c.submeshKey;

                    // Find submesh state
                    auto meshIt = meshStates.find(c.meshPath);
                    if (meshIt == meshStates.end())
                    {
                        return false;
                    }

                    auto subIt = meshIt->second.submeshStates.find(c.submeshKey);
                    if (subIt == meshIt->second.submeshStates.end())
                    {
                        return false;
                    }

                    // Check if any loaded unit depends on this candidate
                    for (const auto& unitState : subIt->second.unitStates)
                    {
                        if (unitState.isLoaded &&
                            unitState.dependsOnUnit == c.unitIndex)
                        {
                            // This unit has a loaded child - can't evict
                            return true;
                        }
                    }

                    return false;
                }),
            candidates.end()
        );
    }

    std::vector<std::vector<EvictionCandidate>> ClusterStreamManager::groupAdjacentCandidates(
        const std::vector<EvictionCandidate>& candidates) const
    {
        std::vector<std::vector<EvictionCandidate>> groups;

        if (candidates.empty())
        {
            return groups;
        }

        if (!evictionConfig.enableGroupEviction)
        {
            // No grouping - each candidate is its own group
            for (const auto& c : candidates)
            {
                groups.push_back({c});
            }
            return groups;
        }

        // Group by meshPath + submeshKey, keeping adjacent unit indices together
        std::unordered_map<std::string, std::vector<EvictionCandidate>> bySubmesh;

        for (const auto& c : candidates)
        {
            std::string key = c.meshPath + ":" + c.submeshKey;
            bySubmesh[key].push_back(c);
        }

        // For each submesh, sort by unit index and group adjacent units
        for (auto& [key, submeshCandidates] : bySubmesh)
        {
            // Sort by unit index for spatial coherence
            std::sort(submeshCandidates.begin(), submeshCandidates.end(),
                [](const EvictionCandidate& a, const EvictionCandidate& b) {
                    return a.unitIndex < b.unitIndex;
                });

            // Group adjacent units (gap of 1 is allowed)
            std::vector<EvictionCandidate> currentGroup;
            uint32_t lastIndex = 0xFFFFFFFF;

            for (const auto& c : submeshCandidates)
            {
                bool adjacent = (lastIndex == 0xFFFFFFFF) ||
                               (c.unitIndex <= lastIndex + 2);  // Allow gap of 1

                if (adjacent)
                {
                    currentGroup.push_back(c);
                }
                else
                {
                    if (!currentGroup.empty())
                    {
                        groups.push_back(std::move(currentGroup));
                        currentGroup.clear();
                    }
                    currentGroup.push_back(c);
                }

                lastIndex = c.unitIndex;
            }

            if (!currentGroup.empty())
            {
                groups.push_back(std::move(currentGroup));
            }
        }

        // Sort groups by average priority (lowest first = evict first)
        std::sort(groups.begin(), groups.end(),
            [](const std::vector<EvictionCandidate>& a, const std::vector<EvictionCandidate>& b) {
                float avgA = 0.0f, avgB = 0.0f;
                for (const auto& c : a) avgA += c.priority;
                for (const auto& c : b) avgB += c.priority;
                avgA /= static_cast<float>(a.size());
                avgB /= static_cast<float>(b.size());
                return avgA < avgB;  // Lower priority = evict first
            });

        return groups;
    }

    size_t ClusterStreamManager::evictGroup(const std::vector<EvictionCandidate>& group)
    {
        size_t totalFreed = 0;

        for (const auto& candidate : group)
        {
            std::string submeshName;
            uint32_t submeshIdx;
            if (!parseSubmeshKey(candidate.submeshKey, submeshName, submeshIdx))
            {
                continue;
            }

            // Evict from GPU (whole submesh for now - unit-level eviction requires ClusterBuffer changes)
            if (clusterBuffer.evictClusterDAG(candidate.meshPath, submeshName, submeshIdx))
            {
                size_t freedBytes = candidate.memorySize;
                currentGPUMemoryUsed -= freedBytes;
                totalFreed += freedBytes;

                // Update internal state
                {
                    std::lock_guard<std::mutex> lock(meshStatesMutex);
                    auto meshIt = meshStates.find(candidate.meshPath);
                    if (meshIt != meshStates.end())
                    {
                        auto& submeshState = meshIt->second.submeshStates[candidate.submeshKey];
                        submeshState.state = gpudriven::ClusterStreamState::Evicted;
                        submeshState.gpuMemoryUsed = 0;

                        // Mark unit as not loaded if applicable
                        if (candidate.unitIndex != 0xFFFFFFFF &&
                            candidate.unitIndex < submeshState.unitStates.size())
                        {
                            submeshState.unitStates[candidate.unitIndex].isLoaded = false;
                        }
                    }
                }

                // Update statistics
                evictionStats.totalEvictions++;
                evictionStats.totalBytesEvicted += freedBytes;

                loggerWarning("ClusterStreamManager: Evicted {}:{} unit {} (freed {} KB)",
                           candidate.meshPath, candidate.submeshKey, candidate.unitIndex, freedBytes / 1024);
            }
        }

        if (group.size() > 1)
        {
            evictionStats.groupEvictions++;
        }

        return totalFreed;
    }

    void ClusterStreamManager::evictWithGroupStrategy(size_t requiredBytes)
    {
        if (currentGPUMemoryUsed + requiredBytes <= maxGPUMemoryBudget)
        {
            return;  // No eviction needed
        }

        size_t bytesToFree = (currentGPUMemoryUsed + requiredBytes) - maxGPUMemoryBudget;
        bytesToFree = std::max(bytesToFree,
            static_cast<size_t>(maxGPUMemoryBudget * evictionConfig.minEvictionPercent));

        // Step 1: Build candidates at streaming unit granularity
        auto candidates = buildEvictionCandidates();

        if (candidates.empty())
        {
            loggerWarning("ClusterStreamManager: No eviction candidates available, memory budget exceeded");
            return;
        }

        // Step 2: Filter by dependencies (respect parent-before-children)
        filterByDependencies(candidates);

        if (candidates.empty())
        {
            evictionStats.dependencyBlocks++;
            loggerWarning("ClusterStreamManager: All candidates blocked by dependencies");
            return;
        }

        // Step 3: Sort by priority (lowest first = evict first)
        std::sort(candidates.begin(), candidates.end());

        // Step 4: Group adjacent candidates in same submesh
        auto groups = groupAdjacentCandidates(candidates);

        // Step 5: Evict groups until memory target met
        size_t freedBytes = 0;
        float prioritySum = 0.0f;
        uint32_t evictCount = 0;

        for (const auto& group : groups)
        {
            if (freedBytes >= bytesToFree)
            {
                break;
            }

            size_t groupFreed = evictGroup(group);
            freedBytes += groupFreed;

            for (const auto& c : group)
            {
                prioritySum += c.priority;
                evictCount++;
            }
        }

        // Update average eviction priority statistic
        if (evictCount > 0)
        {
            evictionStats.averageEvictionPriority =
                (evictionStats.averageEvictionPriority * 0.9f) +
                (prioritySum / static_cast<float>(evictCount)) * 0.1f;
        }

        loggerWarning("ClusterStreamManager: Evicted {} KB to free {} KB required",
                   freedBytes / 1024, bytesToFree / 1024);
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
