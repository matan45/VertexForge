#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"
#include "ContentHelpers.hpp"

#include "print/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        constexpr int64_t maxEntriesCap = 2000;

        util::LogLevel parseLevel(const std::string& name)
        {
            if (name == "trace")   return util::LogLevel::Trace;
            if (name == "debug")   return util::LogLevel::Debug;
            if (name == "info")    return util::LogLevel::Info;
            if (name == "warning") return util::LogLevel::Warning;
            if (name == "error")   return util::LogLevel::Error;
            throw ArgError("argument 'minLevel' must be one of trace, debug, info, warning, error");
        }

        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        void registerLogsRead(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "logs_read";
            tool.title = "Read editor log";
            tool.description =
                "Read the editor console log (engine + script output, oldest first). Poll incrementally by "
                "passing the previous result's lastSeq as sinceSeq. Without sinceSeq the most recent 'max' "
                "matching entries are returned. Script compile errors are logged with an '[Script]' prefix. "
                "Returns {entries:[{seq, level, message}], lastSeq, truncated}; truncated=true means more "
                "matching entries exist than were returned (with sinceSeq: call again with the new lastSeq).";
            tool.inputSchema = schema::object({
                {"sinceSeq", schema::integer("Only entries with seq > sinceSeq. Omit for the most recent entries.")},
                {"minLevel", schema::enumString("Minimum level. Default trace (everything).",
                                                {"trace", "debug", "info", "warning", "error"})},
                {"contains", schema::string("Case-insensitive substring filter on the message")},
                {"max", schema::integer("Maximum entries returned (1-2000). Default 200.")}
            });
            // Only touches the mutex-guarded console buffer, so it never needs the main thread.
            tool.affinity = ThreadAffinity::Worker;
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                ArgReader reader(args);
                std::optional<uint64_t> sinceSeq;
                if (reader.has("sinceSeq"))
                {
                    int64_t value = reader.requireInt("sinceSeq");
                    if (value < 0)
                    {
                        throw ArgError("argument 'sinceSeq' must be >= 0");
                    }
                    sinceSeq = static_cast<uint64_t>(value);
                }
                const util::LogLevel minLevel = parseLevel(reader.optString("minLevel", "trace"));
                const std::string needle = lowercase(reader.optString("contains"));
                const int64_t max = reader.optInt("max", 200);
                if (max < 1 || max > maxEntriesCap)
                {
                    throw ArgError("argument 'max' must be between 1 and 2000");
                }
                const std::size_t limit = static_cast<std::size_t>(max);

                const LogSelection selection = collectLogs(sinceSeq, minLevel, limit, needle);

                nlohmann::json entries = nlohmann::json::array();
                for (const util::LogEntry& entry : selection.entries)
                {
                    entries.push_back({
                        {"seq", entry.sequenceNumber},
                        {"level", logLevelName(entry.level)},
                        {"message", entry.message}
                    });
                }

                return ToolResult::ok({
                    {"entries", std::move(entries)},
                    {"lastSeq", selection.lastSeq.has_value() ? nlohmann::json(*selection.lastSeq)
                                                              : nlohmann::json(nullptr)},
                    {"truncated", selection.truncated}
                });
            };
            registry.add(std::move(tool));
        }
    }

    const char* logLevelName(util::LogLevel level)
    {
        switch (level)
        {
        case util::LogLevel::Trace:   return "trace";
        case util::LogLevel::Debug:   return "debug";
        case util::LogLevel::Info:    return "info";
        case util::LogLevel::Warning: return "warning";
        case util::LogLevel::Error:   return "error";
        default:                      return "info";
        }
    }

    LogSelection collectLogs(std::optional<uint64_t> sinceSeq, util::LogLevel minLevel, std::size_t max,
                             const std::string& containsLower)
    {
        auto matches = [&](const util::LogEntry& entry)
        {
            if (entry.level < minLevel)
            {
                return false;
            }
            return containsLower.empty() || lowercase(entry.message).find(containsLower) != std::string::npos;
        };

        // Copy under the lock (the logger appends from every thread); format outside it.
        LogSelection selection;
        selection.lastSeq = sinceSeq;
        std::vector<util::LogEntry>& selected = selection.entries;
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            const std::vector<util::LogEntry>& buffer = util::imguiConsoleBuffer;

            // Sequence numbers are assigned under this same lock and never reset
            // (the console's Clear empties the buffer but keeps the counter), so
            // the buffer is sorted by seq and sinceSeq stays meaningful.
            auto begin = buffer.begin();
            if (sinceSeq.has_value())
            {
                const uint64_t since = *sinceSeq;
                begin = std::upper_bound(buffer.begin(), buffer.end(), since,
                                         [](uint64_t value, const util::LogEntry& entry)
                                         {
                                             return value < entry.sequenceNumber;
                                         });
            }

            if (sinceSeq.has_value())
            {
                // Oldest first; stop at the cap so the caller can page forward.
                for (auto it = begin; it != buffer.end(); ++it)
                {
                    if (!matches(*it))
                    {
                        selection.lastSeq = it->sequenceNumber;
                        continue;
                    }
                    if (selected.size() == max)
                    {
                        selection.truncated = true;
                        break;
                    }
                    selected.push_back(*it);
                    selection.lastSeq = it->sequenceNumber;
                }
            }
            else
            {
                // Newest 'max' matches, collected backwards then restored to order.
                for (auto it = buffer.rbegin(); it != buffer.rend(); ++it)
                {
                    if (!matches(*it))
                    {
                        continue;
                    }
                    if (selected.size() == max)
                    {
                        selection.truncated = true;
                        break;
                    }
                    selected.push_back(*it);
                }
                std::reverse(selected.begin(), selected.end());
                if (!buffer.empty())
                {
                    selection.lastSeq = buffer.back().sequenceNumber;
                }
            }
        }
        return selection;
    }

    void registerLogTools(ToolRegistry& registry, const ToolContext&)
    {
        registerLogsRead(registry);
    }
}
