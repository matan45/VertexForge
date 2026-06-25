#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace editor::preview
{
    enum class BackgroundMode : uint8_t
    {
        SolidColor,
        Gradient
    };

    enum class LightingMode : uint8_t
    {
        Default,
        ThreePoint
    };

    struct PreviewEnvironment
    {
        BackgroundMode backgroundMode = BackgroundMode::SolidColor;
        glm::vec4 backgroundColor{ 0.15f, 0.15f, 0.15f, 1.0f };
        glm::vec4 gradientTopColor{ 0.165f, 0.184f, 0.271f, 1.0f };
        glm::vec4 gradientBottomColor{ 0.106f, 0.118f, 0.169f, 1.0f };
        bool showGrid = true;
        LightingMode lightingMode = LightingMode::Default;
        float lightingIntensity = 1.0f;

        // VK-1433 Phase 1 — prefab rig debug overlays (only the prefab preview window surfaces
        // these; other preview windows leave them at their defaults).
        bool showSkeleton = false;
        bool showSockets = false;
        bool showIKTargets = false;

        // VK-1433 Phase 1c — currently-selected socket for the highlight halo (part, socket index
        // within that part); -1/-1 = none. The prefab preview window populates these from its live
        // selection only while a socket tab is active; maps to
        // PreviewEnvironmentParams::highlightedSocket{Part,Index}.
        int highlightedSocketPart = -1;
        int highlightedSocketIndex = -1;

        // VK-1433 Phase 3 — prefab rig debug shading selection (the prefab window's "Shading:"
        // dropdown). 0=Lit 1=Clay 2=Normals 3=UVs 4=Albedo-unlit 5=Wireframe. Only the prefab
        // preview window surfaces this; it maps to PreviewEnvironmentParams::shadingMode.
        uint8_t shadingMode = 0;

        // VK-1433 Phase 3 — key-light direction for three-point lighting (spherical, radians).
        float lightAzimuth = 0.6f;
        float lightElevation = 0.6f;
    };
}
