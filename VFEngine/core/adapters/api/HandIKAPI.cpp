#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "HandIKAPI.hpp"
#include "NativeHelpers.hpp"
#include "animator/HandIKHelper.hpp"

namespace core::api
{
    void HandIKAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_handik_calculateHandTarget",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 16)
                {
                    vfLogError("[Script] HandIK.calculateHandTarget: expected 16 arguments");
                    return value::Value(std::monostate{});
                }

                const char* ctx = "HandIK.calculateHandTarget";

                glm::vec3 handPos(
                    extractFloat(args[0], ctx),
                    extractFloat(args[1], ctx),
                    extractFloat(args[2], ctx));

                glm::vec3 shoulderPos(
                    extractFloat(args[3], ctx),
                    extractFloat(args[4], ctx),
                    extractFloat(args[5], ctx));

                glm::vec3 targetPos(
                    extractFloat(args[6], ctx),
                    extractFloat(args[7], ctx),
                    extractFloat(args[8], ctx));

                bool hasRotation = extractBool(args[9], ctx);
                std::optional<glm::quat> targetRotation;
                if (hasRotation)
                {
                    targetRotation = glm::quat(
                        extractFloat(args[13], ctx),  // w
                        extractFloat(args[10], ctx),  // x
                        extractFloat(args[11], ctx),  // y
                        extractFloat(args[12], ctx)); // z
                }

                animator::ik::HandIKConfig config;
                config.maxReachDistance = extractFloat(args[14], ctx);
                config.gripRotationBlend = extractFloat(args[15], ctx);

                auto result = animator::ik::HandIKHelper::calculateHandTarget(
                    handPos, shoulderPos, targetPos, targetRotation, config);

                auto arr = std::make_shared<value::NativeArray>(9, value::ValueType::FLOAT);
                arr->set(0, value::Value(result.targetPosition.x));
                arr->set(1, value::Value(result.targetPosition.y));
                arr->set(2, value::Value(result.targetPosition.z));

                if (result.targetRotation.has_value())
                {
                    const auto& q = *result.targetRotation;
                    arr->set(3, value::Value(q.x));
                    arr->set(4, value::Value(q.y));
                    arr->set(5, value::Value(q.z));
                    arr->set(6, value::Value(q.w));
                }
                else
                {
                    arr->set(3, value::Value(0.0f));
                    arr->set(4, value::Value(0.0f));
                    arr->set(5, value::Value(0.0f));
                    arr->set(6, value::Value(1.0f));
                }

                arr->set(7, value::Value(result.weight));
                arr->set(8, value::Value(result.isReachable ? 1.0f : 0.0f));

                return value::Value(arr);
            }});

        interpreter->registerNativeFunction("_native_handik_calculateTwoHandedGrip",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 15)
                {
                    vfLogError("[Script] HandIK.calculateTwoHandedGrip: expected 15 arguments");
                    return value::Value(std::monostate{});
                }

                const char* ctx = "HandIK.calculateTwoHandedGrip";

                glm::vec3 domHandPos(
                    extractFloat(args[0], ctx),
                    extractFloat(args[1], ctx),
                    extractFloat(args[2], ctx));

                glm::quat domHandRot(
                    extractFloat(args[6], ctx),  // w
                    extractFloat(args[3], ctx),  // x
                    extractFloat(args[4], ctx),  // y
                    extractFloat(args[5], ctx)); // z

                glm::vec3 gripOffset(
                    extractFloat(args[7], ctx),
                    extractFloat(args[8], ctx),
                    extractFloat(args[9], ctx));

                glm::vec3 offShoulderPos(
                    extractFloat(args[10], ctx),
                    extractFloat(args[11], ctx),
                    extractFloat(args[12], ctx));

                animator::ik::HandIKConfig config;
                config.maxReachDistance = extractFloat(args[13], ctx);
                config.gripRotationBlend = extractFloat(args[14], ctx);

                auto result = animator::ik::HandIKHelper::calculateTwoHandedGrip(
                    domHandPos, domHandRot, gripOffset, offShoulderPos, config);

                auto arr = std::make_shared<value::NativeArray>(9, value::ValueType::FLOAT);
                arr->set(0, value::Value(result.targetPosition.x));
                arr->set(1, value::Value(result.targetPosition.y));
                arr->set(2, value::Value(result.targetPosition.z));

                if (result.targetRotation.has_value())
                {
                    const auto& q = *result.targetRotation;
                    arr->set(3, value::Value(q.x));
                    arr->set(4, value::Value(q.y));
                    arr->set(5, value::Value(q.z));
                    arr->set(6, value::Value(q.w));
                }
                else
                {
                    arr->set(3, value::Value(0.0f));
                    arr->set(4, value::Value(0.0f));
                    arr->set(5, value::Value(0.0f));
                    arr->set(6, value::Value(1.0f));
                }

                arr->set(7, value::Value(result.weight));
                arr->set(8, value::Value(result.isReachable ? 1.0f : 0.0f));

                return value::Value(arr);
            }});
    }
}
