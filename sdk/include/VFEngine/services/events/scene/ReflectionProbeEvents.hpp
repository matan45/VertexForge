#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>

// VK-1577 — reflection probe component events. Mirrors FogVolumeEvents.hpp exactly, with one
// addition: BakeReflectionProbesCommand, which is a RENDERER request rather than a component edit.
namespace events::scene
{
    struct AddReflectionProbeComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AddReflectionProbeComponent"; }
    };

    struct RemoveReflectionProbeComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveReflectionProbeComponent"; }
    };

    struct SetReflectionProbeDataCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        services::ReflectionProbeData data;
        std::string_view getName() const override { return "SetReflectionProbeData"; }
    };

    struct HasReflectionProbeComponentQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasReflectionProbeComponent"; }
    };

    struct GetReflectionProbeDataQuery : ::events::IQuery<std::optional<services::ReflectionProbeData>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetReflectionProbeData"; }
    };

    // Marks probes as needing a capture. A default-constructed `entity` (i.e. an invalid handle)
    // means "every probe in the scene" — that is the form the scene-load path and the editor's
    // "Bake All" button use. The renderer picks the work up on subsequent frames; this command only
    // flags intent and returns immediately, so it never blocks the caller on GPU work.
    struct BakeReflectionProbesCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity{};
        std::string_view getName() const override { return "BakeReflectionProbes"; }
    };
}
