#include "ImagePreviewWindow.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include <glm/glm.hpp>
#include <filesystem>

namespace windows
{
    ImagePreviewWindow::ImagePreviewWindow(const std::string& filePath, bool hdr)
        : imagePath(filePath)
        , isHDR(hdr)
    {
        // Extract filename for window title
        std::filesystem::path path(filePath);
        windowTitle = (hdr ? "HDR Preview: " : "Image Preview: ") + path.filename().string();
    }

    ImagePreviewWindow::~ImagePreviewWindow()
    {
        if (imageHandle.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand releaseCmd;
            releaseCmd.handle = imageHandle.imguiDescriptorSet;
            dispatcher.execute(releaseCmd);
        }
    }

    void ImagePreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        // Load image on first draw
        if (needsInit)
        {
            loadImage();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                // Split layout: left panel for info, right for image
                float panelWidth = 150.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Info panel on the left
                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Image viewport on the right
                float viewportWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild("ImagePanel", ImVec2(viewportWidth, contentSize.y), true,
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawImagePanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    void ImagePreviewWindow::loadImage()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::render::LoadEditorTextureCommand loadCmd;
        loadCmd.path = imagePath;
        loadCmd.isHDR = isHDR;
        imageHandle = dispatcher.execute(loadCmd);
    }

    void ImagePreviewWindow::drawImagePanel()
    {
        if (!imageHandle.isValid())
        {
            ImGui::TextDisabled("Failed to load image");
            return;
        }

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        // Calculate image size maintaining aspect ratio
        float imageAspect = static_cast<float>(imageHandle.width) / static_cast<float>(imageHandle.height);
        float availAspect = availSize.x / availSize.y;

        ImVec2 imageSize;
        if (imageAspect > availAspect)
        {
            // Image is wider - fit to width
            imageSize.x = availSize.x * zoom;
            imageSize.y = imageSize.x / imageAspect;
        }
        else
        {
            // Image is taller - fit to height
            imageSize.y = availSize.y * zoom;
            imageSize.x = imageSize.y * imageAspect;
        }

        // Center the image
        float offsetX = (availSize.x - imageSize.x) * 0.5f + panX;
        float offsetY = (availSize.y - imageSize.y) * 0.5f + panY;

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));
        ImGui::Image(imageHandle.imguiDescriptorSet, imageSize);

        // Handle scroll to zoom
        if (ImGui::IsWindowHovered())
        {
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f)
            {
                zoom *= (1.0f + scroll * 0.1f);
                zoom = glm::clamp(zoom, 0.1f, 10.0f);
            }

            // Handle middle mouse drag to pan
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
                panX += delta.x;
                panY += delta.y;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
            }
        }
    }

    void ImagePreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Info");
        ImGui::Separator();

        if (imageHandle.isValid())
        {
            ImGui::Text("Width: %d", imageHandle.width);
            ImGui::Text("Height: %d", imageHandle.height);
            ImGui::Text("Type: %s", isHDR ? "HDR" : "LDR");
        }
        else
        {
            ImGui::TextDisabled("No image");
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Zoom controls
        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            ImGui::SliderFloat("##Zoom", &zoom, 0.1f, 10.0f, "%.1fx");

            // Zoom buttons
            if (ImGui::Button("+", ImVec2(itemWidth / 2 - 2, 0)))
            {
                zoom = glm::clamp(zoom * 1.2f, 0.1f, 10.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("-", ImVec2(itemWidth / 2 - 2, 0)))
            {
                zoom = glm::clamp(zoom / 1.2f, 0.1f, 10.0f);
            }

            ImGui::Spacing();
            
            if (ImGui::Button("Reset View", ImVec2(-1, 0)))
            {
                zoom = 1.0f;
                panX = 0.0f;
                panY = 0.0f;
            }
        }
    }
}
