#pragma once
#include "material/MaterialTypes.hpp"
#include <string>
#include <string_view>


namespace editor::graph {
    
    struct CompilationResult {
        bool success = false;
        std::string vertexShader;
        std::string fragmentShader;
        std::string errorMessage;
    };

    struct TerrainCompilationResult {
        bool success = false;
        std::string materialSnippet; // GLSL body (no header/footer templates)
        std::string errorMessage;
    };
    
    static const std::map<std::string, int> pbrPinToIndex = {
        {"Albedo", 0},      // TextureSlot::Albedo
        {"Normal", 1},      // TextureSlot::Normal
        {"ORM", 2},         // TextureSlot::ORM (packed AO/Roughness/Metallic)
        {"Metallic", 3},    // TextureSlot::Metallic (legacy individual)
        {"Roughness", 4},   // TextureSlot::Roughness (legacy individual)
        {"AO", 5},          // TextureSlot::AO (legacy individual)
        {"Emission", 6},    // TextureSlot::Emission
        {"Displacement", 7} // TextureSlot::Height (displacement/height map)
    };

    
    class ShaderGraphCompiler {
    private:
        // Limits to prevent stack overflow from malicious/malformed material files
        static constexpr size_t MAX_RECURSION_DEPTH = 100;
        static constexpr size_t MAX_NODES = 1000;

        // Shader template paths (relative to executable)
        static constexpr std::string_view VERTEX_TEMPLATE_PATH = "../../resources/shaders/material/material_vertex.glsl";
        static constexpr std::string_view FRAGMENT_HEADER_PATH = "../../resources/shaders/material/material_fragment_header.glsl";
        static constexpr std::string_view FRAGMENT_FOOTER_PATH = "../../resources/shaders/material/material_fragment_footer.glsl";

        // Allowed base directory for shader files (relative to executable)
        static constexpr std::string_view ALLOWED_SHADER_DIR = "../../resources/shaders";

        // Cached shader templates
        static std::string s_vertexTemplate;
        static std::string s_fragmentHeader;
        static std::string s_fragmentFooter;
        static bool s_templatesLoaded;
    public:
        
        static CompilationResult compile(const material::MaterialData& material);

        static CompilationResult compileGraph(const material::ShaderGraph& graph);

        static TerrainCompilationResult compileTerrainGraph(const material::ShaderGraph& graph);

        static void reloadTemplates();

    private:
        static std::string generateVertexShader();
        
        static std::string generateFragmentShader(const material::ShaderGraph& graph);
        
        static bool loadTemplates();
        
        static std::string readTextFile(std::string_view path);

        // Topological sort of nodes for proper evaluation order
        // If outputNode is null, uses graph.findOutputNode() (for regular materials)
        static std::vector<uint32_t> topologicalSort(const material::ShaderGraph& graph,
                                                      const material::ShaderNode* outputNode = nullptr);
        
        static std::string generateNodeCode(const material::ShaderGraph& graph,
                                           uint32_t nodeId,
                                           std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);

        // Get the variable name for a node's input (follows links or uses default)
        static std::string getInputVarName(const material::ShaderGraph& graph,
                                          uint32_t nodeId,
                                          const std::string& pinName,
                                          const std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);


        static int determinePBRTextureIndex(const material::ShaderGraph& graph, uint32_t nodeId);

        static bool validateLinkTypes(const material::ShaderGraph& graph, std::string& errorMessage);

    };

}
