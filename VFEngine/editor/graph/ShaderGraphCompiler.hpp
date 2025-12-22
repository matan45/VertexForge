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

    // Maps PBR output pin names to texture slot indices (matching TextureSlot enum)
    // Note: Slot 2 is reserved for ORM packed textures (handled specially in shader)
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
        // Cached shader templates
        static std::string s_vertexTemplate;
        static std::string s_fragmentHeader;
        static std::string s_fragmentFooter;
        static bool s_templatesLoaded;
    public:
        
        static CompilationResult compile(const material::MaterialData& material);
        
        static CompilationResult compileGraph(const material::ShaderGraph& graph);
        
        static void reloadTemplates();

    private:
        static std::string generateVertexShader();
        
        static std::string generateFragmentShader(const material::ShaderGraph& graph);
        
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

        
        static int determinePBRTextureIndex(const material::ShaderGraph& graph, uint32_t nodeId);
        
    };

}
