#include "AnimationRigSupport.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "math/TransformUtils.hpp"

#include <cmath>
#include <filesystem>

namespace mcp::tools::rig
{
    std::optional<services::MeshData> meshData(const services::EntityHandle& entity)
    {
        events::scene::GetMeshDataQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    std::string meshPath(const services::MeshData& data)
    {
        return data.meshRef.isValid() ? data.meshRef.resolve() : std::string();
    }

    nlohmann::json meshJson(const services::MeshData& data)
    {
        if (!data.meshRef.isValid())
        {
            return nullptr;
        }
        const std::string path = meshPath(data);
        return path.empty() ? nlohmann::json("guid:" + data.meshRef.toHexString()) : nlohmann::json(agentPath(path));
    }

    std::string agentPath(const std::string& enginePath)
    {
        if (enginePath.empty())
        {
            return {};
        }
        const std::optional<std::filesystem::path> root = tryTerrainProjectRoot();
        return projectRelativeUtf8(root, terrainEnginePath(root, enginePath));
    }

    services::MeshSkeletonInfo readSkeleton(const std::string& meshPath)
    {
        events::animationAuthoring::GetMeshSkeletonQuery query;
        query.meshPath = meshPath;
        return events::EventDispatcher::instance().query(query);
    }

    bool isLiveEntity(const services::EntityHandle& entity)
    {
        return entity.isValid() && findEntity(entity).has_value();
    }

    std::optional<events::socket::SocketAttachmentData> socketAttachment(const services::EntityHandle& entity)
    {
        events::socket::GetSocketAttachmentDataQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    std::optional<services::TransformData> localTransform(const services::EntityHandle& entity)
    {
        events::scene::GetTransformQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    glm::vec3 eulerDegrees(const glm::quat& rotation)
    {
        // The engine's own decomposition (GetWorldTransformQuery uses it), so the angles read back the
        // way entity rotations do.
        return math::decomposeMatrix(glm::mat4_cast(rotation)).rotation;
    }

    glm::quat rotationFromEulerDegrees(const glm::vec3& degrees)
    {
        return glm::angleAxis(glm::radians(degrees.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
               glm::angleAxis(glm::radians(degrees.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
               glm::angleAxis(glm::radians(degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    nlohmann::json degreesJson(float degrees)
    {
        if (!std::isfinite(degrees))
        {
            return nullptr;
        }
        const double rounded = std::round(static_cast<double>(degrees) * 1.0e4) / 1.0e4;
        return rounded == 0.0 ? 0.0 : rounded; // never report -0
    }

    nlohmann::json degreesJson(const glm::vec3& degrees)
    {
        return nlohmann::json::array({degreesJson(degrees.x), degreesJson(degrees.y), degreesJson(degrees.z)});
    }
}
