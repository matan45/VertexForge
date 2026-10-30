#include "EQSAdapter.hpp"
#include "../../../services/providers/physics/IPhysicsProvider.hpp"
#include "../../../services/providers/navmesh/INavmeshProvider.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "asset/AssetDatabase.hpp"
#include "behaviortree/SightLine.hpp"
#include "components/CoreComponents.hpp"
#include "eqs/EQSQueryRunner.hpp"
#include "eqs/EQSQuerySpec.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace core
{
    namespace
    {
        // A projected point counts as on the navmesh within this distance (getClosestPoint echoes its input
        // when nothing is near, so every projection is checked again).
        constexpr float kProjectTolerance = 0.5f;

        // A hit within this distance of the end point no longer blocks the line of sight (as the BT LoS).
        constexpr float kSightTolerance = 0.1f;

        // A path whose last point ends farther than this (plus the agent radius) from the goal on XZ did not
        // reach it - the hierarchical path does not always set isPartial.
        constexpr float kPathEndTolerance = 1.0f;

        // True when `entity` is the entity with id `ancestorId` or below it (ParentComponent chain).
        bool isSelfOrDescendant(const entt::registry& registry, services::EntityHandle entity, uint64_t ancestorId)
        {
            if (!entity.isValid() || ancestorId == eqs::kNoEntity)
                return false;

            constexpr int kMaxDepth = 256;
            const entt::entity ancestor = services::internal::fromHandle(services::EntityHandle{ancestorId});
            entt::entity current = services::internal::fromHandle(entity);
            for (int depth = 0; depth < kMaxDepth && current != entt::null && registry.valid(current); ++depth)
            {
                if (current == ancestor)
                    return true;
                const auto* parent = registry.try_get<components::ParentComponent>(current);
                current = parent ? parent->parent : entt::null;
            }
            return false;
        }

        std::string firstError(const eqs::EQSQueryLibrary::Lookup& lookup)
        {
            if (lookup.errors.empty())
                return "the template could not be built";
            const eqs::EQSSpecError& error = lookup.errors.front();
            return error.path.empty() ? error.message : error.path + ": " + error.message;
        }
    }

    EQSAdapter::EQSAdapter(services::IPhysicsProvider* physicsProvider,
                           services::INavmeshProvider* navmeshProvider)
        : physicsProvider(physicsProvider),
          navmeshProvider(navmeshProvider),
          providers(buildProviderRefs()),
          library([](const std::string& name) -> std::optional<std::string>
          {
              // The VFS reads loose files and packed games alike.
              const std::string path = templatePath(name);
              auto& vfs = resource::VirtualFileSystem::instance();
              if (!vfs.exists(path))
                  return std::nullopt;
              const std::vector<uint8_t> bytes = vfs.readFile(path);
              return std::string(bytes.begin(), bytes.end());
          })
    {
    }

    EQSAdapter::~EQSAdapter() = default;

    std::string EQSAdapter::templatePath(const std::string& queryName)
    {
        return asset::AssetDatabase::instance().resolveAssetPath("assets/eqs/" + queryName + ".vfEQSQuery");
    }

    eqs::EQSQueryHandle EQSAdapter::submitQuery(const std::string& queryName, const eqs::EQSContext& context)
    {
        const eqs::EQSQueryLibrary::Lookup lookup = library.lookup(queryName);
        if (!lookup.def)
        {
            warnOnce(queryName, "EQS: query '" + queryName + "' not run: " + firstError(lookup));
            return {};
        }

        const eqs::EQSQueryHandle handle = scheduler.submit(lookup.def, context);
        if (!handle.isValid())
        {
            warnOnce("#queue", "EQS: the query queue is full (" + std::to_string(eqs::EQSScheduler::kMaxPending) +
                               " waiting); queries are dropped until it drains");
        }
        return handle;
    }

    eqs::EQSResult EQSAdapter::getResult(eqs::EQSQueryHandle handle) const
    {
        return scheduler.getResult(handle);
    }

    void EQSAdapter::releaseQuery(eqs::EQSQueryHandle handle)
    {
        scheduler.release(handle);
    }

    void EQSAdapter::releaseAll()
    {
        scheduler.releaseAll();
        std::lock_guard lock(warningMutex);
        warnedKeys.clear();
    }

    void EQSAdapter::update(float budgetMs)
    {
        scheduler.update(providers, budgetMs);
    }

    eqs::EQSDebugResult EQSAdapter::runQueryNow(const std::string& queryName, const eqs::EQSContext& context)
    {
        eqs::EQSDebugResult debug;
        const eqs::EQSQueryLibrary::Lookup lookup = library.lookup(queryName);
        if (!lookup.def)
        {
            debug.result.status = eqs::EQSQueryStatus::Failed;
            debug.result.error = "query '" + queryName + "': " + firstError(lookup);
            return debug;
        }

        debug.result = eqs::runQuery(*lookup.def, context, providers, &debug);
        return debug;
    }

    services::EQSQueryInfo EQSAdapter::lookupQuery(const std::string& queryName)
    {
        services::EQSQueryInfo info;
        info.name = queryName;
        if (eqs::isValidQueryName(queryName))
            info.path = templatePath(queryName);

        const eqs::EQSQueryLibrary::Lookup lookup = library.lookup(queryName);
        info.exists = lookup.exists;
        info.valid = lookup.def != nullptr;
        info.errors = lookup.errors;
        if (lookup.spec)
        {
            info.description = lookup.spec->description;
            info.summary = eqs::summarizeQuery(*lookup.spec);
        }
        return info;
    }

    void EQSAdapter::invalidateQuery(const std::string& queryName)
    {
        library.invalidate(queryName);
        std::lock_guard lock(warningMutex);
        if (queryName.empty())
            warnedKeys.clear();
        else
            warnedKeys.erase(queryName);
    }

    void EQSAdapter::warnOnce(const std::string& key, const std::string& message)
    {
        {
            std::lock_guard lock(warningMutex);
            if (!warnedKeys.insert(key).second)
                return;
        }
        vfLogWarning("{}", message);
    }

    eqs::EQSProviderRefs EQSAdapter::buildProviderRefs() const
    {
        eqs::EQSProviderRefs refs;
        services::INavmeshProvider* navmesh = navmeshProvider;
        services::IPhysicsProvider* physics = physicsProvider;

        if (navmesh)
        {
            refs.isPointOnNavmesh = [navmesh](const glm::vec3& point, float tolerance)
            {
                return navmesh->hasNavmesh() && navmesh->isPointOnNavmesh(point, tolerance);
            };

            refs.projectToNavmesh = [navmesh](const glm::vec3& point, float searchRadius) -> std::optional<glm::vec3>
            {
                if (!navmesh->hasNavmesh())
                    return std::nullopt;
                const glm::vec3 candidate = navmesh->getClosestPoint(point, searchRadius);
                if (!navmesh->isPointOnNavmesh(candidate, kProjectTolerance))
                    return std::nullopt;
                return candidate;
            };

            refs.pathLength = [navmesh](const glm::vec3& from, const glm::vec3& to, float agentRadius,
                                        float agentHeight)
            {
                if (!navmesh->hasNavmesh())
                    return -1.0f;
                const navigation::NavPath path = navmesh->findPath(from, to, agentRadius, agentHeight);
                if (!path.isValid || path.isPartial || path.waypoints.empty())
                    return -1.0f;

                const glm::vec3& end = path.waypoints.back();
                const float endDx = end.x - to.x;
                const float endDz = end.z - to.z;
                const float reach = kPathEndTolerance + agentRadius;
                if (endDx * endDx + endDz * endDz > reach * reach)
                    return -1.0f;

                float length = 0.0f;
                for (size_t i = 1; i < path.waypoints.size(); ++i)
                    length += glm::distance(path.waypoints[i - 1], path.waypoints[i]);
                return length;
            };
        }

        if (physics)
        {
            refs.lineOfSight = [physics](const glm::vec3& from, const glm::vec3& to, uint64_t ignoreA,
                                         uint64_t ignoreB)
            {
                const glm::vec3 delta = to - from;
                const float distance = glm::length(delta);
                if (distance < 1e-4f)
                    return true;

                const std::vector<services::RaycastHit> hits = physics->raycastAll(from, delta / distance, distance);
                const auto& registry = scene::EntityRegistry::getRegistry();
                std::vector<behaviortree::SightHit> sightHits;
                sightHits.reserve(hits.size());
                for (const services::RaycastHit& hit : hits)
                {
                    if (!hit.hit)
                        continue;
                    behaviortree::SightHit sightHit;
                    sightHit.distance = hit.distance;
                    sightHit.kind = (isSelfOrDescendant(registry, hit.entity, ignoreA) ||
                                     isSelfOrDescendant(registry, hit.entity, ignoreB))
                        ? behaviortree::SightHitKind::Ignored
                        : behaviortree::SightHitKind::Blocker;
                    sightHits.push_back(sightHit);
                }
                return behaviortree::isSightLineClear(std::move(sightHits), distance, kSightTolerance);
            };
        }

        refs.findEntities = [](const eqs::EQSEntityFilter& filter)
        {
            struct Found
            {
                eqs::EQSEntityCandidate candidate;
                float distanceSquared = 0.0f;
            };

            std::vector<Found> found;
            if (filter.name.empty())
                return std::vector<eqs::EQSEntityCandidate>{};

            auto& registry = scene::EntityRegistry::getRegistry();
            const float radiusSquared = filter.radius * filter.radius;
            auto view = registry.view<components::NameComponent, components::TransformComponent>();
            for (const entt::entity entity : view)
            {
                const uint64_t id = services::internal::toHandle(entity).id;
                if (id == filter.exclude)
                    continue;

                const std::string& name = view.get<components::NameComponent>(entity).name;
                if (filter.prefix ? !name.starts_with(filter.name) : name != filter.name)
                    continue;
                // Like the scripts' Entity::isActive, but a deactivated parent hides its children too.
                if (!filter.includeInactive && !scene::Entity::isEffectivelyActive(registry, entity))
                    continue;

                const glm::vec3 position = scene::composeWorldPosition(registry, entity);
                const glm::vec3 offset = position - filter.center;
                const float distanceSquared = glm::dot(offset, offset);
                if (distanceSquared > radiusSquared)
                    continue;
                found.push_back({{id, position}, distanceSquared});
            }

            // Nearest first; equal distances by id, so the order never depends on the registry's.
            std::sort(found.begin(), found.end(), [](const Found& a, const Found& b)
            {
                if (a.distanceSquared != b.distanceSquared)
                    return a.distanceSquared < b.distanceSquared;
                return a.candidate.entityId < b.candidate.entityId;
            });

            std::vector<eqs::EQSEntityCandidate> result;
            const size_t count = std::min<size_t>(found.size(), filter.maxCount);
            result.reserve(count);
            for (size_t i = 0; i < count; ++i)
                result.push_back(found[i].candidate);
            return result;
        };

        return refs;
    }
}
