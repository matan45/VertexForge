#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "protocol/ArgReader.hpp"
#include "tools/ComponentBinding.hpp"
#include "tools/SettingsTable.hpp"
#include "tools/VolumetricToolSupport.hpp"

#include <asset/AssetDatabase.hpp>
#include <components/Components.hpp>
#include <data/DTOs.hpp>
#include <data/EntityHandle.hpp>
#include <data/VolumetricNavData.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <events/volumetric/VolumetricNavEvents.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// VK-1660: the VolumetricNavVolume / VolumetricAgent component bindings accept exactly the ranges of their tables
// (VolumetricToolSupport), so every value an agent can set must come back unchanged from scene_save + scene_load;
// otherwise component_get after the reload reports a value the agent never set. Every field is driven to its minimum
// and maximum through the tables' strictPatch (as component_set sets them), copied into the components the way
// VolumetricNavServiceImpl::setVolumeData / setAgentData copy them, saved and reloaded through the public
// SceneSerialization API and read back the way getVolumeData / getAgentData read them; the scene loader clamps
// nothing, so every field must come back identical. Then, through the bindings against a fake store with the
// engine's rules: one step past every bound is refused, and a value the engine stored outside a range passes back
// unchanged and is restored by an undo (ComponentBinding::restore), while one the engine refuses never is.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;
    namespace volumetrictool = mcp::tools::volumetrictool;
    using json = nlohmann::json;
    using VolumeData = services::VolumetricNavVolumeData;
    using AgentData = services::VolumetricAgentData;

    constexpr uint64_t kEntityId = 5;

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
        fs::path root = fs::temp_directory_path() / "vf_mcp_volumetric_ranges";

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
    // on it: a VolumetricNavVolumeComponent left behind would be one more volume for a later test that lists them.
    struct SceneCleanup
    {
        scene::SceneGraphSystem& scene;

        ~SceneCleanup()
        {
            scene.clearScene();
        }
    };

    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json value;
        file >> value;
        return value;
    }

    // ------------------------------------------------------------------
    // Bounds
    // ------------------------------------------------------------------

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
            return static_cast<int64_t>(limit);
        case env::FieldKind::Vec3:
        {
            const json component = env::floatJson(static_cast<float>(limit));
            return json::array({component, component, component});
        }
        default:
            break;
        }
        throw std::logic_error("the volumetric tables hold bool, float, int and vec3 fields only");
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

    // Values a patch must refuse: one step past each bound (a clearly negative value below a 0 minimum: the next
    // float below 0 is a denormal), a bool that is not a JSON boolean, and for connectivity a value inside the
    // integer range that is neither 6 nor 26.
    template <typename T>
    std::vector<json> outsideValues(const env::FieldDesc<T>& field)
    {
        switch (field.kind)
        {
        case env::FieldKind::Bool:
            return {json(1), json("true")};
        case env::FieldKind::Int:
        {
            const int64_t min = static_cast<int64_t>(field.min);
            const int64_t max = static_cast<int64_t>(field.max);
            return {json(min - 1), json(max + 1), json((min + max) / 2)};
        }
        case env::FieldKind::Float:
        case env::FieldKind::Vec3:
        {
            const float min = static_cast<float>(field.min);
            const float max = static_cast<float>(field.max);
            const float below = min == 0.0f ? -1.0e-6f : std::nextafter(min, -std::numeric_limits<float>::infinity());
            const float above = std::nextafter(max, std::numeric_limits<float>::infinity());
            const bool vec3 = field.kind == env::FieldKind::Vec3;
            const auto value = [vec3](float number) -> json
            {
                const json component = env::floatJson(number);
                return vec3 ? json::array({component, component, component}) : component;
            };
            return {value(below), value(above)};
        }
        default:
            break;
        }
        throw std::logic_error("the volumetric tables hold bool, float, int and vec3 fields only");
    }

    // The tables really put every field at the bound.
    template <typename T>
    void checkAtBound(const env::SectionTable<T>& table, const T& value, Bound bound)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            INFO(std::string(table.name) << "." << std::string(field.key) << " at its " << boundName(bound));
            CHECK(field.get(value) == boundValue(field, bound));
        }
    }

    // Every field reads the same, in the form the bindings report it.
    template <typename T>
    void checkSameFields(const env::SectionTable<T>& table, const T& saved, const T& reloaded)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const json before = field.get(saved);
            const json after = field.get(reloaded);
            INFO(std::string(table.name) << "." << std::string(field.key) << ": saved " << before.dump() << ", reloaded "
                                         << after.dump());
            CHECK(after == before);
        }
    }

    // A saved scene value in the form FieldDesc::get reports it: floats narrowed to float.
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
            REQUIRE(saved.is_number_integer());
            return saved.get<int64_t>();
        case env::FieldKind::Vec3:
            REQUIRE(saved.is_array());
            REQUIRE(saved.size() == 3);
            return env::vec3Json(glm::vec3(saved.at(0).get<float>(), saved.at(1).get<float>(), saved.at(2).get<float>()));
        default:
            break;
        }
        throw std::logic_error("the volumetric tables hold bool, float, int and vec3 fields only");
    }

    template <typename T>
    bool hasField(const env::SectionTable<T>& table, const std::string& key)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            if (field.key == key)
            {
                return true;
            }
        }
        return false;
    }

    // The scene saved every table key, each as the table value, and nothing a table does not hold but `others` (the
    // keys the bindings report read-only or not at all). A field added to the serializer must go into a table or
    // onto that list.
    template <typename T>
    void checkSaved(const env::SectionTable<T>& table, const T& value, const json& saved, const std::set<std::string>& others)
    {
        REQUIRE(saved.is_object());
        for (auto it = saved.begin(); it != saved.end(); ++it)
        {
            INFO(std::string(table.name) << ": saved key " << it.key());
            CHECK((hasField(table, it.key()) || others.count(it.key()) == 1));
        }
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const std::string key(field.key);
            INFO(std::string(table.name) << "." << key);
            REQUIRE(saved.contains(key));
            CHECK(reportedForm(field.kind, saved.at(key)) == field.get(value));
        }
    }

    // ------------------------------------------------------------------
    // DTO <-> component, as VolumetricNavServiceImpl copies the writable fields
    // ------------------------------------------------------------------

    void toComponent(const VolumeData& data, components::VolumetricNavVolumeComponent& volume)
    {
        volume.centerOffset = data.centerOffset;
        volume.halfExtents = data.halfExtents;
        volume.voxelSize = data.voxelSize;
        volume.connectivity = static_cast<uint8_t>(data.connectivity);
        volume.agentClearance = data.agentClearance;
        volume.includeTerrain = data.includeTerrain;
        volume.includeStaticMeshes = data.includeStaticMeshes;
        volume.includeColliders = data.includeColliders;
    }

    VolumeData fromComponent(const components::VolumetricNavVolumeComponent& volume)
    {
        VolumeData data;
        data.centerOffset = volume.centerOffset;
        data.halfExtents = volume.halfExtents;
        data.voxelSize = volume.voxelSize;
        data.connectivity = volume.connectivity;
        data.agentClearance = volume.agentClearance;
        data.includeTerrain = volume.includeTerrain;
        data.includeStaticMeshes = volume.includeStaticMeshes;
        data.includeColliders = volume.includeColliders;
        return data;
    }

    void toComponent(const AgentData& data, components::VolumetricAgentComponent& agent)
    {
        agent.radius = data.radius;
        agent.maxSpeed = data.maxSpeed;
        agent.maxAcceleration = data.maxAcceleration;
        agent.stoppingDistance = data.stoppingDistance;
        agent.turnSpeed = data.turnSpeed;
        agent.alignPitch = data.alignPitch;
    }

    AgentData fromComponent(const components::VolumetricAgentComponent& agent)
    {
        AgentData data;
        data.radius = agent.radius;
        data.maxSpeed = agent.maxSpeed;
        data.maxAcceleration = agent.maxAcceleration;
        data.stoppingDistance = agent.stoppingDistance;
        data.turnSpeed = agent.turnSpeed;
        data.alignPitch = agent.alignPitch;
        return data;
    }

    // ------------------------------------------------------------------
    // A fake store with the engine's rules
    // ------------------------------------------------------------------

    // VolumetricNavServiceImpl::setVolumeData's rules.
    bool volumeAccepted(const VolumeData& data)
    {
        const auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        return finite(data.centerOffset) && finite(data.halfExtents) && std::isfinite(data.voxelSize) &&
               std::isfinite(data.agentClearance) && data.halfExtents.x > 0.0f && data.halfExtents.y > 0.0f &&
               data.halfExtents.z > 0.0f && data.voxelSize > 0.0f && (data.connectivity == 6 || data.connectivity == 26) &&
               data.agentClearance >= 0.0f;
    }

    // VolumetricNavServiceImpl::setAgentData's rules.
    bool agentAccepted(const AgentData& data)
    {
        const float values[] = {data.radius, data.maxSpeed, data.maxAcceleration, data.stoppingDistance, data.turnSpeed};
        for (float value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return data.radius > 0.0f && data.maxSpeed >= 0.0f && data.maxAcceleration >= 0.0f &&
               data.stoppingDistance >= 0.0f && data.turnSpeed >= 0.0f;
    }

    // One entity with a VolumetricNavVolume and a VolumetricAgent, in a scene with no baked volume.
    struct FakeStore
    {
        VolumeData volume;
        AgentData agent;
        int volumeWrites = 0;
        int agentWrites = 0;

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();
            d.registerQueryHandler<events::volumetric::GetVolumetricNavVolumeDataQuery>(
                [this](const events::volumetric::GetVolumetricNavVolumeDataQuery& query) -> std::optional<VolumeData>
                {
                    if (query.entity.id != kEntityId)
                    {
                        return std::nullopt;
                    }
                    return volume;
                });
            d.registerCommandHandler<events::volumetric::SetVolumetricNavVolumeDataCommand>(
                [this](const events::volumetric::SetVolumetricNavVolumeDataCommand& command)
                {
                    if (command.entity.id != kEntityId || !volumeAccepted(command.data))
                    {
                        return false;
                    }
                    ++volumeWrites;
                    volume.centerOffset = command.data.centerOffset;
                    volume.halfExtents = command.data.halfExtents;
                    volume.voxelSize = command.data.voxelSize;
                    volume.connectivity = command.data.connectivity;
                    volume.agentClearance = command.data.agentClearance;
                    volume.includeTerrain = command.data.includeTerrain;
                    volume.includeStaticMeshes = command.data.includeStaticMeshes;
                    volume.includeColliders = command.data.includeColliders;
                    return true;
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricVolumeStatusQuery>(
                [this](const events::volumetric::GetVolumetricVolumeStatusQuery& query)
                    -> std::optional<services::VolumetricVolumeStatus>
                {
                    if (query.volume.id != kEntityId)
                    {
                        return std::nullopt;
                    }
                    services::VolumetricVolumeStatus status;
                    status.entity = handleOf(kEntityId);
                    status.name = "Flyer";
                    status.uuid = 1;
                    status.worldMin = volume.centerOffset - volume.halfExtents;
                    status.worldMax = volume.centerOffset + volume.halfExtents;
                    status.voxelSize = volume.voxelSize;
                    status.connectivity = volume.connectivity;
                    status.agentClearance = volume.agentClearance;
                    return status;
                });
            d.registerQueryHandler<events::volumetric::ListVolumetricVolumesQuery>(
                [](const events::volumetric::ListVolumetricVolumesQuery&)
                {
                    return std::vector<services::VolumetricVolumeStatus>{};
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricAgentDataQuery>(
                [this](const events::volumetric::GetVolumetricAgentDataQuery& query) -> std::optional<AgentData>
                {
                    if (query.entity.id != kEntityId)
                    {
                        return std::nullopt;
                    }
                    return agent;
                });
            d.registerCommandHandler<events::volumetric::SetVolumetricAgentDataCommand>(
                [this](const events::volumetric::SetVolumetricAgentDataCommand& command)
                {
                    if (command.entity.id != kEntityId || !agentAccepted(command.data))
                    {
                        return false;
                    }
                    ++agentWrites;
                    agent.radius = command.data.radius;
                    agent.maxSpeed = command.data.maxSpeed;
                    agent.maxAcceleration = command.data.maxAcceleration;
                    agent.stoppingDistance = command.data.stoppingDistance;
                    agent.turnSpeed = command.data.turnSpeed;
                    agent.alignPitch = command.data.alignPitch;
                    return true;
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricAgentStateQuery>(
                [](const events::volumetric::GetVolumetricAgentStateQuery& query)
                    -> std::optional<services::VolumetricAgentState>
                {
                    if (query.entity.id != kEntityId)
                    {
                        return std::nullopt;
                    }
                    return services::VolumetricAgentState{};
                });
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    if (query.entity.id != kEntityId)
                    {
                        return std::nullopt;
                    }
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = "Flyer";
                    data.components = {services::ComponentTypeId::VolumetricNavVolume,
                                       services::ComponentTypeId::VolumetricAgent};
                    return data;
                });
        }
    };

    // Each bound of every field of `table` is accepted by the binding and reported back as given; every value of
    // outsideValues is refused with an ArgError.
    template <typename T>
    void checkBounds(const mcp::tools::ComponentBinding& binding, const env::SectionTable<T>& table,
                     const services::EntityHandle& entity)
    {
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const std::string key(field.key);
            for (const Bound bound : {Bound::Min, Bound::Max})
            {
                INFO(binding.name << "." << key << " at its " << boundName(bound));
                const json value = boundValue(field, bound);
                const std::optional<json> written = binding.patch(entity, json{{key, value}});
                REQUIRE(written.has_value());
                CHECK(written->at(key) == value);
            }
            for (const json& value : outsideValues(field))
            {
                INFO(binding.name << "." << key << " = " << value.dump());
                CHECK_THROWS_AS(binding.patch(entity, json{{key, value}}), mcp::ArgError);
            }
        }
    }

    // ------------------------------------------------------------------
    // Cases
    // ------------------------------------------------------------------

    TEST_CASE("volumetric ranges: every bound of the binding tables survives a scene round trip")
    {
        ScratchProject project;
        const env::SectionTable<VolumeData>& volumeTable = volumetrictool::volumeTable();
        const env::SectionTable<AgentData>& agentTable = volumetrictool::agentTable();

        for (const Bound bound : {Bound::Min, Bound::Max})
        {
            INFO("every field at its " << boundName(bound));
            const VolumeData volume = env::strictPatch(volumeTable, VolumeData{}, boundPatch(volumeTable, bound), "data");
            const AgentData agent = env::strictPatch(agentTable, AgentData{}, boundPatch(agentTable, bound), "data");
            checkAtBound(volumeTable, volume, bound);
            checkAtBound(agentTable, agent, bound);

            const fs::path scenePath = project.root / (std::string("Volumetric_") + boundName(bound) + ".vfScene");
            {
                scene::SceneGraphSystem source;
                SceneCleanup sourceCleanup{source};
                toComponent(volume, source.GetRoot().addOrReplaceComponent<components::VolumetricNavVolumeComponent>());
                toComponent(agent, source.GetRoot().addOrReplaceComponent<components::VolumetricAgentComponent>());
                REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));
            }

            const json sceneJson = readJson(scenePath);
            REQUIRE(sceneJson.contains("root"));
            const json& saved = sceneJson.at("root").at("components");
            REQUIRE(saved.contains("volumetricNavVolume"));
            REQUIRE(saved.contains("volumetricAgent"));
            // bakedRef is reported read-only, as bakedFile (the service resolves it).
            checkSaved(volumeTable, volume, saved.at("volumetricNavVolume"), {"bakedRef"});
            checkSaved(agentTable, agent, saved.at("volumetricAgent"), {});

            asset::AssetDatabase::instance().clear();
            scene::SceneGraphSystem loaded;
            SceneCleanup loadedCleanup{loaded};
            REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
            REQUIRE(loaded.GetRoot().hasComponent<components::VolumetricNavVolumeComponent>());
            REQUIRE(loaded.GetRoot().hasComponent<components::VolumetricAgentComponent>());
            checkSameFields(volumeTable, volume,
                            fromComponent(loaded.GetRoot().getComponent<components::VolumetricNavVolumeComponent>()));
            checkSameFields(agentTable, agent,
                            fromComponent(loaded.GetRoot().getComponent<components::VolumetricAgentComponent>()));
        }
    }

    TEST_CASE("volumetric ranges: the bindings accept every bound and refuse one step past it")
    {
        DispatcherScope scope;
        FakeStore store;
        store.registerHandlers();
        const services::EntityHandle entity = handleOf(kEntityId);

        checkBounds(mcp::tools::makeVolumetricNavVolumeBinding(), volumetrictool::volumeTable(), entity);
        checkBounds(mcp::tools::makeVolumetricAgentBinding(), volumetrictool::agentTable(), entity);
        CHECK(store.volume.connectivity == 26);
    }

    TEST_CASE("volumetric ranges: values the engine stored outside the ranges pass back, and an undo restores them")
    {
        DispatcherScope scope;
        FakeStore store;
        store.registerHandlers();
        const services::EntityHandle entity = handleOf(kEntityId);

        SUBCASE("VolumetricNavVolume")
        {
            const mcp::tools::ComponentBinding binding = mcp::tools::makeVolumetricNavVolumeBinding();
            // A scene file can hold any value the loader reads; these are all ones the engine uses.
            store.volume.centerOffset = glm::vec3(20000.0f, 0.0f, 0.0f);
            store.volume.halfExtents = glm::vec3(8000.0f);
            store.volume.voxelSize = 50.0f;
            store.volume.agentClearance = 25.0f;

            const std::optional<json> reported = binding.get(entity);
            REQUIRE(reported.has_value());
            CHECK(reported->at("voxelSize") == 50.0);
            CHECK(reported->at("halfExtents") == json::array({8000.0, 8000.0, 8000.0}));
            const json writable = mcp::tools::withoutReadOnlyFields(binding, *reported);

            // Passed back whole, nothing changes and nothing is written.
            REQUIRE(binding.patch(entity, writable).has_value());
            CHECK(store.volumeWrites == 0);

            // Another key changes; the stored values stay.
            json changed = writable;
            changed["includeTerrain"] = false;
            REQUIRE(binding.patch(entity, changed).has_value());
            CHECK(store.volumeWrites == 1);
            CHECK(store.volume.includeTerrain == false);
            CHECK(store.volume.voxelSize == 50.0f);

            // A new value outside the range is refused.
            CHECK_THROWS_AS(binding.patch(entity, json{{"voxelSize", 50.5}}), mcp::ArgError);

            // An undo restores a stored value the agent could not set ...
            REQUIRE(binding.patch(entity, json{{"voxelSize", 2}}).has_value());
            CHECK(store.volume.voxelSize == 2.0f);
            REQUIRE(binding.restore(entity, writable).has_value());
            CHECK(store.volume.voxelSize == 50.0f);
            CHECK(store.volume.includeTerrain == true);

            // ... but never one the engine refuses.
            json invalid = writable;
            invalid["voxelSize"] = 0.0;
            CHECK_THROWS_AS(binding.restore(entity, invalid), std::runtime_error);
            CHECK(store.volume.voxelSize == 50.0f);
        }
        SUBCASE("a stored connectivity other than 6 or 26 passes back but blocks other edits")
        {
            const mcp::tools::ComponentBinding binding = mcp::tools::makeVolumetricNavVolumeBinding();
            store.volume.connectivity = 7;
            const std::optional<json> reported = binding.get(entity);
            REQUIRE(reported.has_value());
            const json writable = mcp::tools::withoutReadOnlyFields(binding, *reported);
            CHECK(writable.at("connectivity") == 7);

            REQUIRE(binding.patch(entity, writable).has_value());
            CHECK(store.volumeWrites == 0);
            CHECK_THROWS_AS(binding.patch(entity, json{{"includeColliders", false}}), mcp::ArgError);
            REQUIRE(binding.patch(entity, json{{"includeColliders", false}, {"connectivity", 26}}).has_value());
            CHECK(store.volume.connectivity == 26);
            CHECK(store.volume.includeColliders == false);
        }
        SUBCASE("VolumetricAgent")
        {
            const mcp::tools::ComponentBinding binding = mcp::tools::makeVolumetricAgentBinding();
            store.agent.radius = 9.0f;
            store.agent.turnSpeed = 5000.0f;

            const std::optional<json> reported = binding.get(entity);
            REQUIRE(reported.has_value());
            const json writable = mcp::tools::withoutReadOnlyFields(binding, *reported);
            CHECK(writable.at("radius") == 9.0);
            CHECK(writable.at("turnSpeed") == 5000.0);

            REQUIRE(binding.patch(entity, writable).has_value());
            CHECK(store.agentWrites == 0);
            CHECK_THROWS_AS(binding.patch(entity, json{{"radius", 9.5}}), mcp::ArgError);

            REQUIRE(binding.patch(entity, json{{"radius", 1}, {"turnSpeed", 90}}).has_value());
            CHECK(store.agent.radius == 1.0f);
            CHECK(store.agent.turnSpeed == 90.0f);
            REQUIRE(binding.restore(entity, writable).has_value());
            CHECK(store.agent.radius == 9.0f);
            CHECK(store.agent.turnSpeed == 5000.0f);

            json invalid = writable;
            invalid["radius"] = 0.0;
            CHECK_THROWS_AS(binding.restore(entity, invalid), std::runtime_error);
            CHECK(store.agent.radius == 9.0f);
        }
    }
}
