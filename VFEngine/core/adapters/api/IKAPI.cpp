#include <services/ScriptInterpreter.hpp>

#include "IKAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/IKEvents.hpp"

namespace core::api
{
    void IKAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // ik_setTarget(entityId, chainName, x, y, z)
        interpreter->registerNativeFunction("_native_ik_setTarget",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 5)
                {
                    vfLogError("[Script] IK.setTarget: missing arguments (expected entityId, chainName, x, y, z)");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "IK.setTarget");
                if (entityId < 0) return value::Value(std::monostate{});

                std::string chainName = extractString(args[1], "IK.setTarget");
                float x = extractFloat(args[2], "IK.setTarget");
                float y = extractFloat(args[3], "IK.setTarget");
                float z = extractFloat(args[4], "IK.setTarget");

                events::ik::SetIKTargetCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.chainName = chainName;
                cmd.targetPosition = glm::vec3(x, y, z);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ik_setTargetWithRotation(entityId, chainName, x, y, z, qw, qx, qy, qz)
        interpreter->registerNativeFunction("_native_ik_setTargetWithRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 9)
                {
                    vfLogError("[Script] IK.setTargetWithRotation: missing arguments");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "IK.setTargetWithRotation");
                if (entityId < 0) return value::Value(std::monostate{});

                std::string chainName = extractString(args[1], "IK.setTargetWithRotation");
                float x = extractFloat(args[2], "IK.setTargetWithRotation");
                float y = extractFloat(args[3], "IK.setTargetWithRotation");
                float z = extractFloat(args[4], "IK.setTargetWithRotation");
                float qw = extractFloat(args[5], "IK.setTargetWithRotation");
                float qx = extractFloat(args[6], "IK.setTargetWithRotation");
                float qy = extractFloat(args[7], "IK.setTargetWithRotation");
                float qz = extractFloat(args[8], "IK.setTargetWithRotation");

                events::ik::SetIKTargetCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.chainName = chainName;
                cmd.targetPosition = glm::vec3(x, y, z);
                cmd.targetRotation = glm::quat(qw, qx, qy, qz);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ik_setChainWeight(entityId, chainName, weight)
        interpreter->registerNativeFunction("_native_ik_setChainWeight",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] IK.setChainWeight: missing arguments");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "IK.setChainWeight");
                if (entityId < 0) return value::Value(std::monostate{});

                std::string chainName = extractString(args[1], "IK.setChainWeight");
                float weight = extractFloat(args[2], "IK.setChainWeight");

                events::ik::SetIKChainWeightCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.chainName = chainName;
                cmd.weight = weight;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ik_setChainEnabled(entityId, chainName, enabled)
        interpreter->registerNativeFunction("_native_ik_setChainEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] IK.setChainEnabled: missing arguments");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "IK.setChainEnabled");
                if (entityId < 0) return value::Value(std::monostate{});

                std::string chainName = extractString(args[1], "IK.setChainEnabled");
                bool enabled = extractBool(args[2], "IK.setChainEnabled");

                events::ik::SetIKChainEnabledCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.chainName = chainName;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ik_getChainWeight(entityId, chainName) -> float
        interpreter->registerNativeFunction("_native_ik_getChainWeight",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(0.0f);

                int64_t entityId = extractInt64(args[0], "IK.getChainWeight");
                if (entityId < 0) return value::Value(0.0f);

                std::string chainName = extractString(args[1], "IK.getChainWeight");

                events::ik::GetIKChainWeightQuery query;
                query.entity = intToEntity(entityId);
                query.chainName = chainName;
                return value::Value(dispatcher.query(query));
            });

        // ik_isChainEnabled(entityId, chainName) -> bool
        interpreter->registerNativeFunction("_native_ik_isChainEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                    return value::Value(false);

                int64_t entityId = extractInt64(args[0], "IK.isChainEnabled");
                if (entityId < 0) return value::Value(false);

                std::string chainName = extractString(args[1], "IK.isChainEnabled");

                events::ik::IsIKChainEnabledQuery query;
                query.entity = intToEntity(entityId);
                query.chainName = chainName;
                return value::Value(dispatcher.query(query));
            });

        // ik_getChainNames(entityId) -> string array
        interpreter->registerNativeFunction("_native_ik_getChainNames",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "IK.getChainNames");
                if (entityId < 0) return value::Value(std::monostate{});

                events::ik::GetIKChainNamesQuery query;
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

        // ik_hasComponent(entityId) -> bool
        interpreter->registerNativeFunction("_native_ik_hasComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                    return value::Value(false);

                int64_t entityId = extractInt64(args[0], "IK.hasComponent");
                if (entityId < 0) return value::Value(false);

                events::ik::HasIKComponentQuery query;
                query.entity = intToEntity(entityId);
                return value::Value(dispatcher.query(query));
            });
    }
}
