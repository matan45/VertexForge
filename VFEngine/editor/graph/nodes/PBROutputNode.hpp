#pragma once
#include "ShaderNode.hpp"

namespace editor::graph {

    // PBR Output node - the final material output
    // This node collects all PBR material properties and is used by the compiler
    // to generate the final fragment shader output
    class PBROutputNode : public ShaderNodeBase {
    public:
        PBROutputNode() {
            type = material::NodeType::PBROutput;
            name = "PBR Output";

            // PBR material inputs with sensible defaults
            addInputPin("Albedo", material::PinType::Vec3, glm::vec3(0.8f, 0.8f, 0.8f));
            addInputPin("Normal", material::PinType::Vec3, glm::vec3(0.5f, 0.5f, 1.0f));  // Default normal in texture space [0,1]
            addInputPin("Metallic", material::PinType::Float, 0.0f);
            addInputPin("Roughness", material::PinType::Float, 0.5f);
            addInputPin("AO", material::PinType::Float, 1.0f);
            addInputPin("Emission", material::PinType::Vec3, glm::vec3(0.0f));
            addInputPin("EmissionStrength", material::PinType::Float, 1.0f);
            addInputPin("Opacity", material::PinType::Float, 1.0f);
            addInputPin("Diffuse", material::PinType::Float, 1.0f);
            addInputPin("Specular", material::PinType::Float, 0.5f);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string code;

            // Helper to convert vec3 variable to float (extracts .r component)
            // This handles cases like texture RGB connected to float inputs
            auto toFloat = [](const std::string& var, const std::string& defaultVal) -> std::string {
                if (var.empty()) return defaultVal;
                // If it looks like a vec3 variable (ends with RGB or contains vec3), extract .r
                if (var.find("_RGB") != std::string::npos ||
                    var.find("vec3") != std::string::npos ||
                    var.find("_Result") != std::string::npos) {
                    // Check if it's already a component access
                    if (var.back() == 'r' || var.back() == 'g' || var.back() == 'b' ||
                        var.back() == 'x' || var.back() == 'y' || var.back() == 'z') {
                        return var;
                    }
                    return "(" + var + ").r";
                }
                return var;
            };

            // Get input values or use defaults
            std::string albedo = inputVarNames.count("Albedo") ? inputVarNames.at("Albedo") : "vec3(0.8, 0.8, 0.8)";
            std::string normal = inputVarNames.count("Normal") ? inputVarNames.at("Normal") : "vec3(0.5, 0.5, 1.0)";
            std::string metallic = inputVarNames.count("Metallic") ? toFloat(inputVarNames.at("Metallic"), "0.0") : "0.0";
            std::string roughness = inputVarNames.count("Roughness") ? toFloat(inputVarNames.at("Roughness"), "0.5") : "0.5";
            std::string ao = inputVarNames.count("AO") ? toFloat(inputVarNames.at("AO"), "1.0") : "1.0";
            std::string emission = inputVarNames.count("Emission") ? inputVarNames.at("Emission") : "vec3(0.0)";
            std::string emissionStrength = inputVarNames.count("EmissionStrength") ? inputVarNames.at("EmissionStrength") : "0.0";
            std::string opacity = inputVarNames.count("Opacity") ? inputVarNames.at("Opacity") : "1.0";
            std::string iblDiffuse = inputVarNames.count("IBLDiffuse") ? toFloat(inputVarNames.at("IBLDiffuse"), "1.0") : "1.0";
            std::string iblSpecular = inputVarNames.count("IBLSpecular") ? toFloat(inputVarNames.at("IBLSpecular"), "0.5") : "0.5";

            // Assign to material output variables (these will be used by the PBR lighting code)
            code += "    // Material properties from shader graph\n";
            code += "    vec3 mat_albedo = " + albedo + ";\n";
            code += "    vec3 mat_normalTS = " + normal + ";\n";  // Tangent space normal (for future normal mapping)
            code += "    float mat_metallic = clamp(" + metallic + ", 0.0, 1.0);\n";
            code += "    float mat_roughness = clamp(" + roughness + ", 0.04, 1.0);\n";  // Min roughness to avoid divide by zero
            code += "    float mat_ao = clamp(" + ao + ", 0.0, 1.0);\n";
            code += "    vec3 mat_emissionColor = " + emission + ";\n";
            code += "    float mat_emissionStrength = " + emissionStrength + ";\n";
            code += "    float mat_opacity = clamp(" + opacity + ", 0.0, 1.0);\n";
            code += "    float mat_iblDiffuse = clamp(" + iblDiffuse + ", 0.0, 2.0);\n";
            code += "    float mat_iblSpecular = clamp(" + iblSpecular + ", 0.0, 2.0);\n";

            return code;
        }

        std::string getOutputVarName(const std::string& /*outputVarPrefix*/,
                                    const std::string& /*pinName*/) const override {
            // PBR Output has no output pins
            return "";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "";
        }

        // Helper to get the names of material output variables
        static std::string getAlbedoVar() { return "mat_albedo"; }
        static std::string getMetallicVar() { return "mat_metallic"; }
        static std::string getRoughnessVar() { return "mat_roughness"; }
        static std::string getAOVar() { return "mat_ao"; }
        static std::string getEmissionColorVar() { return "mat_emissionColor"; }
        static std::string getEmissionStrengthVar() { return "mat_emissionStrength"; }
        static std::string getOpacityVar() { return "mat_opacity"; }
        static std::string getIBLDiffuseVar() { return "mat_iblDiffuse"; }
        static std::string getIBLSpecularVar() { return "mat_iblSpecular"; }
    };

}
