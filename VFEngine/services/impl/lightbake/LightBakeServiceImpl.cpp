#include "LightBakeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/lightbake/LightBakeEvents.hpp"
#include "../../providers/lightbake/ILightBakeProvider.hpp"

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

        dispatcher.registerCommandHandler<events::lightbake::LoadLightmapCommand>(
            [this](const events::lightbake::LoadLightmapCommand& cmd) -> bool
            {
                return loadLightmap(cmd.lightmapPath, cmd.texelsPerUnit);
            }
        );

        dispatcher.registerCommandHandler<events::lightbake::ClearLightmapCommand>(
            [this](const events::lightbake::ClearLightmapCommand&)
            {
                clearLightmap();
            }
        );

        dispatcher.registerQueryHandler<events::lightbake::GetTerrainLightmapDataQuery>(
            [this](const events::lightbake::GetTerrainLightmapDataQuery&) -> std::vector<events::lightbake::TerrainTileLightmapEntry>
            {
                auto data = provider->getTerrainLightmapData();
                std::vector<events::lightbake::TerrainTileLightmapEntry> result;
                result.reserve(data.size());
                for (const auto& info : data)
                {
                    events::lightbake::TerrainTileLightmapEntry entry;
                    entry.coordX = info.coordX;
                    entry.coordZ = info.coordZ;
                    entry.scaleOffset = info.scaleOffset;
                    entry.lightmapPath = info.lightmapPath;
                    result.push_back(std::move(entry));
                }
                return result;
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

    bool LightBakeServiceImpl::loadLightmap(const std::string& path, float texelsPerUnit)
    {
        return provider->loadLightmap(path, texelsPerUnit);
    }

    void LightBakeServiceImpl::clearLightmap()
    {
        provider->clearLightmap();
    }
}
