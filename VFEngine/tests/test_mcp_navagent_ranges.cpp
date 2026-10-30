#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "protocol/ArgReader.hpp"
#include "tools/NavigationToolSupport.hpp"
#include "tools/SettingsTable.hpp"

#include <asset/AssetDatabase.hpp>
#include <components/Components.hpp>
#include <data/DTOs.hpp>
#include <scene/Entity.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// VK-1660: component_set NavmeshAgent accepts exactly the ranges of navtool::agentTable() (the inspector's), so every
// value an agent can set must come back unchanged from scene_save + scene_load; otherwise component_get after the
// reload reports a value the agent never set. Every field is driven to its minimum and maximum through the table's
// strictPatch (as the binding applies it), written to a NavmeshAgentComponent the way NavmeshAgentManager does,
// saved and reloaded through the public SceneSerialization API and read back the same way. The loader clamps only
// avoidanceQuality (to at most 3, the table's maximum), so every field must come back identical. Values a scene
// stored outside the ranges pass back unchanged (dropUnchanged), and one step past a bound is refused.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;
    namespace navtool = mcp::tools::navtool;
    using json = nlohmann::json;
    using AgentData = services::NavmeshAgentData;

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
        fs::path root = fs::temp_directory_path() / "vf_mcp_navagent_ranges";

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
    // on it: an agent left behind would show up in a later test that lists the agents.
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

    // The bound in the form FieldDesc::get reports it, which is also the form strictPatch takes back.
    json boundValue(const env::FieldDesc<AgentData>& field, Bound bound)
    {
        const double limit = bound == Bound::Min ? field.min : field.max;
        switch (field.kind)
        {
        case env::FieldKind::Bool:
            return bound == Bound::Max;
        case env::FieldKind::Float:
            return env::floatJson(static_cast<float>(limit));
        case env::FieldKind::Int:
            return static_cast<int64_t>(limit);
        default:
            break;
        }
        throw std::logic_error("the NavmeshAgent table has only bool, float and int fields");
    }

    AgentData boundData(Bound bound)
    {
        json patch = json::object();
        for (const env::FieldDesc<AgentData>& field : navtool::agentTable().fields)
        {
            patch[std::string(field.key)] = boundValue(field, bound);
        }
        return env::strictPatch(navtool::agentTable(), AgentData{}, patch, "NavmeshAgent");
    }

    // NavmeshAgentManager::setAgentData's write and getAgentData's read: the binding's way to the component.
    void storeData(components::NavmeshAgentComponent& agent, const AgentData& data)
    {
        agent.radius = data.radius;
        agent.height = data.height;
        agent.baseOffset = data.baseOffset;
        agent.maxSpeed = data.maxSpeed;
        agent.maxAcceleration = data.maxAcceleration;
        agent.turnSpeed = data.turnSpeed;
        agent.stoppingDistance = data.stoppingDistance;
        agent.avoidanceQuality = static_cast<uint8_t>(data.avoidanceQuality);
        agent.rootMotionDriven = data.rootMotionDriven;
        agent.rootMotionSpeedScale = data.rootMotionSpeedScale;
    }

    AgentData dataOf(const components::NavmeshAgentComponent& agent)
    {
        AgentData data;
        data.radius = agent.radius;
        data.height = agent.height;
        data.baseOffset = agent.baseOffset;
        data.maxSpeed = agent.maxSpeed;
        data.maxAcceleration = agent.maxAcceleration;
        data.turnSpeed = agent.turnSpeed;
        data.stoppingDistance = agent.stoppingDistance;
        data.avoidanceQuality = agent.avoidanceQuality;
        data.rootMotionDriven = agent.rootMotionDriven;
        data.rootMotionSpeedScale = agent.rootMotionSpeedScale;
        return data;
    }

    struct SavedAgent
    {
        AgentData reloaded;
        json block; // the saved scene's "navmeshAgent" component of the agent entity
    };

    // scene_save + scene_load of an entity with a NavmeshAgentComponent through the public serialization API.
    SavedAgent saveAndReload(const ScratchProject& project, const AgentData& data, const std::string& name)
    {
        scene::SceneGraphSystem source;
        SceneCleanup sourceCleanup{source};
        scene::Entity entity("Agent");
        source.addChild(source.GetRoot(), entity);
        storeData(entity.addOrReplaceComponent<components::NavmeshAgentComponent>(), data);

        const fs::path scenePath = project.root / (name + ".vfScene");
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        SavedAgent saved;
        const json sceneJson = readJson(scenePath);
        REQUIRE(sceneJson.contains("root"));
        REQUIRE(sceneJson.at("root").contains("children"));
        for (const json& child : sceneJson.at("root").at("children"))
        {
            if (child.value("name", std::string()) == "Agent" && child.contains("components") &&
                child.at("components").contains("navmeshAgent"))
            {
                saved.block = child.at("components").at("navmeshAgent");
            }
        }
        REQUIRE(saved.block.is_object());

        asset::AssetDatabase::instance().clear();
        scene::SceneGraphSystem loaded;
        SceneCleanup loadedCleanup{loaded};
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        const std::vector<scene::Entity> children = loaded.GetRoot().getChildren();
        REQUIRE(children.size() == 1);
        REQUIRE(children.front().hasComponent<components::NavmeshAgentComponent>());
        saved.reloaded = dataOf(children.front().getComponent<components::NavmeshAgentComponent>());
        return saved;
    }
}

