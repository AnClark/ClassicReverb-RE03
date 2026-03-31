#ifndef CLASSIC_REVERB_UI_H
#define CLASSIC_REVERB_UI_H

#include "DistrhoUI.hpp"
#include "Defines.h"   // For Parameters enum / PARAM_COUNT

// Forward decls.
namespace ImGuiKnobs_Mod {
    struct KnobScaleMark;
}

// -----------------------------------------------------------------------

class ClassicReverbUI : public DISTRHO::UI
{
public:
    ClassicReverbUI();

protected:
    // -------------------------------------------------------------------
    // DSP Callbacks

    void parameterChanged(uint32_t index, float value) override;

    // -------------------------------------------------------------------
    // ImGui Callbacks

    void onImGuiDisplay() override;


private:
    // -------------------------------------------------------------------
    // Local variables

    float fParams[PARAM_COUNT];

    bool fAboutWindowOpened = false;  // Flag to track if the "About" window is open
    int  fLastMouseCursor   = -1;     // Track last mouse cursor state for optimization

    // -------------------------------------------------------------------
    // Internal procedures

    void _loadFonts();  // Load ImGui fonts (invoked in constructor)
    void _drawChassisBackground(float margin, float rounding); // Draw plugin chassis background
    void _drawKjaerhusLogo(const ImVec2& size);
    void _drawPluginName();

    // Helper: add a knob with explicit min/max
    void _addKnob(int paramId, const char* label, float v_min, float v_max,
                  const ImGuiKnobs_Mod::KnobScaleMark* marks, uint32_t mark_count,
                  bool isLogarithmic = false, bool use_pivot = false, float pivot_value = 0.0f);

    bool _BeginSection(const char* title, float width); // Begin a section with centered title
    void _EndSection();                                  // End a section started with _BeginSection
    void _UpdateMouseCursor();                           // Update OS cursor from ImGui cursor state
};

#endif // CLASSIC_REVERB_UI_H
