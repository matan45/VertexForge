#include "VolumetricNavServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <cassert>

namespace services
{
    VolumetricNavServiceImpl::VolumetricNavServiceImpl(IVolumetricNavProvider* provider,
                                                        std::function<bool(glm::vec3, glm::vec3, float)> raycastFn)
        : provider(provider), agentManager(provider), physicsRaycast(std::move(raycastFn))
    {
        assert(provider && "VolumetricNavProvider must not be null");
    }

    void VolumetricNavServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::volumetric::BakeVolumetricNavCommand>(
            [this](const events::volumetric::BakeVolumetricNavCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto enttEntity = internal::fromHandle(cmd.entity);
                if (!registry.valid(enttEntity) ||
                    !registry.all_of<components::VolumetricNavVolumeComponent>(enttEntity))
                {
                    return;
                }

                const auto& vol = registry.get<components::VolumetricNavVolumeComponent>(enttEntity);

                auto raycastFn = physicsRaycast;
                auto isBlocked = [raycastFn](glm::vec3 pos, float radius) -> bool
                {
                    if (!raycastFn)
                        return false;

                    // 6-directional raycast probe to approximate sphere overlap
                    static const glm::vec3 dirs[] = {
                        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
                    };
                    for (const auto& dir : dirs)
                    {
                        if (raycastFn(pos, dir, radius))
                            return true;
                    }
                    return false;
                };

                bool result = provider->bakeVolume(vol.boundsMin, vol.boundsMax, vol.voxelSize,
                                                    vol.connectivity, vol.agentClearance, isBlocked);

                if (result)
                {
                    auto& volMut = registry.get<components::VolumetricNavVolumeComponent>(enttEntity);
                    volMut.isBaked = true;
                }

                events::volumetric::VolumetricBakeCompleteNotification notif;
                notif.success = result;
                notif.message = result ? "Volumetric navigation bake complete" : "Volumetric navigation bake failed";
                ::events::EventDispatcher::instance().publish(notif);
            });

        dispatcher.registerCommandHandler<events::volumetric::ClearVolumetricNavCommand>(
            [this](const events::volumetric::ClearVolumetricNavCommand&)
            {
                provider->clear();
                agentManager.clear();
            });

        dispatcher.registerCommandHandler<events::volumetric::AddVolumetricAgentCommand>(
            [this](const events::volumetric::AddVolumetricAgentCommand& cmd)
            {
                agentManager.addAgent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::volumetric::RemoveVolumetricAgentCommand>(
            [this](const events::volumetric::RemoveVolumetricAgentCommand& cmd)
            {
                agentManager.removeAgent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::volumetric::SetVolumetricAgentDestinationCommand>(
            [this](const events::volumetric::SetVolumetricAgentDestinationCommand& cmd)
            {
                agentManager.setDestination(cmd.entity, cmd.target);
            });

        dispatcher.registerCommandHandler<events::volumetric::StopVolumetricAgentCommand>(
            [this](const events::volumetric::StopVolumetricAgentCommand& cmd)
            {
                agentManager.stopAgent(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::volumetric::FindPath3DQuery>(
            [this](const events::volumetric::FindPath3DQuery& query)
            {
                return provider->findPath3D(query.start, query.end);
            });

        dispatcher.registerQueryHandler<events::volumetric::IsPointNavigable3DQuery>(
            [this](const events::volumetric::IsPointNavigable3DQuery& query)
            {
                return provider->isPointNavigable(query.point);
            });

        dispatcher.registerQueryHandler<events::volumetric::GetVolumetricBakeProgressQuery>(
            [this](const events::volumetric::GetVolumetricBakeProgressQuery&)
            {
                return provider->getBakeProgress();
            });
    }
}
