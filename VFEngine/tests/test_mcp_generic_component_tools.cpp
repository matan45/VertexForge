#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/PluginComponentEvents.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// VK-1651: component_*_generic map their arguments onto the PluginComponentEvents
// commands, surface PluginComponentResult.error in-band and push one undo entry.
namespace
{
    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }
    };

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    // Plugin type "Health" {hp (int), maxHp (int, read-only), icon (AssetRef)} on
    // entities 1 and 2. Entity 1 starts with Health {hp 50, maxHp 100, icon unset}.
    // An AssetRef serializes as GUID hex under its name plus "<name>Path" only when
    // the ref resolves to a path.
    constexpr const char* unsetGuid = "0000000000000000";
    struct FakePluginComponents
    {
        std::map<uint64_t, nlohmann::json> health;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        events::scene::SetPluginComponentFieldsCommand lastSet;
        events::scene::AddPluginComponentCommand lastAdd;
        int setCount = 0;
        int addCount = 0;

        static events::scene::PluginComponentResult fail(std::string error)
        {
            events::scene::PluginComponentResult result;
            result.error = std::move(error);
            return result;
        }

        static events::scene::PluginComponentResult success(nlohmann::json value)
        {
            events::scene::PluginComponentResult result;
            result.ok = true;
            result.value = std::move(value);
            return result;
        }

        // Strict like the real handler: all-or-nothing, unknown and read-only keys fail.
        std::optional<std::string> validate(const nlohmann::json& fields) const
        {
            if (fields.is_null())
            {
                return std::nullopt;
            }
            for (auto it = fields.begin(); it != fields.end(); ++it)
            {
                if (it.key() == "maxHp")
                {
                    return "field 'maxHp' is read-only";
                }
                if (it.key() == "icon" || it.key() == "iconPath")
                {
                    continue;
                }
                if (it.key() != "hp")
                {
                    return "unknown field '" + it.key() + "'; valid fields: hp, maxHp, icon";
                }
                if (!it.value().is_number_integer())
                {
                    return "field 'hp' must be an integer";
                }
            }
            return std::nullopt;
        }

        void registerHandlers()
        {
            health[1] = {{"hp", 50}, {"maxHp", 100}, {"icon", unsetGuid}};

            auto& d = events::EventDispatcher::instance();
            using namespace events::scene;

            d.registerQueryHandler<GetEntityQuery>(
                [](const GetEntityQuery& q) -> std::optional<services::EntityData>
                {
                    if (q.entity.id != 1 && q.entity.id != 2)
                    {
                        return std::nullopt;
                    }
                    services::EntityData data;
                    data.handle = q.entity;
                    data.name = "Entity" + std::to_string(q.entity.id);
                    data.parent = handleOf(100);
                    return data;
                });
            d.registerQueryHandler<GetPluginComponentTypesQuery>(
                [](const GetPluginComponentTypesQuery&)
                {
                    return nlohmann::json::array({{
                        {"name", "Health"},
                        {"plugin", "RTSGameplay"},
                        {"fields", nlohmann::json::array({
                            {{"name", "hp"}, {"type", "int"}},
                            {{"name", "maxHp"}, {"type", "int"}, {"readOnly", true}},
                            {{"name", "icon"}, {"type", "AssetRef"}}
                        })}
                    }});
                });
            d.registerQueryHandler<GetPluginComponentQuery>(
                [this](const GetPluginComponentQuery& q) -> std::optional<nlohmann::json>
                {
                    auto it = health.find(q.entity.id);
                    if (q.type != "Health" || it == health.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                });
            d.registerCommandHandler<SetPluginComponentFieldsCommand>(
                [this](const SetPluginComponentFieldsCommand& c)
                {
                    ++setCount;
                    lastSet = c;
                    auto it = health.find(c.entity.id);
                    if (c.type != "Health" || it == health.end())
                    {
                        return fail("entity has no 'Health' component");
                    }
                    if (auto error = validate(c.fields))
                    {
                        return fail(*error);
                    }
                    // "<name>" wins over "<name>Path", like the real patch helper.
                    for (auto field = c.fields.begin(); field != c.fields.end(); ++field)
                    {
                        if (field.key() == "iconPath" && !c.fields.contains("icon"))
                        {
                            it->second["icon"] = "00000000000000ab";
                            it->second["iconPath"] = field.value();
                        }
                        else if (field.key() == "icon")
                        {
                            it->second["icon"] = field.value();
                            if (field.value() == unsetGuid)
                            {
                                it->second.erase("iconPath");
                            }
                            else if (c.fields.contains("iconPath"))
                            {
                                it->second["iconPath"] = c.fields["iconPath"];  // the GUID resolves to that path
                            }
                        }
                        else if (field.key() != "iconPath")
                        {
                            it->second[field.key()] = field.value();
                        }
                    }
                    return success(it->second);
                });
            d.registerCommandHandler<AddPluginComponentCommand>(
                [this](const AddPluginComponentCommand& c)
                {
                    ++addCount;
                    lastAdd = c;
                    if (c.type != "Health")
                    {
                        return fail("unknown plugin component type '" + c.type + "'");
                    }
                    if (health.contains(c.entity.id))
                    {
                        return fail("entity already has 'Health'");
                    }
                    if (auto error = validate(c.fields))
                    {
                        return fail(*error);
                    }
                    nlohmann::json value{{"hp", 100}, {"maxHp", 100}};
                    if (c.fields.is_object())
                    {
                        value.update(c.fields);
                    }
                    health[c.entity.id] = value;
                    return success(value);
                });
            d.registerCommandHandler<RemovePluginComponentCommand>(
                [this](const RemovePluginComponentCommand& c)
                {
                    auto it = health.find(c.entity.id);
                    if (c.type != "Health" || it == health.end())
                    {
                        return fail("entity has no 'Health' component");
                    }
                    nlohmann::json value = it->second;
                    health.erase(it);
                    return success(value);
                });
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& c)
                {
                    undoStack.push_back(c.command);
                });
        }
    };

    struct GenericFixture
    {
        DispatcherScope dispatcherScope;
        FakePluginComponents plugin;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        GenericFixture()
        {
            plugin.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerPluginComponentTools(registry, context);
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            auto tool = registry.find(name);
            REQUIRE(tool != nullptr);
            return tool->handler(args);
        }
    };
}

