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

    // Sprite-sheet (flipbook) UV remap. Selects a cell from camera.u_Time and outputs a vec2 UV
    // to feed into a TextureSample UV input (samples nothing itself). The +uv offset is folded
    // into the output, so feed this node's output straight into TextureSample (do not chain a
    // tiling node after it). Mirrors resources/shaders/billboard/billboard.glsl and
    // render::computeFlipbookFrame (utilities/render/FlipbookMath.hpp).
    class FlipbookNode : public ShaderNodeBase {
    public:
        FlipbookNode() {
            type = material::NodeType::Flipbook;
            name = "Flipbook";

            properties["rows"] = 4.0f;
            properties["columns"] = 4.0f;
            properties["framesPerSecond"] = 30.0f;
            properties["loop"] = 1.0f;          // 1 = loop, 0 = play-once (hold last)
            properties["totalFrames"] = 0.0f;   // 0 = rows*columns

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f)); // default uses vertex UV
            addInputPin("Time", material::PinType::Float, 0.0f);         // default uses camera.u_Time
            addOutputPin("UV", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            // UV input: Panner default-detection idiom (unconnected -> vertex UV).
            std::string uvVar = "fragTexCoord";
            auto uvIt = inputVarNames.find("UV");
            if (uvIt != inputVarNames.end() && !uvIt->second.empty() &&
                uvIt->second.find("vec2(0.0") == std::string::npos) {
                uvVar = uvIt->second;
            }

            // Time input: float default 0.0 serializes to "0.000000" when unconnected => use
            // camera.u_Time. A wired Time source resolves to a node_<id>_* var name.
            std::string timeVar = "camera.u_Time";
            auto timeIt = inputVarNames.find("Time");
            if (timeIt != inputVarNames.end() && !timeIt->second.empty() &&
                timeIt->second != "0.000000") {
                timeVar = timeIt->second;
            }

            float cols = getPropertyValue<float>("columns", 4.0f);
            float rows = getPropertyValue<float>("rows", 4.0f);
            float fps  = getPropertyValue<float>("framesPerSecond", 30.0f);
            float loop = getPropertyValue<float>("loop", 1.0f);
            float totalOverride = getPropertyValue<float>("totalFrames", 0.0f);

            // Guard degenerate authoring values (at least one cell per axis).
            if (cols < 1.0f) cols = 1.0f;
            if (rows < 1.0f) rows = 1.0f;
            float total = (totalOverride >= 1.0f) ? totalOverride : (cols * rows);

            const std::string colsL  = floatToGLSL(cols);
            const std::string rowsL  = floatToGLSL(rows);
            const std::string fpsL   = floatToGLSL(fps);
            const std::string totalL = floatToGLSL(total);

            const std::string p = outputVarPrefix;
            std::string code;
            // total<=1 or fps<=0 => static full rect (matches computeFlipbookFrame early-out).
            if (total <= 1.0f || fps <= 0.0f) {
                code += "vec2 " + p + "UV = " + uvVar + ";\n";
                return code;
            }

            code += "float " + p + "frame;\n";
            if (loop != 0.0f) {
                code += p + "frame = floor(mod(" + timeVar + " * " + fpsL + ", " + totalL + "));\n";
            } else {
                code += p + "frame = min(floor(" + timeVar + " * " + fpsL + "), " + totalL + " - 1.0);\n";
                code += p + "frame = max(" + p + "frame, 0.0);\n";
            }
            code += "float " + p + "col = mod(" + p + "frame, " + colsL + ");\n";
            code += "float " + p + "row = floor(" + p + "frame / " + colsL + ");\n";
            code += "vec2 "  + p + "tileSize = vec2(1.0 / " + colsL + ", 1.0 / " + rowsL + ");\n";
            code += "vec2 "  + p + "UV = (vec2(" + p + "col, " + p + "row) + " + uvVar + ") * " + p + "tileSize;\n";
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

    // Rotates UV around a center over time (UE5 "Rotator"). angle = speed(rot/sec) * time * 2*PI.
    // cos/sin are emitted as runtime GLSL calls (NOT host-baked like UVTransform) so the rotation
    // animates. The Speed pin (sentinel default 0.0) overrides the rotationSpeed property when wired.
    class RotatorNode : public ShaderNodeBase {
    public:
        RotatorNode() {
            type = material::NodeType::Rotator;
            name = "Rotator";

            properties["rotationSpeed"] = 0.25f;             // rotations per second
            properties["center"] = glm::vec2(0.5f, 0.5f);

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f)); // default uses vertex UV
            addInputPin("Time", material::PinType::Float, 0.0f);        // default uses camera.u_Time
            addInputPin("Speed", material::PinType::Float, 0.0f);       // 0 = use rotationSpeed property
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

            std::string timeVar = "camera.u_Time";
            auto timeIt = inputVarNames.find("Time");
            if (timeIt != inputVarNames.end() && !timeIt->second.empty() &&
                timeIt->second != "0.000000") {
                timeVar = timeIt->second;
            }

            // Speed: wired pin (non-sentinel) overrides the rotationSpeed property.
            std::string speedVar = floatToGLSL(getPropertyValue<float>("rotationSpeed", 0.25f));
            auto speedIt = inputVarNames.find("Speed");
            if (speedIt != inputVarNames.end() && !speedIt->second.empty() &&
                speedIt->second != "0.000000") {
                speedVar = speedIt->second;
            }

            glm::vec2 center = getPropertyValue<glm::vec2>("center", glm::vec2(0.5f, 0.5f));
            const std::string centerVar = vec2ToGLSL(center);

            const std::string p = outputVarPrefix;
            std::string code;
            // angle (radians) = rotations/sec * seconds * 2*PI -- RUNTIME, so cos/sin stay in GLSL.
            code += "float " + p + "Angle = (" + speedVar + ") * (" + timeVar + ") * 6.28318530718;\n";
            code += "float " + p + "Cos = cos(" + p + "Angle);\n";
            code += "float " + p + "Sin = sin(" + p + "Angle);\n";
            code += "vec2 "  + p + "Centered = " + uvVar + " - " + centerVar + ";\n";
            code += "vec2 "  + p + "UV = vec2(" +
                    p + "Centered.x * " + p + "Cos - " + p + "Centered.y * " + p + "Sin, " +
                    p + "Centered.x * " + p + "Sin + " + p + "Centered.y * " + p + "Cos) + " +
                    centerVar + ";\n";
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

    // Rotates UV by a caller-supplied angle (radians) about a caller-supplied center (UE5
    // "CustomRotator"). All-pin, no properties -> data-driven; compose with Time/Multiply to
    // rebuild a Rotator. Runtime cos/sin.
    class CustomRotatorNode : public ShaderNodeBase {
    public:
        CustomRotatorNode() {
            type = material::NodeType::CustomRotator;
            name = "Custom Rotator";

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f));       // default uses vertex UV
            addInputPin("Center", material::PinType::Vec2, glm::vec2(0.5f));   // default center (0.5,0.5)
            addInputPin("Rotation", material::PinType::Float, 0.0f);           // radians
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

            std::string centerVar = "vec2(0.5)";
            auto centerIt = inputVarNames.find("Center");
            if (centerIt != inputVarNames.end() && !centerIt->second.empty()) {
                centerVar = centerIt->second;
            }

            std::string angleVar = "0.0";
            auto rotIt = inputVarNames.find("Rotation");
            if (rotIt != inputVarNames.end() && !rotIt->second.empty()) {
                angleVar = rotIt->second;
            }

            const std::string p = outputVarPrefix;
            std::string code;
            code += "float " + p + "Cos = cos(" + angleVar + ");\n";
            code += "float " + p + "Sin = sin(" + angleVar + ");\n";
            code += "vec2 "  + p + "Centered = " + uvVar + " - " + centerVar + ";\n";
            code += "vec2 "  + p + "UV = vec2(" +
                    p + "Centered.x * " + p + "Cos - " + p + "Centered.y * " + p + "Sin, " +
                    p + "Centered.x * " + p + "Sin + " + p + "Centered.y * " + p + "Cos) + " +
                    centerVar + ";\n";
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

}
