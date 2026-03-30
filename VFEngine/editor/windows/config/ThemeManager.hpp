#pragma once
#include "config/EditorPreferences.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows
{
    class ThemeManager
    {
    public:
        static inline void applyTheme(const config::AppearanceSettings& settings)
        {
            ImGuiStyle& style = ImGui::GetStyle();

            if (settings.theme == "Light")
            {
                ImGui::StyleColorsLight();
                applyLightTheme(style);
            }
            else
            {
                ImGui::StyleColorsDark();
                applyDarkTheme(style);
            }

            applyAccentColor(style, settings.accentColor);
            applyStyleVariables(style, settings);

            style.ScaleAllSizes(settings.uiScale);

            if (settings.compactMode)
            {
                style.WindowPadding = ImVec2(4.0f, 4.0f);
                style.FramePadding = ImVec2(2.0f, 2.0f);
                style.ItemSpacing = ImVec2(4.0f, 2.0f);
            }

            if (settings.panelShadows)
            {
                float shadowAlpha = (settings.theme == "Light") ? 0.15f : 0.3f;
                style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, shadowAlpha);
            }
            else
            {
                style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            style.FontScaleMain = static_cast<float>(settings.fontSize) / 18.0f;
        }

    private:
        static inline void applyDarkTheme(ImGuiStyle& style)
        {
            // Unreal Engine-style neutral dark gray
            style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.53f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 0.80f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.35f);
        }

        static inline void applyLightTheme(ImGuiStyle& style)
        {
            style.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.70f, 0.70f, 0.70f, 0.65f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.86f, 0.86f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.78f, 0.78f, 0.84f, 1.00f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.70f, 0.70f, 0.78f, 1.00f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.82f, 0.82f, 0.86f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.86f, 0.86f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.90f, 0.90f, 0.92f, 0.53f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.80f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.72f, 0.78f, 1.00f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.82f, 0.82f, 0.88f, 0.80f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.76f, 0.76f, 0.84f, 1.00f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.70f, 0.70f, 0.70f, 0.50f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.80f, 0.80f, 0.85f, 1.00f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.86f, 0.86f, 0.90f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.82f, 0.82f, 0.88f, 1.00f);
            style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.20f, 0.20f, 0.20f, 0.70f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.50f, 0.50f, 0.50f, 0.35f);
        }

        static inline void applyAccentColor(ImGuiStyle& style, const glm::vec4& accent)
        {
            auto toIm = [](const glm::vec4& v) { return ImVec4(v.x, v.y, v.z, v.w); };
            auto bright = [](const glm::vec4& c, float f) {
                return ImVec4(std::min(c.x * f, 1.0f), std::min(c.y * f, 1.0f), std::min(c.z * f, 1.0f), c.w);
            };

            ImVec4 base = toIm(accent);
            ImVec4 brighter = bright(accent, 1.15f);
            ImVec4 dimmer = bright(accent, 0.7f);
            ImVec4 semiTransparent = ImVec4(accent.x, accent.y, accent.z, 0.35f);
            ImVec4 transparent70 = ImVec4(accent.x, accent.y, accent.z, 0.70f);

            style.Colors[ImGuiCol_CheckMark] = base;
            style.Colors[ImGuiCol_SliderGrab] = base;
            style.Colors[ImGuiCol_SliderGrabActive] = brighter;
            style.Colors[ImGuiCol_ButtonActive] = base;
            style.Colors[ImGuiCol_HeaderActive] = base;
            style.Colors[ImGuiCol_SeparatorHovered] = base;
            style.Colors[ImGuiCol_SeparatorActive] = base;
            style.Colors[ImGuiCol_ResizeGripHovered] = base;
            style.Colors[ImGuiCol_ResizeGripActive] = base;
            style.Colors[ImGuiCol_TabHovered] = base;
            style.Colors[ImGuiCol_TabActive] = base;
            style.Colors[ImGuiCol_ScrollbarGrab] = dimmer;
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = base;
            style.Colors[ImGuiCol_ScrollbarGrabActive] = brighter;
            style.Colors[ImGuiCol_DockingPreview] = transparent70;
            style.Colors[ImGuiCol_PlotHistogram] = base;
            style.Colors[ImGuiCol_PlotHistogramHovered] = brighter;
            style.Colors[ImGuiCol_PlotLinesHovered] = brighter;
            style.Colors[ImGuiCol_TextSelectedBg] = semiTransparent;
            style.Colors[ImGuiCol_DragDropTarget] = base;
            style.Colors[ImGuiCol_NavHighlight] = base;
        }

        static inline void applyStyleVariables(ImGuiStyle& style, const config::AppearanceSettings& settings)
        {
            style.WindowPadding = ImVec2(8.0f, 8.0f);

            float panelRounding = settings.roundedCorners ? 6.0f : 0.0f;
            float controlRounding = settings.roundedCorners ? 4.0f : 0.0f;

            style.WindowRounding = panelRounding;
            style.ChildRounding = panelRounding;
            style.PopupRounding = panelRounding;
            style.ScrollbarRounding = panelRounding;
            style.FrameRounding = controlRounding;
            style.GrabRounding = controlRounding;
            style.TabRounding = controlRounding;

            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
        }
    };
}
