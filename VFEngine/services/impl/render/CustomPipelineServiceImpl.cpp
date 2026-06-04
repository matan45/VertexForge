#include "CustomPipelineServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/render/CustomPipelineEvents.hpp"
#include "../../providers/render/ICustomPipelineProvider.hpp"

namespace services
{
    CustomPipelineServiceImpl::CustomPipelineServiceImpl(ICustomPipelineProvider* provider)
        : provider(provider)
    {
    }

    CustomPipelineServiceImpl::~CustomPipelineServiceImpl() = default;

    void CustomPipelineServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerPipelineHandlers(dispatcher);
    }

    void CustomPipelineServiceImpl::registerPipelineHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::custompipeline::CreateCustomPipelineCommand>(
            [this](const events::custompipeline::CreateCustomPipelineCommand& cmd) -> plugin::CustomPipelineHandle
            {
                return provider->createPipeline(cmd.desc);
            }
        );

        dispatcher.registerCommandHandler<events::custompipeline::UploadCustomMeshCommand>(
            [this](const events::custompipeline::UploadCustomMeshCommand& cmd) -> plugin::CustomMeshHandle
            {
                return provider->uploadMesh(std::move(cmd.data));
            }
        );

        dispatcher.registerCommandHandler<events::custompipeline::EnqueueCustomDrawCommand>(
            [this](const events::custompipeline::EnqueueCustomDrawCommand& cmd)
            {
                provider->enqueueDraw(std::move(cmd.item));
            }
        );

        dispatcher.registerCommandHandler<events::custompipeline::DestroyCustomPipelineCommand>(
            [this](const events::custompipeline::DestroyCustomPipelineCommand& cmd)
            {
                provider->destroyPipeline(cmd.handle);
            }
        );

        dispatcher.registerCommandHandler<events::custompipeline::DestroyCustomMeshCommand>(
            [this](const events::custompipeline::DestroyCustomMeshCommand& cmd)
            {
                provider->destroyMesh(cmd.handle);
            }
        );
    }
}
