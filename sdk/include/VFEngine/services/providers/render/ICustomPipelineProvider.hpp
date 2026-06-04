#pragma once
#include "../../data/CustomPipelineTypes.hpp"

namespace services {

    class ICustomPipelineProvider {
    public:
        virtual ~ICustomPipelineProvider() = default;

        virtual plugin::CustomPipelineHandle createPipeline(const plugin::CustomPipelineDesc& desc) = 0;

        virtual plugin::CustomMeshHandle uploadMesh(plugin::CustomMeshData&& data) = 0;

        virtual void enqueueDraw(plugin::CustomDrawItem&& item) = 0;

        virtual void destroyPipeline(plugin::CustomPipelineHandle handle) = 0;

        virtual void destroyMesh(plugin::CustomMeshHandle handle) = 0;
    };

}
