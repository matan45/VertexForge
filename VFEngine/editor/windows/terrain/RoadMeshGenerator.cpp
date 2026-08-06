#include "RoadMeshGenerator.hpp"
#include "RoadEntityUndoCommand.hpp"

#include "print/Log.hpp"
#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "asset/AssetRef.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "terrain/SplineSampling.hpp"
#include "types/ProceduralMeshWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    // Strips characters illegal in Windows file names. Mirrors the importer's sanitizeFileStem
    // (Mesh.cpp:371), which is TU-local there.
    [[nodiscard]] std::string sanitizeFileStem(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            const auto uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
                c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                out.push_back('_');
            else
                out.push_back(c);
        }

        const size_t start = out.find_first_not_of(" .");
        if (start == std::string::npos)
            return {};
        const size_t end = out.find_last_not_of(" .");
        return out.substr(start, end - start + 1);
    }

    [[nodiscard]] std::string roadOutputDirectory()
    {
        auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
        if (!project || project->workingDirectory.empty())
            return {};
        return (std::filesystem::path(project->workingDirectory) / "Generated" / "Roads").string();
    }
}

namespace windows
{
    RoadMeshGenerator::~RoadMeshGenerator()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (appliedToken.isValid())
            dispatcher.unsubscribe(appliedToken);
        if (deletedToken.isValid())
            dispatcher.unsubscribe(deletedToken);
    }

    void RoadMeshGenerator::subscribe()
    {
        if (subscribed)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        appliedToken = dispatcher
            .subscribe<events::splineTerrain::SplineAppliedNotification>(
                [this](const events::splineTerrain::SplineAppliedNotification& applied)
                {
                    if (terrain::hasOp(applied.params.ops, terrain::SplineOps::Mesh))
                        generate(applied);
                });

        // VK-1648. The road entities are spawned here, so they have to be retired here too. The
        // Height Layers panel's Delete only removes the layer and recomposes the terrain; without
        // this the ribbon survives its own corridor and ends up floating over restored ground.
        //
        // KNOWN GAP: this retire is not itself undoable, so undoing the layer delete brings the
        // layer back without the road. That is still strictly better than the previous behaviour
        // (road left floating over restored ground, with no way to remove it), but closing it
        // properly means a RoadEntityUndoCommand with the roles inverted, pushed into the same
        // batch deleteSpline records its HeightLayerRecordUndoCommand in — which needs the spawn
        // desc reconstructed from the component plus its children's chunk origins.
        deletedToken = dispatcher
            .subscribe<events::splineTerrain::SplineDeletedNotification>(
                [this](const events::splineTerrain::SplineDeletedNotification& deleted)
                {
                    retireRoadForSpline(deleted.splineId);
                });

        subscribed = true;
    }

    void RoadMeshGenerator::retireRoadForSpline(uint64_t splineId)
    {
        if (splineId == 0)
            return;

        // splineId is persisted on the component (VK-1647), so this also finds roads spawned in an
        // earlier session — the same reason deleteSpline stopped gating on its session-only
        // appliedSplines list.
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<std::pair<services::EntityHandle, asset::AssetRef>> doomed;
        for (auto entity : registry.view<components::RoadSplineComponent>())
        {
            const auto& road = registry.get<components::RoadSplineComponent>(entity);
            if (road.splineId == splineId)
                doomed.emplace_back(services::internal::toHandle(entity), road.generatedMeshRef);
        }

        // Collected before deleting: retireRoad destroys entities, and doing that while iterating
        // an EnTT view over the component being removed invalidates the iteration.
        for (const auto& [entity, meshRef] : doomed)
            retireRoad(entity, meshRef);

        revisionBySpline.erase(splineId);
    }

    void RoadMeshGenerator::update()
    {
        subscribe();
    }

    std::vector<RoadMeshGenerator::RoadEntry> RoadMeshGenerator::listRoads()
    {
        std::vector<RoadEntry> roads;
        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto entity : registry.view<components::RoadSplineComponent>())
        {
            const auto& road = registry.get<components::RoadSplineComponent>(entity);
            RoadEntry entry;
            entry.entity = services::internal::toHandle(entity);
            entry.name = road.params.roadName.empty() ? ("Road_" + std::to_string(road.splineId))
                                                      : road.params.roadName;
            entry.chunkCount = road.chunkCount;
            roads.push_back(std::move(entry));
        }
        return roads;
    }

    bool RoadMeshGenerator::openForEdit(services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity handle = services::internal::fromHandle(entity);
        if (!registry.valid(handle))
            return false;

        const auto* road = registry.try_get<components::RoadSplineComponent>(handle);
        if (!road || road->controlPoints.size() < 2)
            return false;

        events::splineTerrain::LoadSplineForEditCommand cmd;
        cmd.controlPoints.reserve(road->controlPoints.size());
        for (const glm::vec3& point : road->controlPoints)
            cmd.controlPoints.push_back(terrain::SplineControlPoint{point});
        cmd.params = road->params;
        cmd.replacesEntityId = entity.id;

        // VK-1647: the height layer this road already owns, so regenerating updates that corridor
        // in place instead of stacking a second one over it. Persisted on the component, so it
        // survives a reload — which is also why layer ids had to stop being session-scoped.
        cmd.editsSplineId = road->splineId;

        return events::EventDispatcher::instance().query(cmd);
    }

    void RoadMeshGenerator::generate(const events::splineTerrain::SplineAppliedNotification& applied)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Same sampling the service used for sculpt/paint, so the ribbon sits on the corridor that
        // was actually flattened.
        const float sampleStep = (applied.params.road.ringSpacing > 0.0f)
                                     ? std::min(0.5f, applied.params.road.ringSpacing * 0.5f)
                                     : 0.5f;

        events::splineTerrain::BuildSplineRoadMeshQuery buildQuery;
        buildQuery.splineSamples = terrain::sampleSplineCurve(applied.controlPoints, sampleStep);
        buildQuery.profile = applied.params.road;

        const terrain::RoadMeshData road = dispatcher.query(buildQuery);
        if (!road.valid || road.chunks.empty())
        {
            vfLogError("Road '{}': no geometry was produced (spline {})",
                       applied.params.roadName, applied.splineId);
            return;
        }
        if (road.clamped)
        {
            vfLogWarning("Road '{}': corners tighter than the road width were pinched "
                         "(minimum turn radius {:.2f} m)", applied.params.roadName, road.minTurnRadius);
        }

        // Regenerating: continue the previous road's revision numbering (persisted on its
        // component, so it survives a reload) rather than restarting at 1 and writing over the
        // .vfMesh that is currently resident.
        auto& registry = scene::EntityRegistry::getRegistry();
        asset::AssetRef previousMeshRef;
        uint32_t previousRevision = 0;
        services::EntityHandle replaced = services::EntityHandle::invalid();

        if (applied.replacesEntityId != 0)
        {
            replaced = services::EntityHandle{applied.replacesEntityId};
            const entt::entity entity = services::internal::fromHandle(replaced);
            if (registry.valid(entity))
            {
                if (const auto* existing = registry.try_get<components::RoadSplineComponent>(entity))
                {
                    previousRevision = existing->revision;
                    previousMeshRef = existing->generatedMeshRef;
                }
            }
            else
            {
                replaced = services::EntityHandle::invalid();
            }
        }

        uint32_t& tracked = revisionBySpline[applied.splineId];
        tracked = std::max({tracked, previousRevision, 0u}) + 1;
        const uint32_t revision = tracked;

        const std::string meshPath = writeRoadAsset(road, applied.params.roadName, revision);
        if (meshPath.empty())
            return;

        // Drop the old road only once the replacement is safely on disk, so a failed write leaves
        // the existing road intact.
        if (replaced.isValid())
            retireRoad(replaced, previousMeshRef);

        RoadSpawnDesc desc;
        desc.meshPath = meshPath;
        desc.params = applied.params;
        desc.splineId = applied.splineId;
        desc.revision = revision;
        desc.chunkOrigins.reserve(road.chunks.size());
        for (const terrain::RoadChunk& chunk : road.chunks)
            desc.chunkOrigins.push_back(chunk.origin);
        desc.controlPoints.reserve(applied.controlPoints.size());
        for (const terrain::SplineControlPoint& point : applied.controlPoints)
            desc.controlPoints.push_back(point.position);

        const services::EntityHandle root = spawnRoad(desc);
        if (!root.isValid())
            return;

        // Joins the batch the spline service opened around this apply, so one Ctrl+Z takes the
        // terrain deformation, the painted weights and the road entities together. The .vfMesh is
        // deliberately left on disk on undo — redo respawns from it instead of regenerating.
        events::undoredo::PushUndoableCommand pushCmd;
        pushCmd.command = std::make_shared<RoadEntityUndoCommand>(desc, root);
        events::EventDispatcher::instance().execute(pushCmd);
    }

    void RoadMeshGenerator::retireRoad(services::EntityHandle entity, const asset::AssetRef& meshRef) const
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Deleting the root takes its chunk children with it, which is what releases the mesh
        // references (MeshComponentService::setMeshData / AssetLifecycleManager) and lets the
        // streamer drop the old file.
        events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = entity;
        dispatcher.execute(deleteCmd);

        if (!meshRef.isValid())
            return;

        const std::string previousPath = meshRef.resolve();

        events::assetdb::UnregisterAssetCommand unregisterCmd;
        unregisterCmd.guid = meshRef.getGUID();
        dispatcher.execute(unregisterCmd);

        if (previousPath.empty())
            return;

        // Best-effort: the streamer may still hold the file open for another frame
        // (MeshStreamHandle.hpp:70), in which case Windows refuses the delete. That is harmless —
        // the new revision is already live — so log it rather than treating it as an error.
        std::error_code ec;
        std::filesystem::remove(previousPath, ec);
        if (ec)
            vfLogDebug("Road: could not remove the superseded mesh '{}' ({})", previousPath, ec.message());
        else
            std::filesystem::remove(previousPath + ".vfmeta", ec);
    }

    std::string RoadMeshGenerator::writeRoadAsset(const terrain::RoadMeshData& road,
                                                  const std::string& roadName,
                                                  uint32_t revision) const
    {
        const std::string directory = roadOutputDirectory();
        if (directory.empty())
        {
            vfLogError("Road '{}': no project is open, so there is nowhere to write the mesh", roadName);
            return {};
        }

        std::string stem = sanitizeFileStem(roadName);
        if (stem.empty())
            stem = "Road";

        const std::string meshPath =
            (std::filesystem::path(directory) / (stem + "_r" + std::to_string(revision) + ".vfMesh")).string();

        std::vector<types::ProceduralSubmesh> submeshes;
        submeshes.reserve(road.chunks.size());
        for (size_t i = 0; i < road.chunks.size(); ++i)
        {
            types::ProceduralSubmesh submesh;
            submesh.name = stem + "_" + std::to_string(i);
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                submesh.lods[lod].vertices = road.chunks[i].lods[lod].vertices;
                submesh.lods[lod].indices = road.chunks[i].lods[lod].indices;
            }
            submeshes.push_back(std::move(submesh));
        }

        const types::ProceduralMeshWriteResult written =
            types::ProceduralMeshWriter::write(meshPath, submeshes);
        if (!written.success)
        {
            vfLogError("Road '{}': {}", roadName, written.message);
            return {};
        }

        // Registering through the service (rather than letting AssetRef::fromPath auto-register)
        // is what writes the .vfmeta AND publishes AssetRegisteredNotification, so the content
        // browser shows the new road without a manual refresh.
        events::assetdb::RegisterAssetCommand registerCmd;
        registerCmd.path = meshPath;
        registerCmd.type = resource::AssetType::Mesh;
        registerCmd.importSource = "procedural://road";
        events::EventDispatcher::instance().execute(registerCmd);

        return meshPath;
    }

    services::EntityHandle RoadMeshGenerator::spawnRoad(const RoadSpawnDesc& desc)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const terrain::SplineParams& params = desc.params;

        const std::string rootName =
            params.roadName.empty() ? ("Road_" + std::to_string(desc.splineId)) : params.roadName;

        events::scene::CreateEntityCommand createRoot;
        createRoot.name = rootName;
        const services::EntityHandle root = dispatcher.execute(createRoot);
        if (!root.isValid())
        {
            vfLogError("Road '{}': failed to create the root entity", rootName);
            return services::EntityHandle::invalid();
        }

        // The root stays at the origin with an identity transform, so each chunk's LOCAL transform
        // is its world origin and the mesh vertices (stored chunk-relative) land exactly where the
        // spline put them.
        const asset::AssetRef meshRef = asset::AssetRef::fromPath(desc.meshPath);

        for (size_t i = 0; i < desc.chunkOrigins.size(); ++i)
        {
            events::scene::CreateEntityCommand createChunk;
            createChunk.name = rootName + "_" + std::to_string(i);
            createChunk.parent = root;
            const services::EntityHandle entity = dispatcher.execute(createChunk);
            if (!entity.isValid())
                continue;

            services::TransformData transform;
            transform.position = desc.chunkOrigins[i];
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = transform;
            dispatcher.execute(transformCmd);

            events::scene::AddMeshComponentCommand addMesh;
            addMesh.entity = entity;
            dispatcher.execute(addMesh);

            // One submesh per chunk. GPUObjectStreamManager.cpp:326-355 builds a GPUObjectData per
            // submesh using the SUBMESH's own AABB and honours submeshIndex, so each chunk frustum-
            // culls on its own bounds instead of the whole road's.
            services::MeshData meshData;
            meshData.meshRef = meshRef;
            meshData.submeshIndex = static_cast<int32_t>(i);
            events::scene::SetMeshDataCommand meshCmd;
            meshCmd.entity = entity;
            meshCmd.meshData = meshData;
            dispatcher.execute(meshCmd);

            events::material::AddMaterialComponentCommand addMaterial;
            addMaterial.entity = entity;
            dispatcher.execute(addMaterial);

            if (!params.roadMaterialPath.empty())
            {
                events::material::SetDefaultMaterialCommand setMaterial;
                setMaterial.entity = entity;
                setMaterial.materialPath = params.roadMaterialPath;
                dispatcher.execute(setMaterial);
            }

            if (params.roadCollider)
            {
                events::scene::AddColliderComponentCommand addCollider;
                addCollider.entity = entity;
                dispatcher.execute(addCollider);

                // TriangleMesh, not ConvexMesh: a road is a thin non-convex ribbon, and the convex
                // block was written empty. PhysicsShapeFactory reads LOD 2 for this shape
                // (PhysicsShapeFactory.cpp:222), which the ring-decimated chain keeps faithful.
                services::ColliderComponentData colliderData;
                colliderData.shape = types::ColliderShape::TriangleMesh;
                colliderData.meshRef = meshRef;
                colliderData.submeshIndex = static_cast<int32_t>(i);
                events::scene::SetColliderDataCommand setCollider;
                setCollider.entity = entity;
                setCollider.colliderData = colliderData;
                dispatcher.execute(setCollider);

                events::scene::AddRigidBodyComponentCommand addBody;
                addBody.entity = entity;
                dispatcher.execute(addBody);

                services::RigidBodyComponentData bodyData;
                bodyData.type = types::RigidBodyType::Static;
                events::scene::SetRigidBodyDataCommand setBody;
                setBody.entity = entity;
                setBody.rigidBodyData = bodyData;
                dispatcher.execute(setBody);
            }
        }

        // The road's own authoring data lives on the road, so scene serialization persists it and
        // the tool can re-open it after a reload.
        components::RoadSplineComponent component;
        component.controlPoints = desc.controlPoints;
        component.params = params;
        component.generatedMeshRef = meshRef;
        component.splineId = desc.splineId;
        component.revision = desc.revision;
        component.chunkCount = static_cast<uint32_t>(desc.chunkOrigins.size());

        scene::EntityRegistry::getRegistry().emplace_or_replace<components::RoadSplineComponent>(
            services::internal::fromHandle(root), std::move(component));

        vfLogInfo("Road '{}' r{} spawned: {} chunk(s) -> {}",
                  rootName, desc.revision, desc.chunkOrigins.size(), desc.meshPath);
        return root;
    }
}
