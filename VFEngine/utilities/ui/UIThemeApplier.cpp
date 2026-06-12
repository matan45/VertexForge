#include "UIThemeApplier.hpp"
#include "../components/Components.hpp"

namespace utilities::ui
{
    namespace
    {
        void applyColor(const UIThemeStyle& style, const char* name, glm::vec4& target)
        {
            auto it = style.colors.find(name);
            if (it != style.colors.end())
            {
                target = it->second;
            }
        }

        void applyFloat(const UIThemeStyle& style, const char* name, float& target)
        {
            auto it = style.floats.find(name);
            if (it != style.floats.end())
            {
                target = it->second;
            }
        }

        void applyAsset(const UIThemeStyle& style, const char* name, asset::AssetRef& target)
        {
            auto it = style.assets.find(name);
            if (it != style.assets.end())
            {
                target = it->second;
            }
        }
    }

    bool UIThemeApplier::applyStyleToEntity(entt::registry& registry, const UIThemeStyle& style, entt::entity entity)
    {
        bool touched = false;

        if (auto* label = registry.try_get<components::UILabelComponent>(entity))
        {
            applyColor(style, "labelColor", label->color);
            applyFloat(style, "fontSize", label->fontSize);
            applyAsset(style, "font", label->fontRef);
            touched = true;
        }

        if (auto* image = registry.try_get<components::UIImageComponent>(entity))
        {
            applyColor(style, "imageTint", image->colorTint);
            applyAsset(style, "imageTexture", image->textureRef);
            touched = true;
        }

        if (auto* button = registry.try_get<components::UIButtonComponent>(entity))
        {
            applyColor(style, "buttonNormalColor", button->normalColor);
            applyColor(style, "buttonHoveredColor", button->hoveredColor);
            applyColor(style, "buttonPressedColor", button->pressedColor);
            applyColor(style, "buttonDisabledColor", button->disabledColor);
            applyAsset(style, "buttonNormalTexture", button->normalTextureRef);
            applyAsset(style, "buttonHoveredTexture", button->hoverTextureRef);
            applyAsset(style, "buttonPressedTexture", button->pressedTextureRef);
            applyAsset(style, "buttonDisabledTexture", button->disabledTextureRef);
            touched = true;
        }

        if (auto* progress = registry.try_get<components::UIProgressBarComponent>(entity))
        {
            applyColor(style, "progressTrackColor", progress->trackColor);
            applyColor(style, "progressFillColor", progress->fillColor);
            touched = true;
        }

        return touched;
    }

    namespace
    {
        int applyRecursive(entt::registry& registry, const UITheme& theme, entt::entity entity)
        {
            int touched = 0;

            if (auto* styleComp = registry.try_get<components::UIStyleComponent>(entity))
            {
                if (const UIThemeStyle* style = theme.findStyle(styleComp->styleKey))
                {
                    if (UIThemeApplier::applyStyleToEntity(registry, *style, entity))
                    {
                        ++touched;
                    }
                }
            }

            if (auto* children = registry.try_get<components::ChildrenComponent>(entity))
            {
                for (entt::entity child : children->children)
                {
                    if (registry.valid(child))
                    {
                        touched += applyRecursive(registry, theme, child);
                    }
                }
            }
            return touched;
        }
    }

    int UIThemeApplier::applyTheme(entt::registry& registry, const UITheme& theme, entt::entity canvasRoot)
    {
        if (!registry.valid(canvasRoot))
        {
            return 0;
        }
        return applyRecursive(registry, theme, canvasRoot);
    }
}
