#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <sakuga/camera.h>
#include <sakuga/input.h>
#include <sakuga/model.h>
#include <sakuga/shader.h>
#include <sakuga/shader_config.h>

#include <iostream>
#include <string>

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
	glEnable(GL_STENCIL_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);	// write the reference value when stencil and depth pass

	Shader lightShader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Shader outlineShader("resources/shaders/outline.vs.glsl", "resources/shaders/outline.fs.glsl");

	Model twoB("resources/objects/2B/2B-no-skirt-1.glb");

	// shader unforms
	DirLight dirLight;
	Material material;
	const float modelScale = 1.0f;
	const float outlineWidth = 0.002f;	// world units: outline.vs.glsl extrudes along the normal in world space

	// render loop
	while (!glfwWindowShouldClose(window))
	{
		float currentFrameTime = static_cast<float>(glfwGetTime());
		deltaTime = currentFrameTime - lastFrameTime;
		lastFrameTime = currentFrameTime;

		input.processInput(window);

		// background
		if (input.toggleBackground)
			glClearColor(0.05f, 0.05f, 0.05f, 1.0f);	// dark
		else
			glClearColor(0.95f, 0.95f, 0.95f, 1.0f);	// light

		glStencilMask(0xFF);	// the stencil mask must allow writes or the clear is ignored
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		// view/projection transformations
		float aspect = (SCR_HEIGHT > 0) ? (float)SCR_WIDTH / (float)SCR_HEIGHT : 1.0f;
		float nearPlane = 0.1f;
		float farPlane = 100.0f;
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, nearPlane, farPlane);
		glm::mat4 view = camera.GetViewMatrix();

		// moving light
		if (input.lightMovement)
		{
			float speed = 0.7f;
			float amplitude = 0.8f;
			dirLight.direction.x = std::sin(currentFrameTime * speed) * amplitude;
		}

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -0.8f, 0.0f));
		model = glm::scale(model, glm::vec3(modelScale));

		// pass 1: render the model and write 1s into the stencil buffer
		glStencilFunc(GL_ALWAYS, 1, 0xFF);
		glStencilMask(0xFF);

		lightShader.use();
		shader_config::applyCamera(lightShader, projection, view, camera.Position);
		shader_config::applyLightingToggles(lightShader, input.lighting, input.cellShading, input.rimLighting);
		shader_config::applyMaterial(lightShader, material);
		shader_config::applyDirLight(lightShader, dirLight, colors::whiteLight);
		lightShader.setMat4("model", model);
		twoB.Draw(lightShader);

		// pass 2: render the extruded model only where the stencil is not 1
		if (input.outline)
		{
			glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
			glStencilMask(0x00);	// don't modify the stencil buffer
			// depth test stays  so the outline hides behind other objects

			glm::vec3 outlineColor = input.toggleBackground ? colors::whiteOutline : colors::darkBrownOutline;

			outlineShader.use();
			shader_config::applyOutline(outlineShader, outlineColor, outlineWidth, projection, view);
			outlineShader.setMat4("model", model);
			twoB.Draw(outlineShader);

			// reset state
			glStencilMask(0xFF);
			glStencilFunc(GL_ALWAYS, 0, 0xFF);
		}

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