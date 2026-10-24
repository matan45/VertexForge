#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/SettingsTable.hpp"
#include "tools/WaterToolSupport.hpp"
#include "undo/EntityIdRemap.hpp"

#include "components/Components.hpp"
#include "impl/scene/OceanService.hpp"
#include "water/SeaState.hpp"

#include "data/DTOs.hpp"
#include "data/OceanData.hpp"
#include "data/TerrainAuthoringData.hpp"
#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// VK-1658: the MCP water tools (water_get, water_create_body, water_set_body, water_set_params, ocean_enable,
// ocean_set_params), their undo entries (mcp/undo/WaterUndo plus the generic entity undos) and the pure helpers of
// WaterToolSupport, against a fake engine. The fake answers the ocean events the way OceanService does: one tracked
// ocean; GetOceanState / SetOceanState with an invalid entity mean that ocean and refuse any other entity;
// CreateOcean refuses a second ocean and creates a default OceanComponent with the creation data on top; deleting
// the ocean entity drops it. Its entity tree serializes like a prefab snapshot (the ocean has no JSON of its own, a
// water body does), and it samples an analytic terrain whose basin has known lowest / centre / rim heights.

namespace
{
    namespace watertool = mcp::tools::watertool;
    namespace env = mcp::tools::env;
    using json = nlohmann::json;

