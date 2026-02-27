#include "LightBakeServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/LightBakeEvents.hpp"
#include "../providers/ILightBakeProvider.hpp"

namespace services
{
    LightBakeServiceImpl::LightBakeServiceImpl(ILightBakeProvider* provider)
        : provider(provider)
    {
    }

    LightBakeServiceImpl::~LightBakeServiceImpl() = default;

    void LightBakeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        registerBakeHandlers(dispatcher);
    }

    void LightBakeServiceImpl::registerBakeHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::lightbake::StartBakeCommand>(
            [this](const events::lightbake::StartBakeCommand& cmd)
            {
                startBake(cmd.config);
            }
        );

        dispatcher.registerCommandHandler<events::lightbake::CancelBakeCommand>(
            [this](const events::lightbake::CancelBakeCommand&)
            {
                cancelBake();
            }
        );

        dispatcher.registerQueryHandler<events::lightbake::GetBakeProgressQuery>(
            [this](const events::lightbake::GetBakeProgressQuery&) -> float
            {
                return getBakeProgress();
            }
        );

        dispatcher.registerQueryHandler<events::lightbake::IsBakingQuery>(
            [this](const events::lightbake::IsBakingQuery&) -> bool
            {
                return isBaking();
            }
        );

        dispatcher.registerQueryHandler<events::lightbake::GetBakeResultQuery>(
            [this](const events::lightbake::GetBakeResultQuery&) -> LightBakeResult
            {
                return getResult();
            }
        );
    }

    void LightBakeServiceImpl::startBake(const LightBakeConfig& config)
    {
        provider->startBake(config);
    }

    void LightBakeServiceImpl::cancelBake()
    {
        provider->cancelBake();
    }

    float LightBakeServiceImpl::getBakeProgress() const
    {
        return provider->getBakeProgress();
    }

    bool LightBakeServiceImpl::isBaking() const
    {
        return provider->isBaking();
    }

    LightBakeResult LightBakeServiceImpl::getResult() const
    {
        return provider->getResult();
    }
}
