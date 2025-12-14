#include "ShaderGraphCompiler.hpp"
#include "nodes/ShaderNode.hpp"
#include "nodes/ConstantNodes.hpp"
#include "nodes/MathNodes.hpp"
#include "nodes/PBROutputNode.hpp"
#include <queue>
#include <set>
#include <algorithm>

namespace editor::graph {

    // Limits to prevent stack overflow from malicious/malformed material files
    static constexpr size_t MAX_RECURSION_DEPTH = 100;
    static constexpr size_t MAX_NODES = 1000;

    CompilationResult ShaderGraphCompiler::compile(const material::MaterialData& material) {
        return compileGraph(material.graph);
    }

    CompilationResult ShaderGraphCompiler::compileGraph(const material::ShaderGraph& graph) {
        CompilationResult result;

        // Validate node count to prevent DoS
        if (graph.nodes.size() > MAX_NODES) {
            result.success = false;
            result.errorMessage = "Shader graph exceeds maximum node limit (" + std::to_string(MAX_NODES) + ")";
            return result;
        }

        // Find output node
        const material::ShaderNode* outputNode = graph.findOutputNode();
        if (!outputNode) {
            result.success = false;
            result.errorMessage = "No PBR Output node found in shader graph";
            return result;
        }

        // Check for cycles/depth issues before generating shaders
        std::vector<uint32_t> sortedNodes = topologicalSort(graph);
        if (sortedNodes.empty() && !graph.nodes.empty()) {
            result.success = false;
            result.errorMessage = "Shader graph contains a cycle or exceeds maximum depth limit";
            return result;
        }

        // Generate shaders
        result.vertexShader = generateVertexShader();
        result.fragmentShader = generateFragmentShader(graph);
        result.success = true;

        return result;
    }

