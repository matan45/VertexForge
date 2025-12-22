#include "ShaderGraphCompiler.hpp"
#include "nodes/ShaderNode.hpp"
#include "print/EditorLogger.hpp"
#include <queue>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <format>

namespace editor::graph {

    // Limits to prevent stack overflow from malicious/malformed material files
    static constexpr size_t MAX_RECURSION_DEPTH = 100;
    static constexpr size_t MAX_NODES = 1000;

    // Shader template paths (relative to executable)
    static constexpr std::string_view VERTEX_TEMPLATE_PATH = "../../resources/shaders/material/material_vertex.glsl";
    static constexpr std::string_view FRAGMENT_HEADER_PATH = "../../resources/shaders/material/material_fragment_header.glsl";
    static constexpr std::string_view FRAGMENT_FOOTER_PATH = "../../resources/shaders/material/material_fragment_footer.glsl";

    // Allowed base directory for shader files (relative to executable)
    static constexpr std::string_view ALLOWED_SHADER_DIR = "../../resources/shaders";

    // Static member initialization
    std::string ShaderGraphCompiler::s_vertexTemplate;
    std::string ShaderGraphCompiler::s_fragmentHeader;
    std::string ShaderGraphCompiler::s_fragmentFooter;
    bool ShaderGraphCompiler::s_templatesLoaded = false;

    CompilationResult ShaderGraphCompiler::compile(const material::MaterialData& material) {
        return compileGraph(material.graph);
    }

