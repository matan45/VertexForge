#include "PhysicsSettingsSerialization.hpp"
#include "JsonConverters.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

namespace serialization
{
    using json = nlohmann::json;

    // JSON conversion for CollisionLayer
    void to_json(json& j, const types::CollisionLayer& layer)
    {
        j = json{
            {"index", layer.index},
            {"name", layer.name},
            {"builtIn", layer.isBuiltIn}
        };
    }

    void from_json(const json& j, types::CollisionLayer& layer)
    {
        if (j.contains("index")) layer.index = j["index"].get<uint8_t>();
        if (j.contains("name")) layer.name = j["name"].get<std::string>();
        if (j.contains("builtIn")) layer.isBuiltIn = j["builtIn"].get<bool>();
    }

    bool PhysicsSettingsSerialization::save(const types::PhysicsSettings& settings, std::string_view filename)
    {
        try
        {
            json j;
            j["version"] = "1.0";

            // Gravity
            j["gravity"] = json::array({settings.gravity.x, settings.gravity.y, settings.gravity.z});
            j["gravityScale"] = settings.gravityScale;

            // Simulation
            j["simulation"] = {
                {"fixedTimestep", settings.fixedTimestep},
                {"maxAccumulator", settings.maxAccumulator},
                {"maxStepsPerFrame", settings.maxStepsPerFrame}
            };

            // Sleep thresholds
            j["sleepThresholds"] = {
                {"linearVelocity", settings.linearSleepThreshold},
                {"angularVelocity", settings.angularSleepThreshold},
                {"timeToSleep", settings.timeToSleep}
            };

            // Collision layers
            j["collisionLayers"] = json::array();
            for (const auto& layer : settings.layers)
            {
                json layerJson;
                to_json(layerJson, layer);
                j["collisionLayers"].push_back(layerJson);
            }

            // Collision matrix - serialize as array of arrays of booleans
            j["collisionMatrix"] = json::array();
            for (size_t i = 0; i < settings.layers.size(); ++i)
            {
                json row = json::array();
                for (size_t k = 0; k < settings.layers.size(); ++k)
                {
                    row.push_back(settings.collisionMatrix[i].test(k));
                }
                j["collisionMatrix"].push_back(row);
            }

            std::ofstream file{std::string{filename}};
            if (!file.is_open())
            {
                spdlog::error("PhysicsSettingsSerialization: Failed to open file for writing: {}", filename);
                return false;
            }

            file << j.dump(2);
            file.close();

            spdlog::info("PhysicsSettingsSerialization: Saved physics settings to {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            spdlog::error("PhysicsSettingsSerialization: Failed to save: {}", e.what());
            return false;
        }
    }

    bool PhysicsSettingsSerialization::load(std::string_view filename, types::PhysicsSettings& settings)
    {
        try
        {
            std::ifstream file{std::string{filename}};
            if (!file.is_open())
            {
                spdlog::warn("PhysicsSettingsSerialization: File not found: {}", filename);
                return false;
            }

            json j;
            file >> j;
            file.close();

            // Gravity
            if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 3)
            {
                settings.gravity.x = j["gravity"][0].get<float>();
                settings.gravity.y = j["gravity"][1].get<float>();
                settings.gravity.z = j["gravity"][2].get<float>();
            }
            if (j.contains("gravityScale"))
            {
                settings.gravityScale = j["gravityScale"].get<float>();
            }

            // Simulation
            if (j.contains("simulation"))
            {
                const auto& sim = j["simulation"];
                if (sim.contains("fixedTimestep"))
                    settings.fixedTimestep = sim["fixedTimestep"].get<double>();
                if (sim.contains("maxAccumulator"))
                    settings.maxAccumulator = sim["maxAccumulator"].get<double>();
                if (sim.contains("maxStepsPerFrame"))
                    settings.maxStepsPerFrame = sim["maxStepsPerFrame"].get<int>();
            }

            // Sleep thresholds
            if (j.contains("sleepThresholds"))
            {
                const auto& sleep = j["sleepThresholds"];
                if (sleep.contains("linearVelocity"))
                    settings.linearSleepThreshold = sleep["linearVelocity"].get<float>();
                if (sleep.contains("angularVelocity"))
                    settings.angularSleepThreshold = sleep["angularVelocity"].get<float>();
                if (sleep.contains("timeToSleep"))
                    settings.timeToSleep = sleep["timeToSleep"].get<float>();
            }

            // Collision layers
            if (j.contains("collisionLayers") && j["collisionLayers"].is_array())
            {
                settings.layers.clear();
                for (const auto& layerJson : j["collisionLayers"])
                {
                    types::CollisionLayer layer;
                    from_json(layerJson, layer);
                    settings.layers.push_back(layer);
                }
            }

            // Collision matrix
            if (j.contains("collisionMatrix") && j["collisionMatrix"].is_array())
            {
                // Reset all collision matrix entries
                for (auto& row : settings.collisionMatrix)
                {
                    row.reset();
                }

                const auto& matrix = j["collisionMatrix"];
                for (size_t i = 0; i < matrix.size() && i < types::PhysicsSettings::MAX_LAYERS; ++i)
                {
                    if (matrix[i].is_array())
                    {
                        for (size_t k = 0; k < matrix[i].size() && k < types::PhysicsSettings::MAX_LAYERS; ++k)
                        {
                            if (matrix[i][k].is_boolean() && matrix[i][k].get<bool>())
                            {
                                settings.collisionMatrix[i].set(k);
                            }
                        }
                    }
                }
            }

            spdlog::info("PhysicsSettingsSerialization: Loaded physics settings from {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            spdlog::error("PhysicsSettingsSerialization: Failed to load: {}", e.what());
            return false;
        }
    }

    std::string PhysicsSettingsSerialization::getDefaultFilename()
    {
        return "physics.vfPhysicsConfig";
    }
}
