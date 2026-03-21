#include "RenderPassHandler.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "billboard/BillboardTypes.hpp"
#include "text/TextPipeline.hpp"
#include "text/TextTypes.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UIRenderTypes.hpp"
#include "ui/UITextPipeline.hpp"
#include "ui/UITextRenderTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "material/MaterialTypes.hpp"
#include <queue>
#include <unordered_set>
#include <optional>

namespace
{
    std::optional<uint32_t> findTimeNodeId(const material::ShaderGraph& graph)
    {
        for (const auto& node : graph.nodes)
        {
            if (node.type == material::NodeType::Time)
            {
                return node.id;
            }
        }
        return std::nullopt;
    }

    bool timeNodeReachesPBROutput(const material::ShaderGraph& graph, uint32_t startNodeId)
    {
        std::unordered_set<uint32_t> visited;
        std::queue<uint32_t> toVisit;
        toVisit.push(startNodeId);

        while (!toVisit.empty())
        {
            uint32_t currentId = toVisit.front();
            toVisit.pop();

            if (visited.contains(currentId))
            {
                continue;
            }
            visited.insert(currentId);

            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId != currentId)
                {
                    continue;
                }

                for (const auto& node : graph.nodes)
                {
                    if (node.id != link.targetNodeId)
                    {
                        continue;
                    }

                    if (node.type == material::NodeType::PBROutput)
                    {
                        return true;
                    }

                    if (node.type == material::NodeType::TextureSample && link.targetPin == "UV")
                    {
                        return true;
                    }

                    toVisit.push(link.targetNodeId);
                    break;
                }
            }
        }

        return false;
    }
}

namespace render
{
    std::unordered_set<std::string> RenderPassHandler::collectCustomShaderMaterials(
        const std::vector<mesh::MeshRenderData>& meshes) const
    {
        std::unordered_set<std::string> uniqueMaterials;
        for (const auto& mesh : meshes)
        {
            if (!mesh.defaultMaterialPath.empty())
            {
                uniqueMaterials.insert(mesh.defaultMaterialPath);
            }
            for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
            {
                if (!matInfo.materialPath.empty())
                {
                    uniqueMaterials.insert(matInfo.materialPath);
                }
            }
        }

        std::unordered_set<std::string> customShaderMaterials;
        for (const auto& matPath : uniqueMaterials)
        {
            if (materialRequiresCustomShader(matPath))
            {
                customShaderMaterials.insert(matPath);
            }
        }
        return customShaderMaterials;
    }

    void RenderPassHandler::updateDebugBoundingBoxState()
    {
        if (!debugRendererInitialized || !debugRenderer)
        {
            return;
        }

        bool hasBoundingBoxes = false;
        for (const auto& mesh : currentMeshDrawList)
        {
            if (mesh.showBoundingBox) { hasBoundingBoxes = true; break; }
        }
        if (!hasBoundingBoxes)
        {
            for (const auto& mesh : customShaderMeshDrawList)
            {
                if (mesh.showBoundingBox) { hasBoundingBoxes = true; break; }
            }
        }
        debugRenderer->setHasBoundingBoxes(hasBoundingBoxes);
    }

