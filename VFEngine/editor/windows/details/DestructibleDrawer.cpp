#include "DestructibleDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "asset/AssetRef.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "nfd/FileDialog.hpp"
#include <filesystem>
#include <imgui.h>

namespace windows::details
{
    using NfdFilter = std::vector<std::pair<std::wstring, std::wstring>>;

    namespace
    {
        bool drawAssetField(const char* label, const char* id,
                            asset::AssetRef& ref,
                            const NfdFilter& filters)
        {
            bool changed = false;

            if (ref.isValid())
            {
                std::string filename = ref.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("%s: %s", label, filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s: (none)", label);
            }

            ImGui::SameLine();
            std::string selectId = std::string("...##") + id;
            if (ImGui::SmallButton(selectId.c_str()))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(filters);
                if (!path.empty())
                {
                    ref = asset::AssetRef::fromPath(path);
                    changed = true;
                }
            }

            ImGui::SameLine();
            std::string clearId = std::string("X##") + id;
            bool empty = !ref.isValid();
            if (empty) ImGui::BeginDisabled();
            if (ImGui::SmallButton(clearId.c_str()))
            {
                ref = asset::AssetRef::invalid();
                changed = true;
            }
            if (empty) ImGui::EndDisabled();

            return changed;
        }
    }

    bool DestructibleDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasDestructibleComponentQuery hasQuery;
        hasQuery.entity = handle;
        if (!dispatcher.query(hasQuery))
            return false;

        events::scene::GetDestructibleDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);
        if (!dataOpt.has_value())
            return true;

        ImGui::PushID("DestructibleComponent");

        bool remove = false;
        bool isOpen = drawHeader(remove);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            auto data = *dataOpt;
            bool changed = false;

            changed |= drawHealthSettings(data);
            ImGui::Spacing();
            changed |= drawFractureSettings(data);
            ImGui::Spacing();
            changed |= drawEffectSettings(data);
            ImGui::Spacing();
            changed |= drawPropagationSettings(data);

            if (changed)
            {
                events::scene::SetDestructibleDataCommand cmd;
                cmd.entity = handle;
                cmd.data = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        if (remove)
        {
            events::scene::RemoveDestructibleComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        ImGui::PopID();
        return true;
    }

    bool DestructibleDrawer::drawHeader(bool& outRemove)
    {
        bool isOpen = ImGui::CollapsingHeader("Destructible",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::SmallButton("X##RemoveDestructible"))
            outRemove = true;
        ImGui::PopStyleColor();

        return isOpen;
    }

    bool DestructibleDrawer::drawHealthSettings(services::DestructibleComponentData& data)
    {
        bool changed = false;

        ImGui::Text("Health");
        ImGui::Separator();

        if (ImGui::DragFloat("Max Health", &data.maxHealth, 1.0f, 1.0f, 10000.0f))
            changed = true;
        if (ImGui::DragFloat("Destruction Threshold", &data.destructionThreshold, 0.5f, 0.0f, data.maxHealth))
            changed = true;

        const char* modeNames[] = {"One Shot", "Progressive"};
        int modeIdx = static_cast<int>(data.mode);
        if (ImGui::Combo("Mode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames)))
        {
            data.mode = static_cast<components::DestructionMode>(modeIdx);
            changed = true;
        }

        const char* filterNames[] = {"Any", "Explosive", "Ballistic", "Melee"};
        int filterIdx = static_cast<int>(data.damageFilter);
        if (ImGui::Combo("Damage Filter", &filterIdx, filterNames, IM_ARRAYSIZE(filterNames)))
        {
            data.damageFilter = static_cast<components::DamageType>(filterIdx);
            changed = true;
        }

        return changed;
    }

    bool DestructibleDrawer::drawFractureSettings(services::DestructibleComponentData& data)
    {
        bool changed = false;

        ImGui::Text("Fracture");
        ImGui::Separator();

        NfdFilter meshFilters = {{L"VF Mesh (*.vfMesh)", L"*.vfMesh"}};
        bool fractureRefChanged = drawAssetField("Fracture Mesh", "fractureRef", data.fractureAssetRef, meshFilters);
        if (fractureRefChanged)
        {
            // Auto-detect fragment count from .vfmeta
            if (data.fractureAssetRef.isValid())
            {
                std::string meshPath = data.fractureAssetRef.resolve();
                auto metaPath = std::filesystem::path(meshPath).string() + ".vfmeta";
                auto meta = asset::AssetMetadataSerializer::load(metaPath);
                if (meta && meta->fractureData)
                    data.fragmentCount = meta->fractureData->fragmentCount;
            }
            else
            {
                data.fragmentCount = 0;
            }
        }
        changed |= fractureRefChanged;

        int fragCount = static_cast<int>(data.fragmentCount);
        if (ImGui::InputInt("Fragment Count", &fragCount, 1, 5))
        {
            data.fragmentCount = static_cast<uint32_t>(std::max(0, fragCount));
            changed = true;
        }

        if (ImGui::DragFloat("Fragment Mass", &data.fragmentMassTotal, 0.1f, 0.1f, 100.0f))
            changed = true;
        if (ImGui::DragFloat("Fragment Lifetime", &data.fragmentLifetime, 0.5f, 1.0f, 60.0f))
            changed = true;

        return changed;
    }

    bool DestructibleDrawer::drawEffectSettings(services::DestructibleComponentData& data)
    {
        bool changed = false;

        if (!ImGui::TreeNode("Effects"))
            return false;

        NfdFilter vfxFilters = {{L"VF VFX (*.vfVFX)", L"*.vfVFX"}};
        NfdFilter audioFilters = {{L"VF Audio (*.vfAudio)", L"*.vfAudio"}};
        NfdFilter texFilters = {{L"VF Image (*.vfImage)", L"*.vfImage"}};

        ImGui::Text("On Damage");
        changed |= drawAssetField("VFX", "dmgVFX", data.onDamageVFX, vfxFilters);
        changed |= drawAssetField("Audio", "dmgAudio", data.onDamageAudio, audioFilters);

        ImGui::Spacing();
        ImGui::Text("On Destroy");
        changed |= drawAssetField("VFX", "destroyVFX", data.onDestroyVFX, vfxFilters);
        changed |= drawAssetField("Audio", "destroyAudio", data.onDestroyAudio, audioFilters);

        ImGui::Spacing();
        ImGui::Text("Fragments");
        changed |= drawAssetField("Collision Audio", "fragAudio", data.fragmentCollisionAudio, audioFilters);

        ImGui::Spacing();
        ImGui::Text("Decals");
        changed |= drawAssetField("Albedo", "decalAlbedo", data.damageDecalAlbedo, texFilters);
        changed |= drawAssetField("Normal", "decalNormal", data.damageDecalNormal, texFilters);

        ImGui::TreePop();
        return changed;
    }

    bool DestructibleDrawer::drawPropagationSettings(services::DestructibleComponentData& data)
    {
        bool changed = false;

        ImGui::Text("Propagation");
        ImGui::Separator();

        if (ImGui::DragFloat("Radius", &data.propagationRadius, 0.1f, 0.0f, 50.0f))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Radial damage to nearby destructibles (0 = disabled)");

        if (data.propagationRadius > 0.0f)
        {
            if (ImGui::DragFloat("Damage", &data.propagationDamage, 1.0f, 0.0f, 1000.0f))
                changed = true;
        }

        return changed;
    }
}
