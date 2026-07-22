#pragma once
#include "../EventTypes.hpp"
#include "../../../utilities/meshbrush/MeshBrushTypes.hpp"
#include <vector>
#include <optional>
#include <glm/glm.hpp>

namespace events::meshBrush {

    // ============================================
    // Commands
    // ============================================

    struct SetMeshBrushModeActiveCommand : ICommand<> {
        bool active = false;

        std::string_view getName() const override { return "SetMeshBrushModeActive"; }
    };

    struct SetMeshBrushParamsCommand : ICommand<> {
        meshbrush::MeshBrushParams params;

        std::string_view getName() const override { return "SetMeshBrushParams"; }
    };

    struct SetMeshBrushPaletteCommand : ICommand<> {
        std::vector<meshbrush::MeshPaletteEntry> palette;

        std::string_view getName() const override { return "SetMeshBrushPalette"; }
    };

    struct SetMeshBrushModeCommand : ICommand<> {
        meshbrush::MeshBrushMode mode = meshbrush::MeshBrushMode::Paint;

        std::string_view getName() const override { return "SetMeshBrushMode"; }
    };

    struct SetMeshBrushSelectedEntryCommand : ICommand<> {
        int selectedIndex = -1; // -1 = all (weighted random)

        std::string_view getName() const override { return "SetMeshBrushSelectedEntry"; }
    };

    struct ApplyMeshBrushCommand : ICommand<> {
        glm::vec3 worldPosition{0.0f};
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        float deltaTime = 0.0f;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyMeshBrush"; }
    };

    struct FinalizeMeshBrushCommand : ICommand<> {
        std::string_view getName() const override { return "FinalizeMeshBrush"; }
    };

    // Internal service transaction used by mesh-brush undo/redo. Removals are
    // applied before respawns so a stable ID can be replaced atomically.
    struct ApplyMeshBrushInstanceDeltaCommand : ICommand<> {
        std::vector<uint64_t> removeIds;
        std::vector<meshbrush::MeshBrushInstanceSpec> respawnSpecs;

        std::string_view getName() const override { return "ApplyMeshBrushInstanceDelta"; }
    };

    // ============================================
    // Queries
    // ============================================

    struct IsMeshBrushModeActiveQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsMeshBrushModeActive"; }
    };

    struct GetMeshBrushParamsQuery : IQuery<meshbrush::MeshBrushParams> {
        std::string_view getName() const override { return "GetMeshBrushParams"; }
    };

    struct GetMeshBrushPaletteQuery : IQuery<std::vector<meshbrush::MeshPaletteEntry>> {
        std::string_view getName() const override { return "GetMeshBrushPalette"; }
    };

    struct GetMeshBrushModeQuery : IQuery<meshbrush::MeshBrushMode> {
        std::string_view getName() const override { return "GetMeshBrushMode"; }
    };

    // ============================================
    // Notifications
    // ============================================

    struct MeshBrushModeChangedNotification : INotification {
        bool isActive = false;

        std::string_view getName() const override { return "MeshBrushModeChanged"; }
    };

    struct MeshBrushParamsChangedNotification : INotification {
        meshbrush::MeshBrushParams params;

        std::string_view getName() const override { return "MeshBrushParamsChanged"; }
    };

    struct MeshBrushAppliedNotification : INotification {
        glm::vec3 position{0.0f};
        uint32_t count = 0;

        std::string_view getName() const override { return "MeshBrushApplied"; }
    };

}
