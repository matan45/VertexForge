#pragma once

#include "MaterialIR.hpp"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace material
{
    struct MaterialShaderMap
    {
        std::string irHash;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
        std::string shaderMapKey;
        std::string compiledShaderPath;
        bool hasGeneratedShader = false;
    };

    struct MaterialResourceBinding
    {
        std::string name;
        TextureSlot slot = TextureSlot::Albedo;
        MaterialIRAssetRef defaultTexture;
    };

    struct MaterialRuntimeData
    {
        MaterialIR ir;
        std::string irHash;
        MaterialShaderMap shaderMap;
        MaterialParameterSet parameterLayout;
        std::vector<std::byte> defaultParameterBlock;
        std::vector<MaterialResourceBinding> resourceBindings;
        BlendMode blendMode = BlendMode::Opaque;
        MaterialDomain domain = MaterialDomain::Surface;
        ShadingModel shadingModel = ShadingModel::DefaultLit;
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        bool depthWrite = true;
        bool blendEnabled = false;
        bool gpuDrivenSupported = true;
        std::string gpuDrivenFallbackReason;
    };

    class MaterialRuntimeDataBuilder
    {
    public:
        static MaterialRuntimeData fromMaterialData(
            const MaterialData& material,
            const std::map<std::string, ParameterValue>* parameterOverrides = nullptr);

        static std::string shaderSourceHash(const std::string& source);
    };
}
