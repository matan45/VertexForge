#include "AnimatorAsset.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>

namespace animator
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    std::optional<json> AnimatorAsset::readJsonFromFile(std::string_view path)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("Animator file not found: {}", path);
            return std::nullopt;
        }

        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read animator file size '{}': {}", path, ec.message());
            return std::nullopt;
        }

        constexpr size_t MAX_ANIMATOR_FILE_SIZE = 10 * 1024 * 1024;
        if (fileSize > MAX_ANIMATOR_FILE_SIZE)
        {
            vfLogError("Animator file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_ANIMATOR_FILE_SIZE);
            return std::nullopt;
        }

        json j;
        try { j = resource::readJsonFile(filePath.string()); }
        catch (const json::parse_error& e)
        {
            vfLogError("Animator file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }
        if (j.is_null())
        {
            vfLogError("Failed to open animator file: {}", path);
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Animator file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }
        return j;
    }

    std::optional<AnimatorData> AnimatorAsset::load(std::string_view path)
    {
        auto jsonOpt = readJsonFromFile(path);
        if (!jsonOpt.has_value())
            return std::nullopt;

        const json& j = jsonOpt.value();
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;

        auto logWarningLimited = [&](const std::string& msg)
        {
            if (warningCount < MAX_WARNINGS)
            {
                vfLogWarning("{}", msg);
                if (++warningCount == MAX_WARNINGS)
                    vfLogWarning("(suppressing further warnings for this file)");
            }
        };

        try
        {
            AnimatorData animator = parseAnimatorData(j, logWarningLimited);
            if (warningCount > 0)
                vfLogWarning("Loaded animator '{}' with {} warning(s)", animator.name, warningCount);

            vfLogInfo("Loaded animator '{}': {} states, {} transitions, {} parameters, {} layers",
                      animator.name, animator.graph.states.size(),
                      animator.graph.transitions.size(), animator.graph.parameters.size(),
                      animator.layers.size());
            return animator;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse animator file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading animator '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    AnimatorData AnimatorAsset::createDefault(const std::string& name)
    {
        AnimatorData animator;
        animator.name = name;
        animator.version = ANIMATOR_FORMAT_VERSION;

        animator.graph.entryPosition = glm::vec2(50.0f, 100.0f);
        animator.graph.anyStatePosition = glm::vec2(50.0f, 250.0f);

        AnimatorState idleState;
        idleState.id = animator.graph.nextStateId++;
        idleState.name = "Idle";
        idleState.loop = true;
        idleState.position = glm::vec2(250.0f, 100.0f);
        uint32_t idleId = idleState.id;
        animator.graph.states.push_back(std::move(idleState));

        animator.graph.defaultStateId = idleId;

        return animator;
    }
}
