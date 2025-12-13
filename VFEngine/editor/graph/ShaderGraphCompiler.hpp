#pragma once
#include "material/MaterialTypes.hpp"
#include <string>
#include <optional>

namespace editor::graph {

    // Result of shader compilation
    struct CompilationResult {
        bool success = false;
        std::string vertexShader;
        std::string fragmentShader;
        std::string errorMessage;
    };

    // Compiles a shader graph to GLSL code
    class ShaderGraphCompiler {
    public:
        // Compile a material's shader graph to GLSL
        static CompilationResult compile(const material::MaterialData& material);

        // Compile just a shader graph (without material wrapper)
        static CompilationResult compileGraph(const material::ShaderGraph& graph);

    private:
        // Generate the vertex shader (mostly static, same for all materials)
        static std::string generateVertexShader();

        // Generate the fragment shader from the graph
        static std::string generateFragmentShader(const material::ShaderGraph& graph);

        // Topological sort of nodes for proper evaluation order
        static std::vector<uint32_t> topologicalSort(const material::ShaderGraph& graph);

        // Generate code for a single node
        static std::string generateNodeCode(const material::ShaderGraph& graph,
                                           uint32_t nodeId,
                                           std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);

        // Get the variable name for a node's input (follows links or uses default)
        static std::string getInputVarName(const material::ShaderGraph& graph,
                                          uint32_t nodeId,
                                          const std::string& pinName,
                                          const std::map<uint32_t, std::map<std::string, std::string>>& nodeOutputVars);
    };

}
