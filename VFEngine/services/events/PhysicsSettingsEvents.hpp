#pragma once

#include "EventTypes.hpp"
#include "../../utilities/types/PhysicsTypes.hpp"
#include <string>
#include <vector>

namespace events::physics {

    struct ApplyPhysicsSettingsCommand : ::events::ICommand<bool> {
        types::PhysicsSettings settings;
        std::string_view getName() const override { return "ApplyPhysicsSettings"; }
    };

    struct AddCollisionLayerCommand : ::events::ICommand<bool> {
        std::string name;
        std::string_view getName() const override { return "AddCollisionLayer"; }
    };

    struct RemoveCollisionLayerCommand : ::events::ICommand<bool> {
        uint8_t layerIndex;
        std::string_view getName() const override { return "RemoveCollisionLayer"; }
    };

    struct RenameCollisionLayerCommand : ::events::ICommand<bool> {
        uint8_t layerIndex;
        std::string newName;
        std::string_view getName() const override { return "RenameCollisionLayer"; }
    };

    struct SetLayerCollisionCommand : ::events::ICommand<void> {
        uint8_t layer1;
        uint8_t layer2;
        bool shouldCollide;
        std::string_view getName() const override { return "SetLayerCollision"; }
    };

    struct GetPhysicsSettingsQuery : ::events::IQuery<types::PhysicsSettings> {
        std::string_view getName() const override { return "GetPhysicsSettings"; }
    };

    struct GetCollisionLayersQuery : ::events::IQuery<std::vector<types::CollisionLayer>> {
        std::string_view getName() const override { return "GetCollisionLayers"; }
    };

    struct GetLayerNameQuery : ::events::IQuery<std::string> {
        uint8_t layerIndex;
        std::string_view getName() const override { return "GetLayerName"; }
    };

    struct ShouldLayersCollideQuery : ::events::IQuery<bool> {
        uint8_t layer1;
        uint8_t layer2;
        std::string_view getName() const override { return "ShouldLayersCollide"; }
    };

}
