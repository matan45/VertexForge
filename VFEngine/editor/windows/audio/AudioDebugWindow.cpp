#include "AudioDebugWindow.hpp"
#include "AudioWidgets.hpp"
#include "events/EventDispatcher.hpp"
#include "events/audio/AudioSettingsEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
    constexpr float kLevelMeterWidth = 70.0f;
    constexpr float kLevelMeterHeight = 12.0f;

    // Dim a paused voice's whole row: it still holds a slot, but it is contributing nothing.
    const ImVec4 kPausedTint(0.6f, 0.6f, 0.6f, 1.0f);
}

namespace windows
{
    void AudioDebugWindow::setCaptureEnabled(bool enabled)
    {
        try
        {
            // Declare-then-assign: events derive from IEvent, which has virtual functions,
            // so they are not aggregates and `Command{enabled}` would not compile.
            events::audio::SetVoiceDebugEnabledCommand cmd;
            cmd.enabled = enabled;
            events::EventDispatcher::instance().execute(cmd);
        }
        catch (...)
        {
            // No audio service registered (no Audio DLL). Nothing to enable; drawSummary
            // reports it. Swallowed rather than propagated because this is a debug view and
            // the dispatcher throws straight through ImGui's frame.
        }
    }

    void AudioDebugWindow::draw()
    {
        // Edge-detect around `visible`, which ImGui's window close button also writes. This
        // is what keeps the audio thread from building rows nobody is looking at — and it
        // has to run even when !visible, to catch the close.
        if (visible != captureEnabled)
        {
            setCaptureEnabled(visible);
            captureEnabled = visible;
            // Re-arm so reopening refreshes immediately rather than showing the stale rows
            // from the last time it was open.
            refreshTimer = REFRESH_INTERVAL;
        }

        if (!visible) return;

        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshData();
            refreshTimer = 0.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(820, 460), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Audio Debug", &visible))
        {
            drawSummary();
            ImGui::Separator();
            drawVoiceTable();
        }
        ImGui::End();
    }

    void AudioDebugWindow::refreshData()
    {
        // The dispatcher throws when nothing has registered a handler, which is the normal
        // state with no Audio DLL — AudioConfigWindow guards the same way. AudioMixerWindow
        // does not, and would take the editor's frame down with it.
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            cachedVoices = dispatcher.query(events::audio::GetActiveVoicesQuery{});
            cachedStats = dispatcher.query(events::audio::GetVoiceCountQuery{});
            audioAvailable = true;
        }
        catch (...)
        {
            cachedVoices.clear();
            cachedStats = types::AudioVoiceStats{};
            audioAvailable = false;
        }
    }

    void AudioDebugWindow::drawSummary()
    {
        if (!audioAvailable)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Audio service unavailable");
            return;
        }

        audiowidgets::drawVoiceCountReadout(cachedStats);

        const int streams = voicerows::countKind(cachedVoices, types::AudioVoiceKind::Stream);
        ImGui::SameLine();
        ImGui::TextDisabled("| streaming: %d", streams);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Streaming voices never draw from the source pool, so they do\n"
                              "not count against the real-voice budget.");

        // Per-bus counts: the fastest way to see which bus is eating the budget without
        // reading every row.
        const auto busCounts = voicerows::countByBus(cachedVoices);
        if (!busCounts.empty())
        {
            ImGui::TextDisabled("Buses:");
            for (const auto& bus : busCounts)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s %d", bus.busName.c_str(), bus.voices);
            }
        }

        ImGui::TextDisabled("Levels are estimated (pre-effects), sampled at 10 Hz");
    }

    void AudioDebugWindow::drawVoiceTable()
    {
        if (cachedVoices.empty())
        {
            bool playMode = false;
            try
            {
                playMode = events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
            }
            catch (...)
            {
                playMode = false;
            }

            // Drawn in edit mode too rather than hidden: the editor previews audio, and
            // "nothing is playing" is a legitimate and useful answer. The hint only explains
            // the empty table for the case where the user expected sound.
            if (audioAvailable && !playMode)
                ImGui::TextDisabled("Nothing playing. Enter Play mode, or audition a sound.");
            else if (audioAvailable)
                ImGui::TextDisabled("Nothing playing.");
            return;
        }

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("AudioVoicesTable", 7, flags,
                               ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            return;
        }

        // Column order must match voicerows::Column — the sort spec is an index into it.
        ImGui::TableSetupColumn("Sound", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Level",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
                                110.0f);
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
        {
            if (sortSpecs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                sortColumn = static_cast<voicerows::Column>(spec.ColumnIndex);
                sortAscending = spec.SortDirection == ImGuiSortDirection_Ascending;
                sortSpecs->SpecsDirty = false;
            }
        }
        // Sorted every frame, not just when SpecsDirty: refreshData() replaces the vector in
        // publish order twice a second, so a dirty-only sort would let the rows scramble
        // between refreshes.
        voicerows::sortRows(cachedVoices, sortColumn, sortAscending);

        for (const types::AudioVoiceRow& voice : cachedVoices)
        {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(voice.handle));

            const bool dimmed = !voice.playing;
            if (dimmed)
                ImGui::PushStyleColor(ImGuiCol_Text, kPausedTint);

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(std::string(voicerows::displayFileName(voice.path)).c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\nPosition: %.2f, %.2f, %.2f%s",
                                  voice.path.c_str(),
                                  voice.position.x, voice.position.y, voice.position.z,
                                  voice.playing ? "" : "\n(paused — still holding its slot)");
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(voice.busName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(types::audioVoiceKindToString(voice.kind));
            if (voice.kind == types::AudioVoiceKind::Virtual && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Lost the voice budget. Held on a simulated clock and\n"
                                  "revived at the right offset if it becomes audible again.");
            }

            ImGui::TableSetColumnIndex(3);
            {
                const ImVec2 meterPos = ImGui::GetCursorScreenPos();
                audiowidgets::drawLevelBar(meterPos, ImVec2(kLevelMeterWidth, kLevelMeterHeight),
                                           voice.level, dimmed);
                ImGui::Dummy(ImVec2(kLevelMeterWidth, kLevelMeterHeight));
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Estimated audible level (pre-effects)\n"
                                      "RMS envelope x distance attenuation x source gain\n"
                                      "(source gain carries bus volume, mute and solo)\n"
                                      "Not the bus meters' input, which is pre-bus-fader.\n"
                                      "Floor: %.0f dB", audiowidgets::kMeterFloorDb);
                }
                ImGui::SameLine();
                ImGui::Text("%.0f%%", audiowidgets::meterNorm(voice.level) * 100.0f);
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%u (%s)", static_cast<unsigned>(voice.priority),
                        voicerows::priorityLabel(voice.priority));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Lower is more important: 0 = critical, 128 = normal, 255 = least.");

            ImGui::TableSetColumnIndex(5);
            // 2D and streaming voices are listener-relative, so a distance would be a made-up
            // number rather than a zero.
            if (voice.kind == types::AudioVoiceKind::Sound2D ||
                voice.kind == types::AudioVoiceKind::Stream)
            {
                ImGui::TextDisabled("--");
            }
            else
            {
                ImGui::Text("%.1f", voice.distance);
            }

            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.1fs", voice.playbackPosition);

            if (dimmed)
                ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}
