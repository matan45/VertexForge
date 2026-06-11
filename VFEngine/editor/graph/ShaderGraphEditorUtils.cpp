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

    bool ShaderGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const {

        const material::NodePin* startPin = findPin(startPinId);
        const material::NodePin* endPin = findPin(endPinId);

        if (!startPin || !endPin) return false;

        if (startPin->kind == endPin->kind) return false;

        const material::NodePin* sourcePin = (startPin->kind == material::PinKind::Output) ? startPin : endPin;
        const material::NodePin* targetPin = (startPin->kind == material::PinKind::Input) ? startPin : endPin;

        // Use conversion nodes (FloatToVec3, Vec3ToFloat, etc.) for type conversions
        return sourcePin->type == targetPin->type;
    }

    std::string ShaderGraphEditor::getTypeMismatchMessage(uint32_t startPinId, uint32_t endPinId) const {
        const material::NodePin* startPin = findPin(startPinId);
        const material::NodePin* endPin = findPin(endPinId);

        if (!startPin || !endPin) return "";

        const material::NodePin* sourcePin = (startPin->kind == material::PinKind::Output) ? startPin : endPin;
        const material::NodePin* targetPin = (startPin->kind == material::PinKind::Input) ? startPin : endPin;

        if (sourcePin->type == targetPin->type) return "";

        std::string srcTypeName = material::pinTypeToString(sourcePin->type);
        std::string dstTypeName = material::pinTypeToString(targetPin->type);

        std::string conversionNode = getConversionNodeName(sourcePin->type, targetPin->type);

        return "Cannot connect " + srcTypeName + " to " + dstTypeName + ".\nUse '" + conversionNode + "' node.";
    }


    std::string ShaderGraphEditor::getConversionNodeName(material::PinType srcType, material::PinType dstType) const {
        if (srcType == material::PinType::Float) {
            switch (dstType) {
                case material::PinType::Vec2: return "Float To Vec2";
                case material::PinType::Vec3: return "Float To Vec3";
                case material::PinType::Vec4: return "Float To Vec4";
                default: break;
            }
        }
        else if (srcType == material::PinType::Vec2) {
            switch (dstType) {
                case material::PinType::Float: return "Vec2 To Float";
                case material::PinType::Vec3: return "Vec2 To Vec3";
                case material::PinType::Vec4: return "Vec2 To Vec4";
                default: break;
            }
        }
        else if (srcType == material::PinType::Vec3) {
            switch (dstType) {
                case material::PinType::Float: return "Vec3 To Float";
                case material::PinType::Vec2: return "Vec3 To Vec2";
                case material::PinType::Vec4: return "Vec3 To Vec4";
                default: break;
            }
        }
        else if (srcType == material::PinType::Vec4) {
            switch (dstType) {
                case material::PinType::Float: return "Vec4 To Float";
                case material::PinType::Vec2: return "Vec4 To Vec2";
                case material::PinType::Vec3: return "Vec4 To Vec3";
                default: break;
            }
        }

        return "a conversion";
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
            case material::NodeType::FloatToVec2:    return "Float To Vec2";
            case material::NodeType::FloatToVec3:    return "Float To Vec3";
            case material::NodeType::FloatToVec4:    return "Float To Vec4";
            case material::NodeType::Vec2ToFloat:    return "Vec2 To Float";
            case material::NodeType::Vec3ToFloat:    return "Vec3 To Float";
            case material::NodeType::Vec4ToFloat:    return "Vec4 To Float";
            case material::NodeType::Vec2ToVec3:     return "Vec2 To Vec3";
            case material::NodeType::Vec2ToVec4:     return "Vec2 To Vec4";
            case material::NodeType::Vec3ToVec2:     return "Vec3 To Vec2";
            case material::NodeType::Vec3ToVec4:     return "Vec3 To Vec4";
            case material::NodeType::Vec4ToVec2:     return "Vec4 To Vec2";
            case material::NodeType::Vec4ToVec3:     return "Vec4 To Vec3";
            case material::NodeType::WorldPosition:  return "World Position";
            case material::NodeType::Panner:         return "Panner";
            case material::NodeType::UVTransform:    return "UV Transform";
            case material::NodeType::Remap:          return "Remap";
            default:                                 return "Unknown";
        }
    }

}
