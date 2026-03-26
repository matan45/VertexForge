#include "EQSServiceImpl.hpp"
#include "../../events/ai/EQSEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include <cassert>

namespace services
{
    EQSServiceImpl::EQSServiceImpl(IEQSProvider* provider)
        : provider(provider)
    {
        assert(provider && "IEQSProvider must not be null");
    }

    EQSServiceImpl::~EQSServiceImpl() = default;

    void EQSServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::ai::RegisterEQSQueryCommand>(
            [this](const auto& cmd)
            {
                provider->registerQuery(cmd.queryName, cmd.queryDef);
            });

        dispatcher.registerCommandHandler<events::ai::SubmitEQSQueryCommand>(
            [this](const auto& cmd)
            {
                return provider->submitQuery(cmd.queryName, cmd.context);
            });

        dispatcher.registerQueryHandler<events::ai::GetEQSQueryResultQuery>(
            [this](const auto& query)
            {
                return provider->getResult(query.handle);
            });

        dispatcher.registerCommandHandler<events::ai::CancelEQSQueryCommand>(
            [this](const auto& cmd)
            {
                provider->cancelQuery(cmd.handle);
            });
    }

    void EQSServiceImpl::update(float frameBudgetMs)
    {
        if (provider)
            provider->update(frameBudgetMs);
    }
}
