#include "PhysicsSkeletonConverter.hpp"
#include "JoltConversions.hpp"
#include <algorithm>

namespace core::physics
{
    SkeletonConversionResult PhysicsSkeletonConverter::convertToPhysicsSkeleton(
        const resource::SkeletonData& skeletonData,
        const types::PhysicsAnimationConfig& config)
    {
        SkeletonConversionResult result;

        if (config.boneBodyMappings.empty() || !skeletonData.hasBones())
        {
            return result;
        }

        std::unordered_map<int, int> animBoneToMappingOrder;
        for (size_t i = 0; i < config.boneBodyMappings.size(); ++i)
        {
            int32_t boneIdx = skeletonData.getBoneIndex(config.boneBodyMappings[i].boneName);
            if (boneIdx >= 0)
            {
                animBoneToMappingOrder[boneIdx] = static_cast<int>(i);
            }
        }

        if (animBoneToMappingOrder.empty())
        {
            return result;
        }

        // Collect mapped bone indices sorted by animation order (parents before children)
        std::vector<int> sortedAnimIndices;
        sortedAnimIndices.reserve(animBoneToMappingOrder.size());
        for (const auto& [animIdx, _] : animBoneToMappingOrder)
        {
            sortedAnimIndices.push_back(animIdx);
        }
        std::sort(sortedAnimIndices.begin(), sortedAnimIndices.end());

        result.physicsSkeleton = new JPH::Skeleton();
        result.physicsToAnimBoneIndex.reserve(sortedAnimIndices.size());

        for (int animIdx : sortedAnimIndices)
        {
            const auto& bone = skeletonData.bones[animIdx];

            int physicsParentIndex = -1;
            int parentAnimIdx = bone.parentIndex;
            while (parentAnimIdx >= 0)
            {
                auto parentIt = result.animToPhysicsBoneIndex.find(parentAnimIdx);
                if (parentIt != result.animToPhysicsBoneIndex.end())
                {
                    physicsParentIndex = parentIt->second;
                    break;
                }
                parentAnimIdx = skeletonData.bones[parentAnimIdx].parentIndex;
            }

            int physicsIdx = static_cast<int>(result.physicsToAnimBoneIndex.size());
            result.physicsSkeleton->AddJoint(JPH::String(bone.name.c_str()), physicsParentIndex);
            result.physicsToAnimBoneIndex.push_back(animIdx);
            result.animToPhysicsBoneIndex[animIdx] = physicsIdx;
        }

        return result;
    }

    JPH::Ref<JPH::Skeleton> PhysicsSkeletonConverter::convertFullSkeleton(
        const resource::SkeletonData& skeletonData)
    {
        JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton();

        for (size_t i = 0; i < skeletonData.bones.size(); ++i)
        {
            const auto& bone = skeletonData.bones[i];
            skeleton->AddJoint(JPH::String(bone.name.c_str()), bone.parentIndex);
        }

        return skeleton;
    }

    JPH::SkeletonPose PhysicsSkeletonConverter::buildPhysicsSkeletonPose(
        const JPH::Skeleton* physicsSkeleton,
        const std::vector<glm::mat4>& animBoneWorldTransforms,
        const std::vector<int>& physicsToAnimBoneIndex,
        const glm::vec3& entityPosition)
    {
        JPH::SkeletonPose pose;
        pose.SetSkeleton(physicsSkeleton);
        pose.SetRootOffset(toJoltR(entityPosition));

        auto& jointMatrices = pose.GetJointMatrices();
        jointMatrices.resize(physicsSkeleton->GetJointCount());

        for (int i = 0; i < physicsSkeleton->GetJointCount(); ++i)
        {
            int animIdx = physicsToAnimBoneIndex[i];
            if (animIdx >= 0 && animIdx < static_cast<int>(animBoneWorldTransforms.size()))
            {
                jointMatrices[i] = toJoltMat44(animBoneWorldTransforms[animIdx]);
            }
            else
            {
                jointMatrices[i] = JPH::Mat44::sIdentity();
            }
        }

        pose.CalculateJointStates();

        return pose;
    }

