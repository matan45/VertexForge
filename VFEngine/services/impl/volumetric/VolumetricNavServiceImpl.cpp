#include "VolumetricNavServiceImpl.hpp"
#include "../navigation/SceneGeometryCollector.hpp"
#include "../../events/volumetric/VolumetricNavEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"
#include "components/Components.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "navigation/volumetric/VolumetricSerializer.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "threading/JobSystem.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace services
{
    namespace
    {
        namespace fs = std::filesystem;

        struct VolumeBox
        {
            glm::vec3 min{0.0f};
            glm::vec3 max{0.0f};
        };

        bool isFinite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        std::optional<uint64_t> volumeUuid(const entt::registry& registry, entt::entity entity)
        {
            const auto* uuid = registry.try_get<components::UUIDComponent>(entity);
            if (!uuid || uuid->id.getValue() == 0)
                return std::nullopt;
            return uuid->id.getValue();
        }

        // The volume's box now: around the entity's WORLD position, axis-aligned.
        VolumeBox worldBox(const entt::registry& registry, entt::entity entity,
                           const components::VolumetricNavVolumeComponent& volume)
        {
            const glm::vec3 center = scene::composeWorldPosition(registry, entity) + volume.centerOffset;
            return VolumeBox{center - volume.halfExtents, center + volume.halfExtents};
        }

        volumetric::VolumetricBakeSettings settingsOf(const components::VolumetricNavVolumeComponent& volume)
        {
            volumetric::VolumetricBakeSettings settings;
            settings.voxelSize = volume.voxelSize;
            settings.clearance = volume.agentClearance;
            settings.connectivity = volume.connectivity;
            return settings;
        }

        // Why the bake cannot use these values; "" when it can.
        std::string invalidVolumeReason(const glm::vec3& centerOffset, const glm::vec3& halfExtents, float voxelSize,
                                        int connectivity, float clearance)
        {
            if (!isFinite(centerOffset) || !isFinite(halfExtents) || !std::isfinite(voxelSize) ||
                !std::isfinite(clearance))
                return "a value is not a finite number";
            if (!(halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f))
                return "halfExtents must be positive on every axis";
            if (!(voxelSize > 0.0f))
                return "voxelSize must be positive";
            if (connectivity != 6 && connectivity != 26)
                return "connectivity must be 6 or 26";
            if (clearance < 0.0f)
                return "agentClearance must not be negative";
            return {};
        }

        // The loaded grid still is what the volume's box and settings would bake now.
        bool gridMatches(const volumetric::VoxelGrid& grid, const VolumeBox& box,
                         const volumetric::VolumetricBakeSettings& settings)
        {
            constexpr float kOriginTolerance = 1.0e-3f;
            const glm::vec3 offset = glm::abs(grid.origin() - box.min);
            return offset.x <= kOriginTolerance && offset.y <= kOriginTolerance && offset.z <= kOriginTolerance &&
                   grid.dims() == volumetric::gridDimensionsFor(box.min, box.max, settings.voxelSize) &&
                   grid.settings() == settings;
        }

        float navigablePercent(int64_t navigable, int64_t voxels)
        {
            return voxels > 0 ? 100.0f * static_cast<float>(navigable) / static_cast<float>(voxels) : 0.0f;
        }

        std::string normalizedPath(const std::string& path)
        {
            return fs::path(path).lexically_normal().generic_string();
        }

        std::string entityName(const entt::registry& registry, entt::entity entity)
        {
            const auto* name = registry.try_get<components::NameComponent>(entity);
            return name ? name->name : std::string();
        }

        // A file-name-safe form of the volume's name ("Volume" when nothing is left).
        std::string fileStem(const std::string& name)
        {
            std::string stem;
            stem.reserve(name.size());
            for (unsigned char c : name)
                stem += (std::isalnum(c) || c == '_' || c == '-') ? static_cast<char>(c) : '_';
            return stem.empty() ? std::string("Volume") : stem;
        }

        // The current scene's file stem, "untitled" for a scene never saved.
        std::string sceneStem()
        {
            try
            {
                const std::string path =
                    ::events::EventDispatcher::instance().query(::events::scene::GetCurrentScenePathQuery{});
                if (!path.empty())
                {
                    const std::string stem = fs::path(path).stem().string();
                    if (!stem.empty())
                        return stem;
                }
            }
            catch (const std::exception&)
            {
                // No scene service (tests): an unsaved scene.
            }
            return "untitled";
        }

        bool writeAtomically(const fs::path& path, const std::vector<uint8_t>& bytes, std::string& error)
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = std::format("could not create '{}': {}", path.parent_path().string(), ec.message());
                return false;
            }

            const fs::path temp(path.string() + ".tmp");
            {
                std::ofstream file(temp, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    error = std::format("could not write '{}'", temp.string());
                    return false;
                }
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                file.flush();
                if (!file.good())
                {
                    file.close();
                    fs::remove(temp, ec);
                    error = std::format("could not write '{}'", temp.string());
                    return false;
                }
            }

            fs::rename(temp, path, ec);
            if (ec)
            {
                std::error_code removeError;
                fs::remove(temp, removeError);
                error = std::format("could not replace '{}': {}", path.string(), ec.message());
                return false;
            }
            return true;
        }

        double millisecondsSince(std::chrono::steady_clock::time_point started)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
    }

    VolumetricNavServiceImpl::VolumetricNavServiceImpl(IVolumetricNavProvider* provider)
        : provider(provider),
          agentManager(provider)
    {
        assert(provider && "VolumetricNavProvider must not be null");
    }

    VolumetricNavServiceImpl::~VolumetricNavServiceImpl()
    {
        std::vector<RunningBake> running;
        {
            std::lock_guard lock(mutex);
            running = std::move(bakes);
            bakes.clear();
        }
        for (RunningBake& bake : running)
        {
            if (bake.cancel)
                bake.cancel->cancel();
            if (bake.future.valid())
                bake.future.wait();
        }

        subscriptions.clear();
        unregisterEventHandlers();
    }

    void VolumetricNavServiceImpl::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = ::events::EventDispatcher::instance();
        namespace vol = ::events::volumetric;

        // === Bake ===
        dispatcher.registerCommandHandler<vol::RequestVolumetricBakeCommand>(
            [this](const vol::RequestVolumetricBakeCommand& cmd) { return requestBake(cmd.volume); });
        dispatcher.registerQueryHandler<vol::GetVolumetricBakeOutcomeQuery>(
            [this](const vol::GetVolumetricBakeOutcomeQuery& query) { return getBakeOutcome(query.ticket); });
        dispatcher.registerCommandHandler<vol::CancelVolumetricBakeCommand>(
            [this](const vol::CancelVolumetricBakeCommand& cmd) { cancelBake(cmd.volume); });
        dispatcher.registerCommandHandler<vol::SaveVolumetricVolumeCommand>(
            [this](const vol::SaveVolumetricVolumeCommand& cmd) { return saveVolume(cmd.volume, cmd.directory); });
        dispatcher.registerCommandHandler<vol::LoadSceneVolumetricVolumesCommand>(
            [this](const vol::LoadSceneVolumetricVolumesCommand&) { return loadSceneVolumes(); });
        dispatcher.registerCommandHandler<vol::ClearVolumetricNavCommand>(
            [this](const vol::ClearVolumetricNavCommand& cmd) { clearVolume(cmd.volume); });
        dispatcher.registerCommandHandler<vol::UnloadVolumetricNavCommand>(
            [this](const vol::UnloadVolumetricNavCommand&) { unload(); });
        dispatcher.registerQueryHandler<vol::GetVolumetricVolumeStatusQuery>(
            [this](const vol::GetVolumetricVolumeStatusQuery& query) { return getVolumeStatus(query.volume); });
        dispatcher.registerQueryHandler<vol::ListVolumetricVolumesQuery>(
            [this](const vol::ListVolumetricVolumesQuery&) { return listVolumes(); });

        // === Queries ===
        dispatcher.registerQueryHandler<vol::FindPath3DQuery>(
            [this](const vol::FindPath3DQuery& query)
            {
                volumetric::PathRequest request;
                request.start = query.start;
                request.end = query.end;
                request.snapRadius = query.snapRadius;
                return provider->findPath3D(request);
            });
        dispatcher.registerQueryHandler<vol::IsPointNavigable3DQuery>(
            [this](const vol::IsPointNavigable3DQuery& query)
            {
                return provider->queryPoint(query.point, 0.0f).navigable;
            });
        dispatcher.registerQueryHandler<vol::GetNearestNavigable3DQuery>(
            [this](const vol::GetNearestNavigable3DQuery& query) -> std::optional<glm::vec3>
            {
                const volumetric::VolumetricNavWorld::PointQuery point = provider->queryPoint(query.point, query.snapRadius);
                if (point.volumeId == 0)
                    return std::nullopt; // no loaded volume holds the point
                return point.nearest;
            });

        // === Agents ===
        dispatcher.registerCommandHandler<vol::SetVolumetricAgentDestinationCommand>(
            [this](const vol::SetVolumetricAgentDestinationCommand& cmd)
            {
                if (isPlayMode())
                    agentManager.setDestination(cmd.entity, cmd.target);
            });
        dispatcher.registerCommandHandler<vol::StopVolumetricAgentCommand>(
            [this](const vol::StopVolumetricAgentCommand& cmd) { agentManager.stopAgent(cmd.entity); });
        dispatcher.registerQueryHandler<vol::GetVolumetricAgentStateQuery>(
            [this](const vol::GetVolumetricAgentStateQuery& query) { return agentManager.getState(query.entity); });

        // === Component data ===
        dispatcher.registerQueryHandler<vol::GetVolumetricNavVolumeDataQuery>(
            [this](const vol::GetVolumetricNavVolumeDataQuery& query) { return getVolumeData(query.entity); });
        dispatcher.registerCommandHandler<vol::SetVolumetricNavVolumeDataCommand>(
            [this](const vol::SetVolumetricNavVolumeDataCommand& cmd) { return setVolumeData(cmd.entity, cmd.data); });
        dispatcher.registerQueryHandler<vol::GetVolumetricAgentDataQuery>(
            [this](const vol::GetVolumetricAgentDataQuery& query) { return getAgentData(query.entity); });
        dispatcher.registerCommandHandler<vol::SetVolumetricAgentDataCommand>(
            [this](const vol::SetVolumetricAgentDataCommand& cmd) { return setAgentData(cmd.entity, cmd.data); });

        // Play gates the agents and refuses bakes (the Runtime publishes Edit -> Play when it starts).
        subscriptions.emplace_back(dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                playMode.store(notification.currentMode == EditorMode::Play, std::memory_order_relaxed);
                if (notification.previousMode == EditorMode::Play && notification.currentMode == EditorMode::Edit)
                    agentManager.clear();
            }));
    }

    void VolumetricNavServiceImpl::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        namespace vol = ::events::volumetric;
        dispatcher.unregisterCommandHandler<vol::RequestVolumetricBakeCommand>();
        dispatcher.unregisterQueryHandler<vol::GetVolumetricBakeOutcomeQuery>();
        dispatcher.unregisterCommandHandler<vol::CancelVolumetricBakeCommand>();
        dispatcher.unregisterCommandHandler<vol::SaveVolumetricVolumeCommand>();
        dispatcher.unregisterCommandHandler<vol::LoadSceneVolumetricVolumesCommand>();
        dispatcher.unregisterCommandHandler<vol::ClearVolumetricNavCommand>();
        dispatcher.unregisterCommandHandler<vol::UnloadVolumetricNavCommand>();
        dispatcher.unregisterQueryHandler<vol::GetVolumetricVolumeStatusQuery>();
        dispatcher.unregisterQueryHandler<vol::ListVolumetricVolumesQuery>();
        dispatcher.unregisterQueryHandler<vol::FindPath3DQuery>();
        dispatcher.unregisterQueryHandler<vol::IsPointNavigable3DQuery>();
        dispatcher.unregisterQueryHandler<vol::GetNearestNavigable3DQuery>();
        dispatcher.unregisterCommandHandler<vol::SetVolumetricAgentDestinationCommand>();
        dispatcher.unregisterCommandHandler<vol::StopVolumetricAgentCommand>();
        dispatcher.unregisterQueryHandler<vol::GetVolumetricAgentStateQuery>();
        dispatcher.unregisterQueryHandler<vol::GetVolumetricNavVolumeDataQuery>();
        dispatcher.unregisterCommandHandler<vol::SetVolumetricNavVolumeDataCommand>();
        dispatcher.unregisterQueryHandler<vol::GetVolumetricAgentDataQuery>();
        dispatcher.unregisterCommandHandler<vol::SetVolumetricAgentDataCommand>();
    }

    void VolumetricNavServiceImpl::update(float deltaTime, bool simulate)
    {
        harvestBakes();
        pruneVolumes();
        if (simulate)
            agentManager.update(deltaTime);
        if (debugVisible())
            drawDebug();
    }

    // === Bake ===

    VolumetricBakeTicket VolumetricNavServiceImpl::requestBake(EntityHandle volume)
    {
        VolumetricBakeTicket ticket;
        auto refuse = [&ticket](std::string error)
        {
            vfLogWarning("VolumetricNav: bake not started: {}", error);
            ticket.error = std::move(error);
            return ticket;
        };

        if (isPlayMode())
            return refuse("volumes cannot be baked in Play mode");

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(volume, registry))
            return refuse("the volume entity does not exist");
        const entt::entity entity = internal::fromHandle(volume);
        const auto* component = registry.try_get<components::VolumetricNavVolumeComponent>(entity);
        if (!component)
            return refuse("the entity has no VolumetricNavVolume");
        const std::optional<uint64_t> uuid = volumeUuid(registry, entity);
        if (!uuid)
            return refuse("the entity has no UUID");

        {
            std::lock_guard lock(mutex);
            for (const RunningBake& bake : bakes)
            {
                if (bake.uuid == *uuid && !bake.discard)
                    return refuse("a bake of this volume is already running");
            }
        }

        const std::string invalid = invalidVolumeReason(component->centerOffset, component->halfExtents,
                                                        component->voxelSize, component->connectivity,
                                                        component->agentClearance);
        if (!invalid.empty())
            return refuse(invalid);

        const VolumeBox box = worldBox(registry, entity, *component);
        if (!isFinite(box.min) || !isFinite(box.max))
            return refuse("the volume entity's world position is not finite");
        const int64_t voxels = volumetric::voxelCountFor(box.min, box.max, component->voxelSize);
        if (voxels <= 0)
            return refuse("the volume's box or voxel size is invalid");
        if (voxels > volumetric::kMaxVoxels)
        {
            return refuse(std::format("the volume would have {} voxels; the limit is {} - raise voxelSize to >= {:.3f}",
                                      voxels, volumetric::kMaxVoxels, volumetric::minVoxelSizeFor(box.min, box.max)));
        }

        // The world-space snapshot, taken here on the main thread: the voxelizer touches no engine state.
        auto input = std::make_shared<volumetric::VolumetricBakeInput>();
        input->boundsMin = box.min;
        input->boundsMax = box.max;
        input->settings = settingsOf(*component);
        SceneGeometrySources sources;
        sources.terrain = component->includeTerrain;
        sources.staticMeshes = component->includeStaticMeshes;
        sources.foliage = component->includeStaticMeshes;
        sources.colliders = component->includeColliders;
        sources.obstacles = false;
        SceneGeometryCollector(sources).collectVolumetric(box.min, box.max, *input);

        RunningBake bake;
        bake.uuid = *uuid;
        bake.volume = volume;
        bake.cancel = threading::CancellationToken::create();
        bake.progress = std::make_shared<std::atomic<float>>(0.0f);
        bake.started = std::chrono::steady_clock::now();

        std::shared_ptr<const volumetric::VolumetricBakeInput> snapshot = std::move(input);
        threading::CancellationToken::Ptr cancel = bake.cancel;
        std::shared_ptr<std::atomic<float>> progress = bake.progress;
        bake.future = threading::JobSystem::instance().submit(
            [snapshot, cancel, progress]()
            {
                return volumetric::voxelize(*snapshot, cancel.get(), [progress](float value)
                {
                    progress->store(value, std::memory_order_relaxed);
                });
            },
            threading::JobPriority::LOW);

        std::lock_guard lock(mutex);
        bake.ticket = nextTicket++;
        ticket.accepted = true;
        ticket.ticket = bake.ticket;
        bakes.push_back(std::move(bake));
        return ticket;
    }

    VolumetricBakeOutcome VolumetricNavServiceImpl::getBakeOutcome(uint64_t ticket) const
    {
        VolumetricBakeOutcome outcome;
        outcome.ticket = ticket;
        if (ticket == 0)
            return outcome;

        std::lock_guard lock(mutex);
        for (const RunningBake& bake : bakes)
        {
            if (bake.ticket != ticket)
                continue;
            outcome.volume = bake.volume;
            outcome.state = VolumetricBakeState::Running;
            outcome.progress = bake.progress ? bake.progress->load(std::memory_order_relaxed) : 0.0f;
            outcome.durationMs = millisecondsSince(bake.started);
            return outcome;
        }
        for (const VolumetricBakeOutcome& finished : outcomes)
        {
            if (finished.ticket == ticket)
                return finished;
        }
        return outcome;
    }

    void VolumetricNavServiceImpl::cancelBake(EntityHandle volume)
    {
        uint64_t uuid = 0;
        if (volume.isValid())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!internal::isValidHandle(volume, registry))
                return;
            const std::optional<uint64_t> id = volumeUuid(registry, internal::fromHandle(volume));
            if (!id)
                return;
            uuid = *id;
        }

        std::lock_guard lock(mutex);
        for (RunningBake& bake : bakes)
        {
            if ((uuid == 0 || bake.uuid == uuid) && bake.cancel)
                bake.cancel->cancel();
        }
    }

    void VolumetricNavServiceImpl::discardBakes(uint64_t uuid)
    {
        std::lock_guard lock(mutex);
        for (RunningBake& bake : bakes)
        {
            if (uuid != 0 && bake.uuid != uuid)
                continue;
            bake.discard = true;
            if (bake.cancel)
                bake.cancel->cancel();
        }
    }

    void VolumetricNavServiceImpl::harvestBakes()
    {
        std::vector<RunningBake> finished;
        {
            std::lock_guard lock(mutex);
            for (auto it = bakes.begin(); it != bakes.end();)
            {
                if (it->future.valid() && it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    finished.push_back(std::move(*it));
                    it = bakes.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        if (finished.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        for (RunningBake& bake : finished)
        {
            volumetric::VolumetricBakeResult result;
            std::string thrown;
            try
            {
                result = bake.future.get();
            }
            catch (const std::exception& e)
            {
                thrown = e.what();
            }

            VolumetricBakeOutcome outcome;
            outcome.ticket = bake.ticket;
            outcome.volume = bake.volume;
            outcome.durationMs = millisecondsSince(bake.started);
            outcome.progress = bake.progress ? bake.progress->load(std::memory_order_relaxed) : 0.0f;

            const entt::entity entity = scene::EntityRegistry::findByUUID(bake.uuid);
            auto* component = (entity != entt::null && registry.valid(entity))
                ? registry.try_get<components::VolumetricNavVolumeComponent>(entity)
                : nullptr;

            if (bake.discard || result.cancelled || (bake.cancel && bake.cancel->isCancelled()))
            {
                outcome.state = VolumetricBakeState::Cancelled;
                outcome.message = "the bake was cancelled";
            }
            else if (!thrown.empty() || !result.grid)
            {
                outcome.state = VolumetricBakeState::Failed;
                outcome.message = !thrown.empty() ? "the bake threw: " + thrown
                    : (result.error.empty() ? std::string("the bake failed") : result.error);
            }
            else if (!component)
            {
                outcome.state = VolumetricBakeState::Failed;
                outcome.message = "the volume no longer exists";
            }
            else
            {
                provider->setVolume(bake.uuid, result.grid);
                setLoaded(bake.uuid, result.grid, std::string(), result.navigable);
                component->isBaked = true;
                outcome.state = VolumetricBakeState::Succeeded;
                outcome.progress = 1.0f;
                outcome.dims = result.grid->dims();
                outcome.voxels = result.voxels;
                outcome.navigable = result.navigable;
                outcome.message = std::format("baked {}x{}x{} voxels ({:.0f}% navigable)", outcome.dims.x,
                                              outcome.dims.y, outcome.dims.z,
                                              navigablePercent(result.navigable, result.voxels));
            }

            recordOutcome(outcome, bake.uuid);
            if (outcome.state == VolumetricBakeState::Succeeded)
                vfLogInfo("VolumetricNav: {}", outcome.message);
            else if (outcome.state == VolumetricBakeState::Failed)
                vfLogWarning("VolumetricNav: bake failed: {}", outcome.message);

            ::events::volumetric::VolumetricBakeCompleteNotification notification;
            notification.volume = bake.volume;
            notification.ticket = bake.ticket;
            notification.success = outcome.state == VolumetricBakeState::Succeeded;
            notification.message = outcome.message;
            ::events::EventDispatcher::instance().publish(notification);
        }
    }

    void VolumetricNavServiceImpl::recordOutcome(const VolumetricBakeOutcome& outcome, uint64_t uuid)
    {
        std::lock_guard lock(mutex);
        outcomes.push_back(outcome);
        while (outcomes.size() > MAX_BAKE_OUTCOMES)
            outcomes.pop_front();
        // A volume's status reports its latest bake.
        lastBakeState[uuid] = outcome.state;
    }

    void VolumetricNavServiceImpl::setLoaded(uint64_t uuid, std::shared_ptr<const volumetric::VoxelGrid> grid,
                                             std::string source, int64_t navigable)
    {
        std::lock_guard lock(mutex);
        LoadedVolume& record = loaded[uuid];
        record.grid = std::move(grid);
        record.source = std::move(source);
        record.navigable = navigable;
    }

    // Grids whose volume entity was deleted or lost its component (e.g. the component removed) are dropped.
    void VolumetricNavServiceImpl::pruneVolumes()
    {
        std::vector<uint64_t> stale;
        {
            std::lock_guard lock(mutex);
            auto& registry = scene::EntityRegistry::getRegistry();
            for (const auto& [uuid, record] : loaded)
            {
                const entt::entity entity = scene::EntityRegistry::findByUUID(uuid);
                if (entity == entt::null || !registry.valid(entity) ||
                    !registry.all_of<components::VolumetricNavVolumeComponent>(entity))
                {
                    stale.push_back(uuid);
                }
            }
            for (uint64_t uuid : stale)
                loaded.erase(uuid);
        }
        for (uint64_t uuid : stale)
            provider->removeVolume(uuid);
    }

    // === Save / load / clear ===

    VolumetricSaveResult VolumetricNavServiceImpl::saveVolume(EntityHandle volume, const std::string& directory)
    {
        VolumetricSaveResult result;
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(volume, registry))
        {
            result.error = "the volume entity does not exist";
            return result;
        }
        const entt::entity entity = internal::fromHandle(volume);
        auto* component = registry.try_get<components::VolumetricNavVolumeComponent>(entity);
        if (!component)
        {
            result.error = "the entity has no VolumetricNavVolume";
            return result;
        }
        const std::optional<uint64_t> uuid = volumeUuid(registry, entity);
        const std::shared_ptr<const volumetric::VoxelGrid> grid = uuid ? provider->getVolume(*uuid) : nullptr;
        if (!grid)
        {
            result.error = "the volume has no baked grid to save (bake it first)";
            return result;
        }

        fs::path folder;
        if (!directory.empty())
            folder = fs::path(asset::AssetDatabase::instance().resolveAssetPath(directory));
        else if (component->bakedRef.isValid() && !component->bakedRef.resolve().empty())
            folder = fs::path(component->bakedRef.resolve()).parent_path();
        else
            folder = fs::path(asset::AssetDatabase::instance().resolveAssetPath("assets/volnav/" + sceneStem()));
        if (folder.empty())
        {
            result.error = "no folder to save the volume into";
            return result;
        }

        const fs::path path = folder / std::format("{}_{:016x}.vfVolNav", fileStem(entityName(registry, entity)), *uuid);
        if (!writeAtomically(path, volumetric::encodeVolume(*grid), result.error))
            return result;

        const std::string written = path.generic_string();
        setLoaded(*uuid, grid, written, grid->navigableCount());
        const asset::AssetRef ref = asset::AssetRef::fromPath(written);
        if (!ref.isValid())
        {
            result.error = std::format("'{}' was written, but the asset database does not know .vfVolNav files", written);
            return result;
        }
        component->bakedRef = ref;
        component->isBaked = true;

        ::events::resource::AssetSavedNotification notification;
        notification.filePath = written;
        ::events::EventDispatcher::instance().publish(notification);

        vfLogInfo("VolumetricNav: saved volume '{}' to {}", entityName(registry, entity), written);
        result.success = true;
        result.path = written;
        return result;
    }

    int VolumetricNavServiceImpl::loadSceneVolumes()
    {
        int count = 0;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VolumetricNavVolumeComponent>();
        for (auto entity : view)
        {
            auto& component = view.get<components::VolumetricNavVolumeComponent>(entity);
            const std::optional<uint64_t> uuid = volumeUuid(registry, entity);
            if (!uuid)
                continue;

            if (!component.bakedRef.isValid())
            {
                // A bake not saved yet (Play -> Stop) keeps its grid.
                component.isBaked = provider->getVolume(*uuid) != nullptr;
                continue;
            }

            const std::string path = component.bakedRef.resolve();
            {
                std::lock_guard lock(mutex);
                auto it = loaded.find(*uuid);
                if (it != loaded.end() && it->second.grid && provider->getVolume(*uuid) == it->second.grid &&
                    (it->second.source.empty() || normalizedPath(it->second.source) == normalizedPath(path)))
                {
                    // Already loaded from this file, or a newer bake not saved yet (Play -> Stop): keep it.
                    component.isBaked = true;
                    ++count;
                    continue;
                }
            }

            auto& vfs = resource::VirtualFileSystem::instance();
            const std::vector<uint8_t> bytes = (!path.empty() && vfs.exists(path)) ? vfs.readFile(path)
                                                                                   : std::vector<uint8_t>{};
            if (bytes.empty())
            {
                vfLogWarning("VolumetricNav: the baked file of volume '{}' is missing: {}",
                             entityName(registry, entity), path);
                component.isBaked = false;
                continue;
            }

            std::string error;
            std::optional<volumetric::VoxelGrid> grid = volumetric::decodeVolume(bytes, error);
            if (!grid)
            {
                vfLogWarning("VolumetricNav: '{}' of volume '{}' is not a valid .vfVolNav: {}", path,
                             entityName(registry, entity), error);
                component.isBaked = false;
                continue;
            }

            auto shared = std::make_shared<const volumetric::VoxelGrid>(std::move(*grid));
            const int64_t navigable = shared->navigableCount();
            provider->setVolume(*uuid, shared);
            setLoaded(*uuid, shared, path, navigable);
            component.isBaked = true;
            ++count;
        }
        return count;
    }

    void VolumetricNavServiceImpl::clearVolume(EntityHandle volume)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!volume.isValid())
        {
            discardBakes(0);
            {
                std::lock_guard lock(mutex);
                loaded.clear();
            }
            provider->clearVolumes();
            auto view = registry.view<components::VolumetricNavVolumeComponent>();
            for (auto entity : view)
            {
                auto& component = view.get<components::VolumetricNavVolumeComponent>(entity);
                component.bakedRef = asset::AssetRef::invalid();
                component.isBaked = false;
            }
            return;
        }

        if (!internal::isValidHandle(volume, registry))
            return;
        const entt::entity entity = internal::fromHandle(volume);
        auto* component = registry.try_get<components::VolumetricNavVolumeComponent>(entity);
        if (!component)
            return;

        if (const std::optional<uint64_t> uuid = volumeUuid(registry, entity))
        {
            discardBakes(*uuid);
            {
                std::lock_guard lock(mutex);
                loaded.erase(*uuid);
            }
            provider->removeVolume(*uuid);
        }
        component->bakedRef = asset::AssetRef::invalid();
        component->isBaked = false;
    }

    void VolumetricNavServiceImpl::unload()
    {
        discardBakes(0);
        {
            std::lock_guard lock(mutex);
            loaded.clear();
            lastBakeState.clear();
        }
        provider->clearVolumes();
        agentManager.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VolumetricNavVolumeComponent>();
        for (auto entity : view)
            view.get<components::VolumetricNavVolumeComponent>(entity).isBaked = false;
    }

    // === Status / data ===

    VolumetricVolumeStatus VolumetricNavServiceImpl::makeStatus(entt::entity entity, uint64_t uuid,
                                                               const components::VolumetricNavVolumeComponent& component) const
    {
        const auto& registry = scene::EntityRegistry::getRegistry();
        VolumetricVolumeStatus status;
        status.entity = internal::toHandle(entity);
        status.name = entityName(registry, entity);
        status.uuid = uuid;
        const VolumeBox box = worldBox(registry, entity, component);
        status.worldMin = box.min;
        status.worldMax = box.max;
        status.voxelSize = component.voxelSize;
        status.connectivity = component.connectivity;
        status.agentClearance = component.agentClearance;
        if (component.bakedRef.isValid())
            status.file = component.bakedRef.resolve();

        const std::shared_ptr<const volumetric::VoxelGrid> grid = provider->getVolume(uuid);
        int64_t navigable = 0;
        bool navigableKnown = false;
        {
            std::lock_guard lock(mutex);
            if (auto it = loaded.find(uuid); it != loaded.end() && it->second.grid == grid)
            {
                navigable = it->second.navigable;
                navigableKnown = true;
            }

            bool running = false;
            for (const RunningBake& bake : bakes)
            {
                if (bake.uuid == uuid && !bake.discard)
                {
                    running = true;
                    status.bakeState = VolumetricBakeState::Running;
                    status.progress = bake.progress ? bake.progress->load(std::memory_order_relaxed) : 0.0f;
                    break;
                }
            }
            if (!running)
            {
                if (auto it = lastBakeState.find(uuid); it != lastBakeState.end())
                {
                    status.bakeState = it->second;
                    status.progress = it->second == VolumetricBakeState::Succeeded ? 1.0f : 0.0f;
                }
            }
        }

        if (grid)
        {
            status.baked = true;
            status.dims = grid->dims();
            if (!navigableKnown)
                navigable = grid->navigableCount();
            status.navigablePercent = navigablePercent(navigable, grid->voxelCount());
            status.stale = !gridMatches(*grid, box, settingsOf(component));
        }
        return status;
    }

    std::optional<VolumetricVolumeStatus> VolumetricNavServiceImpl::getVolumeStatus(EntityHandle volume) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(volume, registry))
            return std::nullopt;
        const entt::entity entity = internal::fromHandle(volume);
        const auto* component = registry.try_get<components::VolumetricNavVolumeComponent>(entity);
        if (!component)
            return std::nullopt;
        return makeStatus(entity, volumeUuid(registry, entity).value_or(0), *component);
    }

    std::vector<VolumetricVolumeStatus> VolumetricNavServiceImpl::listVolumes() const
    {
        std::vector<VolumetricVolumeStatus> statuses;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::VolumetricNavVolumeComponent>();
        for (auto entity : view)
        {
            const auto& component = view.get<components::VolumetricNavVolumeComponent>(entity);
            statuses.push_back(makeStatus(entity, volumeUuid(registry, entity).value_or(0), component));
        }
        return statuses;
    }

    std::optional<VolumetricNavVolumeData> VolumetricNavServiceImpl::getVolumeData(EntityHandle entity) const
    {
        const std::optional<VolumetricVolumeStatus> status = getVolumeStatus(entity);
        if (!status)
            return std::nullopt;

        const auto& registry = scene::EntityRegistry::getRegistry();
        const auto& component = registry.get<components::VolumetricNavVolumeComponent>(internal::fromHandle(entity));
        VolumetricNavVolumeData data;
        data.centerOffset = component.centerOffset;
        data.halfExtents = component.halfExtents;
        data.voxelSize = component.voxelSize;
        data.connectivity = component.connectivity;
        data.agentClearance = component.agentClearance;
        data.includeTerrain = component.includeTerrain;
        data.includeStaticMeshes = component.includeStaticMeshes;
        data.includeColliders = component.includeColliders;
        data.baked = status->baked;
        data.stale = status->stale;
        data.dims = status->dims;
        data.voxels = status->baked ? static_cast<int64_t>(status->dims.x) * status->dims.y * status->dims.z : 0;
        data.navigablePercent = status->navigablePercent;
        data.bakedFile = status->file;
        return data;
    }

    bool VolumetricNavServiceImpl::setVolumeData(EntityHandle entity, const VolumetricNavVolumeData& data)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;
        auto* component = registry.try_get<components::VolumetricNavVolumeComponent>(internal::fromHandle(entity));
        if (!component)
            return false;
        if (!invalidVolumeReason(data.centerOffset, data.halfExtents, data.voxelSize, data.connectivity,
                                 data.agentClearance).empty())
            return false;

        component->centerOffset = data.centerOffset;
        component->halfExtents = data.halfExtents;
        component->voxelSize = data.voxelSize;
        component->connectivity = static_cast<uint8_t>(data.connectivity);
        component->agentClearance = data.agentClearance;
        component->includeTerrain = data.includeTerrain;
        component->includeStaticMeshes = data.includeStaticMeshes;
        component->includeColliders = data.includeColliders;
        return true;
    }

    std::optional<VolumetricAgentData> VolumetricNavServiceImpl::getAgentData(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return std::nullopt;
        const auto* component = registry.try_get<components::VolumetricAgentComponent>(internal::fromHandle(entity));
        if (!component)
            return std::nullopt;

        VolumetricAgentData data;
        data.radius = component->radius;
        data.maxSpeed = component->maxSpeed;
        data.maxAcceleration = component->maxAcceleration;
        data.stoppingDistance = component->stoppingDistance;
        data.turnSpeed = component->turnSpeed;
        data.alignPitch = component->alignPitch;
        data.registered = agentManager.isRegistered(entity);
        return data;
    }

    bool VolumetricNavServiceImpl::setAgentData(EntityHandle entity, const VolumetricAgentData& data)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
            return false;
        auto* component = registry.try_get<components::VolumetricAgentComponent>(internal::fromHandle(entity));
        if (!component)
            return false;

        const float values[] = {data.radius, data.maxSpeed, data.maxAcceleration, data.stoppingDistance, data.turnSpeed};
        for (float value : values)
        {
            if (!std::isfinite(value))
                return false;
        }
        if (!(data.radius > 0.0f) || data.maxSpeed < 0.0f || data.maxAcceleration < 0.0f ||
            data.stoppingDistance < 0.0f || data.turnSpeed < 0.0f)
            return false;

        component->radius = data.radius;
        component->maxSpeed = data.maxSpeed;
        component->maxAcceleration = data.maxAcceleration;
        component->stoppingDistance = data.stoppingDistance;
        component->turnSpeed = data.turnSpeed;
        component->alignPitch = data.alignPitch;
        return true;
    }

    // === Debug overlay ===

    // The overlay toggle (GetShowNavmeshDebugQuery) has an editor-only handler; once a query finds none (the
    // Runtime) the overlay stays off instead of throwing every frame.
    bool VolumetricNavServiceImpl::debugVisible()
    {
        if (debugToggleMissing)
            return false;
        try
        {
            return ::events::EventDispatcher::instance().query(::events::render::GetShowNavmeshDebugQuery{});
        }
        catch (const std::exception&)
        {
            debugToggleMissing = true;
            return false;
        }
    }

    // Each volume's box - cyan when its grid is loaded and current, orange when stale, gray when unbaked - and
    // every moving agent's remaining path (magenta).
    void VolumetricNavServiceImpl::drawDebug()
    {
        const glm::vec4 bakedColor{0.2f, 0.9f, 1.0f, 0.8f};
        const glm::vec4 staleColor{1.0f, 0.6f, 0.1f, 0.8f};
        const glm::vec4 unbakedColor{0.6f, 0.6f, 0.6f, 0.5f};
        const glm::vec4 pathColor{1.0f, 0.3f, 0.9f, 1.0f};

        auto& dispatcher = ::events::EventDispatcher::instance();
        try
        {
            for (const VolumetricVolumeStatus& status : listVolumes())
            {
                ::events::debugdraw::DrawBoxCommand box;
                box.center = (status.worldMin + status.worldMax) * 0.5f;
                box.halfExtents = (status.worldMax - status.worldMin) * 0.5f;
                box.color = !status.baked ? unbakedColor : (status.stale ? staleColor : bakedColor);
                dispatcher.execute(box);
            }

            for (const std::vector<glm::vec3>& path : agentManager.activePaths())
            {
                for (size_t i = 0; i + 1 < path.size(); ++i)
                {
                    ::events::debugdraw::DrawLineCommand line;
                    line.start = path[i];
                    line.end = path[i + 1];
                    line.color = pathColor;
                    dispatcher.execute(line);
                }
            }
        }
        catch (const std::exception& e)
        {
            debugToggleMissing = true; // no debug-draw handlers in this binary
            vfLogWarning("VolumetricNav: the debug overlay is unavailable: {}", e.what());
        }
    }
}
