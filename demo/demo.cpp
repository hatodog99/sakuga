#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui/imgui.h>

#include <sakuga/camera.h>
#include <sakuga/gui.h>
#include <sakuga/input.h>
#include <sakuga/model.h>
#include <sakuga/shader.h>
#include <sakuga/shader_config.h>

#include <cmath>
#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// screen
unsigned int SCR_WIDTH = 1024;
unsigned int SCR_HEIGHT = 576;

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
	input.attach(window);	// cursor and scroll callbacks (must come before gui::init)

	glfwSwapInterval(0);	// disable vsync for accurate fps measurement

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "failed to initialize glad" << std::endl;
		return -1;
	}

	gui::init(window);
	 
	// fps display
	float fpsTimer = 0.0f;
	int fpsFrames = 0;
	float displayedFPS = 0.0f;
	float displayedMs = 0.0f;

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);	// write the reference value when stencil and depth pass

	glEnable(GL_CULL_FACE);

	Shader lightShader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Shader outlineShader("resources/shaders/outline.vs.glsl", "resources/shaders/outline.fs.glsl");

	Model twoB("resources/objects/2B/2B-no-skirt-1.glb");

	// shader unforms
	DirLight dirLight;
	Material material;
	Outline outline;

	const float modelScale = 1.0f;

	lastFrameTime = static_cast<float>(glfwGetTime());	// avoid first huge deltaTime

	// render loop
	while (!glfwWindowShouldClose(window))
	{
		float currentFrameTime = static_cast<float>(glfwGetTime());
		deltaTime = currentFrameTime - lastFrameTime;
		lastFrameTime = currentFrameTime;

		input.processInput(window);

		// ui (built first, so edits apply to this frame)
		gui::setMouseEnabled(!input.mouseLook);
		gui::beginFrame();

		fpsTimer += deltaTime;
		++fpsFrames;
		if (fpsTimer >= 0.5f)
		{
			displayedMs = (fpsTimer / fpsFrames) * 1000.0f;
			displayedFPS = fpsFrames / fpsTimer;
			fpsTimer = 0.0f;
			fpsFrames = 0.0f;
		}

		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoNav);	// so hitting TAB doesn't affect the gui
		ImGui::Text("%.1f FPS (%.2f ms)", displayedFPS, displayedMs);
		ImGui::Separator();
		ImGui::Checkbox("Lighting", &input.lighting);
		ImGui::Checkbox("Cell shading", &input.cellShading);
		ImGui::Checkbox("Rim lighting", &input.rimLighting);

		ImGui::Checkbox("Outline", &input.outline);
		ImGui::SameLine();
		ImGui::SliderFloat("##outlineWidth", &outline.width, 0.0f, 0.01f, "%.4f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Light movement", &input.lightMovement);
		ImGui::Checkbox("Dark background", &input.toggleBackground);
		ImGui::Separator();
		ImGui::SliderFloat3("Light direction", &dirLight.direction.x, -2.0f, 2.0f);
		ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 128.0f);
		ImGui::End();

		// background
		if (input.toggleBackground)
			glClearColor(0.10f, 0.10f, 0.10f, 1.0f);	// dark
		else
			glClearColor(0.90f, 0.90f, 0.90f, 1.0f);	// light

		glStencilMask(0xFF);	// the stencil mask must allow writes or the clear is ignored
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		// view/projection transformations
		float aspect = (SCR_HEIGHT > 0) ? (float)SCR_WIDTH / (float)SCR_HEIGHT : 1.0f;
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, 0.1f, 100.0f);
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
			// depth test stays enabled so the outline hides behind other objects

			glm::vec3 outlineColor = input.toggleBackground ? colors::whiteOutline : colors::darkBrownOutline;

			outlineShader.use();
			shader_config::applyOutline(outlineShader, outline, outlineColor, projection, view);
			outlineShader.setMat4("model", model);
			twoB.Draw(outlineShader);

			// reset state
			glStencilMask(0xFF);
			glStencilFunc(GL_ALWAYS, 0, 0xFF);
		}

		// ui is drawn last so it sits on top of the scene
		gui::endFrame();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	gui::shutdown();	// before glfwTerminate

	glfwTerminate();
	return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	SCR_WIDTH = width;
	SCR_HEIGHT = height;
	glViewport(0, 0, width, height);
}