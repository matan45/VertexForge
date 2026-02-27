#pragma once
#include "../interfaces/ILightBakeService.hpp"

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class ILightBakeProvider;

    class LightBakeServiceImpl : public ILightBakeService
    {
    private:
        ILightBakeProvider* provider;

    public:
        explicit LightBakeServiceImpl(ILightBakeProvider* provider);
        ~LightBakeServiceImpl() override;

        void registerEventHandlers() override;

        void startBake(const LightBakeConfig& config) override;
        void cancelBake() override;
        float getBakeProgress() const override;
        bool isBaking() const override;
        LightBakeResult getResult() const override;
        bool loadLightmap(const std::string& path, float texelsPerUnit) override;

    private:
        void registerBakeHandlers(::events::EventDispatcher& dispatcher);
    };
}
