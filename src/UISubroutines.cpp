#include "UI.h"

#include "CenteredSeparatorText.hpp"
#include "imgui-knobs.h"
#include "AddTextScaled.hpp"

#include "../fonts/LiberationSans-Regular.hpp"
#include "../fonts/CormorantFont.hpp"
#include "src/Resources.hpp"    // For DejaVu Sans font (bundled with DGL)
#include "../fonts/FontAwesome5.hpp"
#include "../fonts/IconFontAwesome5.h"

// Scale-mark style shared across all knobs
ImGuiKnobs_Mod::KnobScaleMarkStyle kScaleMarkStyle = {
    .outer_radius = 1.20f,
    .tick_length  = 0.50f,
    .font_size    = 12.5f,
};

// -----------------------------------------------------------------------
// Font loading

void ClassicReverbUI::_loadFonts()
{
    // Font atlas:
    //   #0  Liberation Sans 12.5 px  – chassis labels, knob labels
    //   #1  Liberation Sans 14.0 px  – section titles
    //   #2  Liberation Sans 20.0 px  – scale marks (down-sampled at draw time), logo text
    //   #3  Cormorant SemiBold Italic 20.0 px  – "Classic Reverb" plugin-name text
    //   #4  DejaVu Sans 14.5 px  – ImGui UI text (tooltips, menus), merged with Font Awesome icons

    ImGuiIO& io(ImGui::GetIO());

    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    fc.OversampleH = 1;
    fc.OversampleV = 1;
    fc.PixelSnapH  = true;

    io.Fonts->Clear();

    // ↓ Font #0: Chassis regular text (knob labels, small chassis text)
    static constexpr ImWchar kChassisRanges[] = { ' ', '~', 178, 178 + 1, 0 }; // Basic Latin + '²'
    io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)LiberationSansTTF_Compressed_compressed_data,
        LiberationSansTTF_Compressed_compressed_size,
        12.5f * getScaleFactor(), &fc, kChassisRanges);

    // ↓ Font #1: Section titles (always uppercase)
    static constexpr ImWchar kTitleRanges[] = { 'A', 'Z', 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)LiberationSansTTF_Compressed_compressed_data,
        LiberationSansTTF_Compressed_compressed_size,
        14.0f * getScaleFactor(), &fc, kTitleRanges);

    // ↓ Font #2: Large text for scale marks and logo (down-sampled for crispness)
    static constexpr ImWchar kScaleMarkRanges[] = {
        'A', 'Z', 'a', 'z', '0', '9',
        ' ', ' ' + 1,
        '+', ':',
        8734, 8734 + 1,   // ∞
        198, 198 + 1,     // Æ (for KJÆRHUS)
        0
    };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)LiberationSansTTF_Compressed_compressed_data,
        LiberationSansTTF_Compressed_compressed_size,
        20.0f * getScaleFactor(), &fc, kScaleMarkRanges);

    // ↓ Font #3: Cormorant SemiBold Italic – "Classic Reverb" name text
    static constexpr ImWchar kPluginNameRanges[] = { 'A', 'Z', 'a', 'z', '0', '9', ' ', ' ' + 1, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)CormorantSemiBoldItalicTTF_compressed_data,
        CormorantSemiBoldItalicTTF_compressed_size,
        20.0f * getScaleFactor(), &fc, kPluginNameRanges);

    // ↓ Font #4: DejaVu Sans – ImGui UI text (full charset)
    io.Fonts->AddFontFromMemoryTTF(
        (void*)dpf_resources::dejavusans_ttf,
        dpf_resources::dejavusans_ttf_size,
        14.5f * getScaleFactor(), &fc);

    // ↓ Font #4 (merged): Font Awesome icons merged into DejaVu Sans
    static constexpr ImWchar kFontAwesomeRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    fc.MergeMode = true;
    io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)FontAwesomeTTF_compressed_data,
        FontAwesomeTTF_compressed_size,
        14.5f * getScaleFactor(), &fc, kFontAwesomeRanges);
    fc.MergeMode = false;

    io.Fonts->Build();
    io.FontDefault = io.Fonts->Fonts[4];

    // Use the large font for scale marks so they render crisply when down-sampled
    kScaleMarkStyle.custom_font = io.Fonts->Fonts[2];
}

