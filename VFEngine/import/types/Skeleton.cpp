#include "Skeleton.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <fstream>
#include <filesystem>

#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/type_ptr.hpp>

namespace
{
    // Assimp to GLM conversion helpers
    glm::mat4 convertMatrix(const aiMatrix4x4& m)
    {
        // Assimp uses row-major, GLM uses column-major
        return glm::transpose(glm::make_mat4(&m.a1));
    }
}

namespace types
{
    std::string Skeleton::extractAndSave(const aiScene* scene, std::string_view fileName,
                                         std::string_view location,
                                         SkeletonProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        // Extract skeleton from scene
        resource::SkeletonData skeleton = extractFromScene(scene);

        if (progressCallback) progressCallback(0.7f);

        if (!skeleton.hasBones())
        {
            vfLogInfo("No skeleton found in scene: {}", fileName);
            return "";
        }

        // Set skeleton name (same as mesh file)
        skeleton.name = std::string(fileName);

        // Save to file
        saveToFile(location, fileName, skeleton);

        if (progressCallback) progressCallback(1.0f);

        return getSkeletonPath(location, fileName);
    }

    resource::SkeletonData Skeleton::extractFromScene(const aiScene* scene) const
    {
        resource::SkeletonData skeleton;
        skeleton.headerFileType = resource::FileType::SKELETON;
        skeleton.version = {Version::major, Version::minor, Version::patch};

        // Collect all bone names from meshes
        std::unordered_set<std::string> boneNames = collectBoneNames(scene);

        if (boneNames.empty())
        {
            vfLogInfo("No bones found in scene meshes");
            return skeleton;
        }

        vfLogInfo("Found {} bones in scene meshes", boneNames.size());

        // Extract inverse bind poses from mesh bones (aiBone.mOffsetMatrix)
        std::unordered_map<std::string, glm::mat4> inverseBindPoseMap = extractInverseBindPoses(scene);

        // Build bone hierarchy by traversing node tree
        std::unordered_map<std::string, int32_t> boneIndexMap;
        buildBoneHierarchy(scene->mRootNode, boneNames, skeleton.bones, boneIndexMap, -1, glm::mat4(1.0f));

        // Compute world-space bind poses
        computeBindPoses(skeleton.bones, skeleton.bindPoses);

        // Build inverse bind poses array in bone order
        skeleton.inverseBindPoses.resize(skeleton.bones.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const std::string& boneName = skeleton.bones[i].name;
            auto it = inverseBindPoseMap.find(boneName);
            if (it != inverseBindPoseMap.end())
            {
                skeleton.inverseBindPoses[i] = it->second;
            }
            else
            {
                // Fallback: compute inverse bind pose from bind pose
                skeleton.inverseBindPoses[i] = glm::inverse(skeleton.bindPoses[i]);
                vfLogWarning("Bone '{}' not found in mesh aiBones, computed inverse bind pose from hierarchy", boneName);
            }
        }

        // Compute global inverse transform from scene root
        skeleton.globalInverseTransform = glm::inverse(convertMatrix(scene->mRootNode->mTransformation));

        vfLogInfo("Extracted unified skeleton with {} bones", skeleton.bones.size());

        return skeleton;
    }

    void Skeleton::saveToFile(std::string_view location, std::string_view baseName,
                              const resource::SkeletonData& skeleton) const
    {
        std::filesystem::path outputPath = std::filesystem::path(location) /
                                           (std::string(baseName) + "." + FileExtension::skeleton);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open skeleton file for writing: {}", outputPath.string());
            return;
        }

        // Write magic header "VFSK"
        outFile.write("VFSK", 4);

        // Write version
        resource::endian::writeLE<uint32_t>(outFile, skeleton.version.major);
        resource::endian::writeLE<uint32_t>(outFile, skeleton.version.minor);
        resource::endian::writeLE<uint32_t>(outFile, skeleton.version.patch);

        // Write skeleton name
        writeString(outFile, skeleton.name);

        // Write bone count
        uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
        resource::endian::writeLE<uint32_t>(outFile, boneCount);

        // Write each bone
        for (size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const auto& bone = skeleton.bones[i];

            // Write bone name
            writeString(outFile, bone.name);

            // Write parent index
            resource::endian::writeLE<int32_t>(outFile, bone.parentIndex);

            // Write offset matrix (local transform)
            writeMatrix(outFile, bone.offsetMatrix);

            // Write pre-transform
            writeMatrix(outFile, bone.preTransform);

            // Write bind pose (world space)
            writeMatrix(outFile, skeleton.bindPoses[i]);

            // Write inverse bind pose
            writeMatrix(outFile, skeleton.inverseBindPoses[i]);
        }

        // Write global inverse transform
        writeMatrix(outFile, skeleton.globalInverseTransform);

        outFile.close();

