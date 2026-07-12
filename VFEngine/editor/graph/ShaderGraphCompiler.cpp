#include "print/Log.hpp"
#include "ShaderGraphCompiler.hpp"
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

    namespace
    {
        // Emits the per-tile terrain composite loop for one shader permutation. Both permutations
        // share a single body here so the per-layer logic (weight sampling, ORM unpack, tiling /
        // gradient math) can only be edited in one place; `detail` inserts the extra work the
        // TERRAIN_DETAIL_MAPS permutation samples — the normal-map fetch, the emission-texture
        // path, and the mat_normalTS output. Output is byte-identical to the two hand-maintained
        // branches this replaced.
        std::string buildTerrainCompositeLoop(bool detail)
        {
            std::string s;
            s += "vec3 ls_Albedo = vec3(0.0);\n";
            if (detail) s += "vec3 ls_Normal = vec3(0.0);\n";
            s += "float ls_Roughness = 0.0;\n";
            s += "float ls_Metallic = 0.0;\n";
            s += "float ls_AO = 0.0;\n";
            s += "float ls_Emission = 0.0;\n";
            if (detail)
            {
                s += "float ls_EmissionScalar = 0.0;\n";
                s += "vec3 ls_EmissionColor = vec3(0.0);\n";
            }
            s += "float ls_TotalW = 0.0;\n";
            s += "uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);\n";
            s += "uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);\n";
            s += "for (int ch = 0; ch < 8; ch++) {\n";
            s += "    uint packedWord = (ch < 4) ? packedLI : packedLI2;\n";
            s += "    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;\n";
            s += "    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, "
                 "uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);\n";
            s += "    if (w < 0.001) continue;\n";
            // Layer samples run inside per-fragment-divergent control flow (this `continue`, and
            // mesh_terrain's RVT resolved/fallback branch), where implicit-LOD texture() derivatives
            // are undefined and cause mip shimmer at RVT seams — so sample with EXPLICIT gradients
            // (textureGrad). The includer must define triplanarWorldUVdx/dy (screen-space gradients
            // of triplanarWorldUV) in uniform control flow before including this snippet.
            if (detail)
                s += "    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.\n";
            s += "    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;\n";
            s += "    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;\n";
            s += "    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;\n";
            s += "    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;\n";
            s += "    vec3 layerAlbedo = (albedoIdx > 0u) ? "
                 "textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);\n";
            // The non-detail permutation lights terrain with the geometric normal only (mesh_terrain
            // uses N = normalize(fragNormal); the RVT bake writes no normal plane), so it omits the
            // per-layer normal fetch — a composited tangent-space normal would be dead work there.
            if (detail)
            {
                s += "    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;\n";
                s += "    vec3 layerNormal = (normalIdx > 0u) ? "
                     "textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);\n";
            }
            s += "    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;\n";
            s += "    float layerAO, layerRoughness, layerMetallic;\n";
            s += "    if (ormIdx > 0u) {\n";
            s += "        vec3 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy).rgb;\n";
            s += "        layerAO = ormSample.r;\n";
            s += "        layerRoughness = ormSample.g;\n";
            s += "        layerMetallic = ormSample.b;\n";
            s += "    } else {\n";
            s += "        layerAO = terrainLayers[paletteIdx].ao;\n";
            s += "        layerRoughness = terrainLayers[paletteIdx].roughness;\n";
            s += "        layerMetallic = terrainLayers[paletteIdx].metallic;\n";
            s += "    }\n";
            s += "    float layerEmission = terrainLayers[paletteIdx].emissionStrength;\n";
            if (detail)
            {
                s += "    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;\n";
                s += "    if (emissionIdx > 0u) {\n";
                s += "        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;\n";
                s += "        ls_EmissionColor += emissionSample * layerEmission * w;\n";
                s += "    } else {\n";
                s += "        ls_EmissionScalar += layerEmission * w;\n";
                s += "    }\n";
            }
            s += "    ls_Albedo += layerAlbedo * w;\n";
            if (detail) s += "    ls_Normal += layerNormal * w;\n";
            s += "    ls_Roughness += layerRoughness * w;\n";
            s += "    ls_Metallic += layerMetallic * w;\n";
            s += "    ls_AO += layerAO * w;\n";
            s += "    ls_Emission += layerEmission * w;\n";
            s += "    ls_TotalW += w;\n";
            s += "}\n";
            s += "float ls_InvW = 1.0 / max(ls_TotalW, 0.001);\n";
            s += "ls_Albedo *= ls_InvW;\n";
            if (detail) s += "ls_Normal *= ls_InvW;\n";
            s += "ls_Roughness *= ls_InvW;\n";
            s += "ls_Metallic *= ls_InvW;\n";
            s += "ls_AO *= ls_InvW;\n";
            s += "ls_Emission *= ls_InvW;\n";
            if (detail)
            {
                s += "ls_EmissionScalar *= ls_InvW;\n";
                s += "ls_EmissionColor *= ls_InvW;\n";
            }
            s += "// Terrain material properties\n";
            s += "vec3 mat_albedo = ls_Albedo;\n";
            if (detail)
            {
                s += "#define MAT_NORMALTS_DEFINED\n";
                s += "float ls_NormalLengthSq = dot(ls_Normal, ls_Normal);\n";
                s += "vec3 mat_normalTS = (ls_NormalLengthSq > 1e-8) ? ls_Normal * inversesqrt(ls_NormalLengthSq) : vec3(0.0, 0.0, 1.0);\n";
            }
            s += "float mat_metallic = ls_Metallic;\n";
            s += "float mat_roughness = ls_Roughness;\n";
            s += "float mat_ao = ls_AO;\n";
            s += "#define MAT_EMISSION_DEFINED\n";
            if (detail)
                s += "vec3 mat_emission = mat_albedo * ls_EmissionScalar + ls_EmissionColor;\n";
            else
                s += "vec3 mat_emission = mat_albedo * ls_Emission;\n";
            return s;
        }
    }

    TerrainCompilationResult ShaderGraphCompiler::compileTerrainMaterial(const terrain::TerrainMaterialData& material) {
        TerrainCompilationResult result;

        // Static 8-channel loop with per-tile palette indirection
        std::string code;
        code += "// Generated terrain material code\n";
        code += "// Per-tile palette: 8 channels with runtime indirection into palette of " + std::to_string(material.activeLayerCount) + " layer(s)\n";
        code += "#ifdef TERRAIN_DETAIL_MAPS\n";
        code += buildTerrainCompositeLoop(/*detail=*/true);
        code += "#else\n";
        code += buildTerrainCompositeLoop(/*detail=*/false);
        code += "#endif\n";

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
            auto f = [](float v) { return std::to_string(v); };
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