TEST_SUITE("McpGenericComponentTools")
{
    TEST_CASE("component_list_types returns the curated bindings and the plugin types")
    {
        GenericFixture fixture;

        auto result = fixture.call("component_list_types", nlohmann::json::object());
        REQUIRE_FALSE(result.isError);
        const nlohmann::json& builtin = result.structured["builtin"];
        REQUIRE(builtin.size() == 9);
        CHECK(builtin[0]["name"] == "Mesh");
        CHECK(builtin[0]["fieldHelp"].get<std::string>().find("meshRef") != std::string::npos);
        REQUIRE(result.structured["plugin"].size() == 1);
        CHECK(result.structured["plugin"][0]["name"] == "Health");
    }

    TEST_CASE("component_get_generic returns the fields or an in-band error")
    {
        GenericFixture fixture;

        auto result = fixture.call("component_get_generic", {{"entity", 1}, {"type", "Health"}});
        REQUIRE_FALSE(result.isError);
        CHECK(result.structured["entity"] == 1);
        CHECK(result.structured["fields"]["hp"] == 50);

        CHECK(fixture.call("component_get_generic", {{"entity", 2}, {"type", "Health"}}).isError);
        CHECK_THROWS_AS((fixture.call("component_get_generic", {{"entity", 1}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("component_get_generic", {{"entity", 9}, {"type", "Health"}})),
                        std::runtime_error);
    }

    TEST_CASE("component_set_generic maps arguments and pushes an undo of only the written keys")
    {
        GenericFixture fixture;

        auto result = fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                             {"fields", {{"hp", 75}}}});
        REQUIRE_FALSE(result.isError);
        CHECK(fixture.plugin.lastSet.entity.id == 1);
        CHECK(fixture.plugin.lastSet.type == "Health");
        CHECK(fixture.plugin.lastSet.fields == nlohmann::json{{"hp", 75}});
        CHECK(result.structured["fields"]["hp"] == 75);

        REQUIRE(fixture.plugin.undoStack.size() == 1);
        auto entry = fixture.plugin.undoStack.back();
        CHECK(entry->getDescription() == "MCP: Set Health fields");

        entry->undo();
        CHECK(fixture.plugin.lastSet.fields == nlohmann::json{{"hp", 50}});  // no read-only maxHp
        CHECK(fixture.plugin.health.at(1)["hp"] == 50);
        entry->execute();
        CHECK(fixture.plugin.health.at(1)["hp"] == 75);
    }

    TEST_CASE("component_set_generic surfaces the handler error and pushes nothing")
    {
        GenericFixture fixture;

        auto readOnly = fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                               {"fields", {{"maxHp", 5}}}});
        CHECK(readOnly.isError);
        CHECK(readOnly.text.find("read-only") != std::string::npos);

        auto missing = fixture.call("component_set_generic", {{"entity", 2}, {"type", "Health"},
                                                              {"fields", {{"hp", 1}}}});
        CHECK(missing.isError);

        CHECK_THROWS_AS((fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                                {"fields", nlohmann::json::object()}})),
                        mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                                {"fields", {1, 2}}})),
                        mcp::ArgError);
        CHECK(fixture.plugin.undoStack.empty());
        CHECK(fixture.plugin.health.at(1)["hp"] == 50);
    }

    TEST_CASE("component_add_generic and component_remove_generic are undoable")
    {
        GenericFixture fixture;

        auto added = fixture.call("component_add_generic", {{"entity", 2}, {"type", "Health"},
                                                            {"fields", {{"hp", 30}}}});
        REQUIRE_FALSE(added.isError);
        CHECK(fixture.plugin.lastAdd.fields == nlohmann::json{{"hp", 30}});
        CHECK(added.structured["fields"]["hp"] == 30);
        REQUIRE(fixture.plugin.undoStack.size() == 1);

        fixture.plugin.undoStack.back()->undo();
        CHECK_FALSE(fixture.plugin.health.contains(2));
        fixture.plugin.undoStack.back()->execute();
        CHECK(fixture.plugin.health.at(2)["hp"] == 30);

        auto removed = fixture.call("component_remove_generic", {{"entity", 1}, {"type", "Health"}});
        REQUIRE_FALSE(removed.isError);
        CHECK(removed.structured["fields"]["hp"] == 50);
        CHECK_FALSE(fixture.plugin.health.contains(1));
        REQUIRE(fixture.plugin.undoStack.size() == 2);

        // Re-added with writable fields only: maxHp is read-only and must not be sent.
        fixture.plugin.undoStack.back()->undo();
        CHECK(fixture.plugin.lastAdd.fields == nlohmann::json{{"hp", 50}, {"icon", unsetGuid}});
        CHECK(fixture.plugin.health.at(1)["hp"] == 50);
    }

    TEST_CASE("component_add_generic errors are in-band and push nothing")
    {
        GenericFixture fixture;

        CHECK(fixture.call("component_add_generic", {{"entity", 1}, {"type", "Health"}}).isError);
        CHECK(fixture.call("component_add_generic", {{"entity", 2}, {"type", "Mana"}}).isError);
        CHECK(fixture.call("component_remove_generic", {{"entity", 2}, {"type", "Health"}}).isError);
        CHECK(fixture.plugin.undoStack.empty());

        // No 'fields' argument: the command carries null fields.
        REQUIRE_FALSE(fixture.call("component_add_generic", {{"entity", 2}, {"type", "Health"}}).isError);
        CHECK(fixture.plugin.lastAdd.fields.is_null());
    }

    TEST_CASE("setting only <name>Path also records the GUID key so an unset ref restores")
    {
        GenericFixture fixture;

        REQUIRE_FALSE(fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                             {"fields", {{"iconPath", "ui/heart.vfImage"}}}}).isError);
        CHECK(fixture.plugin.health.at(1)["iconPath"] == "ui/heart.vfImage");

        fixture.plugin.undoStack.back()->undo();
        CHECK(fixture.plugin.lastSet.fields == nlohmann::json{{"icon", unsetGuid}});
        CHECK(fixture.plugin.health.at(1)["icon"] == unsetGuid);
        CHECK_FALSE(fixture.plugin.health.at(1).contains("iconPath"));

        fixture.plugin.undoStack.back()->execute();
        CHECK(fixture.plugin.health.at(1)["iconPath"] == "ui/heart.vfImage");
    }

    TEST_CASE("an undo entry whose target is gone is skipped instead of blocking the stack")
    {
        GenericFixture fixture;

        REQUIRE_FALSE(fixture.call("component_set_generic", {{"entity", 1}, {"type", "Health"},
                                                             {"fields", {{"hp", 10}}}}).isError);
        fixture.plugin.health.erase(1);
        // The undo service would put a throwing entry back on the stack forever.
        CHECK_NOTHROW(fixture.plugin.undoStack.back()->undo());
        CHECK_NOTHROW(fixture.plugin.undoStack.back()->execute());
    }
}