    CompilationResult ShaderGraphCompiler::compileGraph(const material::ShaderGraph& graph) {
        CompilationResult result;

        // Load templates if not already loaded
        if (!loadTemplates()) {
            result.success = false;
            result.errorMessage = "Failed to load shader templates from files";
            return result;
        }

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

    std::string ShaderGraphCompiler::readTextFile(std::string_view path) {
        namespace fs = std::filesystem;
        std::string pathStr(path);

        // Validate path is within allowed shader directory to prevent path traversal attacks
        try {
            fs::path requestedPath = fs::weakly_canonical(pathStr);
            fs::path allowedDir = fs::weakly_canonical(std::string(ALLOWED_SHADER_DIR));

            // Check that the requested path starts with the allowed directory
            auto [reqIt, allowIt] = std::mismatch(
                requestedPath.begin(), requestedPath.end(),
                allowedDir.begin(), allowedDir.end()
            );

            if (allowIt != allowedDir.end()) {
                vfLogError("Path traversal attempt detected, path outside allowed directory: {}", pathStr);
                return "";
            }
        } catch (const fs::filesystem_error& e) {
            vfLogError("Path validation failed: {}", e.what());
            return "";
        }

        std::ifstream file(pathStr);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    bool ShaderGraphCompiler::loadTemplates() {
        if (s_templatesLoaded) {
            return true;
        }

        // Load vertex shader template
        s_vertexTemplate = readTextFile(VERTEX_TEMPLATE_PATH);
        if (s_vertexTemplate.empty()) {
            vfLogError("Failed to load vertex shader template from: {}", VERTEX_TEMPLATE_PATH);
            return false;
        }

        // Load fragment shader header
        s_fragmentHeader = readTextFile(FRAGMENT_HEADER_PATH);
        if (s_fragmentHeader.empty()) {
            vfLogError("Failed to load fragment shader header from: {}", FRAGMENT_HEADER_PATH);
            return false;
        }

        // Load fragment shader footer
        s_fragmentFooter = readTextFile(FRAGMENT_FOOTER_PATH);
        if (s_fragmentFooter.empty()) {
            vfLogError("Failed to load fragment shader footer from: {}", FRAGMENT_FOOTER_PATH);
            return false;
        }

        s_templatesLoaded = true;
        vfLogInfo("Loaded shader templates from files");
        return true;
    }

    void ShaderGraphCompiler::reloadTemplates() {
        s_templatesLoaded = false;
        s_vertexTemplate.clear();
        s_fragmentHeader.clear();
        s_fragmentFooter.clear();
        loadTemplates();
    }

    std::string ShaderGraphCompiler::generateVertexShader() {
        return s_vertexTemplate;
    }

    std::string ShaderGraphCompiler::generateFragmentShader(const material::ShaderGraph& graph) {
        std::string code;

        // Start with cached fragment header (includes uniforms, PBR functions, and main() opening)
        code += s_fragmentHeader;

        // Get topologically sorted nodes
        std::vector<uint32_t> sortedNodes = topologicalSort(graph);

        // Generate code for each node in order
        std::map<uint32_t, std::map<std::string, std::string>> nodeOutputVars;

        code += "    // Generated shader graph code\n";

        for (uint32_t nodeId : sortedNodes) {
            code += generateNodeCode(graph, nodeId, nodeOutputVars);
        }

        // Append cached fragment footer (PBR lighting calculation and main() closing)
        code += s_fragmentFooter;

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

        // Track which node caused the issue for debugging
        uint32_t problemNodeId = 0;
        std::string problemReason;

        // DFS-based topological sort with depth limit
        std::function<void(uint32_t, size_t)> visit = [&](uint32_t nodeId, size_t depth) {
            if (cycleDetected) return;
            if (visited.count(nodeId)) return;

            // Check if node exists in graph
            if (dependencies.find(nodeId) == dependencies.end()) {
                vfLogWarning("Link references non-existent node {}, skipping", nodeId);
                return;
            }

            // Check depth limit to prevent stack overflow
            if (depth > MAX_RECURSION_DEPTH) {
                cycleDetected = true;
                problemNodeId = nodeId;
                problemReason = "depth limit exceeded";
                return;
            }

            if (inStack.count(nodeId)) {
                // Cycle detected
                cycleDetected = true;
                problemNodeId = nodeId;
                problemReason = "cycle detected";
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

        // If cycle detected, log detailed error
        if (cycleDetected) {
            const material::ShaderNode* problemNode = graph.findNode(problemNodeId);
            std::string nodeName = problemNode ? problemNode->name : "unknown";
            vfLogError("Shader graph error at node '{}' (id={}): {}", nodeName, problemNodeId, problemReason);
            return {};
        }

        return result;
    }

    int ShaderGraphCompiler::determinePBRTextureIndex(const material::ShaderGraph& graph, uint32_t nodeId) {
        std::set<uint32_t> visited;
        std::queue<uint32_t> toVisit;
        toVisit.push(nodeId);

        size_t iterations = 0;
        while (!toVisit.empty() && iterations < MAX_NODES) {
            ++iterations;
            uint32_t currentId = toVisit.front();
            toVisit.pop();

            if (visited.contains(currentId)) continue;
            visited.insert(currentId);

            // Find all links where this node is the source
            for (const auto& link : graph.links) {
                if (link.sourceNodeId == currentId) {
                    // Check if target is PBR output node
                    const material::ShaderNode* targetNode = graph.findNode(link.targetNodeId);
                    if (targetNode && targetNode->type == material::NodeType::PBROutput) {
                        // Found connection to PBR output - return the index for this pin
                        auto it = pbrPinToIndex.find(link.targetPin);
                        if (it != pbrPinToIndex.end()) {
                            return it->second;
                        }
                    }
                    // Continue searching through this node
                    toVisit.push(link.targetNodeId);
                }
            }
        }

        return -1; // Not connected to PBR output
    }

    std::string ShaderGraphCompiler::generateNodeCode(const material::ShaderGraph& graph,
                                                      uint32_t nodeId,
                                                      std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars) {
        const material::ShaderNode* nodeData = graph.findNode(nodeId);
        if (!nodeData) return "";

        // Create a mutable copy of node data for TextureSample nodes
        // so we can set the correct texture index based on PBR connection
        material::ShaderNode modifiedNodeData = *nodeData;

        // For TextureSample nodes, determine correct texture index based on PBR connection
        // Note: OrmSample nodes always use slot 2 (ORM) - they have multiple outputs from one texture
        if (nodeData->type == material::NodeType::TextureSample) {
            int pbrIndex = determinePBRTextureIndex(graph, nodeId);
            if (pbrIndex >= 0) {
                modifiedNodeData.properties["textureIndex"] = static_cast<float>(pbrIndex);
            }
        }
        
        auto node = ShaderNodeFactory::createNodeFromData(modifiedNodeData);
        if (!node) return "";
        
        std::map<std::string, std::string> inputVarNames;
        for (const auto& pin : node->getInputPins()) {
            inputVarNames[pin.name] = getInputVarName(graph, nodeId, pin.name, nodeOutputVars);
        }
        
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
                                return std::format("{:.6f}", arg);
                            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                                return std::format("vec2({:.6f}, {:.6f})", arg.x, arg.y);
                            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                                return std::format("vec3({:.6f}, {:.6f}, {:.6f})", arg.x, arg.y, arg.z);
                            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                                return std::format("vec4({:.6f}, {:.6f}, {:.6f}, {:.6f})", arg.x, arg.y, arg.z, arg.w);
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
