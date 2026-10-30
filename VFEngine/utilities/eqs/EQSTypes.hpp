#pragma once

#include "../../services/data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// Environment Query System value types (rewritten for VK-1660). A query is one generator (candidate
// points or entities around the querier or the target) plus ordered tests: filters drop candidates,
// scorers rank the survivors. Templates are JSON assets (EQSQuerySpec.hpp); EQSQueryRunner runs one.
namespace eqs
{
    // Entity ids are services::EntityHandle ids. kNoEntity = none (entity id 0 is a real entity).
    inline constexpr uint64_t kNoEntity = services::EntityHandle::INVALID_ID;

    // A test writes kNoMetric for a candidate it cannot measure (PathCost: no path). Filters reject it;
    // scorers give it the worst score (0).
    inline constexpr float kNoMetric = std::numeric_limits<float>::quiet_NaN();

    struct EQSQueryHandle
    {
        uint64_t id = 0;

        bool isValid() const { return id != 0; }

        bool operator==(const EQSQueryHandle& other) const { return id == other.id; }
        bool operator!=(const EQSQueryHandle& other) const { return id != other.id; }

        struct Hash
        {
            size_t operator()(const EQSQueryHandle& handle) const
            {
                return std::hash<uint64_t>{}(handle.id);
            }
        };
    };

    // Scripts see the numbers (EQS.mt): 0 Pending, 1 Running, 2 Completed, 3 Failed.
    enum class EQSQueryStatus : uint8_t
    {
        Pending,
        Running,
        Completed,
        Failed
    };

    struct EQSCandidate
    {
        glm::vec3 position{0.0f};
        uint64_t entityId = kNoEntity; // the entity at `position` (Entities generator); kNoEntity for a point
        float totalScore = 0.0f;
        bool filtered = false;
    };

    // Who asks and about what. Positions are world space.
    struct EQSContext
    {
        uint64_t querierEntityId = kNoEntity;
        glm::vec3 querierPosition{0.0f};
        glm::vec3 querierForward{0.0f, 0.0f, 1.0f}; // XZ unit vector
        // A template that uses the target (a generator centered on it, Distance to it, Dot toTarget,
        // PathCost or Trace from it) fails when hasTarget is false.
        bool hasTarget = false;
        uint64_t targetEntityId = kNoEntity; // kNoEntity for a point target
        glm::vec3 targetPosition{0.0f};
        float agentRadius = 0.3f; // the querier's navigation size (PathCost)
        float agentHeight = 2.0f;
        uint64_t seed = 0;        // NavMeshPoints sampling: the same seed gives the same points
    };

    struct EQSResult
    {
        EQSQueryStatus status = EQSQueryStatus::Pending;
        std::vector<EQSCandidate> candidates; // best first, at most the query's maxResults
        std::string error;                    // Failed: why

        bool hasResults() const
        {
            return status == EQSQueryStatus::Completed && !candidates.empty();
        }

        glm::vec3 getBestPosition() const
        {
            return candidates.empty() ? glm::vec3{0.0f} : candidates.front().position;
        }

        uint64_t getBestEntity() const
        {
            return candidates.empty() ? kNoEntity : candidates.front().entityId;
        }

        float getBestScore() const
        {
            return candidates.empty() ? 0.0f : candidates.front().totalScore;
        }
    };

    enum class EQSScoringMode : uint8_t
    {
        Linear,        // high metric = good
        InverseLinear, // low metric = good (e.g. Distance: nearest)
        Square,
        Constant
    };

    enum class EQSScoreCombine : uint8_t
    {
        Multiply,
        Additive
    };

    enum class EQSTestPurpose : uint8_t
    {
        Filter,
        Score
    };

    // How the runner uses one test's metric.
    struct EQSTestConfig
    {
        EQSTestPurpose purpose = EQSTestPurpose::Score;
        // Filter: a candidate passes when filterMin <= metric <= filterMax; an unset bound is open.
        std::optional<float> filterMin;
        std::optional<float> filterMax;
        // Score: the metric is min-max normalised over the surviving candidates and shaped by `mode` (a metric
        // equal for all of them scores 1 in every mode), multiplied by `weight` and combined into the
        // candidate's total, which starts at 1.
        EQSScoringMode mode = EQSScoringMode::Linear;
        float weight = 1.0f;
        EQSScoreCombine combine = EQSScoreCombine::Multiply;

        bool operator==(const EQSTestConfig&) const = default;
    };

    // Entities generator input (EQSProviderRefs::findEntities).
    struct EQSEntityFilter
    {
        glm::vec3 center{0.0f};
        float radius = 25.0f;          // 3D distance from center to the entity's world position
        std::string name;              // NameComponent name
        bool prefix = false;           // match names that start with `name` instead of equal names
        bool includeInactive = false;
        uint64_t exclude = kNoEntity;  // the querier
        uint32_t maxCount = 64;        // the nearest ones
    };

    struct EQSEntityCandidate
    {
        uint64_t entityId = kNoEntity;
        glm::vec3 position{0.0f}; // world position
    };

    // Engine access for generators and tests. A generator or test that needs a function left unset fails the
    // query (EQSResult::error names it) instead of guessing.
    struct EQSProviderRefs
    {
        std::function<bool(const glm::vec3& point, float tolerance)> isPointOnNavmesh;
        // The nearest navmesh point within searchRadius; nullopt when there is none.
        std::function<std::optional<glm::vec3>(const glm::vec3& point, float searchRadius)> projectToNavmesh;
        // True when nothing blocks the segment from -> to. Hits on ignoreA / ignoreB (kNoEntity = none) and on
        // their descendants are skipped.
        std::function<bool(const glm::vec3& from, const glm::vec3& to, uint64_t ignoreA, uint64_t ignoreB)> lineOfSight;
        // Navmesh path length for an agent of the given size; < 0 = unreachable.
        std::function<float(const glm::vec3& from, const glm::vec3& to, float agentRadius, float agentHeight)> pathLength;
        // Entities matching the filter, nearest first, at most filter.maxCount.
        std::function<std::vector<EQSEntityCandidate>(const EQSEntityFilter& filter)> findEntities;
    };

    // Per-candidate detail of one run (debug overlay, MCP eqs_run).
    struct EQSDebugCandidate
    {
        glm::vec3 position{0.0f};
        uint64_t entityId = kNoEntity;
        int filteredByTest = -1;    // index into EQSDebugResult::testNames of the filter that dropped it; -1 = kept
        std::vector<float> metrics; // per test, kNoMetric where it was not measured (dropped earlier) or has none
        std::vector<float> scores;  // per test: the shaped, weighted score (0 for filters and dropped candidates)
        float total = 0.0f;         // the candidate's final total (0 when dropped)
    };

    struct EQSDebugResult
    {
        EQSResult result;
        std::vector<std::string> testNames;        // the query's tests in declared order
        std::vector<EQSDebugCandidate> candidates; // every generated candidate, in generation order
        double elapsedMs = 0.0;
    };
}