        vfLogInfo("Skeleton saved to: {} ({} bones)", outputPath.string(), boneCount);
    }

    bool Skeleton::skeletonExists(std::string_view location, std::string_view meshFileName)
    {
        std::filesystem::path skeletonPath = std::filesystem::path(location) /
                                             (std::string(meshFileName) + "." + FileExtension::skeleton);
        return std::filesystem::exists(skeletonPath);
    }

    std::string Skeleton::getSkeletonPath(std::string_view location, std::string_view meshFileName)
    {
        std::filesystem::path skeletonPath = std::filesystem::path(location) /
                                             (std::string(meshFileName) + "." + FileExtension::skeleton);
        return skeletonPath.string();
    }

    std::unordered_set<std::string> Skeleton::collectBoneNames(const aiScene* scene) const
    {
        std::unordered_set<std::string> boneNames;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                boneNames.insert(mesh->mBones[b]->mName.C_Str());
            }
        }

        return boneNames;
    }

    std::unordered_map<std::string, glm::mat4> Skeleton::extractInverseBindPoses(const aiScene* scene) const
    {
        std::unordered_map<std::string, glm::mat4> inverseBindPoses;

        for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            for (uint32_t b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                // Skip if already extracted (from another mesh)
                if (inverseBindPoses.contains(boneName))
                    continue;

                // Convert Assimp matrix to GLM (row-major to column-major)
                const auto& aiMat = bone->mOffsetMatrix;
                glm::mat4 offsetMatrix = glm::transpose(glm::mat4(
                    aiMat.a1, aiMat.a2, aiMat.a3, aiMat.a4,
                    aiMat.b1, aiMat.b2, aiMat.b3, aiMat.b4,
                    aiMat.c1, aiMat.c2, aiMat.c3, aiMat.c4,
                    aiMat.d1, aiMat.d2, aiMat.d3, aiMat.d4
                ));

                inverseBindPoses[boneName] = offsetMatrix;
            }
        }

        return inverseBindPoses;
    }

    void Skeleton::buildBoneHierarchy(const aiNode* node,
                                      const std::unordered_set<std::string>& boneNames,
                                      std::vector<resource::SkeletonBone>& bones,
                                      std::unordered_map<std::string, int32_t>& boneIndexMap,
                                      int32_t parentIndex,
                                      const glm::mat4& accumulatedTransform) const
    {
        std::string nodeName = node->mName.C_Str();
        int32_t currentIndex = parentIndex;
        glm::mat4 currentAccumulated = accumulatedTransform;

        // Get this node's local transform
        glm::mat4 nodeTransform = convertMatrix(node->mTransformation);

        // Check if this node is a bone
        if (boneNames.find(nodeName) != boneNames.end())
        {
            resource::SkeletonBone bone;
            bone.name = nodeName;
            bone.parentIndex = parentIndex;

            // Store the bone's own local transform
            bone.offsetMatrix = nodeTransform;

            // Store accumulated transforms from non-bone parent nodes
            bone.preTransform = accumulatedTransform;

            currentIndex = static_cast<int32_t>(bones.size());
            boneIndexMap[nodeName] = currentIndex;
            bones.push_back(bone);

            // Reset accumulated transform since we've stored it
            currentAccumulated = glm::mat4(1.0f);
        }
        else
        {
            // This node is not a bone - accumulate its transform for child bones
            currentAccumulated = accumulatedTransform * nodeTransform;
        }

        // Recursively process children
        for (uint32_t i = 0; i < node->mNumChildren; ++i)
        {
            buildBoneHierarchy(node->mChildren[i], boneNames, bones, boneIndexMap,
                               currentIndex, currentAccumulated);
        }
    }

    void Skeleton::computeBindPoses(const std::vector<resource::SkeletonBone>& bones,
                                    std::vector<glm::mat4>& bindPoses) const
    {
        bindPoses.resize(bones.size(), glm::mat4(1.0f));

        for (size_t i = 0; i < bones.size(); ++i)
        {
            const auto& bone = bones[i];

            // Compute world-space bind pose
            // bindPose = parentBindPose * preTransform * offsetMatrix
            glm::mat4 localBindPose = bone.preTransform * bone.offsetMatrix;

            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(i))
            {
                bindPoses[i] = bindPoses[bone.parentIndex] * localBindPose;
            }
            else
            {
                bindPoses[i] = localBindPose;
            }
        }
    }

    void Skeleton::writeString(std::ofstream& file, const std::string& str) const
    {
        resource::endian::writeLE<uint32_t>(file, static_cast<uint32_t>(str.size()));
        if (!str.empty())
        {
            file.write(str.data(), str.size());
        }
    }

    void Skeleton::writeMatrix(std::ofstream& file, const glm::mat4& matrix) const
    {
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                resource::endian::writeLE<float>(file, matrix[col][row]);
            }
        }
    }
}