    void RenderPassHandler::rebuildCombinedMeshDrawList()
    {
        combinedMeshDrawList.clear();
        combinedMeshDrawList.reserve(currentMeshDrawList.size() + customShaderMeshDrawList.size());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    currentMeshDrawList.begin(), currentMeshDrawList.end());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    customShaderMeshDrawList.begin(), customShaderMeshDrawList.end());
    }

    void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
    {
        currentMeshDrawList.clear();
        customShaderMeshDrawList.clear();

        auto customShaderMaterials = collectCustomShaderMaterials(meshes);

        for (auto& mesh : meshes)
        {
            bool needsCustomShader = false;

            if (!mesh.defaultMaterialPath.empty()
                && customShaderMaterials.contains(mesh.defaultMaterialPath))
            {
                needsCustomShader = true;
            }

            if (!needsCustomShader)
            {
                for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
                {
                    if (!matInfo.materialPath.empty()
                        && customShaderMaterials.contains(matInfo.materialPath))
                    {
                        needsCustomShader = true;
                        break;
                    }
                }
            }

            if (needsCustomShader)
            {
                customShaderMeshDrawList.push_back(std::move(mesh));
            }
            else
            {
                currentMeshDrawList.push_back(std::move(mesh));
            }
        }

        updateDebugBoundingBoxState();
        rebuildCombinedMeshDrawList();
    }

    void RenderPassHandler::registerExternalTexture(const std::string& key, vk::ImageView imageView, vk::Sampler sampler)
    {
        if (uiPipelineInitialized && uiPipeline)
            uiPipeline->registerExternalTexture(key, imageView, sampler);

        if (billboardPipelineInitialized && billboardPipeline)
            billboardPipeline->registerExternalTexture(key, imageView, sampler);
    }

    void RenderPassHandler::initBillboardPipeline()
    {
        if (billboardPipelineInitialized)
        {
            return;
        }

        if (sharedCameraUBO)
        {
            billboardPipeline->setExternalCameraBuffer(sharedCameraUBO->getBuffer());
        }
        billboardPipeline->init();
        billboardPipelineInitialized = true;
    }

    void RenderPassHandler::setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards)
    {
        currentBillboardDrawList = std::move(billboards);
        if (billboardPipelineInitialized && billboardPipeline)
        {
            billboardPipeline->setBillboardList(currentBillboardDrawList);
        }
    }

    void RenderPassHandler::initTextPipeline()
    {
        if (textPipelineInitialized)
        {
            return;
        }

        if (sharedCameraUBO)
        {
            textPipeline->setExternalCameraBuffer(sharedCameraUBO->getBuffer());
        }
        textPipeline->init();
        textPipelineInitialized = true;
    }

    void RenderPassHandler::setTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList = std::move(textEntities);
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::appendTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList.insert(currentTextDrawList.end(),
                                   std::make_move_iterator(textEntities.begin()),
                                   std::make_move_iterator(textEntities.end()));
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::initUIRenderPipeline()
    {
        if (uiPipelineInitialized)
        {
            return;
        }

        uiPipeline->init();
        uiPipelineInitialized = true;
    }

    void RenderPassHandler::setUIImageDrawList(std::vector<ui::UIImageRenderData>&& images)
    {
        currentUIImageDrawList = std::move(images);
        if (uiPipelineInitialized && uiPipeline)
        {
            uiPipeline->setUIImageDrawList(currentUIImageDrawList);
        }
    }

    void RenderPassHandler::initUITextPipeline()
    {
        if (uiTextPipelineInitialized)
        {
            return;
        }

        initTextPipeline();

        uiTextPipeline->init();
        uiTextPipelineInitialized = true;
    }

    void RenderPassHandler::setUITextDrawList(std::vector<ui::UITextRenderData>&& labels)
    {
        currentUITextDrawList = std::move(labels);
        if (uiTextPipelineInitialized && uiTextPipeline)
        {
            uiTextPipeline->setUITextDrawList(currentUITextDrawList);
        }
    }

    void RenderPassHandler::initDebugRenderer()
    {
        if (debugRendererInitialized)
        {
            return;
        }

        if (!meshPipelineInitialized)
        {
            return;
        }

        debugRenderer->init(meshPipeline->getRenderPass());
        debugRendererInitialized = true;
    }

    bool RenderPassHandler::materialRequiresCustomShader(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return false;
        }

        auto it = customShaderRequirementCache.find(materialPath);
        if (it != customShaderRequirementCache.end())
        {
            return it->second;
        }

        bool result = computeMaterialRequiresCustomShader(materialPath);
        customShaderRequirementCache[materialPath] = result;
        return result;
    }

    bool RenderPassHandler::computeMaterialRequiresCustomShader(const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return false;
        }

        std::string parentPath = materialPath;

        if (material::isInstanceFile(materialPath))
        {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(materialPath));
            if (instanceData && instanceData->parentMaterialRef.isValid())
            {
                parentPath = instanceData->parentMaterialRef.resolve();
            }
            else
            {
                return false;
            }
        }

        auto matData = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(parentPath));
        if (!matData)
        {
            return false;
        }

        auto timeNodeId = findTimeNodeId(matData->graph);
        if (!timeNodeId)
        {
            return false;
        }

        return timeNodeReachesPBROutput(matData->graph, *timeNodeId);
    }
}
