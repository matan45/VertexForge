// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return makeVec3Array(result->position);
            });

        interpreter->registerNativeFunction("_native_camera_setPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_camera_getRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetTransformQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return makeVec3Array(result->rotation);
            });

        interpreter->registerNativeFunction("_native_camera_setRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_camera_getFOV",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->fieldOfView);
            });

        interpreter->registerNativeFunction("_native_camera_setFOV",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_camera_getNearPlane",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->nearPlane);
            });

        interpreter->registerNativeFunction("_native_camera_setNearPlane",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_camera_getFarPlane",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                events::scene::GetCameraDataQuery q;
                q.entity = handle;
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(std::monostate{});
                return value::Value(result->farPlane);
            });

        interpreter->registerNativeFunction("_native_camera_setFarPlane",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_camera_getViewMatrix",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                const auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                return makeMat4Array(cam.viewMatrix);
            });

        interpreter->registerNativeFunction("_native_camera_getProjectionMatrix",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
                if (!entityOpt) return value::Value(std::monostate{});
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::CameraComponent>(*entityOpt))
                    return value::Value(std::monostate{});
                const auto& cam = registry.get<components::CameraComponent>(*entityOpt);
                return makeMat4Array(cam.projectionMatrix);
            });

        interpreter->registerNativeFunction("_native_camera_getPrimary",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto result = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
                if (!result.has_value()) return value::Value(static_cast<int64_t>(-1));
                return value::Value(entityToInt(*result));
            });

        interpreter->registerNativeFunction("_native_camera_setIsPrimary",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });
    }
}
