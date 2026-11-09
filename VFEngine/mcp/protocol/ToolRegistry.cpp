#include "ToolRegistry.hpp"

#include <unordered_set>

namespace mcp
{
    ToolResult ToolResult::ok(nlohmann::json structured, std::string text)
    {
        ToolResult result;
        result.structured = std::move(structured);
        result.text = std::move(text);
        return result;
    }

    ToolResult ToolResult::error(std::string message)
    {
        ToolResult result;
        result.text = std::move(message);
        result.isError = true;
        return result;
    }

    ToolResult ToolResult::image(std::string base64, std::string mimeType, nlohmann::json structured,
                                 std::string text)
    {
        ToolResult result = ok(std::move(structured), std::move(text));
        result.extraContent.push_back(nlohmann::json{
            {"type", "image"},
            {"data", std::move(base64)},
            {"mimeType", std::move(mimeType)}
        });
        return result;
    }

    nlohmann::json ToolResult::toJson() const
    {
        std::string contentText = text;
        if (contentText.empty())
        {
            // replace: engine strings (paths, names) are not guaranteed UTF-8.
            contentText = structured.is_null()
                ? std::string("ok")
                : structured.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        nlohmann::json out{
            {"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", contentText}}})},
            {"isError", isError}
        };
        for (const nlohmann::json& block : extraContent)
        {
            out["content"].push_back(block);
        }

        if (!structured.is_null())
        {
            out["structuredContent"] = structured.is_object()
                ? structured
                : nlohmann::json{{"result", structured}};
        }
        return out;
    }

    namespace
    {
        nlohmann::json describeTool(const ToolDef& tool)
        {
            nlohmann::json entry{
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", tool.inputSchema},
                {"annotations", nlohmann::json{
                    {"readOnlyHint", tool.readOnly},
                    {"destructiveHint", tool.destructive}
                }}
            };
            if (!tool.title.empty())
            {
                entry["title"] = tool.title;
                entry["annotations"]["title"] = tool.title;
            }
            return entry;
        }

        nlohmann::json describeAll(const std::vector<std::shared_ptr<const ToolDef>>& tools)
        {
            nlohmann::json list = nlohmann::json::array();
            for (const std::shared_ptr<const ToolDef>& tool : tools)
            {
                list.push_back(describeTool(*tool));
            }
            return list;
        }
    }

    ToolRegistry::ToolRegistry()
        : mutex(std::make_unique<std::mutex>())
    {
    }

    ToolRegistry::ToolRegistry(ToolRegistry&& other) noexcept
        : mutex(std::make_unique<std::mutex>())
    {
        std::lock_guard<std::mutex> lock(*other.mutex);
        tools = std::move(other.tools);
        indexByName = std::move(other.indexByName);
        rev = other.rev;
        other.tools.clear();
        other.indexByName.clear();
    }

    ToolRegistry& ToolRegistry::operator=(ToolRegistry&& other) noexcept
    {
        if (this != &other)
        {
            std::scoped_lock lock(*mutex, *other.mutex);
            tools = std::move(other.tools);
            indexByName = std::move(other.indexByName);
            rev = other.rev;
            other.tools.clear();
            other.indexByName.clear();
        }
        return *this;
    }

    void ToolRegistry::add(ToolDef tool)
    {
        auto shared = std::make_shared<const ToolDef>(std::move(tool));
        std::lock_guard<std::mutex> lock(*mutex);
        if (auto it = indexByName.find(shared->name); it != indexByName.end())
        {
            tools[it->second] = std::move(shared);
        }
        else
        {
            indexByName.emplace(shared->name, tools.size());
            tools.push_back(std::move(shared));
        }
        ++rev;
    }

    std::shared_ptr<const ToolDef> ToolRegistry::find(std::string_view name) const
    {
        std::lock_guard<std::mutex> lock(*mutex);
        auto it = indexByName.find(std::string(name));
        return it == indexByName.end() ? nullptr : tools[it->second];
    }

    std::size_t ToolRegistry::size() const
    {
        std::lock_guard<std::mutex> lock(*mutex);
        return tools.size();
    }

    bool ToolRegistry::replaceGroup(const std::string& owner, std::vector<ToolDef> defs,
                                    std::vector<std::string>* rejected)
    {
        std::lock_guard<std::mutex> lock(*mutex);

        // Accept a def only when its name is free or already this group's, and
        // only its first occurrence within `defs`.
        std::vector<std::shared_ptr<const ToolDef>> accepted;
        accepted.reserve(defs.size());
        std::unordered_set<std::string> acceptedNames;
        for (ToolDef& def : defs)
        {
            auto existing = indexByName.find(def.name);
            const bool ownedElsewhere = existing != indexByName.end() && tools[existing->second]->owner != owner;
            if (ownedElsewhere || acceptedNames.contains(def.name))
            {
                if (rejected)
                {
                    rejected->push_back(def.name);
                }
                continue;
            }
            acceptedNames.insert(def.name);
            def.owner = owner;
            accepted.push_back(std::make_shared<const ToolDef>(std::move(def)));
        }

        const nlohmann::json before = describeAll(tools);

        std::erase_if(tools, [&owner](const std::shared_ptr<const ToolDef>& tool)
        {
            return tool->owner == owner;
        });
        for (std::shared_ptr<const ToolDef>& tool : accepted)
        {
            tools.push_back(std::move(tool));
        }
        rebuildIndex();

        const bool changed = describeAll(tools) != before;
        if (changed)
        {
            ++rev;
        }
        return changed;
    }

    uint64_t ToolRegistry::revision() const
    {
        std::lock_guard<std::mutex> lock(*mutex);
        return rev;
    }

    nlohmann::json ToolRegistry::listJson() const
    {
        std::lock_guard<std::mutex> lock(*mutex);
        return describeAll(tools);
    }

    void ToolRegistry::rebuildIndex()
    {
        indexByName.clear();
        for (std::size_t i = 0; i < tools.size(); ++i)
        {
            indexByName.emplace(tools[i]->name, i);
        }
    }
}
