#include "EditorCameraTools.hpp"
#include "../camera/EditorCamera.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"

#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "math/Frustum.hpp"
#include "math/TransformUtils.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

namespace editor
{
    namespace
    {
        constexpr float minFov = 1.0f;
        constexpr float maxFov = 170.0f;
        constexpr float maxPitch = 89.0f;
        constexpr float focusMargin = 1.2f;   // keeps the bounding sphere off the frame edge
        constexpr float defaultRadius = 1.0f; // entities without mesh bounds

        float wrapYaw(float yaw)
        {
            float wrapped = std::fmod(yaw, 360.0f);
            if (wrapped < 0.0f)
            {
                wrapped += 360.0f;
            }
            return wrapped >= 360.0f ? 0.0f : wrapped;
        }

        // The editor camera's world transform is T(position) * Ry(yaw) * Rx(pitch) * Rz(roll)
        // (EditorCamera::updateViewMatrix) and it looks down its local -Z, so
        // forward = Ry * Rx * (0,0,-1) = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)).
        // Inverting for a unit direction d: pitch = asin(d.y), yaw = atan2(-d.x, -d.z).
        glm::vec2 pitchYawFromDirection(const glm::vec3& direction)
        {
            const glm::vec3 d = glm::normalize(direction);
            const float pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
            const float yaw = glm::degrees(std::atan2(-d.x, -d.z));
            return {pitch, yaw};
        }

        void requireFinite(const glm::vec3& value, const char* name)
        {
            if (!math::isFinite(value))
            {
                throw mcp::ArgError(std::string("argument '") + name + "' must contain finite numbers");
            }
        }

        nlohmann::json cameraState(const EditorCamera& camera)
        {
            return {
                {"position", mcp::vec3ToJson(camera.position)},
                {"rotation", mcp::vec3ToJson(camera.rotation)},
                {"fov", camera.fieldOfView},
                {"near", camera.nearPlane},
                {"far", camera.farPlane}
            };
        }

        services::EntityHandle toHandle(uint32_t id)
        {
            services::EntityHandle handle;
            handle.id = static_cast<uint64_t>(id);
            return handle;
        }

        void registerCameraGet(mcp::ToolRegistry& registry, EditorCamera* camera)
        {
            mcp::ToolDef tool;
            tool.name = "camera_get";
            tool.title = "Get editor camera";
            tool.description =
                "Read the editor viewport camera: {position [x,y,z], rotation [pitch, yaw, roll] in degrees, "
                "fov (vertical, degrees), near, far}. Yaw 0 looks down -Z; positive pitch looks up.";
            tool.readOnly = true;
            tool.handler = [camera](const nlohmann::json&) -> mcp::ToolResult
            {
                return mcp::ToolResult::ok(cameraState(*camera));
            };
            registry.add(std::move(tool));
        }

        void registerCameraSet(mcp::ToolRegistry& registry, EditorCamera* camera)
        {
            mcp::ToolDef tool;
            tool.name = "camera_set";
            tool.title = "Set editor camera";
            tool.description =
                "Move / aim the editor viewport camera. All arguments are optional but at least one is required. "
                "'lookAt' aims the camera at a world point (from the new position when 'position' is also given) "
                "and overrides 'rotation' (roll becomes 0). Pitch is clamped to +/-89 degrees and yaw wrapped to "
                "[0, 360). Camera moves are view state and are NOT undoable. Returns the new camera state.";
            tool.inputSchema = mcp::schema::object({
                {"position", mcp::schema::vec3("World position [x, y, z]")},
                {"rotation", mcp::schema::vec3("[pitch, yaw, roll] in degrees. Yaw 0 looks down -Z; positive pitch looks up.")},
                {"lookAt", mcp::schema::vec3("World point [x, y, z] to aim at (overrides rotation)")},
                {"fov", {
                    {"type", "number"}, {"minimum", minFov}, {"maximum", maxFov},
                    {"description", "Vertical field of view in degrees (1-170)"}
                }}
            });
            tool.handler = [camera](const nlohmann::json& args) -> mcp::ToolResult
            {
                mcp::ArgReader reader(args);
                const std::optional<glm::vec3> position = reader.optVec3("position");
                const std::optional<glm::vec3> rotation = reader.optVec3("rotation");
                const std::optional<glm::vec3> lookAt = reader.optVec3("lookAt");
                std::optional<float> fov;
                if (reader.has("fov"))
                {
                    const double value = reader.requireNumber("fov");
                    if (!(value >= minFov && value <= maxFov))
                    {
                        throw mcp::ArgError("argument 'fov' must be between 1 and 170 degrees");
                    }
                    fov = static_cast<float>(value);
                }
                if (!position && !rotation && !lookAt && !fov)
                {
                    throw mcp::ArgError("provide at least one of 'position', 'rotation', 'lookAt', 'fov'");
                }
                if (position) requireFinite(*position, "position");
                if (rotation) requireFinite(*rotation, "rotation");
                if (lookAt) requireFinite(*lookAt, "lookAt");

                // Validate everything before touching the camera.
                const glm::vec3 newPosition = position.value_or(camera->position);
                glm::vec3 newRotation = rotation.value_or(camera->rotation);
                if (lookAt)
                {
                    const glm::vec3 direction = *lookAt - newPosition;
                    if (glm::length(direction) < 1e-4f)
                    {
                        throw mcp::ArgError("argument 'lookAt' coincides with the camera position");
                    }
                    const glm::vec2 pitchYaw = pitchYawFromDirection(direction);
                    newRotation = glm::vec3(pitchYaw.x, pitchYaw.y, 0.0f);
                }
                newRotation.x = std::clamp(newRotation.x, -maxPitch, maxPitch);
                newRotation.y = wrapYaw(newRotation.y);

                camera->position = newPosition;
                camera->rotation = newRotation;
                camera->updateViewMatrix();
                if (fov && *fov != camera->fieldOfView)
                {
                    camera->fieldOfView = *fov;
                    camera->updateProjectionMatrix();
                }
                return mcp::ToolResult::ok(cameraState(*camera));
            };
            registry.add(std::move(tool));
        }

