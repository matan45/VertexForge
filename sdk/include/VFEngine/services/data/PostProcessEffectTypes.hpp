#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace plugin {

    struct PostProcessEffectHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    // Ordering of a plugin post-process effect relative to the engine's built-in
    // effects, expressed on the same integer priority scale PostProcessEffect
    // uses (ascending == earlier in the chain). Built-in tone mapping is priority
    // 100, so BeforeTonemap (95) runs on the linear HDR scene color and
    // AfterTonemap (130) runs on the display-referred (post-tonemap) result.
    // For finer control set PostProcessEffectDesc::customPriority (a non-zero
    // value overrides the enum).
    enum class PostProcessOrder : uint32_t {
        BeforeTonemap = 0,
        AfterTonemap  = 1
    };

    inline constexpr uint32_t POSTPROCESS_PRIORITY_BEFORE_TONEMAP = 95;
    inline constexpr uint32_t POSTPROCESS_PRIORITY_AFTER_TONEMAP  = 130;

    // Maximum analytic push-constant params, in bytes. The engine pushes these to
    // the fragment stage at offset 0; 64 stays within the Vulkan-guaranteed
    // 128-byte minimum.
    inline constexpr uint32_t MAX_POSTPROCESS_PARAMS_SIZE = 64;

    // Description of a plugin full-screen post-process effect. The engine compiles
    // the fragment GLSL (prepending its own fullscreen-triangle vertex stage),
    // owns the scene-color sampler, the ping-pong targets, every layout
    // transition, and the chain ordering. No Vulkan object crosses the DLL
    // boundary — the plugin only ever sees the returned handle.
    //
    // fragmentGlsl is a complete fragment shader (with its own #version) that
    // declares:
    //   layout(location = 0) in  vec2 texCoord;
    //   layout(location = 0) out vec4 outColor;
    //   layout(set = 0, binding = 0) uniform sampler2D inputTexture;  // scene color in
    //   layout(push_constant) uniform PC { ... } pc;                  // paramsSize bytes
    // A BeforeTonemap effect samples linear HDR (unbounded); an AfterTonemap
    // effect samples the display-referred result (roughly [0,1], post-ACES).
    struct PostProcessEffectDesc {
        std::string fragmentGlsl;
        PostProcessOrder order = PostProcessOrder::AfterTonemap;
        uint32_t customPriority = 0;   // 0 = derive from `order`; else explicit
        uint32_t paramsSize = 0;       // user bytes, <= MAX_POSTPROCESS_PARAMS_SIZE
        bool startEnabled = true;
        std::string debugName;
    };

    // Resolve the chain priority an effect should sort at. Pure; CPU-testable.
    inline uint32_t postProcessOrderToPriority(const PostProcessEffectDesc& desc) {
        if (desc.customPriority != 0) return desc.customPriority;
        return desc.order == PostProcessOrder::BeforeTonemap
                   ? POSTPROCESS_PRIORITY_BEFORE_TONEMAP
                   : POSTPROCESS_PRIORITY_AFTER_TONEMAP;
    }

    // Reject malformed descriptors before any GPU work. Pure; CPU-testable, and
    // also the real engine-side entry gate.
    inline bool validatePostProcessEffectDesc(const PostProcessEffectDesc& desc) {
        if (desc.fragmentGlsl.empty()) return false;
        if (desc.paramsSize > MAX_POSTPROCESS_PARAMS_SIZE) return false;
        return true;
    }

}
