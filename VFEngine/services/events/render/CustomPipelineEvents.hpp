#pragma once
#include "../EventTypes.hpp"
#include "../../data/CustomPipelineTypes.hpp"

namespace events::custompipeline {

    struct CreateCustomPipelineCommand : ICommand<plugin::CustomPipelineHandle> {
        mutable plugin::CustomPipelineDesc desc;

        std::string_view getName() const override { return "CreateCustomPipeline"; }
    };

    struct UploadCustomMeshCommand : ICommand<plugin::CustomMeshHandle> {
        mutable plugin::CustomMeshData data;

        std::string_view getName() const override { return "UploadCustomMesh"; }
    };

    struct EnqueueCustomDrawCommand : ICommand<> {
        mutable plugin::CustomDrawItem item;

        std::string_view getName() const override { return "EnqueueCustomDraw"; }
    };

    struct DestroyCustomPipelineCommand : ICommand<> {
        plugin::CustomPipelineHandle handle;

        std::string_view getName() const override { return "DestroyCustomPipeline"; }
    };

    struct DestroyCustomMeshCommand : ICommand<> {
        plugin::CustomMeshHandle handle;

        std::string_view getName() const override { return "DestroyCustomMesh"; }
    };

}
