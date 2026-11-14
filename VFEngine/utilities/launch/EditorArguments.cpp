#include "EditorArguments.hpp"

#include <charconv>
#include <system_error>

namespace launch
{
    std::optional<uint16_t> parseMcpPort(std::string_view text) noexcept
    {
        if (text.empty())
            return std::nullopt;

        int value = 0;
        const char* last = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(text.data(), last, value);
        if (ec != std::errc{} || ptr != last)
            return std::nullopt;
        if (value < minMcpPort || value > maxMcpPort)
            return std::nullopt;
        return static_cast<uint16_t>(value);
    }

    EditorArgumentsResult parseEditorArguments(std::span<const std::string_view> arguments)
    {
        EditorArgumentsResult result;
        EditorArguments& args = result.args;

        bool seenMcp = false;
        bool seenPort = false;
        bool seenToken = false;
        bool seenStatus = false;
        bool seenProject = false;

        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            const std::string_view arg = arguments[i];

            // A flag's value is the next argument. A value that looks like another flag means the
            // value was left out ("--mcp-port --mcp"); it is not consumed.
            auto takeValue = [&](std::string_view flag) -> std::optional<std::string_view>
            {
                if (i + 1 >= arguments.size() || arguments[i + 1].starts_with("--"))
                {
                    result.errors.push_back(std::string(flag) + " needs a value");
                    return std::nullopt;
                }
                return arguments[++i];
            };

            // Returns true for a repeat. The first occurrence is the one kept.
            auto isRepeat = [&](bool& seen, std::string_view flag)
            {
                if (seen)
                {
                    result.errors.push_back(std::string(flag) + " is given more than once");
                    return true;
                }
                seen = true;
                return false;
            };

            if (arg == "--mcp")
            {
                isRepeat(seenMcp, arg);
                args.mcpEnable = true;
            }
            else if (arg == "--mcp-port")
            {
                const bool repeat = isRepeat(seenPort, arg);
                if (auto value = takeValue(arg))
                {
                    auto port = parseMcpPort(*value);
                    if (!port)
                    {
                        result.errors.push_back("--mcp-port " + std::string(*value) + ": not a port in " +
                                                std::to_string(minMcpPort) + "-" + std::to_string(maxMcpPort));
                    }
                    else if (!repeat)
                    {
                        args.mcpPort = *port;
                    }
                }
            }
            else if (arg == "--mcp-token")
            {
                const bool repeat = isRepeat(seenToken, arg);
                if (auto value = takeValue(arg); value && !repeat)
                {
                    args.mcpToken = std::string(*value);
                }
            }
            else if (arg == "--launch-status")
            {
                const bool repeat = isRepeat(seenStatus, arg);
                if (auto value = takeValue(arg))
                {
                    if (value->empty())
                    {
                        result.errors.push_back("--launch-status needs a file path");
                    }
                    else if (!repeat)
                    {
                        args.launchStatusPath = std::string(*value);
                    }
                }
            }
            else if (arg.starts_with("-"))
            {
                result.errors.push_back("unknown argument " + std::string(arg));
            }
            else if (arg.empty())
            {
                result.errors.push_back("an empty argument is not a project path");
            }
            else if (!seenProject)
            {
                seenProject = true;
                args.projectPath = std::string(arg);
            }
            else
            {
                result.errors.push_back("unexpected extra argument " + std::string(arg) +
                                        " (the project path is " + args.projectPath + ")");
            }
        }

        return result;
    }

    std::string_view editorUsage() noexcept
    {
        return "usage: Editor.exe [project.vfproj] [--launch-status <file>] [--mcp] "
               "[--mcp-port <1024-65535>] [--mcp-token <token>]";
    }
}
