#include "ShaderNode.hpp"
#include "ConstantNodes.hpp"
#include "MathNodes.hpp"
#include "PBROutputNode.hpp"

namespace editor::graph {

    std::unique_ptr<ShaderNodeBase> ShaderNodeFactory::createNode(material::NodeType type) {
        switch (type) {
            // Output
            case material::NodeType::PBROutput:
                return std::make_unique<PBROutputNode>();

            // Constants
            case material::NodeType::ConstantScalar:
                return std::make_unique<ConstantScalarNode>();
            case material::NodeType::ConstantVec2:
                return std::make_unique<ConstantVec2Node>();
            case material::NodeType::ConstantVec3:
                return std::make_unique<ConstantVec3Node>();
            case material::NodeType::ConstantColor:
                return std::make_unique<ConstantColorNode>();

            // Vertex inputs
            case material::NodeType::VertexUV:
                return std::make_unique<VertexUVNode>();
            case material::NodeType::VertexNormal:
                return std::make_unique<VertexNormalNode>();
            case material::NodeType::VertexPosition:
                return std::make_unique<VertexPositionNode>();
            case material::NodeType::CameraPosition:
                return std::make_unique<CameraPositionNode>();
            case material::NodeType::Time:
                return std::make_unique<TimeNode>();

            // Math operations
            case material::NodeType::Add:
                return std::make_unique<AddNode>();
            case material::NodeType::Subtract:
                return std::make_unique<SubtractNode>();
            case material::NodeType::Multiply:
                return std::make_unique<MultiplyNode>();
            case material::NodeType::Divide:
                return std::make_unique<DivideNode>();
            case material::NodeType::Power:
                return std::make_unique<PowerNode>();
            case material::NodeType::Lerp:
                return std::make_unique<LerpNode>();
            case material::NodeType::Clamp:
                return std::make_unique<ClampNode>();
            case material::NodeType::Saturate:
                return std::make_unique<SaturateNode>();
            case material::NodeType::OneMinus:
                return std::make_unique<OneMinusNode>();
            case material::NodeType::Abs:
                return std::make_unique<AbsNode>();
            case material::NodeType::Floor:
                return std::make_unique<FloorNode>();
            case material::NodeType::Ceil:
                return std::make_unique<CeilNode>();
            case material::NodeType::Fract:
                return std::make_unique<FractNode>();
            case material::NodeType::Sin:
                return std::make_unique<SinNode>();
            case material::NodeType::Cos:
                return std::make_unique<CosNode>();
            case material::NodeType::Dot:
                return std::make_unique<DotNode>();
            case material::NodeType::Cross:
                return std::make_unique<CrossNode>();
            case material::NodeType::Normalize:
                return std::make_unique<NormalizeNode>();
            case material::NodeType::Length:
                return std::make_unique<LengthNode>();
            case material::NodeType::MakeVec3:
                return std::make_unique<MakeVec3Node>();
            case material::NodeType::Fresnel:
                return std::make_unique<FresnelNode>();

            // Texture
            case material::NodeType::TextureSample:
                return std::make_unique<TextureSampleNode>();

            default:
                return std::make_unique<ConstantScalarNode>();  // Default fallback
        }
    }

    std::unique_ptr<ShaderNodeBase> ShaderNodeFactory::createNodeFromData(const material::ShaderNode& data) {
        auto node = createNode(data.type);
        if (node) {
            node->setId(data.id);
            node->setName(data.name.empty() ? node->getName() : data.name);
            node->setPosition(data.position);

            // Copy properties
            for (const auto& [key, value] : data.properties) {
                node->setProperty(key, value);
            }
        }
        return node;
    }

    void ShaderNodeFactory::initializeNode(material::ShaderNode& node, uint32_t& nextPinId) {
        // Create a temporary node to get the pin definitions
        auto tempNode = createNode(node.type);
        if (!tempNode) return;

        // Copy input pins with assigned IDs
        node.inputs.clear();
        for (const auto& pin : tempNode->getInputPins()) {
            material::NodePin newPin = pin;
            newPin.id = nextPinId++;
            node.inputs.push_back(newPin);
        }

        // Copy output pins with assigned IDs
        node.outputs.clear();
        for (const auto& pin : tempNode->getOutputPins()) {
            material::NodePin newPin = pin;
            newPin.id = nextPinId++;
            node.outputs.push_back(newPin);
        }

        // Copy default properties
        for (const auto& [key, value] : tempNode->getProperties()) {
            if (node.properties.find(key) == node.properties.end()) {
                node.properties[key] = value;
            }
        }
    }

}
