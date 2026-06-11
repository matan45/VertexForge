#pragma once
#include "ShaderNode.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace editor::graph {

    // World-space fragment position (fragWorldPos from the vertex stage)
    class WorldPositionNode : public ShaderNodeBase {
    public:
        WorldPositionNode() {
            type = material::NodeType::WorldPosition;
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

    // Scrolls UVs over time: uv + speed * time
    class PannerNode : public ShaderNodeBase {
    public:
        PannerNode() {
            type = material::NodeType::Panner;
            name = "Panner";

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f)); // default uses vertex UV
            addInputPin("Speed", material::PinType::Vec2, glm::vec2(0.1f, 0.1f));
            addOutputPin("UV", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string uvVar = "fragTexCoord";
            auto uvIt = inputVarNames.find("UV");
            if (uvIt != inputVarNames.end() && !uvIt->second.empty() &&
                uvIt->second.find("vec2(0.0") == std::string::npos) {
                uvVar = uvIt->second;
            }

            std::string speedVar = "vec2(0.1, 0.1)";
            auto speedIt = inputVarNames.find("Speed");
            if (speedIt != inputVarNames.end() && !speedIt->second.empty()) {
                speedVar = speedIt->second;
            }

            return "vec2 " + outputVarPrefix + "UV = " + uvVar + " + " + speedVar + " * camera.u_Time;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "UV";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Tiling / offset / rotation (degrees, around UV center 0.5,0.5)
    class UVTransformNode : public ShaderNodeBase {
    public:
        UVTransformNode() {
            type = material::NodeType::UVTransform;
            name = "UV Transform";
            properties["tiling"] = glm::vec2(1.0f, 1.0f);
            properties["offset"] = glm::vec2(0.0f, 0.0f);
            properties["rotationDegrees"] = 0.0f;

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f)); // default uses vertex UV
            addOutputPin("UV", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string uvVar = "fragTexCoord";
            auto uvIt = inputVarNames.find("UV");
            if (uvIt != inputVarNames.end() && !uvIt->second.empty() &&
                uvIt->second.find("vec2(0.0") == std::string::npos) {
                uvVar = uvIt->second;
            }

            glm::vec2 tiling = getPropertyValue<glm::vec2>("tiling", glm::vec2(1.0f));
            glm::vec2 offset = getPropertyValue<glm::vec2>("offset", glm::vec2(0.0f));
            float rotationDegrees = getPropertyValue<float>("rotationDegrees", 0.0f);
            float radians = rotationDegrees * glm::pi<float>() / 180.0f;

            std::string code;
            if (rotationDegrees != 0.0f) {
                std::string cr = floatToGLSL(std::cos(radians));
                std::string sr = floatToGLSL(std::sin(radians));
                code += "vec2 " + outputVarPrefix + "Centered = " + uvVar + " - vec2(0.5);\n";
                code += "vec2 " + outputVarPrefix + "Rotated = vec2(" +
                        outputVarPrefix + "Centered.x * " + cr + " - " + outputVarPrefix + "Centered.y * " + sr + ", " +
                        outputVarPrefix + "Centered.x * " + sr + " + " + outputVarPrefix + "Centered.y * " + cr +
                        ") + vec2(0.5);\n";
                code += "vec2 " + outputVarPrefix + "UV = " + outputVarPrefix + "Rotated * " +
                        vec2ToGLSL(tiling) + " + " + vec2ToGLSL(offset) + ";\n";
            } else {
                code += "vec2 " + outputVarPrefix + "UV = " + uvVar + " * " +
                        vec2ToGLSL(tiling) + " + " + vec2ToGLSL(offset) + ";\n";
            }
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "UV";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Remaps Value from [InMin, InMax] to [OutMin, OutMax]
    class RemapNode : public ShaderNodeBase {
    public:
        RemapNode() {
            type = material::NodeType::Remap;
            name = "Remap";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addInputPin("InMin", material::PinType::Float, 0.0f);
            addInputPin("InMax", material::PinType::Float, 1.0f);
            addInputPin("OutMin", material::PinType::Float, 0.0f);
            addInputPin("OutMax", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            auto input = [&inputVarNames](const char* pin, const char* fallback) -> std::string {
                auto it = inputVarNames.find(pin);
                return (it != inputVarNames.end() && !it->second.empty()) ? it->second : fallback;
            };

            std::string value = input("Value", "0.0");
            std::string inMin = input("InMin", "0.0");
            std::string inMax = input("InMax", "1.0");
            std::string outMin = input("OutMin", "0.0");
            std::string outMax = input("OutMax", "1.0");

            std::string code;
            code += "float " + outputVarPrefix + "T = (" + value + " - " + inMin + ") / "
                    "max(" + inMax + " - " + inMin + ", 1e-5);\n";
            code += "float " + outputVarPrefix + "Result = " + outMin + " + " + outputVarPrefix + "T * (" +
                    outMax + " - " + outMin + ");\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

}
