#pragma once
#include "ShaderNode.hpp"
#include <algorithm>

namespace editor::graph {

    // Constant scalar (float) node
    class ConstantScalarNode : public ShaderNodeBase {
    public:
        ConstantScalarNode() {
            type = material::NodeType::ConstantScalar;
            name = "Constant";
            properties["value"] = 0.0f;

            addOutputPin("Value", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            float value = getPropertyValue<float>("value", 0.0f);
            return "float " + outputVarPrefix + "Value = " + floatToGLSL(value) + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Value";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Constant vec2 node
    class ConstantVec2Node : public ShaderNodeBase {
    public:
        ConstantVec2Node() {
            type = material::NodeType::ConstantVec2;
            name = "Vector2";
            properties["value"] = glm::vec2(0.0f);

            addOutputPin("Value", material::PinType::Vec2);
            addOutputPin("X", material::PinType::Float);
            addOutputPin("Y", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            glm::vec2 value = getPropertyValue<glm::vec2>("value", glm::vec2(0.0f));
            std::string code;
            code += "vec2 " + outputVarPrefix + "Value = " + vec2ToGLSL(value) + ";\n";
            code += "float " + outputVarPrefix + "X = " + outputVarPrefix + "Value.x;\n";
            code += "float " + outputVarPrefix + "Y = " + outputVarPrefix + "Value.y;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            if (pinName == "X") return outputVarPrefix + "X";
            if (pinName == "Y") return outputVarPrefix + "Y";
            return outputVarPrefix + "Value";
        }

        std::string getOutputType(const std::string& pinName) const override {
            if (pinName == "X" || pinName == "Y") return "float";
            return "vec2";
        }
    };

    // Constant vec3 node
    class ConstantVec3Node : public ShaderNodeBase {
    public:
        ConstantVec3Node() {
            type = material::NodeType::ConstantVec3;
            name = "Vector3";
            properties["value"] = glm::vec3(0.0f);

            addOutputPin("Value", material::PinType::Vec3);
            addOutputPin("X", material::PinType::Float);
            addOutputPin("Y", material::PinType::Float);
            addOutputPin("Z", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            glm::vec3 value = getPropertyValue<glm::vec3>("value", glm::vec3(0.0f));
            std::string code;
            code += "vec3 " + outputVarPrefix + "Value = " + vec3ToGLSL(value) + ";\n";
            code += "float " + outputVarPrefix + "X = " + outputVarPrefix + "Value.x;\n";
            code += "float " + outputVarPrefix + "Y = " + outputVarPrefix + "Value.y;\n";
            code += "float " + outputVarPrefix + "Z = " + outputVarPrefix + "Value.z;\n";
            return code;
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& pinName) const override {
            if (pinName == "X") return outputVarPrefix + "X";
            if (pinName == "Y") return outputVarPrefix + "Y";
            if (pinName == "Z") return outputVarPrefix + "Z";
            return outputVarPrefix + "Value";
        }

        std::string getOutputType(const std::string& pinName) const override {
            if (pinName == "X" || pinName == "Y" || pinName == "Z") return "float";
            return "vec3";
        }
    };

    // Constant color (vec4 with RGBA) node
    class ConstantColorNode : public ShaderNodeBase {
    public:
        ConstantColorNode() {
            type = material::NodeType::ConstantColor;
            name = "Color";
            properties["value"] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

            addOutputPin("RGBA", material::PinType::Vec4);
            addOutputPin("RGB", material::PinType::Vec3);
            addOutputPin("R", material::PinType::Float);
            addOutputPin("G", material::PinType::Float);
            addOutputPin("B", material::PinType::Float);
            addOutputPin("A", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            glm::vec4 value = getPropertyValue<glm::vec4>("value", glm::vec4(1.0f));
            std::string code;
            code += "vec4 " + outputVarPrefix + "RGBA = " + vec4ToGLSL(value) + ";\n";
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

    // Vertex UV coordinate input
    class VertexUVNode : public ShaderNodeBase {
    public:
        VertexUVNode() {
            type = material::NodeType::VertexUV;
            name = "Texture Coordinate";

            addOutputPin("UV", material::PinType::Vec2);
            addOutputPin("U", material::PinType::Float);
            addOutputPin("V", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            std::string code;
            code += "vec2 " + outputVarPrefix + "UV = fragTexCoord;\n";
            code += "float " + outputVarPrefix + "U = fragTexCoord.x;\n";
            code += "float " + outputVarPrefix + "V = fragTexCoord.y;\n";
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

    // Vertex normal input
    class VertexNormalNode : public ShaderNodeBase {
    public:
        VertexNormalNode() {
            type = material::NodeType::VertexNormal;
            name = "Vertex Normal";

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

    // Time input for animations
    class TimeNode : public ShaderNodeBase {
    public:
        TimeNode() {
            type = material::NodeType::Time;
            name = "Time";

            addOutputPin("Time", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& /*inputVarNames*/) const override {
            return "float " + outputVarPrefix + "Time = camera.u_Time;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Time";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Texture sampler node - samples a texture at UV coordinates
    class TextureSampleNode : public ShaderNodeBase {
    public:
        TextureSampleNode() {
            type = material::NodeType::TextureSample;
            name = "Texture Sample";
            properties["texturePath"] = std::string("");  // Path to .vfImage file
            properties["textureIndex"] = 0.0f;  // Index in texture array (for shader binding)

            addInputPin("UV", material::PinType::Vec2, glm::vec2(0.0f));  // Default uses vertex UV
            addOutputPin("RGBA", material::PinType::Vec4);
            addOutputPin("RGB", material::PinType::Vec3);
            addOutputPin("R", material::PinType::Float);
            addOutputPin("G", material::PinType::Float);
            addOutputPin("B", material::PinType::Float);
            addOutputPin("A", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            // Get UV input (use vertex UV if not connected or if using default value)
            std::string uvVar = "fragTexCoord";
            auto it = inputVarNames.find("UV");
            if (it != inputVarNames.end() && !it->second.empty()) {
                // Check if it's the default value (vec2(0.0, 0.0)) - if so, use fragTexCoord
                const std::string& uv = it->second;
                if (uv.find("vec2(0.0") == std::string::npos) {
                    uvVar = uv;
                }
            }

            // Get texture index from properties (clamped to valid range 0-5)
            int texIndex = static_cast<int>(getPropertyValue<float>("textureIndex", 0.0f));
            texIndex = std::clamp(texIndex, 0, 5);  // Max 6 textures per material (indices 0-5)

            std::string code;
            // Sample the texture - uses texture array indexed by textureIndex
            code += "vec4 " + outputVarPrefix + "RGBA = texture(u_Textures[" + std::to_string(texIndex) + "], " + uvVar + ");\n";
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

}
