#include "TerrainService.hpp"
#include "terrain/TerrainMaterialAsset.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/TerrainHeightBlend.hpp"
#include "terrain/TerrainFileAccess.hpp"
#include "resource/ResourceManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/TerrainMaterialAssetEvents.hpp"
#include <asset/AssetRef.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// VK-1653 .vfTerrainMat authoring without the Terrain Material Editor window (MCP P4).
//
// TerrainMaterialEditorWindow, the paint panel and the renderer all reach a terrain material through
// ResourceManager::loadTerrainMaterial, which caches ONE instance per GUID. The edit below therefore
// goes through that same instance -- built on a copy, swapped in, saved, and rolled back if the save
// fails -- instead of rewriting the file behind an open window, which would keep its stale copy and
// drop the new layer on its next Save. The render thread reads that instance too, so this is MAIN
// THREAD ONLY with the render thread idle, like every VK-1653 authoring handler.
//
// No shader compile. ShaderGraphCompiler::compileTerrainMaterial emits buildTerrainCompositeSnippet()
// -- a fixed 8-channel loop with per-tile palette indirection -- plus one provenance comment carrying
// the layer COUNT, so no layer edit changes the generated code. What a layer edit does need is
// TerrainMaterialCompiledNotification: without it GPUDrivenRenderer::registerTerrainLayerTextures
// early-outs on the unchanged material path and never uploads the new or retextured layer.

namespace services
{
    namespace
    {
        // The Terrain Material Editor's Tiling slider range.
        constexpr float MIN_LAYER_TILING_SCALE = 0.01f;
        constexpr float MAX_LAYER_TILING_SCALE = 100.0f;

        // Upper bound on <name>_<n> probing, so a directory full of collisions fails instead of
        // spinning.
        constexpr int MAX_UNIQUE_NAME_ATTEMPTS = 10000;

        // Characters Windows refuses in a file name; '/' and '\\' would also escape the directory.
        constexpr std::string_view RESERVED_FILE_NAME_CHARACTERS = "<>:\"/\\|?*";

        std::string lowerExtension(const std::string& path)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        // A file STEM, not a path.
        bool isPlainFileStem(const std::string& name)
        {
            if (name.empty() || name == "." || name == "..")
                return false;

            // Windows strips a trailing dot or space, so the file would not carry the name asked for.
            if (name.back() == '.' || name.back() == ' ')
                return false;

            return std::none_of(name.begin(), name.end(), [](unsigned char c)
            {
                return c < 0x20 ||
                       RESERVED_FILE_NAME_CHARACTERS.find(static_cast<char>(c)) != std::string_view::npos;
            });
        }