TEST_CASE("mcp navagent ranges: every NavmeshAgent field at its minimum and maximum survives a scene save and reload")
{
    ScratchProject project;
    const env::SectionTable<AgentData>& table = navtool::agentTable();
    for (const Bound bound : {Bound::Min, Bound::Max})
    {
        CAPTURE(boundName(bound));
        const AgentData data = boundData(bound);

        // The table really put every field at the bound.
        for (const env::FieldDesc<AgentData>& field : table.fields)
        {
            INFO(std::string(field.key) << " at its " << boundName(bound));
            CHECK(field.get(data) == boundValue(field, bound));
        }

        const SavedAgent saved = saveAndReload(project, data, std::string("Agent_") + boundName(bound));
        for (const env::FieldDesc<AgentData>& field : table.fields)
        {
            const json before = field.get(data);
            const json after = field.get(saved.reloaded);
            INFO(std::string(field.key) << " at its " << boundName(bound) << ": saved " << before.dump() << ", reloaded "
                 << after.dump());
            CHECK(after == before);
        }
        // What component_get reports reads the same after the reload.
        CHECK(env::toJson(table, saved.reloaded) == env::toJson(table, data));
    }
}

TEST_CASE("mcp navagent ranges: the saved navmeshAgent block holds every table key")
{
    ScratchProject project;
    const SavedAgent saved = saveAndReload(project, boundData(Bound::Max), "Keys");
    for (const env::FieldDesc<AgentData>& field : navtool::agentTable().fields)
    {
        INFO("table key " << std::string(field.key));
        CHECK(saved.block.contains(std::string(field.key)));
    }
}

TEST_CASE("mcp navagent ranges: every reported bound is accepted back and one step past it is refused")
{
    const env::SectionTable<AgentData>& table = navtool::agentTable();
    const json ranges = env::rangesJson(table);
    REQUIRE(ranges.size() == table.fields.size());
    for (const env::FieldDesc<AgentData>& field : table.fields)
    {
        const std::string key(field.key);
        INFO("key " << key);
        REQUIRE(ranges.contains(key));
        const json& entry = ranges.at(key);
        const std::string type = entry.at("type").get<std::string>();

        std::vector<json> accepted;
        std::vector<json> refused;
        if (type == "boolean")
        {
            accepted = {false, true};
            refused = {1, "true"};
        }
        else if (type == "integer")
        {
            const int64_t min = entry.at("min").get<int64_t>();
            const int64_t max = entry.at("max").get<int64_t>();
            accepted = {min, max};
            refused = {min - 1, max + 1, 1.5};
        }
        else
        {
            REQUIRE(type == "number");
            const float min = entry.at("min").get<float>();
            const float max = entry.at("max").get<float>();
            // The next float past each bound (a clearly negative value below a 0 minimum: the next float below 0
            // is a denormal).
            const float below = min == 0.0f ? -1.0e-6f : std::nextafter(min, -std::numeric_limits<float>::infinity());
            const float above = std::nextafter(max, std::numeric_limits<float>::infinity());
            accepted = {env::floatJson(min), env::floatJson(max)};
            refused = {env::floatJson(below), env::floatJson(above), "1"};
        }

        for (const json& value : accepted)
        {
            INFO("accepted " << value.dump());
            AgentData result{};
            REQUIRE_NOTHROW(result = env::strictPatch(table, AgentData{}, json{{key, value}}, "NavmeshAgent"));
            CHECK(field.get(result) == value);
        }
        for (const json& value : refused)
        {
            INFO("refused " << value.dump());
            CHECK_THROWS_AS(env::strictPatch(table, AgentData{}, json{{key, value}}, "NavmeshAgent"), mcp::ArgError);
        }
    }
}

TEST_CASE("mcp navagent ranges: stored out-of-range values echo back unchanged")
{
    const env::SectionTable<AgentData>& table = navtool::agentTable();

    // What a scene file or a script may store: every float field outside its range, avoidanceQuality inside the
    // loader's clamp.
    AgentData stored;
    stored.radius = 7.0f;
    stored.height = 12.0f;
    stored.baseOffset = -8.0f;
    stored.maxSpeed = 80.0f;
    stored.maxAcceleration = 250.0f;
    stored.turnSpeed = 2000.0f;
    stored.stoppingDistance = 9.0f;
    stored.rootMotionSpeedScale = 10.0f;
    const json reported = env::toJson(table, stored);

    // The whole component_get object passes back: every key echoes its stored value, so nothing is written.
    const json echoed = env::dropUnchanged(table, stored, reported, "NavmeshAgent");
    CHECK(echoed.empty());
    AgentData written{};
    REQUIRE_NOTHROW(written = env::strictPatch(table, stored, echoed, "NavmeshAgent"));
    CHECK(env::toJson(table, written) == reported);

    // Changing one field keeps the others as stored; a new out-of-range value is still refused.
    const json changed = env::dropUnchanged(table, stored, json{{"radius", 7.0}, {"height", 3.0}}, "NavmeshAgent");
    CHECK(changed == json{{"height", 3.0}});
    REQUIRE_NOTHROW(written = env::strictPatch(table, stored, changed, "NavmeshAgent"));
    CHECK(written.radius == 7.0f);
    CHECK(written.height == 3.0f);
    CHECK(written.turnSpeed == 2000.0f);
    CHECK_THROWS_AS(env::strictPatch(table, stored,
                                     env::dropUnchanged(table, stored, json{{"radius", 6.0}}, "NavmeshAgent"),
                                     "NavmeshAgent"),
                    mcp::ArgError);
}