// -----------------------------------------------------------------------
// Chassis background

void ClassicReverbUI::_drawChassisBackground(float margin, float rounding)
{
    const ImVec2 winPos  = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();

    const ImVec2 panelMin = ImVec2(winPos.x + margin,             winPos.y + margin);
    const ImVec2 panelMax = ImVec2(winPos.x + winSize.x - margin, winPos.y + winSize.y - margin);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Soft drop-shadow (light from upper-left)
    static constexpr int   kShadowLayers = 6;
    static constexpr float kShadowMax    = 9.0f;
    for (int i = kShadowLayers; i >= 1; --i)
    {
        const float frac   = static_cast<float>(i) / kShadowLayers;
        const float offset = kShadowMax * frac;
        const int   alpha  = static_cast<int>(70.0f * (kShadowLayers - i + 1) / kShadowLayers);
        dl->AddRectFilled(
            ImVec2(panelMin.x + offset, panelMin.y + offset),
            ImVec2(panelMax.x + offset, panelMax.y + offset),
            IM_COL32(0, 0, 0, alpha), rounding);
    }

    // Main panel – base colour #ac6848 (slightly different from RE-04, for distinction)
    dl->AddRectFilled(panelMin, panelMax, IM_COL32(0xac, 0x68, 0x48, 0xff), rounding);

    // Subtle top-left highlight edge
    dl->AddRect(panelMin, panelMax, IM_COL32(0xff, 0xe0, 0xb8, 60), rounding, 0, 1.5f);
}

// -----------------------------------------------------------------------
// Kjaerhus Audio logo (clickable → opens About window)

void ClassicReverbUI::_drawKjaerhusLogo(const ImVec2& size)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    // Invisible button acts as the clickable area and reserves layout space
    if (ImGui::InvisibleButton("##Logo_Clickable", size))
        fAboutWindowOpened = true;

    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // "Triangle" shape (Bezier curves giving a concave look)
    {
        const float left_line_length = 40.0f;
        const float triangle_height  = 35.0f;
        const float curve_inset      = 10.0f;

        const ImVec2 p1 = ImVec2(pos.x + 72.0f, pos.y - 1.0f);
        const ImVec2 p2 = ImVec2(p1.x, p1.y + left_line_length);
        const ImVec2 p3 = ImVec2(p1.x + triangle_height, p1.y + (left_line_length * 0.5f));

        const ImVec2 ctrl_top = ImVec2(
            (p1.x + p3.x) * 0.5f, (p1.y + p3.y) * 0.5f + curve_inset);
        const ImVec2 ctrl_bot = ImVec2(
            (p3.x + p2.x) * 0.5f, (p3.y + p2.y) * 0.5f - curve_inset);

        draw_list->PathClear();
        draw_list->PathLineTo(p1);
        draw_list->PathBezierQuadraticCurveTo(ctrl_top, p3);
        draw_list->PathBezierQuadraticCurveTo(ctrl_bot, p2);
        draw_list->PathFillConcave(IM_COL32(255, 255, 255, 60));
    }

    // "KJÆRHUS AUDIO" text
    ImGuiExt::AddTextScaled(draw_list, ImGui::GetIO().Fonts->Fonts[2], 20.0f,
                            ImVec2(pos.x + 10.0f, pos.y + 8.0f),
                            IM_COL32(255, 255, 255, 255),
                            "KJ\xc3\x86RHUS AUDIO", 0.65f, 1.0f);

    // "Recreated by AnClark" badge with semi-transparent background
    {
        const char* info_text      = "Recreated by AnClark";
        constexpr float kFontSz    = 16.0f;
        constexpr float kScaleX    = 0.8f;
        constexpr float kScaleY    = 0.8f;
        constexpr float kPadX      = 8.0f;
        constexpr float kPadY      = 1.0f;
        constexpr float kRounding  = 3.0f;
        constexpr ImU32 kBgColor   = IM_COL32(100, 100, 100, 60);

        ImFont*      font     = ImGui::GetIO().Fonts->Fonts[2];
        const ImVec2 text_pos = ImVec2(pos.x + 10.0f, pos.y + 8.0f + 22.0f);
        const ImVec2 raw_sz   = font->CalcTextSizeA(kFontSz, FLT_MAX, 0.0f, info_text);
        const ImVec2 text_sz  = ImVec2(raw_sz.x * kScaleX, raw_sz.y * kScaleY);

        draw_list->AddRectFilled(
            ImVec2(text_pos.x - kPadX, text_pos.y - kPadY),
            ImVec2(text_pos.x + text_sz.x + kPadX, text_pos.y + text_sz.y + kPadY),
            kBgColor, kRounding);

        ImGuiExt::AddTextScaled(draw_list, font, kFontSz,
                                text_pos, IM_COL32(255, 255, 255, 255),
                                info_text, kScaleX, kScaleY);
    }
}

