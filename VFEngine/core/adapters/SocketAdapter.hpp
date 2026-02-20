#pragma once
#include "../../services/providers/ISocketProvider.hpp"

namespace core
{
    class SocketAdapter : public services::ISocketProvider
    {
    public:
        SocketAdapter();
        ~SocketAdapter() override;

        bool attachToSocket(services::EntityHandle childEntity, services::EntityHandle parentEntity,
                            const std::string& socketName) override;
        void detachFromSocket(services::EntityHandle childEntity) override;
        void setSocketActive(services::EntityHandle entity, bool active) override;

        bool addSocketAttachmentComponent(services::EntityHandle entity) override;
        bool removeSocketAttachmentComponent(services::EntityHandle entity) override;

        bool addSocketOverrideComponent(services::EntityHandle entity) override;
        bool removeSocketOverrideComponent(services::EntityHandle entity) override;
        [[nodiscard]] bool hasSocketOverrideComponent(services::EntityHandle entity) const override;

        [[nodiscard]] std::vector<std::string> getSocketNames(services::EntityHandle entity) const override;
        [[nodiscard]] bool hasSocket(services::EntityHandle entity, const std::string& socketName) const override;
        [[nodiscard]] bool isAttached(services::EntityHandle entity) const override;
        [[nodiscard]] bool hasSocketAttachmentComponent(services::EntityHandle entity) const override;
        [[nodiscard]] std::optional<events::socket::SocketAttachmentData>
            getSocketAttachmentData(services::EntityHandle entity) const override;
        [[nodiscard]] glm::vec3 getSocketWorldPosition(services::EntityHandle parentEntity,
                                                        const std::string& socketName) const override;
        [[nodiscard]] glm::mat4 getSocketWorldTransform(services::EntityHandle parentEntity,
                                                         const std::string& socketName) const override;
    };
}
