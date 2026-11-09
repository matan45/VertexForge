#include "PromptRegistry.hpp"

namespace mcp
{
    void PromptRegistry::add(PromptDef prompt)
    {
        for (PromptDef& existing : prompts)
        {
            if (existing.name == prompt.name)
            {
                existing = std::move(prompt);
                return;
            }
        }
        prompts.push_back(std::move(prompt));
    }

    const PromptDef* PromptRegistry::find(std::string_view name) const
    {
        for (const PromptDef& prompt : prompts)
        {
            if (prompt.name == name)
            {
                return &prompt;
            }
        }
        return nullptr;
    }

    nlohmann::json PromptRegistry::listJson() const
    {
        nlohmann::json list = nlohmann::json::array();
        for (const PromptDef& prompt : prompts)
        {
            nlohmann::json arguments = nlohmann::json::array();
            for (const PromptArgument& argument : prompt.arguments)
            {
                arguments.push_back(nlohmann::json{
                    {"name", argument.name},
                    {"description", argument.description},
                    {"required", argument.required}
                });
            }

            nlohmann::json entry{
                {"name", prompt.name},
                {"description", prompt.description},
                {"arguments", std::move(arguments)}
            };
            if (!prompt.title.empty())
            {
                entry["title"] = prompt.title;
            }
            list.push_back(std::move(entry));
        }
        return list;
    }
}
