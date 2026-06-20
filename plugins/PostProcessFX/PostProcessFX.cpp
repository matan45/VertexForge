#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "imguiHandler/ImguiWindow.hpp"
#include <imgui.h>
#include <memory>
#include <string>

// VK-1409 sample: two plugin custom post-process effects exercising both ends of
// the built-in tone mapping.
//
//   * BeforeTonemap — exposure + contrast applied to the *linear HDR* scene color
//     (priority 95, runs before the engine's ACES tonemap at 100).
//   * AfterTonemap  — a saturation / lift / gain color grade applied to the
//     *display-referred* result (priority 130).
//
// The plugin supplies only fragment GLSL + a 16-byte params block per effect; the
// engine owns the fullscreen pass, scene-color sampler, ping-pong and barriers.

namespace
{
    // 16-byte push-constant blocks (a single vec4 each — std430 scalar rules).
    struct ExposureParams { float exposure; float contrast; float pad0; float pad1; };
    struct GradeParams    { float saturation; float lift; float gain; float pad0; };

    constexpr const char* kExposureFrag = R"GLSL(
#version 460 core
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(push_constant) uniform PC { vec4 p; } pc; // x=exposure, y=contrast
void main()
{
    vec3 c = texture(inputTexture, texCoord).rgb;   // linear HDR (pre-tonemap)
    c *= pc.p.x;                                    // exposure
    c = (c - 0.18) * pc.p.y + 0.18;                 // contrast about mid-grey
    outColor = vec4(max(c, vec3(0.0)), 1.0);
}
)GLSL";

    constexpr const char* kGradeFrag = R"GLSL(
#version 460 core
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(push_constant) uniform PC { vec4 p; } pc; // x=saturation, y=lift, z=gain
void main()
{
    vec3 c = texture(inputTexture, texCoord).rgb;   // display-referred (post-tonemap)
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, pc.p.x);                    // saturation
    c = c * pc.p.z + pc.p.y;                         // gain, then lift
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

    struct FxSettings
    {
        bool  exposureEnabled = true;
        float exposure = 1.0f;
        float contrast = 1.0f;

        bool  gradeEnabled = true;
        float saturation = 1.15f;
        float lift = 0.0f;
        float gain = 1.0f;
    };
}

// Editor control panel. draw() runs inside the engine's ImGui frame; the context
// was pointed at the engine's during onInitialize.
class PostProcessFXWindow : public controllers::imguiHandler::ImguiWindow
{
private:
    std::shared_ptr<FxSettings> settings;

public:
    explicit PostProcessFXWindow(std::shared_ptr<FxSettings> settings)
        : settings(std::move(settings)) {}

    void draw() override
    {
        if (!ImGui::Begin("Post-Process FX"))
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Before Tonemap (linear HDR)");
        ImGui::Checkbox("Enable##exp", &settings->exposureEnabled);
        ImGui::SliderFloat("Exposure", &settings->exposure, 0.0f, 4.0f);
        ImGui::SliderFloat("Contrast", &settings->contrast, 0.5f, 2.0f);

        ImGui::SeparatorText("After Tonemap (color grade)");
        ImGui::Checkbox("Enable##grade", &settings->gradeEnabled);
        ImGui::SliderFloat("Saturation", &settings->saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("Lift", &settings->lift, -0.25f, 0.25f);
        ImGui::SliderFloat("Gain", &settings->gain, 0.0f, 2.0f);

        ImGui::End();
    }
};

class PostProcessFX : public plugin::IPlugin
{
private:
    plugin::PluginContext* ctx = nullptr;
    std::shared_ptr<FxSettings> settings = std::make_shared<FxSettings>();

    plugin::PostProcessEffectHandle exposureFx;
    plugin::PostProcessEffectHandle gradeFx;
    bool prevExposureEnabled = true;
    bool prevGradeEnabled = true;

public:
    plugin::PluginInfo getInfo() const override
    {
        return {"PostProcessFX", "VertexForge",
                "Sample plugin custom post-process effects (exposure + color grade)", 1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        if (!ctx->hasCapability(std::string(plugin::capability::graphics)))
        {
            ctx->logError("[PostProcessFX] graphics capability not available");
            return false;
        }

        plugin::PostProcessEffectDesc exp;
        exp.fragmentGlsl = kExposureFrag;
        exp.order = plugin::PostProcessOrder::BeforeTonemap;
        exp.paramsSize = sizeof(ExposureParams);
        exp.startEnabled = settings->exposureEnabled;
        exp.debugName = "PPFX_Exposure";
        exposureFx = ctx->registerPostProcessEffect(exp);

        plugin::PostProcessEffectDesc grade;
        grade.fragmentGlsl = kGradeFrag;
        grade.order = plugin::PostProcessOrder::AfterTonemap;
        grade.paramsSize = sizeof(GradeParams);
        grade.startEnabled = settings->gradeEnabled;
        grade.debugName = "PPFX_Grade";
        gradeFx = ctx->registerPostProcessEffect(grade);

        if (ctx->hasCapability(std::string(plugin::capability::editor)))
        {
            ImGui::SetCurrentContext(ctx->getImGuiContext());
            ctx->registerEditorWindow(std::make_shared<PostProcessFXWindow>(settings), "Post-Process FX");
        }

        ctx->logInfo("[PostProcessFX] effects registered - Plugins > Post-Process FX to tune");
        return true;
    }

    void onUpdate(float /*deltaTime*/) override
    {
        if (exposureFx.isValid())
        {
            if (settings->exposureEnabled != prevExposureEnabled)
            {
                ctx->setPostProcessEffectEnabled(exposureFx, settings->exposureEnabled);
                prevExposureEnabled = settings->exposureEnabled;
            }
            ExposureParams p{settings->exposure, settings->contrast, 0.0f, 0.0f};
            ctx->updatePostProcessEffectParams(exposureFx, &p, sizeof(p));
        }

        if (gradeFx.isValid())
        {
            if (settings->gradeEnabled != prevGradeEnabled)
            {
                ctx->setPostProcessEffectEnabled(gradeFx, settings->gradeEnabled);
                prevGradeEnabled = settings->gradeEnabled;
            }
            GradeParams p{settings->saturation, settings->lift, settings->gain, 0.0f};
            ctx->updatePostProcessEffectParams(gradeFx, &p, sizeof(p));
        }
    }

    void onShutdown() override
    {
        if (ctx)
        {
            if (exposureFx.isValid()) ctx->unregisterPostProcessEffect(exposureFx);
            if (gradeFx.isValid())    ctx->unregisterPostProcessEffect(gradeFx);
        }
    }
};

VF_IMPLEMENT_PLUGIN(PostProcessFX)
