#pragma once
#include "ShaderNode.hpp"
#include <algorithm>
#include <format>

namespace editor::graph {

    // Terrain PBR Output - terminal node for terrain materials
    // Produces mat_albedo, mat_metallic, mat_roughness, mat_ao variables
    // consumed by the terrain fragment shader
    class TerrainPBROutputNode : public ShaderNodeBase {
    public:
        TerrainPBROutputNode() {
            type = material::NodeType::TerrainPBROutput;
            name = "Terrain PBR Output";

            addInputPin("Albedo", material::PinType::Vec3, glm::vec3(0.4f, 0.35f, 0.3f));
            addInputPin("Normal", material::PinType::Vec3, glm::vec3(0.5f, 0.5f, 1.0f));
        }

        std::string generateCode(const std::string& /*outputVarPrefix*/,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string albedo = inputVarNames.count("Albedo") ? inputVarNames.at("Albedo") : "vec3(0.400000, 0.350000, 0.300000)";
            std::string normal = inputVarNames.count("Normal") ? inputVarNames.at("Normal") : "vec3(0.500000, 0.500000, 1.000000)";

            std::string code;
            code += "// Terrain material properties from shader graph\n";
            code += "vec3 mat_albedo = " + albedo + ";\n";
            code += "vec3 mat_normalTS = " + normal + ";\n";
            code += "float mat_metallic = 0.0;\n";
            code += "float mat_roughness = 0.9;\n";
            code += "float mat_ao = 1.0;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& /*outputVarPrefix*/,
                                    const std::string& /*pinName*/) const override {
            return "";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "";
        }
    };

    // World Position - outputs fragWorldPos from terrain mesh shader
    class TerrainWorldPositionNode : public ShaderNodeBase {
    public:
        TerrainWorldPositionNode() {
            type = material::NodeType::TerrainWorldPosition;
            name = "World Position";

            addOutputPin("Position", material::PinType::Vec3);
            addOutputPin("X", material::PinType::Float);
            addOutputPin("Y", material::PinType::Float);
            addOutputPin("Z", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            std::string code;
            code += "vec3 " + outputVarPrefix + "Position = fragWorldPos;\n";
            code += "float " + outputVarPrefix + "X = fragWorldPos.x;\n";
            code += "float " + outputVarPrefix + "Y = fragWorldPos.y;\n";
            code += "float " + outputVarPrefix + "Z = fragWorldPos.z;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            if (pinName == "X") return outputVarPrefix + "X";
            if (pinName == "Y") return outputVarPrefix + "Y";
            if (pinName == "Z") return outputVarPrefix + "Z";
            return outputVarPrefix + "Position";
        }

        std::string getOutputType(const std::string& pinName) const override {
            if (pinName == "X" || pinName == "Y" || pinName == "Z") return "float";
            return "vec3";
        }
    };