    std::string ShaderGraphCompiler::generateVertexShader() {
        // Standard vertex shader - same for all materials
        return R"(#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * worldPos;
}
)";
    }

    std::string ShaderGraphCompiler::generateFragmentShader(const material::ShaderGraph& graph) {
        std::string code;

        // Header
        code += R"(#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Material textures (max 8 textures per material)
layout(set = 1, binding = 0) uniform sampler2D u_Textures[8];

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

// PBR Functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
)";

        // Get topologically sorted nodes
        std::vector<uint32_t> sortedNodes = topologicalSort(graph);

        // Generate code for each node in order
        std::map<uint32_t, std::map<std::string, std::string>> nodeOutputVars;

        code += "    // Generated shader graph code\n";

        for (uint32_t nodeId : sortedNodes) {
            code += generateNodeCode(graph, nodeId, nodeOutputVars);
        }

        // PBR lighting calculation (uses mat_albedo, mat_metallic, etc. from PBROutputNode)
        code += R"(
    // PBR Lighting
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, mat_albedo, mat_metallic);

    // IBL Ambient Lighting
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, mat_roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - mat_metallic;

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * mat_albedo;

    // Specular IBL
    vec3 prefilteredColor = textureLod(prefilterMap, R, mat_roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), mat_roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    // Combine
    vec3 ambient = (kD * diffuse + specular) * mat_ao;
    vec3 color = ambient + mat_emission;

    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, mat_opacity);
}
)";

        return code;
    }

    std::vector<uint32_t> ShaderGraphCompiler::topologicalSort(const material::ShaderGraph& graph) {
        std::vector<uint32_t> result;
        std::set<uint32_t> visited;
        std::set<uint32_t> inStack;
        bool cycleDetected = false;

        // Build adjacency list (node -> nodes it depends on)
        std::map<uint32_t, std::set<uint32_t>> dependencies;
        for (const auto& node : graph.nodes) {
            dependencies[node.id] = {};
        }

        for (const auto& link : graph.links) {
            // Target node depends on source node
            dependencies[link.targetNodeId].insert(link.sourceNodeId);
        }

        // DFS-based topological sort with depth limit
        std::function<void(uint32_t, size_t)> visit = [&](uint32_t nodeId, size_t depth) {
            if (cycleDetected) return;
            if (visited.count(nodeId)) return;

            // Check depth limit to prevent stack overflow
            if (depth > MAX_RECURSION_DEPTH) {
                cycleDetected = true;
                return;
            }

            if (inStack.count(nodeId)) {
                // Cycle detected
                cycleDetected = true;
                return;
            }

            inStack.insert(nodeId);

            for (uint32_t dep : dependencies[nodeId]) {
                visit(dep, depth + 1);
                if (cycleDetected) return;
            }

            inStack.erase(nodeId);
            visited.insert(nodeId);
            result.push_back(nodeId);
        };

        // Visit all nodes, starting from output node
        const material::ShaderNode* outputNode = graph.findOutputNode();
        if (outputNode) {
            visit(outputNode->id, 0);
        }

        // If cycle detected, return empty result (will generate default shader)
        if (cycleDetected) {
            return {};
        }

        return result;
    }

    std::string ShaderGraphCompiler::generateNodeCode(const material::ShaderGraph& graph,
                                                      uint32_t nodeId,
                                                      std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars) {
        // Find the node
        const material::ShaderNode* nodeData = graph.findNode(nodeId);
        if (!nodeData) return "";

        // Create runtime node
        auto node = ShaderNodeFactory::createNodeFromData(*nodeData);
        if (!node) return "";

        // Build input variable map
        std::map<std::string, std::string> inputVarNames;
        for (const auto& pin : node->getInputPins()) {
            inputVarNames[pin.name] = getInputVarName(graph, nodeId, pin.name, nodeOutputVars);
        }

        // Generate code
        std::string prefix = "node_" + std::to_string(nodeId) + "_";
        std::string code = "    " + node->generateCode(prefix, inputVarNames);

        // Replace newlines with proper indentation
        size_t pos = 0;
        while ((pos = code.find('\n', pos)) != std::string::npos) {
            if (pos + 1 < code.size() && code[pos + 1] != '\n' && code[pos + 1] != '}') {
                code.insert(pos + 1, "    ");
                pos += 5;
            } else {
                pos++;
            }
        }

        // Store output variable names
        for (const auto& pin : node->getOutputPins()) {
            nodeOutputVars[nodeId][pin.name] = node->getOutputVarName(prefix, pin.name);
        }

        return code;
    }

    std::string ShaderGraphCompiler::getInputVarName(const material::ShaderGraph& graph,
                                                     uint32_t nodeId,
                                                     const std::string& pinName,
                                                     const std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars) {
        // Find link connected to this input
        for (const auto& link : graph.links) {
            if (link.targetNodeId == nodeId && link.targetPin == pinName) {
                // Found a connection - use the source node's output variable
                auto it = nodeOutputVars.find(link.sourceNodeId);
                if (it != nodeOutputVars.end()) {
                    auto pinIt = it->second.find(link.sourcePin);
                    if (pinIt != it->second.end()) {
                        return pinIt->second;
                    }
                }
            }
        }

        // No connection found - return default value based on pin type
        const material::ShaderNode* nodeData = graph.findNode(nodeId);
        if (nodeData) {
            auto node = ShaderNodeFactory::createNodeFromData(*nodeData);
            if (node) {
                for (const auto& pin : node->getInputPins()) {
                    if (pin.name == pinName && pin.defaultValue) {
                        // Format default value as GLSL
                        return std::visit([](auto&& arg) -> std::string {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, float>) {
                                char buffer[32];
                                snprintf(buffer, sizeof(buffer), "%.6f", arg);
                                return buffer;
                            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                                char buffer[64];
                                snprintf(buffer, sizeof(buffer), "vec2(%.6f, %.6f)", arg.x, arg.y);
                                return buffer;
                            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                                char buffer[96];
                                snprintf(buffer, sizeof(buffer), "vec3(%.6f, %.6f, %.6f)", arg.x, arg.y, arg.z);
                                return buffer;
                            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                                char buffer[128];
                                snprintf(buffer, sizeof(buffer), "vec4(%.6f, %.6f, %.6f, %.6f)", arg.x, arg.y, arg.z, arg.w);
                                return buffer;
                            }
                            return "0.0";
                        }, *pin.defaultValue);
                    }
                }
            }
        }

        // Ultimate fallback
        return "0.0";
    }

}
