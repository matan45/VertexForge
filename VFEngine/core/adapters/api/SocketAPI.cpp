#include <services/ScriptInterpreter.hpp>

#include "SocketAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SocketEvents.hpp"
#include "components/Components.hpp"
#include <glm/gtc/quaternion.hpp>

namespace core::api
{
    void SocketAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_socket_attach",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] Socket.attach: missing arguments (expected childId, parentId, socketName)");
                    return value::Value(false);
                }

                int64_t childId = extractInt64(args[0], "Socket.attach");
                int64_t parentId = extractInt64(args[1], "Socket.attach");
                std::string socketName = extractString(args[2], "Socket.attach");

                if (childId < 0 || parentId < 0)
                    return value::Value(false);

                events::socket::AttachToSocketCommand cmd;
                cmd.childEntity = intToEntity(childId);
                cmd.parentEntity = intToEntity(parentId);
                cmd.socketName = socketName;
                return value::Value(dispatcher.execute(cmd));
            });

        interpreter->registerNativeFunction("_native_socket_detach",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Socket.detach: missing entityId argument");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Socket.detach");
                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::DetachFromSocketCommand cmd;
                cmd.childEntity = intToEntity(entityId);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_socket_setActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Socket.setActive: missing arguments");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Socket.setActive");
                bool active = extractBool(args[1], "Socket.setActive");

                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::SetSocketActiveCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.active = active;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_socket_isAttached",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                    return value::Value(false);

                int64_t entityId = extractInt64(args[0], "Socket.isAttached");
                if (entityId < 0)
                    return value::Value(false);

                events::socket::IsAttachedQuery query;
                query.entity = intToEntity(entityId);
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_socket_hasSocket",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(false);

                int64_t entityId = extractInt64(args[0], "Socket.hasSocket");
                std::string socketName = extractString(args[1], "Socket.hasSocket");

                if (entityId < 0)
                    return value::Value(false);

                events::socket::HasSocketQuery query;
                query.entity = intToEntity(entityId);
                query.socketName = socketName;
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_socket_getPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Socket.getPosition");
                std::string socketName = extractString(args[1], "Socket.getPosition");

                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::GetSocketWorldPositionQuery query;
                query.parentEntity = intToEntity(entityId);
                query.socketName = socketName;
                glm::vec3 pos = dispatcher.query(query);
                return makeVec3Array(pos);
            });

        interpreter->registerNativeFunction("_native_socket_getTransform",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Socket.getTransform");
                std::string socketName = extractString(args[1], "Socket.getTransform");

                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::GetSocketWorldTransformQuery query;
                query.parentEntity = intToEntity(entityId);
                query.socketName = socketName;
                glm::mat4 mat = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(16, value::ValueType::FLOAT);
                const float* data = &mat[0][0];
                for (int i = 0; i < 16; ++i)
                {
                    arr->set(i, value::Value(data[i]));
                }
                return value::Value(arr);
            });

        interpreter->registerNativeFunction("_native_socket_getSockets",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Socket.getSockets");
                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::GetSocketNamesQuery query;
                query.entity = intToEntity(entityId);
                auto names = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(
                    static_cast<int>(names.size()), value::ValueType::STRING);
                for (size_t i = 0; i < names.size(); ++i)
                {
                    arr->set(static_cast<int>(i), value::Value(names[i]));
                }
                return value::Value(arr);
            });

        interpreter->registerNativeFunction("_native_socket_getRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Socket.getRotation");
                std::string socketName = extractString(args[1], "Socket.getRotation");

                if (entityId < 0)
                    return value::Value(std::monostate{});

                events::socket::GetSocketWorldTransformQuery query;
                query.parentEntity = intToEntity(entityId);
                query.socketName = socketName;
                glm::mat4 mat = dispatcher.query(query);

                glm::quat rot = glm::quat_cast(glm::mat3(mat));
                auto arr = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                arr->set(0, value::Value(rot.w));
                arr->set(1, value::Value(rot.x));
                arr->set(2, value::Value(rot.y));
                arr->set(3, value::Value(rot.z));
                return value::Value(arr);
            });

        interpreter->registerNativeFunction("_native_socket_getParentEntity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                    return value::Value(static_cast<int64_t>(-1));

                int64_t entityId = extractInt64(args[0], "Socket.getParentEntity");
                if (entityId < 0)
                    return value::Value(static_cast<int64_t>(-1));

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
                if (!registry.valid(entity))
                    return value::Value(static_cast<int64_t>(-1));

                auto* attachment = registry.try_get<components::SocketAttachmentComponent>(entity);
                if (!attachment || attachment->parentEntity == entt::null)
                    return value::Value(static_cast<int64_t>(-1));

                return value::Value(static_cast<int64_t>(static_cast<uint32_t>(attachment->parentEntity)));
            });
    }
}
