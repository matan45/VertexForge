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

    static const std::map<std::string, int> pbrPinToIndex = {
        {"Albedo", 0},
        {"Metallic", 1},
        {"Roughness", 2},
        {"AO", 3},
        {"Normal", 4},
        {"Emission", 5}
    };

    
    class ShaderGraphCompiler {
    public:
        // Compile a material's shader graph to GLSL
        static CompilationResult compile(const material::MaterialData& material);

        // Compile just a shader graph (without material wrapper)
        static CompilationResult compileGraph(const material::ShaderGraph& graph);

        // Reload shader templates from disk (call after editing template files)
        static void reloadTemplates();

    private:
        static std::string generateVertexShader();
        
        static std::string generateFragmentShader(const material::ShaderGraph& graph);

        // Load shader templates from files (caches them for subsequent calls)
        static bool loadTemplates();
        
        static std::string readTextFile(std::string_view path);

        // Topological sort of nodes for proper evaluation order
        static std::vector<uint32_t> topologicalSort(const material::ShaderGraph& graph);
        
        static std::string generateNodeCode(const material::ShaderGraph& graph,
                                           uint32_t nodeId,
                                           std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);

        // Get the variable name for a node's input (follows links or uses default)
        static std::string getInputVarName(const material::ShaderGraph& graph,
                                          uint32_t nodeId,
                                          const std::string& pinName,
                                          const std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);

        // Determine texture index for a TextureSample node based on which PBR output it connects to
        // Returns: 0=Albedo, 1=Metallic, 2=Roughness, 3=AO, 4=Normal, 5=Emission, -1=unknown
        static int determinePBRTextureIndex(const material::ShaderGraph& graph, uint32_t nodeId);

        // Cached shader templates
        static std::string s_vertexTemplate;
        static std::string s_fragmentHeader;
        static std::string s_fragmentFooter;
        static bool s_templatesLoaded;
    };

}
