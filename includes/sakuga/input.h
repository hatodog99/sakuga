#ifndef INPUT_H
#define INPUT_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <sakuga/camera.h>

#include <iostream>
#include <string>

class Input {
public:
    // toggles that main.cpp reads
    bool lightingEnabled = true;
    bool lightMovementEnabled = false;
    bool cellShadingEnabled = true;
    bool rimLightingEnabled = true;
    bool outlineEnabled = true;
    bool toggleBackground = true;
    float mixValue = 0.3f;

    Input(Camera& camera, float& deltaTime, float inputCooldownTime = 0.05f);

    void attach(GLFWwindow* window);    // registers the mouse and scroll callbacks
    void processInput(GLFWwindow* window);

private:
    Camera& camera;        // references: operate on main's real objects
    float& deltaTime;
    float inputCooldownTime;
    float mixCooldownTimer = 0.0f;

    bool isFullscreen = false;
    int windowedX = 0, windowedY = 0, windowedWidth = 0, windowedHeight = 0;

    bool firstInput = true;
    double lastX = 0.0;
    double lastY = 0.0;

    void toggleFullscreen(GLFWwindow* window);

    void onCursorPos(GLFWwindow* window, double x, double y);
    void onScroll(double yoffset);

    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

#endif