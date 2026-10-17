#ifndef GUI_H
#define GUI_H

struct GLFWwindow;

namespace gui
{
    void init(GLFWwindow* window);
    void beginFrame();
    void endFrame();
    void shutdown();

    void setMouseEnabled(bool enabled);
}

#endif