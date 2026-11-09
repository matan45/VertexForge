#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "protocol/ArgReader.hpp"
#include "tools/SettingsTable.hpp"
#include "tools/WaterToolSupport.hpp"

#include <asset/AssetDatabase.hpp>
#include <components/Components.hpp>
#include <data/DTOs.hpp>
#include <data/EntityHandle.hpp>
#include <data/OceanData.hpp>
#include <impl/scene/OceanService.hpp>
#include <scene/Entity.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// VK-1658: the water tools accept exactly the ranges in their tables (WaterToolSupport) and report the scene's
// ocean through OceanService::stateFromComponent, so every value an agent can set must come back unchanged from
// scene_save + scene_load; otherwise water_get after the reload reports a value the agent never set. Every look,
// band and physics field is driven to its minimum and maximum through the tables' strictPatch (as
// water_set_params / ocean_set_params set them), together with the ocean's own level, weather response, sea state
// and visibility, applied to an OceanComponent with OceanService::applyState, saved and reloaded through the
// public SceneSerialization API and read back with stateFromComponent. The scene loader clamps nothing, so every
// field must come back identical. Water bodies go the same way through their WaterBodyComponent and transform.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;
    namespace watertool = mcp::tools::watertool;
    using json = nlohmann::json;

    enum class Bound
    {
        Min,
        Max
    };

    const char* boundName(Bound bound)
    {
        return bound == Bound::Min ? "min" : "max";
    }

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    // A fresh directory and asset database, removed again when the test ends.
    struct ScratchProject
    {
        fs::path root = fs::temp_directory_path() / "vf_mcp_water_ranges";

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

    // A SceneGraphSystem leaves its root in the shared registry when it goes, so each scene clears what a case put
    // on it (the root's components and its children): an OceanComponent left behind would be one more ocean for a
    // later test that looks for one.
    struct SceneCleanup
    {
        scene::SceneGraphSystem& scene;

        ~SceneCleanup()
        {
            scene.clearScene();
        }
    };

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json value;
        file >> value;
        return value;
    }

    std::string bandPath(std::size_t band)
    {
        return "bands." + std::string(watertool::bandNames[band]);
    }

    // The bound in the form FieldDesc::get reports it, which is also the form strictPatch takes back.
    template <typename T>
    json boundValue(const env::FieldDesc<T>& field, Bound bound)
    {
        const double limit = bound == Bound::Min ? field.min : field.max;
        switch (field.kind)
        {
        case env::FieldKind::Bool:
            return bound == Bound::Max;
        case env::FieldKind::Float:
            return env::floatJson(static_cast<float>(limit));
        case env::FieldKind::Int:
        case env::FieldKind::UInt:
            return static_cast<int64_t>(limit);
        case env::FieldKind::Vec3:
        {
            const json component = env::floatJson(static_cast<float>(limit));
            return json::array({component, component, component});
        }
        case env::FieldKind::Enum:
        case env::FieldKind::String:
            break;
        }
        throw std::logic_error("the water tables have no enum or string fields");
    }

    // Every field of `table` at the bound.
    template <typename T>
    json boundPatch(const env::SectionTable<T>& table, Bound bound)
    {
        json patch = json::object();
        for (const env::FieldDesc<T>& field : table.fields)
        {
            patch[std::string(field.key)] = boundValue(field, bound);
        }
        return patch;
    }

    // Every look, band and physics field at the bound, set through the tables' strictPatch the way
    // water_set_params and ocean_set_params set them, plus the ocean's own values at theirs: the level (+/-1e6),
    // the weather response (0-2), the sea state (0-12), weatherDriven and the visibility. What the tools do not
    // expose (density, the deep alpha, maxVisibleDepth, ssrDebugView, gravity) keeps the component's defaults.
    services::OceanStateData boundState(Bound bound)
    {
        services::OceanStateData state = services::OceanService::stateFromComponent(components::OceanComponent{});
        for (const env::SectionTable<watertool::Visual>& section : watertool::lookSections())
        {
            state.visual = env::strictPatch(section, state.visual, boundPatch(section, bound), section.name);
        }
        for (std::size_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            state.bands[i] = env::strictPatch(watertool::bandTable(), state.bands[i], boundPatch(watertool::bandTable(), bound),
                                              bandPath(i));
        }
        state.physics = env::strictPatch(watertool::physicsTable(), state.physics,
                                         boundPatch(watertool::physicsTable(), bound), "physics");

        const bool max = bound == Bound::Max;
        state.waterHeight = static_cast<float>(max ? watertool::maxLevel : -watertool::maxLevel);
        state.weatherResponse = static_cast<float>(max ? watertool::maxWeatherResponse : 0.0);
        state.currentBeaufort = static_cast<float>(max ? watertool::maxBeaufort : 0.0);
        state.weatherDriven = max;
        state.isActive = max;
        return state;
    }

    struct SavedOcean
    {
        services::OceanStateData reloaded;
        json oceanBlock; // the saved scene's root "ocean" component
    };

    // scene_save + scene_load of a root OceanComponent through the public serialization API, the round trip of
    // test_water_body.cpp ("a water body survives a scene round trip").
    SavedOcean saveAndReload(const ScratchProject& project, const services::OceanStateData& state, const std::string& name)
    {
        scene::SceneGraphSystem source;
        SceneCleanup sourceCleanup{source};
        services::OceanService::applyState(source.GetRoot().addOrReplaceComponent<components::OceanComponent>(), state);

        const fs::path scenePath = project.root / (name + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        SavedOcean saved;
        const json sceneJson = readJson(scenePath);
        REQUIRE(sceneJson.contains("root"));
        REQUIRE(sceneJson.at("root").contains("components"));
        REQUIRE(sceneJson.at("root").at("components").contains("ocean"));
        saved.oceanBlock = sceneJson.at("root").at("components").at("ocean");

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        SceneCleanup loadedCleanup{loaded};
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::OceanComponent>());
        saved.reloaded =
            services::OceanService::stateFromComponent(loaded.GetRoot().getComponent<components::OceanComponent>());
        return saved;
    }

    // The tables really put every field at the bound.
    template <typename T>
    void checkAtBound(const env::SectionTable<T>& table, const T& value, Bound bound, const std::string& where)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            INFO(where << "." << std::string(field.key) << " at its " << boundName(bound));
            CHECK(field.get(value) == boundValue(field, bound));
        }
    }

    // Every field reads the same, in the form the tools report it.
    template <typename T>
    void checkSameFields(const env::SectionTable<T>& table, const T& saved, const T& reloaded, const std::string& where)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const json before = field.get(saved);
            const json after = field.get(reloaded);
            INFO(where << "." << std::string(field.key) << ": saved " << before.dump() << ", reloaded " << after.dump());
            CHECK(after == before);
        }
    }

    // A saved scene value in the form FieldDesc::get reports it: floats narrowed to float, a vec3 or vec4 as its rgb.
    json reportedForm(env::FieldKind kind, const json& saved)
    {
        switch (kind)
        {
        case env::FieldKind::Bool:
            REQUIRE(saved.is_boolean());
            return saved;
        case env::FieldKind::Float:
            REQUIRE(saved.is_number());
            return env::floatJson(saved.get<float>());
        case env::FieldKind::Int:
        case env::FieldKind::UInt:
            REQUIRE(saved.is_number_integer());
            return saved.get<int64_t>();
        case env::FieldKind::Vec3:
            REQUIRE(saved.is_array());
            REQUIRE(saved.size() >= 3);
            return env::vec3Json(glm::vec3(saved.at(0).get<float>(), saved.at(1).get<float>(), saved.at(2).get<float>()));
        case env::FieldKind::Enum:
        case env::FieldKind::String:
            break;
        }
        throw std::logic_error("the water tables have no enum or string fields");
    }

    // The saved ocean keys no look or physics table holds: the ocean's own values water_get reports (level,
    // visibility, weather, sea state), the bands (checked band by band), and what the tools deliberately keep out
    // (VK-1658 plan, "Not exposed"). A field added to the serializer must go into a table or onto this list.
    const std::set<std::string>& otherOceanKeys()
    {
        static const std::set<std::string> keys{"waterHeight", "isActive",    "weatherDriven",   "weatherResponse",
                                                "currentBeaufort", "oceanBands", "density", "maxVisibleDepth",
                                                "ssrDebugView", "oceanGravity"};
        return keys;
    }

    // Every value a ranges entry names (both bounds, each listed value, false and true) is accepted by the table's
    // strictPatch and reads back the same; one step past a bound is refused.
    template <typename T>
    void checkRangesAccepted(const env::SectionTable<T>& table, const json& entries, const std::string& where)
    {
        REQUIRE(entries.is_object());
        CHECK(entries.size() == table.fields.size());
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const std::string key(field.key);
            INFO(where << "." << key);
            REQUIRE(entries.contains(key));
            const json& entry = entries.at(key);
            const std::string type = entry.at("type").get<std::string>();

            std::vector<json> accepted;
            std::vector<json> refused;
            if (type == "boolean")
            {
                accepted = {false, true};
                refused = {1, "true"};
            }
            else if (entry.contains("values"))
            {
                const json& values = entry.at("values");
                REQUIRE(values.size() >= 2);
                for (const json& value : values)
                {
                    accepted.push_back(value);
                }
                const int64_t first = values.front().get<int64_t>();
                const int64_t second = values.at(1).get<int64_t>();
                refused = {first - 1, values.back().get<int64_t>() + 1, (first + second) / 2};
            }
            else if (type == "integer")
            {
                const int64_t min = entry.at("min").get<int64_t>();
                const int64_t max = entry.at("max").get<int64_t>();
                accepted = {min, max};
                refused = {min - 1, max + 1};
            }
            else
            {
                REQUIRE((type == "number" || type == "vec3"));
                const float min = entry.at("min").get<float>();
                const float max = entry.at("max").get<float>();
                // The next float past each bound (a clearly negative value below a 0 minimum: the next float
                // below 0 is a denormal).
                const float below = min == 0.0f ? -1.0e-6f : std::nextafter(min, -std::numeric_limits<float>::infinity());
                const float above = std::nextafter(max, std::numeric_limits<float>::infinity());
                const bool vec3 = type == "vec3";
                const auto value = [vec3](float number) -> json
                {
                    const json component = env::floatJson(number);
                    return vec3 ? json::array({component, component, component}) : component;
                };
                accepted = {value(min), value(max)};
                refused = {value(below), value(above)};
            }

            for (const json& value : accepted)
            {
                INFO("accepted " << value.dump());
                T result{};
                REQUIRE_NOTHROW(result = env::strictPatch(table, T{}, json{{key, value}}, where));
                CHECK(field.get(result) == value);
            }
            for (const json& value : refused)
            {
                INFO("refused " << value.dump());
                CHECK_THROWS_AS(env::strictPatch(table, T{}, json{{key, value}}, where), mcp::ArgError);
            }
        }
    }

    services::TransformData transformOf(const components::TransformComponent& transform)
    {
        services::TransformData out;
        out.position = transform.position;
        out.rotation = transform.rotation;
        out.scale = transform.scale;
        return out;
    }

    services::WaterBodyComponentData bodyDataOf(const components::WaterBodyComponent& body)
    {
        services::WaterBodyComponentData out;
        out.type = static_cast<uint32_t>(body.type);
        out.waterHeight = body.waterHeight;
        out.halfExtents = body.halfExtents;
        out.depth = body.depth;
        out.bandMask = body.bandMask;
        out.physicsEnabled = body.physicsEnabled;
        out.isActive = body.isActive;
        return out;
    }
}

