#include "ThumbnailRenderAdapter.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../graphics/controllers/preview/MeshPreviewController.hpp"
#include "../../graphics/controllers/preview/MaterialPreviewController.hpp"
#include "material/MaterialTypes.hpp"
#include "math/Frustum.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace core
{
    namespace
    {
        constexpr int GC_FRAMES = 8; // frames a finished, un-taken job lingers before reclaim

        // Frame a target so it roughly fills a square viewport. Returns a Vulkan
        // (Y-flipped, [0,1] depth — GLM_FORCE_DEPTH_ZERO_TO_ONE is global) view/proj.
        void framingCamera(const glm::vec3& center, float radius,
                           glm::mat4& view, glm::mat4& proj, glm::vec3& camPos)
        {
            if (radius < 1e-4f)
            {
                radius = 1.0f;
            }
            const float fovy = glm::radians(40.0f);
            const float dist = radius / std::sin(fovy * 0.5f) * 1.25f;
            const glm::vec3 dir = glm::normalize(glm::vec3(0.6f, 0.55f, 1.0f));
            camPos = center + dir * dist;
            view = glm::lookAt(camPos, center, glm::vec3(0.0f, 1.0f, 0.0f));
            const float nearP = std::max(0.01f, dist - radius * 2.0f);
            const float farP = dist + radius * 2.0f;
            proj = glm::perspective(fovy, 1.0f, nearP, farP);
            proj[1][1] *= -1.0f;
        }

        controllers::PreviewMaterialParams toControllerParams(const services::MaterialPreviewParams& p)
        {
            controllers::PreviewMaterialParams out;
            out.albedo = p.albedo;
            out.metallic = p.metallic;
            out.roughness = p.roughness;
            out.ao = p.ao;
            out.emission = p.emission;
            out.albedoTexturePath = p.albedoTexturePath;
            out.normalTexturePath = p.normalTexturePath;
            out.ormTexturePath = p.ormTexturePath;
            out.metallicTexturePath = p.metallicTexturePath;
            out.roughnessTexturePath = p.roughnessTexturePath;
            out.aoTexturePath = p.aoTexturePath;
            out.emissionTexturePath = p.emissionTexturePath;
            out.heightTexturePath = p.heightTexturePath;
            out.materialPath = p.materialPath;
            out.useCustomShader = false; // thumbnails use the standard PBR path
            if (p.materialDataHandle.has_value())
            {
                try
                {
                    out.materialData = std::any_cast<std::shared_ptr<material::MaterialData>>(p.materialDataHandle);
                }
                catch (const std::bad_any_cast&)
                {
                }
            }
            return out;
        }
    }

    ThumbnailRenderAdapter::~ThumbnailRenderAdapter()
    {
        cleanUp();
    }

    void ThumbnailRenderAdapter::init()
    {
        if (handlersRegistered)
        {
            return;
        }
        handlersRegistered = true;

        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::LoadRenderThumbnailAsyncCommand>(
            [this](const events::render::LoadRenderThumbnailAsyncCommand& cmd) { queue(cmd); });

        dispatcher.registerCommandHandler<events::render::CancelRenderThumbnailCommand>(
            [this](const events::render::CancelRenderThumbnailCommand& cmd) { cancel(cmd.instanceId); });

        dispatcher.registerCommandHandler<events::render::ReleaseRenderThumbnailCommand>(
            [this](const events::render::ReleaseRenderThumbnailCommand& cmd) { release(cmd.handle); });

        dispatcher.registerQueryHandler<events::render::GetRenderThumbnailProgressQuery>(
            [this](const events::render::GetRenderThumbnailProgressQuery& query)
            {
                return getProgress(query.instanceId);
            });

        dispatcher.registerQueryHandler<events::render::GetRenderThumbnailHandleQuery>(
            [this](const events::render::GetRenderThumbnailHandleQuery& query)
            {
                return takeHandle(query.instanceId);
            });
    }

    void ThumbnailRenderAdapter::ensureControllers()
    {
        if (!meshController)
        {
            meshController = std::make_unique<controllers::MeshPreviewController>();
        }
        if (!materialController)
        {
            materialController = std::make_unique<controllers::MaterialPreviewController>();
        }
    }

    void ThumbnailRenderAdapter::queue(const events::render::LoadRenderThumbnailAsyncCommand& cmd)
    {
        if (!cmd.instanceId)
        {
            return;
        }
        Job job;
        job.id = cmd.instanceId;
        job.kind = cmd.kind;
        job.meshPath = cmd.meshPath;
        job.materialParams = cmd.materialParams;
        job.stage = Stage::Pending;
        jobs[cmd.instanceId] = std::move(job);

        if (cmd.kind == events::render::RenderThumbnailKind::Mesh)
        {
            meshQueue.push_back(cmd.instanceId);
        }
        else
        {
            materialQueue.push_back(cmd.instanceId);
        }
    }

    void ThumbnailRenderAdapter::removeFromQueue(std::deque<void*>& queue, void* id)
    {
        queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
    }

    void ThumbnailRenderAdapter::cancel(void* id)
    {
        auto it = jobs.find(id);
        if (it == jobs.end())
        {
            return;
        }

        if (id == activeMesh)
        {
            if (meshController)
            {
                meshController->cancelMeshLoading();
            }
            activeMesh = nullptr;
        }
        if (id == activeMaterial)
        {
            activeMaterial = nullptr;
        }

        removeFromQueue(meshQueue, id);
        removeFromQueue(materialQueue, id);

        if (it->second.handle.imguiDescriptorSet)
        {
            release(it->second.handle.imguiDescriptorSet);
        }
        jobs.erase(it);
    }

    services::TextureLoadingProgress ThumbnailRenderAdapter::getProgress(void* id) const
    {
        services::TextureLoadingProgress progress;
        auto it = jobs.find(id);
        if (it == jobs.end())
        {
            progress.state = services::LoadingState::Idle;
            return progress;
        }

        switch (it->second.stage)
        {
        case Stage::Done:
            progress.state = services::LoadingState::Complete;
            progress.progress = 1.0f;
            break;
        case Stage::Failed:
            progress.state = services::LoadingState::Error;
            break;
        default:
            progress.state = services::LoadingState::Loading;
            break;
        }
        return progress;
    }

    services::EditorTextureHandle ThumbnailRenderAdapter::takeHandle(void* id)
    {
        auto it = jobs.find(id);
        if (it == jobs.end() || it->second.stage != Stage::Done)
        {
            return {};
        }
        services::EditorTextureHandle handle = it->second.handle;
        jobs.erase(it); // ownership of the GPU texture transfers to the caller
        return handle;
    }

    void ThumbnailRenderAdapter::release(void* handle)
    {
        auto it = handleOwner.find(handle);
        if (it == handleOwner.end())
        {
            return;
        }
        if (it->second == events::render::RenderThumbnailKind::Mesh && meshController)
        {
            meshController->releaseSnapshot(handle);
        }
        else if (it->second == events::render::RenderThumbnailKind::Material && materialController)
        {
            materialController->releaseSnapshot(handle);
        }
        handleOwner.erase(it);
    }

    void ThumbnailRenderAdapter::processMeshSlot()
    {
        if (!activeMesh)
        {
            if (meshQueue.empty())
            {
                return;
            }
            activeMesh = meshQueue.front();
            meshQueue.pop_front();
            auto it = jobs.find(activeMesh);
            if (it == jobs.end())
            {
                activeMesh = nullptr;
                return;
            }
            ensureControllers();
            meshController->loadMeshAsync(it->second.meshPath);
            it->second.stage = Stage::MeshLoading;
        }

        auto it = jobs.find(activeMesh);
        if (it == jobs.end())
        {
            activeMesh = nullptr;
            return;
        }
        Job& job = it->second;

        if (job.stage == Stage::MeshLoading)
        {
            meshController->updateAsyncLoading();
            services::MeshLoadingProgress prog = meshController->getMeshLoadingProgress();
            if (prog.state == services::LoadingState::Complete)
            {
                services::PreviewEnvironmentParams env;
                env.showGrid = false;
                meshController->setEnvironment(env);

                const math::AABB& bounds = meshController->getMeshBounds();
                glm::mat4 view, proj;
                glm::vec3 camPos;
                framingCamera(bounds.getCenter(), glm::length(bounds.getExtents()), view, proj, camPos);
                meshController->updateCamera(view, proj, camPos);

                job.stage = Stage::Rendering;
                job.settleFrames = MESH_SETTLE_FRAMES;
            }
            else if (prog.state == services::LoadingState::Error
                     || prog.state == services::LoadingState::Cancelled)
            {
                job.stage = Stage::Failed;
            }
        }

        if (job.stage == Stage::Rendering)
        {
            meshController->render();
            if (--job.settleFrames <= 0)
            {
                void* descriptor = meshController->snapshot(THUMB_SIZE);
                if (descriptor)
                {
                    job.handle = {};
                    job.handle.imguiDescriptorSet = descriptor;
                    job.handle.width = THUMB_SIZE;
                    job.handle.height = THUMB_SIZE;
                    handleOwner[descriptor] = events::render::RenderThumbnailKind::Mesh;
                    job.stage = Stage::Done;
                }
                else
                {
                    job.stage = Stage::Failed;
                }
            }
        }

        if (job.stage == Stage::Done || job.stage == Stage::Failed)
        {
            activeMesh = nullptr;
        }
    }

    void ThumbnailRenderAdapter::processMaterialSlot()
    {
        if (!activeMaterial)
        {
            if (materialQueue.empty())
            {
                return;
            }
            activeMaterial = materialQueue.front();
            materialQueue.pop_front();
            auto it = jobs.find(activeMaterial);
            if (it == jobs.end())
            {
                activeMaterial = nullptr;
                return;
            }
            ensureControllers();
            materialController->init();
            materialController->setMaterialParams(toControllerParams(it->second.materialParams));

            glm::mat4 view, proj;
            glm::vec3 camPos;
            framingCamera(glm::vec3(0.0f), 1.0f, view, proj, camPos);
            materialController->updateCamera(view, proj, camPos, 0.0f);

            it->second.stage = Stage::Rendering;
            it->second.settleFrames = MATERIAL_SETTLE_FRAMES;
        }

        auto it = jobs.find(activeMaterial);
        if (it == jobs.end())
        {
            activeMaterial = nullptr;
            return;
        }
        Job& job = it->second;

        if (job.stage == Stage::Rendering)
        {
            materialController->render();
            if (--job.settleFrames <= 0)
            {
                void* descriptor = materialController->snapshot(THUMB_SIZE);
                if (descriptor)
                {
                    job.handle = {};
                    job.handle.imguiDescriptorSet = descriptor;
                    job.handle.width = THUMB_SIZE;
                    job.handle.height = THUMB_SIZE;
                    handleOwner[descriptor] = events::render::RenderThumbnailKind::Material;
                    job.stage = Stage::Done;
                }
                else
                {
                    job.stage = Stage::Failed;
                }
            }
        }

        if (job.stage == Stage::Done || job.stage == Stage::Failed)
        {
            activeMaterial = nullptr;
        }
    }

    void ThumbnailRenderAdapter::process()
    {
        processMeshSlot();
        processMaterialSlot();

        // Reclaim finished jobs the cache never picked up (e.g. its entry was
        // evicted before it observed completion), releasing any orphaned texture.
        for (auto it = jobs.begin(); it != jobs.end();)
        {
            Job& job = it->second;
            if ((job.stage == Stage::Done || job.stage == Stage::Failed)
                && job.id != activeMesh && job.id != activeMaterial)
            {
                if (++job.settleFrames > GC_FRAMES)
                {
                    if (job.stage == Stage::Done && job.handle.imguiDescriptorSet)
                    {
                        release(job.handle.imguiDescriptorSet);
                    }
                    it = jobs.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    void ThumbnailRenderAdapter::cleanUp()
    {
        jobs.clear();
        meshQueue.clear();
        materialQueue.clear();
        handleOwner.clear();
        activeMesh = nullptr;
        activeMaterial = nullptr;
        // Controller destructors wait for GPU idle and free their snapshots.
        materialController.reset();
        meshController.reset();
    }
}
