#include "MeshStreamManager.hpp"
#include "../gpudriven/MergedMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace render::mesh {

    MeshStreamManager::MeshStreamManager(core::Device& device,
                                          gpudriven::MergedMeshBuffer& mergedBuffer)
        : device(device)
        , mergedBuffer(mergedBuffer) {
    }

    MeshStreamManager::~MeshStreamManager() {
        // Wait for any pending reads to complete
        for (auto& future : pendingReads) {
            if (future.valid()) {
                future.wait();
            }
        }
    }

    bool MeshStreamManager::requestMesh(const std::string& meshPath) {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it != meshStates.end()) {
            // Already tracking, increment reference
            it->second.referenceCount++;
            return true;
        }

        // New mesh - start streaming
        MeshStreamingState state;
        state.referenceCount = 1;
        state.headerParsed = false;
        meshStates[meshPath] = std::move(state);

        // Open stream and schedule initial LODs (done outside lock)
        // We'll do this in update() to avoid holding the lock too long
        return true;
    }

    void MeshStreamManager::releaseMesh(const std::string& meshPath) {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it != meshStates.end()) {
            it->second.referenceCount--;
            if (it->second.referenceCount == 0) {
                // No more references, but keep in cache for now
                // Could implement LRU eviction here
            }
        }
    }

    void MeshStreamManager::openMeshStream(const std::string& meshPath) {
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || it->second.headerParsed) {
            return;
        }

        auto& state = it->second;

        // Open stream handle
        state.handle = resource::MeshStreamResource::openStream(meshPath);
        if (!state.handle) {
            loggerError("MeshStreamManager: Failed to open stream for {}", meshPath);
            return;
        }

        state.headerParsed = true;

        // Reserve space in merged buffer
        const auto& header = state.handle->getHeader();
        auto* meshInfo = mergedBuffer.reserveMesh(meshPath, header);
        if (!meshInfo) {
            loggerError("MeshStreamManager: Failed to reserve space for {}", meshPath);
            state.handle.reset();
            state.headerParsed = false;
            return;
        }

        loggerInfo("MeshStreamManager: Opened stream for {} with {} submeshes",
                    meshPath, header.numSubmeshes);

        // Schedule LOD3 (lowest detail) for all submeshes first
        scheduleInitialLODs(meshPath);
    }

    void MeshStreamManager::scheduleInitialLODs(const std::string& meshPath) {
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || !it->second.handle) {
            return;
        }

        const auto& header = it->second.handle->getHeader();

        // Queue LOD3 (lowest detail) first for quick visibility
        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx) {
            const auto& submesh = header.submeshes[subIdx];

            // Start with LOD3 (highest priority for initial visibility)
            StreamingRequest request;
            request.meshPath = meshPath;
            request.submeshName = submesh.name;
            request.submeshIndex = subIdx;
            request.lodLevel = 3;  // LOD3 first
            request.priority = 1000.0f;  // High priority for initial load
            request.worldCenter = glm::vec3(0.0f);
            request.boundingRadius = 1.0f;

            streamingQueue.push(request);

            // Mark as queued
            auto* loc = mergedBuffer.getSubmeshLocationMutable(meshPath, submesh.name, subIdx);
            if (loc) {
                loc->lodStates[3] = gpudriven::LODStreamState::Queued;
            }
        }

        stats.lodsQueued += header.numSubmeshes;
    }

    void MeshStreamManager::update(const glm::vec3& cameraPos,
                                    const glm::mat4& viewProj,
                                    float deltaTime) {
        bytesStreamedThisFrame = 0;

        // Open streams for new meshes
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            for (auto& [path, state] : meshStates) {
                if (!state.headerParsed && state.referenceCount > 0) {
                    openMeshStream(path);
                }
            }
        }

        // Process completed reads
        processPendingReads();

        // Process completed uploads
        processPendingUploads();

        // Update priorities based on camera position
        updatePriorities(cameraPos, viewProj);

        // Start new streaming requests
        processStreamingQueue();

        // Update stats
        stats.bytesStreamedThisFrame = bytesStreamedThisFrame;
        stats.totalBytesStreamed += bytesStreamedThisFrame;
        stats.meshesTracked = static_cast<uint32_t>(meshStates.size());
        stats.lodsStreaming = static_cast<uint32_t>(pendingReads.size());
        stats.lodsUploading = static_cast<uint32_t>(pendingUploads.size());

        // Count ready meshes
        stats.meshesReady = 0;
        for (const auto& [path, state] : meshStates) {
            if (mergedBuffer.hasRenderableData(path)) {
                stats.meshesReady++;
            }
        }
    }

    void MeshStreamManager::processStreamingQueue() {
        // Limit concurrent reads
        while (pendingReads.size() < maxPendingReads &&
               !streamingQueue.empty() &&
               bytesStreamedThisFrame < maxBytesPerFrame) {

            StreamingRequest request = streamingQueue.top();
            streamingQueue.pop();

            // Check if already streaming or ready
            auto* loc = mergedBuffer.getSubmeshLocationMutable(request.meshPath, request.submeshName, request.submeshIndex);
            if (!loc) continue;

            auto currentState = loc->lodStates[request.lodLevel];
            if (currentState != gpudriven::LODStreamState::Queued) {
                continue;  // Already streaming or ready
            }

            // Mark as streaming
            loc->lodStates[request.lodLevel] = gpudriven::LODStreamState::Streaming;
            stats.lodsQueued--;

            // Start async read
            auto future = asyncReadLOD(request.meshPath, request.submeshName,
                                        request.submeshIndex, request.lodLevel);
            pendingReads.push_back(std::move(future));
        }
    }

    void MeshStreamManager::processPendingReads() {
        // Check for completed reads
        for (auto it = pendingReads.begin(); it != pendingReads.end(); ) {
            if (it->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                StreamingResult result = it->get();

                if (result.success) {
                    // Upload to GPU
                    bool uploaded = mergedBuffer.uploadLOD(
                        result.meshPath,
                        result.submeshName,
                        result.submeshIndex,
                        result.lodLevel,
                        result.vertices.data(),
                        static_cast<uint32_t>(result.vertices.size()),
                        result.indices.data(),
                        static_cast<uint32_t>(result.indices.size())
                    );

                    if (uploaded) {
                        // Track pending upload
                        PendingUpload pending;
                        pending.meshPath = result.meshPath;
                        pending.submeshName = result.submeshName;
                        pending.submeshIndex = result.submeshIndex;
                        pending.lodLevel = result.lodLevel;
                        pendingUploads.push_back(pending);

                        // Track bytes
                        size_t bytes = result.vertices.size() * sizeof(resource::Vertex) +
                                       result.indices.size() * sizeof(uint32_t);
                        bytesStreamedThisFrame += bytes;

                        // Queue higher quality LODs if this was LOD3
                        if (result.lodLevel == 3) {
                            // Schedule LOD2, LOD1, LOD0 with decreasing priority
                            auto it2 = meshStates.find(result.meshPath);
                            if (it2 != meshStates.end() && it2->second.handle) {
                                for (uint32_t lod = 2; lod < 4; --lod) {
                                    auto* loc = mergedBuffer.getSubmeshLocationMutable(
                                        result.meshPath, result.submeshName, result.submeshIndex);
                                    if (loc && loc->lodStates[lod] == gpudriven::LODStreamState::NotRequested) {
                                        StreamingRequest req;
                                        req.meshPath = result.meshPath;
                                        req.submeshName = result.submeshName;
                                        req.submeshIndex = result.submeshIndex;
                                        req.lodLevel = lod;
                                        req.priority = 100.0f * (3 - lod);  // LOD2 > LOD1 > LOD0
                                        req.worldCenter = glm::vec3(0.0f);
                                        req.boundingRadius = 1.0f;

                                        streamingQueue.push(req);
                                        loc->lodStates[lod] = gpudriven::LODStreamState::Queued;
                                        stats.lodsQueued++;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    loggerError("MeshStreamManager: Failed to read LOD {} for {}:{}",
                                result.lodLevel, result.meshPath, result.submeshName);
                }

                it = pendingReads.erase(it);
            } else {
                ++it;
            }
        }
    }

    void MeshStreamManager::processPendingUploads() {
        // For now, we assume uploads complete immediately after waitForPendingTransfers
        // In a more advanced system, we'd track transfer completion via fences

        // Mark all pending uploads as ready
        for (const auto& pending : pendingUploads) {
            mergedBuffer.markLODReady(pending.meshPath, pending.submeshName, pending.submeshIndex, pending.lodLevel);
        }
        pendingUploads.clear();
    }

    void MeshStreamManager::updatePriorities(const glm::vec3& cameraPos,
                                              const glm::mat4& viewProj) {
        // Rebuild priority queue with updated priorities
        std::vector<StreamingRequest> requests;
        while (!streamingQueue.empty()) {
            requests.push_back(streamingQueue.top());
            streamingQueue.pop();
        }

        for (auto& request : requests) {
            request.priority = calculatePriority(request, cameraPos, viewProj);
            streamingQueue.push(request);
        }
    }

    float MeshStreamManager::calculatePriority(const StreamingRequest& request,
                                                const glm::vec3& cameraPos,
                                                const glm::mat4& viewProj) const {
        // Base priority: lower LOD = higher urgency (LOD3 loads first)
        float lodUrgency = (4.0f - static_cast<float>(request.lodLevel)) * 25.0f;

        // Distance factor: closer objects get higher priority
        float distance = glm::length(request.worldCenter - cameraPos);
        float distanceFactor = 1.0f / (1.0f + distance * 0.01f);

        // Screen-space size estimate
        float screenSize = request.boundingRadius * 2.0f / std::max(1.0f, distance);
        float sizeFactor = std::min(1.0f, screenSize * 10.0f);

        return lodUrgency + distanceFactor * 50.0f + sizeFactor * 25.0f;
    }

    std::future<StreamingResult> MeshStreamManager::asyncReadLOD(
        const std::string& meshPath,
        const std::string& submeshName,
        uint32_t submeshIndex,
        uint32_t lodLevel) {

        // Get LOD info under lock, then release lock before file I/O
        resource::LODFileInfo lodInfo;
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto it = meshStates.find(meshPath);
            if (it == meshStates.end() || !it->second.handle) {
                // Return failed future immediately
                std::promise<StreamingResult> promise;
                StreamingResult result;
                result.meshPath = meshPath;
                result.submeshName = submeshName;
                result.submeshIndex = submeshIndex;
                result.lodLevel = lodLevel;
                result.success = false;
                promise.set_value(result);
                return promise.get_future();
            }

            const auto& header = it->second.handle->getHeader();
            if (submeshIndex >= header.numSubmeshes) {
                std::promise<StreamingResult> promise;
                StreamingResult result;
                result.meshPath = meshPath;
                result.submeshName = submeshName;
                result.submeshIndex = submeshIndex;
                result.lodLevel = lodLevel;
                result.success = false;
                promise.set_value(result);
                return promise.get_future();
            }

            // Use the passed submeshIndex directly (no name lookup needed)
            lodInfo = header.submeshes[submeshIndex].lods[lodLevel];
        }
        // Lock released here

        // Now do async file I/O without holding the lock
        return std::async(std::launch::async, [meshPath, submeshName, submeshIndex, lodLevel, lodInfo]() {
            StreamingResult result;
            result.meshPath = meshPath;
            result.submeshName = submeshName;
            result.submeshIndex = submeshIndex;
            result.lodLevel = lodLevel;

            // Use static method that opens its own file handle (thread-safe)
            result.success = resource::MeshStreamResource::readLODFromFile(
                meshPath, lodInfo, result.vertices, result.indices);

            return result;
        });
    }

    void MeshStreamManager::waitForPendingTransfers() {
        // Wait for all async reads to complete
        for (auto& future : pendingReads) {
            if (future.valid()) {
                future.wait();
            }
        }
    }

    bool MeshStreamManager::isMeshRenderable(const std::string& meshPath) const {
        return mergedBuffer.hasRenderableData(meshPath);
    }

    uint32_t MeshStreamManager::getBestAvailableLOD(const std::string& meshPath,
                                                     const std::string& submeshName,
                                                     uint32_t submeshIndex,
                                                     uint32_t preferredLOD) const {
        const auto* loc = mergedBuffer.getSubmeshLocation(meshPath, submeshName, submeshIndex);
        if (!loc) {
            return gpudriven::LOD_LEVEL_COUNT;
        }
        return loc->getBestAvailableLOD(preferredLOD);
    }

}
