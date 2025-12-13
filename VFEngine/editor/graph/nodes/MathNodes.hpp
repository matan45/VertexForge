#pragma once
#include "ShaderNode.hpp"

namespace editor::graph {

    // Helper base class for binary math operations (A op B)
    class BinaryMathNode : public ShaderNodeBase {
    protected:
        std::string operation;  // GLSL operation ("+", "*", etc.)

        BinaryMathNode(material::NodeType nodeType, const std::string& nodeName, const std::string& op)
            : operation(op) {
            type = nodeType;
            name = nodeName;

            addInputPin("A", material::PinType::Float, 0.0f);
            addInputPin("B", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

    public:
        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string a = inputVarNames.count("A") ? inputVarNames.at("A") : "0.0";
            std::string b = inputVarNames.count("B") ? inputVarNames.at("B") : "0.0";
            return "float " + outputVarPrefix + "Result = " + a + " " + operation + " " + b + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Add node: A + B
    class AddNode : public BinaryMathNode {
    public:
        AddNode() : BinaryMathNode(material::NodeType::Add, "Add", "+") {}
    };

    // Subtract node: A - B
    class SubtractNode : public BinaryMathNode {
    public:
        SubtractNode() : BinaryMathNode(material::NodeType::Subtract, "Subtract", "-") {}
    };

    // Multiply node: A * B
    class MultiplyNode : public BinaryMathNode {
    public:
        MultiplyNode() : BinaryMathNode(material::NodeType::Multiply, "Multiply", "*") {}
    };

    // Divide node: A / B
    class DivideNode : public BinaryMathNode {
    public:
        DivideNode() : BinaryMathNode(material::NodeType::Divide, "Divide", "/") {}
    };

    // Power node: pow(A, B)
    class PowerNode : public ShaderNodeBase {
    public:
        PowerNode() {
            type = material::NodeType::Power;
            name = "Power";

            addInputPin("Base", material::PinType::Float, 0.0f);
            addInputPin("Exponent", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string base = inputVarNames.count("Base") ? inputVarNames.at("Base") : "0.0";
            std::string exp = inputVarNames.count("Exponent") ? inputVarNames.at("Exponent") : "1.0";
            return "float " + outputVarPrefix + "Result = pow(" + base + ", " + exp + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Lerp (Linear Interpolation): mix(A, B, Alpha)
    class LerpNode : public ShaderNodeBase {
    public:
        LerpNode() {
            type = material::NodeType::Lerp;
            name = "Lerp";

            addInputPin("A", material::PinType::Float, 0.0f);
            addInputPin("B", material::PinType::Float, 1.0f);
            addInputPin("Alpha", material::PinType::Float, 0.5f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string a = inputVarNames.count("A") ? inputVarNames.at("A") : "0.0";
            std::string b = inputVarNames.count("B") ? inputVarNames.at("B") : "1.0";
            std::string alpha = inputVarNames.count("Alpha") ? inputVarNames.at("Alpha") : "0.5";
            return "float " + outputVarPrefix + "Result = mix(" + a + ", " + b + ", " + alpha + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Clamp: clamp(Value, Min, Max)
    class ClampNode : public ShaderNodeBase {
    public:
        ClampNode() {
            type = material::NodeType::Clamp;
            name = "Clamp";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addInputPin("Min", material::PinType::Float, 0.0f);
            addInputPin("Max", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            std::string minVal = inputVarNames.count("Min") ? inputVarNames.at("Min") : "0.0";
            std::string maxVal = inputVarNames.count("Max") ? inputVarNames.at("Max") : "1.0";
            return "float " + outputVarPrefix + "Result = clamp(" + value + ", " + minVal + ", " + maxVal + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Saturate: clamp(Value, 0, 1)
    class SaturateNode : public ShaderNodeBase {
    public:
        SaturateNode() {
            type = material::NodeType::Saturate;
            name = "Saturate";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = clamp(" + value + ", 0.0, 1.0);\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // One Minus: 1.0 - Value
    class OneMinusNode : public ShaderNodeBase {
    public:
        OneMinusNode() {
            type = material::NodeType::OneMinus;
            name = "One Minus";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = 1.0 - " + value + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Abs: abs(Value)
    class AbsNode : public ShaderNodeBase {
    public:
        AbsNode() {
            type = material::NodeType::Abs;
            name = "Absolute";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = abs(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Floor
    class FloorNode : public ShaderNodeBase {
    public:
        FloorNode() {
            type = material::NodeType::Floor;
            name = "Floor";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = floor(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Ceil
    class CeilNode : public ShaderNodeBase {
    public:
        CeilNode() {
            type = material::NodeType::Ceil;
            name = "Ceil";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = ceil(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Fract
    class FractNode : public ShaderNodeBase {
    public:
        FractNode() {
            type = material::NodeType::Fract;
            name = "Fraction";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = fract(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Sin
    class SinNode : public ShaderNodeBase {
    public:
        SinNode() {
            type = material::NodeType::Sin;
            name = "Sine";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = sin(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Cos
    class CosNode : public ShaderNodeBase {
    public:
        CosNode() {
            type = material::NodeType::Cos;
            name = "Cosine";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string value = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "float " + outputVarPrefix + "Result = cos(" + value + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Dot product (vec3)
    class DotNode : public ShaderNodeBase {
    public:
        DotNode() {
            type = material::NodeType::Dot;
            name = "Dot Product";

            addInputPin("A", material::PinType::Vec3, glm::vec3(0.0f));
            addInputPin("B", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string a = inputVarNames.count("A") ? inputVarNames.at("A") : "vec3(0.0)";
            std::string b = inputVarNames.count("B") ? inputVarNames.at("B") : "vec3(0.0)";
            return "float " + outputVarPrefix + "Result = dot(" + a + ", " + b + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Cross product (vec3)
    class CrossNode : public ShaderNodeBase {
    public:
        CrossNode() {
            type = material::NodeType::Cross;
            name = "Cross Product";

            addInputPin("A", material::PinType::Vec3, glm::vec3(0.0f));
            addInputPin("B", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string a = inputVarNames.count("A") ? inputVarNames.at("A") : "vec3(0.0)";
            std::string b = inputVarNames.count("B") ? inputVarNames.at("B") : "vec3(0.0)";
            return "vec3 " + outputVarPrefix + "Result = cross(" + a + ", " + b + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Normalize (vec3)
    class NormalizeNode : public ShaderNodeBase {
    public:
        NormalizeNode() {
            type = material::NodeType::Normalize;
            name = "Normalize";

            addInputPin("Vector", material::PinType::Vec3, glm::vec3(0.0f, 1.0f, 0.0f));
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string vec = inputVarNames.count("Vector") ? inputVarNames.at("Vector") : "vec3(0.0, 1.0, 0.0)";
            return "vec3 " + outputVarPrefix + "Result = normalize(" + vec + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Length (vec3 -> float)
    class LengthNode : public ShaderNodeBase {
    public:
        LengthNode() {
            type = material::NodeType::Length;
            name = "Length";

            addInputPin("Vector", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string vec = inputVarNames.count("Vector") ? inputVarNames.at("Vector") : "vec3(0.0)";
            return "float " + outputVarPrefix + "Result = length(" + vec + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Make Vec3 from components
    class MakeVec3Node : public ShaderNodeBase {
    public:
        MakeVec3Node() {
            type = material::NodeType::MakeVec3;
            name = "Make Vector3";

            addInputPin("X", material::PinType::Float, 0.0f);
            addInputPin("Y", material::PinType::Float, 0.0f);
            addInputPin("Z", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string x = inputVarNames.count("X") ? inputVarNames.at("X") : "0.0";
            std::string y = inputVarNames.count("Y") ? inputVarNames.at("Y") : "0.0";
            std::string z = inputVarNames.count("Z") ? inputVarNames.at("Z") : "0.0";
            return "vec3 " + outputVarPrefix + "Result = vec3(" + x + ", " + y + ", " + z + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Fresnel effect
    class FresnelNode : public ShaderNodeBase {
    public:
        FresnelNode() {
            type = material::NodeType::Fresnel;
            name = "Fresnel";

            addInputPin("Normal", material::PinType::Vec3, glm::vec3(0.0f, 1.0f, 0.0f));
            addInputPin("ViewDir", material::PinType::Vec3, glm::vec3(0.0f, 0.0f, 1.0f));
            addInputPin("Power", material::PinType::Float, 5.0f);
            addOutputPin("Result", material::PinType::Float);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string normal = inputVarNames.count("Normal") ? inputVarNames.at("Normal") : "normalize(fragNormal)";
            std::string viewDir = inputVarNames.count("ViewDir") ? inputVarNames.at("ViewDir") : "normalize(camera.cameraPos - fragWorldPos)";
            std::string power = inputVarNames.count("Power") ? inputVarNames.at("Power") : "5.0";
            return "float " + outputVarPrefix + "Result = pow(1.0 - max(dot(" + normal + ", " + viewDir + "), 0.0), " + power + ");\n";
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
