#include "print/Log.hpp"
#include "ShaderGraphCompiler.hpp"
#include "TerrainCompositeSnippet.hpp"
#include "nodes/ShaderNode.hpp"
#include "material/MaterialParameterSet.hpp"
#include <queue>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <format>

namespace editor::graph {

    std::string ShaderGraphCompiler::s_vertexTemplate;
    std::string ShaderGraphCompiler::s_fragmentHeader;
    std::string ShaderGraphCompiler::s_fragmentFooter;
    bool ShaderGraphCompiler::s_templatesLoaded = false;

    TerrainCompilationResult ShaderGraphCompiler::compileTerrainMaterial(const terrain::TerrainMaterialData& material) {
        TerrainCompilationResult result;

        // Static 8-channel loop with per-tile palette indirection. The body is emitted by
        // TerrainCompositeSnippet.hpp (header-only so the Tests project can pin it); only the
        // provenance comments, which carry this material's layer count, are material-dependent.
        std::string code;
        code += "// Generated terrain material code\n";
        code += "// Per-tile palette: 8 channels with runtime indirection into palette of " + std::to_string(material.activeLayerCount) + " layer(s)\n";
        code += buildTerrainCompositeSnippet();

        result.materialSnippet = code;
        result.success = true;
        return result;
    }

    CompilationResult ShaderGraphCompiler::compileGraph(const material::ShaderGraph& graph,
                                                        const ShaderCompileOptions& opts) {
        CompilationResult result;

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

        const material::ShaderNode* outputNode = graph.findOutputNode();
        if (!outputNode) {
            result.success = false;
            result.errorMessage = "No PBR Output node found in shader graph";
            return result;
        }

        std::vector<uint32_t> sortedNodes = topologicalSort(graph);
        if (sortedNodes.empty() && !graph.nodes.empty()) {
            result.success = false;
            result.errorMessage = "Shader graph contains a cycle or exceeds maximum depth limit";
            return result;
        }
        
        std::string typeErrorMessage;
        if (!validateLinkTypes(graph, typeErrorMessage)) {
            result.success = false;
            result.errorMessage = typeErrorMessage;
            return result;
        }

        result.vertexShader = generateVertexShader();
        result.fragmentShader = generateFragmentShader(graph, opts);
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

        s_vertexTemplate = readTextFile(VERTEX_TEMPLATE_PATH);
        if (s_vertexTemplate.empty()) {
            vfLogError("Failed to load vertex shader template from: {}", VERTEX_TEMPLATE_PATH);
            return false;
        }

        s_fragmentHeader = readTextFile(FRAGMENT_HEADER_PATH);
        if (s_fragmentHeader.empty()) {
            vfLogError("Failed to load fragment shader header from: {}", FRAGMENT_HEADER_PATH);
            return false;
        }

        s_fragmentFooter = readTextFile(FRAGMENT_FOOTER_PATH);
        if (s_fragmentFooter.empty()) {
            vfLogError("Failed to load fragment shader footer from: {}", FRAGMENT_FOOTER_PATH);
            return false;
        }

        s_templatesLoaded = true;
        vfLogInfo("Loaded shader templates from files");
        return true;
    }

    std::string ShaderGraphCompiler::generateVertexShader() {
        return s_vertexTemplate;
    }
    
    static bool isDisplacementConnected(const material::ShaderGraph& graph) {
        const material::ShaderNode* outputNode = graph.findOutputNode();
        if (!outputNode) return false;

        for (const auto& link : graph.links) {
            if (link.targetNodeId == outputNode->id && link.targetPin == "Displacement") {
                return true;
            }
        }
        return false;
    }

