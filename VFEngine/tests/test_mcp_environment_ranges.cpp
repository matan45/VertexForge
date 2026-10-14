#include <doctest.h>

#include "tools/EnvironmentToolSupport.hpp"

#include <asset/AssetDatabase.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <types/RenderSettings.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

// VK-1654: the environment tools accept exactly the ranges in their field tables (EnvironmentToolSupport),
// so every value an agent can set must come back unchanged from scene_save + scene_load; otherwise a *_get
// after the reload reports a value the agent never set. Each table is driven to all-minimum and
// all-maximum through its own FieldDesc::set, saved and reloaded through the public SceneSerialization
// API, and every field must come back identical: each range sits inside the scene loader's clamp.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;

    enum class Bound
    {
        Min,
        Max
    };

    const char* boundName(Bound bound)
    {
        return bound == Bound::Min ? "min" : "max";
    }

    // A fresh directory and asset database, removed again when the test ends.
    struct ScratchProject
    {
        fs::path root = fs::temp_directory_path() / "vf_mcp_environment_ranges";

        ScratchProject()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            asset::AssetDatabase::instance().clear();
        }

        ~ScratchProject()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
        }
    };

    nlohmann::json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        nlohmann::json json;
        file >> json;
        return json;
    }

    template <typename T>
    const env::FieldDesc<T>* findField(const env::SectionTable<T>& table, std::string_view key)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.key == key)
            {
                return &field;
            }
        }
        return nullptr;
    }

    // The bound in the canonical form FieldDesc::set takes: floats as numbers, integers as integers, a
    // vec3 as three equal components, enums by canonical name (the first is the minimum, the last the
    // maximum) and bools false / true.
    template <typename T>
    nlohmann::json boundValue(const env::FieldDesc<T>& field, Bound bound)
    {
        const double limit = bound == Bound::Min ? field.min : field.max;
        switch (field.kind)
        {
        case env::FieldKind::Bool:
            return bound == Bound::Max;
        case env::FieldKind::Float:
            return limit;
        case env::FieldKind::Int:
        case env::FieldKind::UInt:
            return static_cast<int64_t>(limit);
        case env::FieldKind::Vec3:
            return nlohmann::json::array({limit, limit, limit});
        case env::FieldKind::Enum:
            return std::string(bound == Bound::Min ? field.enumNames.front() : field.enumNames.back());
        }
        return nullptr;
    }

    // Every persisted field of the table at the bound (the live-only GI debug views keep their default).
    template <typename T>
    T boundSettings(const env::SectionTable<T>& table, Bound bound)
    {
        T value{};
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.persisted)
            {
                field.set(value, boundValue(field, bound));
            }
        }
        return value;
    }

    struct Environment
    {
        env::AtmosphereSettings atmosphere;
        env::CloudSettings clouds;
        env::FogSettings fog;
        env::GISettings gi;
    };

    Environment boundEnvironment(Bound bound)
    {
        Environment environment{boundSettings(env::atmosphereTable(), bound), boundSettings(env::cloudTable(), bound),
                                boundSettings(env::fogTable(), bound), boundSettings(env::giTable(), bound)};

        // planetRadius and atmosphereRadius share one range and the atmosphere must be the larger radius, so
        // they cannot sit on the same bound. All-min keeps the smallest planet under the largest atmosphere;
        // all-max keeps the largest atmosphere over the largest planet below it. (atmosphereRadius at its
        // minimum would need a planet below the planet's own minimum, so no valid setting reaches it.)
        const env::FieldDesc<env::AtmosphereSettings>* planet = findField(env::atmosphereTable(), "planetRadius");
        const env::FieldDesc<env::AtmosphereSettings>* outer = findField(env::atmosphereTable(), "atmosphereRadius");
        REQUIRE(planet != nullptr);
        REQUIRE(outer != nullptr);
        if (bound == Bound::Min)
        {
            outer->set(environment.atmosphere, boundValue(*outer, Bound::Max));
        }
        else
        {
            const float belowOuter = std::nextafter(static_cast<float>(outer->max), 0.0f);
            planet->set(environment.atmosphere, std::min(planet->max, static_cast<double>(belowOuter)));
        }

        // The cloud layer's base stays below its top at both bounds without an adjustment.
        const bool atmosphereChecked = env::atmosphereTable().crossCheck != nullptr;
        const bool cloudsChecked = env::cloudTable().crossCheck != nullptr;
        REQUIRE(atmosphereChecked);
        REQUIRE(cloudsChecked);
        const std::string atmosphereError = env::atmosphereTable().crossCheck(environment.atmosphere);
        const std::string cloudError = env::cloudTable().crossCheck(environment.clouds);
        INFO(boundName(bound) << ": " << atmosphereError << cloudError);
        REQUIRE(atmosphereError.empty());
        REQUIRE(cloudError.empty());
        return environment;
    }

    struct SavedEnvironment
    {
        Environment reloaded;
        nlohmann::json settingsFile; // the saved .vfSettings
    };

    // scene_save + scene_load through the public serialization API, like test_scene_settings.
    SavedEnvironment saveAndReload(const ScratchProject& project, const Environment& environment, const std::string& name)
    {
        types::RenderSettings settings = types::RenderSettings::createDefault();
        settings.atmosphere = environment.atmosphere;
        settings.cloud = environment.clouds;
        settings.postProcess.volumetricFog = environment.fog;
        settings.gi = environment.gi;

        scene::SceneGraphSystem source;
        source.setRenderSettings(settings);
        const fs::path scenePath = project.root / (name + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        SavedEnvironment saved;
        saved.settingsFile = readJson(project.root / (name + ".vfSettings"));

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        const types::RenderSettings& reloaded = loaded.getRenderSettings();
        saved.reloaded = Environment{reloaded.atmosphere, reloaded.cloud, reloaded.postProcess.volumetricFog, reloaded.gi};
        return saved;
    }

    // Every persisted field reads the same, in the form the tools report it.
    template <typename T>
    void checkSameFields(const env::SectionTable<T>& table, const T& saved, const T& reloaded, Bound bound)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted)
            {
                continue;
            }
            const nlohmann::json before = field.get(saved);
            const nlohmann::json after = field.get(reloaded);
            INFO(std::string(table.name) << "." << std::string(field.key) << " at its " << boundName(bound) << ": saved "
                 << before.dump() << ", reloaded " << after.dump());
            CHECK(after == before);
        }
    }

    // The saved section holds every persisted table key, and nothing the table cannot report and set.
    template <typename T>
    void checkSavedKeys(const env::SectionTable<T>& table, const nlohmann::json& section)
    {
        INFO("section " << std::string(table.name));
        REQUIRE(section.is_object());
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.persisted)
            {
                INFO("table key " << std::string(field.key));
                CHECK(section.contains(std::string(field.key)));
            }
        }
        for (auto it = section.begin(); it != section.end(); ++it)
        {
            const env::FieldDesc<T>* field = findField(table, it.key());
            INFO("saved key " << it.key());
            CHECK((field != nullptr && field->persisted));
        }
    }

    // The settable keys exactly as a *_get reports them, applied by strictPatch to a default struct: no key
    // is rejected (the bounds are inclusive after float narrowing) and every key reads back the same.
    template <typename T>
    void checkAcceptedBack(const env::SectionTable<T>& table, const T& value)
    {
        nlohmann::json patch = nlohmann::json::object();
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.persisted && field.setHint.empty())
            {
                patch[std::string(field.key)] = field.get(value);
            }
        }
        INFO("section " << std::string(table.name) << ", patch " << patch.dump());

        T accepted{};
        REQUIRE_NOTHROW(accepted = env::strictPatch(table, T{}, patch, std::string_view()));
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.persisted && field.setHint.empty())
            {
                INFO("key " << std::string(field.key));
                CHECK(field.get(accepted) == field.get(value));
            }
        }

        // The whole reported object, read-only keys included, patched onto the same settings: the read-only
        // keys echo their current value, so nothing is refused and nothing changes.
        T echoed{};
        REQUIRE_NOTHROW(echoed = env::strictPatch(table, value, env::toJson(table, value), std::string_view()));
        CHECK(env::toJson(table, echoed) == env::toJson(table, value));
    }
}

