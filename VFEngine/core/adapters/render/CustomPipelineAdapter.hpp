#pragma once
#include "../../services/providers/render/ICustomPipelineProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class CustomPipelineAdapter : public services::ICustomPipelineProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit CustomPipelineAdapter(controllers::OffScreen* offScreen);
        ~CustomPipelineAdapter() override = default;

        plugin::CustomPipelineHandle createPipeline(const plugin::CustomPipelineDesc& desc) override;
        plugin::CustomMeshHandle uploadMesh(plugin::CustomMeshData&& data) override;
        void enqueueDraw(plugin::CustomDrawItem&& item) override;
        void destroyPipeline(plugin::CustomPipelineHandle handle) override;
        void destroyMesh(plugin::CustomMeshHandle handle) override;
    };
}
