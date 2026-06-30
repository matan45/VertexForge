#include "MaterialRuntimeData.hpp"
#include "../archive/VFPakFormat.hpp"
#include "../asset/AssetRef.hpp"

namespace material
{
    namespace
    {
        MaterialIRAssetRef textureAssetRef(const std::string& path)
        {
            MaterialIRAssetRef ref;
            ref.path = path;
            if (!path.empty())
            {
                asset::AssetRef assetRef = asset::AssetRef::fromPath(path);
                if (assetRef.isValid())
                {
                    ref.guid = assetRef.toHexString();
                    const std::string& resolved = assetRef.resolve();
                    if (!resolved.empty())
                    {
                        ref.path = resolved;
                    }
                }
            }
            return ref;
        }

        TextureSlot slotFromIndex(int slot)
        {
            if (slot < 0 || slot >= static_cast<int>(TextureSlot::Count))
            {
                return TextureSlot::Albedo;
            }
            return static_cast<TextureSlot>(slot);
        }
    }

    MaterialRuntimeData MaterialRuntimeDataBuilder::fromMaterialData(
        const MaterialData& material,
        const std::map<std::string, ParameterValue>* parameterOverrides)
    {
        MaterialRuntimeData data;
        data.ir = MaterialIRBuilder::fromMaterialData(material);
        data.irHash = MaterialIRBuilder::computeHashString(data.ir);
        data.parameterLayout = data.ir.parameters;
        data.defaultParameterBlock.resize(data.parameterLayout.uniformBlockSize);
        if (!data.defaultParameterBlock.empty())
        {
            const std::map<std::string, ParameterValue> emptyOverrides;
            const auto& overrides = parameterOverrides ? *parameterOverrides : emptyOverrides;
            writeStd140(data.parameterLayout,
                        overrides,
                        data.defaultParameterBlock);
        }

        data.blendMode = material.blendMode;
        data.domain = material.domain;
        data.shadingModel = material.shadingModel;
        data.opacity = material.opacity;
        data.alphaCutoff = material.alphaCutoff;
        data.depthWrite = !isTransparentBlendMode(material.blendMode);
        data.blendEnabled = isTransparentBlendMode(material.blendMode);
        data.gpuDrivenSupported = data.ir.gpuDrivenSupported;
        data.gpuDrivenFallbackReason = data.ir.gpuDrivenFallbackReason;

        for (const auto& texture : data.parameterLayout.textures)
        {
            MaterialResourceBinding binding;
            binding.name = texture.name;
            binding.slot = slotFromIndex(texture.slot);
            binding.defaultTexture = textureAssetRef(texture.defaultTexturePath);
            data.resourceBindings.push_back(std::move(binding));
        }

        data.shaderMap.irHash = data.irHash;
        data.shaderMap.vertexShaderHash = shaderSourceHash(material.cachedVertexShader);
        data.shaderMap.fragmentShaderHash = shaderSourceHash(material.cachedFragmentShader);
        data.shaderMap.shaderMapKey = data.irHash + "_" +
            data.shaderMap.vertexShaderHash + "_" + data.shaderMap.fragmentShaderHash;
        data.shaderMap.compiledShaderPath = "Assets/materials/compiled/" +
            data.shaderMap.vertexShaderHash + "_" + data.shaderMap.fragmentShaderHash + ".vfshader";
        data.shaderMap.hasGeneratedShader =
            !material.cachedVertexShader.empty() && !material.cachedFragmentShader.empty();
        return data;
    }

    std::string MaterialRuntimeDataBuilder::shaderSourceHash(const std::string& source)
    {
        return MaterialIRBuilder::formatHash(archive::hashPath(source));
    }
}
