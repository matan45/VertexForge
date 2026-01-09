#pragma once

#include "EventTypes.hpp"
#include "../../utilities/types/PhysicsTypes.hpp"
#include <string>
#include <vector>

namespace events::physics {

    // ============================================================
    // COMMANDS - Modify physics settings
    // ============================================================

    // Apply physics settings to the physics system
    struct ApplyPhysicsSettingsCommand : ::events::ICommand<bool> {
        types::PhysicsSettings settings;
        std::string_view getName() const override { return "ApplyPhysicsSettings"; }
    };

    // Save physics settings to a file
    struct SavePhysicsSettingsCommand : ::events::ICommand<bool> {
        types::PhysicsSettings settings;
        std::string filename;  // Optional, uses default if empty
        std::string_view getName() const override { return "SavePhysicsSettings"; }
    };

    // Load physics settings from a file
    struct LoadPhysicsSettingsCommand : ::events::ICommand<bool> {
        std::string filename;  // Optional, uses default if empty
        std::string_view getName() const override { return "LoadPhysicsSettings"; }
    };

    // Add a new collision layer
    struct AddCollisionLayerCommand : ::events::ICommand<bool> {
        std::string name;
        std::string_view getName() const override { return "AddCollisionLayer"; }
    };

    // Remove a collision layer by index
    struct RemoveCollisionLayerCommand : ::events::ICommand<bool> {
        uint8_t layerIndex;
        std::string_view getName() const override { return "RemoveCollisionLayer"; }
    };

    // Rename a collision layer
    struct RenameCollisionLayerCommand : ::events::ICommand<bool> {
        uint8_t layerIndex;
        std::string newName;
        std::string_view getName() const override { return "RenameCollisionLayer"; }
    };

    // Set collision between two layers
    struct SetLayerCollisionCommand : ::events::ICommand<void> {
        uint8_t layer1;
        uint8_t layer2;
        bool shouldCollide;
        std::string_view getName() const override { return "SetLayerCollision"; }
    };

    // ============================================================
    // QUERIES - Read physics settings
    // ============================================================

    // Get current physics settings
    struct GetPhysicsSettingsQuery : ::events::IQuery<types::PhysicsSettings> {
        std::string_view getName() const override { return "GetPhysicsSettings"; }
    };

    // Get all collision layers
    struct GetCollisionLayersQuery : ::events::IQuery<std::vector<types::CollisionLayer>> {
        std::string_view getName() const override { return "GetCollisionLayers"; }
    };

    // Get layer name by index
    struct GetLayerNameQuery : ::events::IQuery<std::string> {
        uint8_t layerIndex;
        std::string_view getName() const override { return "GetLayerName"; }
    };

    // Check if two layers should collide
    struct ShouldLayersCollideQuery : ::events::IQuery<bool> {
        uint8_t layer1;
        uint8_t layer2;
        std::string_view getName() const override { return "ShouldLayersCollide"; }
    };

    // ============================================================
    // NOTIFICATIONS - Physics settings changes (pub/sub)
    // ============================================================

    // Published when physics settings are changed
    struct PhysicsSettingsChangedNotification : ::events::INotification {
        types::PhysicsSettings settings;
        std::string_view getName() const override { return "PhysicsSettingsChanged"; }
    };

    // Published when collision layers are modified
    struct CollisionLayersChangedNotification : ::events::INotification {
        std::vector<types::CollisionLayer> layers;
        std::string_view getName() const override { return "CollisionLayersChanged"; }
    };

    // Published when collision matrix is modified
    struct CollisionMatrixChangedNotification : ::events::INotification {
        std::string_view getName() const override { return "CollisionMatrixChanged"; }
    };

}
