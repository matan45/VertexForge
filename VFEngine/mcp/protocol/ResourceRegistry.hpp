#pragma once

#include "ToolRegistry.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// MCP resources (VK-1652): read-only engine context the agent can pull without a
// tool call (resources/list, resources/templates/list, resources/read).
namespace mcp
{
    struct ResourceContents
    {
        std::string uri;
        std::string mimeType;
        std::string text;
    };

    // Thrown by a reader when the URI names nothing (e.g. a missing script):
    // resources/read answers JSON-RPC -32002 "Resource not found".
    class ResourceNotFound : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct ResourceDef
    {
        std::string uri;          // e.g. "vf://scene/hierarchy"
        std::string name;
        std::string title;
        std::string description;
        std::string mimeType;
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::chrono::milliseconds timeout{10000};
        std::function<ResourceContents()> reader;
    };

    // RFC 6570 template with one trailing variable, e.g. "vf://scripts/{+path}".
    // A URI matches when it starts with `prefix`; the remainder is percent-decoded
    // and passed to the reader.
    struct ResourceTemplateDef
    {
        std::string uriTemplate;
        std::string prefix;       // e.g. "vf://scripts/"
        std::string name;
        std::string title;
        std::string description;
        std::string mimeType;
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::chrono::milliseconds timeout{10000};
        std::function<ResourceContents(const std::string& uri, const std::string& decodedSuffix)> reader;
    };

    // Concrete resources computed on every resources/list (e.g. the project's game
    // scripts). Runs with the given affinity; only uri/name/title/description/
    // mimeType of the returned defs are used (reads go through the templates).
    struct ResourceListContributor
    {
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::function<std::vector<ResourceDef>()> list;
    };

    // Filled on the main thread before the server starts, then read-only.
    class ResourceRegistry
    {
    public:
        void add(ResourceDef resource);
        void addTemplate(ResourceTemplateDef resourceTemplate);
        void addContributor(ResourceListContributor contributor);

        const ResourceDef* findExact(std::string_view uri) const;
        // Longest matching prefix wins.
        const ResourceTemplateDef* findTemplate(std::string_view uri) const;

        const std::vector<ResourceDef>& resources() const { return staticResources; }
        const std::vector<ResourceTemplateDef>& templates() const { return resourceTemplates; }
        const std::vector<ResourceListContributor>& contributors() const { return listContributors; }

        // {uri, name, title?, description, mimeType} for resources/list.
        static nlohmann::json describe(const ResourceDef& resource);
        // {uriTemplate, name, title?, description, mimeType} for resources/templates/list.
        static nlohmann::json describe(const ResourceTemplateDef& resourceTemplate);

        // Percent-decodes (RFC 3986). Returns nullopt on a malformed escape or an
        // encoded NUL.
        static std::optional<std::string> percentDecode(std::string_view text);

    private:
        std::vector<ResourceDef> staticResources;
        std::vector<ResourceTemplateDef> resourceTemplates;
        std::vector<ResourceListContributor> listContributors;
    };
}
