#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <glm/glm.hpp>

namespace behaviortree
{
    // A persistent entity reference held by a per-entity blackboard start value (VK-1660).
    // Raw entity ids change on every scene load and Play->Stop, so the target is stored as its
    // UUIDComponent value plus its name. Resolution tries the UUID first and falls back to the first
    // entity with that name: prefab instantiation and undo re-creation mint new UUIDs (the socket
    // attachment precedent, SocketAttachmentUpdater).
    struct BlackboardEntityRef
    {
        uint64_t uuid = 0;
        std::string name;

        bool empty() const { return uuid == 0 && name.empty(); }
        bool operator==(const BlackboardEntityRef&) const = default;
    };

    // Alternative order matches BlackboardValueType / BlackboardValue (Float, Int, Bool, String, Vec3,
    // Entity), so index() converts directly.
    using BlackboardInitialData = std::variant<float, int32_t, bool, std::string, glm::vec3, BlackboardEntityRef>;

    static_assert(std::is_same_v<std::variant_alternative_t<0, BlackboardInitialData>, float>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, BlackboardInitialData>, int32_t>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, BlackboardInitialData>, bool>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, BlackboardInitialData>, std::string>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, BlackboardInitialData>, glm::vec3>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, BlackboardInitialData>, BlackboardEntityRef>);

    // One per-entity blackboard start value. Stored on BehaviorTreeComponent and applied when the tree
    // attaches at Edit->Play (BehaviorTreePlayModeHandler); a value whose key is missing from the tree,
    // or whose type differs from the key's, is skipped with a warning.
    struct BlackboardInitialValue
    {
        std::string key;
        BlackboardInitialData value = 0.0f;

        bool operator==(const BlackboardInitialValue&) const = default;
    };
}