TEST_CASE("mcp water ranges: every look, band, physics and ocean value at its minimum and maximum survives a scene save and reload")
{
    ScratchProject project;
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const services::OceanStateData state = boundState(bound);
        for (const auto& section : watertool::lookSections())
        {
            checkAtBound(section, state.visual, bound, std::string(section.name));
        }
        for (std::size_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            checkAtBound(watertool::bandTable(), state.bands[i], bound, bandPath(i));
        }
        checkAtBound(watertool::physicsTable(), state.physics, bound, "physics");

        const SavedOcean saved = saveAndReload(project, state, std::string("Ocean_") + boundName(bound));

        for (const auto& section : watertool::lookSections())
        {
            checkSameFields(section, state.visual, saved.reloaded.visual, std::string(section.name));
        }
        for (std::size_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            checkSameFields(watertool::bandTable(), state.bands[i], saved.reloaded.bands[i], bandPath(i));
        }
        checkSameFields(watertool::physicsTable(), state.physics, saved.reloaded.physics, "physics");

        // The ocean's own values, then everything the component holds, the unexposed fields included.
        CHECK(saved.reloaded.waterHeight == state.waterHeight);
        CHECK(saved.reloaded.isActive == state.isActive);
        CHECK(saved.reloaded.weatherDriven == state.weatherDriven);
        CHECK(saved.reloaded.weatherResponse == state.weatherResponse);
        CHECK(saved.reloaded.currentBeaufort == state.currentBeaufort);
        CHECK(saved.reloaded == state);

        // What water_get reports reads the same after the reload.
        CHECK(watertool::lookJson(saved.reloaded.visual) == watertool::lookJson(state.visual));
        CHECK(watertool::oceanParamsJson(saved.reloaded) == watertool::oceanParamsJson(state));
    }
}