    JPH::SkeletonPose PhysicsSkeletonConverter::buildTargetPoseFromAnimatorMatrices(
        const JPH::Skeleton* physicsSkeleton,
        const std::vector<glm::mat4>& animatorSkinningMatrices,
        const std::vector<int>& physicsToAnimBoneIndex,
        const resource::SkeletonData& skeletonData,
        const glm::vec3& entityPosition,
        const glm::quat& entityRotation)
    {
        JPH::SkeletonPose pose;
        pose.SetSkeleton(physicsSkeleton);
        pose.SetRootOffset(toJoltR(entityPosition));

        // Entity rotation cancels out of parent-relative joint states; it only
        // orients the root joint so the root drive can track entity yaw
        glm::mat4 globalTransform =
            glm::mat4_cast(entityRotation) * glm::inverse(skeletonData.globalInverseTransform);

        auto& jointMatrices = pose.GetJointMatrices();
        jointMatrices.resize(physicsSkeleton->GetJointCount());

        for (int i = 0; i < physicsSkeleton->GetJointCount(); ++i)
        {
            int animIdx = physicsToAnimBoneIndex[i];
            if (animIdx >= 0 && animIdx < static_cast<int>(animatorSkinningMatrices.size()) &&
                animIdx < static_cast<int>(skeletonData.bindPoses.size()))
            {
                glm::mat4 boneModel =
                    globalTransform * animatorSkinningMatrices[animIdx] * skeletonData.bindPoses[animIdx];
                jointMatrices[i] = toJoltMat44(boneModel);
            }
            else
            {
                jointMatrices[i] = JPH::Mat44::sIdentity();
            }
        }

        pose.CalculateJointStates();

        return pose;
    }

    std::vector<glm::mat4> PhysicsSkeletonConverter::ragdollPoseToSkinningMatrices(
        const JPH::SkeletonPose& ragdollPose,
        const SkeletonConversionResult& conversion,
        const resource::SkeletonData& skeletonData,
        const std::vector<glm::mat4>& fallbackAnimWorldTransforms)
    {
        size_t boneCount = skeletonData.bones.size();
        std::vector<glm::mat4> skinningMatrices(boneCount, glm::mat4(1.0f));

        const auto& ragdollJointMatrices = ragdollPose.GetJointMatrices();

        for (size_t i = 0; i < boneCount; ++i)
        {
            glm::mat4 worldTransform;

            auto it = conversion.animToPhysicsBoneIndex.find(static_cast<int>(i));
            if (it != conversion.animToPhysicsBoneIndex.end())
            {
                int physicsIdx = it->second;
                if (physicsIdx >= 0 && physicsIdx < static_cast<int>(ragdollJointMatrices.size()))
                {
                    worldTransform = toGlmMat4(ragdollJointMatrices[physicsIdx]);
                }
                else
                {
                    worldTransform = fallbackAnimWorldTransforms[i];
                }
            }
            else
            {
                worldTransform = fallbackAnimWorldTransforms[i];
            }

            // Same formula as AnimationEvaluator::evaluatePose
            skinningMatrices[i] = skeletonData.globalInverseTransform * worldTransform * skeletonData.inverseBindPoses[i];
        }

        return skinningMatrices;
    }

    glm::mat4 PhysicsSkeletonConverter::toGlmMat4(const JPH::Mat44& m)
    {
        glm::mat4 result;
        auto c0 = m.GetColumn4(0);
        auto c1 = m.GetColumn4(1);
        auto c2 = m.GetColumn4(2);
        auto c3 = m.GetColumn4(3);

        result[0] = glm::vec4(c0.GetX(), c0.GetY(), c0.GetZ(), c0.GetW());
        result[1] = glm::vec4(c1.GetX(), c1.GetY(), c1.GetZ(), c1.GetW());
        result[2] = glm::vec4(c2.GetX(), c2.GetY(), c2.GetZ(), c2.GetW());
        result[3] = glm::vec4(c3.GetX(), c3.GetY(), c3.GetZ(), c3.GetW());

        return result;
    }

    JPH::Mat44 PhysicsSkeletonConverter::toJoltMat44(const glm::mat4& m)
    {
        return JPH::Mat44(
            JPH::Vec4(m[0].x, m[0].y, m[0].z, m[0].w),
            JPH::Vec4(m[1].x, m[1].y, m[1].z, m[1].w),
            JPH::Vec4(m[2].x, m[2].y, m[2].z, m[2].w),
            JPH::Vec4(m[3].x, m[3].y, m[3].z, m[3].w)
        );
    }
}
