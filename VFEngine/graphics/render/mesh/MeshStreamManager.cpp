#include "MeshStreamManager.hpp"
#include "../gpudriven/MergedMeshBuffer.hpp"
#include "../gpudriven/MeshletBuffer.hpp"
#include "../../core/Device.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace render::mesh
{
    MeshStreamManager::MeshStreamManager(core::Device& device,
                                         gpudriven::MergedMeshBuffer& mergedBuffer)
        : device(device)
          , mergedBuffer(mergedBuffer)
    {
    }

    MeshStreamManager::~MeshStreamManager()
    {
        std::lock_guard<std::mutex> lock(pendingReadsMutex);
        for (auto& future : pendingReads)
        {
            if (future.valid())
            {
                future.wait();
            }
        }
    }

    void MeshStreamManager::requestMesh(const std::string& meshPath)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it != meshStates.end())
        {
            it->second.referenceCount++;
            return;
        }

        MeshStreamingState state;
        state.referenceCount = 1;
        state.headerParsed = false;
        meshStates[meshPath] = std::move(state);
    }

    void MeshStreamManager::openMeshStream(const std::string& meshPath)
    {
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || it->second.headerParsed)
        {
            return;
        }

        auto& state = it->second;

        state.handle = resource::MeshStreamResource::openStream(meshPath);
        if (!state.handle)
        {
            loggerError("MeshStreamManager: Failed to open stream for {}", meshPath);
            return;
        }

        state.headerParsed = true;

        const auto& header = state.handle->getHeader();
        auto* meshInfo = mergedBuffer.reserveMesh(meshPath, header);
        if (!meshInfo)
        {
            loggerError("MeshStreamManager: Failed to reserve space for {}", meshPath);
            state.handle.reset();
            state.headerParsed = false;
            return;
        }
        
        if (meshletBuffer && state.handle->hasMeshletData())
        {
            auto* meshletAlloc = meshletBuffer->reserveMeshlets(meshPath, header);
            if (meshletAlloc)
            {
                for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
                {
                    const auto& submeshInfo = header.submeshes[subIdx];
                    auto* loc = mergedBuffer.getSubmeshLocationMutable(meshPath, submeshInfo.name, subIdx);
                    if (loc)
                    {
                        const auto* alloc = meshletBuffer->getAllocation(meshPath, submeshInfo.name, subIdx);
                        if (alloc)
                        {
                            for (uint32_t lod = 0; lod < gpudriven::LOD_LEVEL_COUNT; ++lod)
                            {
                                loc->meshletLods[lod] = meshletBuffer->getMeshletLODInfo(*alloc, lod);
                            }
                        }
                    }
                }
            }
            else
            {
                loggerWarning(
                    "MeshStreamManager: Failed to reserve meshlet space for {}, mesh shader rendering will use fallback",
                    meshPath);
            }
        }

        scheduleInitialLODs(meshPath);
    }

    void MeshStreamManager::scheduleInitialLODs(const std::string& meshPath)
    {
        auto it = meshStates.find(meshPath);
        if (it == meshStates.end() || !it->second.handle)
        {
            return;
        }

        const auto& header = it->second.handle->getHeader();

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& submesh = header.submeshes[subIdx];
            
            StreamingRequest request;
            request.meshPath = meshPath;
            request.submeshName = submesh.name;
            request.submeshIndex = subIdx;
            request.lodLevel = 3;
            request.priority = 1000.0f;
            request.worldCenter = glm::vec3(0.0f);
            request.boundingRadius = 1.0f;

            streamingQueue.push(request);
            
            auto* loc = mergedBuffer.getSubmeshLocationMutable(meshPath, submesh.name, subIdx);
            if (loc)
            {
                loc->lodStates[3] = gpudriven::LODStreamState::Queued;
            }
        }
    }

    void MeshStreamManager::update(const glm::vec3& cameraPos)
    {
        bytesStreamedThisFrame = 0;
        
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

        processPendingReads();

        processPendingUploads();

        updatePriorities(cameraPos);

        processStreamingQueue();
    }

    void MeshStreamManager::processStreamingQueue()
    {
        std::lock_guard<std::mutex> lock(pendingReadsMutex);
        
        while (pendingReads.size() < maxPendingReads &&
            !streamingQueue.empty() &&
            bytesStreamedThisFrame < maxBytesPerFrame)
        {
            StreamingRequest request = streamingQueue.top();
            streamingQueue.pop();
            
            auto* loc = mergedBuffer.getSubmeshLocationMutable(request.meshPath, request.submeshName,
                                                               request.submeshIndex);
            if (!loc) continue;

            auto currentState = loc->lodStates[request.lodLevel];
            if (currentState != gpudriven::LODStreamState::Queued)
            {
                continue; 
            }

            
            loc->lodStates[request.lodLevel] = gpudriven::LODStreamState::Streaming;
            
            auto future = asyncReadLOD(request.meshPath, request.submeshName,
                                       request.submeshIndex, request.lodLevel);
            pendingReads.push_back(std::move(future));
        }
    }

    void MeshStreamManager::handleCompletedRead(const StreamingResult& result)
    {
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

        if (!uploaded) return;
        
        if (meshletBuffer)
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto it = meshStates.find(result.meshPath);
            if (it != meshStates.end() && it->second.handle && it->second.handle->hasMeshletData())
            {
                resource::SubmeshMeshletData meshletData;
                if (it->second.handle->readMeshletData(result.submeshIndex, meshletData))
                {
                    const auto* loc = mergedBuffer.getSubmeshLocation(
                        result.meshPath, result.submeshName, result.submeshIndex);

                    if (loc && meshletData.hasMeshletData())
                    {
                        const auto& lodInfo = loc->lods[result.lodLevel];
                        if (lodInfo.vertexCount > 0 && loc->meshletLods[result.lodLevel].meshletCount > 0)
                        {
                            meshletBuffer->uploadMeshletData(
                                result.meshPath,
                                result.submeshName,
                                result.submeshIndex,
                                result.lodLevel,
                                meshletData,
                                lodInfo.vertexOffset
                            );
                        }
                    }
                }
            }
        }

        PendingUpload pending;
        pending.meshPath = result.meshPath;
        pending.submeshName = result.submeshName;
        pending.submeshIndex = result.submeshIndex;
        pending.lodLevel = result.lodLevel;
        pendingUploads.push_back(pending);

        size_t bytes = result.vertices.size() * sizeof(resource::Vertex) +
            result.indices.size() * sizeof(uint32_t);
        bytesStreamedThisFrame += bytes;

        if (result.lodLevel == 3)
        {
            queueHigherQualityLODs(result);
        }
    }

    void MeshStreamManager::queueHigherQualityLODs(const StreamingResult& result)
    {
        auto it = meshStates.find(result.meshPath);
        if (it == meshStates.end() || !it->second.handle) return;

        for (int lod = 2; lod >= 0; --lod)
        {
            auto* loc = mergedBuffer.getSubmeshLocationMutable(
                result.meshPath, result.submeshName, result.submeshIndex);

            if (loc && loc->lodStates[lod] == gpudriven::LODStreamState::NotRequested)
            {
                StreamingRequest req;
                req.meshPath = result.meshPath;
                req.submeshName = result.submeshName;
                req.submeshIndex = result.submeshIndex;
                req.lodLevel = lod;
                req.priority = 100.0f * (3 - lod);
                req.worldCenter = glm::vec3(0.0f);
                req.boundingRadius = 1.0f;

                streamingQueue.push(req);
                loc->lodStates[lod] = gpudriven::LODStreamState::Queued;
            }
        }
    }

    void MeshStreamManager::processPendingReads()
    {
        std::lock_guard<std::mutex> lock(pendingReadsMutex);

        for (auto it = pendingReads.begin(); it != pendingReads.end();)
        {
            if (it->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                StreamingResult result = it->get();

                if (result.success)
                {
                    handleCompletedRead(result);
                }
                else
                {
                    loggerError("MeshStreamManager: Failed to read LOD {} for {}:{}",
                                result.lodLevel, result.meshPath, result.submeshName);
                }

                it = pendingReads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void MeshStreamManager::processPendingUploads()
    {
        for (const auto& pending : pendingUploads)
        {
            mergedBuffer.markLODReady(pending.meshPath, pending.submeshName, pending.submeshIndex, pending.lodLevel);
        }
        pendingUploads.clear();
    }

    void MeshStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        std::vector<StreamingRequest> requests;
        while (!streamingQueue.empty())
        {
            requests.push_back(streamingQueue.top());
            streamingQueue.pop();
        }

        for (auto& request : requests)
        {
            request.priority = calculatePriority(request, cameraPos);
            streamingQueue.push(request);
        }
    }

    float MeshStreamManager::calculatePriority(const StreamingRequest& request,
                                               const glm::vec3& cameraPos) const
    {
        // Base priority: lower LOD = higher urgency (LOD3 loads first)
        float lodUrgency = (4.0f - static_cast<float>(request.lodLevel)) * 25.0f;

        float distance = glm::length(request.worldCenter - cameraPos);
        float distanceFactor = 1.0f / (1.0f + distance * 0.01f);

        float screenSize = request.boundingRadius * 2.0f / std::max(1.0f, distance);
        float sizeFactor = std::min(1.0f, screenSize * 10.0f);

        return lodUrgency + distanceFactor * 50.0f + sizeFactor * 25.0f;
    }

    std::future<StreamingResult> MeshStreamManager::asyncReadLOD(
        const std::string& meshPath,
        const std::string& submeshName,
        uint32_t submeshIndex,
        uint32_t lodLevel)
    {
        resource::LODFileInfo lodInfo;
        bool hasBoneData = false;
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto it = meshStates.find(meshPath);
            if (it == meshStates.end() || !it->second.handle)
            {
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
            if (submeshIndex >= header.numSubmeshes)
            {
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
            hasBoneData = it->second.handle->hasBoneData();
        }

        return std::async(std::launch::async, [meshPath, submeshName, submeshIndex, lodLevel, lodInfo, hasBoneData]()
        {
            StreamingResult result;
            result.meshPath = meshPath;
            result.submeshName = submeshName;
            result.submeshIndex = submeshIndex;
            result.lodLevel = lodLevel;

            // Use static method that opens its own file handle (thread-safe)
            result.success = resource::MeshStreamResource::readLODFromFile(
                meshPath, lodInfo, result.vertices, result.indices, hasBoneData);

            return result;
        });
    }
}
