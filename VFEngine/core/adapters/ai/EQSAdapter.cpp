#include "EQSAdapter.hpp"
#include "../../../services/providers/physics/IPhysicsProvider.hpp"
#include "../../../services/providers/navmesh/INavmeshProvider.hpp"
#include "print/Log.hpp"

namespace core
{
    EQSAdapter::EQSAdapter(services::IPhysicsProvider* physicsProvider,
                           services::INavmeshProvider* navmeshProvider)
        : physicsProvider(physicsProvider),
          navmeshProvider(navmeshProvider)
    {
        registerBuiltInQueries();
    }

    EQSAdapter::~EQSAdapter() = default;

    void EQSAdapter::registerQuery(const std::string& name, const eqs::EQSQueryDef& def)
    {
        queryDefs[name] = def;
    }

    eqs::EQSQueryHandle EQSAdapter::submitQuery(const std::string& queryName, const eqs::EQSContext& context)
    {
        auto it = queryDefs.find(queryName);
        if (it == queryDefs.end())
        {
            vfLogWarning("EQS: query '{}' not registered", queryName);
            return eqs::EQSQueryHandle{};
        }

        auto providers = buildProviderRefs();
        return engine.submitQuery(it->second, context, providers);
    }

    eqs::EQSResult EQSAdapter::getResult(eqs::EQSQueryHandle handle) const
    {
        return engine.getResult(handle);
    }

    void EQSAdapter::cancelQuery(eqs::EQSQueryHandle handle)
    {
        engine.cancelQuery(handle);
    }

    void EQSAdapter::update(float /*frameBudgetMs*/)
    {
        engine.update();
    }

    void EQSAdapter::registerBuiltInQueries()
    {
        // Built-in queries can be registered here when generators/tests are available.
        // Example:
        // eqs::EQSQueryDef findCover;
        // findCover.name = "FindCover";
        // findCover.generator = std::make_shared<SomeGenerator>(...);
        // findCover.tests.push_back({std::make_shared<SomeTest>(...), config});
        // findCover.maxResults = 1;
        // queryDefs["FindCover"] = std::move(findCover);
    }

    eqs::EQSProviderRefs EQSAdapter::buildProviderRefs() const
    {
        eqs::EQSProviderRefs refs;

        refs.isPointOnNavmesh = [this](const glm::vec3& point, float tolerance) -> bool
        {
            if (!navmeshProvider) return false;
            return navmeshProvider->isPointOnNavmesh(point, tolerance);
        };

        refs.getClosestPointOnNavmesh = [this](const glm::vec3& point, float searchRadius) -> glm::vec3
        {
            if (!navmeshProvider) return point;
            return navmeshProvider->getClosestPoint(point, searchRadius);
        };

        refs.raycast = [this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> bool
        {
            if (!physicsProvider) return false;
            auto hit = physicsProvider->raycast(origin, direction, maxDistance);
            return hit.hit;
        };

        refs.pathfindingCost = [this](const glm::vec3& from, const glm::vec3& to) -> float
        {
            if (!navmeshProvider) return -1.0f;
            auto path = navmeshProvider->findPath(from, to, 0.3f, 2.0f);
            if (path.waypoints.empty()) return -1.0f;

            float totalLength = 0.0f;
            for (size_t i = 1; i < path.waypoints.size(); ++i)
            {
                totalLength += glm::distance(path.waypoints[i - 1], path.waypoints[i]);
            }
            return totalLength;
        };

        return refs;
    }
}
