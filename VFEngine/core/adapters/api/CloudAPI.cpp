// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

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
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                return value::Value(dispatcher.query(events::cloud::GetCloudEnabledQuery{}));
            });

        interpreter->registerNativeFunction("_native_cloud_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::cloud::SetCloudEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ── Cloud Layer ──

        interpreter->registerNativeFunction("_native_cloud_setMinAltitude",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudMinAltitude = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setMaxAltitude",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudMaxAltitude = extractFloat(args[0]); });
            });

        // ── Color ──

        interpreter->registerNativeFunction("_native_cloud_setColorTint",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudColorTint = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])); });
            });

        interpreter->registerNativeFunction("_native_cloud_getColorTint",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return makeVec3Array(s.cloudColorTint);
            });

        // ── Density & Coverage ──

        interpreter->registerNativeFunction("_native_cloud_setDensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.globalDensity = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_getDensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return value::Value(static_cast<double>(s.globalDensity));
            });

        interpreter->registerNativeFunction("_native_cloud_setCoverage",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.globalCoverage = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_getCoverage",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::cloud::GetCloudSettingsQuery{});
                return value::Value(static_cast<double>(s.globalCoverage));
            });

        interpreter->registerNativeFunction("_native_cloud_setCloudType",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.cloudType = extractFloat(args[0]); });
            });

        // ── Noise Shaping ──

        interpreter->registerNativeFunction("_native_cloud_setShapeScale",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.shapeScale = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setDetailScale",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.detailScale = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setErosionStrength",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.erosionStrength = extractFloat(args[0]); });
            });

        // ── Wind ──

        interpreter->registerNativeFunction("_native_cloud_setWindSpeed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.windSpeed = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setWindDirection",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.windDirectionDeg = extractFloat(args[0]); });
            });

        // ── Lighting ──

        interpreter->registerNativeFunction("_native_cloud_setLightAbsorption",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.lightAbsorption = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setAmbientIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.ambientIntensity = extractFloat(args[0]); });
            });

        // ── Performance ──

        interpreter->registerNativeFunction("_native_cloud_setTemporalBlend",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.temporalBlendFactor = extractFloat(args[0]); });
            });

        interpreter->registerNativeFunction("_native_cloud_setMaxMarchSteps",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifyCloud(dispatcher, [&](render::cloud::CloudSettings& s)
                { s.maxMarchSteps = static_cast<uint32_t>(extractInt64(args[0])); });
            });
    }
}
