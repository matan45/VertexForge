#include "SocketComponentService.hpp"
#include "../../providers/ISocketProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/SocketEvents.hpp"

namespace services
{
    SocketComponentService::SocketComponentService(ISocketProvider* provider)
        : socketProvider(provider)
    {
    }

    bool SocketComponentService::attachToSocket(EntityHandle childEntity, EntityHandle parentEntity,
                                                 const std::string& socketName)
    {
        return socketProvider->attachToSocket(childEntity, parentEntity, socketName);
    }

    void SocketComponentService::detachFromSocket(EntityHandle childEntity)
    {
        socketProvider->detachFromSocket(childEntity);
    }

    void SocketComponentService::setSocketActive(EntityHandle entity, bool active)
    {
        socketProvider->setSocketActive(entity, active);
    }

    std::vector<std::string> SocketComponentService::getSocketNames(EntityHandle entity) const
    {
        return socketProvider->getSocketNames(entity);
    }

    bool SocketComponentService::hasSocket(EntityHandle entity, const std::string& socketName) const
    {
        return socketProvider->hasSocket(entity, socketName);
    }

    bool SocketComponentService::isAttached(EntityHandle entity) const
    {
        return socketProvider->isAttached(entity);
    }

    bool SocketComponentService::addSocketAttachmentComponent(EntityHandle entity)
    {
        return socketProvider->addSocketAttachmentComponent(entity);
    }

    bool SocketComponentService::removeSocketAttachmentComponent(EntityHandle entity)
    {
        return socketProvider->removeSocketAttachmentComponent(entity);
    }

    bool SocketComponentService::hasSocketAttachmentComponent(EntityHandle entity) const
    {
        return socketProvider->hasSocketAttachmentComponent(entity);
    }

    std::optional<::events::socket::SocketAttachmentData>
    SocketComponentService::getSocketAttachmentData(EntityHandle entity) const
    {
        return socketProvider->getSocketAttachmentData(entity);
    }

    glm::vec3 SocketComponentService::getSocketWorldPosition(EntityHandle parentEntity,
                                                              const std::string& socketName) const
    {
        return socketProvider->getSocketWorldPosition(parentEntity, socketName);
    }

    glm::mat4 SocketComponentService::getSocketWorldTransform(EntityHandle parentEntity,
                                                               const std::string& socketName) const
    {
        return socketProvider->getSocketWorldTransform(parentEntity, socketName);
    }

    void SocketComponentService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<::events::socket::AttachToSocketCommand>(
            [this](const ::events::socket::AttachToSocketCommand& cmd)
            {
                return attachToSocket(cmd.childEntity, cmd.parentEntity, cmd.socketName);
            });

        dispatcher.registerCommandHandler<::events::socket::DetachFromSocketCommand>(
            [this](const ::events::socket::DetachFromSocketCommand& cmd)
            {
                detachFromSocket(cmd.childEntity);
            });

        dispatcher.registerCommandHandler<::events::socket::SetSocketActiveCommand>(
            [this](const ::events::socket::SetSocketActiveCommand& cmd)
            {
                setSocketActive(cmd.entity, cmd.active);
            });

        dispatcher.registerQueryHandler<::events::socket::GetSocketNamesQuery>(
            [this](const ::events::socket::GetSocketNamesQuery& query)
            {
                return getSocketNames(query.entity);
            });

        dispatcher.registerQueryHandler<::events::socket::HasSocketQuery>(
            [this](const ::events::socket::HasSocketQuery& query)
            {
                return hasSocket(query.entity, query.socketName);
            });

        dispatcher.registerQueryHandler<::events::socket::IsAttachedQuery>(
            [this](const ::events::socket::IsAttachedQuery& query)
            {
                return isAttached(query.entity);
            });

        dispatcher.registerQueryHandler<::events::socket::GetSocketWorldPositionQuery>(
            [this](const ::events::socket::GetSocketWorldPositionQuery& query)
            {
                return getSocketWorldPosition(query.parentEntity, query.socketName);
            });

        dispatcher.registerQueryHandler<::events::socket::GetSocketWorldTransformQuery>(
            [this](const ::events::socket::GetSocketWorldTransformQuery& query)
            {
                return getSocketWorldTransform(query.parentEntity, query.socketName);
            });

        dispatcher.registerCommandHandler<::events::socket::AddSocketAttachmentComponentCommand>(
            [this](const ::events::socket::AddSocketAttachmentComponentCommand& cmd)
            {
                return addSocketAttachmentComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<::events::socket::RemoveSocketAttachmentComponentCommand>(
            [this](const ::events::socket::RemoveSocketAttachmentComponentCommand& cmd)
            {
                return removeSocketAttachmentComponent(cmd.entity);
            });

        dispatcher.registerQueryHandler<::events::socket::HasSocketAttachmentComponentQuery>(
            [this](const ::events::socket::HasSocketAttachmentComponentQuery& query)
            {
                return hasSocketAttachmentComponent(query.entity);
            });

        dispatcher.registerQueryHandler<::events::socket::GetSocketAttachmentDataQuery>(
            [this](const ::events::socket::GetSocketAttachmentDataQuery& query)
            {
                return getSocketAttachmentData(query.entity);
            });
    }
}