        // Validates every engaged field of `patch` and resolves its material reference WITHOUT
        // touching any material, so a refused edit has nothing to roll back. Returns the reason, or
        // an empty string when the patch is valid.
        std::string validateLayerPatch(const TerrainMaterialLayerPatch& patch, asset::AssetRef& materialRefOut)
        {
            materialRefOut = asset::AssetRef::invalid();

            if (patch.name && patch.name->empty())
                return "a layer name cannot be empty";

            if (patch.tilingScale)
            {
                const float tiling = *patch.tilingScale;
                if (!std::isfinite(tiling) || tiling < MIN_LAYER_TILING_SCALE || tiling > MAX_LAYER_TILING_SCALE)
                {
                    return std::format("tilingScale {} is outside [{}, {}]", tiling,
                                       MIN_LAYER_TILING_SCALE, MAX_LAYER_TILING_SCALE);
                }
            }

            // MAX_HEIGHT_BLEND_CONTRAST is a correctness bound, not a taste limit: it keeps the
            // shader's exp2 argument finite (TerrainHeightBlend.hpp).
            if (patch.heightContrast)
            {
                const float contrast = *patch.heightContrast;
                if (!std::isfinite(contrast) || contrast < 0.0f || contrast > terrain::MAX_HEIGHT_BLEND_CONTRAST)
                {
                    return std::format("heightContrast {} is outside [0, {}]", contrast,
                                       terrain::MAX_HEIGHT_BLEND_CONTRAST);
                }
            }

            if (patch.materialPath)
            {
                const std::string& materialPath = *patch.materialPath;
                if (materialPath.empty())
                    return "a layer material path cannot be empty";

                if (!std::filesystem::path(materialPath).is_absolute())
                    return std::format("{} is not an absolute path", materialPath);

                // A terrain layer sources its PBR from a .vfMat or .vfMatInstance and nothing else
                // (the editor's Browse filter).
                const std::string extension = lowerExtension(materialPath);
                if (extension != ".vfmat" && extension != ".vfmatinstance")
                    return std::format("{} is not a .vfMat or .vfMatInstance", materialPath);

                std::error_code ec;
                if (!std::filesystem::is_regular_file(materialPath, ec))
                    return std::format("material not found: {}", materialPath);

                // What the editor's Browse button stores. fromPath registers an untracked file, so an
                // invalid ref here means the asset database refused it outright.
                materialRefOut = asset::AssetRef::fromPath(materialPath);
                if (!materialRefOut.isValid())
                    return std::format("{} could not be registered with the asset database", materialPath);
            }

            return {};
        }

        void applyLayerPatch(terrain::TerrainMaterialLayer& layer, const TerrainMaterialLayerPatch& patch,
                             const asset::AssetRef& materialRef)
        {
            if (patch.name)
                layer.name = *patch.name;
            if (patch.materialPath)
                layer.materialRef = materialRef;
            if (patch.tilingScale)
                layer.tilingScale = *patch.tilingScale;
            if (patch.heightContrast)
                layer.heightContrast = *patch.heightContrast;
            if (patch.heightBlend)
            {
                layer.blendMode = *patch.heightBlend ? terrain::TerrainLayerBlendMode::HeightBlend
                                                     : terrain::TerrainLayerBlendMode::Linear;
            }
            if (patch.enabled)
                layer.enabled = *patch.enabled;
        }

