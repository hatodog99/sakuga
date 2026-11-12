#ifndef GUI_H
#define GUI_H

#include <glm/glm.hpp>

struct GLFWwindow;

namespace gui
{
    void init(GLFWwindow* window);
    void beginFrame();
    void endFrame();
    void shutdown();

    void setMouseEnabled(bool enabled);

    // Scales fonts, padding, panel widths and fixed-size widgets. 1.0 = the base layout.
    // Call before beginFrame(); the style is captured on the first call, so set any custom
    // ImGuiStyle values before then.
    void setScale(float scale);
    float scale();

    // ---- panel layout -----------------------------------------------------
    // Panels are stacked top to bottom in a column pinned to the left or right
    // edge of the window. index 1 puts the column next to the first one.
    //
    //   gui::PanelStack left = gui::leftColumn();
    //   if (gui::beginPanel(left, "Performance")) { ...widgets... }
    //   gui::endPanel(left);          // always call, even when collapsed
    struct PanelStack
    {
        bool right = false;
        float xShift = 0.0f;
        float y = 10.0f;
        float width = 280.0f;
        float margin = 10.0f;
        float gap = 8.0f;
    };
    PanelStack leftColumn(int index = 0);
    PanelStack rightColumn(int index = 0);
    bool beginPanel(PanelStack& stack, const char* title, bool startCollapsed = false);
    void endPanel(PanelStack& stack);

    // ---- widgets ----------------------------------------------------------
    void sectionLabel(const char* text);                        // small heading
    bool colorRow(const char* label, glm::vec3& color);         // R G B (0-255) fields + swatch/picker
    void rampStrip(const glm::vec3& shadow, const glm::vec3& mid, const glm::vec3& light,
                   int levels);                                 // preview of the toon color bands
    void fpsGraph(const float* values, int count, int offset);  // small line graph (ring buffer)
    void controlsBar();                                         // key hints along the bottom
}

#endif