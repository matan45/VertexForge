#pragma once

#include <string>
#include <vector>

namespace shaderCompiler
{
	struct ShaderPermutation
	{
		std::string glslPath;
		std::vector<std::string> macroNames;
		std::vector<std::pair<std::string, std::string>> macroValues;
	};

	inline std::vector<ShaderPermutation> getShaderPermutations()
	{
		return {
			// MeshShaderPipeline: gpudriven scene rendering
			// task_gpudriven.glsl and mesh_shader_gpudriven.glsl share the same macro set
			{"gpudriven/task_gpudriven.glsl",     {"WBOIT_ENABLED"},                                   {}},
			{"gpudriven/task_gpudriven.glsl",     {"GI_ENABLED"},                                      {}},
			{"gpudriven/task_gpudriven.glsl",     {"CAUSTICS_ENABLED"},                                 {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/task_gpudriven.glsl",     {"WBOIT_ENABLED", "GI_ENABLED"},                     {}},
			{"gpudriven/task_gpudriven.glsl",     {"WBOIT_ENABLED", "CAUSTICS_ENABLED"},               {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/task_gpudriven.glsl",     {"GI_ENABLED", "CAUSTICS_ENABLED"},                  {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/task_gpudriven.glsl",     {"WBOIT_ENABLED", "GI_ENABLED", "CAUSTICS_ENABLED"}, {{"CAUSTIC_SET", "12"}}},

			{"gpudriven/mesh_shader_gpudriven.glsl", {"WBOIT_ENABLED"},                                   {}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"GI_ENABLED"},                                      {}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"CAUSTICS_ENABLED"},                                 {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"WBOIT_ENABLED", "GI_ENABLED"},                     {}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"WBOIT_ENABLED", "CAUSTICS_ENABLED"},               {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"GI_ENABLED", "CAUSTICS_ENABLED"},                  {{"CAUSTIC_SET", "12"}}},
			{"gpudriven/mesh_shader_gpudriven.glsl", {"WBOIT_ENABLED", "GI_ENABLED", "CAUSTICS_ENABLED"}, {{"CAUSTIC_SET", "12"}}},

			// TerrainMeshShaderPipeline: terrain rendering
			// task_terrain.glsl and mesh_terrain.glsl share the same macro set
			{"gpudriven/task_terrain.glsl",  {"CAUSTICS_ENABLED"},                          {{"CAUSTIC_SET", "12"}}},

			{"gpudriven/mesh_terrain.glsl",  {"CAUSTICS_ENABLED"},                          {{"CAUSTIC_SET", "12"}}},

			// ProbeTracePipeline: GI probe ray tracing
			{"gi/probe_trace.glsl", {"USE_RAY_QUERY"},                          {}},
			{"gi/probe_trace.glsl", {"USE_LIGHT_DATA"},                         {}},
			{"gi/probe_trace.glsl", {"USE_RAY_QUERY", "USE_LIGHT_DATA"},        {}},
		};
	}
}
