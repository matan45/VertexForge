#pragma once
#include <cstdint>
#include <functional>

namespace services {

    // Opaque handle for entities - presentation layer uses this instead of entt::entity
    // This decouples the UI from the ECS implementation
    struct EntityHandle {
        // Use max uint64_t as invalid sentinel since entt uses 0 as a valid entity ID
        static constexpr uint64_t INVALID_ID = ~0ULL;

        uint64_t id = INVALID_ID;

        bool isValid() const { return id != INVALID_ID; }

        bool operator==(const EntityHandle& other) const { return id == other.id; }
        bool operator!=(const EntityHandle& other) const { return id != other.id; }
        bool operator<(const EntityHandle& other) const { return id < other.id; }

        struct Hash {
            size_t operator()(const EntityHandle& handle) const {
                return std::hash<uint64_t>{}(handle.id);
            }
        };

        // Create an invalid handle
        static EntityHandle invalid() { return EntityHandle{ INVALID_ID }; }
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
        DirectionalLight,
        PointLight,
        SpotLight,
        Material,
        Billboard,
        AudioSource2D,
        AudioSource3D,
        Script,
        Collider,
        RigidBody,
        Animator,
        VFX,
        Text,
        UICanvas,
        UIRect,
        UIImage,
        UIScroll,
        UILayoutGroup,
        UILabel,
        UIButton,
        UITextInput,
        UICheckbox,
        UIDropdown,
        UITabs,
    };

}
