#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <sakuga/camera.h>
#include <sakuga/input.h>
#include <sakuga/model.h>
#include <sakuga/shader.h>

#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void updatePerformanceCounter(GLFWwindow* window);

// screen
unsigned int SCR_WIDTH = 853;
unsigned int SCR_HEIGHT = 480;

// camera (driven by Input: WASD, mouse look, scroll zoom)
glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
Camera camera(cameraPos);

// frame times
float deltaTime = 0.0f;
float lastFrameTime = 0.0f;

Input input(camera, deltaTime);	

int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "demo", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "failed to create glfw window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	input.attach(window);	// cursor and scroll callbacks

	glfwSwapInterval(0);	// disable vsync accurate fps measurement

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "failed to initialize glad" << std::endl;
		return -1;
	}

	glEnable(GL_DEPTH_TEST);

	Shader lightShader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Model twoB("resources/objects/2B/2B-no-skirt-1.glb");

	while (!glfwWindowShouldClose(window))
	{
		float currentFrameTime = static_cast<float>(glfwGetTime());
		deltaTime = currentFrameTime - lastFrameTime;
		lastFrameTime = currentFrameTime;

		input.processInput(window);

		if (input.toggleBackground)
			glClearColor(0.05f, 0.05f, 0.05f, 1.0f);	// dark
		else
			glClearColor(0.95f, 0.95f, 0.95f, 1.0f);	// light
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		lightShader.use();

		float aspect = (SCR_HEIGHT > 0) ? (float)SCR_WIDTH / (float)SCR_HEIGHT : 1.0f;
		float zNear = 0.1f;
		float zFar = 100.0f;
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, zNear, zFar);
		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -0.8f, 0.0f));

		lightShader.setMat4("projection", projection);
		lightShader.setMat4("view", view);
		lightShader.setMat4("model", model);
		twoB.Draw(lightShader);

		glfwSwapBuffers(window);
		glfwPollEvents();

		updatePerformanceCounter(window);
	}

	glfwTerminate();
	return 0;
}

void updatePerformanceCounter(GLFWwindow* window)
{
	static double lastTime = glfwGetTime();
	static int frameCount = 0;

	double currentTime = glfwGetTime();
	frameCount++;

	if (currentTime - lastTime >= 1.0)
	{
		float fps = (float)frameCount / (currentTime - lastTime);
		float msPerFrame = (currentTime - lastTime) * 1000.0f / (float)frameCount;

		std::string title = "demo | fps: " + std::to_string((int)fps) + " | ms/frame: " + std::to_string(msPerFrame);
		glfwSetWindowTitle(window, title.c_str());

		frameCount = 0;
		lastTime = currentTime;
	}
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	SCR_WIDTH = width;
	SCR_HEIGHT = height;
	glViewport(0, 0, width, height);
}