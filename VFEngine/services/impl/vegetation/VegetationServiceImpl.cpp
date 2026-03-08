#include "VegetationServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationEvents.hpp"
#include "../../events/project/SceneEvents.hpp"

namespace services
{
    VegetationServiceImpl::VegetationServiceImpl(IVegetationProvider* vegetationProvider)
        : provider(vegetationProvider)
    {
    }

    VegetationServiceImpl::~VegetationServiceImpl()
    {
        if (sceneClearedToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
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
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const auto&)
            {
                provider->clearAllSpecies();
            });
    }

    uint32_t VegetationServiceImpl::addSpecies(const vegetation::VegetationSpeciesConfig& config)
    {
        uint32_t id = provider->addSpecies(config);

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = id;
        events::EventDispatcher::instance().publish(notification);

        return id;
    }

    void VegetationServiceImpl::removeSpecies(uint32_t speciesId)
    {
        provider->removeSpecies(speciesId);

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = speciesId;
        events::EventDispatcher::instance().publish(notification);
    }

    void VegetationServiceImpl::updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config)
    {
        provider->updateSpecies(speciesId, config);

        events::vegetation::VegetationSpeciesChangedNotification notification;
        notification.speciesId = speciesId;
        events::EventDispatcher::instance().publish(notification);
    }
}