TEST_CASE("mcp environment ranges: every table at its minimum and maximum survives a scene save and reload")
{
    ScratchProject project;
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const Environment environment = boundEnvironment(bound);
        const SavedEnvironment saved = saveAndReload(project, environment, std::string("Bounds_") + boundName(bound));

        checkSameFields(env::atmosphereTable(), environment.atmosphere, saved.reloaded.atmosphere, bound);
        checkSameFields(env::cloudTable(), environment.clouds, saved.reloaded.clouds, bound);
        checkSameFields(env::fogTable(), environment.fog, saved.reloaded.fog, bound);
        checkSameFields(env::giTable(), environment.gi, saved.reloaded.gi, bound);
    }
}

TEST_CASE("mcp environment ranges: the .vfSettings saves every table key, and only keys the tables report")
{
    ScratchProject project;
    const SavedEnvironment saved = saveAndReload(project, boundEnvironment(Bound::Max), "Keys");

    REQUIRE(saved.settingsFile.contains("renderSettings"));
    const nlohmann::json& renderSettings = saved.settingsFile.at("renderSettings");
    REQUIRE(renderSettings.contains("atmosphere"));
    REQUIRE(renderSettings.contains("cloud"));
    REQUIRE(renderSettings.contains("postProcess"));
    REQUIRE(renderSettings.at("postProcess").contains("volumetricFog"));
    REQUIRE(renderSettings.contains("gi"));

    checkSavedKeys(env::atmosphereTable(), renderSettings.at("atmosphere"));
    checkSavedKeys(env::cloudTable(), renderSettings.at("cloud"));
    checkSavedKeys(env::fogTable(), renderSettings.at("postProcess").at("volumetricFog"));
    checkSavedKeys(env::giTable(), renderSettings.at("gi"));
}

TEST_CASE("mcp environment ranges: every bound a *_get reports is accepted back")
{
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const Environment environment = boundEnvironment(bound);
        checkAcceptedBack(env::atmosphereTable(), environment.atmosphere);
        checkAcceptedBack(env::cloudTable(), environment.clouds);
        checkAcceptedBack(env::fogTable(), environment.fog);
        checkAcceptedBack(env::giTable(), environment.gi);
    }
}
