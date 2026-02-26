#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <entt/entt.hpp>
#include "../uuid/UUID.hpp"
#include "../rendertexture/RenderTextureTypes.hpp"

namespace components
{
    struct WorldTransformComponent
    {
        glm::mat4 worldMatrix;
    };

    struct ParentComponent
    {
        entt::entity parent = entt::null;
    };

    struct ChildrenComponent
    {
        std::vector<entt::entity> children;
    };

    struct NameComponent
    {
        std::string name;
        bool isActive = true;
    };

    struct UUIDComponent
    {
        uuid::UUID id;

        UUIDComponent() : id()
        {
        }

        explicit UUIDComponent(uuid::UUID existingId) : id(existingId)
        {
        }

        explicit UUIDComponent(uint64_t existingId) : id(existingId)
        {
        }
    };

    struct IBLComponent
    {
        std::string fileName;
    };

    struct TransformComponent
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        bool isDirty = true;
        bool isStatic = true;

        glm::mat4 getMatrix() const
        {
            auto transform = glm::mat4(1.0f);
            transform = glm::translate(transform, position);
            transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
            transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
            transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
            transform = glm::scale(transform, scale);
            return transform;
        }
    };

    struct CameraComponent
    {
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        bool isPerspective = true;
        bool isPrimary = false;
        bool showFrustum = false;
        float fieldOfView = 90.0f;
        float orthoSize = 10.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 1.778f;

        uint32_t cameraId = 0;
        bool enableOcclusionCulling = true;
        bool isRegistered = false;

        static inline uint32_t nextCameraId = 0;

        static uint32_t generateCameraId()
        {
            return nextCameraId++;
        }

        CameraComponent()
        {
            cameraId = generateCameraId();
            updateProjectionMatrix();
            viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        }

        void updateProjectionMatrix()
        {
            if (isPerspective)
            {
                projectionMatrix = glm::perspective(
                    glm::radians(fieldOfView),
                    aspectRatio,
                    nearPlane,
                    farPlane
                );
            }
            else
            {
                float orthoHalfWidth = orthoSize * aspectRatio;
                projectionMatrix = glm::ortho(
                    -orthoHalfWidth,
                    orthoHalfWidth,
                    -orthoSize,
                    orthoSize,
                    nearPlane,
                    farPlane
                );
            }

            projectionMatrix[1][1] *= -1;
        }

        void updateViewMatrix(const glm::vec3& position, const glm::vec3& rotation)
        {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, position);
            model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));

            viewMatrix = glm::inverse(model);
        }

        void updateViewMatrixFromWorld(const glm::mat4& worldMatrix)
        {
            viewMatrix = glm::inverse(worldMatrix);
        }
    };

    struct RenderTextureComponent
    {
        rendertexture::RenderTextureId textureId = rendertexture::INVALID_RENDER_TEXTURE_ID;
        uint32_t width = 512;
        uint32_t height = 512;
        rendertexture::UpdateMode updateMode = rendertexture::UpdateMode::EveryFrame;
        float fixedIntervalSeconds = 1.0f / 30.0f;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        uint32_t priority = 0;
        bool enabled = true;
        bool needsRender = true;
        float timeSinceLastRender = 0.0f;
    };

    struct MeshComponent
    {
        std::string meshPath;
        std::string animatorPath;
        bool showBoundingBox = false;
        bool applyRootMotion = false;
        float maxDrawDistance = 0.0f; // 0 = use category default from render config
    };

    struct MaterialComponent
    {
        std::string defaultMaterial;
        std::map<std::string, std::string> subMeshMaterials;
        std::map<std::string, float> parameterOverrides;

        void setSubMeshMaterial(const std::string& submeshName, const std::string& matPath)
        {
            subMeshMaterials[submeshName] = matPath;
        }

        void setDefaultMaterial(const std::string& matPath)
        {
            defaultMaterial = matPath;
        }

        std::string getMaterialForSubmesh(const std::string& submeshName) const
        {
            auto it = subMeshMaterials.find(submeshName);
            if (it != subMeshMaterials.end())
            {
                return it->second;
            }
            return defaultMaterial;
        }
    };
}
