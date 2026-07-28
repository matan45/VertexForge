#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include "../asset/AssetRef.hpp"
#include "UIComponents.hpp"

namespace components
{
    struct DirectionalLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float lightSize{1.0f};
        bool showGizmo = false;
    };

    struct PointLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float radius{10.0f};
        float lightSize{0.1f};
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct SpotLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float innerAngle{30.0f};
        float outerAngle{45.0f};
        float range{20.0f};
        float lightSize{0.1f};
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct ShadowOverrideComponent
    {
        float depthBias = -1.0f;
        float slopeBias = -1.0f;
        float normalBias = -1.0f;
        uint32_t maxPages = 0;
        bool softShadows = false;
        bool hasSoftShadowOverride = false;
    };

    enum class FogVolumeShape : uint8_t
    {
        Box = 0,
        Sphere = 1,
        Cylinder = 2
    };

    enum class FogVolumeBlendMode : uint8_t
    {
        Additive = 0,
        Subtractive = 1
    };

    struct FogVolumeComponent
    {
        FogVolumeShape shape = FogVolumeShape::Box;
        glm::vec3 halfExtents{5.0f};
        float density = 0.5f;
        glm::vec3 albedo{0.8f, 0.85f, 0.9f};
        glm::vec3 emission{0.0f};
        float edgeFalloff = 0.5f;
        FogVolumeBlendMode blendMode = FogVolumeBlendMode::Additive;
        bool showGizmo = false;
    };

    // VK-1577 — local reflection probe / environment volume.
    //
    // Captures the surrounding scene into a small cubemap and overrides the GLOBAL IBL *specular*
    // term inside its bounds, parallax-corrected against those bounds and cross-faded to the global
    // environment over `blendDistance`. Diffuse ambient is deliberately untouched — it stays on the
    // global IBL + DDGI probes, which already cover diffuse GI.
    //
    // The probe captures from its entity's world position; move the entity to move the capture point.
    enum class ReflectionProbeShape : uint8_t
    {
        Box = 0,
        Sphere = 1
    };

    struct ReflectionProbeComponent
    {
        ReflectionProbeShape shape = ReflectionProbeShape::Box;
        // Box: per-axis half-size. Sphere: radius is taken from .x (y/z ignored).
        glm::vec3 halfExtents{5.0f};
        // World units of smooth falloff measured INWARD from the bounds surface. The probe reaches
        // full strength at `blendDistance` inside the bounds and fades to the global IBL at the edge.
        float blendDistance = 1.0f;
        float intensity = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        // Higher priority wins where probes overlap; ties break toward the smaller volume.
        int32_t priority = 0;
        // Capture with shadows. Off by default: render-texture views sample the MAIN camera's VSM
        // clipmap, which is centered for the primary view and misprojects (see RenderTextureViewPort).
        bool captureShadows = false;
        // Defaults ON, unlike the sibling volume/light components. A probe has no mesh and no
        // billboard icon, so the bounds gizmo is its ONLY affordance in the viewport — without it a
        // newly added probe would be invisible and unplaceable.
        bool showGizmo = true;
        // Set to request a (re)bake; cleared by the renderer once the probe has been captured.
        bool dirty = true;
    };

    struct TextComponent
    {
        asset::AssetRef fontRef;
        std::string text = "Hello World";
        float fontSize = 32.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        // VK-1637: the layout box, in the same layout pixels as fontSize. maxWidth is the
        // horizontal alignment box AND - when wordWrap is on - the word-wrap width.
        // 0 = no box: no wrap, no ellipsis, and horizontal alignment falls back to aligning
        // lines against the widest line.
        float maxWidth = 0.0f;
        // VK-1637: the vertical half of the box. There is no UIRect out here, so the
        // component carries its own. 0 = no box, which forces VerticalAlignment::Top.
        // Nothing wraps or truncates against it - it only positions the block.
        float rectHeight = 0.0f;
        FontStyle fontStyle = FontStyle::Normal;
        // VK-1637: mirrors UILabelComponent. Both are no-ops without the matching box
        // dimension above.
        HorizontalAlignment horizontalAlignment = HorizontalAlignment::Left;
        VerticalAlignment verticalAlignment = VerticalAlignment::Top;
        // VK-1637: Overflow and Ellipsis only. This path renders through the 3D text
        // pipeline, which issues no scissor, and a scissor is the only implementation of
        // Clip for UI text (UITextPipeline). Clip is accepted and round-trips losslessly -
        // the enum is shared with UILabel - but renders as Overflow.
        TextOverflow overflow = TextOverflow::Overflow;
        // VK-1637: false = maxWidth still boxes alignment and ellipsis but does not wrap.
        // Defaults true, which is exactly the historical "maxWidth wraps" behaviour.
        bool wordWrap = true;
        // VK-1635: outline / drop shadow / glow. Off by default; see TextEffects.hpp.
        TextEffectSettings effects;
    };

}
