// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "CloudAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/CloudEvents.hpp"

namespace core::api
{
    namespace
    {
        template<typename Mutator>
        value::Value modifyCloud(events::EventDispatcher& dispatcher, Mutator&& mutator)
        {
            auto settings = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
            mutator(settings);
            events::cloud::ApplyCloudSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
            return value::Value(std::monostate{});
        }
    }

    void CloudAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // ── Enable / Disable ──

        interpreter->registerNativeFunction("_native_cloud_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::cloud::GetCloudEnabledQuery{}));
            }});

        interpreter->registerNativeFunction("_native_cloud_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::cloud::SetCloudEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // ── Cloud Layer ──

        interpreter->registerNativeFunction("_native_cloud_setMinAltitude",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudMinAltitude = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setMaxAltitude",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudMaxAltitude = extractFloat(args[0]); });
            }});

        // ── Color ──

        interpreter->registerNativeFunction("_native_cloud_setColorTint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudColorTint = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            }});

        interpreter->registerNativeFunction("_native_cloud_getColorTint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return makeVec3Array(s.cloudColorTint);
            }});

        // ── Density & Coverage ──

        interpreter->registerNativeFunction("_native_cloud_setDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.globalDensity = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_getDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return value::Value(static_cast<double>(s.globalDensity));
            }});

        interpreter->registerNativeFunction("_native_cloud_setCoverage",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.globalCoverage = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_getCoverage",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return value::Value(static_cast<double>(s.globalCoverage));
            }});

        interpreter->registerNativeFunction("_native_cloud_setCloudType",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudType = extractFloat(args[0]); });
            }});

        // ── Noise Shaping ──

        interpreter->registerNativeFunction("_native_cloud_setShapeScale",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.shapeScale = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setDetailScale",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.detailScale = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setErosionStrength",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.erosionStrength = extractFloat(args[0]); });
            }});

        // ── Wind ──

        interpreter->registerNativeFunction("_native_cloud_setWindSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.windSpeed = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setWindDirection",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.windDirectionDeg = extractFloat(args[0]); });
            }});

        // ── Lighting ──

        interpreter->registerNativeFunction("_native_cloud_setLightAbsorption",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.lightAbsorption = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setAmbientIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.ambientIntensity = extractFloat(args[0]); });
            }});

        // ── Silver Lining ──

        interpreter->registerNativeFunction("_native_cloud_setSilverLiningIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.silverLiningIntensity = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setSilverLiningSpread",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.silverLiningSpread = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setMultiScatterBoost",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.multiScatterBoost = extractFloat(args[0]); });
            }});

        // ── Performance ──

        interpreter->registerNativeFunction("_native_cloud_setTemporalBlend",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.temporalBlendFactor = extractFloat(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_cloud_setMaxMarchSteps",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.maxMarchSteps = static_cast<uint32_t>(extractInt64(args[0])); });
            }});
    }
}
