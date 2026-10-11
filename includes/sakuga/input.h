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

    void processInput(GLFWwindow* window);

private:
    Camera& camera;        // references: operate on main's real objects
    float& deltaTime;
    float inputCooldownTime;
    float mixCooldownTimer = 0.0f;

    bool isFullscreen = false;
    int windowedX = 0, windowedY = 0, windowedWidth = 0, windowedHeight = 0;

    void toggleFullscreen(GLFWwindow* window);
};

#endif