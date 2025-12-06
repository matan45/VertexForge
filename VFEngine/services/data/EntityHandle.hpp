#pragma once
#include <cstdint>
#include <functional>

namespace services {

    // Opaque handle for entities - presentation layer uses this instead of entt::entity
    // This decouples the UI from the ECS implementation
    struct EntityHandle {
        uint64_t id = 0;

        bool isValid() const { return id != 0; }

        bool operator==(const EntityHandle& other) const { return id == other.id; }
        bool operator!=(const EntityHandle& other) const { return id != other.id; }
        bool operator<(const EntityHandle& other) const { return id < other.id; }

        struct Hash {
            size_t operator()(const EntityHandle& handle) const {
                return std::hash<uint64_t>{}(handle.id);
            }
        };

        // Create an invalid handle
        static EntityHandle invalid() { return EntityHandle{ 0 }; }
    };

    // Component type identifiers - decouples from actual component types
    enum class ComponentTypeId : uint32_t {
        None = 0,
        Transform,
        Camera,
        Name,
        Parent,
        Children,
        WorldTransform,
        IBL,
        Mesh,
        Light,
        Material,
        // Add more as needed
    };

}
