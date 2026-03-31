#include "UI.h"

#include "CenteredSeparatorText.hpp"
#include "imgui-knobs.h"

#include "config.h"

// -----------------------------------------------------------------------
// Knob scale mark tables

// Room Size (m²) – logarithmic, same physical range as Trial4.1
static const ImGuiKnobs_Mod::KnobScaleMark kSizeMarks[] = {
    {   0.625f, "0.6"  },
    {   1.25f,  "1.25" },
    {   2.5f,   "2.5"  },
    {   5.0f,   "5"    },
    {  10.0f,   "10"   },
    {  20.0f,   "20"   },
    {  40.0f,   "40"   },
    {  80.0f,   "80"   },
    { 160.0f,   "160"  },
    { 320.0f,   "320"  },
    { 640.0f,   "640"  },
};

// Damping – normalised 0..1
static const ImGuiKnobs_Mod::KnobScaleMark kDampingMarks[] = {
    { 0.0f,    "MIN" },
    { 0.125f,  nullptr },
    { 0.25f,   nullptr },
    { 0.375f,  nullptr },
    { 0.5f,    nullptr },
    { 0.625f,  nullptr },
    { 0.75f,   nullptr },
    { 0.875f,  nullptr },
    { 1.0f,    "MAX" },
};

// Pre-Delay (ms) – same physical range as Trial4.1
static const ImGuiKnobs_Mod::KnobScaleMark kPreDelayMarks[] = {
    { -150.0f, "-150" },
    { -120.0f, "-120" },
    {  -90.0f, "-90"  },
    {  -60.0f, "-60"  },
    {  -30.0f, "-30"  },
    {    0.0f, "0"    },
    {   30.0f, "30"   },
    {   60.0f, "60"   },
    {   90.0f, "90"   },
    {  120.0f, "120"  },
    {  150.0f, "150"  },
};

// Stereo Width – normalised 0..1
static const ImGuiKnobs_Mod::KnobScaleMark kStereoMarks[] = {
    { 0.0f,   "MONO" },
    { 0.125f,  nullptr },
     { 0.25f,  nullptr },
    { 0.375f, nullptr },
    { 0.5f,   nullptr },
    { 0.625f, nullptr },
    { 0.75f,  nullptr },
    { 0.875f, nullptr },
    { 1.0f,   "WIDE" },
};

// HF Colour – normalised 0..1
static const ImGuiKnobs_Mod::KnobScaleMark kHFColourMarks[] = {
    { 0.0f,   "DARK" },
    { 0.125f,  nullptr },
     { 0.25f,  nullptr },
    { 0.375f, nullptr },
    { 0.5f,   "MID" },
    { 0.625f, nullptr },
    { 0.75f,  nullptr },
    { 0.875f, nullptr },
    { 1.0f,   "BRITE" },
};

// Early Mix – normalised 0..1
static const ImGuiKnobs_Mod::KnobScaleMark kEarlyMixMarks[] = {
    { 0.0f,   "OFF" },
    { 0.125f,  nullptr },
     { 0.25f,  nullptr },
    { 0.375f, nullptr },
    { 0.5f,   "MID" },
    { 0.625f, nullptr },
    { 0.75f,  nullptr },
    { 0.875f, nullptr },
    { 1.0f,   "MAX" },
};

// Wet/Dry Mix – normalised 0..1
static const ImGuiKnobs_Mod::KnobScaleMark kMixMarks[] = {
    { 0.0f,    "DIR." },
    { 0.125f,  nullptr },
    { 0.25f,   nullptr },
    { 0.375f,  nullptr },
    { 0.5f,    "1:1" },
    { 0.625f,  nullptr },
    { 0.75f,   nullptr },
    { 0.875f,  nullptr },
    { 1.0f,    "EFF." },
};

// Output Level – normalised 0..1, pivot at 0.5 (= 0 dB)
// DSP formula: gain = 2^((vol - 0.5) * 10)  →  range -15 dB .. +15 dB
static const ImGuiKnobs_Mod::KnobScaleMark kLevelMarks[] = {
    { 0.0f,   "-15" },
    { 0.125f,  nullptr },
    { 0.25f,   nullptr },
    { 0.375f,  nullptr },
    { 0.5f,   "0"   },
    { 0.625f,  nullptr },
    { 0.75f,   nullptr },
    { 0.875f,  nullptr },
    { 1.0f,   "+15" },
};

