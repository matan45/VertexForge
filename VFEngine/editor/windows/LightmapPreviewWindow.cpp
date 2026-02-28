#include "LightmapPreviewWindow.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "lightbake/LightmapAtlas.hpp"
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace windows
{
    LightmapPreviewWindow::LightmapPreviewWindow(const std::string& filePath)
        : lightmapPath(filePath)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Lightmap: " + path.filename().string();
    }

    LightmapPreviewWindow::~LightmapPreviewWindow()
    {
        if (textureHandle.isValid())
        {
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = textureHandle.imguiDescriptorSet;
            events::EventDispatcher::instance().execute(releaseCmd);
        }
    }

    void LightmapPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        if (needsLoad)
        {
            loadLightmap();
            needsLoad = false;
        }

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float panelWidth = 180.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                float viewportWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("ImagePanel", ImVec2(viewportWidth, contentSize.y), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawImagePanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void LightmapPreviewWindow::loadLightmap()
    {
        lightmapData = lightbake::LightmapAtlas::load(lightmapPath);

        if (lightmapData.width == 0 || lightmapData.height == 0 || lightmapData.texels.empty())
        {
            loadFailed = true;
            return;
        }

        // Convert HDR float texels to RGBA8 for display
        uint32_t w = lightmapData.width;
        uint32_t h = lightmapData.height;
        uint32_t ch = lightmapData.channels;

        resource::TextureData texData;
        texData.width = w;
        texData.height = h;
        texData.numbersOfChannels = 4; // RGBA
        texData.mipLevels = 1;

        resource::MipLevelData mip0;
        mip0.width = w;
        mip0.height = h;
        mip0.data.resize(w * h * 4);

        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                size_t srcIdx = (y * w + x) * ch;
                size_t dstIdx = (y * w + x) * 4;

                float r = (ch > 0 && srcIdx < lightmapData.texels.size()) ? lightmapData.texels[srcIdx] : 0.0f;
                float g = (ch > 1 && srcIdx + 1 < lightmapData.texels.size()) ? lightmapData.texels[srcIdx + 1] : 0.0f;
                float b = (ch > 2 && srcIdx + 2 < lightmapData.texels.size()) ? lightmapData.texels[srcIdx + 2] : 0.0f;

                // Apply exposure and simple Reinhard tone mapping
                r *= exposure;
                g *= exposure;
                b *= exposure;
                r = r / (1.0f + r);
                g = g / (1.0f + g);
                b = b / (1.0f + b);

                // Gamma correct
                r = std::pow(r, 1.0f / 2.2f);
                g = std::pow(g, 1.0f / 2.2f);
                b = std::pow(b, 1.0f / 2.2f);

                mip0.data[dstIdx + 0] = static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                mip0.data[dstIdx + 1] = static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                mip0.data[dstIdx + 2] = static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f));
                mip0.data[dstIdx + 3] = 255;
            }
        }

        texData.mipData.push_back(std::move(mip0));

        events::render::LoadEditorTextureFromDataCommand loadCmd;
        loadCmd.textureData = std::move(texData);
        textureHandle = events::EventDispatcher::instance().execute(loadCmd);

        if (!textureHandle.isValid())
        {
            loadFailed = true;
        }
    }

    void LightmapPreviewWindow::drawImagePanel()
    {
        if (loadFailed)
        {
            ImGui::TextDisabled("Failed to load lightmap");
            return;
        }

        if (!textureHandle.isValid())
        {
            ImGui::TextDisabled("No lightmap loaded");
            return;
        }

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        float imageAspect = static_cast<float>(textureHandle.width) / static_cast<float>(textureHandle.height);
        float availAspect = availSize.x / availSize.y;

        ImVec2 imageSize;
        if (imageAspect > availAspect)
        {
            imageSize.x = availSize.x * zoom;
            imageSize.y = imageSize.x / imageAspect;
        }
        else
        {
            imageSize.y = availSize.y * zoom;
            imageSize.x = imageSize.y * imageAspect;
        }

        ImGui::Image(textureHandle.imguiDescriptorSet, imageSize);
    }

    void LightmapPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Lightmap Info");
        ImGui::Separator();

        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Load failed");
            return;
        }

        if (lightmapData.width > 0)
        {
            ImGui::Text("Size: %ux%u", lightmapData.width, lightmapData.height);
            ImGui::Text("Channels: %u", lightmapData.channels);
            ImGui::Text("Entities: %zu", lightmapData.entityRegions.size());
        }
        else
        {
            ImGui::TextDisabled("No data");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float prevExposure = exposure;
            ImGui::SliderFloat("Exposure", &exposure, 0.1f, 10.0f, "%.1f");

            if (prevExposure != exposure && lightmapData.width > 0)
            {
                if (textureHandle.isValid())
                {
                    events::render::ReleaseEditorTextureCommand releaseCmd;
                    releaseCmd.handle = textureHandle.imguiDescriptorSet;
                    events::EventDispatcher::instance().execute(releaseCmd);
                    textureHandle = {};
                }
                loadFailed = false;
                needsLoad = true;
            }

            ImGui::Spacing();

            ImGui::Text("Zoom");
            ImGui::SliderFloat("##Zoom", &zoom, 0.1f, 10.0f, "%.1fx");

            if (ImGui::Button("Reset View", ImVec2(-1, 0)))
            {
                zoom = 1.0f;
                if (exposure != 1.0f)
                {
                    exposure = 1.0f;
                    if (textureHandle.isValid())
                    {
                        events::render::ReleaseEditorTextureCommand releaseCmd;
                        releaseCmd.handle = textureHandle.imguiDescriptorSet;
                        events::EventDispatcher::instance().execute(releaseCmd);
                        textureHandle = {};
                    }
                    loadFailed = false;
                    needsLoad = true;
                }
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (!lightmapData.entityRegions.empty() &&
            ImGui::CollapsingHeader("Entity Regions"))
        {
            for (size_t i = 0; i < lightmapData.entityRegions.size(); ++i)
            {
                const auto& region = lightmapData.entityRegions[i];
                ImGui::PushID(static_cast<int>(i));

                if (ImGui::TreeNode("##entity", "Entity %u", region.entityId))
                {
                    ImGui::Text("Offset: %u, %u", region.x, region.y);
                    ImGui::Text("Size: %ux%u", region.width, region.height);
                    ImGui::Text("Scale: %.3f, %.3f", region.scaleOffset.x, region.scaleOffset.y);
                    ImGui::Text("Offset: %.3f, %.3f", region.scaleOffset.z, region.scaleOffset.w);
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
    }
}
