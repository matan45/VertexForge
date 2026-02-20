#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../events/SocketEvents.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>

namespace events { class EventDispatcher; }

namespace services
{
    class ISocketProvider;

    class SocketComponentService
    {
    private:
        ISocketProvider* socketProvider;

    public:
        explicit SocketComponentService(ISocketProvider* provider);

        bool attachToSocket(EntityHandle childEntity, EntityHandle parentEntity, const std::string& socketName);
        void detachFromSocket(EntityHandle childEntity);
        void setSocketActive(EntityHandle entity, bool active);

        bool addSocketAttachmentComponent(EntityHandle entity);
        bool removeSocketAttachmentComponent(EntityHandle entity);

        bool addSocketOverrideComponent(EntityHandle entity);
        bool removeSocketOverrideComponent(EntityHandle entity);
        bool hasSocketOverrideComponent(EntityHandle entity) const;

        std::vector<std::string> getSocketNames(EntityHandle entity) const;
        bool hasSocket(EntityHandle entity, const std::string& socketName) const;
        bool isAttached(EntityHandle entity) const;
        bool hasSocketAttachmentComponent(EntityHandle entity) const;
        std::optional<::events::socket::SocketAttachmentData> getSocketAttachmentData(EntityHandle entity) const;
        glm::vec3 getSocketWorldPosition(EntityHandle parentEntity, const std::string& socketName) const;
        glm::mat4 getSocketWorldTransform(EntityHandle parentEntity, const std::string& socketName) const;

        void registerEventHandlers(::events::EventDispatcher& dispatcher);
    };
}
