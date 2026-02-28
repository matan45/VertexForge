#pragma once
#include "../providers/ILightBakeProvider.hpp"

namespace services
{
    class ILightBakeService
    {
    public:
        virtual ~ILightBakeService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void startBake(const LightBakeConfig& config) = 0;
        virtual void cancelBake() = 0;
        virtual float getBakeProgress() const = 0;
        virtual bool isBaking() const = 0;
        virtual LightBakeResult getResult() const = 0;
        virtual bool loadLightmap(const std::string& path, float texelsPerUnit) = 0;
        virtual void clearLightmap() = 0;
    };
}