// -----------------------------------------------------------------------
// Plugin name: "Classic Reverb"  +  RE│03 capsule badge

void ClassicReverbUI::_drawPluginName()
{
    ImGui::BeginGroup();

    // "Classic Reverb" in Cormorant italic
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[3]);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::SameLine();
    ImGui::Text("Classic Reverb");
    ImGui::PopFont();

    ImGui::SameLine();

    // RE│03 capsule badge
    {
        ImDrawList*     dl      = ImGui::GetWindowDrawList();
        ImFont*         font    = ImGui::GetIO().Fonts->Fonts[2];
        constexpr float kFontSz = 12.5f;
        constexpr float kPadX   = 5.0f - 2.0f;
        constexpr float kPadY   = 2.0f;
        constexpr float kRound  = 4.0f;

        const ImVec2 re_sz  = font->CalcTextSizeA(kFontSz, FLT_MAX, 0.0f, "RE");
        const ImVec2 o3_sz  = font->CalcTextSizeA(kFontSz, FLT_MAX, 0.0f, "03");
        const float  height = re_sz.y + kPadY * 2.0f;
        const float  lw     = re_sz.x + kPadX * 2.0f;
        const float  rw     = o3_sz.x + kPadX * 2.0f;

        const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        const ImVec2 p0  = ImVec2(cursor_pos.x, cursor_pos.y + 4.0f);
        const ImVec2 mid = ImVec2(p0.x + lw,      p0.y);
        const ImVec2 p1  = ImVec2(p0.x + lw + rw, p0.y + height);

        // Right half – solid white fill
        dl->AddRectFilled(mid, p1, IM_COL32(255, 255, 255, 200), kRound, ImDrawFlags_RoundCornersRight);

        // Outer border for the whole capsule
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 255), kRound);

        // "RE" – white text on transparent left half
        dl->AddText(font, kFontSz, ImVec2(p0.x + kPadX, p0.y + kPadY), IM_COL32(255, 255, 255, 255), "RE");

        // "03" – black text on white right half
        dl->AddText(font, kFontSz, ImVec2(mid.x + kPadX, p0.y + kPadY), IM_COL32(0, 0, 0, 255), "03");

        // Reserve layout space
        ImGui::InvisibleButton("##Extra_Info", ImVec2(lw + rw, height));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
        {
            ImGui::SetTooltip("\"RE\" stands for Reverse Engineering. Different models of Classic Reverb RE have different timbre.\n"
                "Classic Reverb RE is an open source recreation of the original Kjaerhus Classic Reverb.");
        }
    }

    ImGui::EndGroup();
}

// -----------------------------------------------------------------------
// Knob helper

