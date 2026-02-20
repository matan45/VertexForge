#pragma once
#include "../data/EntityHandle.hpp"
#include "../events/SocketEvents.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>

namespace services
{
    class ISocketProvider
    {
    public:
        virtual ~ISocketProvider() = default;

        virtual bool attachToSocket(EntityHandle childEntity, EntityHandle parentEntity,
                                    const std::string& socketName) = 0;
        virtual void detachFromSocket(EntityHandle childEntity) = 0;
        virtual void setSocketActive(EntityHandle entity, bool active) = 0;

        virtual bool addSocketAttachmentComponent(EntityHandle entity) = 0;
        virtual bool removeSocketAttachmentComponent(EntityHandle entity) = 0;

        [[nodiscard]] virtual std::vector<std::string> getSocketNames(EntityHandle entity) const = 0;
        [[nodiscard]] virtual bool hasSocket(EntityHandle entity, const std::string& socketName) const = 0;
        [[nodiscard]] virtual bool isAttached(EntityHandle entity) const = 0;
        [[nodiscard]] virtual bool hasSocketAttachmentComponent(EntityHandle entity) const = 0;
        [[nodiscard]] virtual std::optional<events::socket::SocketAttachmentData>
            getSocketAttachmentData(EntityHandle entity) const = 0;
        [[nodiscard]] virtual glm::vec3 getSocketWorldPosition(EntityHandle parentEntity,
                                                                const std::string& socketName) const = 0;
        [[nodiscard]] virtual glm::mat4 getSocketWorldTransform(EntityHandle parentEntity,
                                                                 const std::string& socketName) const = 0;
    };
}