    // World Normal - outputs normalized fragNormal from terrain mesh shader
    class TerrainWorldNormalNode : public ShaderNodeBase {
    public:
        TerrainWorldNormalNode() {
            type = material::NodeType::TerrainWorldNormal;
            name = "World Normal";

            addOutputPin("Normal", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            return "vec3 " + outputVarPrefix + "Normal = normalize(fragNormal);\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Normal";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // World UV - outputs fragWorldUV (world-space XZ tiled by terrainTextureScale)
    class TerrainWorldUVNode : public ShaderNodeBase {
    public:
        TerrainWorldUVNode() {
            type = material::NodeType::TerrainWorldUV;
            name = "World UV";

            addOutputPin("UV", material::PinType::Vec2);
            addOutputPin("U", material::PinType::Float);
            addOutputPin("V", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            std::string code;
            code += "vec2 " + outputVarPrefix + "UV = fragWorldUV;\n";
            code += "float " + outputVarPrefix + "U = fragWorldUV.x;\n";
            code += "float " + outputVarPrefix + "V = fragWorldUV.y;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            if (pinName == "U") return outputVarPrefix + "U";
            if (pinName == "V") return outputVarPrefix + "V";
            return outputVarPrefix + "UV";
        }

        std::string getOutputType(const std::string& pinName) const override {
            if (pinName == "U" || pinName == "V") return "float";
            return "vec2";
        }
    };

    // Height Sample - terrain height at the fragment's world position
    class TerrainHeightSampleNode : public ShaderNodeBase {
    public:
        TerrainHeightSampleNode() {
            type = material::NodeType::TerrainHeightSample;
            name = "Height Sample";

            addOutputPin("Height", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            return "float " + outputVarPrefix + "Height = fragWorldPos.y;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Height";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Slope Sample - terrain slope derived from world normal
    // 0.0 = flat (normal pointing up), 1.0 = vertical cliff
    class TerrainSlopeSampleNode : public ShaderNodeBase {
    public:
        TerrainSlopeSampleNode() {
            type = material::NodeType::TerrainSlopeSample;
            name = "Slope Sample";

            addOutputPin("Slope", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            return "float " + outputVarPrefix + "Slope = 1.0 - abs(dot(normalize(fragNormal), vec3(0.0, 1.0, 0.0)));\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Slope";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Weight Sample - samples weight map for a specific layer index
    // Until VK-215 adds real weight maps, uses fallback: layer 0 = 1.0, others = 0.0
    class TerrainWeightSampleNode : public ShaderNodeBase {
    public:
        TerrainWeightSampleNode() {
            type = material::NodeType::TerrainWeightSample;
            name = "Weight Sample";
            properties["layerIndex"] = 0.0f;

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f));
            addOutputPin("Weight", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            int layerIndex = static_cast<int>(getPropertyValue<float>("layerIndex", 0.0f));
            layerIndex = std::clamp(layerIndex, 0, 15);

            return std::format("float {}Weight = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, "
                "uint(tiles[fragTileIndex].aabbMin.w), {}u, fragTexCoord);\n",
                outputVarPrefix, layerIndex);
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Weight";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Layer Blend - blends two terrain layer colors by their weights
    // Result = (LayerA * WeightA + LayerB * WeightB) / max(WeightA + WeightB, 0.001)
    class TerrainLayerBlendNode : public ShaderNodeBase {
    public:
        TerrainLayerBlendNode() {
            type = material::NodeType::TerrainLayerBlend;
            name = "Layer Blend";

            addInputPin("LayerA", material::PinType::Vec3, glm::vec3(0.5f));
            addInputPin("LayerB", material::PinType::Vec3, glm::vec3(0.5f));
            addInputPin("WeightA", material::PinType::Float, 1.0f);
            addInputPin("WeightB", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string layerA = inputVarNames.count("LayerA") ? inputVarNames.at("LayerA") : "vec3(0.500000, 0.500000, 0.500000)";
            std::string layerB = inputVarNames.count("LayerB") ? inputVarNames.at("LayerB") : "vec3(0.500000, 0.500000, 0.500000)";
            std::string weightA = inputVarNames.count("WeightA") ? inputVarNames.at("WeightA") : "1.0";
            std::string weightB = inputVarNames.count("WeightB") ? inputVarNames.at("WeightB") : "0.0";

            std::string code;
            code += "float " + outputVarPrefix + "TotalW = max(" + weightA + " + " + weightB + ", 0.001);\n";
            code += "vec3 " + outputVarPrefix + "Result = (" + layerA + " * " + weightA + " + " + layerB + " * " + weightB + ") / " + outputVarPrefix + "TotalW;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Terrain Texture Sample - samples a terrain layer texture via the terrainLayerTextures array
    // layerIndex (0-15) and textureType (0=albedo, 1=normal) -> array index = layerIndex * 2 + textureType
    class TerrainTextureSampleNode : public ShaderNodeBase {
    public:
        TerrainTextureSampleNode() {
            type = material::NodeType::TerrainTextureSample;
            name = "Terrain Texture";
            properties["layerIndex"] = 0.0f;
            properties["textureType"] = 0.0f; // 0 = albedo, 1 = normal

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f));
            addOutputPin("RGBA", material::PinType::Vec4);
            addOutputPin("RGB", material::PinType::Vec3);
            addOutputPin("R", material::PinType::Float);
            addOutputPin("G", material::PinType::Float);
            addOutputPin("B", material::PinType::Float);
            addOutputPin("A", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            // Get UV input (use fragWorldUV if not connected or default)
            std::string uvVar = "fragWorldUV";
            auto it = inputVarNames.find("UV");
            if (it != inputVarNames.end() && !it->second.empty()) {
                const std::string& uv = it->second;
                if (uv.find("vec2(0.0") == std::string::npos) {
                    uvVar = uv;
                }
            }

            int layerIndex = static_cast<int>(getPropertyValue<float>("layerIndex", 0.0f));
            layerIndex = std::clamp(layerIndex, 0, 15);
            int texType = static_cast<int>(getPropertyValue<float>("textureType", 0.0f));
            texType = std::clamp(texType, 0, 1);
            int arrayIndex = layerIndex * 2 + texType;

            std::string code;
            code += "vec4 " + outputVarPrefix + "RGBA = texture(terrainLayerTextures[" + std::to_string(arrayIndex) + "], " + uvVar + ");\n";
            code += "vec3 " + outputVarPrefix + "RGB = " + outputVarPrefix + "RGBA.rgb;\n";
            code += "float " + outputVarPrefix + "R = " + outputVarPrefix + "RGBA.r;\n";
            code += "float " + outputVarPrefix + "G = " + outputVarPrefix + "RGBA.g;\n";
            code += "float " + outputVarPrefix + "B = " + outputVarPrefix + "RGBA.b;\n";
            code += "float " + outputVarPrefix + "A = " + outputVarPrefix + "RGBA.a;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            if (pinName == "RGB") return outputVarPrefix + "RGB";
            if (pinName == "R") return outputVarPrefix + "R";
            if (pinName == "G") return outputVarPrefix + "G";
            if (pinName == "B") return outputVarPrefix + "B";
            if (pinName == "A") return outputVarPrefix + "A";
            return outputVarPrefix + "RGBA";
        }

        std::string getOutputType(const std::string& pinName) const override {
            if (pinName == "R" || pinName == "G" || pinName == "B" || pinName == "A") return "float";
            if (pinName == "RGB") return "vec3";
            return "vec4";
        }
    };

    // Terrain Layer Stack - defines terrain layers and auto-blends them by weight maps.
    //
    // Each layer has: name, albedo texture path, normal texture path, tiling scale,
    // blend mode (Linear/HeightBased/Overlay), and enabled flag.
    // Properties:
    //   layerCount (1-16)
    //   layer{i}_name      (string) - user-facing layer name
    //   layer{i}_albedo    (string) - albedo texture file path
    //   layer{i}_normal    (string) - normal texture file path
    //   layer{i}_tiling    (float)  - UV tiling scale
    //   layer{i}_blendMode (string) - blend mode (Linear/HeightBased/Overlay)
    //   layer{i}_enabled   (float)  - 1.0 = enabled, 0.0 = disabled
    //
    // The paint brush system reads these layer definitions to let the user
    // pick which layer to paint. Weight maps control per-pixel blending.
    //
    // Outputs blended Albedo + Normal to connect to TerrainPBROutput.
    //
    // Current state (pre VK-215):
    //   - Texture paths are stored but not sampled (no GPU bindings yet)
    //   - Layer 0 weight = 1.0, others = 0.0 (no weight maps yet)
    //   - Falls back to default color
    // After VK-215: weight maps from painting, blending works
    class TerrainLayerStackNode : public ShaderNodeBase {
    public:
        TerrainLayerStackNode() {
            type = material::NodeType::TerrainLayerStack;
            name = "Layer Stack";
            properties["layerCount"] = 1.0f;

            // Initialize default layer 0
            properties["layer0_name"] = std::string("Layer 0");
            properties["layer0_albedo"] = std::string("");
            properties["layer0_normal"] = std::string("");
            properties["layer0_tiling"] = 1.0f;
            properties["layer0_blendMode"] = std::string("Linear");
            properties["layer0_enabled"] = 1.0f;

            addOutputPin("Albedo", material::PinType::Vec3);
            addOutputPin("Normal", material::PinType::Vec3);
        }

        // Ensure properties exist for all active layers
        void ensureLayerProperties(int layerCount) {
            for (int i = 0; i < layerCount; ++i) {
                std::string prefix = "layer" + std::to_string(i) + "_";
                if (properties.find(prefix + "name") == properties.end())
                    properties[prefix + "name"] = std::string("Layer " + std::to_string(i));
                if (properties.find(prefix + "albedo") == properties.end())
                    properties[prefix + "albedo"] = std::string("");
                if (properties.find(prefix + "normal") == properties.end())
                    properties[prefix + "normal"] = std::string("");
                if (properties.find(prefix + "tiling") == properties.end())
                    properties[prefix + "tiling"] = 1.0f;
                if (properties.find(prefix + "blendMode") == properties.end())
                    properties[prefix + "blendMode"] = std::string("Linear");
                if (properties.find(prefix + "enabled") == properties.end())
                    properties[prefix + "enabled"] = 1.0f;
            }
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            int layerCount = static_cast<int>(getPropertyValue<float>("layerCount", 1.0f));
            layerCount = std::clamp(layerCount, 1, 16);

            std::string code;
            code += "// Terrain Layer Stack - blending " + std::to_string(layerCount) + " layer(s)\n";
            code += "vec3 " + outputVarPrefix + "Albedo = vec3(0.0);\n";
            code += "vec3 " + outputVarPrefix + "Normal = vec3(0.0);\n";
            code += "float " + outputVarPrefix + "TotalW = 0.0;\n";

            for (int i = 0; i < layerCount; ++i) {
                std::string prefix = "layer" + std::to_string(i) + "_";

                // Skip disabled layers
                float enabledVal = getPropertyValue<float>(prefix + "enabled", 1.0f);
                if (enabledVal < 0.5f) {
                    std::string layerName = getPropertyValue<std::string>(prefix + "name", "Layer " + std::to_string(i));
                    code += "// Layer " + std::to_string(i) + " (" + layerName + ") - disabled\n";
                    continue;
                }

                std::string albedoPath = getPropertyValue<std::string>(prefix + "albedo", "");
                std::string normalPath = getPropertyValue<std::string>(prefix + "normal", "");
                float tiling = getPropertyValue<float>(prefix + "tiling", 1.0f);
                std::string blendMode = getPropertyValue<std::string>(prefix + "blendMode", "Linear");
                std::string layerName = getPropertyValue<std::string>(prefix + "name", "Layer " + std::to_string(i));

                code += "{ // Layer " + std::to_string(i) + " (" + layerName + ") - blend: " + blendMode + "\n";

                // Sample weight from weight map SSBO
                code += std::format("    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, "
                    "uint(tiles[fragTileIndex].aabbMin.w), {}u, fragTexCoord);\n", i);

                // Sample layer textures from bindless system via terrainLayers SSBO
                code += std::format("    vec2 layerUV = fragWorldUV * terrainLayers[{}].tilingScale;\n", i);
                code += std::format("    uint albedoIdx_{0} = terrainLayers[{0}].albedoTextureIndex;\n", i);
                code += std::format("    vec3 layerAlbedo = (albedoIdx_{0} > 0u) ? "
                    "texture(bindlessTextures[nonuniformEXT(albedoIdx_{0})], layerUV).rgb : vec3(0.5);\n", i);
                code += std::format("    uint normalIdx_{0} = terrainLayers[{0}].normalTextureIndex;\n", i);
                code += std::format("    vec3 layerNormal = (normalIdx_{0} > 0u) ? "
                    "texture(bindlessTextures[nonuniformEXT(normalIdx_{0})], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);\n", i);

                code += "    " + outputVarPrefix + "Albedo += layerAlbedo * w;\n";
                code += "    " + outputVarPrefix + "Normal += layerNormal * w;\n";
                code += "    " + outputVarPrefix + "TotalW += w;\n";
                code += "}\n";
            }

            // Normalize
            code += "float " + outputVarPrefix + "InvW = 1.0 / max(" + outputVarPrefix + "TotalW, 0.001);\n";
            code += outputVarPrefix + "Albedo *= " + outputVarPrefix + "InvW;\n";
            code += outputVarPrefix + "Normal = normalize(" + outputVarPrefix + "Normal);\n";

            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            return outputVarPrefix + pinName;
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

}