    constexpr uint64_t kRootId = 1;
    constexpr uint64_t kTerrainId = 2;

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    struct DispatcherScope
    {
        DispatcherScope() { clearAll(); }
        ~DispatcherScope() { clearAll(); }

        static void clearAll()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }
    };

    std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    bool textHas(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    // Some entry of a JSON list of strings (notes, warnings) contains `needle`.
    bool listHas(const json& list, const std::string& needle)
    {
        if (!list.is_array())
        {
            return false;
        }
        return std::any_of(list.begin(), list.end(), [&needle](const json& entry)
        {
            return entry.is_string() && textHas(entry.get<std::string>(), needle);
        });
    }

    json jsonOf(const glm::vec3& v)
    {
        return json::array({v.x, v.y, v.z});
    }

    glm::vec3 vec3Of(const json& v)
    {
        return glm::vec3(v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>());
    }

    // ------------------------------------------------------------------
    // The analytic terrain
    // ------------------------------------------------------------------

    // 4 x 4 tiles of 32 m at low resolution (33 vertices per tile: 1 m between height samples) covering [-64, 64] on
    // both axes. A basin centred on (-24, 16): a floor at -5 out to 3 m, a ring at -2 out to 6 m, and a 1 m pit at -7
    // two metres east of the centre; a 0.2 m well at -9 on (40, 40), narrower than the sample spacing; a strip below
    // z = -40 at -10 (the terrain's lowest ground); 0 elsewhere. The features are wide enough that the float
    // positions of a footprint grid (centre +/- whole metres, give or take rounding) never sit on a boundary, so a
    // 10 x 10 m footprint on the basin has min -7, centre -5 and rim -2.
    constexpr float kTerrainHalf = 64.0f;
    const glm::vec2 kBasin(-24.0f, 16.0f);
    const glm::vec2 kWell(40.0f, 40.0f);

    services::TerrainHeightSample terrainHeight(const glm::vec2& point)
    {
        services::TerrainHeightSample sample;
        if (std::fabs(point.x) > kTerrainHalf || std::fabs(point.y) > kTerrainHalf)
        {
            return sample; // off the terrain: valid = false
        }
        sample.valid = true;
        sample.onTerrain = true;
        const float dx = point.x - kBasin.x;
        const float dz = point.y - kBasin.y;
        const float ring = std::max(std::fabs(dx), std::fabs(dz));
        if (std::fabs(point.x - kWell.x) < 0.1f && std::fabs(point.y - kWell.y) < 0.1f)
        {
            sample.height = -9.0f; // the well
        }
        else if (std::fabs(dx - 2.0f) < 0.5f && std::fabs(dz) < 0.5f)
        {
            sample.height = -7.0f; // the pit
        }
        else if (ring < 3.0f)
        {
            sample.height = -5.0f; // the basin floor
        }
        else if (ring < 6.0f)
        {
            sample.height = -2.0f; // its ring
        }
        else if (point.y < -40.0f)
        {
            sample.height = -10.0f; // the low south edge
        }
        else
        {
            sample.height = 0.0f;
        }
        return sample;
    }

    services::TerrainSummary terrainSummary(uint64_t id)
    {
        services::TerrainSummary terrain;
        terrain.entity = handleOf(id);
        terrain.name = id == kTerrainId ? "Coast" : "Coast " + std::to_string(id);
        terrain.live = true;
        terrain.data.resolution = 0;       // low: 33 vertices per tile
        terrain.data.worldTileSize = 32.0f;
        terrain.data.gridMinX = -2;
        terrain.data.gridMinZ = -2;
        terrain.data.gridMaxX = 1;
        terrain.data.gridMaxZ = 1;
        return terrain;
    }

    // ------------------------------------------------------------------
    // The engine side the tools reach
    // ------------------------------------------------------------------

    // OceanService::createOcean: a default OceanComponent with the creation data on top, visible.
    services::OceanStateData createdState(const services::OceanCreationData& config)
    {
        components::OceanComponent component;
        component.waterHeight = config.waterHeight;
        component.physicsEnabled = config.physicsEnabled;
        component.shallowColor = config.shallowColor;
        component.deepColor = config.deepColor;
        component.isActive = true;
        services::OceanStateData state = services::OceanService::stateFromComponent(component);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            state.bands[i] = config.oceanConfig.bands[i];
        }
        state.gravity = config.oceanConfig.gravity;
        return state;
    }

    // OceanService::applyWaterBodyData's clamps.
    services::WaterBodyComponentData storedBody(const services::WaterBodyComponentData& data)
    {
        services::WaterBodyComponentData out = data;
        out.type = data.type == 1u ? 1u : 0u;
        out.halfExtents = glm::max(data.halfExtents, glm::vec2(0.0f));
        out.depth = std::max(data.depth, 0.0f);
        out.bandMask = data.bandMask & 0x7u;
        return out;
    }

    // SceneGraphSystem::moveEntity's rule: insertIndex refers to the child list BEFORE the move; a same-parent move
    // from in front of the slot decrements it.
    void moveInList(std::vector<uint64_t>& oldList, std::vector<uint64_t>& newList, bool sameParent, uint64_t id,
                    int insertIndex)
    {
        if (insertIndex >= 0 && sameParent)
        {
            const auto it = std::find(oldList.begin(), oldList.end(), id);
            if (it != oldList.end() && std::distance(oldList.begin(), it) < insertIndex)
            {
                --insertIndex;
            }
        }
        oldList.erase(std::remove(oldList.begin(), oldList.end(), id), oldList.end());
        if (insertIndex < 0)
        {
            newList.push_back(id);
            return;
        }
        const std::size_t index = std::min(static_cast<std::size_t>(insertIndex), newList.size());
        newList.insert(newList.begin() + static_cast<std::ptrdiff_t>(index), id);
    }

    struct Node
    {
        std::string name;
        std::optional<uint64_t> parent;
        std::vector<uint64_t> children;
        services::TransformData transform;
        std::optional<services::WaterBodyComponentData> body;
        std::optional<services::OceanStateData> ocean;   // an OceanComponent
        json extraComponents = json::object();           // other components the prefab snapshot carries
    };

    struct FakeWaterEngine
    {
        bool playMode = false;
        bool loadPending = false;
        bool worldMode = false;
        bool weatherEnabled = false;
        int liveTerrains = 1;            // ListTerrainsQuery: how many live terrains (ids 2, 3, ...)
        bool refuseStateWrites = false;  // SetOceanStateCommand answers false
        bool failBodyWrites = false;     // SetWaterBodyDataCommand throws

        std::map<uint64_t, Node> nodes;
        uint64_t nextId = 100;
        uint64_t tracked = 0;            // the ocean OceanService tracks; 0 = none
        std::vector<services::OceanCreationData> createdOceans;   // every CreateOceanCommand, refused ones too
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        uint64_t lastHeightTerrain = 0;

        FakeWaterEngine()
        {
            Node root;
            root.name = "Root";
            nodes[kRootId] = root;
        }

        bool alive(uint64_t id) const { return nodes.contains(id); }

        uint64_t addNode(const std::string& name, uint64_t parent)
        {
            const uint64_t id = nextId++;
            Node added;
            added.name = name;
            added.parent = parent;
            nodes[id] = added;
            nodes.at(parent).children.push_back(id);
            return id;
        }

        uint64_t addBody(const std::string& name, const glm::vec3& position,
                         const services::WaterBodyComponentData& data = {})
        {
            const uint64_t id = addNode(name, kRootId);
            nodes.at(id).transform.position = position;
            nodes.at(id).body = storedBody(data);
            return id;
        }

        void reparent(uint64_t id, uint64_t parent)
        {
            Node& moved = nodes.at(id);
            std::vector<uint64_t>& siblings = nodes.at(*moved.parent).children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
            moved.parent = parent;
            nodes.at(parent).children.push_back(id);
        }

        // Erases `id` and its subtree; deleting the tracked ocean drops it (OceanService::onEntityDeleted).
        void erase(uint64_t id)
        {
            for (uint64_t child : std::vector<uint64_t>(nodes.at(id).children))
            {
                erase(child);
            }
            if (id == tracked)
            {
                tracked = 0;
            }
            nodes.erase(id);
        }

        Node& node(uint64_t id)
        {
            REQUIRE(alive(id));
            return nodes.at(id);
        }

        services::WaterBodyComponentData& bodyOf(uint64_t id)
        {
            Node& found = node(id);
            REQUIRE(found.body.has_value());
            return *found.body;
        }

        services::OceanStateData& oceanState()
        {
            REQUIRE(tracked != 0);
            Node& found = node(tracked);
            REQUIRE(found.ocean.has_value());
            return *found.ocean;
        }

        json serialize(uint64_t id) const
        {
            const Node& stored = nodes.at(id);
            json components = stored.extraComponents;
            if (stored.body.has_value())
            {
                const services::WaterBodyComponentData& body = *stored.body;
                components["waterBody"] = {
                    {"type", body.type},
                    {"waterHeight", body.waterHeight},
                    {"halfExtents", json::array({body.halfExtents.x, body.halfExtents.y})},
                    {"depth", body.depth},
                    {"bandMask", body.bandMask},
                    {"physicsEnabled", body.physicsEnabled},
                    {"isActive", body.isActive}
                };
            }
            json children = json::array();
            for (uint64_t child : stored.children)
            {
                children.push_back(serialize(child));
            }
            return {
                {"name", stored.name},
                {"isActive", true},
                {"transform", {
                    {"position", jsonOf(stored.transform.position)},
                    {"rotation", jsonOf(stored.transform.rotation)},
                    {"scale", jsonOf(stored.transform.scale)}
                }},
                {"components", std::move(components)},
                {"children", std::move(children)}
            };
        }

        uint64_t instantiate(const json& entity, uint64_t parent)
        {
            const uint64_t id = addNode(entity.value("name", std::string("Entity")), parent);
            Node& created = nodes.at(id);
            const json& transform = entity.at("transform");
            created.transform.position = vec3Of(transform.at("position"));
            created.transform.rotation = vec3Of(transform.at("rotation"));
            created.transform.scale = vec3Of(transform.at("scale"));
            const json& components = entity.at("components");
            for (auto it = components.begin(); it != components.end(); ++it)
            {
                if (it.key() != "waterBody")
                {
                    created.extraComponents[it.key()] = it.value();
                    continue;
                }
                const json& body = it.value();
                services::WaterBodyComponentData data;
                data.type = body.at("type").get<uint32_t>();
                data.waterHeight = body.at("waterHeight").get<float>();
                data.halfExtents = glm::vec2(body.at("halfExtents").at(0).get<float>(),
                                             body.at("halfExtents").at(1).get<float>());
                data.depth = body.at("depth").get<float>();
                data.bandMask = body.at("bandMask").get<uint32_t>();
                data.physicsEnabled = body.at("physicsEnabled").get<bool>();
                data.isActive = body.at("isActive").get<bool>();
                created.body = storedBody(data);
            }
            for (const json& child : entity.at("children"))
            {
                instantiate(child, id);
            }
            return id;
        }

        void registerHandlers(bool terrainService)
        {
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::world::IsWorldModeQuery>(
                [this](const events::world::IsWorldModeQuery&) { return worldMode; });
            d.registerQueryHandler<events::weather::IsWeatherEnabledQuery>(
                [this](const events::weather::IsWeatherEnabledQuery&) { return weatherEnabled; });

            // Entities.
            d.registerQueryHandler<events::scene::GetRootEntityQuery>(
                [](const events::scene::GetRootEntityQuery&) { return handleOf(kRootId); });
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    const Node& stored = nodes.at(query.entity.id);
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = stored.name;
                    if (stored.parent.has_value())
                    {
                        data.parent = handleOf(*stored.parent);
                    }
                    for (uint64_t child : stored.children)
                    {
                        data.children.push_back(handleOf(child));
                    }
                    data.localTransform = stored.transform;
                    data.worldTransform = stored.transform;
                    return data;
                });
            d.registerQueryHandler<events::scene::GetTransformQuery>(
                [this](const events::scene::GetTransformQuery& query) -> std::optional<services::TransformData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    return nodes.at(query.entity.id).transform;
                });
            d.registerCommandHandler<events::scene::SetTransformCommand>(
                [this](const events::scene::SetTransformCommand& command)
                {
                    if (alive(command.entity.id))
                    {
                        nodes.at(command.entity.id).transform = command.transform;
                    }
                });
            d.registerCommandHandler<events::scene::SetEntityNameCommand>(
                [this](const events::scene::SetEntityNameCommand& command)
                {
                    if (alive(command.entity.id))
                    {
                        nodes.at(command.entity.id).name = command.newName;
                    }
                });
            d.registerCommandHandler<events::scene::DeleteEntityCommand>(
                [this](const events::scene::DeleteEntityCommand& command)
                {
                    if (!alive(command.entity.id) || command.entity.id == kRootId)
                    {
                        return false;
                    }
                    std::vector<uint64_t>& siblings = nodes.at(*nodes.at(command.entity.id).parent).children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), command.entity.id), siblings.end());
                    erase(command.entity.id);
                    return true;
                });
            d.registerQueryHandler<events::scene::CopyEntityToJsonQuery>(
                [this](const events::scene::CopyEntityToJsonQuery& query) -> std::string
                {
                    if (!alive(query.entity.id) || query.entity.id == kRootId)
                    {
                        return {};
                    }
                    return serialize(query.entity.id).dump();
                });
            d.registerCommandHandler<events::scene::InstantiateEntityFromJsonCommand>(
                [this](const events::scene::InstantiateEntityFromJsonCommand& command)
                    -> std::optional<services::EntityHandle>
                {
                    const uint64_t parent =
                        command.parent.has_value() && alive(command.parent->id) ? command.parent->id : kRootId;
                    return handleOf(instantiate(json::parse(command.jsonText), parent));
                });
            d.registerCommandHandler<events::scene::ReorderEntityCommand>(
                [this](const events::scene::ReorderEntityCommand& command)
                {
                    if (!alive(command.entity.id) || !alive(command.newParent.id))
                    {
                        return false;
                    }
                    Node& moved = nodes.at(command.entity.id);
                    const uint64_t oldParent = *moved.parent;
                    moveInList(nodes.at(oldParent).children, nodes.at(command.newParent.id).children,
                               oldParent == command.newParent.id, command.entity.id, command.insertIndex);
                    moved.parent = command.newParent.id;
                    return true;
                });

            // The ocean, answered the way OceanService answers.
            d.registerQueryHandler<events::ocean::GetOceanEntityQuery>(
                [this](const events::ocean::GetOceanEntityQuery&)
                {
                    return tracked != 0 ? handleOf(tracked) : services::EntityHandle{};
                });
            d.registerQueryHandler<events::ocean::GetOceanStateQuery>(
                [this](const events::ocean::GetOceanStateQuery& query) -> std::optional<services::OceanStateData>
                {
                    if (tracked == 0 || (query.entity.isValid() && query.entity.id != tracked))
                    {
                        return std::nullopt;
                    }
                    return nodes.at(tracked).ocean;
                });
            d.registerCommandHandler<events::ocean::SetOceanStateCommand>(
                [this](const events::ocean::SetOceanStateCommand& command)
                {
                    if (refuseStateWrites || tracked == 0 || (command.entity.isValid() && command.entity.id != tracked))
                    {
                        return false;
                    }
                    nodes.at(tracked).ocean = command.state;
                    return true;
                });
            d.registerCommandHandler<events::ocean::CreateOceanCommand>(
                [this](const events::ocean::CreateOceanCommand& command)
                {
                    createdOceans.push_back(command.config);
                    if (tracked != 0)
                    {
                        return services::EntityHandle{}; // one ocean per scene
                    }
                    const uint64_t id = addNode("Ocean", kRootId);
                    nodes.at(id).ocean = createdState(command.config);
                    tracked = id;
                    return handleOf(id);
                });
            d.registerQueryHandler<events::ocean::HasOceanComponentQuery>(
                [this](const events::ocean::HasOceanComponentQuery& query)
                {
                    return alive(query.entity.id) && nodes.at(query.entity.id).ocean.has_value();
                });

            // Water bodies.
            d.registerCommandHandler<events::ocean::CreateWaterBodyCommand>(
                [this](const events::ocean::CreateWaterBodyCommand& command)
                {
                    const uint64_t id = addNode(command.name.empty() ? std::string("WaterBody") : command.name, kRootId);
                    nodes.at(id).transform.position = command.position;
                    nodes.at(id).body = storedBody(command.data);
                    return handleOf(id);
                });
            d.registerCommandHandler<events::ocean::SetWaterBodyDataCommand>(
                [this](const events::ocean::SetWaterBodyDataCommand& command)
                {
                    if (failBodyWrites)
                    {
                        throw std::runtime_error("the water body write failed");
                    }
                    if (alive(command.entity.id) && nodes.at(command.entity.id).body.has_value())
                    {
                        nodes.at(command.entity.id).body = storedBody(command.data);
                    }
                });
            d.registerQueryHandler<events::ocean::GetWaterBodyDataQuery>(
                [this](const events::ocean::GetWaterBodyDataQuery& query)
                    -> std::optional<services::WaterBodyComponentData>
                {
                    return alive(query.entity.id) ? nodes.at(query.entity.id).body : std::nullopt;
                });
            d.registerQueryHandler<events::ocean::HasWaterBodyComponentQuery>(
                [this](const events::ocean::HasWaterBodyComponentQuery& query)
                {
                    return alive(query.entity.id) && nodes.at(query.entity.id).body.has_value();
                });
            d.registerQueryHandler<events::ocean::GetWaterBodiesQuery>(
                [this](const events::ocean::GetWaterBodiesQuery&)
                {
                    // Newest first, so water_get's own sort by id is what orders its report.
                    std::vector<services::WaterBodyEntry> entries;
                    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                    {
                        if (it->second.body.has_value())
                        {
                            entries.push_back(services::WaterBodyEntry{handleOf(it->first), *it->second.body});
                        }
                    }
                    return entries;
                });

            // The terrain service (absent when !terrainService).
            if (terrainService)
            {
                d.registerQueryHandler<events::terrainAuthoring::ListTerrainsQuery>(
                    [this](const events::terrainAuthoring::ListTerrainsQuery&)
                    {
                        std::vector<services::TerrainSummary> terrains;
                        for (int i = 0; i < liveTerrains; ++i)
                        {
                            terrains.push_back(terrainSummary(kTerrainId + static_cast<uint64_t>(i)));
                        }
                        return terrains;
                    });
                d.registerQueryHandler<events::terrainAuthoring::GetTerrainHeightsQuery>(
                    [this](const events::terrainAuthoring::GetTerrainHeightsQuery& query)
                    {
                        lastHeightTerrain = query.terrainEntity.id;
                        std::vector<services::TerrainHeightSample> heights;
                        heights.reserve(query.positions.size());
                        for (const glm::vec2& point : query.positions)
                        {
                            heights.push_back(terrainHeight(point));
                        }
                        return heights;
                    });
            }

            // Undo: PushUndoableCommand stores without executing, like UndoRedoServiceImpl.
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
        }
    };

    struct WaterFixture
    {
        DispatcherScope dispatcherScope;
        FakeWaterEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        explicit WaterFixture(bool terrainService = true)
        {
            engine.registerHandlers(terrainService);
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerWaterTools(registry, context);
        }

        WaterFixture(const WaterFixture&) = delete;
        WaterFixture& operator=(const WaterFixture&) = delete;

        const mcp::ToolDef& tool(const char* name) const
        {
            const auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        json callOk(const char* name, const json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        // The message of an ArgError the call throws.
        std::string argError(const char* name, const json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        // The failure of a call, reported in-band (isError) or thrown.
        std::string failure(const char* name, const json& args)
        {
            try
            {
                const mcp::ToolResult result = call(name, args);
                return result.isError ? result.text : std::string("<no error>");
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
        }

        // ocean_enable {enabled:true, ...}; the new ocean's id.
        uint64_t createOcean(json args = json::object())
        {
            args["enabled"] = true;
            callOk("ocean_enable", args);
            REQUIRE(engine.tracked != 0);
            return engine.tracked;
        }

        uint64_t createBody(const json& args)
        {
            return callOk("water_create_body", args)["entity"].get<uint64_t>();
        }
    };

    // A lake over the basin: a 10 x 10 m footprint whose terrain has min -7, centre -5, rim -2.
    json basinLake(json extra = json::object())
    {
        json args{{"center", {-24, 16}}, {"halfExtents", {5, 5}}};
        args.update(extra);
        return args;
    }

    // The value a patch gives `field` at its minimum or maximum, in the form FieldDesc::get reports.
    template <typename T>
    json boundValue(const env::FieldDesc<T>& field, bool max)
    {
        const double bound = max ? field.max : field.min;
        switch (field.kind)
        {
        case env::FieldKind::Bool:
            return max;
        case env::FieldKind::Float:
            return env::floatJson(static_cast<float>(bound));
        case env::FieldKind::Int:
        case env::FieldKind::UInt:
            return static_cast<int64_t>(bound);
        case env::FieldKind::Vec3:
        {
            const json component = env::floatJson(static_cast<float>(bound));
            return json::array({component, component, component});
        }
        default:
            break;
        }
        throw std::logic_error("the water tables have no enum or string fields");
    }

    // Every field of `table` at its minimum (or maximum).
    template <typename T>
    json boundPatch(const env::SectionTable<T>& table, bool max)
    {
        json patch = json::object();
        for (const env::FieldDesc<T>& field : table.fields)
        {
            patch[std::string(field.key)] = boundValue(field, max);
        }
        return patch;
    }

    // Every field of `table` at a value other than its value in `base` (37 % into the range; bools flipped).
    template <typename T>
    json distinctPatch(const env::SectionTable<T>& table, const T& base)
    {
        json patch = json::object();
        for (const env::FieldDesc<T>& field : table.fields)
        {
            const double value = field.min + (field.max - field.min) * 0.37;
            json entry;
            switch (field.kind)
            {
            case env::FieldKind::Bool:
                entry = !field.get(base).template get<bool>();
                break;
            case env::FieldKind::Float:
                entry = env::floatJson(static_cast<float>(value));
                break;
            case env::FieldKind::Int:
            case env::FieldKind::UInt:
                entry = static_cast<int64_t>(value);
                break;
            case env::FieldKind::Vec3:
            {
                const json component = env::floatJson(static_cast<float>(value));
                entry = json::array({component, component, component});
                break;
            }
            default:
                throw std::logic_error("the water tables have no enum or string fields");
            }
            patch[std::string(field.key)] = std::move(entry);
        }
        return patch;
    }

    // Equal up to a few ulps. The sea-state math runs std::pow (windSpeedFromBeaufort, beaufortFromWindSpeed),
    // and an optimised build may fold it at compile time here, with constant arguments, but not in the tool,
    // which gets its numbers from the arguments at run time. Relative, so a 1e-7 amplitude is still checked.
    bool sameFloat(float a, float b)
    {
        return a == b || std::fabs(a - b) <= 4.0f * std::numeric_limits<float>::epsilon() * std::max(std::fabs(a), std::fabs(b));
    }

    void checkSeaStateBands(const services::OceanStateData& state, const water::SeaState& expected)
    {
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            const services::OceanBandConfigData& band = state.bands[i];
            const water::SeaStateBandParams& want = expected.bands[i];
            CAPTURE(i);
            CAPTURE(band.windSpeed);
            CAPTURE(want.windSpeed);
            CAPTURE(band.amplitude);
            CAPTURE(want.amplitude);
            CHECK(sameFloat(band.windSpeed, want.windSpeed));
            CHECK(sameFloat(band.windDirection, want.windDirection));
            CHECK(sameFloat(band.amplitude, want.amplitude));
            CHECK(sameFloat(band.choppiness, want.choppiness));
            CHECK(sameFloat(band.displacementScale, want.displacementScale));
            CHECK(sameFloat(band.foamThreshold, want.foamThreshold));
        }
    }

    services::TerrainHeightSample validHeight(float height)
    {
        services::TerrainHeightSample sample;
        sample.height = height;
        sample.valid = true;
        sample.onTerrain = true;
        return sample;
    }
}

TEST_SUITE("McpWaterTools")
{
    // ------------------------------------------------------------------
    // Registration
    // ------------------------------------------------------------------

    TEST_CASE("registering the water tools dispatches nothing")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerWaterTools(registry, context));
        CHECK(registry.size() == 6);
    }

    TEST_CASE("the water tools run on the main thread with closed schemas and their annotations")
    {
        WaterFixture fixture;

        struct Expected
        {
            const char* name;
            bool readOnly;
            bool destructive;
            long long timeoutMs;
        };
        for (const Expected& entry : {Expected{"water_get", true, false, 10000},
                                      Expected{"water_create_body", false, false, 30000},
                                      Expected{"water_set_body", false, false, 30000},
                                      Expected{"water_set_params", false, false, 10000},
                                      Expected{"ocean_enable", false, true, 30000},
                                      Expected{"ocean_set_params", false, false, 30000}})
        {
            CAPTURE(entry.name);
            const mcp::ToolDef& def = fixture.tool(entry.name);
            CHECK(def.affinity == mcp::ThreadAffinity::Main);
            CHECK(def.readOnly == entry.readOnly);
            CHECK(def.destructive == entry.destructive);
            CHECK(def.timeout.count() == entry.timeoutMs);
            CHECK(def.inputSchema["type"] == "object");
            CHECK(def.inputSchema["additionalProperties"] == false);
        }
        CHECK(fixture.tool("water_create_body").inputSchema["required"] == json::array({"center", "halfExtents"}));
        CHECK(fixture.tool("water_set_body").inputSchema["required"] == json::array({"entity"}));
        CHECK(fixture.tool("ocean_enable").inputSchema["required"] == json::array({"enabled"}));

        // water_set_params: exactly the nine look sections, each closed.
        const json& look = fixture.tool("water_set_params").inputSchema["properties"];
        CHECK(look.size() == watertool::lookSections().size());
        for (const auto& section : watertool::lookSections())
        {
            CAPTURE(section.name);
            REQUIRE(look.contains(std::string(section.name)));
            CHECK(look[std::string(section.name)]["additionalProperties"] == false);
        }

        // ocean_set_params: the three bands, closed.
        const json& bands = fixture.tool("ocean_set_params").inputSchema["properties"]["bands"];
        CHECK(bands["additionalProperties"] == false);
        for (std::string_view name : watertool::bandNames)
        {
            CAPTURE(name);
            CHECK(bands["properties"].contains(std::string(name)));
        }
    }

    // ------------------------------------------------------------------
    // water_get
    // ------------------------------------------------------------------

    TEST_CASE("water_get without water reports the default look the renderer uses")
    {
        WaterFixture fixture;

        const json out = fixture.callOk("water_get", json::object());
        CHECK(out["ocean"].is_null());
        CHECK(out["lookSource"] == "defaults");
        CHECK(out["look"] == watertool::lookJson(watertool::Visual{}));
        CHECK(out["look"]["shoreFoam"]["shoreFoamRange"].get<double>() == 0.0);
        CHECK(out["bodies"] == json::array());
        CHECK(listHas(out["notes"], "No water in the scene"));
        CHECK_FALSE(out.contains("ranges"));
    }

    TEST_CASE("water_get reports the ocean and the bodies sorted by id, also in Play mode")
    {
        WaterFixture fixture;
        fixture.createOcean({{"level", -6}});
        const uint64_t lake = fixture.createBody(basinLake({{"level", -2.5}}));
        const uint64_t pool = fixture.createBody({{"center", {30, 0}}, {"halfExtents", {2, 3}}, {"level", 1},
                                                  {"type", "pool"}, {"waves", {"ripples"}}});

        fixture.engine.playMode = true;
        const json out = fixture.callOk("water_get", json::object());

        REQUIRE(out["ocean"].is_object());
        CHECK(out["ocean"]["entity"] == fixture.engine.tracked);
        CHECK(out["ocean"]["name"] == "Ocean");
        CHECK(out["ocean"]["visible"] == true);
        CHECK(out["ocean"]["params"]["level"].get<float>() == -6.0f);
        CHECK(out["ocean"]["params"] == watertool::oceanParamsJson(fixture.engine.oceanState()));
        CHECK(out["lookSource"] == "ocean");
        // A created ocean carries the component's look (shore foam 3 m), not OceanVisualSettings{}.
        CHECK(out["look"]["shoreFoam"]["shoreFoamRange"].get<float>() == 3.0f);

        REQUIRE(out["bodies"].size() == 2);
        CHECK(out["bodies"][0]["entity"] == lake);
        CHECK(out["bodies"][1]["entity"] == pool);
        const json& body = out["bodies"][1];
        CHECK(body["name"] == "Pool");
        CHECK(body["type"] == "pool");
        CHECK(body["center"] == json::array({30, 0}));
        CHECK(body["halfExtents"] == json::array({2, 3}));
        CHECK(body["level"].get<float>() == 1.0f);
        CHECK(body["depth"].get<float>() == 2.0f); // flat ground at 0: 1 m below the lowest point
        CHECK(body["waves"] == json::array({"ripples"}));
        CHECK(body["physicsEnabled"] == true);
        CHECK(body["active"] == true);
        CHECK(out["notes"] == json::array());
    }

    TEST_CASE("water_get notes what does not show and why")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const uint64_t lake = fixture.createBody(basinLake({{"level", -2.5}, {"waves", {"swell"}}}));
        fixture.callOk("ocean_enable", {{"enabled", false}});
        const uint64_t group = fixture.engine.addNode("Group", kRootId);
        fixture.engine.reparent(lake, group);
        fixture.engine.oceanState().weatherDriven = true;
        fixture.engine.loadPending = true;

        json out = fixture.callOk("water_get", json::object());
        CHECK(listHas(out["notes"], "The ocean is hidden"));
        CHECK(listHas(out["notes"], "weatherDriven is on but the weather is disabled"));
        CHECK(listHas(out["notes"], "bodies are placed by their LOCAL position"));
        CHECK(listHas(out["notes"], "has waves, but they are the ocean's FFT bands"));
        CHECK(listHas(out["notes"], "A scene load is pending"));

        fixture.engine.weatherEnabled = true;
        out = fixture.callOk("water_get", json::object());
        CHECK(listHas(out["notes"], "the live weather sets the sea state"));
    }

    TEST_CASE("water_get notes when more bodies are active than clip the ocean")
    {
        WaterFixture fixture;
        for (int i = 0; i < 9; ++i)
        {
            fixture.engine.addBody("Pool " + std::to_string(i), glm::vec3(10.0f * static_cast<float>(i), 0.0f, 0.0f));
        }
        const json out = fixture.callOk("water_get", json::object());
        CHECK(out["bodies"].size() == 9);
        CHECK(listHas(out["notes"], "Only the 8 bodies nearest the camera"));
    }

    TEST_CASE("water_get ranges:true lists every range, and unknown arguments are refused")
    {
        WaterFixture fixture;

        const json out = fixture.callOk("water_get", {{"ranges", true}});
        REQUIRE(out.contains("ranges"));
        CHECK(out["ranges"] == watertool::rangesJson());
        CHECK(out["ranges"]["look"].size() == 9);
        CHECK(out["ranges"]["bands"]["resolution"]["values"] == json::array({64, 128, 256, 512}));
        CHECK(out["ranges"]["bands"]["windDirection"]["max"].get<double>() == 1.0e6);
        CHECK(out["ranges"]["ocean"]["windDirection"]["max"].get<double>() == 720.0);
        CHECK(out["ranges"]["ocean"]["seaState"]["max"].get<double>() == 12.0);
        CHECK(out["ranges"]["body"]["halfExtents"]["max"].get<double>() == 5000.0);

        CHECK(textHas(fixture.argError("water_get", {{"range", true}}), "unknown argument 'range'"));
    }

    // ------------------------------------------------------------------
    // water_create_body
    // ------------------------------------------------------------------

    TEST_CASE("water_create_body measures levelAboveTerrain from the lowest point, the centre or the rim")
    {
        WaterFixture fixture;

        struct Case
        {
            const char* reference;
            double above;
            float level;
        };
        for (const Case& entry : {Case{"lowest", 1.0, -6.0f}, Case{"center", 0.5, -4.5f}, Case{"rim", -0.5, -2.5f}})
        {
            CAPTURE(entry.reference);
            const json out = fixture.callOk("water_create_body", basinLake({{"levelAboveTerrain", entry.above},
                                                                             {"terrainReference", entry.reference}}));
            CHECK(out["body"]["level"].get<float>() == entry.level);
            CHECK(out["terrain"] == kTerrainId);
            const json& footprint = out["footprint"];
            CHECK(footprint["samples"] == 121);
            CHECK(footprint["valid"] == 121);
            CHECK(footprint["min"].get<float>() == -7.0f);
            CHECK(footprint["max"].get<float>() == -2.0f);
            CHECK(footprint["center"].get<float>() == -5.0f);
            CHECK(footprint["rimMin"].get<float>() == -2.0f);
            // Without 'depth' the floor sits 1 m below the lowest terrain point.
            CHECK(out["body"]["depth"].get<float>() == entry.level + 7.0f + 1.0f);
            CHECK(out["warnings"] == json::array());

            // The transform carries the level; the component's offset stays 0.
            const Node& created = fixture.engine.node(out["entity"].get<uint64_t>());
            CHECK(created.name == "Lake");
            CHECK(created.parent == kRootId);
            CHECK(created.transform.position == glm::vec3(-24.0f, entry.level, 16.0f));
            REQUIRE(created.body.has_value());
            CHECK(created.body->waterHeight == 0.0f);
            CHECK(created.body->halfExtents == glm::vec2(5.0f));
            CHECK(created.body->type == 0u);
            CHECK(created.body->bandMask == 0u);
        }

        // terrainReference defaults to lowest.
        const json out = fixture.callOk("water_create_body", basinLake({{"levelAboveTerrain", 1.0}}));
        CHECK(out["body"]["level"].get<float>() == -6.0f);
        CHECK(fixture.engine.undoStack.size() == 4);
    }

    TEST_CASE("water_create_body: a footprint grid that steps over the exact centre still measures it")
    {
        WaterFixture fixture;

        // 2.5 x 2.5 m at 1 m spacing is a 4 x 4 grid (38.75, 39.58, 40.42, 41.25 on each axis), which misses the
        // 0.2 m well at its centre; the appended centre sample finds it, so 'lowest' is never above 'center'.
        const json well{{"center", {40, 40}}, {"halfExtents", {1.25, 1.25}}};
        for (const char* reference : {"lowest", "center"})
        {
            CAPTURE(reference);
            json args = well;
            args["levelAboveTerrain"] = 1;
            args["terrainReference"] = reference;
            const json out = fixture.callOk("water_create_body", args);
            CHECK(out["body"]["level"].get<float>() == -8.0f);
            CHECK(out["footprint"] == json{{"samples", 16}, {"valid", 16}, {"min", -9}, {"max", 0}, {"center", -9},
                                           {"rimMin", 0}});
            CHECK(out["body"]["depth"].get<float>() == 2.0f); // 1 m below the centre, the lowest point
            CHECK(out["warnings"] == json::array());
        }

        json args = well;
        args["levelAboveTerrain"] = -0.5;
        args["terrainReference"] = "rim";
        CHECK(fixture.callOk("water_create_body", args)["body"]["level"].get<float>() == -0.5f);
    }

    TEST_CASE("water_create_body warns when the water would show past the rim, lie under the ground or leave the terrain")
    {
        WaterFixture fixture;

        SUBCASE("above the rim")
        {
            const json out = fixture.callOk("water_create_body", basinLake({{"level", 3}}));
            CHECK(listHas(out["warnings"],
                          "(level 3.0) stands 5.0 m above the lowest point of the rectangle's edge (-2.0)"));
            CHECK(out["body"]["depth"].get<float>() == 11.0f);
        }
        SUBCASE("below the terrain everywhere")
        {
            const json out = fixture.callOk("water_create_body", basinLake({{"level", -8}}));
            CHECK(listHas(out["warnings"], "is below the terrain everywhere"));
            CHECK_FALSE(listHas(out["warnings"], "above the lowest point"));
            CHECK(out["body"]["depth"].get<float>() == 0.5f); // clamped
        }
        SUBCASE("partly off the terrain")
        {
            // x from 56.5 to 66.5: the three columns past 64 are off the terrain.
            const json out = fixture.callOk("water_create_body",
                                            {{"center", {61.5, 0}}, {"halfExtents", {5, 5}}, {"level", 0}});
            CHECK(out["warnings"].size() == 1);
            CHECK(listHas(out["warnings"], "33 of 121 footprint samples are off the terrain"));
            CHECK(out["footprint"]["valid"] == 88);
        }
        SUBCASE("wholly off the terrain")
        {
            const json out = fixture.callOk("water_create_body",
                                            {{"center", {200, 200}}, {"halfExtents", {5, 5}}, {"level", 0}});
            CHECK(listHas(out["warnings"], "No terrain height was found under the rectangle: it lies outside the terrain."));
            CHECK(out["footprint"]["min"].is_null());
            CHECK(out["footprint"]["center"].is_null());
            CHECK(out["body"]["depth"].get<float>() == 10.0f); // the component's default
        }
        SUBCASE("a zero half extent")
        {
            const json out = fixture.callOk("water_create_body",
                                            {{"center", {-24, 16}}, {"halfExtents", {0, 5}}, {"level", -2.5}});
            CHECK(listHas(out["warnings"], "A zero half extent"));
        }
        SUBCASE("waves without a visible ocean")
        {
            json out = fixture.callOk("water_create_body", basinLake({{"level", -2.5}, {"waves", {"swell", "ripples"}}}));
            CHECK(listHas(out["warnings"], "'waves' are the ocean's FFT bands"));
            CHECK(out["body"]["waves"] == json::array({"swell", "ripples"}));

            fixture.createOcean();
            out = fixture.callOk("water_create_body", basinLake({{"level", -2.5}, {"waves", {"swell"}}}));
            CHECK_FALSE(listHas(out["warnings"], "'waves' are the ocean's FFT bands"));
        }
        SUBCASE("an explicit depth and a pool")
        {
            const json out = fixture.callOk("water_create_body", basinLake({{"level", -2.5}, {"depth", 4}, {"type", "pool"}}));
            CHECK(out["body"]["depth"].get<float>() == 4.0f);
            CHECK(out["body"]["name"] == "Pool");
            CHECK(fixture.engine.bodyOf(out["entity"].get<uint64_t>()).type == 1u);
        }
        SUBCASE("no terrain in the scene")
        {
            fixture.engine.liveTerrains = 0;
            const json out = fixture.callOk("water_create_body", basinLake({{"level", 2}}));
            CHECK(out["terrain"].is_null());
            CHECK(out["footprint"].is_null());
            CHECK(out["body"]["depth"].get<float>() == 10.0f);
            CHECK(out["warnings"] == json::array());
        }
    }

    TEST_CASE("water_create_body refusals change nothing")
    {
        WaterFixture fixture;
        const json lake = basinLake({{"level", 1}});

        SUBCASE("World mode")
        {
            fixture.engine.worldMode = true;
            CHECK(textHas(fixture.failure("water_create_body", lake), "World mode"));
        }
        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(textHas(fixture.failure("water_create_body", lake), "Play mode"));
        }
        SUBCASE("a pending scene load")
        {
            fixture.engine.loadPending = true;
            CHECK(textHas(fixture.failure("water_create_body", lake), "scene load"));
        }
        SUBCASE("argument errors")
        {
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"rotation", 45}})),
                          "unknown argument 'rotation'"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"levelAboveTerrain", 1}})),
                          "not both"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake()), "pass 'level'"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"terrainReference", "rim"}})),
                          "'terrainReference' only applies with 'levelAboveTerrain'"));
            CHECK(textHas(fixture.argError("water_create_body", {{"center", {0, 0}}, {"halfExtents", {5}}, {"level", 1}}),
                          "'halfExtents' must be [halfX, halfZ]"));
            CHECK(textHas(fixture.argError("water_create_body", {{"center", {0, 0}}, {"halfExtents", {-1, 5}}, {"level", 1}}),
                          "halfExtents[0]"));
            CHECK(textHas(fixture.argError("water_create_body",
                                           {{"center", {0, 0}}, {"halfExtents", {6000, 5}}, {"level", 1}}),
                          "halfExtents[0]"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"waves", {"foam"}}})), "waves[0]"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"type", "river"}})),
                          "argument 'type' must be one of"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 1}, {"name", ""}})), "must not be empty"));
            CHECK(textHas(fixture.argError("water_create_body", basinLake({{"level", 2e6}})), "argument 'level' must be between"));
        }
        SUBCASE("levelAboveTerrain without a terrain")
        {
            fixture.engine.liveTerrains = 0;
            CHECK(textHas(fixture.failure("water_create_body", basinLake({{"levelAboveTerrain", 1}})), "No terrain in the scene"));
        }
        SUBCASE("levelAboveTerrain off the terrain")
        {
            CHECK(textHas(fixture.failure("water_create_body",
                                          {{"center", {200, 200}}, {"halfExtents", {5, 5}}, {"levelAboveTerrain", 1}}),
                          "No terrain height was found under the rectangle for terrainReference 'lowest'"));
        }
        SUBCASE("two terrains need 'terrain' for levelAboveTerrain")
        {
            fixture.engine.liveTerrains = 2;
            CHECK(textHas(fixture.failure("water_create_body", basinLake({{"levelAboveTerrain", 1}})), "pass 'terrain'"));
        }

        CHECK(fixture.engine.undoStack.empty());
        for (const auto& entry : fixture.engine.nodes)
        {
            CAPTURE(entry.first);
            CHECK_FALSE(entry.second.body.has_value());
        }
    }

    TEST_CASE("water_create_body picks the terrain to sample")
    {
        WaterFixture fixture;
        fixture.engine.liveTerrains = 2;

        // Two terrains and an absolute level: nothing to sample, no footprint.
        json out = fixture.callOk("water_create_body", basinLake({{"level", 1}}));
        CHECK(out["terrain"].is_null());
        CHECK(out["footprint"].is_null());

        // 'terrain' picks one.
        out = fixture.callOk("water_create_body", basinLake({{"levelAboveTerrain", 1}, {"terrain", 3}}));
        CHECK(out["terrain"] == 3);
        CHECK(fixture.engine.lastHeightTerrain == 3);
        CHECK(out["body"]["level"].get<float>() == -6.0f);
    }

    TEST_CASE("water_create_body without a terrain service")
    {
        WaterFixture fixture(/*terrainService=*/false);

        const json out = fixture.callOk("water_create_body", basinLake({{"level", 1}}));
        CHECK(out["footprint"].is_null());
        CHECK(textHas(fixture.failure("water_create_body", basinLake({{"levelAboveTerrain", 1}})), "no terrain service"));
    }

    TEST_CASE("water_create_body is one undo step: undo deletes the body, redo re-creates it under a new id")
    {
        WaterFixture fixture;

        const json out = fixture.callOk("water_create_body", basinLake({{"levelAboveTerrain", -0.5}, {"terrainReference", "rim"},
                                                                         {"waves", {"ripples"}}, {"physicsEnabled", false}}));
        const uint64_t id = out["entity"].get<uint64_t>();
        const Node before = fixture.engine.node(id);
        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(batchSize(fixture.engine.undoStack.back()) == 1);

        fixture.engine.undoStack.back()->undo();
        CHECK_FALSE(fixture.engine.alive(id));

        fixture.engine.undoStack.back()->execute();
        const uint64_t again = mcp::undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(id));
        CHECK(again != id);
        const Node& after = fixture.engine.node(again);
        CHECK(after.name == before.name);
        CHECK(after.parent == kRootId);
        CHECK(after.transform == before.transform);
        REQUIRE(after.body.has_value());
        CHECK(watertool::sameBodyData(*after.body, *before.body));
    }

    // ------------------------------------------------------------------
    // water_set_body
    // ------------------------------------------------------------------

    TEST_CASE("water_set_body passes a water_get body back unchanged")
    {
        WaterFixture fixture;
        const uint64_t lake = fixture.createBody(basinLake({{"level", 1}}));

        // An editor-made offset: the reported level is transform Y + waterHeight, and re-deriving the transform
        // from it (0.3f - 0.2f) would not give 0.1f back - the echo must leave the transform alone.
        Node& stored = fixture.engine.node(lake);
        stored.body->waterHeight = 0.2f;
        stored.transform.position.y = 0.1f;
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        const json body = fixture.callOk("water_get", json::object())["bodies"][0];
        const json out = fixture.callOk("water_set_body", body);
        CHECK(out["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
        CHECK(stored.transform.position.y == 0.1f);
        CHECK(stored.body->waterHeight == 0.2f);
        CHECK(out["body"] == body);
    }

    TEST_CASE("water_set_body edits the component, the transform and the name, each undoable")
    {
        WaterFixture fixture;
        const uint64_t lake = fixture.createBody(basinLake({{"level", 1}}));
        const services::WaterBodyComponentData original = fixture.engine.bodyOf(lake);
        const services::TransformData originalTransform = fixture.engine.node(lake).transform;
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        SUBCASE("component")
        {
            const json out = fixture.callOk("water_set_body", {{"entity", lake}, {"depth", 3}, {"waves", {"ripples", "swell"}}});
            CHECK(out["changed"] == true);
            CHECK(fixture.engine.bodyOf(lake).depth == 3.0f);
            CHECK(fixture.engine.bodyOf(lake).bandMask == 5u);
            CHECK(fixture.engine.node(lake).transform == originalTransform);
            CHECK(listHas(out["warnings"], "'waves' are the ocean's FFT bands"));
            REQUIRE(fixture.engine.undoStack.size() == undoBefore + 1);
            CHECK(batchSize(fixture.engine.undoStack.back()) == 1);

            fixture.engine.undoStack.back()->undo();
            CHECK(watertool::sameBodyData(fixture.engine.bodyOf(lake), original));
            fixture.engine.undoStack.back()->execute();
            CHECK(fixture.engine.bodyOf(lake).depth == 3.0f);
        }
        SUBCASE("transform")
        {
            const json out = fixture.callOk("water_set_body", {{"entity", lake}, {"center", {0, 0}}, {"level", 2}});
            CHECK(out["changed"] == true);
            CHECK(fixture.engine.node(lake).transform.position == glm::vec3(0.0f, 2.0f, 0.0f));
            CHECK(watertool::sameBodyData(fixture.engine.bodyOf(lake), original));
            CHECK(out["body"]["center"] == json::array({0, 0}));
            CHECK(batchSize(fixture.engine.undoStack.back()) == 1);

            fixture.engine.undoStack.back()->undo();
            CHECK(fixture.engine.node(lake).transform == originalTransform);
        }
        SUBCASE("name")
        {
            const json out = fixture.callOk("water_set_body", {{"entity", lake}, {"name", "Pond"}});
            CHECK(fixture.engine.node(lake).name == "Pond");
            CHECK(out["body"]["name"] == "Pond");
            fixture.engine.undoStack.back()->undo();
            CHECK(fixture.engine.node(lake).name == "Lake");
        }
        SUBCASE("all three are one undo step")
        {
            fixture.callOk("water_set_body", {{"entity", lake}, {"name", "Pond"}, {"depth", 3}, {"center", {0, 0}}});
            REQUIRE(fixture.engine.undoStack.size() == undoBefore + 1);
            CHECK(batchSize(fixture.engine.undoStack.back()) == 3);

            fixture.engine.undoStack.back()->undo();
            CHECK(fixture.engine.node(lake).name == "Lake");
            CHECK(fixture.engine.node(lake).transform == originalTransform);
            CHECK(watertool::sameBodyData(fixture.engine.bodyOf(lake), original));
        }
        SUBCASE("a level change keeps the component's offset")
        {
            fixture.engine.bodyOf(lake).waterHeight = 0.5f;
            fixture.callOk("water_set_body", {{"entity", lake}, {"level", 4}});
            CHECK(fixture.engine.node(lake).transform.position.y == 3.5f);
            CHECK(fixture.engine.bodyOf(lake).waterHeight == 0.5f);
        }
        SUBCASE("levelAboveTerrain measures the rectangle where it ends up")
        {
            json out = fixture.callOk("water_set_body", {{"entity", lake}, {"center", {30, 0}}, {"levelAboveTerrain", 0.25}});
            CHECK(fixture.engine.node(lake).transform.position == glm::vec3(30.0f, 0.25f, 0.0f));
            CHECK(out["footprint"]["min"].get<float>() == 0.0f);

            out = fixture.callOk("water_set_body", {{"entity", lake}, {"center", {-24, 16}}, {"levelAboveTerrain", -0.5},
                                                    {"terrainReference", "rim"}});
            CHECK(fixture.engine.node(lake).transform.position == glm::vec3(-24.0f, -2.5f, 16.0f));
            CHECK(out["body"]["level"].get<float>() == -2.5f);
        }
    }

    TEST_CASE("water_set_body refusals, and a failed write rolls the whole call back")
    {
        WaterFixture fixture;
        const uint64_t lake = fixture.createBody(basinLake({{"level", 1}}));
        const services::TransformData originalTransform = fixture.engine.node(lake).transform;
        const services::WaterBodyComponentData original = fixture.engine.bodyOf(lake);
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        SUBCASE("not a water body")
        {
            const uint64_t rock = fixture.engine.addNode("Rock", kRootId);
            CHECK(textHas(fixture.failure("water_set_body", {{"entity", rock}, {"depth", 3}}), "has no water body"));
        }
        SUBCASE("unknown key")
        {
            CHECK(textHas(fixture.argError("water_set_body", {{"entity", lake}, {"rotation", 3}}), "unknown argument 'rotation'"));
        }
        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(textHas(fixture.failure("water_set_body", {{"entity", lake}, {"depth", 3}}), "Play mode"));
        }
        SUBCASE("levelAboveTerrain without a terrain")
        {
            fixture.engine.liveTerrains = 0;
            CHECK(textHas(fixture.failure("water_set_body", {{"entity", lake}, {"levelAboveTerrain", 1}}),
                          "No terrain in the scene"));
        }
        SUBCASE("rollback")
        {
            fixture.engine.failBodyWrites = true;
            CHECK(textHas(fixture.failure("water_set_body", {{"entity", lake}, {"center", {0, 0}}, {"depth", 3}}), "rolled back"));
            CHECK(fixture.engine.node(lake).transform == originalTransform);
        }

        CHECK(fixture.engine.undoStack.size() == undoBefore);
        CHECK(watertool::sameBodyData(fixture.engine.bodyOf(lake), original));
    }

    // ------------------------------------------------------------------
    // water_set_params
    // ------------------------------------------------------------------

    TEST_CASE("water_set_params with no ocean keeps the look in a new HIDDEN ocean seeded with the lakes' look")
    {
        WaterFixture fixture;
        fixture.createBody(basinLake({{"level", -2.5}}));

        const json out = fixture.callOk("water_set_params", {{"color", {{"opacity", 0.5}}}});
        CHECK(out["changed"] == true);
        CHECK(out["createdHiddenOcean"] == true);
        CHECK(out["ocean"] == fixture.engine.tracked);
        CHECK(out["lookSource"] == "ocean");
        CHECK(out["diff"] == json{{"color", {{"opacity", 0.5}}}});
        CHECK(listHas(out["warnings"], "a hidden ocean entity now holds the water look"));

        REQUIRE(fixture.engine.createdOceans.size() == 1);
        CHECK(fixture.engine.createdOceans[0].oceanConfig.enabled);
        CHECK(fixture.engine.createdOceans[0].waterHeight == 0.0f);
        const services::OceanStateData created = fixture.engine.oceanState();
        CHECK_FALSE(created.isActive);
        // The look the lakes had (OceanVisualSettings{}: no shore foam), not the component's (3 m).
        services::OceanVisualSettings expected;
        expected.shallowColor.w = 0.5f;
        CHECK(created.visual == expected);
        CHECK(out["look"]["shoreFoam"]["shoreFoamRange"].get<double>() == 0.0);

        // One undo step: undo removes the ocean again, redo brings the same hidden ocean back.
        REQUIRE(fixture.engine.undoStack.size() == 2);
        CHECK(batchSize(fixture.engine.undoStack.back()) == 1);
        const uint64_t first = fixture.engine.tracked;

        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.tracked == 0);
        CHECK_FALSE(fixture.engine.alive(first));
        const json after = fixture.callOk("water_get", json::object());
        CHECK(after["ocean"].is_null());
        CHECK(after["lookSource"] == "defaults");

        fixture.engine.undoStack.back()->execute();
        REQUIRE(fixture.engine.tracked != 0);
        CHECK(fixture.engine.oceanState() == created);
        CHECK(mcp::undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(first)) == fixture.engine.tracked);
    }

    TEST_CASE("water_set_params passes a water_get look back unchanged")
    {
        WaterFixture fixture;

        SUBCASE("no ocean: nothing is created")
        {
            const json look = fixture.callOk("water_get", json::object())["look"];
            const json out = fixture.callOk("water_set_params", look);
            CHECK(out["changed"] == false);
            CHECK(out["createdHiddenOcean"] == false);
            CHECK(out["ocean"].is_null());
            CHECK(out["lookSource"] == "defaults");
            CHECK(fixture.engine.tracked == 0);
            CHECK(fixture.engine.createdOceans.empty());
            CHECK(fixture.engine.undoStack.empty());
        }
        SUBCASE("an ocean")
        {
            fixture.createOcean();
            const std::size_t undoBefore = fixture.engine.undoStack.size();
            const json look = fixture.callOk("water_get", json::object())["look"];
            const json out = fixture.callOk("water_set_params", look);
            CHECK(out["changed"] == false);
            CHECK(out["diff"] == json::object());
            CHECK(fixture.engine.undoStack.size() == undoBefore);
        }
    }

    TEST_CASE("water_set_params is strict and all-or-nothing")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const services::OceanStateData before = fixture.engine.oceanState();
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        CHECK(textHas(fixture.argError("water_set_params", {{"color", {{"opacity", 0.5}}},
                                                            {"refraction", {{"refractionStrength", 9}}}}),
                      "refraction.refractionStrength"));
        CHECK(textHas(fixture.argError("water_set_params", {{"colour", {{"opacity", 0.5}}}}), "unknown argument 'colour'"));
        CHECK(textHas(fixture.argError("water_set_params", {{"color", {{"alpha", 1}}}}), "unknown color key 'alpha'"));
        CHECK(textHas(fixture.argError("water_set_params", {{"reflections", {{"ssrEnabled", 1}}}}), "reflections.ssrEnabled"));
        CHECK(textHas(fixture.argError("water_set_params", {{"antiTiling", {{"hexBandMask", 8}}}}), "antiTiling.hexBandMask"));
        CHECK(textHas(fixture.argError("water_set_params", {{"color", {{"shallowColor", {1, 2}}}}}), "color.shallowColor"));
        CHECK(textHas(fixture.argError("water_set_params", json::object()), "nothing to change"));

        CHECK(fixture.engine.oceanState() == before);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
    }

    TEST_CASE("water_set_params accepts every look key at its minimum and at its maximum")
    {
        WaterFixture fixture;
        fixture.createOcean();

        for (bool max : {false, true})
        {
            CAPTURE(max);
            json args = json::object();
            for (const auto& section : watertool::lookSections())
            {
                args[std::string(section.name)] = boundPatch(section, max);
            }
            const json out = fixture.callOk("water_set_params", args);
            for (const auto& section : watertool::lookSections())
            {
                for (const auto& field : section.fields)
                {
                    CAPTURE(section.name);
                    CAPTURE(field.key);
                    CHECK(out["look"][std::string(section.name)][std::string(field.key)] == boundValue(field, max));
                }
            }
        }
    }

    TEST_CASE("water_set_params: opacity is the shallow colour's alpha, and the fields it does not expose keep their values")
    {
        WaterFixture fixture;
        fixture.createOcean();
        services::OceanStateData& state = fixture.engine.oceanState();
        state.visual.deepColor.w = 0.33f;
        state.visual.maxVisibleDepth = 42.0f;
        state.visual.ssrDebugView = true;
        state.physics.density = 1025.0f;
        state.gravity = 9.5f;

        fixture.callOk("water_set_params", {{"color", {{"opacity", 0.25}}}});
        CHECK(fixture.engine.oceanState().visual.shallowColor == glm::vec4(0.0f, 0.4f, 0.6f, 0.25f));

        fixture.callOk("water_set_params", {{"color", {{"shallowColor", {0.1, 0.2, 0.3}}, {"deepColor", {0.4, 0.5, 0.6}}}}});
        CHECK(fixture.engine.oceanState().visual.shallowColor == glm::vec4(0.1f, 0.2f, 0.3f, 0.25f));
        CHECK(fixture.engine.oceanState().visual.deepColor == glm::vec4(0.4f, 0.5f, 0.6f, 0.33f));

        fixture.callOk("water_set_params", {{"antiTiling", {{"hexBandMask", 5}}}, {"reflections", {{"ssrIntensity", 0.5}}}});
        const services::OceanStateData& after = fixture.engine.oceanState();
        CHECK(after.visual.hexBandMask == 5u);
        CHECK(after.visual.ssrIntensity == 0.5f);
        CHECK(after.visual.maxVisibleDepth == 42.0f);
        CHECK(after.visual.ssrDebugView);
        CHECK(after.physics.density == 1025.0f);
        CHECK(after.gravity == 9.5f);
        CHECK(after.isActive);
    }

    TEST_CASE("undoing water_set_params restores only the sections it changed")
    {
        WaterFixture fixture;
        fixture.createOcean();
        fixture.callOk("water_set_params", {{"color", {{"fresnelPower", 7}}}});
        REQUIRE(fixture.engine.undoStack.size() == 2);

        // The Ocean Editor changes another section, and the deep alpha, in between.
        fixture.engine.oceanState().visual.shoreFoamRange = 9.0f;
        fixture.engine.oceanState().visual.deepColor.w = 0.5f;

        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.oceanState().visual.fresnelPower == 5.0f);
        CHECK(fixture.engine.oceanState().visual.shoreFoamRange == 9.0f);
        CHECK(fixture.engine.oceanState().visual.deepColor.w == 0.5f);

        fixture.engine.undoStack.back()->execute();
        CHECK(fixture.engine.oceanState().visual.fresnelPower == 7.0f);
        CHECK(fixture.engine.oceanState().visual.shoreFoamRange == 9.0f);
    }

    TEST_CASE("water_set_params warns where a section does not apply")
    {
        WaterFixture fixture;

        SUBCASE("no visible ocean")
        {
            const json out = fixture.callOk("water_set_params", {{"caustics", {{"causticStrength", 2}}},
                                                                 {"shoreline", {{"shoalingEnabled", true}}},
                                                                 {"antiTiling", {{"hexTilingEnabled", true}}}});
            CHECK(listHas(out["warnings"], "caustics show only on the ground under the visible ocean"));
            CHECK(listHas(out["warnings"], "applies to the visible ocean only"));
            CHECK(listHas(out["warnings"], "antiTiling acts on the ocean's FFT waves"));
            CHECK_FALSE(listHas(out["warnings"], "shoreline effects need terrain"));
        }
        SUBCASE("no terrain")
        {
            fixture.engine.liveTerrains = 0;
            fixture.createOcean();
            const json out = fixture.callOk("water_set_params", {{"shoreline", {{"shoreWavesEnabled", true}}}});
            CHECK(listHas(out["warnings"], "shoreline effects need terrain"));
            CHECK_FALSE(listHas(out["warnings"], "applies to the visible ocean only"));
        }
        SUBCASE("a visible ocean over terrain")
        {
            fixture.createOcean();
            const json out = fixture.callOk("water_set_params", {{"caustics", {{"causticStrength", 2}}},
                                                                 {"shoreline", {{"shoreWavesEnabled", true}}}});
            CHECK(out["warnings"] == json::array());
        }
    }

    TEST_CASE("water_set_params deletes a half-made hidden ocean again, and refuses in Play mode")
    {
        WaterFixture fixture;

        SUBCASE("the state write is refused")
        {
            fixture.engine.refuseStateWrites = true;
            CHECK(textHas(fixture.failure("water_set_params", {{"color", {{"opacity", 0.5}}}}), "The water look was not changed"));
            CHECK(fixture.engine.createdOceans.size() == 1);
            CHECK(fixture.engine.tracked == 0);
            CHECK(fixture.engine.nodes.size() == 1); // the root alone
        }
        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(textHas(fixture.failure("water_set_params", {{"color", {{"opacity", 0.5}}}}), "Play mode"));
            CHECK(fixture.engine.createdOceans.empty());
        }
        SUBCASE("a pending scene load")
        {
            fixture.engine.loadPending = true;
            CHECK(textHas(fixture.failure("water_set_params", {{"color", {{"opacity", 0.5}}}}), "scene load"));
        }
        CHECK(fixture.engine.undoStack.empty());
    }

    // ------------------------------------------------------------------
    // ocean_enable
    // ------------------------------------------------------------------

    TEST_CASE("ocean_enable creates the ocean with the Ocean Editor's defaults, as one undo step")
    {
        WaterFixture fixture;

        const json out = fixture.callOk("ocean_enable", {{"enabled", true}});
        CHECK(out["created"] == true);
        CHECK(out["changed"] == true);
        CHECK(out["removed"] == false);
        CHECK(out["ocean"]["visible"] == true);
        CHECK(out["ocean"]["params"]["level"].get<float>() == 0.0f);
        CHECK(out["terrainReference"].is_null());

        REQUIRE(fixture.engine.createdOceans.size() == 1);
        const services::OceanCreationData& config = fixture.engine.createdOceans[0];
        const services::OceanCreationData editor{}; // OceanEditorWindow::createOcean
        CHECK(config.waterHeight == 0.0f);
        CHECK(config.physicsEnabled == editor.physicsEnabled);
        CHECK(config.shallowColor == editor.shallowColor);
        CHECK(config.deepColor == editor.deepColor);
        services::OceanFFTConfigData bands;
        bands.enabled = true;
        CHECK(config.oceanConfig == bands);
        CHECK(fixture.engine.oceanState().bands[0].patchSize == 100.0f);

        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(batchSize(fixture.engine.undoStack.back()) == 1);
        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.tracked == 0);
        fixture.engine.undoStack.back()->execute();
        REQUIRE(fixture.engine.tracked != 0);
        CHECK(fixture.engine.oceanState().isActive);
    }

    TEST_CASE("ocean_enable places a new ocean at a level, above the terrain's lowest point or above a terrain point")
    {
        WaterFixture fixture;

        SUBCASE("level")
        {
            fixture.callOk("ocean_enable", {{"enabled", true}, {"level", -6}});
            CHECK(fixture.engine.oceanState().waterHeight == -6.0f);
        }
        SUBCASE("the terrain's lowest point")
        {
            const json out = fixture.callOk("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 2}});
            CHECK(fixture.engine.oceanState().waterHeight == -8.0f);
            CHECK(out["terrainReference"] == json{{"terrain", kTerrainId}, {"point", "lowest"}, {"height", -10}});
        }
        SUBCASE("a terrain point")
        {
            const json out = fixture.callOk("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 1},
                                                             {"terrainPoint", {-24, 16}}});
            CHECK(fixture.engine.oceanState().waterHeight == -4.0f);
            CHECK(out["terrainReference"]["point"] == json::array({-24, 16}));
            CHECK(out["terrainReference"]["height"].get<float>() == -5.0f);
        }
        SUBCASE("refusals")
        {
            CHECK(textHas(fixture.argError("ocean_enable", {{"enabled", true}, {"terrainPoint", {0, 0}}}),
                          "'terrainPoint' only applies with 'levelAboveTerrain'"));
            CHECK(textHas(fixture.argError("ocean_enable", {{"enabled", true}, {"terrain", kTerrainId}}),
                          "'terrain' only applies with 'levelAboveTerrain'"));
            CHECK(textHas(fixture.argError("ocean_enable", {{"enabled", true}, {"level", 1}, {"levelAboveTerrain", 1}}),
                          "not both"));
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 1},
                                                           {"terrainPoint", {200, 0}}}),
                          "has no terrain height"));
            fixture.engine.liveTerrains = 0;
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 1}}),
                          "No terrain in the scene"));
            CHECK(fixture.engine.tracked == 0);
            CHECK(fixture.engine.undoStack.empty());
        }
    }

    TEST_CASE("ocean_enable seaState sets the new ocean's bands from one Beaufort number")
    {
        WaterFixture fixture;

        const json out = fixture.callOk("ocean_enable", {{"enabled", true}, {"seaState", 5}});
        const services::OceanStateData& state = fixture.engine.oceanState();
        checkSeaStateBands(state, water::seaStateFromBeaufort(5.0f, 45.0f)); // the default swell direction
        CHECK(state.currentBeaufort == 5.0f);
        CHECK(out["ocean"]["seaState"].get<float>() == 5.0f);

        // What a sea state never touches keeps the creation values.
        const services::OceanFFTConfigData defaults;
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            CAPTURE(i);
            CHECK(state.bands[i].resolution == defaults.bands[i].resolution);
            CHECK(state.bands[i].patchSize == defaults.bands[i].patchSize);
            CHECK(state.bands[i].enabled == defaults.bands[i].enabled);
            CHECK(state.bands[i].foamPersistence == defaults.bands[i].foamPersistence);
            CHECK(state.bands[i].foamDecay == defaults.bands[i].foamDecay);
        }

        // One undo step; redo re-creates the ocean WITH its sea state.
        const services::OceanStateData created = state;
        REQUIRE(fixture.engine.undoStack.size() == 1);
        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.tracked == 0);
        fixture.engine.undoStack.back()->execute();
        CHECK(fixture.engine.oceanState() == created);
    }

    TEST_CASE("ocean_enable hides and shows the ocean and keeps every setting")
    {
        WaterFixture fixture;
        fixture.createOcean({{"level", -6}});
        const services::OceanStateData visible = fixture.engine.oceanState();

        json out = fixture.callOk("ocean_enable", {{"enabled", false}});
        CHECK(out["changed"] == true);
        CHECK(out["ocean"]["visible"] == false);
        CHECK(listHas(out["notes"], "The ocean is hidden"));
        services::OceanStateData hidden = visible;
        hidden.isActive = false;
        CHECK(fixture.engine.oceanState() == hidden);
        REQUIRE(fixture.engine.undoStack.size() == 2);

        // Hiding a hidden ocean changes nothing.
        out = fixture.callOk("ocean_enable", {{"enabled", false}});
        CHECK(out["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == 2);

        // Showing it again, at a new level, is one step.
        out = fixture.callOk("ocean_enable", {{"enabled", true}, {"level", 3}});
        CHECK(out["created"] == false);
        CHECK(out["ocean"]["visible"] == true);
        CHECK(fixture.engine.oceanState().waterHeight == 3.0f);
        REQUIRE(fixture.engine.undoStack.size() == 3);

        // Showing a visible ocean changes nothing.
        out = fixture.callOk("ocean_enable", {{"enabled", true}});
        CHECK(out["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == 3);

        fixture.engine.undoStack[2]->undo();
        CHECK(fixture.engine.oceanState() == hidden);
        fixture.engine.undoStack[1]->undo();
        CHECK(fixture.engine.oceanState() == visible);
    }

    TEST_CASE("ocean_enable remove deletes the ocean, and undo brings the same ocean back under a new id")
    {
        WaterFixture fixture;
        const uint64_t oldId = fixture.createOcean({{"level", -6}, {"seaState", 5}});
        const services::OceanStateData before = fixture.engine.oceanState();
        fixture.createBody(basinLake({{"level", -2.5}}));
        // The ocean is the root's first child: re-creation must put it back there.
        REQUIRE(fixture.engine.nodes.at(kRootId).children.front() == oldId);

        const json out = fixture.callOk("ocean_enable", {{"enabled", false}, {"remove", true}});
        CHECK(out["removed"] == true);
        CHECK(out["changed"] == true);
        CHECK(out["ocean"].is_null());
        CHECK(listHas(out["warnings"], "Lakes and pools now use the default water look"));
        CHECK(fixture.engine.tracked == 0);
        CHECK_FALSE(fixture.engine.alive(oldId));

        fixture.engine.undoStack.back()->undo();
        REQUIRE(fixture.engine.tracked != 0);
        const uint64_t newId = fixture.engine.tracked;
        CHECK(newId != oldId);
        CHECK(mcp::undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(oldId)) == newId);
        CHECK(fixture.engine.oceanState() == before);
        CHECK(fixture.engine.node(newId).name == "Ocean");
        CHECK(fixture.engine.nodes.at(kRootId).children.front() == newId);

        fixture.engine.undoStack.back()->execute();
        CHECK(fixture.engine.tracked == 0);
    }

    TEST_CASE("undo entries recorded before a removal follow the re-created ocean")
    {
        WaterFixture fixture;
        fixture.createOcean();                                    // entry 0: create
        fixture.callOk("ocean_set_params", {{"level", 3}});       // entry 1: level 0 -> 3
        fixture.callOk("ocean_enable", {{"enabled", false}, {"remove", true}}); // entry 2: remove
        REQUIRE(fixture.engine.undoStack.size() == 3);

        fixture.engine.undoStack[2]->undo();
        REQUIRE(fixture.engine.tracked != 0);
        CHECK(fixture.engine.oceanState().waterHeight == 3.0f);

        fixture.engine.undoStack[1]->undo(); // resolves the old id to the re-created ocean
        CHECK(fixture.engine.oceanState().waterHeight == 0.0f);

        fixture.engine.undoStack[0]->undo();
        CHECK(fixture.engine.tracked == 0);
    }

    TEST_CASE("ocean_enable remove is refused while the Ocean entity holds more than the ocean")
    {
        WaterFixture fixture;
        const uint64_t ocean = fixture.createOcean();
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        SUBCASE("a child")
        {
            fixture.engine.addNode("Buoy", ocean);
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", false}, {"remove", true}}), "1 child entity"));
        }
        SUBCASE("another component")
        {
            fixture.engine.node(ocean).extraComponents["mesh"] = {{"meshRef", "meshes/buoy.vfMesh"}};
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", false}, {"remove", true}}), "also carries mesh"));
        }

        CHECK(fixture.engine.tracked == ocean);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
    }

    TEST_CASE("undoing an ocean's creation, or redoing its removal, leaves it in place once it holds more than the ocean")
    {
        // Re-creating the ocean could never bring back what was added to it since, so the delete is refused: the
        // entry throws, McpBatchUndoCommand logs it and the entry is consumed.
        WaterFixture fixture;
        const uint64_t ocean = fixture.createOcean();
        REQUIRE(fixture.engine.undoStack.size() == 1);

        SUBCASE("a child added after the creation")
        {
            const uint64_t buoy = fixture.engine.addNode("Buoy", ocean);
            CHECK_NOTHROW(fixture.engine.undoStack.back()->undo());
            CHECK(fixture.engine.tracked == ocean);
            CHECK(fixture.engine.alive(buoy));
        }
        SUBCASE("a component added after the creation")
        {
            fixture.engine.node(ocean).extraComponents["mesh"] = {{"meshRef", "meshes/buoy.vfMesh"}};
            CHECK_NOTHROW(fixture.engine.undoStack.back()->undo());
            CHECK(fixture.engine.tracked == ocean);
        }
        SUBCASE("a child added to the ocean a removal's undo brought back")
        {
            fixture.callOk("ocean_enable", {{"enabled", false}, {"remove", true}});
            REQUIRE(fixture.engine.undoStack.size() == 2);
            fixture.engine.undoStack.back()->undo();
            REQUIRE(fixture.engine.tracked != 0);
            const uint64_t again = fixture.engine.tracked;
            const uint64_t buoy = fixture.engine.addNode("Buoy", again);

            CHECK_NOTHROW(fixture.engine.undoStack.back()->execute());
            CHECK(fixture.engine.tracked == again);
            CHECK(fixture.engine.alive(buoy));
        }
    }

    TEST_CASE("ocean_enable argument rules and no-ops")
    {
        WaterFixture fixture;

        CHECK(textHas(fixture.argError("ocean_enable", {{"enabled", true}, {"remove", true}}), "'remove' needs enabled:false"));
        CHECK(textHas(fixture.argError("ocean_enable", json::object()), "enabled"));

        // No ocean: hiding or removing changes nothing.
        json out = fixture.callOk("ocean_enable", {{"enabled", false}});
        CHECK(out["changed"] == false);
        CHECK(listHas(out["notes"], "there is no ocean to hide"));
        out = fixture.callOk("ocean_enable", {{"enabled", false}, {"remove", true}});
        CHECK(out["changed"] == false);
        CHECK(listHas(out["notes"], "there is no ocean to remove"));
        CHECK(fixture.engine.createdOceans.empty());
        CHECK(fixture.engine.undoStack.empty());

        fixture.createOcean();
        for (const json& args : {json{{"enabled", false}, {"level", 1}}, json{{"enabled", false}, {"seaState", 3}},
                                 json{{"enabled", false}, {"terrainPoint", {0, 0}}}})
        {
            CAPTURE(args.dump());
            CHECK(textHas(fixture.argError("ocean_enable", args), "with enabled:false only 'remove' applies"));
        }

        fixture.engine.oceanState().weatherDriven = true;
        CHECK(textHas(fixture.argError("ocean_enable", {{"enabled", true}, {"seaState", 3}}),
                      "'seaState' is refused while weatherDriven is on"));
    }

    TEST_CASE("ocean_enable deletes a half-made ocean again, and refuses in Play mode")
    {
        WaterFixture fixture;

        SUBCASE("the sea-state write is refused")
        {
            fixture.engine.refuseStateWrites = true;
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}, {"seaState", 5}}), "The ocean was not created"));
            CHECK(fixture.engine.tracked == 0);
            CHECK(fixture.engine.nodes.size() == 1);
        }
        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}}), "Play mode"));
        }
        SUBCASE("a pending scene load")
        {
            fixture.engine.loadPending = true;
            CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}}), "scene load"));
        }
        CHECK(fixture.engine.undoStack.empty());
    }

    // ------------------------------------------------------------------
    // ocean_set_params
    // ------------------------------------------------------------------

    TEST_CASE("ocean_set_params seaState and windSpeed set every band the way a sea state does")
    {
        WaterFixture fixture;
        fixture.createOcean();

        SUBCASE("seaState")
        {
            const json out = fixture.callOk("ocean_set_params", {{"seaState", 7}});
            CHECK(out["changed"] == true);
            checkSeaStateBands(fixture.engine.oceanState(), water::seaStateFromBeaufort(7.0f, 45.0f));
            CHECK(fixture.engine.oceanState().currentBeaufort == 7.0f);
            CHECK(out["ocean"]["seaState"].get<float>() == 7.0f);
        }
        SUBCASE("windSpeed")
        {
            fixture.callOk("ocean_set_params", {{"windSpeed", 10}});
            const float beaufort = water::beaufortFromWindSpeed(10.0f);
            checkSeaStateBands(fixture.engine.oceanState(), water::seaStateFromBeaufort(beaufort, 45.0f));
            CHECK(sameFloat(fixture.engine.oceanState().currentBeaufort, beaufort));
        }
        SUBCASE("explicit band keys win over the sea state")
        {
            fixture.callOk("ocean_set_params", {{"seaState", 5}, {"bands", {{"swell", {{"windSpeed", 30}}}}}});
            const water::SeaState sea = water::seaStateFromBeaufort(5.0f, 45.0f);
            CHECK(fixture.engine.oceanState().bands[0].windSpeed == 30.0f);
            CHECK(fixture.engine.oceanState().bands[1].windSpeed == sea.bands[1].windSpeed);
        }
        SUBCASE("not both")
        {
            CHECK(textHas(fixture.argError("ocean_set_params", {{"seaState", 3}, {"windSpeed", 5}}), "not both"));
        }
    }

    TEST_CASE("ocean_set_params windDirection turns the swell band and keeps the other bands' offsets")
    {
        WaterFixture fixture;
        fixture.createOcean(); // bands at 45, 60 and 30 degrees

        struct Case
        {
            double direction;
            float swell;
            float agitation;
            float ripples;
        };
        for (const Case& entry : {Case{350.0, 350.0f, 5.0f, 335.0f}, Case{-30.0, 330.0f, 345.0f, 315.0f},
                                  Case{720.0, 0.0f, 15.0f, 345.0f}})
        {
            CAPTURE(entry.direction);
            fixture.callOk("ocean_set_params", {{"windDirection", entry.direction}});
            const services::OceanStateData& state = fixture.engine.oceanState();
            CHECK(state.bands[0].windDirection == entry.swell);
            CHECK(state.bands[1].windDirection == entry.agitation);
            CHECK(state.bands[2].windDirection == entry.ripples);
        }
        CHECK(textHas(fixture.argError("ocean_set_params", {{"windDirection", 721}}), "argument 'windDirection' must be between"));
    }

    TEST_CASE("a sea state may push a band's windDirection past 720 degrees, and the params still echo back")
    {
        WaterFixture fixture;
        fixture.createOcean();

        // A sea state writes the swell direction +/- 15 into the other bands, unwrapped.
        fixture.callOk("ocean_set_params", {{"bands", {{"swell", {{"windDirection", 715}}}}}});
        fixture.callOk("ocean_set_params", {{"seaState", 5}});
        CHECK(fixture.engine.oceanState().bands[0].windDirection == 715.0f);
        CHECK(fixture.engine.oceanState().bands[1].windDirection == 730.0f);
        CHECK(fixture.engine.oceanState().bands[2].windDirection == 700.0f);

        const std::size_t undoBefore = fixture.engine.undoStack.size();
        const json params = fixture.callOk("water_get", json::object())["ocean"]["params"];
        CHECK(fixture.callOk("ocean_set_params", params)["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == undoBefore);

        // A band's own direction takes any value within +/-1e6; the windDirection argument stays +/-720.
        fixture.callOk("ocean_set_params", {{"bands", {{"ripples", {{"windDirection", -1.0e6}}}}}});
        CHECK(fixture.engine.oceanState().bands[2].windDirection == -1.0e6f);
        CHECK(textHas(fixture.argError("ocean_set_params", {{"bands", {{"ripples", {{"windDirection", 1000001}}}}}}),
                      "bands.ripples.windDirection"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"windDirection", 730}}), "argument 'windDirection' must be between"));
    }

    TEST_CASE("ocean_set_params choppiness, single bands and the resolution set")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const services::OceanStateData before = fixture.engine.oceanState();

        fixture.callOk("ocean_set_params", {{"choppiness", 2.5}});
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            CHECK(fixture.engine.oceanState().bands[i].choppiness == 2.5f);
        }

        fixture.callOk("ocean_set_params", {{"bands", {{"agitation", {{"patchSize", 250}}}}}});
        CHECK(fixture.engine.oceanState().bands[1].patchSize == 250.0f);
        CHECK(fixture.engine.oceanState().bands[0].patchSize == before.bands[0].patchSize);
        CHECK(fixture.engine.oceanState().bands[2].patchSize == before.bands[2].patchSize);

        fixture.callOk("ocean_set_params", {{"bands", {{"ripples", {{"resolution", 512}}}}}});
        CHECK(fixture.engine.oceanState().bands[2].resolution == 512u);

        CHECK(textHas(fixture.argError("ocean_set_params", {{"bands", {{"swell", {{"resolution", 100}}}}}}),
                      "must be 64, 128, 256 or 512"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"bands", {{"wind", {{"patchSize", 100}}}}}}),
                      "unknown bands key 'wind'"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"bands", {{"swell", {{"speed", 1}}}}}}),
                      "unknown bands.swell key 'speed'"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"choppiness", 6}}), "choppiness"));
    }

    TEST_CASE("ocean_set_params accepts every band and physics key at its minimum and at its maximum")
    {
        WaterFixture fixture;
        fixture.createOcean();

        for (bool max : {false, true})
        {
            CAPTURE(max);
            json bands = json::object();
            for (std::string_view name : watertool::bandNames)
            {
                bands[std::string(name)] = boundPatch(watertool::bandTable(), max);
            }
            const json out = fixture.callOk("ocean_set_params",
                                            {{"bands", bands}, {"physics", boundPatch(watertool::physicsTable(), max)}});
            const json& params = out["ocean"]["params"];
            for (std::string_view name : watertool::bandNames)
            {
                for (const auto& field : watertool::bandTable().fields)
                {
                    CAPTURE(name);
                    CAPTURE(field.key);
                    CHECK(params["bands"][std::string(name)][std::string(field.key)] == boundValue(field, max));
                }
            }
            for (const auto& field : watertool::physicsTable().fields)
            {
                CAPTURE(field.key);
                CHECK(params["physics"][std::string(field.key)] == boundValue(field, max));
            }
        }
    }

    TEST_CASE("ocean_set_params: while weather-driven, the weather owns the waves")
    {
        WaterFixture fixture;
        fixture.createOcean();
        fixture.callOk("ocean_set_params", {{"weatherDriven", true}});
        REQUIRE(fixture.engine.oceanState().weatherDriven);
        const services::OceanStateData driven = fixture.engine.oceanState();

        for (const json& args : {json{{"seaState", 3}}, json{{"windSpeed", 5}}, json{{"choppiness", 1}},
                                 json{{"windDirection", 90}}, json{{"bands", {{"swell", {{"amplitude", 0.0001}}}}}}})
        {
            CAPTURE(args.dump());
            CHECK(textHas(fixture.argError("ocean_set_params", args), "weatherDriven is on"));
        }
        CHECK(fixture.engine.oceanState() == driven);

        // Fields the weather does not own, and the response, stay editable.
        fixture.callOk("ocean_set_params", {{"bands", {{"swell", {{"patchSize", 300}}}}}, {"weatherResponse", 1.5}});
        CHECK(fixture.engine.oceanState().bands[0].patchSize == 300.0f);
        CHECK(fixture.engine.oceanState().weatherResponse == 1.5f);

        // Handing the bands back in the same call authors them.
        fixture.callOk("ocean_set_params", {{"weatherDriven", false}, {"seaState", 3}});
        CHECK_FALSE(fixture.engine.oceanState().weatherDriven);
        checkSeaStateBands(fixture.engine.oceanState(), water::seaStateFromBeaufort(3.0f, 45.0f));

        fixture.engine.weatherEnabled = true;
        const json out = fixture.callOk("ocean_set_params", {{"weatherDriven", true}});
        CHECK(listHas(out["notes"], "the live weather sets the sea state"));
    }

    TEST_CASE("ocean_set_params physics: drag and buoyancy tune all water; density is refused")
    {
        WaterFixture fixture;
        fixture.createOcean();

        fixture.callOk("ocean_set_params", {{"physics", {{"drag", 2}, {"buoyancyStrength", 3}, {"physicsEnabled", false}}}});
        const services::OceanPhysicsSettings& physics = fixture.engine.oceanState().physics;
        CHECK(physics.drag == 2.0f);
        CHECK(physics.buoyancyStrength == 3.0f);
        CHECK_FALSE(physics.physicsEnabled);
        CHECK(physics.density == services::OceanPhysicsSettings{}.density);

        CHECK(textHas(fixture.argError("ocean_set_params", {{"physics", {{"density", 1}}}}), "'physics.density' is not exposed"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"physics", {{"drag", 11}}}}), "physics.drag"));
    }

    TEST_CASE("ocean_set_params passes water_get's ocean params back unchanged")
    {
        WaterFixture fixture;
        fixture.createOcean({{"seaState", 5}});
        fixture.callOk("ocean_set_params", {{"weatherDriven", true}, {"weatherResponse", 0.5}});
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        const json params = fixture.callOk("water_get", json::object())["ocean"]["params"];
        const json out = fixture.callOk("ocean_set_params", params);
        CHECK(out["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
    }

    TEST_CASE("ocean_set_params level, a hidden ocean and the refusals")
    {
        WaterFixture fixture;

        CHECK(textHas(fixture.failure("ocean_set_params", {{"level", 1}}), "There is no ocean in the scene"));

        fixture.createOcean();
        fixture.callOk("ocean_set_params", {{"level", 5}});
        CHECK(fixture.engine.oceanState().waterHeight == 5.0f);
        const json out = fixture.callOk("ocean_set_params", {{"levelAboveTerrain", 1}, {"terrainPoint", {-24, 16}}});
        CHECK(fixture.engine.oceanState().waterHeight == -4.0f);
        CHECK(out["terrainReference"]["height"].get<float>() == -5.0f);

        CHECK(textHas(fixture.argError("ocean_set_params", {{"level", 1}, {"terrainPoint", {0, 0}}}),
                      "'terrainPoint' only applies with 'levelAboveTerrain'"));
        CHECK(textHas(fixture.argError("ocean_set_params", json::object()), "nothing to change"));
        CHECK(textHas(fixture.argError("ocean_set_params", {{"terrain", kTerrainId}}), "nothing to change"));

        // A hidden ocean is tuned all the same.
        fixture.callOk("ocean_enable", {{"enabled", false}});
        const json hidden = fixture.callOk("ocean_set_params", {{"level", 2}});
        CHECK(fixture.engine.oceanState().waterHeight == 2.0f);
        CHECK(listHas(hidden["notes"], "The ocean is hidden"));

        // A refused write changes nothing and records nothing.
        const std::size_t undoBefore = fixture.engine.undoStack.size();
        fixture.engine.refuseStateWrites = true;
        CHECK(textHas(fixture.failure("ocean_set_params", {{"level", 7}}), "The ocean was not changed"));
        CHECK(fixture.engine.oceanState().waterHeight == 2.0f);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
        fixture.engine.refuseStateWrites = false;

        fixture.engine.playMode = true;
        CHECK(textHas(fixture.failure("ocean_set_params", {{"level", 1}}), "Play mode"));
        fixture.engine.playMode = false;
        fixture.engine.loadPending = true;
        CHECK(textHas(fixture.failure("ocean_set_params", {{"level", 1}}), "scene load"));
    }

    TEST_CASE("undoing ocean_set_params restores only the parts it changed")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const float foamDecay = fixture.engine.oceanState().bands[2].foamDecay;
        fixture.callOk("ocean_set_params", {{"bands", {{"ripples", {{"foamDecay", 2}}}}}});

        // Edits made elsewhere in between.
        fixture.engine.oceanState().bands[0].patchSize = 400.0f;
        fixture.engine.oceanState().visual.shoreFoamRange = 9.0f;

        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.oceanState().bands[2].foamDecay == foamDecay);
        CHECK(fixture.engine.oceanState().bands[0].patchSize == 400.0f);
        CHECK(fixture.engine.oceanState().visual.shoreFoamRange == 9.0f);

        fixture.engine.undoStack.back()->execute();
        CHECK(fixture.engine.oceanState().bands[2].foamDecay == 2.0f);
    }

    TEST_CASE("every ocean and body argument bound water_get reports is accepted back")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const json ranges = watertool::rangesJson();

        for (const char* key : {"level", "weatherResponse", "seaState", "windSpeed", "windDirection", "choppiness",
                                "levelAboveTerrain"})
        {
            for (const char* bound : {"min", "max"})
            {
                CAPTURE(key);
                CAPTURE(bound);
                fixture.callOk("ocean_set_params", {{key, ranges["ocean"][key][bound]}});
            }
        }

        const json& body = ranges["body"];
        for (const char* bound : {"min", "max"})
        {
            CAPTURE(bound);
            const json center = body["center"][bound];
            const json extent = body["halfExtents"][bound];
            fixture.callOk("water_create_body", {{"center", {center, center}}, {"halfExtents", {extent, extent}},
                                                 {"level", body["level"][bound]}, {"depth", body["depth"][bound]}});
            fixture.callOk("water_create_body", basinLake({{"levelAboveTerrain", body["levelAboveTerrain"][bound]}}));
        }
    }

    TEST_CASE("ocean_set_params refuses one step past every range of its own arguments and changes nothing")
    {
        WaterFixture fixture;
        fixture.createOcean();
        const json ranges = watertool::rangesJson();
        const services::OceanStateData before = fixture.engine.oceanState();
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        for (const char* key : {"level", "weatherResponse", "seaState", "windSpeed", "windDirection", "choppiness",
                                "levelAboveTerrain"})
        {
            CAPTURE(key);
            const double min = ranges["ocean"][key]["min"].get<double>();
            const double max = ranges["ocean"][key]["max"].get<double>();
            for (const double outside : {min - 1.0, max + 1.0})
            {
                CAPTURE(outside);
                CHECK(textHas(fixture.argError("ocean_set_params", {{key, outside}}),
                              std::string("argument '") + key + "' must be between"));
            }
        }

        CHECK(fixture.engine.oceanState() == before);
        CHECK(fixture.engine.undoStack.size() == undoBefore);
    }

    TEST_CASE("ocean_enable and ocean_set_params pick the terrain they measure from")
    {
        WaterFixture fixture;
        fixture.engine.liveTerrains = 2;

        // Two live terrains: levelAboveTerrain needs 'terrain', and the refusal creates nothing.
        CHECK(textHas(fixture.failure("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 2}}), "pass 'terrain'"));
        CHECK(fixture.engine.tracked == 0);

        // 'terrain' picks one; without terrainPoint the level is measured from its lowest point (the strip at -10).
        json out = fixture.callOk("ocean_enable", {{"enabled", true}, {"levelAboveTerrain", 2}, {"terrain", 3}});
        CHECK(out["created"] == true);
        CHECK(out["terrainReference"]["terrain"] == 3);
        CHECK(fixture.engine.lastHeightTerrain == 3);
        CHECK(fixture.engine.oceanState().waterHeight == -8.0f);

        CHECK(textHas(fixture.failure("ocean_set_params", {{"levelAboveTerrain", 1}}), "pass 'terrain'"));
        out = fixture.callOk("ocean_set_params", {{"levelAboveTerrain", 1}, {"terrainPoint", {-24, 16}}, {"terrain", 2}});
        CHECK(out["terrainReference"]["terrain"] == 2);
        CHECK(fixture.engine.lastHeightTerrain == 2);
        CHECK(fixture.engine.oceanState().waterHeight == -4.0f);   // the basin floor at -5, plus 1

        // An id that is not a terrain is refused and changes nothing.
        CHECK(textHas(fixture.failure("ocean_set_params", {{"levelAboveTerrain", 1}, {"terrain", 99}}), "does not exist"));
        CHECK(fixture.engine.oceanState().waterHeight == -4.0f);
    }
}

TEST_SUITE("McpWaterToolSupport")
{
    TEST_CASE("footprintGrid: samples about 'spacing' apart, at most 32 per axis, then the exact centre")
    {
        const watertool::Footprint footprint = watertool::footprintGrid(glm::vec2(10.0f, 20.0f), glm::vec2(5.0f, 2.0f), 1.0f);
        CHECK(footprint.samplesX == 11);
        CHECK(footprint.samplesZ == 5);
        REQUIRE(footprint.points.size() == 56);
        CHECK(footprint.points.front().x == doctest::Approx(5.0f));
        CHECK(footprint.points.front().y == doctest::Approx(18.0f));
        CHECK(footprint.points[54].x == doctest::Approx(15.0f));
        CHECK(footprint.points[54].y == doctest::Approx(22.0f));
        CHECK(footprint.points.back() == glm::vec2(10.0f, 20.0f));

        const watertool::Footprint wide = watertool::footprintGrid(glm::vec2(0.0f), glm::vec2(1000.0f, 0.0f), 1.0f);
        CHECK(wide.samplesX == 32);
        CHECK(wide.samplesZ == 1); // a zero extent: one row through the centre
        CHECK(wide.points.size() == 33);
        CHECK(wide.points.front().y == 0.0f);

        CHECK(watertool::footprintGrid(glm::vec2(0.0f), glm::vec2(2.0f), 0.0f).samplesX == 5); // spacing 0 = 1 m
        CHECK(watertool::footprintGrid(glm::vec2(0.0f), glm::vec2(0.1f), 1.0f).samplesX == 2); // at least the corners
    }

    TEST_CASE("footprintStats folds the exact centre into min and max; referenceHeight")
    {
        watertool::Footprint footprint;
        footprint.samplesX = 3;
        footprint.samplesZ = 3;
        footprint.points.resize(10); // 3 x 3 grid + the centre; positions do not matter to the stats

        // Row-major: index 4 is the only grid sample off the rim; index 9 is the exact centre.
        std::vector<services::TerrainHeightSample> heights{validHeight(-1.0f), validHeight(-2.0f), validHeight(-3.0f),
                                                           validHeight(-4.0f), validHeight(-9.0f), validHeight(-5.0f),
                                                           validHeight(-6.0f), validHeight(-7.0f), validHeight(-8.0f),
                                                           validHeight(-9.5f)};

        watertool::FootprintStats stats = watertool::footprintStats(footprint, heights);
        CHECK(stats.total == 9);
        CHECK(stats.valid == 9);
        CHECK(stats.min == -9.5f); // the centre, below every grid sample
        CHECK(stats.max == -1.0f);
        REQUIRE(stats.rimMin.has_value());
        CHECK(*stats.rimMin == -8.0f);
        REQUIRE(stats.center.has_value());
        CHECK(*stats.center == -9.5f);
        CHECK(stats.anyHeight());
        CHECK(watertool::referenceHeight(stats, watertool::TerrainReference::Lowest) == -9.5f);
        CHECK(watertool::referenceHeight(stats, watertool::TerrainReference::Center) == -9.5f);
        CHECK(watertool::referenceHeight(stats, watertool::TerrainReference::Rim) == -8.0f);

        heights[8].valid = false; // a rim sample off the terrain
        heights[9].valid = false; // and the centre
        stats = watertool::footprintStats(footprint, heights);
        CHECK(stats.valid == 8);
        CHECK(stats.min == -9.0f);
        CHECK(*stats.rimMin == -7.0f);
        CHECK_FALSE(stats.center.has_value());
        CHECK_FALSE(watertool::referenceHeight(stats, watertool::TerrainReference::Center).has_value());
        CHECK(watertool::referenceHeight(stats, watertool::TerrainReference::Lowest) == -9.0f);

        // Only the centre on the terrain: no grid sample, but a height.
        for (std::size_t i = 0; i < 9; ++i)
        {
            heights[i].valid = false;
        }
        heights[9] = validHeight(-3.0f);
        stats = watertool::footprintStats(footprint, heights);
        CHECK(stats.valid == 0);
        CHECK(stats.anyHeight());
        CHECK(stats.min == -3.0f);
        CHECK(stats.max == -3.0f);
        CHECK_FALSE(stats.rimMin.has_value());
        CHECK(watertool::referenceHeight(stats, watertool::TerrainReference::Lowest) == -3.0f);
        CHECK_FALSE(watertool::referenceHeight(stats, watertool::TerrainReference::Rim).has_value());
        CHECK(watertool::footprintJson(stats) ==
              json{{"samples", 9}, {"valid", 0}, {"min", -3}, {"max", -3}, {"center", -3}, {"rimMin", nullptr}});

        // Nothing on the terrain.
        heights[9].valid = false;
        stats = watertool::footprintStats(footprint, heights);
        CHECK(stats.valid == 0);
        CHECK_FALSE(stats.anyHeight());
        CHECK_FALSE(watertool::referenceHeight(stats, watertool::TerrainReference::Lowest).has_value());
        CHECK_FALSE(watertool::referenceHeight(stats, watertool::TerrainReference::Rim).has_value());
        const json report = watertool::footprintJson(stats);
        CHECK(report["min"].is_null());
        CHECK(report["max"].is_null());
        CHECK(report["center"].is_null());
        CHECK(report["rimMin"].is_null());

        heights.pop_back();
        CHECK_THROWS_AS(watertool::footprintStats(footprint, heights), std::logic_error);
    }

    TEST_CASE("band names and masks round-trip")
    {
        for (uint32_t mask = 0; mask < 8; ++mask)
        {
            CAPTURE(mask);
            CHECK(watertool::bandMaskFromNames(watertool::bandMaskNames(mask), "waves") == mask);
        }
        CHECK(watertool::bandMaskFromNames(json::array({"Swell", "RIPPLES"}), "waves") == 5u);
        CHECK(watertool::bandMaskNames(8u) == json::array());
        CHECK(watertool::bandMaskNames(7u) == json::array({"swell", "agitation", "ripples"}));
        CHECK_THROWS_AS(watertool::bandMaskFromNames(json::array({"foam"}), "waves"), mcp::ArgError);
        CHECK_THROWS_AS(watertool::bandMaskFromNames(json("swell"), "waves"), mcp::ArgError);
        CHECK(watertool::bandIndex("Agitation") == 1);
        CHECK(watertool::bandIndex("wind") == -1);
    }

    TEST_CASE("rotateWind keeps the bands' offsets and wraps to [0, 360)")
    {
        services::OceanStateData state;
        state.bands[0].windDirection = 45.0f;
        state.bands[1].windDirection = 60.0f;
        state.bands[2].windDirection = 30.0f;

        watertool::rotateWind(state, 350.0f);
        CHECK(state.bands[0].windDirection == 350.0f);
        CHECK(state.bands[1].windDirection == 5.0f);
        CHECK(state.bands[2].windDirection == 335.0f);

        watertool::rotateWind(state, -720.0f);
        CHECK(state.bands[0].windDirection == 0.0f);
        CHECK_FALSE(std::signbit(state.bands[0].windDirection)); // never "-0"
        CHECK(state.bands[1].windDirection == 15.0f);
        CHECK(state.bands[2].windDirection == 345.0f);
    }

    TEST_CASE("applySeaState is OceanService::setSeaState(beaufort, 0) on a state value")
    {
        for (float beaufort : {0.0f, 3.0f, 6.5f, 12.0f, 15.0f, -1.0f})
        {
            CAPTURE(beaufort);
            services::OceanStateData state;
            state.bands[0].windDirection = 200.0f;
            const services::OceanStateData before = state;

            watertool::applySeaState(state, beaufort);
            const float clamped = std::clamp(beaufort, 0.0f, 12.0f);
            checkSeaStateBands(state, water::seaStateFromBeaufort(clamped, 200.0f));
            CHECK(state.currentBeaufort == clamped);
            for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
            {
                CHECK(state.bands[i].resolution == before.bands[i].resolution);
                CHECK(state.bands[i].patchSize == before.bands[i].patchSize);
                CHECK(state.bands[i].enabled == before.bands[i].enabled);
                CHECK(state.bands[i].foamPersistence == before.bands[i].foamPersistence);
                CHECK(state.bands[i].foamDecay == before.bands[i].foamDecay);
            }
        }

        services::OceanStateData state;
        watertool::setChoppiness(state, 1.75f);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            CHECK(state.bands[i].choppiness == 1.75f);
        }
    }

    TEST_CASE("weatherOwnedChanged covers exactly what the live weather writes")
    {
        const services::OceanStateData base;
        services::OceanStateData other = base;
        other.bands[1].patchSize += 1.0f;
        other.bands[2].resolution = 512;
        other.bands[0].enabled = false;
        other.bands[0].foamDecay += 1.0f;
        other.weatherResponse = 0.5f;
        CHECK_FALSE(watertool::weatherOwnedChanged(base, other));

        for (int field = 0; field < 7; ++field)
        {
            CAPTURE(field);
            services::OceanStateData changed = base;
            switch (field)
            {
            case 0: changed.bands[2].windSpeed += 1.0f; break;
            case 1: changed.bands[1].windDirection += 1.0f; break;
            case 2: changed.bands[0].amplitude *= 2.0f; break;
            case 3: changed.bands[0].choppiness += 0.5f; break;
            case 4: changed.bands[1].displacementScale += 1.0f; break;
            case 5: changed.bands[2].foamThreshold += 0.1f; break;
            default: changed.currentBeaufort += 1.0f; break;
            }
            CHECK(watertool::weatherOwnedChanged(base, changed));
        }
    }

    TEST_CASE("mergeParts copies exactly one look section per part bit")
    {
        const services::OceanStateData base;
        services::OceanStateData source = base;
        for (const auto& section : watertool::lookSections())
        {
            source.visual = env::strictPatch(section, source.visual, distinctPatch(section, base.visual), section.name);
        }
        // The unexposed members differ too: a section merge must never copy them.
        source.visual.deepColor.w = 0.123f;
        source.visual.maxVisibleDepth = 77.0f;
        source.visual.ssrDebugView = true;

        const auto& sections = watertool::lookSections();
        for (const auto& section : sections)
        {
            for (const auto& field : section.fields)
            {
                CAPTURE(section.name);
                CAPTURE(field.key);
                REQUIRE(field.get(source.visual) != field.get(base.visual));
            }
        }

        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            CAPTURE(sections[i].name);
            services::OceanStateData target = base;
            const watertool::OceanParts part = watertool::partLook0 << i;
            watertool::mergeParts(target, source, part);
            for (std::size_t j = 0; j < sections.size(); ++j)
            {
                CAPTURE(sections[j].name);
                const services::OceanVisualSettings& expected = j == i ? source.visual : base.visual;
                CHECK(env::toJson(sections[j], target.visual) == env::toJson(sections[j], expected));
            }
            CHECK(watertool::changedParts(base, target) == part);
            CHECK(target.visual.deepColor.w == base.visual.deepColor.w);
            CHECK(target.visual.maxVisibleDepth == base.visual.maxVisibleDepth);
            CHECK(target.visual.ssrDebugView == base.visual.ssrDebugView);
        }
    }

    TEST_CASE("mergeParts and changedParts for the ocean's other parts")
    {
        const services::OceanStateData base;
        services::OceanStateData source = base;
        source.waterHeight = 4.0f;
        source.isActive = false;
        source.physics.physicsEnabled = false;
        source.physics.drag = 3.0f;
        source.physics.buoyancyStrength = 4.0f;
        source.physics.density = 1025.0f; // never exposed: never merged
        source.weatherDriven = true;
        source.weatherResponse = 0.5f;
        source.currentBeaufort = 8.0f;
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            source.bands[i].patchSize += 10.0f;
        }

        struct Case
        {
            watertool::OceanParts part;
            const char* text;
        };
        for (const Case& entry : {Case{watertool::partLevel, "level"}, Case{watertool::partVisible, "visibility"},
                                  Case{watertool::partPhysics, "physics"}, Case{watertool::partWeather, "weather"},
                                  Case{watertool::partSeaState, "sea state"},
                                  Case{watertool::partBand0 << 0, "swell band"},
                                  Case{watertool::partBand0 << 1, "agitation band"},
                                  Case{watertool::partBand0 << 2, "ripples band"}})
        {
            CAPTURE(entry.text);
            services::OceanStateData target = base;
            watertool::mergeParts(target, source, entry.part);
            CHECK(watertool::changedParts(base, target) == entry.part);
            CHECK(watertool::partsText(entry.part) == entry.text);
            CHECK(target.physics.density == base.physics.density);
        }

        // Every part at once is the whole state, but for what no part covers (density; gravity, unchanged here).
        const watertool::OceanParts everything = (watertool::partLook0 << watertool::lookSections().size()) - 1u;
        services::OceanStateData all = base;
        watertool::mergeParts(all, source, everything);
        services::OceanStateData expected = source;
        expected.physics.density = base.physics.density;
        CHECK(all == expected);

        CHECK(watertool::partsText(watertool::partLevel | (watertool::partBand0 << 1) | watertool::partLook0) ==
              "level, agitation band, color");
    }

    TEST_CASE("creationDefaults is the Ocean Editor's Create Ocean; creationFromState carries a state's bands")
    {
        const services::OceanCreationData defaults = watertool::creationDefaults(-3.0f);
        const services::OceanCreationData editor{};
        CHECK(defaults.waterHeight == -3.0f);
        CHECK(defaults.physicsEnabled == editor.physicsEnabled);
        CHECK(defaults.shallowColor == editor.shallowColor);
        CHECK(defaults.deepColor == editor.deepColor);
        services::OceanFFTConfigData bands;
        bands.enabled = true;
        CHECK(defaults.oceanConfig == bands);

        services::OceanStateData state;
        state.waterHeight = 2.5f;
        state.physics.physicsEnabled = false;
        state.visual.shallowColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        state.visual.deepColor = glm::vec4(0.5f, 0.6f, 0.7f, 0.8f);
        state.bands[1].patchSize = 321.0f;
        state.gravity = 9.0f;
        const services::OceanCreationData fromState = watertool::creationFromState(state);
        CHECK(fromState.waterHeight == 2.5f);
        CHECK_FALSE(fromState.physicsEnabled);
        CHECK(fromState.shallowColor == state.visual.shallowColor);
        CHECK(fromState.deepColor == state.visual.deepColor);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            CHECK(fromState.oceanConfig.bands[i] == state.bands[i]);
        }
        CHECK(fromState.oceanConfig.gravity == 9.0f);
        CHECK(fromState.oceanConfig.enabled);
    }

    TEST_CASE("bodyJson, bodyLevel and sameBodyData")
    {
        services::TransformData transform;
        transform.position = glm::vec3(3.0f, 1.5f, -4.0f);
        services::WaterBodyComponentData data;
        data.type = 1;
        data.waterHeight = 0.25f;
        data.halfExtents = glm::vec2(2.0f, 6.0f);
        data.depth = 3.0f;
        data.bandMask = 4u;
        data.physicsEnabled = false;

        CHECK(watertool::bodyLevel(transform, data) == 1.75f);
        const json body = watertool::bodyJson(handleOf(42), "Pool", transform, data);
        CHECK(body == json{{"entity", 42}, {"name", "Pool"}, {"type", "pool"}, {"center", {3, -4}},
                           {"halfExtents", {2, 6}}, {"level", 1.75}, {"depth", 3}, {"waves", {"ripples"}},
                           {"physicsEnabled", false}, {"active", true}});

        services::WaterBodyComponentData other = data;
        CHECK(watertool::sameBodyData(data, other));
        other.isActive = false;
        CHECK_FALSE(watertool::sameBodyData(data, other));
    }

    TEST_CASE("oceanParamsJson lists every band key and the physics keys")
    {
        const json params = watertool::oceanParamsJson(services::OceanStateData{});
        CHECK(params.size() == 5);
        for (std::string_view name : watertool::bandNames)
        {
            CAPTURE(name);
            CHECK(params["bands"][std::string(name)].size() == watertool::bandTable().fields.size());
        }
        CHECK(params["physics"].size() == 3);
        CHECK_FALSE(params["physics"].contains("density"));
    }
}
