#include <sakuga/gui.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // OpenPopupStack / ClosePopupToLevel (no public "close all popups")
#include <imgui/backend/imgui_impl_glfw.h>
#include <imgui/backend/imgui_impl_opengl3.h>

#include <cfloat>
#include <cmath>

namespace
{
    ImU32 toU32(const glm::vec3& c)
    {
        auto ch = [](float v) { return (int)std::lround(glm::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return IM_COL32(ch(c.r), ch(c.g), ch(c.b), 255);
    }

    // same mapping the shader uses: t = 0 shadow, 0.5 midtone, 1 highlight
    glm::vec3 bandColor(const glm::vec3& shadow, const glm::vec3& mid, const glm::vec3& light, float t)
    {
        return t < 0.5f ? glm::mix(shadow, mid, t * 2.0f)
            : glm::mix(mid, light, (t - 0.5f) * 2.0f);
    }
}

namespace gui
{
    static float gScale = 1.0f;

    void setScale(float s)
    {
        static ImGuiStyle baseStyle;
        static bool haveBase = false;
        static float applied = 0.0f;

        if (!haveBase)
        {
            baseStyle = ImGui::GetStyle();   // unscaled style (includes any custom tweaks)
            haveBase = true;
        }
        if (s == applied)
            return;

        ImGuiStyle& style = ImGui::GetStyle();
        style = baseStyle;
        style.ScaleAllSizes(s);
        ImGui::GetIO().FontGlobalScale = s;
        gScale = s;
        applied = s;
    }

    float scale() { return gScale; }

    void init(GLFWwindow* window)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();   // default ImGui look, no custom theme

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;   // panels are laid out in code, no saved positions

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
    }

    void beginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void endFrame()
    {
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void setMouseEnabled(bool enabled)
    {
        static bool wasEnabled = true;

        ImGuiIO& io = ImGui::GetIO();
        if (enabled)
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

        // the mouse was just captured (Tab): popups like the color picker can no longer be
        // clicked away, so close them instead of leaving them stuck open
        if (wasEnabled && !enabled)
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            if (g.OpenPopupStack.Size > 0)
                ImGui::ClosePopupToLevel(0, true);
        }
        wasEnabled = enabled;
    }

    // ---- panel layout -------------------------------------------------------

    PanelStack leftColumn(int index)
    {
        PanelStack s;
        s.right = false;
        s.y *= gScale; s.width *= gScale; s.margin *= gScale; s.gap *= gScale;
        s.xShift = index * (s.width + s.margin);
        return s;
    }

    PanelStack rightColumn(int index)
    {
        PanelStack s;
        s.right = true;
        s.y *= gScale; s.width *= gScale; s.margin *= gScale; s.gap *= gScale;
        s.xShift = index * (s.width + s.margin);
        return s;
    }

    bool beginPanel(PanelStack& stack, const char* title, bool startCollapsed)
    {
        ImGuiIO& io = ImGui::GetIO();
        float x = stack.right ? io.DisplaySize.x - stack.width - stack.margin - stack.xShift
            : stack.margin + stack.xShift;

        ImGui::SetNextWindowPos(ImVec2(x, stack.y), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(stack.width, 0.0f), ImVec2(stack.width, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::SetNextWindowCollapsed(startCollapsed, ImGuiCond_FirstUseEver);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoNav |             // so hitting TAB doesn't affect the gui
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;
        return ImGui::Begin(title, nullptr, flags);
    }

    void endPanel(PanelStack& stack)
    {
        stack.y += ImGui::GetWindowHeight() + stack.gap;
        ImGui::End();
    }

    // ---- widgets ------------------------------------------------------------

    void sectionLabel(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", text);
    }

    bool colorRow(const char* label, glm::vec3& color)
    {
        ImGui::PushID(label);
        bool changed = false;

        int v[3] = {
            (int)std::lround(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
            (int)std::lround(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
            (int)std::lround(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f)
        };
        const char* names[3] = { "R", "G", "B" };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(names[i]);
            ImGui::SameLine(0.0f, 3.0f * gScale);
            ImGui::SetNextItemWidth(34.0f * gScale);
            ImGui::PushID(i);
            if (ImGui::InputInt("##v", &v[i], 0, 0))
            {
                v[i] = v[i] < 0 ? 0 : (v[i] > 255 ? 255 : v[i]);
                color[i] = v[i] / 255.0f;
                changed = true;
            }
            ImGui::PopID();
        }

        ImGui::SameLine();
        if (ImGui::ColorButton("##swatch", ImVec4(color.r, color.g, color.b, 1.0f),
            ImGuiColorEditFlags_NoTooltip, ImVec2(20.0f * gScale, 20.0f * gScale)))
            ImGui::OpenPopup("picker");

        if (ImGui::BeginPopup("picker"))
        {
            if (ImGui::ColorPicker3("##picker", &color.x, ImGuiColorEditFlags_NoSidePreview))
                changed = true;
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return changed;
    }

    void rampStrip(const glm::vec3& shadow, const glm::vec3& mid, const glm::vec3& light,
        int levels, float softness, float lowT, float highT)
    {
        const float h = 20.0f * scale();    // drop "* scale()" if your gui.cpp has no scale helper
        int n = levels < 2 ? 2 : (levels > 8 ? 8 : levels);
        float s = softness > 0.0001f ? softness : 0.0001f;

        float w = ImGui::GetContentRegionAvail().x;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // one column per pixel, same math as RampColor() in light.fs.glsl
        int cols = (int)w;
        for (int c = 0; c < cols; ++c)
        {
            float ndl = (c + 0.5f) / (float)cols;

            float t = 0.0f;
            for (int i = 0; i < n - 1; ++i)
            {
                float k = (n > 2) ? (float)i / (float)(n - 2) : 0.5f;
                float edge = lowT + (highT - lowT) * k;
                float x = glm::clamp((ndl - (edge - s)) / (2.0f * s), 0.0f, 1.0f);
                t += x * x * (3.0f - 2.0f * x);    // smoothstep
            }
            t /= (float)(n - 1);

            dl->AddRectFilled(ImVec2(p.x + c, p.y), ImVec2(p.x + c + 1.0f, p.y + h),
                toU32(bandColor(shadow, mid, light, t)));
        }
        ImGui::Dummy(ImVec2(w, h));
    }

    void fpsGraph(const float* values, int count, int offset)
    {
        ImGui::PlotLines("##fps", values, count, offset, nullptr, 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f * gScale));
    }

    void controlsBar()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 10.0f * gScale),
            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.6f);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##controls", nullptr, flags))
            ImGui::TextUnformatted("Tab: free mouse   WASD: move   Space/Shift: up/down   Scroll: zoom");
        ImGui::End();
    }
}