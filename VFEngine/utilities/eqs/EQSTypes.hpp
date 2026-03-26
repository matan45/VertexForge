#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <functional>
#include "../../services/data/EntityHandle.hpp"

namespace eqs
{
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

    enum class EQSQueryStatus : uint8_t
    {
        Pending,
        Running,
        Completed,
        Failed
    };

    struct EQSCandidate
    {
        glm::vec3 position{ 0.0f };
        float totalScore = 0.0f;
        bool filtered = false;
    };

    struct EQSContext
    {
        services::EntityHandle querierEntity;
        glm::vec3 querierPosition{ 0.0f };
        glm::vec3 querierForward{ 0.0f, 0.0f, 1.0f };
        services::EntityHandle targetEntity;
        glm::vec3 targetPosition{ 0.0f };
        bool hasTarget = false;
    };

    struct EQSResult
    {
        EQSQueryStatus status = EQSQueryStatus::Pending;
        std::vector<EQSCandidate> candidates;

        glm::vec3 getBestPosition() const
        {
            if (candidates.empty())
                return glm::vec3{ 0.0f };
            return candidates.front().position;
        }

        float getBestScore() const
        {
            if (candidates.empty())
                return 0.0f;
            return candidates.front().totalScore;
        }

        bool hasResults() const
        {
            return status == EQSQueryStatus::Completed && !candidates.empty();
        }
    };

    enum class EQSScoringMode : uint8_t
    {
        Linear,
        InverseLinear,
        Square,
        Constant
    };

    enum class EQSScoreCombine : uint8_t
    {
        Multiply,
        Additive
    };

    struct EQSTestConfig
    {
        float weight = 1.0f;
        EQSScoringMode scoringMode = EQSScoringMode::Linear;
        EQSScoreCombine scoreCombine = EQSScoreCombine::Multiply;
        bool isFilter = false;
        float filterThreshold = 0.0f;
    };

    struct EQSProviderRefs
    {
        std::function<bool(const glm::vec3&, float)> isPointOnNavmesh;
        std::function<glm::vec3(const glm::vec3&, float)> getClosestPointOnNavmesh;
        std::function<bool(const glm::vec3&, const glm::vec3&, float)> raycast;
        std::function<float(const glm::vec3&, const glm::vec3&)> pathfindingCost;
    };
}
