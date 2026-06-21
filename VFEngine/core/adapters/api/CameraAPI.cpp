// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "CameraAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "components/CoreComponents.hpp"

namespace core::api
{
    void CameraAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_camera_getPosition",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return makeVec3Array(result->position);
            }});

        interpreter->registerNativeFunction("_native_camera_setPosition",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->position.x = extractFloat(args[1]);
                current->position.y = extractFloat(args[2]);
                current->position.z = extractFloat(args[3]);

                events::scene::SetTransformCommand cmd;
                cmd.entity = handle;
                cmd.transform = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_camera_getRotation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return makeVec3Array(result->rotation);
            }});

        interpreter->registerNativeFunction("_native_camera_setRotation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->rotation.x = extractFloat(args[1]);
                current->rotation.y = extractFloat(args[2]);
                current->rotation.z = extractFloat(args[3]);

                events::scene::SetTransformCommand cmd;
                cmd.entity = handle;
                cmd.transform = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_camera_getFOV",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->fieldOfView);
            }});

        interpreter->registerNativeFunction("_native_camera_setFOV",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->fieldOfView = extractFloat(args[1]);

                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_camera_getNearPlane",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->nearPlane);
            }});

        interpreter->registerNativeFunction("_native_camera_setNearPlane",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->nearPlane = extractFloat(args[1]);

                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_camera_getFarPlane",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->farPlane);
            }});

        interpreter->registerNativeFunction("_native_camera_setFarPlane",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->farPlane = extractFloat(args[1]);

                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_camera_getViewMatrix",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                const auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                return makeMat4Array(cam.viewMatrix);
            }});

        interpreter->registerNativeFunction("_native_camera_getProjectionMatrix",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                const auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                return makeMat4Array(cam.projectionMatrix);
            }});

        interpreter->registerNativeFunction("_native_camera_getPrimary",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto result = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
                if (!result.has_value()) return value::Value(static_cast<int64_t>(-1));
                return value::Value(entityToInt(*result));
            }});

        interpreter->registerNativeFunction("_native_camera_setIsPrimary",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->isPrimary = extractBool(args[1]);

                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_camera_getCullingMask(entityId) -> int  (VK-1415)
        // The 32-bit per-camera render-layer mask. A mesh on layer N is visible to
        // this camera only if bit N is set. Returns nil if the entity has no camera.
        interpreter->registerNativeFunction("_native_camera_getCullingMask",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(static_cast<int64_t>(result->cullingMask));
            }});

        // _native_camera_setCullingMask(entityId, mask)  (VK-1415)
        // Sets the per-camera render-layer mask (0xFFFFFFFF = all layers). Takes effect
        // next frame for this camera's view (incl. an RTT driven by this camera).
        interpreter->registerNativeFunction("_native_camera_setCullingMask",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));

                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});

                current->cullingMask = static_cast<uint32_t>(extractInt64(args[1]));

                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // ===== VK-1416: full camera control =====

        // _native_camera_lookAt(id, tx,ty,tz, upx,upy,upz) — aim from the current world eye; pin view.
        interpreter->registerNativeFunction("_native_camera_lookAt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 7) return value::Value(std::monostate{});
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});

                glm::vec3 target(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                glm::vec3 up(extractFloat(args[4]), extractFloat(args[5]), extractFloat(args[6]));

                glm::vec3 eye(0.0f);
                if (registry.all_of<components::WorldTransformComponent>(*entityOpt))
                    eye = glm::vec3(registry.get<components::WorldTransformComponent>(*entityOpt).worldMatrix[3]);
                else if (registry.all_of<components::TransformComponent>(*entityOpt))
                    eye = registry.get<components::TransformComponent>(*entityOpt).position;

                auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                cam.viewMatrix = glm::lookAt(eye, target, up);
                cam.viewMatrixOverride = true;
                return value::Value(std::monostate{});
            }});

        // _native_camera_setViewMatrix(id, mat16) — row-major 16-float array (Matrix4f layout).
        interpreter->registerNativeFunction("_native_camera_setViewMatrix",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                glm::mat4 m(1.0f);
                if (!extractMat4(args[1], m)) return value::Value(std::monostate{});
                auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                cam.viewMatrix = m;
                cam.viewMatrixOverride = true;
                return value::Value(std::monostate{});
            }});

        // _native_camera_setProjectionMatrix(id, mat16)
        interpreter->registerNativeFunction("_native_camera_setProjectionMatrix",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                glm::mat4 m(1.0f);
                if (!extractMat4(args[1], m)) return value::Value(std::monostate{});
                auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                cam.projectionMatrix = m;
                cam.viewMatrixOverride = true;
                return value::Value(std::monostate{});
            }});

        // _native_camera_setViewMatrixOverride(id, bool) — false returns to engine-driven view.
        interpreter->registerNativeFunction("_native_camera_setViewMatrixOverride",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                registry.get<components::CameraComponent>(*entityOpt).viewMatrixOverride = extractBool(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_camera_getViewMatrixOverride(id) -> bool
        interpreter->registerNativeFunction("_native_camera_getViewMatrixOverride",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                return value::Value(registry.get<components::CameraComponent>(*entityOpt).viewMatrixOverride);
            }});

        // _native_camera_setOrthographic(id, bool) — true = orthographic. Routes via CameraData DTO.
        interpreter->registerNativeFunction("_native_camera_setOrthographic",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});
                current->isPerspective = !extractBool(args[1]);
                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_camera_setOrthoSize(id, size)
        interpreter->registerNativeFunction("_native_camera_setOrthoSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(std::monostate{});
                current->orthoSize = extractFloat(args[1]);
                events::scene::SetCameraDataCommand cmd;
                cmd.entity = handle;
                cmd.cameraData = *current;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_camera_isOrthographic(id) -> bool
        interpreter->registerNativeFunction("_native_camera_isOrthographic",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(!result->isPerspective);
            }});

        // _native_camera_getOrthoSize(id) -> float
        interpreter->registerNativeFunction("_native_camera_getOrthoSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->orthoSize);
            }});
    }
}