    std::string ShaderGraphCompiler::generateFragmentShader(const material::ShaderGraph& graph,
                                                            const ShaderCompileOptions& opts) {
        std::string code;

        bool useParallax = isDisplacementConnected(graph);

        // Build the preprocessor block spliced right after the #version line. USE_PARALLAX
        // toggles POM; TOON_ENABLED + the baked TOON_* profile constants drive the toon
        // preview branch in material_fragment_footer.glsl (self-contained — no UBO/descriptor
        // changes; a profile value edit recompiles, which callers debounce).
        std::string defines;
        if (useParallax) {
            defines += "#define USE_PARALLAX 1\n";
        }
        if (opts.toonEnabled) {
            const material::ToonProfile& p = opts.toonProfile;
            // std::format is locale-independent by default (no 'L' option), unlike
            // std::to_string which honors LC_NUMERIC and would emit '0,35' under a
            // comma-decimal locale — invalid GLSL. Fixed 6-digit output keeps a
            // decimal point so the values stay float literals.
            auto f = [](float v) { return std::format("{:.6f}", v); };
            auto v3 = [&](const glm::vec3& c) {
                return "vec3(" + f(c.x) + ", " + f(c.y) + ", " + f(c.z) + ")";
            };
            defines += "#define TOON_ENABLED 1\n";
            defines += "#define TOON_SHADE_COLOR "      + v3(p.shadeColor)      + "\n";
            defines += "#define TOON_MID_COLOR "        + v3(p.midColor)        + "\n";
            defines += "#define TOON_SHADOW_THRESHOLD " + f(p.shadowThreshold)  + "\n";
            defines += "#define TOON_MID_THRESHOLD "    + f(p.midThreshold)     + "\n";
            defines += "#define TOON_BAND_SMOOTHNESS "  + f(p.bandSmoothness)   + "\n";
            defines += "#define TOON_GI_SCALE "         + f(p.giScale)          + "\n";
            defines += "#define TOON_SPEC_COLOR "       + v3(p.specColor)       + "\n";
            defines += "#define TOON_SPEC_THRESHOLD "   + f(p.specThreshold)    + "\n";
            defines += "#define TOON_SPEC_SMOOTHNESS "  + f(p.specSmoothness)   + "\n";
            defines += "#define TOON_SPEC_INTENSITY "   + f(p.specIntensity)    + "\n";
            defines += "#define TOON_SPEC_SHININESS "   + f(p.specShininess)    + "\n";
            defines += "#define TOON_RIM_COLOR "        + v3(p.rimColor)        + "\n";
            defines += "#define TOON_RIM_POWER "        + f(p.rimPower)         + "\n";
            defines += "#define TOON_RIM_INTENSITY "    + f(p.rimIntensity)     + "\n";
        }

        if (!defines.empty()) {
            // Find the end of the #version line (after #type FRAGMENT line)
            size_t versionPos = s_fragmentHeader.find("#version");
            if (versionPos != std::string::npos) {
                size_t versionEnd = s_fragmentHeader.find('\n', versionPos);
                if (versionEnd != std::string::npos) {
                    code += s_fragmentHeader.substr(0, versionEnd + 1);
                    code += defines;
                    code += s_fragmentHeader.substr(versionEnd + 1);
                } else {
                    code += s_fragmentHeader;
                    code += defines;
                }
            } else {
                code += defines;
                code += s_fragmentHeader;
            }
        } else {
            code += s_fragmentHeader;
        }

        // Exposed parameters become a std140 uniform block (set 2) instead of baked
        // literals, so value edits and overrides don't require a shader recompile.
        material::MaterialParameterSet paramSet = material::collectParameters(graph);
        if (paramSet.hasValueParameters()) {
            std::string paramBlock = material::emitGlslUniformBlock(
                paramSet, material::PARAMETER_DESCRIPTOR_SET, material::PARAMETER_DESCRIPTOR_BINDING);
            size_t mainPos = code.rfind("void main()");
            if (mainPos != std::string::npos) {
                code.insert(mainPos, paramBlock + "\n");
            } else {
                vfLogWarning("Fragment header has no 'void main()' anchor; parameter block not injected");
            }
        }

        std::vector<uint32_t> sortedNodes = topologicalSort(graph);
        std::map<uint32_t, std::map<std::string, std::string>> nodeOutputVars;

        code += "    // Generated shader graph code\n";

        for (uint32_t nodeId : sortedNodes) {
            code += generateNodeCode(graph, nodeId, nodeOutputVars, paramSet);
        }

        // Append cached fragment footer (PBR lighting calculation and main() closing)
        code += s_fragmentFooter;

        return code;
    }

