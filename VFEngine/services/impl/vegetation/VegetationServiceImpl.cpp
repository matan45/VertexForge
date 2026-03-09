#include "VegetationServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    VegetationServiceImpl::VegetationServiceImpl(IVegetationProvider* vegetationProvider,
                                                   IVegetationRenderProvider* vegetationRenderProvider)
        : provider(vegetationProvider)
        , renderProvider(vegetationRenderProvider)
    {
    }

    VegetationServiceImpl::~VegetationServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (sceneClearedToken.isValid())
        {
            dispatcher.unsubscribe(sceneClearedToken);
        }
        if (terrainDeletedToken.isValid())
        {
            dispatcher.unsubscribe(terrainDeletedToken);
        }
    }

    void VegetationServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetation::AddVegetationSpeciesCommand>(
            [this](const auto& cmd)
            {
                return addSpecies(cmd.config);
            });

        dispatcher.registerCommandHandler<events::vegetation::RemoveVegetationSpeciesCommand>(
            [this](const auto& cmd)
            {
                removeSpecies(cmd.speciesId);
            });

        dispatcher.registerCommandHandler<events::vegetation::UpdateVegetationSpeciesCommand>(
            [this](const auto& cmd)
            {
                updateSpecies(cmd.speciesId, cmd.config);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetVegetationSpeciesQuery>(
            [this](const auto& q)
            {
                auto* species = provider->getSpecies(q.speciesId);
                return species ? *species : vegetation::VegetationSpeciesConfig{};
            });

        dispatcher.registerQueryHandler<events::vegetation::GetAllVegetationSpeciesQuery>(
            [this](const auto&)
            {
                return provider->getAllSpecies();
            });

        dispatcher.registerQueryHandler<events::vegetation::GetVegetationSpeciesCountQuery>(
            [this](const auto&)
            {
                return provider->getSpeciesCount();
            });

        dispatcher.registerCommandHandler<events::vegetation::ClearAllVegetationSpeciesCommand>(
            [this](const auto&)
            {
                provider->clearAllSpecies();
                if (renderProvider)
                {
                    renderProvider->clearAllSpecies();
                }
            });

        dispatcher.registerCommandHandler<events::vegetation::SetVegetationDebugLODViewCommand>(
            [this](const auto& cmd)
            {
                if (renderProvider)
                {
                    renderProvider->setDebugLODView(cmd.enabled);
                }
            });

        dispatcher.registerQueryHandler<events::vegetation::GetVegetationDebugLODViewQuery>(
            [this](const auto&)
            {
                return renderProvider ? renderProvider->isDebugLODView() : false;
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const auto&)
            {
                provider->clearAllSpecies();
                if (renderProvider)
                {
                    renderProvider->clearAllSpecies();
                }
            });

        terrainDeletedToken = dispatcher.subscribe<events::terrain::TerrainDeletedNotification>(
            [this](const auto&)
            {
                provider->clearAllSpecies();
                if (renderProvider)
                {
                    renderProvider->clearAllSpecies();
                }
            });
    }

    uint32_t VegetationServiceImpl::addSpecies(const vegetation::VegetationSpeciesConfig& config)
    {
        uint32_t id = provider->addSpecies(config);

        if (renderProvider)
        {
            renderProvider->updateSpecies(id, config);
        }

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = id;
        events::EventDispatcher::instance().publish(notification);

        return id;
    }

    void VegetationServiceImpl::removeSpecies(uint32_t speciesId)
    {
        provider->removeSpecies(speciesId);

        if (renderProvider)
        {
            renderProvider->removeSpecies(speciesId);
        }

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = speciesId;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationServiceImpl::updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config)
    {
        provider->updateSpecies(speciesId, config);

        if (renderProvider)
        {
            renderProvider->updateSpecies(speciesId, config);
        }

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = speciesId;
        events::EventDispatcher::instance().publish(notification);
    }
}
