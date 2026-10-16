#include <doctest.h>

#include "tools/RenderSettingsToolSupport.hpp"

#include <asset/AssetDatabase.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <types/RenderSettings.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

// VK-1655: render_settings_set accepts exactly the ranges in the render-settings tables
// (RenderSettingsToolSupport), so every value an agent can set must come back unchanged from scene_save +
// scene_load; otherwise render_settings_get after the reload reports a value the agent never set. Each
// table is driven to all-minimum and all-maximum through its own FieldDesc::set, saved and reloaded through
// the public SceneSerialization API, and every field must come back identical.
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

    struct ScratchProject
    {
        fs::path root = fs::temp_directory_path() / "vf_mcp_render_settings_ranges";

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
    const env::FieldDesc<T>* fieldNamed(const env::SectionTable<T>& table, std::string_view key)
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

    // The bound in the canonical form FieldDesc::set takes: floats as numbers, integers as integers, a vec3
    // as three equal components, enums by canonical name (the first is the minimum, the last the maximum),
    // bools false / true and strings "" / a sample path (the loader stores any string).
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
        case env::FieldKind::String:
            return bound == Bound::Min ? std::string() : std::string("luts/") + std::string(field.key) + ".cube";
        }
        return nullptr;
    }

    template <typename T>
    T boundSection(const env::SectionTable<T>& table, Bound bound)
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

    // Every render-settings table at the bound, stored the way writeSections stores it.
    types::RenderSettings boundSettings(Bound bound)
    {
        types::RenderSettings settings = types::RenderSettings::createDefault();
        settings.postProcess.enabled = boundSection(env::postProcessMasterTable(), bound).enabled;
        env::forEachPostProcessSection([&](std::string_view, const auto& table, auto member)
        {
            settings.postProcess.*member = boundSection(table, bound);
        });
        env::storeShadowQuality(settings.shadows, boundSection(env::shadowTable(), bound));
        settings.display.msaa = boundSection(env::displayTable(), bound).msaa;
        return settings;
    }

    struct Saved
    {
        types::RenderSettings reloaded;
        nlohmann::json renderSettings; // the saved .vfSettings' renderSettings
    };

    Saved saveAndReload(const ScratchProject& project, const types::RenderSettings& settings, const std::string& name)
    {
        scene::SceneGraphSystem source;
        source.setRenderSettings(settings);
        const fs::path scenePath = project.root / (name + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        Saved saved;
        const nlohmann::json file = readJson(project.root / (name + ".vfSettings"));
        REQUIRE(file.contains("renderSettings"));
        saved.renderSettings = file.at("renderSettings");

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        saved.reloaded = loaded.getRenderSettings();
        return saved;
    }

    template <typename T>
    void checkSameFields(const env::SectionTable<T>& table, const T& saved, const T& reloaded, Bound bound)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const nlohmann::json before = field.get(saved);
            const nlohmann::json after = field.get(reloaded);
            INFO(std::string(table.name) << "." << std::string(field.key) << " at its " << boundName(bound) << ": saved "
                 << before.dump() << ", reloaded " << after.dump());
            CHECK(after == before);
        }
    }

    // Every table key is in the saved section; with `exact`, the section holds nothing else either (the
    // tools own the whole section, so an unknown saved key would be a field they cannot report or set).
    template <typename T>
    void checkSavedKeys(const env::SectionTable<T>& table, const nlohmann::json& section, bool exact)
    {
        INFO("section " << std::string(table.name));
        REQUIRE(section.is_object());
        for (const env::FieldDesc<T>& field : table.fields)
        {
            INFO("table key " << std::string(field.key));
            CHECK(section.contains(std::string(field.key)));
        }
        if (!exact)
        {
            return;
        }
        for (auto it = section.begin(); it != section.end(); ++it)
        {
            INFO("saved key " << it.key());
            CHECK(fieldNamed(table, it.key()) != nullptr);
        }
    }

    // The keys exactly as render_settings_get reports them, applied by strictPatch to a default struct: no
    // key is rejected (the bounds are inclusive after float narrowing) and every key reads back the same.
    template <typename T>
    void checkAcceptedBack(const env::SectionTable<T>& table, const T& value)
    {
        const nlohmann::json patch = env::toJson(table, value);
        INFO("section " << std::string(table.name) << ", patch " << patch.dump());
        T accepted{};
        REQUIRE_NOTHROW(accepted = env::strictPatch(table, T{}, patch, std::string_view()));
        CHECK(env::toJson(table, accepted) == patch);
    }
}

TEST_CASE("mcp render settings ranges: every table at its minimum and maximum survives a scene save and reload")
{
    ScratchProject project;
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const types::RenderSettings settings = boundSettings(bound);
        const Saved saved = saveAndReload(project, settings, std::string("Bounds_") + boundName(bound));
        const types::RenderSettings& reloaded = saved.reloaded;

        checkSameFields(env::postProcessMasterTable(), env::PostProcessMasterSettings{settings.postProcess.enabled},
                        env::PostProcessMasterSettings{reloaded.postProcess.enabled}, bound);
        env::forEachPostProcessSection([&](std::string_view, const auto& table, auto member)
        {
            checkSameFields(table, settings.postProcess.*member, reloaded.postProcess.*member, bound);
        });
        checkSameFields(env::shadowTable(), env::shadowQualityOf(settings.shadows), env::shadowQualityOf(reloaded.shadows),
                        bound);
        checkSameFields(env::displayTable(), env::DisplayAntiAliasingSettings{settings.display.msaa},
                        env::DisplayAntiAliasingSettings{reloaded.display.msaa}, bound);
    }
}

TEST_CASE("mcp render settings ranges: the .vfSettings saves every table key at the path the tools use")
{
    ScratchProject project;
    const Saved saved = saveAndReload(project, boundSettings(Bound::Max), "Keys");
    const nlohmann::json& renderSettings = saved.renderSettings;

    REQUIRE(renderSettings.contains("postProcess"));
    const nlohmann::json& postProcess = renderSettings.at("postProcess");
    CHECK(postProcess.contains("enabled"));
    env::forEachPostProcessSection([&](std::string_view key, const auto& table, auto)
    {
        INFO("postProcess." << std::string(key));
        REQUIRE(postProcess.contains(std::string(key)));
        checkSavedKeys(table, postProcess.at(std::string(key)), true);
    });

    // The shadows and display sections hold more than the tools own (clipmap, VSM resolutions, present mode).
    REQUIRE(renderSettings.contains("shadows"));
    checkSavedKeys(env::shadowTable(), renderSettings.at("shadows"), false);
    REQUIRE(renderSettings.contains("display"));
    checkSavedKeys(env::displayTable(), renderSettings.at("display"), false);
}

TEST_CASE("mcp render settings ranges: every bound render_settings_get reports is accepted back")
{
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        checkAcceptedBack(env::postProcessMasterTable(), boundSection(env::postProcessMasterTable(), bound));
        env::forEachPostProcessSection([&](std::string_view, const auto& table, auto)
        {
            checkAcceptedBack(table, boundSection(table, bound));
        });
        checkAcceptedBack(env::shadowTable(), boundSection(env::shadowTable(), bound));
        checkAcceptedBack(env::displayTable(), boundSection(env::displayTable(), bound));
    }
}