    std::vector<uint32_t> ShaderGraphCompiler::topologicalSort(const material::ShaderGraph& graph,
                                                                const material::ShaderNode* outputNode) {
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

        uint32_t problemNodeId = 0;
        std::string problemReason;

        // DFS-based topological sort with depth limit
        std::function<void(uint32_t, size_t)> visit = [&](uint32_t nodeId, size_t depth) {
            if (cycleDetected) return;
            if (visited.count(nodeId)) return;

            if (dependencies.find(nodeId) == dependencies.end()) {
                vfLogWarning("Link references non-existent node {}, skipping", nodeId);
                return;
            }

            if (depth > MAX_RECURSION_DEPTH) {
                cycleDetected = true;
                problemNodeId = nodeId;
                problemReason = "depth limit exceeded";
                return;
            }

            if (inStack.count(nodeId)) {
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

        const material::ShaderNode* startNode = outputNode ? outputNode : graph.findOutputNode();
        if (startNode) {
            visit(startNode->id, 0);
        }

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

            for (const auto& link : graph.links) {
                if (link.sourceNodeId == currentId) {
                    const material::ShaderNode* targetNode = graph.findNode(link.targetNodeId);
                    if (targetNode && targetNode->type == material::NodeType::PBROutput) {
                        auto it = pbrPinToIndex.find(link.targetPin);
                        if (it != pbrPinToIndex.end()) {
                            return it->second;
                        }
                    }
                    toVisit.push(link.targetNodeId);
                }
            }
        }

        return -1; // Not connected to PBR output
    }

    std::string ShaderGraphCompiler::generateNodeCode(const material::ShaderGraph& graph,
                                                      uint32_t nodeId,
                                                      std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars,
                                                      const material::MaterialParameterSet& paramSet) {
        const material::ShaderNode* nodeData = graph.findNode(nodeId);
        if (!nodeData) return "";

        // Create a mutable copy of node data for TextureSample nodes
        // so we can set the correct texture index based on PBR connection
        material::ShaderNode modifiedNodeData = *nodeData;

        if (nodeData->type == material::NodeType::TextureSample) {
            int pbrIndex = determinePBRTextureIndex(graph, nodeId);
            if (pbrIndex >= 0) {
                modifiedNodeData.properties["textureIndex"] = static_cast<float>(pbrIndex);
            }
        }

        // Exposed value parameters read from the uniform block instead of baking literals
        auto glslNameIt = paramSet.nodeToGlslName.find(nodeId);
        if (glslNameIt != paramSet.nodeToGlslName.end()) {
            modifiedNodeData.properties["glslUniformName"] = glslNameIt->second;
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

        for (const auto& pin : node->getOutputPins()) {
            nodeOutputVars[nodeId][pin.name] = node->getOutputVarName(prefix, pin.name);
        }

        return code;
    }

    std::string ShaderGraphCompiler::getInputVarName(const material::ShaderGraph& graph,
                                                     uint32_t nodeId,
                                                     const std::string& pinName,
                                                     const std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars) {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == nodeId && link.targetPin == pinName) {
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
        
        return "0.0";
    }

    bool ShaderGraphCompiler::validateLinkTypes(const material::ShaderGraph& graph, std::string& errorMessage) {
        for (const auto& link : graph.links) {
            const material::ShaderNode* sourceNode = graph.findNode(link.sourceNodeId);
            const material::ShaderNode* targetNode = graph.findNode(link.targetNodeId);

            if (!sourceNode || !targetNode) {
                errorMessage = "Invalid link: source or target node not found";
                return false;
            }

            material::PinType sourceType = material::PinType::Float;
            bool foundSource = false;
            for (const auto& pin : sourceNode->outputs) {
                if (pin.name == link.sourcePin) {
                    sourceType = pin.type;
                    foundSource = true;
                    break;
                }
            }

            material::PinType targetType = material::PinType::Float;
            bool foundTarget = false;
            for (const auto& pin : targetNode->inputs) {
                if (pin.name == link.targetPin) {
                    targetType = pin.type;
                    foundTarget = true;
                    break;
                }
            }

            if (!foundSource || !foundTarget) {
                errorMessage = "Invalid link: pin not found on node";
                return false;
            }

            if (sourceType != targetType) {
                errorMessage = "Type mismatch in link from '" + sourceNode->name + "' (" + link.sourcePin +
                              ") to '" + targetNode->name + "' (" + link.targetPin + ").\n" +
                              "Expected: " + material::pinTypeToString(targetType) +
                              ", Got: " + material::pinTypeToString(sourceType) + ".\n" +
                              "Use a conversion node to fix this.";
                vfLogError("Shader graph type mismatch: {} ({}) -> {} ({}) : {} vs {}",
                          sourceNode->name, link.sourcePin,
                          targetNode->name, link.targetPin,
                          material::pinTypeToString(sourceType), material::pinTypeToString(targetType));
                return false;
            }
        }

        return true;
    }

}
