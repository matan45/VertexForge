#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// MCP prompts (VK-1652): reusable, parameterised instructions the client surfaces
// to the user (Claude Code: /mcp__<server>__<prompt>). Pure text; no engine access.
namespace mcp
{
    struct PromptArgument
    {
        std::string name;
        std::string description;
        bool required = false;
    };

    struct PromptDef
    {
        std::string name;
        std::string title;
        std::string description;
        std::vector<PromptArgument> arguments;
        // args: string -> string object (MCP prompt arguments are strings).
        // Returns the prompts/get `messages` array:
        // [{role:"user", content:{type:"text", text}}...]. May throw ArgError for a
        // bad value (answered as -32602).
        std::function<nlohmann::json(const nlohmann::json& args)> build;
    };

    // Filled on the main thread before the server starts, then read-only.
    class PromptRegistry
    {
    public:
        // Replaces a prompt with the same name.
        void add(PromptDef prompt);
        const PromptDef* find(std::string_view name) const;
        const std::vector<PromptDef>& all() const { return prompts; }

        // The `prompts` array for prompts/list.
        nlohmann::json listJson() const;

    private:
        std::vector<PromptDef> prompts;
    };
}
