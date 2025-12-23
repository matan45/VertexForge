#pragma once
#include "ShaderNode.hpp"

namespace editor::graph {

    // Float to Vec2: broadcasts value to both components
    class FloatToVec2Node : public ShaderNodeBase {
    public:
        FloatToVec2Node() {
            type = material::NodeType::FloatToVec2;
            name = "Float To Vec2";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec2 " + outputVarPrefix + "Result = vec2(" + val + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Float to Vec3: broadcasts value to all components
    class FloatToVec3Node : public ShaderNodeBase {
    public:
        FloatToVec3Node() {
            type = material::NodeType::FloatToVec3;
            name = "Float To Vec3";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec3 " + outputVarPrefix + "Result = vec3(" + val + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Float to Vec4: broadcasts value to xyz, w = 1.0
    class FloatToVec4Node : public ShaderNodeBase {
    public:
        FloatToVec4Node() {
            type = material::NodeType::FloatToVec4;
            name = "Float To Vec4";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec4);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec4 " + outputVarPrefix + "Result = vec4(vec3(" + val + "), 1.0);\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec4";
        }
    };

    // Vec2 to Float: extracts X component
    class Vec2ToFloatNode : public ShaderNodeBase {
    public:
        Vec2ToFloatNode() {
            type = material::NodeType::Vec2ToFloat;
            name = "Vec2 To Float";

            addInputPin("Value", material::PinType::Vec2, glm::vec2(0.0f));
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec2(0.0)";
            return "float " + outputVarPrefix + "Result = " + val + ".x;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec3 to Float: extracts X/R component
    class Vec3ToFloatNode : public ShaderNodeBase {
    public:
        Vec3ToFloatNode() {
            type = material::NodeType::Vec3ToFloat;
            name = "Vec3 To Float";

            addInputPin("Value", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec3(0.0)";
            return "float " + outputVarPrefix + "Result = " + val + ".x;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec4 to Float: extracts X/R component
    class Vec4ToFloatNode : public ShaderNodeBase {
    public:
        Vec4ToFloatNode() {
            type = material::NodeType::Vec4ToFloat;
            name = "Vec4 To Float";

            addInputPin("Value", material::PinType::Vec4, glm::vec4(0.0f));
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec4(0.0)";
            return "float " + outputVarPrefix + "Result = " + val + ".x;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec3 to Vec4: appends W component
    class Vec3ToVec4Node : public ShaderNodeBase {
    public:
        Vec3ToVec4Node() {
            type = material::NodeType::Vec3ToVec4;
            name = "Vec3 To Vec4";

            addInputPin("RGB", material::PinType::Vec3, glm::vec3(0.0f));
            addInputPin("W", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Vec4);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string rgb = inputVarNames.count("RGB") ? inputVarNames.at("RGB") : "vec3(0.0)";
            std::string w = inputVarNames.count("W") ? inputVarNames.at("W") : "1.0";
            return "vec4 " + outputVarPrefix + "Result = vec4(" + rgb + ", " + w + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec4";
        }
    };

    // Vec4 to Vec3: drops W component
    class Vec4ToVec3Node : public ShaderNodeBase {
    public:
        Vec4ToVec3Node() {
            type = material::NodeType::Vec4ToVec3;
            name = "Vec4 To Vec3";

            addInputPin("Value", material::PinType::Vec4, glm::vec4(0.0f));
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec4(0.0)";
            return "vec3 " + outputVarPrefix + "Result = " + val + ".xyz;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

}
