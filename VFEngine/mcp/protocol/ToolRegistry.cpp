#include "ToolRegistry.hpp"

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

        if (!structured.is_null())
        {
            out["structuredContent"] = structured.is_object()
                ? structured
                : nlohmann::json{{"result", structured}};
        }
        return out;
    }

    void ToolRegistry::add(ToolDef tool)
    {
        if (auto it = indexByName.find(tool.name); it != indexByName.end())
        {
            tools[it->second] = std::move(tool);
            return;
        }
        indexByName.emplace(tool.name, tools.size());
        tools.push_back(std::move(tool));
    }

    const ToolDef* ToolRegistry::find(std::string_view name) const
    {
        auto it = indexByName.find(std::string(name));
        return it == indexByName.end() ? nullptr : &tools[it->second];
    }

    nlohmann::json ToolRegistry::listJson() const
    {
        nlohmann::json list = nlohmann::json::array();
        for (const ToolDef& tool : tools)
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
            list.push_back(std::move(entry));
        }
        return list;
    }
}
