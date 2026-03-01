#include <services/ScriptInterpreter.hpp>

#include "FootIKAPI.hpp"
#include "NativeHelpers.hpp"
#include "animator/FootIKHelper.hpp"

namespace core::api
{
    void FootIKAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_footik_calculateFootTarget",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 16)
                {
                    vfLogError("[Script] FootIK.calculateFootTarget: expected 16 arguments");
                    return value::Value(std::monostate{});
                }

                const char* ctx = "FootIK.calculateFootTarget";

                glm::vec3 footPos(
                    extractFloat(args[0], ctx),
                    extractFloat(args[1], ctx),
                    extractFloat(args[2], ctx));

                glm::vec3 characterUp(
                    extractFloat(args[3], ctx),
                    extractFloat(args[4], ctx),
                    extractFloat(args[5], ctx));

                animator::ik::FootRaycastResult raycast;
                raycast.hit = extractBool(args[6], ctx);
                raycast.hitPoint = glm::vec3(
                    extractFloat(args[7], ctx),
                    extractFloat(args[8], ctx),
                    extractFloat(args[9], ctx));
                raycast.hitNormal = glm::vec3(
                    extractFloat(args[10], ctx),
                    extractFloat(args[11], ctx),
                    extractFloat(args[12], ctx));
                raycast.hitDistance = extractFloat(args[13], ctx);

                animator::ik::FootIKConfig config;
                config.footHeight = extractFloat(args[14], ctx);
                config.maxStepHeight = extractFloat(args[15], ctx);

                auto result = animator::ik::FootIKHelper::calculateFootTarget(
                    footPos, characterUp, raycast, config);

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
                arr->set(8, value::Value(result.isGrounded ? 1.0f : 0.0f));

                return value::Value(arr);
            });

        interpreter->registerNativeFunction("_native_footik_calculatePelvisOffset",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 15)
                {
                    vfLogError("[Script] FootIK.calculatePelvisOffset: expected 15 arguments");
                    return value::Value(0.0f);
                }

                const char* ctx = "FootIK.calculatePelvisOffset";

                animator::ik::FootIKResult leftFoot;
                leftFoot.targetPosition = glm::vec3(
                    extractFloat(args[0], ctx),
                    extractFloat(args[1], ctx),
                    extractFloat(args[2], ctx));
                leftFoot.weight = extractFloat(args[3], ctx);
                leftFoot.isGrounded = extractBool(args[4], ctx);
                float leftOriginalY = extractFloat(args[5], ctx);

                animator::ik::FootIKResult rightFoot;
                rightFoot.targetPosition = glm::vec3(
                    extractFloat(args[6], ctx),
                    extractFloat(args[7], ctx),
                    extractFloat(args[8], ctx));
                rightFoot.weight = extractFloat(args[9], ctx);
                rightFoot.isGrounded = extractBool(args[10], ctx);
                float rightOriginalY = extractFloat(args[11], ctx);

                float currentOffset = extractFloat(args[12], ctx);
                float deltaTime = extractFloat(args[13], ctx);
                float adjustSpeed = extractFloat(args[14], ctx);

                float result = animator::ik::FootIKHelper::calculatePelvisOffset(
                    leftFoot, leftOriginalY, rightFoot, rightOriginalY,
                    currentOffset, deltaTime, adjustSpeed);

                return value::Value(result);
            });
    }
}
