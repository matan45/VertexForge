#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <entt/entt.hpp>
#include "../uuid/UUID.hpp"
#include "../asset/AssetRef.hpp"
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
        asset::AssetRef hdrRef;
    };

    struct WorldSectorComponent
    {
        std::string worldFilePath;
    };

    struct HLODProxyComponent
    {
        int32_t cellX = 0;
        int32_t cellZ = 0;
        uint8_t tier = 0;

        HLODProxyComponent() = default;
        HLODProxyComponent(int32_t x, int32_t z, uint8_t t) : cellX(x), cellZ(z), tier(t) {}
    };

    struct AdditiveSceneComponent
    {
        std::string sceneName;
        std::string scenePath;
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
        float farPlane = 2000.0f;
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

        // Build the camera view from the entity's world transform while interpreting the camera's
        // OWN rotation in camera (Y·X·Z) order. Two traps this avoids (VK-1350):
        //   * Inverting worldMatrix directly bakes in TransformComponent::getMatrix()'s object
        //     X·Y·Z order, whose fixed-pitch vertical look is cos(yaw)·sin(pitch) — it zeroes at
        //     yaw=±90° and flips sign past it (the sky-flip).
        //   * Decomposing worldMatrix to Euler goes through extractEulerAngleXYZ, whose gimbal
        //     singularity is on the middle (yaw) axis at ±90°, producing the same flip.
        // Instead, strip the local object-order rotation back out of the world matrix (recovering
        // the parent's world transform exactly) and re-apply the local rotation in camera order.
        // This keeps a rotating parent's orientation (child/vehicle cameras) and never extracts an
        // Euler angle, so there is no singularity. For a root camera the parent term is identity and
        // this reduces to a plain camera-order local view.
        void updateViewMatrixFromWorldEye(const glm::mat4& worldMatrix, const TransformComponent& localTransform)
        {
            glm::mat4 parentWorld = worldMatrix * glm::inverse(localTransform.getMatrix());

            glm::mat4 localCamera = glm::translate(glm::mat4(1.0f), localTransform.position);
            localCamera = glm::rotate(localCamera, glm::radians(localTransform.rotation.y), glm::vec3(0, 1, 0));
            localCamera = glm::rotate(localCamera, glm::radians(localTransform.rotation.x), glm::vec3(1, 0, 0));
            localCamera = glm::rotate(localCamera, glm::radians(localTransform.rotation.z), glm::vec3(0, 0, 1));
            localCamera = glm::scale(localCamera, localTransform.scale);

            viewMatrix = glm::inverse(parentWorld * localCamera);
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
        asset::AssetRef meshRef;
        asset::AssetRef animatorRef;
        bool showBoundingBox = false;
        bool applyRootMotion = false;
        float maxDrawDistance = 0.0f; // 0 = use category default from render config
        int32_t submeshIndex = -1; // -1 = render all, >= 0 = render only this submesh
    };

    struct MaterialComponent
    {
        asset::AssetRef defaultMaterialRef;
        std::map<std::string, asset::AssetRef> subMeshMaterials;
        std::map<std::string, float> parameterOverrides;

        void setSubMeshMaterial(const std::string& submeshName, const asset::AssetRef& matRef)
        {
            subMeshMaterials[submeshName] = matRef;
        }

        void setDefaultMaterial(const asset::AssetRef& matRef)
        {
            defaultMaterialRef = matRef;
        }

        asset::AssetRef getMaterialForSubmesh(const std::string& submeshName) const
        {
            auto it = subMeshMaterials.find(submeshName);
            if (it != subMeshMaterials.end())
            {
                return it->second;
            }
            return defaultMaterialRef;
        }
    };
}
