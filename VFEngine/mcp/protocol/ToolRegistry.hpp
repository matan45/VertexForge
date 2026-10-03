#pragma once

#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp
{
    // Where a tool handler runs. Main = marshalled onto the editor main thread
    // (anything touching EventDispatcher handlers / EnTT / ImGui state). Worker =
    // runs directly on the HTTP connection thread (pure filesystem work, long
    // imports); such tools must hop to the main thread themselves for any
    // dispatcher call.
    enum class ThreadAffinity
    {
        Main,
        Worker
    };

    struct ToolResult
    {
        nlohmann::json structured;  // returned as structuredContent (wrapped in {"result":..} when not an object)
        std::string text;           // human-readable text; defaults to structured.dump(2)
        bool isError = false;
        // Content blocks emitted after the text block (MCP 2025-06-18), e.g. images.
        std::vector<nlohmann::json> extraContent;

        static ToolResult ok(nlohmann::json structured, std::string text = {});
        static ToolResult error(std::string message);
        // One {type:"image", data, mimeType} block; `base64` is the encoded image bytes.
        static ToolResult image(std::string base64, std::string mimeType, nlohmann::json structured,
                                std::string text = {});

        nlohmann::json toJson() const;
    };

    using ToolHandler = std::function<ToolResult(const nlohmann::json& args)>;

    struct ToolDef
    {
        std::string name;
        std::string title;
        std::string description;
        nlohmann::json inputSchema = nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        ToolHandler handler;
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::chrono::milliseconds timeout{10000};
        bool readOnly = false;     // annotations.readOnlyHint
        bool destructive = false;  // annotations.destructiveHint
        // Registration group: empty = core tools; "plugins" = PluginToolBridge (VK-1652).
        std::string owner;
    };

    // Thread-safe (VK-1652): core tools are added on the main thread before start;
    // plugin tools are swapped in and out at runtime by PluginToolBridge while
    // connection threads read. find() hands out shared ownership, so a tool removed
    // mid-call stays alive until that call finishes.
    class ToolRegistry
    {
    public:
        ToolRegistry();
        ToolRegistry(ToolRegistry&& other) noexcept;
        ToolRegistry& operator=(ToolRegistry&& other) noexcept;
        ToolRegistry(const ToolRegistry&) = delete;
        ToolRegistry& operator=(const ToolRegistry&) = delete;

        // Replaces an existing tool with the same name (and owner).
        void add(ToolDef tool);
        std::shared_ptr<const ToolDef> find(std::string_view name) const;
        std::size_t size() const;

        // Atomically replaces every tool whose owner == `owner` with `defs` (each
        // def's owner is forced to `owner`). A def whose name belongs to another
        // owner (e.g. a core tool) is skipped and reported in `rejected`. Returns
        // true when the tools/list output changed.
        bool replaceGroup(const std::string& owner, std::vector<ToolDef> defs,
                          std::vector<std::string>* rejected = nullptr);

        // Bumped on every change to the listing.
        uint64_t revision() const;

        // The `tools` array for a tools/list result, in registration order.
        nlohmann::json listJson() const;

    private:
        void rebuildIndex();

        std::unique_ptr<std::mutex> mutex;  // unique_ptr keeps the registry movable
        std::vector<std::shared_ptr<const ToolDef>> tools;
        std::unordered_map<std::string, std::size_t> indexByName;
        uint64_t rev = 0;
    };
}
