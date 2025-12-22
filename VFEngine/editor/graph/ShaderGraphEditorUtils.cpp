#include "ShaderGraphEditor.hpp"

namespace editor::graph {

    const material::NodePin* ShaderGraphEditor::findPin(uint32_t pinId) const {
        for (const auto& node : currentGraph->nodes) {
            for (const auto& pin : node.inputs) {
                if (pin.id == pinId) return &pin;
            }
            for (const auto& pin : node.outputs) {
                if (pin.id == pinId) return &pin;
            }
        }
        return nullptr;
    }

    material::ShaderNode* ShaderGraphEditor::findNodeByPinId(uint32_t pinId) {
        for (auto& node : currentGraph->nodes) {
            for (const auto& pin : node.inputs) {
                if (pin.id == pinId) return &node;
            }
            for (const auto& pin : node.outputs) {
                if (pin.id == pinId) return &node;
            }
        }
        return nullptr;
    }

    bool ShaderGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const {
        
        const material::NodePin* startPin = findPin(startPinId);
        const material::NodePin* endPin = findPin(endPinId);

        if (!startPin || !endPin) return false;

        // Can't connect input to input or output to output
        if (startPin->kind == endPin->kind) return false;

        // Determine source (output) and target (input) pins
        const material::NodePin* sourcePin = (startPin->kind == material::PinKind::Output) ? startPin : endPin;
        const material::NodePin* targetPin = (startPin->kind == material::PinKind::Input) ? startPin : endPin;

        material::PinType srcType = sourcePin->type;
        material::PinType dstType = targetPin->type;
        
        // Same type is always compatible
        if (srcType == dstType) return true;

        //TODO https://matan33214.atlassian.net/browse/VK-56
        // Texture2D can only connect to Texture2D
        if (srcType == material::PinType::Texture2D || dstType == material::PinType::Texture2D) {
            return false;
        }

        // Float can connect to any vector type (broadcasts)
        if (srcType == material::PinType::Float) {
            return true;
        }

        // Vector types can connect to Float (takes first component)
        if (dstType == material::PinType::Float) {
            return true;
        }

        // Vector type conversions (with padding/truncating)
        return true;
    }

    ImU32 ShaderGraphEditor::getPinColor(material::PinType type) const {
        switch (type) {
            case material::PinType::Float:     return IM_COL32(150, 200, 150, 255);
            case material::PinType::Vec2:      return IM_COL32(150, 200, 255, 255);
            case material::PinType::Vec3:      return IM_COL32(255, 200, 150, 255);
            case material::PinType::Vec4:      return IM_COL32(255, 150, 200, 255);
            case material::PinType::Texture2D: return IM_COL32(200, 150, 255, 255);
            default:                           return IM_COL32(200, 200, 200, 255);
        }
    }

    ImU32 ShaderGraphEditor::getNodeHeaderColor(material::NodeType type) const {
        switch (type) {
            case material::NodeType::PBROutput:
                return IM_COL32(150, 80, 80, 255);
            case material::NodeType::ConstantScalar:
            case material::NodeType::ConstantVec2:
            case material::NodeType::ConstantVec3:
            case material::NodeType::ConstantColor:
                return IM_COL32(80, 150, 80, 255);
            case material::NodeType::Add:
            case material::NodeType::Subtract:
            case material::NodeType::Multiply:
            case material::NodeType::Divide:
            case material::NodeType::Lerp:
            case material::NodeType::Power:
                return IM_COL32(80, 80, 150, 255);
            case material::NodeType::VertexNormal:
            case material::NodeType::VertexUV:
            case material::NodeType::Time:
                return IM_COL32(150, 150, 80, 255);
            case material::NodeType::TextureSample:
                return IM_COL32(180, 100, 180, 255);
            case material::NodeType::OrmSample:
                return IM_COL32(180, 100, 180, 255);  // Same purple as TextureSample
            case material::NodeType::MixColor:
                return IM_COL32(100, 180, 100, 255);  // Green for blend nodes
            default:
                return IM_COL32(100, 100, 100, 255);
        }
    }

    std::string_view ShaderGraphEditor::getNodeTypeName(material::NodeType type) const {
        switch (type) {
            case material::NodeType::PBROutput:      return "PBR Output";
            case material::NodeType::ConstantScalar: return "Scalar";
            case material::NodeType::ConstantVec2:   return "Vector2";
            case material::NodeType::ConstantVec3:   return "Vector3";
            case material::NodeType::ConstantColor:  return "Color";
            case material::NodeType::Add:            return "Add";
            case material::NodeType::Subtract:       return "Subtract";
            case material::NodeType::Multiply:       return "Multiply";
            case material::NodeType::Divide:         return "Divide";
            case material::NodeType::Power:          return "Power";
            case material::NodeType::Lerp:           return "Lerp";
            case material::NodeType::Clamp:          return "Clamp";
            case material::NodeType::Saturate:       return "Saturate";
            case material::NodeType::OneMinus:       return "One Minus";
            case material::NodeType::Abs:            return "Abs";
            case material::NodeType::Floor:          return "Floor";
            case material::NodeType::Ceil:           return "Ceil";
            case material::NodeType::Fract:          return "Fract";
            case material::NodeType::Sin:            return "Sin";
            case material::NodeType::Cos:            return "Cos";
            case material::NodeType::Dot:            return "Dot";
            case material::NodeType::Cross:          return "Cross";
            case material::NodeType::Normalize:      return "Normalize";
            case material::NodeType::Length:         return "Length";
            case material::NodeType::MakeVec2:       return "Make Vec2";
            case material::NodeType::MakeVec3:       return "Make Vec3";
            case material::NodeType::MakeVec4:       return "Make Vec4";
            case material::NodeType::SplitVec2:      return "Split Vec2";
            case material::NodeType::SplitVec3:      return "Split Vec3";
            case material::NodeType::SplitVec4:      return "Split Vec4";
            case material::NodeType::Fresnel:        return "Fresnel";
            case material::NodeType::VertexNormal:   return "Normal";
            case material::NodeType::VertexUV:       return "UV";
            case material::NodeType::Time:           return "Time";
            case material::NodeType::TextureSample:  return "Texture Sample";
            case material::NodeType::OrmSample:      return "ORM Sample";
            case material::NodeType::MixColor:       return "Mix Color";
            default:                                 return "Unknown";
        }
    }

}