TEST_CASE("mcp water ranges: every table key is a key of the saved ocean block, opacity the shallow colour's alpha")
{
    ScratchProject project;
    const services::OceanStateData state = boundState(Bound::Max);
    const SavedOcean saved = saveAndReload(project, state, "Keys");
    const json& block = saved.oceanBlock;
    REQUIRE(block.is_object());

    std::set<std::string> tableKeys;
    for (const auto& section : watertool::lookSections())
    {
        for (const auto& field : section.fields)
        {
            const std::string key(field.key);
            INFO(std::string(section.name) << "." << key);
            if (key == "opacity")
            {
                REQUIRE(block.contains("shallowColor"));
                REQUIRE(block.at("shallowColor").is_array());
                REQUIRE(block.at("shallowColor").size() == 4);
                CHECK(env::floatJson(block.at("shallowColor").at(3).get<float>()) == field.get(state.visual));
                continue;
            }
            tableKeys.insert(key);
            REQUIRE(block.contains(key));
            CHECK(reportedForm(field.kind, block.at(key)) == field.get(state.visual));
        }
    }
    for (const auto& field : watertool::physicsTable().fields)
    {
        const std::string key(field.key);
        INFO("physics." << key);
        tableKeys.insert(key);
        REQUIRE(block.contains(key));
        CHECK(reportedForm(field.kind, block.at(key)) == field.get(state.physics));
    }

    REQUIRE(block.contains("oceanBands"));
    const json& bands = block.at("oceanBands");
    REQUIRE(bands.is_array());
    REQUIRE(bands.size() == services::MAX_OCEAN_BANDS);
    for (std::size_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
    {
        const json& band = bands.at(i);
        REQUIRE(band.is_object());
        for (const auto& field : watertool::bandTable().fields)
        {
            const std::string key(field.key);
            INFO(bandPath(i) << "." << key);
            REQUIRE(band.contains(key));
            CHECK(reportedForm(field.kind, band.at(key)) == field.get(state.bands[i]));
        }
        // ... and a saved band holds nothing the band table does not.
        for (auto it = band.begin(); it != band.end(); ++it)
        {
            INFO(bandPath(i) << " saved key " << it.key());
            CHECK(env::detail::findField(watertool::bandTable(), it.key()) != nullptr);
        }
    }

    // The ocean's own values water_get reports: level, visible, weatherDriven, weatherResponse, seaState.
    CHECK(block.at("waterHeight").get<float>() == state.waterHeight);
    CHECK(block.at("isActive") == state.isActive);
    CHECK(block.at("weatherDriven") == state.weatherDriven);
    CHECK(block.at("weatherResponse").get<float>() == state.weatherResponse);
    CHECK(block.at("currentBeaufort").get<float>() == state.currentBeaufort);

    for (auto it = block.begin(); it != block.end(); ++it)
    {
        INFO("saved key " << it.key());
        CHECK((tableKeys.count(it.key()) == 1 || otherOceanKeys().count(it.key()) == 1));
    }
}

TEST_CASE("mcp water ranges: every bound water_get {ranges:true} reports is accepted by its table, one step past it is not")
{
    const json ranges = watertool::rangesJson();

    const json& look = ranges.at("look");
    REQUIRE(look.size() == watertool::lookSections().size());
    for (const auto& section : watertool::lookSections())
    {
        const std::string name(section.name);
        REQUIRE(look.contains(name));
        checkRangesAccepted(section, look.at(name), name);
    }
    checkRangesAccepted(watertool::bandTable(), ranges.at("bands"), "bands.swell");
    checkRangesAccepted(watertool::physicsTable(), ranges.at("physics"), "physics");

    // A band's windDirection takes any value a sea state writes (the swell's direction +/- 15, unwrapped); the
    // windDirection argument of ocean_set_params, which turns the swell band, keeps +/-720.
    CHECK(ranges.at("bands").at("windDirection").at("max").get<double>() == watertool::maxBandWindDirection);
    CHECK(ranges.at("ocean").at("windDirection").at("max").get<double>() == watertool::maxWindDirection);
}

TEST_CASE("mcp water ranges: a water body at every bound water_get reports survives a scene save and reload")
{
    ScratchProject project;
    const json body = watertool::rangesJson().at("body");
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const char* side = boundName(bound);
        const bool max = bound == Bound::Max;
        const json& center = body.at("center").at(side);
        const json& halfExtent = body.at("halfExtents").at(side);
        const json& level = body.at("level").at(side);
        const json& depth = body.at("depth").at(side);
        const json waves = max ? body.at("waves").at("values") : json::array();
        const std::string name = max ? "Pool" : "Lake";

        // water_create_body's layout: the level in the transform, the component's own offset 0.
        scene::SceneGraphSystem source;
        SceneCleanup sourceCleanup{source};
        scene::Entity entity(name);
        source.addChild(source.GetRoot(), entity);
        entity.getComponent<components::TransformComponent>().position =
            glm::vec3(center.get<float>(), level.get<float>(), center.get<float>());
        components::WaterBodyComponent& component = entity.addOrReplaceComponent<components::WaterBodyComponent>();
        component.type = max ? components::WaterBodyType::Pool : components::WaterBodyType::Lake;
        component.waterHeight = 0.0f;
        component.halfExtents = glm::vec2(halfExtent.get<float>());
        component.depth = depth.get<float>();
        component.bandMask = watertool::bandMaskFromNames(waves, "waves");
        component.physicsEnabled = max;
        component.isActive = max;
        const json reported = watertool::bodyJson(handleOf(7), name, transformOf(entity.getComponent<components::TransformComponent>()),
                                                  bodyDataOf(component));

        const fs::path scenePath = project.root / ("Body_" + std::string(side) + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        SceneCleanup loadedCleanup{loaded};
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        const std::vector<scene::Entity> children = loaded.GetRoot().getChildren();
        REQUIRE(children.size() == 1);
        const scene::Entity& reloaded = children.front();
        CHECK(reloaded.getName() == name);
        REQUIRE(reloaded.hasComponent<components::WaterBodyComponent>());
        const components::TransformComponent& transform = reloaded.getComponent<components::TransformComponent>();
        const components::WaterBodyComponent& reloadedBody = reloaded.getComponent<components::WaterBodyComponent>();
        CHECK(transform.position == entity.getComponent<components::TransformComponent>().position);
        CHECK(watertool::sameBodyData(bodyDataOf(reloadedBody), bodyDataOf(component)));

        // water_get reports the same body after the reload, at exactly the bounds it lists.
        const json after = watertool::bodyJson(handleOf(7), reloaded.getName(), transformOf(transform), bodyDataOf(reloadedBody));
        CHECK(after == reported);
        CHECK(after.at("center") == json::array({center, center}));
        CHECK(after.at("halfExtents") == json::array({halfExtent, halfExtent}));
        CHECK(after.at("level") == level);
        CHECK(after.at("depth") == depth);
        CHECK(after.at("type") == (max ? "pool" : "lake"));
        CHECK(after.at("waves") == waves);
        CHECK(after.at("physicsEnabled") == max);
        CHECK(after.at("active") == max);
    }
}