void ClassicReverbUI::_addKnob(int paramId, const char* label,
                                float v_min, float v_max,
                                const ImGuiKnobs_Mod::KnobScaleMark* marks, uint32_t mark_count,
                                bool isLogarithmic, bool use_pivot, float pivot_value)
{
    constexpr float KNOB_SIZE    = 50.0f;
    constexpr int   DEFAULT_STEP = 10;

    constexpr auto  IMGUIKNOBS_PI = 3.14159265358979323846f;
    constexpr float angle_min     = IMGUIKNOBS_PI * (130.0f / 180.0f);
    constexpr float angle_max     = IMGUIKNOBS_PI * (410.0f / 180.0f);

    ImGuiKnobFlags flags = ImGuiKnobFlags_TitleBottom;
    if (isLogarithmic) flags |= ImGuiKnobFlags_Logarithmic;
    if (use_pivot)     flags |= ImGuiKnobFlags_Pivot;

    // Knob colour
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0x2f + 70, 0x4d + 70, 0x44 + 70, 0xff));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0x2f + 90, 0x4d + 90, 0x44 + 90, 0xff));

    // Switch to chassis font for knob label and scale marks
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);

    if (ImGuiKnobs_Mod::Knob(label, &fParams[paramId], v_min, v_max, 0.0f, "%.2f",
                              ImGuiKnobVariant_Tick, KNOB_SIZE, flags,
                              DEFAULT_STEP, angle_min, angle_max,
                              marks, mark_count, &kScaleMarkStyle, pivot_value))
    {
        setParameterValue((uint32_t)paramId, fParams[paramId]);
        fPresetManager->markModified();
    }

    if (ImGui::IsItemActivated())
        editParameter((uint32_t)paramId, true);

    if (ImGui::IsItemDeactivated())
        editParameter((uint32_t)paramId, false);

    ImGui::PopFont();
    ImGui::PopStyleColor(2);
}

// -----------------------------------------------------------------------
// Section helpers

bool ClassicReverbUI::_BeginSection(const char* title, float width)
{
    ImGui::BeginGroup();

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);  // 14 px section-title font
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(255, 255, 255, 255));
    ImGuiExt::CenteredSeparatorText(title, width);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Gap between title and top scale marks
    ImGui::Dummy(ImVec2(0, 8));

    return true;
}

void ClassicReverbUI::_EndSection()
{
    ImGui::EndGroup();
}

// -----------------------------------------------------------------------
// Mouse cursor synchronisation

void ClassicReverbUI::_UpdateMouseCursor()
{
    ImGuiMouseCursor mouse_cursor =
        ImGui::GetIO().MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();

    if (fLastMouseCursor != mouse_cursor)
    {
        fLastMouseCursor = mouse_cursor;
        switch (mouse_cursor)
        {
        case ImGuiMouseCursor_None:       getWindow().setCursor(MouseCursor::kMouseCursorArrow);           break;
        case ImGuiMouseCursor_Arrow:      getWindow().setCursor(MouseCursor::kMouseCursorArrow);           break;
        case ImGuiMouseCursor_TextInput:  getWindow().setCursor(MouseCursor::kMouseCursorCaret);           break;
        case ImGuiMouseCursor_ResizeAll:  getWindow().setCursor(MouseCursor::kMouseCursorCrosshair);       break;
        case ImGuiMouseCursor_ResizeNS:   getWindow().setCursor(MouseCursor::kMouseCursorUpDown);          break;
        case ImGuiMouseCursor_ResizeEW:   getWindow().setCursor(MouseCursor::kMouseCursorLeftRight);       break;
        case ImGuiMouseCursor_ResizeNESW: getWindow().setCursor(MouseCursor::kMouseCursorUpRightDownLeft); break;
        case ImGuiMouseCursor_ResizeNWSE: getWindow().setCursor(MouseCursor::kMouseCursorUpLeftDownRight); break;
        case ImGuiMouseCursor_Hand:       getWindow().setCursor(MouseCursor::kMouseCursorHand);            break;
        case ImGuiMouseCursor_NotAllowed: getWindow().setCursor(MouseCursor::kMouseCursorNotAllowed);      break;
        default:                          getWindow().setCursor(MouseCursor::kMouseCursorArrow);           break;
        }
    }
}
