#include "MeshStreamManager.hpp"
#include "../gpudriven/scene/MergedMeshBuffer.hpp"
#include "../gpudriven/scene/MeshletBuffer.hpp"
#include "../../core/Device.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"
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

    bool MeshStreamManager::registerInMemoryMesh(const InMemoryMeshData& mesh)
    {
        if (mesh.meshKey.empty() || mesh.submeshes.empty())
            return false;

        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto existing = meshStates.find(mesh.meshKey);
            if (existing != meshStates.end())
            {
                // Already registered (or, pathologically, a file-backed mesh is using this key).
                // Either way there is nothing to upload.
                return existing->second.inMemory;
            }
        }

        // reserveMesh reads exactly three things off the header - numSubmeshes, each submesh name,
        // and each LOD's vertex/index COUNT - so everything file-related is left at its default.
        resource::MeshStreamHeader header;
        header.numSubmeshes = static_cast<uint32_t>(mesh.submeshes.size());
        header.submeshes.resize(mesh.submeshes.size());

        for (size_t subIdx = 0; subIdx < mesh.submeshes.size(); ++subIdx)
        {
            const auto& src = mesh.submeshes[subIdx];
            auto& dst = header.submeshes[subIdx];
            dst.name = src.name;
            for (uint32_t lod = 0; lod < gpudriven::LOD_LEVEL_COUNT; ++lod)
            {
                dst.lods[lod].vertexCount = src.lods[lod].vertexCount;
                dst.lods[lod].indexCount = src.lods[lod].indexCount;
            }
        }

        auto* meshInfo = mergedBuffer.reserveMesh(mesh.meshKey, header);
        if (!meshInfo)
        {
            vfLogError("MeshStreamManager: Failed to reserve space for in-memory mesh {}", mesh.meshKey);
            return false;
        }

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& src = mesh.submeshes[subIdx];
            for (uint32_t lod = 0; lod < gpudriven::LOD_LEVEL_COUNT; ++lod)
            {
                const auto& lodData = src.lods[lod];
                if (lodData.vertexCount == 0 || lodData.indexCount == 0)
                    continue;

                gpudriven::LODUploadData upload;
                upload.vertexData = lodData.vertices;
                upload.vertexCount = lodData.vertexCount;
                upload.indexData = lodData.indices;
                upload.indexCount = lodData.indexCount;

                if (!mergedBuffer.uploadLOD(mesh.meshKey, src.name, subIdx, lod, upload))
                {
                    vfLogError("MeshStreamManager: in-memory upload failed for {}:{} LOD{}",
                               mesh.meshKey, src.name, lod);
                    mergedBuffer.freeMesh(mesh.meshKey);
                    return false;
                }

                // Without this the LOD stays in Uploading and is never considered renderable.
                mergedBuffer.markLODReady(mesh.meshKey, src.name, subIdx, lod);
            }
        }

        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            MeshStreamingState state;
            state.referenceCount = 1;
            state.headerParsed = true; // keeps update() from ever calling openMeshStream on it
            state.inMemory = true;
            meshStates[mesh.meshKey] = std::move(state);
        }

        // NOTE: meshlets are deliberately not registered. HLOD proxies fall back to the
        // non-mesh-shader draw path, the same fallback openMeshStream already logs when
        // MeshletBuffer::reserveMeshlets fails.
        vfLogInfo("MeshStreamManager: registered in-memory mesh '{}' ({} submeshes)",
                  mesh.meshKey, header.numSubmeshes);
        return true;
    }

    void MeshStreamManager::releaseInMemoryMesh(const std::string& meshKey)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshKey);
        if (it == meshStates.end() || !it->second.inMemory)
            return;

        if (meshletBuffer)
            meshletBuffer->freeAllMeshlets(meshKey);
        mergedBuffer.freeMesh(meshKey);
        meshStates.erase(it);

        vfLogInfo("MeshStreamManager: released in-memory mesh '{}'", meshKey);
    }

    bool MeshStreamManager::isInMemoryMesh(const std::string& meshKey) const
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);
        auto it = meshStates.find(meshKey);
        return it != meshStates.end() && it->second.inMemory;
    }

    void MeshStreamManager::unrequestMesh(const std::string& meshPath)
    {
        std::lock_guard<std::mutex> lock(meshStatesMutex);

        auto it = meshStates.find(meshPath);
        if (it == meshStates.end())
        {
            return;
        }

        // VK-1594: an in-memory mesh has no backing file, so the force-release below would drop
        // geometry nothing can ever restore. Only releaseInMemoryMesh may free it.
        if (it->second.inMemory)
        {
            return;
        }

        // Force-release: requestMesh is called per-frame so ref count is not a
        // traditional acquire/release counter. Free GPU resources immediately.
        if (meshletBuffer)
        {
            meshletBuffer->freeAllMeshlets(meshPath);
        }
        mergedBuffer.freeMesh(meshPath);

        it->second.handle.reset();
        meshStates.erase(it);

        vfLogInfo("MeshStreamManager::unrequestMesh: Released '{}'", meshPath);
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
            vfLogError("MeshStreamManager: Failed to open stream for {}", meshPath);
            return;
        }

        state.headerParsed = true;

        const auto& header = state.handle->getHeader();
        auto* meshInfo = mergedBuffer.reserveMesh(meshPath, header);
        if (!meshInfo)
        {
            vfLogError("MeshStreamManager: Failed to reserve space for {}", meshPath);
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
                vfLogWarning(
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
        gpudriven::LODUploadData uploadData{
            result.vertices.data(),
            static_cast<uint32_t>(result.vertices.size()),
            result.indices.data(),
            static_cast<uint32_t>(result.indices.size())
        };
        bool uploaded = mergedBuffer.uploadLOD(
            result.meshPath,
            result.submeshName,
            result.submeshIndex,
            result.lodLevel,
            uploadData
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
                    vfLogError("MeshStreamManager: Failed to read LOD {} for {}:{}",
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
        {
            std::lock_guard<std::mutex> lock(meshStatesMutex);
            auto it = meshStates.find(meshPath);
            if (it == meshStates.end() || !it->second.handle)
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

            lodInfo = header.submeshes[submeshIndex].lods[lodLevel];
        }

        return std::async(std::launch::async, [meshPath, submeshName, submeshIndex, lodLevel, lodInfo]()
        {
            StreamingResult result;
            result.meshPath = meshPath;
            result.submeshName = submeshName;
            result.submeshIndex = submeshIndex;
            result.lodLevel = lodLevel;

            result.success = resource::MeshStreamResource::readLODFromFile(
                meshPath, lodInfo, result.vertices, result.indices);

            return result;
        });
    }
}
