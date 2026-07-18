// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "GraphicsAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/RenderEvents.hpp"
#include "../../../services/events/render/RuntimeRenderSettingsApply.hpp"
#include "../../../services/events/project/ApplicationEvents.hpp"
#include "../../../services/events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../../services/events/vfx/VFXRuntimeEvents.hpp"
#include "../../../services/events/animation/AnimationBudgetEvents.hpp"
#include "../../../services/events/save/ConfigEvents.hpp"
#include "types/RenderSettings.hpp"
#include "types/RenderSettingsConfig.hpp"

namespace core::api
{
    namespace
    {
        // Persisted config keys (config.json, next to the game's saves/).
        constexpr const char* KEY_PRESET  = "gfx.preset";
        constexpr const char* KEY_PRESENT = "gfx.presentMode";
        constexpr const char* KEY_MSAA    = "gfx.msaa";

        int64_t cfgGetInt(const char* key, int64_t def)
        {
            events::save::GetConfigIntQuery q;
            q.key = key;
            q.defaultValue = def;
            return events::EventDispatcher::instance().query(q);
        }

        void cfgSetInt(const char* key, int64_t value)
        {
            events::save::SetConfigIntCommand cmd;
            cmd.key = key;
            cmd.value = value;
            events::EventDispatcher::instance().execute(cmd);
        }

        // Dispatch the runtime-applicable subset of a RenderSettings: the shadow /
        // culling / distance / terrain / VT / dynamic-resolution bundle (ApplyShadow-
        // SettingsCommand), the present-mode + MSAA swapchain rebuild (ApplyDisplay-
        // SettingsNotification), and the VFX / animation LOD budgets. This mirrors
        // RenderConfigWindow::applySettings() MINUS the scene-graph write and the
        // post-process command — config is the runtime source of truth, and post-
        // process / atmosphere / cloud must not be reset to preset defaults (that
        // would clobber the scene-authored look).
        void applyRuntimeRenderSettings(const types::RenderSettings& s)
        {
            services::render::applyRuntimeRenderSettings(events::EventDispatcher::instance(), s);
        }

        // Current active RenderSettings from the scene graph (runtime-registered via
        // ScenePersistenceService). Used as the fallback for the getters when the
        // player has not persisted an explicit choice yet.
        types::RenderSettings currentRenderSettings()
        {
            return events::EventDispatcher::instance().query(events::scene::GetRenderSettingsQuery{});
        }
    }

    void GraphicsAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // Graphics.applyPreset(preset) — apply Low/Medium/High/Ultra now and persist it.
        // Present-mode + MSAA are preserved from their own persisted values so switching
        // preset does not stomp the player's VSync/anti-aliasing choice.
        interpreter->registerNativeFunction("_native_graphics_applyPreset",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                if (args.empty()) return value::Value(std::monostate{});
                int preset = static_cast<int>(extractInt64(args[0], "Graphics.applyPreset"));
                int present = static_cast<int>(cfgGetInt(KEY_PRESENT, static_cast<int>(types::PresentMode::Mailbox)));
                int msaa = static_cast<int>(cfgGetInt(KEY_MSAA, static_cast<int>(types::MsaaSamples::Off)));
                types::RenderSettings s = types::buildFromConfig(preset, present, msaa);
                applyRuntimeRenderSettings(s);
                cfgSetInt(KEY_PRESET, static_cast<int64_t>(static_cast<int>(s.activePreset)));
                return value::Value(std::monostate{});
            }});

        // Graphics.setPresentMode(mode) — 0=Fifo(VSync) 1=Mailbox 2=Immediate.
        interpreter->registerNativeFunction("_native_graphics_setPresentMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                if (args.empty()) return value::Value(std::monostate{});
                types::PresentMode mode = types::clampPresentMode(static_cast<int>(extractInt64(args[0], "Graphics.setPresentMode")));
                cfgSetInt(KEY_PRESENT, static_cast<int64_t>(static_cast<int>(mode)));
                events::application::ApplyDisplaySettingsNotification notif;
                notif.presentMode = mode;
                notif.msaa = types::clampMsaa(static_cast<int>(cfgGetInt(KEY_MSAA, static_cast<int>(types::MsaaSamples::Off))));
                events::EventDispatcher::instance().publish(notif);
                return value::Value(std::monostate{});
            }});

        // Graphics.setMsaa(samples) — 0=Off 1=2x 2=4x 3=8x (clamped to device support downstream).
        interpreter->registerNativeFunction("_native_graphics_setMsaa",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                if (args.empty()) return value::Value(std::monostate{});
                types::MsaaSamples msaa = types::clampMsaa(static_cast<int>(extractInt64(args[0], "Graphics.setMsaa")));
                cfgSetInt(KEY_MSAA, static_cast<int64_t>(static_cast<int>(msaa)));
                events::application::ApplyDisplaySettingsNotification notif;
                notif.presentMode = types::clampPresentMode(static_cast<int>(cfgGetInt(KEY_PRESENT, static_cast<int>(types::PresentMode::Mailbox))));
                notif.msaa = msaa;
                events::EventDispatcher::instance().publish(notif);
                return value::Value(std::monostate{});
            }});

        // Getters return the persisted value; if the player never chose one, they fall
        // back to the currently-active scene-baked value so a menu opens on the truth.
        interpreter->registerNativeFunction("_native_graphics_getPreset",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                int64_t v = cfgGetInt(KEY_PRESET, -1);
                if (v < 0) v = static_cast<int64_t>(static_cast<int>(currentRenderSettings().activePreset));
                return value::Value(v);
            }});

        interpreter->registerNativeFunction("_native_graphics_getPresentMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                int64_t v = cfgGetInt(KEY_PRESENT, -1);
                if (v < 0) v = static_cast<int64_t>(static_cast<int>(currentRenderSettings().display.presentMode));
                return value::Value(v);
            }});

        interpreter->registerNativeFunction("_native_graphics_getMsaa",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value {
                int64_t v = cfgGetInt(KEY_MSAA, -1);
                if (v < 0) v = static_cast<int64_t>(static_cast<int>(currentRenderSettings().display.msaa));
                return value::Value(v);
            }});
    }
}
