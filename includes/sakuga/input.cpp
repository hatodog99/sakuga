#include <sakuga/input.h>

Input::Input(Camera& camera, float& deltaTime, float inputCooldownTime)
    : camera(camera), deltaTime(deltaTime), inputCooldownTime(inputCooldownTime)
{
}

void Input::attach(GLFWwindow* window)
{
    // GLFW callbacks are plain functions; the user pointer is how they find this Input again
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
}

void Input::processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.ProcessKeyboard(DOWN, deltaTime);

    // texture mix, repeating every inputCooldownTime while the key is held
    mixCooldownTimer += deltaTime;
    if (mixCooldownTimer >= inputCooldownTime)
    {
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        {
            mixValue = glm::clamp(mixValue + 0.05f, 0.0f, 1.0f);
            mixCooldownTimer = 0.0f;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        {
            mixValue = glm::clamp(mixValue - 0.05f, 0.0f, 1.0f);
            mixCooldownTimer = 0.0f;
        }
    }
}

void Input::toggleFullscreen(GLFWwindow* window)
{
    if (!isFullscreen)
    {
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else
        glfwSetWindowMonitor(window, NULL, windowedX, windowedY, windowedWidth, windowedHeight, 0);
    isFullscreen = !isFullscreen;
}

// ------------------------------------------------------------------------
void Input::onCursorPos(GLFWwindow* window, double x, double y)
{
    // the first event only records the position, so the camera doesn't jump
    if (firstInput)
    {
        lastX = x;
        lastY = y;
        firstInput = false;
    }

    float xoffset = static_cast<float>(x - lastX);
    float yoffset = static_cast<float>(lastY - y);  // reversed: window y goes top to bottom

    lastX = x;
    lastY = y;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void Input::onScroll(double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void Input::onKey(GLFWwindow* window, int key, int action, int mods)
{
    if (action != GLFW_PRESS) return;

    switch (key)
    {
    case GLFW_KEY_F11: toggleFullscreen(window); break;
    default: break;
    }
}


// ------------------------------------------------------------------------
void Input::cursorPosCallback(GLFWwindow* window, double x, double y)
{
    if (Input* self = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        self->onCursorPos(window, x, y);
}

void Input::scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
    if (Input* self = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        self->onScroll(yoffset);
}

void Input::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods)
{
    if (Input* self = static_cast<Input*>(glfwGetWindowUserPointer(window)))
        self->onKey(window, key, action, mods);
}