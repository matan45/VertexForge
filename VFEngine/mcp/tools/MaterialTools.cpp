#include "CoreTools.hpp"
#include "PathSandbox.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/McpUndo.hpp"
#include "../undo/McpUndoCommands.hpp"

#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        // Main thread only. ProjectConfig::workingDirectory is the project's asset root.
        fs::path assetsRoot()
        {
            auto project = events::EventDispatcher::instance().query(events::project::GetCurrentProjectQuery{});
            if (!project.has_value() || project->workingDirectory.empty())
            {
                throw std::runtime_error("No project is loaded; open one with project_open first");
            }
            // workingDirectory is built with path::string() (narrow) by ProjectServiceImpl.
            return fs::absolute(fs::path(project->workingDirectory));
        }

        std::string projectRelative(const fs::path& root, const fs::path& path)
        {
            std::error_code ec;
            fs::path relative = fs::relative(path, root, ec);
            if (ec || relative.empty() || *relative.begin() == "..")
            {
                return genericPathToUtf8(path);
            }
            return genericPathToUtf8(relative);
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        services::EntityHandle requireLiveEntity(const ArgReader& reader, const char* name)
        {
            uint32_t id = reader.requireEntity(name);
            services::EntityHandle handle;
            handle.id = static_cast<uint64_t>(id);

            events::scene::GetEntityQuery query;
            query.entity = handle;
            if (!events::EventDispatcher::instance().query(query).has_value())
            {
                throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + name +
                                         "') does not exist; use entity_find or scene_get_hierarchy");
            }
            return handle;
        }

        // A project-relative (or absolute, inside the project) .vfMat / .vfMatInstance
        // that exists on disk.
        fs::path resolveMaterialFile(const fs::path& root, const std::string& path)
        {
            std::string error;
            auto resolved = resolveInside(root, path, error);
            if (!resolved)
            {
                throw ArgError(error);
            }
            const std::string extension = lowercase(genericPathToUtf8(resolved->extension()));
            if (extension != ".vfmat" && extension != ".vfmatinstance")
            {
                throw ArgError("material '" + path + "' must be a .vfMat or .vfMatInstance file");
            }
            std::error_code ec;
            if (!fs::is_regular_file(*resolved, ec))
            {
                throw ArgError("material '" + path + "' does not exist (see material_list / material_create)");
            }
            return *resolved;
        }

        nlohmann::json parameterToJson(const ::material::ParameterValue& value)
        {
            return std::visit([](const auto& v) -> nlohmann::json
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                {
                    return v;
                }
                else
                {
                    nlohmann::json out = nlohmann::json::array();
                    for (int i = 0; i < T::length(); ++i)
                    {
                        out.push_back(v[i]);
                    }
                    return out;
                }
            }, value);
        }

        ::material::ParameterValue parameterFromJson(const std::string& name, const nlohmann::json& value)
        {
            if (value.is_number())
            {
                return value.get<float>();
            }
            if (value.is_array() && value.size() >= 2 && value.size() <= 4 &&
                std::all_of(value.begin(), value.end(), [](const nlohmann::json& c) { return c.is_number(); }))
            {
                switch (value.size())
                {
                case 2: return glm::vec2(value[0].get<float>(), value[1].get<float>());
                case 3: return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                default:
                    return glm::vec4(value[0].get<float>(), value[1].get<float>(),
                                     value[2].get<float>(), value[3].get<float>());
                }
            }
            throw ArgError("parameter '" + name + "' must be a number or an array of 2-4 numbers");
        }

        nlohmann::json materialState(const services::EntityHandle& entity, const fs::path& root)
        {
            events::material::GetMaterialDataQuery query;
            query.entity = entity;
            std::optional<services::MaterialData> data = events::EventDispatcher::instance().query(query);

            nlohmann::json out{{"entity", static_cast<uint32_t>(entity.id)}, {"hasMaterial", data.has_value()}};
            if (!data.has_value())
            {
                return out;
            }

            auto refPath = [&](const asset::AssetRef& ref) -> nlohmann::json
            {
                if (!ref.isValid())
                {
                    return nullptr;
                }
                return projectRelative(root, fs::path(ref.resolve()));
            };

            nlohmann::json subMeshes = nlohmann::json::object();
            for (const auto& [subMesh, ref] : data->subMeshMaterials)
            {
                subMeshes[subMesh] = refPath(ref);
            }
            nlohmann::json overrides = nlohmann::json::object();
            for (const auto& [name, value] : data->parameterOverrides)
            {
                overrides[name] = parameterToJson(value);
            }

            out["defaultMaterial"] = refPath(data->defaultMaterialRef);
            out["subMeshMaterials"] = std::move(subMeshes);
            out["parameterOverrides"] = std::move(overrides);
            return out;
        }

        void registerMaterialCreate(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "material_create";
            tool.title = "Create material asset";
            tool.description =
                "Create a new default material asset (.vfMat with only a PBR Output node — edit its graph in "
                "the Material Editor) in a project folder. 'directory' is relative to the project asset root "
                "(created if missing). When '<name>.vfMat' exists, '<name>_<n>.vfMat' is used. "
                "Returns {path} (project-relative), usable with material_assign.";
            tool.inputSchema = schema::object({
                {"name", schema::string("Material name (file name without extension; no path separators)")},
                {"directory", schema::string("Folder relative to the asset root, e.g. 'materials'. Default: the root.")}
            }, {"name"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string name = reader.requireString("name");
                if (name.empty() || name == "." || name == ".." ||
                    name.find_first_of("/\\:*?\"<>|") != std::string::npos)
                {
                    throw ArgError("argument 'name' must be a plain file name (no path separators or :*?\"<>|)");
                }

                const fs::path root = assetsRoot();
                std::string error;
                auto directory = resolveInside(root, reader.optString("directory", "."), error);
                if (!directory)
                {
                    throw ArgError(error);
                }
                std::error_code ec;
                fs::create_directories(*directory, ec);
                if (ec)
                {
                    return ToolResult::error("Failed to create folder '" + pathToUtf8(*directory) + "': " + ec.message());
                }

                events::material::CreateMaterialAssetCommand command;
                command.directory = pathToUtf8(*directory);
                command.name = name;
                events::material::CreateMaterialAssetResult result =
                    events::EventDispatcher::instance().execute(command);
                if (!result.success)
                {
                    return ToolResult::error("material_create failed: " + result.error);
                }

                return ToolResult::ok({{"path", projectRelative(root, pathFromUtf8(result.path))}});
            };
            registry.add(std::move(tool));
        }

        void registerMaterialAssign(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "material_assign";
            tool.title = "Assign material";
            tool.description =
                "Assign a material asset to an entity (adds a MaterialComponent if missing). Without 'subMesh' "
                "it sets the default material used by every submesh without its own assignment; with 'subMesh' "
                "it overrides that submesh (by submesh name). An empty 'material' clears the assignment. "
                "Undoable (material_create / material_set are not). "
                "Returns the entity's material state {defaultMaterial, subMeshMaterials, parameterOverrides}.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"material", schema::string("Project-relative .vfMat / .vfMatInstance path, or \"\" to clear")},
                {"subMesh", schema::string("Submesh name to override. Omit to set the default material.")}
            }, {"entity", "material"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityHandle entity = requireLiveEntity(reader, "entity");
                const std::string material = reader.requireString("material");
                const fs::path root = assetsRoot();

                // Narrow, like the editor's own callers; AssetRef::fromPath builds an
                // fs::path from it and resolves it against the asset database.
                const std::string materialPath = material.empty() ? std::string{} : resolveMaterialFile(root, material).string();

                auto& dispatcher = events::EventDispatcher::instance();

                // The assignment as it was, for undo. A missing component means this
                // call adds it, so undo removes it again.
                events::material::GetMaterialDataQuery beforeQuery;
                beforeQuery.entity = entity;
                const std::optional<services::MaterialData> before = dispatcher.query(beforeQuery);
                std::optional<std::string> undoSubMesh;
                std::string beforePath;

                bool applied = false;
                if (reader.has("subMesh"))
                {
                    const std::string subMesh = reader.requireString("subMesh");
                    if (subMesh.empty())
                    {
                        throw ArgError("argument 'subMesh' must not be empty");
                    }
                    undoSubMesh = subMesh;
                    if (before.has_value())
                    {
                        auto it = before->subMeshMaterials.find(subMesh);
                        if (it != before->subMeshMaterials.end() && it->second.isValid())
                        {
                            beforePath = it->second.resolve();
                        }
                    }
                    events::material::SetSubMeshMaterialCommand command;
                    command.entity = entity;
                    command.submeshName = subMesh;
                    command.materialPath = materialPath;
                    applied = dispatcher.execute(command);
                }
                else
                {
                    if (before.has_value() && before->defaultMaterialRef.isValid())
                    {
                        beforePath = before->defaultMaterialRef.resolve();
                    }
                    events::material::SetDefaultMaterialCommand command;
                    command.entity = entity;
                    command.materialPath = materialPath;
                    applied = dispatcher.execute(command);
                }

                if (!applied)
                {
                    return ToolResult::error("The material service rejected the assignment");
                }

                undo::UndoRecorder recorder("MCP: Assign material");
                recorder.add(std::make_unique<undo::MaterialAssignUndo>(static_cast<uint32_t>(entity.id), undoSubMesh,
                                                                        before.has_value(), beforePath, materialPath));
                recorder.push();
                return ToolResult::ok(materialState(entity, root));
            };
            registry.add(std::move(tool));
        }

        void registerMaterialSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "material_set";
            tool.title = "Set material parameter overrides";
            tool.description =
                "Set per-entity runtime overrides of named material parameters (the material graph's Parameter "
                "nodes; materials are node graphs, so there are no fixed albedo/metallic fields). Values are a "
                "number (scalar) or [x,y], [x,y,z], [x,y,z,w] (vectors / RGBA colors). 'clear' removes the "
                "named overrides, 'clearAll' removes every override; clears apply before 'parameters'. The "
                "entity must already have a material (material_assign). Returns the entity's material state.";
            tool.inputSchema = schema::object({
                {"entity", schema::entity()},
                {"parameters", schema::anyObject("Map of parameter name -> number | [2-4 numbers]")},
                {"clear", schema::array(schema::string("Parameter name"), "Override names to remove")},
                {"clearAll", schema::boolean("Remove all overrides first. Default false.")}
            }, {"entity"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const services::EntityHandle entity = requireLiveEntity(reader, "entity");

                // Validate everything before mutating anything.
                std::vector<std::pair<std::string, ::material::ParameterValue>> values;
                if (reader.has("parameters"))
                {
                    const nlohmann::json& parameters = reader.raw("parameters");
                    if (!parameters.is_object())
                    {
                        throw ArgError("argument 'parameters' must be an object");
                    }
                    for (const auto& [name, value] : parameters.items())
                    {
                        if (name.empty())
                        {
                            throw ArgError("parameter names must not be empty");
                        }
                        values.emplace_back(name, parameterFromJson(name, value));
                    }
                }
                std::vector<std::string> clear;
                if (reader.has("clear"))
                {
                    clear = reader.requireStringArray("clear");
                }
                const bool clearAll = reader.optBool("clearAll", false);
                if (values.empty() && clear.empty() && !clearAll)
                {
                    throw ArgError("nothing to do: pass 'parameters', 'clear' or 'clearAll'");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                events::material::HasMaterialComponentQuery hasQuery;
                hasQuery.entity = entity;
                if (!dispatcher.query(hasQuery))
                {
                    return ToolResult::error("Entity " + std::to_string(static_cast<uint32_t>(entity.id)) +
                                             " has no material; call material_assign first");
                }

                if (clearAll)
                {
                    events::material::ClearMaterialParameterCommand command;
                    command.entity = entity;  // empty name clears every override
                    dispatcher.execute(command);
                }
                for (const std::string& name : clear)
                {
                    if (name.empty())
                    {
                        continue;  // an empty name would clear everything
                    }
                    events::material::ClearMaterialParameterCommand command;
                    command.entity = entity;
                    command.parameterName = name;
                    dispatcher.execute(command);  // false == was not overridden; not an error
                }
                for (const auto& [name, value] : values)
                {
                    events::material::SetMaterialParameterCommand command;
                    command.entity = entity;
                    command.parameterName = name;
                    command.value = value;
                    if (!dispatcher.execute(command))
                    {
                        return ToolResult::error("Failed to set parameter '" + name + "'");
                    }
                }

                return ToolResult::ok(materialState(entity, assetsRoot()));
            };
            registry.add(std::move(tool));
        }

        void registerMaterialList(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "material_list";
            tool.title = "List materials";
            tool.description =
                "List the project's material assets (.vfMat and .vfMatInstance) from the asset database. "
                "Returns {materials:[{path, type, guid}]} with project-relative paths.";
            tool.inputSchema = schema::object({
                {"contains", schema::string("Case-insensitive substring filter on the path")}
            });
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                const std::string needle = lowercase(reader.optString("contains"));
                const fs::path root = assetsRoot();
                auto& dispatcher = events::EventDispatcher::instance();

                nlohmann::json materials = nlohmann::json::array();
                for (resource::AssetType type : {resource::AssetType::Material, resource::AssetType::MaterialInstance})
                {
                    events::assetdb::GetAssetsByTypeQuery query;
                    query.type = type;
                    for (const events::assetdb::AssetEntryData& entry : dispatcher.query(query))
                    {
                        const std::string path = projectRelative(root, fs::path(entry.path));
                        if (!needle.empty() && lowercase(path).find(needle) == std::string::npos)
                        {
                            continue;
                        }
                        materials.push_back({
                            {"path", path},
                            {"type", resource::assetTypeName(entry.type)},
                            {"guid", entry.guid.toString()}
                        });
                    }
                }
                return ToolResult::ok({{"materials", std::move(materials)}});
            };
            registry.add(std::move(tool));
        }
    }

    void registerMaterialTools(ToolRegistry& registry, const ToolContext&)
    {
        registerMaterialCreate(registry);
        registerMaterialAssign(registry);
        registerMaterialSet(registry);
        registerMaterialList(registry);
    }
}