// -----------------------------------------------------------------------
// Constructor and UI callbacks

ClassicReverbUI::ClassicReverbUI()
    : DISTRHO::UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
{
    std::memset(fParams, 0, sizeof(fParams));
    _loadFonts();
}

void ClassicReverbUI::parameterChanged(uint32_t index, float value)
{
    DISTRHO_SAFE_ASSERT_RETURN(index < PARAM_COUNT, )
    fParams[index] = value;
}

void ClassicReverbUI::onImGuiDisplay()
{
    const float margin = 4.0f;

    // ── Main viewport (fullscreen, no decoration) ────────────────────────────
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    static constexpr auto window_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings;

    // White background for the host window
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    if (ImGui::Begin("Main Window", nullptr, window_flags))
    {
        const float  rounding = 10.0f;
        const ImVec2 winSize  = ImGui::GetWindowSize();

        // ── Draw the plugin chassis background ───────────────────────────────
        _drawChassisBackground(margin, rounding);

        // ── Child window (transparent overlay for placing controls) ──────────
        ImGui::SetCursorPos(ImVec2(margin, margin));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);

        const ImVec2 childSize = ImVec2(winSize.x - 2.0f * margin, winSize.y - 2.0f * margin);
        if (ImGui::BeginChild("BackgroundPanel", childSize, false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse))
        {
            // Left margin
            ImGui::Dummy(ImVec2(2, 0));
            ImGui::SameLine();

            // ── REVERBERATION section: SIZE, DAMPING, PREDELAY ──────────────
            if (_BeginSection("REVERBERATION", (90.0f - 4.0f) * 3))
            {
                // Extra left margin so the leftmost scale mark isn't clipped
                ImGui::Dummy(ImVec2(12, 0));
                ImGui::SameLine();

                _addKnob(PARAM_ROOM_SIZE,  "SIZE (m\xc2\xb2)", 0.625f, 640.0f,
                         kSizeMarks, IM_ARRAYSIZE(kSizeMarks), /*log=*/true);

                ImGui::SameLine(0, 35);

                _addKnob(PARAM_DAMPING, "DAMPING", 0.0f, 1.0f,
                         kDampingMarks, IM_ARRAYSIZE(kDampingMarks));

                ImGui::SameLine(0, 35);

                _addKnob(PARAM_PREDELAY, "PREDELAY (ms)", -150.0f, 150.0f,
                         kPreDelayMarks, IM_ARRAYSIZE(kPreDelayMarks));

                _EndSection();
            }

            ImGui::SameLine(0, 10);

            // ── SHAPING section: STEREO WIDTH, HF COLOUR ─────────────────────
            if (_BeginSection("SHAPING", (90.0f - 6.0f) * 2))
            {
                ImGui::Dummy(ImVec2(8, 0));
                ImGui::SameLine();

                _addKnob(PARAM_STEREO, "STEREO WIDTH", 0.0f, 1.0f,
                         kStereoMarks, IM_ARRAYSIZE(kStereoMarks));

                ImGui::SameLine(0, 20);

                _addKnob(PARAM_HF_COLOUR, "HF COLOUR", 0.0f, 1.0f,
                         kHFColourMarks, IM_ARRAYSIZE(kHFColourMarks));

                _EndSection();
            }

            ImGui::SameLine(0, 16 - 2);

            // ── OUTPUT section: EARLY MIX, WET/DRY, LEVEL ───────────────────
            if (_BeginSection("OUTPUT", (80.0f - 2.0f) * 3))
            {
                ImGui::Dummy(ImVec2(2, 0));
                ImGui::SameLine();

                _addKnob(PARAM_EARLY_MIX, "EARLY MIX", 0.0f, 1.0f,
                         kEarlyMixMarks, IM_ARRAYSIZE(kEarlyMixMarks));

                ImGui::SameLine(0, 30);

                _addKnob(PARAM_WET_DRY, "MIX", 0.0f, 1.0f,
                         kMixMarks, IM_ARRAYSIZE(kMixMarks));

                ImGui::SameLine(0, 30);

                // NOTE: Level knob is stored as linear 0..1 but mapped to dB in the DSP,
                //       so we use a custom scale mark table and disable ImGuiKnobs' built-in logarithmic mode.
                _addKnob(PARAM_OUTPUT_VOLUME, " LEVEL (dB)", 0.0f, 1.0f,
                         kLevelMarks, IM_ARRAYSIZE(kLevelMarks),
                         /*log=*/false, /*use_pivot=*/true, /*pivot=*/0.5f);

                _EndSection();
            }

            ImGui::SameLine(0, 10.0f);

            // ── Right panel: Logo + Plugin name ─────────────────────────────
            {
                ImGui::BeginGroup();

                ImGui::Dummy(ImVec2(0, 2));

                _drawKjaerhusLogo(ImVec2(100, 50));

                // Vertical spacer (matches space where preset button would be)
                ImGui::Dummy(ImVec2(0, 23));

                _drawPluginName();

                ImGui::EndGroup();
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleVar();   // ChildRounding
        ImGui::PopStyleColor(); // ChildBg

        ImGui::End();
    }
    ImGui::PopStyleColor(); // WindowBg

    // ── "About" window ───────────────────────────────────────────────────────
    static constexpr auto about_window_flags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoCollapse    | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

    if (fAboutWindowOpened)
    {
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        if (ImGui::Begin("About Window", &fAboutWindowOpened, about_window_flags))
        {
            {
                ImGui::Columns(2, "AboutColumns", false);
                ImGui::SetColumnWidth(0, 400.0f - 5.0f);
                ImGui::SetColumnWidth(1, 420.0f - 15.0f);

                {
                    const String versionStr = String("Classic Reverb RE-03") + "  |  Version " +
                                        String(VERSION_MAJOR) + "." +
                                        String(VERSION_MINOR) + "." +
                                        String(VERSION_PATCH);

                    ImGui::SeparatorText(versionStr);
                    ImGui::Text("Reverse engineering of Kjaerhus Audio Classic Reverb (2003).");
                    ImGui::Text("Original algorithm by Kjaerhus Audio.");
                    ImGui::Text("Copyright (c) 2026 AnClark Liu <clarklaw4701@qq.com>");

                    ImGui::SeparatorText("License: GNU General Public License v3.0 or later");
                    ImGui::Dummy(ImVec2(0, 2));
                    ImGui::TextWrapped("Classic Reverb RE-03 is free software: "
                                       "you can redistribute it and/or modify it under the terms of the "
                                       "GNU General Public License as published by the Free Software Foundation, "
                                       "either version 3 of the License, or (at your option) any later version.");
                }

                ImGui::NextColumn();

                {
                    ImGui::SeparatorText("Disclaimer");
                    ImGui::TextWrapped("This is an unofficial, reverse-engineered clone of the discontinued "
                                       "Kjaerhus Classic Reverb, aiming at bringing this vintage and "
                                       "fantastic plugin to life again.");
                    ImGui::TextWrapped("This project is NOT related to official Kjaerhus Audio, "
                                       "Acoustica LLC. and their affiliates.");
                    ImGui::Dummy(ImVec2(0, 2));
                    ImGui::TextWrapped("The Kjaerhus logo is used under fair use for identification "
                                       "purposes only, and is not intended to infringe any trademarks.");
                    ImGui::Dummy(ImVec2(0, 2));
                    ImGui::TextWrapped("VST is a trademark of Steinberg GmbH.");
                }

                ImGui::Columns(1);
            }

            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0x2f, 0x4d, 0x44, 0xff));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0x2f + 20, 0x4d + 20, 0x44 + 20, 0xff));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x2f + 40, 0x4d + 40, 0x44 + 40, 0xff));

                static constexpr ImVec2 button_size = ImVec2(60 - 5, 25);
                ImVec2 buttonPos = ImVec2(viewport->Pos.x + viewport->Size.x - button_size.x - 22.0f,
                                         viewport->Pos.y + viewport->Size.y - button_size.y - 10.0f);
                ImGui::SetCursorScreenPos(buttonPos);
                if (ImGui::Button("OK", button_size))
                    fAboutWindowOpened = false;

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(); // FrameRounding
            }

            ImGui::End();
        }
    }

    // Update OS mouse cursor from ImGui cursor state
    _UpdateMouseCursor();
}

// -----------------------------------------------------------------------

START_NAMESPACE_DISTRHO

UI* createUI()
{
    return new ClassicReverbUI();
}

END_NAMESPACE_DISTRHO
