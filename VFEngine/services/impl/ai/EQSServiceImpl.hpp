#pragma once
#include "../../interfaces/ai/IEQSService.hpp"
#include "../../providers/ai/IEQSProvider.hpp"

namespace services
{
    class EQSServiceImpl : public IEQSService
    {
    private:
        IEQSProvider* provider;

    public:
        explicit EQSServiceImpl(IEQSProvider* provider);
        ~EQSServiceImpl() override;

        void registerEventHandlers() override;
    };
}
