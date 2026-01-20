#pragma once
#include "../data/VFXTypes.hpp"

namespace services
{
    class IVFXRuntimeProvider;

    class VFXRuntimeServiceImpl
    {
    public:
        explicit VFXRuntimeServiceImpl(IVFXRuntimeProvider* provider);
        ~VFXRuntimeServiceImpl() = default;

        void registerEventHandlers();

        // Direct API (optional, for non-event usage)
        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        void destroyInstance(VFXInstanceId id);
        void destroyAllInstances();
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void playInstance(VFXInstanceId id);
        void stopInstance(VFXInstanceId id);
        void resetInstance(VFXInstanceId id);
        void update(float deltaTime);
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& cameraPos, float time);

        bool isInstancePlaying(VFXInstanceId id) const;
        bool isInstanceActive(VFXInstanceId id) const;
        size_t getInstanceCount() const;
        size_t getTotalParticleCount() const;
        bool isInitialized() const;

    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;
    };
}
