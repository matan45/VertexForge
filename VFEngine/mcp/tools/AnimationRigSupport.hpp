#pragma once

#include "data/AnimationAuthoringData.hpp"
#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "events/physics/SocketEvents.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// VK-1656 helpers shared by the IK tools (AnimationIkTools.cpp), the socket tools (AnimationSocketTools.cpp)
// and their undo entries (mcp/undo/IkStateUndo, mcp/undo/SocketUndo). Functions that dispatch run on the
// main thread (inside a Main-affinity handler or an undo command); the others say "Any thread".
namespace mcp::tools::rig
{
    // GetMeshDataQuery: the entity's Mesh component, nullopt without one.
    std::optional<services::MeshData> meshData(const services::EntityHandle& entity);

    // The absolute engine path of the mesh asset; "" when none is assigned or it cannot be resolved.
    std::string meshPath(const services::MeshData& data);

    // The mesh as the agent addresses assets: a project-relative path, "guid:<hex>" when the asset cannot
    // be resolved, null when none is assigned.
    nlohmann::json meshJson(const services::MeshData& data);

    // `enginePath` relative to the project asset root when it lies inside it, else the whole path.
    std::string agentPath(const std::string& enginePath);

    // GetMeshSkeletonQuery: skeleton, sockets and authored IK chains, read fresh from the .vfMesh.
    services::MeshSkeletonInfo readSkeleton(const std::string& meshPath);

    // True when GetEntityQuery finds it (an invalid or stale handle is not live).
    bool isLiveEntity(const services::EntityHandle& entity);

    // GetSocketAttachmentDataQuery: nullopt without a SocketAttachment component. The parent handle is
    // invalid while the link is unresolved.
    std::optional<events::socket::SocketAttachmentData> socketAttachment(const services::EntityHandle& entity);

    // GetTransformQuery: the local transform.
    std::optional<services::TransformData> localTransform(const services::EntityHandle& entity);

    // Euler angles in DEGREES in the convention of entity rotationEuler (TransformComponent::getMatrix:
    // R = Rx * Ry * Rz). Any thread.
    glm::vec3 eulerDegrees(const glm::quat& rotation);
    glm::quat rotationFromEulerDegrees(const glm::vec3& degrees);

    // An angle for a report, rounded to 1e-4 so 90 reads 90 rather than 89.99999 after a radians round
    // trip; a non-finite value is null. Any thread.
    nlohmann::json degreesJson(float degrees);
    nlohmann::json degreesJson(const glm::vec3& degrees);
}
