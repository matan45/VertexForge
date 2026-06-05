#pragma once
#include <functional>

namespace services
{
    // Strongly-typed identifier for preview instances.
    // Used as map keys to track multiple preview windows (Material, Mesh editors).
    // Callers typically pass 'this' pointer converted to this type.
    struct PreviewInstanceId
    {
        std::uintptr_t value = 0;

        PreviewInstanceId() = default;

        explicit PreviewInstanceId(void* ptr)
            : value(reinterpret_cast<std::uintptr_t>(ptr))
        {
        }

        explicit PreviewInstanceId(std::uintptr_t val)
            : value(val)
        {
        }

        bool operator==(const PreviewInstanceId& other) const = default;

        bool isValid() const { return value != 0; }

        // For debugging/logging
        std::uintptr_t raw() const { return value; }
    };
}

// Hash specialization for use in std::unordered_map
template <>
struct std::hash<services::PreviewInstanceId>
{
    std::size_t operator()(const services::PreviewInstanceId& id) const noexcept
    {
        return std::hash<std::uintptr_t>{}(id.value);
    }
};