        uint32_t countEnabledLayers(const terrain::TerrainMaterialData& material)
        {
            const uint32_t count = std::min<uint32_t>(material.activeLayerCount,
                                                      static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS));
            uint32_t enabled = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (material.layers[i].enabled)
                    ++enabled;
            }
            return enabled;
        }

        TerrainMaterialInfo buildMaterialInfo(const std::string& path, const terrain::TerrainMaterialData& material)
        {
            TerrainMaterialInfo info;
            info.path = path;
            info.name = material.name;
            info.maxLayers = static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS);
            info.activeLayerCount = std::min<uint32_t>(material.activeLayerCount, info.maxLayers);
            info.layers.reserve(info.activeLayerCount);

            for (uint32_t i = 0; i < info.activeLayerCount; ++i)
            {
                const terrain::TerrainMaterialLayer& layer = material.layers[i];

                TerrainMaterialLayerInfo layerInfo;
                layerInfo.name = layer.name;
                // Empty for a GUID the database cannot place, which reads the same as unset.
                if (layer.materialRef.isValid())
                    layerInfo.materialPath = layer.materialRef.resolve();
                layerInfo.tilingScale = layer.tilingScale;
                layerInfo.heightBlend = layer.blendMode == terrain::TerrainLayerBlendMode::HeightBlend;
                layerInfo.heightContrast = layer.heightContrast;
                layerInfo.enabled = layer.enabled;
                info.layers.push_back(std::move(layerInfo));
            }

            return info;
        }

        // The cached instance the editor window, the paint panel and the renderer share, or the
        // reason there is none.
        std::string loadSharedTerrainMaterial(const std::string& path,
                                              std::shared_ptr<terrain::TerrainMaterialData>& materialOut)
        {
            materialOut.reset();

            if (path.empty())
                return "no terrain material path given";

            if (!std::filesystem::path(path).is_absolute())
                return std::format("{} is not an absolute path", path);

            if (lowerExtension(path) != ".vfterrainmat")
                return std::format("{} is not a .vfTerrainMat", path);

            // Through TerrainFileAccess, as TerrainMaterialAsset::load reads.
            if (!terrain::terrainFileExists(path))
                return std::format("terrain material not found: {}", path);

            const asset::AssetRef ref = asset::AssetRef::fromPath(path);
            if (!ref.isValid())
                return std::format("{} is not registered with the asset database", path);

            materialOut = resource::ResourceManager::loadTerrainMaterial(ref);
            if (!materialOut)
                return std::format("could not load {} (see the log)", path);

            return {};
        }

        void publishAssetSaved(const std::string& path)
        {
            ::events::resource::AssetSavedNotification notification;
            notification.filePath = path;
            ::events::EventDispatcher::instance().publish(notification);
        }
    }

    CreateTerrainMaterialAssetResult TerrainService::createTerrainMaterialAsset(
        const ::events::terrainMaterial::CreateTerrainMaterialAssetCommand& cmd)
    {
        CreateTerrainMaterialAssetResult result;

        if (cmd.directory.empty())
        {
            result.error = "no directory given for the terrain material";
            return result;
        }

        const std::filesystem::path directory(cmd.directory);
        if (!directory.is_absolute())
        {
            result.error = std::format("{} is not an absolute path", cmd.directory);
            return result;
        }

        if (!isPlainFileStem(cmd.name))
        {
            result.error = std::format("\"{}\" is not a valid file name for a terrain material", cmd.name);
            return result;
        }

        asset::AssetRef baseMaterialRef;
        if (std::string patchError = validateLayerPatch(cmd.baseLayer, baseMaterialRef); !patchError.empty())
        {
            result.error = std::move(patchError);
            return result;
        }

        // The base layer is a new material's only layer, so disabling it would hide the last
        // visible layer -- which the editor refuses (see editTerrainMaterialLayer).
        if (cmd.baseLayer.enabled && !*cmd.baseLayer.enabled)
        {
            result.error = "the only layer of a new terrain material cannot be disabled";
            return result;
        }

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
        {
            result.error = std::format("could not create {}: {}", cmd.directory, ec.message());
            return result;
        }

        // Never overwrites: <name>.vfTerrainMat, then <name>_1, <name>_2 ... -- the naming of the
        // Content Browser's Create Terrain Material modal (ContentBrowserModalsCreate.cpp).
        std::filesystem::path target = directory / (cmd.name + ".vfTerrainMat");
        for (int counter = 1; std::filesystem::exists(target, ec); ++counter)
        {
            if (counter > MAX_UNIQUE_NAME_ATTEMPTS)
            {
                result.error = std::format("no free file name for \"{}\" in {}", cmd.name, cmd.directory);
                return result;
            }
            target = directory / (cmd.name + "_" + std::to_string(counter) + ".vfTerrainMat");
        }
        if (ec)
        {
            // exists() could not tell -- writing blind could replace a file it failed to see.
            result.error = std::format("could not check {}: {}", target.generic_string(), ec.message());
            return result;
        }
        const std::string path = target.generic_string();

        terrain::TerrainMaterialData material = terrain::TerrainMaterialAsset::createDefault(cmd.name);
        applyLayerPatch(material.layers[0], cmd.baseLayer, baseMaterialRef);

        if (!terrain::TerrainMaterialAsset::save(path, material))
        {
            result.error = std::format("writing {} failed (see the log)", path);
            return result;
        }

        // The asset database assigns the GUID on this notification (AssetDatabaseServiceImpl::
        // onAssetSaved), exactly as for a material created from the Content Browser.
        publishAssetSaved(path);

        // SetTerrainMaterialPathCommand stores AssetRef::fromPath(path); an invalid ref there is a
        // terrain with no material and no error. Checked here so the caller hears about it instead.
        if (!asset::AssetRef::fromPath(path).isValid())
        {
            result.error = std::format("{} was written but the asset database could not register it", path);
            return result;
        }

        result.success = true;
        result.path = path;
        return result;
    }

    TerrainMaterialEditResult TerrainService::editTerrainMaterialLayer(
        const ::events::terrainMaterial::EditTerrainMaterialLayerCommand& cmd)
    {
        TerrainMaterialEditResult result;

        std::shared_ptr<terrain::TerrainMaterialData> material;
        if (std::string loadError = loadSharedTerrainMaterial(cmd.materialPath, material); !loadError.empty())
        {
            result.error = std::move(loadError);
            return result;
        }

        asset::AssetRef patchMaterialRef;
        if (std::string patchError = validateLayerPatch(cmd.patch, patchMaterialRef); !patchError.empty())
        {
            result.error = std::move(patchError);
            return result;
        }

        constexpr uint32_t maxLayers = static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS);
        // Clamped like TerrainMaterialAsset::load clamps it, so no index below can leave the array.
        const uint32_t layerCount = std::min<uint32_t>(material->activeLayerCount, maxLayers);

        // Built on a copy and checked whole before the shared instance is touched.
        terrain::TerrainMaterialData candidate = *material;
        uint32_t index = 0;

        if (cmd.index)
        {
            index = *cmd.index;
            if (index >= layerCount)
            {
                result.error = std::format("layer {} does not exist (the material has {} layer{})",
                                           index, layerCount, layerCount == 1 ? "" : "s");
                return result;
            }
        }
        else
        {
            if (layerCount >= maxLayers)
            {
                result.error = std::format("the material already has the maximum of {} layers", maxLayers);
                return result;
            }

            // The editor's "+ Add Layer": a default layer named for its slot.
            index = layerCount;
            candidate.layers[index] = terrain::TerrainMaterialLayer{};
            candidate.layers[index].name = "Layer " + std::to_string(index);
            candidate.activeLayerCount = static_cast<uint8_t>(layerCount + 1);
        }

        applyLayerPatch(candidate.layers[index], cmd.patch, patchMaterialRef);

        // The editor refuses to hide the last visible layer: with no weight left, the composite's
        // 1/max(totalW, 0.001) clamp renders painted ground black. Only an edit that DISABLES is
        // refused, so a material that arrived all-hidden can still be repaired one field at a time.
        if (cmd.patch.enabled && !*cmd.patch.enabled && countEnabledLayers(candidate) == 0)
        {
            result.error = "at least one layer must stay enabled";
            return result;
        }

        // needsRecompile and cachedMaterialSnippet ride through untouched: see the file comment.
        terrain::TerrainMaterialData snapshot = *material;
        *material = std::move(candidate);

        if (!terrain::TerrainMaterialAsset::save(cmd.materialPath, *material))
        {
            // Nothing that holds the shared instance may keep an edit that is not on disk.
            *material = std::move(snapshot);
            result.error = std::format("writing {} failed (see the log); the material is unchanged",
                                       cmd.materialPath);
            return result;
        }

        publishAssetSaved(cmd.materialPath);
        ::events::EventDispatcher::instance().publish(::events::terrain::TerrainMaterialCompiledNotification{});

        result.success = true;
        result.index = index;
        result.material = buildMaterialInfo(cmd.materialPath, *material);
        return result;
    }

    std::optional<TerrainMaterialInfo> TerrainService::getTerrainMaterialInfo(const std::string& materialPath) const
    {
        std::shared_ptr<terrain::TerrainMaterialData> material;
        if (!loadSharedTerrainMaterial(materialPath, material).empty())
            return std::nullopt;

        return buildMaterialInfo(materialPath, *material);
    }
}
