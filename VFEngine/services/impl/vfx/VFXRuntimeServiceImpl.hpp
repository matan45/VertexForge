#pragma once
#include "../../data/VFXTypes.hpp"

namespace services
{
    class IVFXRuntimeProvider;

    class VFXRuntimeServiceImpl
    {
    private:
        IVFXRuntimeProvider* vfxProvider = nullptr;

    public:
        explicit VFXRuntimeServiceImpl(IVFXRuntimeProvider* provider);
        ~VFXRuntimeServiceImpl() = default;

        void registerEventHandlers();

        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        void destroyInstance(VFXInstanceId id);
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void playInstance(VFXInstanceId id);
        void stopInstance(VFXInstanceId id);
        void resetInstance(VFXInstanceId id);
        void update(float deltaTime);

        bool isInstancePlaying(VFXInstanceId id) const;
    };
}