        void registerCameraFocus(mcp::ToolRegistry& registry, EditorCamera* camera)
        {
            mcp::ToolDef tool;
            tool.name = "camera_focus";
            tool.title = "Focus camera on entity";
            tool.description =
                "Frame an entity in the editor viewport and select it. Keeps the current pitch/yaw and moves the "
                "camera back along its view direction so the entity's world bounds fit (mesh bounds; entities "
                "without a mesh use a 1-unit radius around their world position). 'distance' overrides the "
                "computed distance from the bounds centre. NOT undoable. Returns the new camera state plus "
                "{center, radius, distance}.";
            tool.inputSchema = mcp::schema::object({
                {"entity", mcp::schema::entity()},
                {"distance", mcp::schema::number("Distance from the bounds centre (> 0). Default: fit the bounds.")}
            }, {"entity"});
            tool.handler = [camera](const nlohmann::json& args) -> mcp::ToolResult
            {
                mcp::ArgReader reader(args);
                const uint32_t id = reader.requireEntity("entity");
                std::optional<float> distanceArg;
                if (reader.has("distance"))
                {
                    const double value = reader.requireNumber("distance");
                    if (!(value > 0.0) || !std::isfinite(value))
                    {
                        throw mcp::ArgError("argument 'distance' must be a positive number");
                    }
                    distanceArg = static_cast<float>(value);
                }

                auto& dispatcher = events::EventDispatcher::instance();
                const services::EntityHandle handle = toHandle(id);

                events::scene::GetEntityQuery entityQuery;
                entityQuery.entity = handle;
                const std::optional<services::EntityData> entity = dispatcher.query(entityQuery);
                if (!entity.has_value())
                {
                    throw std::runtime_error("Entity " + std::to_string(id) +
                                             " (argument 'entity') does not exist; use entity_find or scene_get_hierarchy");
                }

                const services::TransformData& world = entity->worldTransform;
                glm::vec3 center = world.position;
                float radius = defaultRadius;

                events::scene::GetMeshDataQuery meshQuery;
                meshQuery.entity = handle;
                const std::optional<services::MeshData> mesh = dispatcher.query(meshQuery);
                if (mesh.has_value() && mesh->meshRef.isValid())
                {
                    events::render::GetMeshBoundingBoxQuery boundsQuery;
                    boundsQuery.meshPath = mesh->meshRef.resolve();
                    if (!boundsQuery.meshPath.empty())
                    {
                        if (auto bounds = dispatcher.query(boundsQuery))
                        {
                            // worldTransform is decomposed from the world matrix with XYZ Euler
                            // angles; composeMatrix is its inverse.
                            const glm::mat4 worldMatrix = math::composeMatrix(world.position, world.rotation, world.scale);
                            const math::AABB worldBounds = math::AABB(bounds->min, bounds->max).getTransformed(worldMatrix);
                            const glm::vec3 extents = worldBounds.getExtents();
                            if (worldBounds.isValid() && math::isFinite(extents) && glm::length(extents) > 1e-4f)
                            {
                                center = worldBounds.getCenter();
                                radius = glm::length(extents);
                            }
                        }
                    }
                }

                const float halfFov = glm::radians(camera->fieldOfView) * 0.5f;
                const float distance = distanceArg.value_or(radius / std::sin(halfFov) * focusMargin);

                // Same convention as pitchYawFromDirection: forward = R * (0,0,-1).
                const glm::vec3 forward = math::forwardFromEulerDegrees(camera->rotation);
                camera->position = center - forward * distance;
                camera->updateViewMatrix();

                events::scene::SelectEntityCommand select;
                select.entity = handle;
                dispatcher.execute(select);

                nlohmann::json state = cameraState(*camera);
                state["center"] = mcp::vec3ToJson(center);
                state["radius"] = radius;
                state["distance"] = distance;
                return mcp::ToolResult::ok(std::move(state));
            };
            registry.add(std::move(tool));
        }
    }

    void registerEditorCameraTools(mcp::ToolRegistry& registry, EditorCamera* camera)
    {
        registerCameraGet(registry, camera);
        registerCameraSet(registry, camera);
        registerCameraFocus(registry, camera);
    }
}
