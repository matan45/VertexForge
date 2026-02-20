#pragma once

#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <optional>

namespace events::socket
{
    // ========== DATA ==========

    struct SocketAttachmentData
    {
        ::services::EntityHandle parentEntity;
        std::string parentEntityName;
        std::string socketName;
        bool isActive = true;
    };

    // ========== COMMANDS ==========

    struct AttachToSocketCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle childEntity;
        ::services::EntityHandle parentEntity;
        std::string socketName;
        std::string_view getName() const override { return "AttachToSocket"; }
    };

    struct DetachFromSocketCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle childEntity;
        std::string_view getName() const override { return "DetachFromSocket"; }
    };

    struct SetSocketActiveCommand : ::events::ICommand<void>
    {
        ::services::EntityHandle entity;
        bool active;
        std::string_view getName() const override { return "SetSocketActive"; }
    };

    struct AddSocketAttachmentComponentCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "AddSocketAttachmentComponent"; }
    };

    struct RemoveSocketAttachmentComponentCommand : ::events::ICommand<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveSocketAttachmentComponent"; }
    };

    // ========== QUERIES ==========

    struct GetSocketNamesQuery : ::events::IQuery<std::vector<std::string>>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetSocketNames"; }
    };

    struct HasSocketQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string socketName;
        std::string_view getName() const override { return "HasSocket"; }
    };

    struct IsAttachedQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "IsAttached"; }
    };

    struct HasSocketAttachmentComponentQuery : ::events::IQuery<bool>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "HasSocketAttachmentComponent"; }
    };

    struct GetSocketAttachmentDataQuery : ::events::IQuery<std::optional<SocketAttachmentData>>
    {
        ::services::EntityHandle entity;
        std::string_view getName() const override { return "GetSocketAttachmentData"; }
    };

    struct GetSocketWorldPositionQuery : ::events::IQuery<glm::vec3>
    {
        ::services::EntityHandle parentEntity;
        std::string socketName;
        std::string_view getName() const override { return "GetSocketWorldPosition"; }
    };

    struct GetSocketWorldTransformQuery : ::events::IQuery<glm::mat4>
    {
        ::services::EntityHandle parentEntity;
        std::string socketName;
        std::string_view getName() const override { return "GetSocketWorldTransform"; }
    };

    // ========== NOTIFICATIONS ==========

    struct SocketAttachmentChangedNotification : ::events::INotification
    {
        ::services::EntityHandle childEntity;
        ::services::EntityHandle parentEntity;
        std::string socketName;
        bool attached; // true = attached, false = detached
        std::string_view getName() const override { return "SocketAttachmentChanged"; }
    };

    struct SocketDataSavedNotification : ::events::INotification
    {
        std::string meshPath;
        std::string_view getName() const override { return "SocketDataSaved"; }
    };
}
