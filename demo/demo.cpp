#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <stb/stb_image.h>

#include <sakuga/camera.h>
#include <sakuga/gui.h>
#include <sakuga/input.h>
#include <sakuga/model.h>
#include <sakuga/shader.h>
#include <sakuga/shader_config.h>

#include <cmath>
#include <iostream>

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

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	SCR_WIDTH = width;
	SCR_HEIGHT = height;
	glViewport(0, 0, width, height);
}

unsigned int floorVAO = 0;
unsigned int floorVBO;
unsigned int floorEBO;
void loadFloor() {
	if (floorVAO == 0) {

		float vertices[] = {
			//  position           	normal					uv
			0.5f,  0.5f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 3.0f,  // top right
			0.5f, -0.5f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 0.0f,  // bottom right
			-0.5f, -0.5f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 0.0f,  // bottom left
			-0.5f,  0.5f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 3.0f   // top left
		};
		unsigned int indices[] = {  // note that we start from 0!
			0, 3, 1,   // first triangle
			1, 3, 2    // second triangle
		};  
		glGenVertexArrays(1, &floorVAO);
		glGenBuffers(1, &floorVBO);
		glGenBuffers(1, &floorEBO);
		
		glBindVertexArray(floorVAO);
	
		glBindBuffer(GL_ARRAY_BUFFER, floorVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, floorEBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(2);
	
		glBindBuffer(GL_ARRAY_BUFFER, 0); 
	
		glBindVertexArray(0);
	}
}

unsigned int loadTexture(const std::string& path) {
	unsigned int floorTexture;
	glGenTextures(1, &floorTexture);
	glBindTexture(GL_TEXTURE_2D, floorTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	int width, height, nrComponents;
	unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrComponents, 4);
	if (data) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	} else {
		std::cout << "failed to load texture: " << path << std::endl;
	}
	stbi_image_free(data);
	return floorTexture;
}

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 1024, SHADOW_HEIGHT = 1024;
void configureDepthMapFBO() {
	glGenFramebuffers(1, &depthMapFBO);
	glGenTextures(1, &depthMap);
	glBindTexture(GL_TEXTURE_2D, depthMap);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_REPEAT);

	glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindBuffer(GL_FRAMEBUFFER, 0);
}

void generateDepthMap(unsigned int texture) {
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
		// glClear(GL_DEPTH_BUFFER_BIT);
		// glActiveTexture(GL_TEXTURE0);
		// glBindTexture(GL_TEXTURE_2D, texture);
		// renderScene(simpleDepthBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_SAMPLES, 4);

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

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);	// write the reference value when stencil and depth pass

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glEnable(GL_MULTISAMPLE);

	gui::init(window);
	 
	// fps display
	float fpsTimer = 0.0f;
	int fpsFrames = 0;
	float displayedFPS = 0.0f;
	float displayedMs = 0.0f;

	Shader lightShader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Shader outlineShader("resources/shaders/outline.vs.glsl", "resources/shaders/outline.fs.glsl");
	Shader floorShader("resources/shaders/floor.vs.glsl", "resources/shaders/floor.fs.glsl");

	Model object("resources/objects/2B/2B-no-skirt-1.glb");
	//Model object("resources/objects/ijichi-nijika/1.fbx");

	// shader unforms
	DirLight dirLight;
	Material material;
	Outline outline;

	const float modelScale = 1.0f;

	lastFrameTime = static_cast<float>(glfwGetTime());	// avoid first huge deltaTime

	loadFloor();
	unsigned int floorTexture = loadTexture("resources/textures/concrete/Concrete047A_1K-JPG_Color.jpg");	
	configureDepthMapFBO();

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
			fpsFrames = 0;
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
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::SliderFloat2("##outline", &outline.width, 0.0f, 0.05f, "%.4f");

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

		lightShader.use();
		shader_config::applyCamera(lightShader, projection, view, camera.Position);
		shader_config::applyLightingToggles(lightShader, input.lighting, input.cellShading, input.rimLighting);
		shader_config::applyMaterial(lightShader, material);
		shader_config::applyDirLight(lightShader, dirLight, colors::whiteLight);
		lightShader.setMat4("model", model);
		object.Draw(lightShader);

		if (input.outline)
		{
			glCullFace(GL_FRONT);

			glm::vec3 outlineColor = input.toggleBackground ? colors::whiteOutline : colors::blackOutline;

			outlineShader.use();
			shader_config::applyOutline(outlineShader, outline, outlineColor, projection, view);
			outlineShader.setMat4("model", model);
			outlineShader.setFloat("aspect", (float)SCR_WIDTH / (float)SCR_HEIGHT);
			object.Draw(outlineShader);

			glCullFace(GL_BACK);
		}

		floorShader.use();
		floorShader.setInt("textureID", 0);   // sampler reads texture unit 0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, floorTexture);

		floorShader.setMat4("projection", projection);
		floorShader.setMat4("view", view);

		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -0.8f, 0.0f));
		model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		model = glm::scale(model, glm::vec3(10.0f, 10.0f, 1.0f));	// 10x10 floor

		shader_config::applyCamera(lightShader, projection, view, camera.Position);
		shader_config::applyLightingToggles(lightShader, input.lighting, input.cellShading, input.rimLighting);
		shader_config::applyMaterial(lightShader, material);
		shader_config::applyDirLight(lightShader, dirLight, colors::whiteLight);

		floorShader.setMat4("model", model);
		
		glBindVertexArray(floorVAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		// generateDepthMap(floorTexture);

		// ui is drawn last so it sits on top of the scene
		gui::endFrame();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	gui::shutdown();	// before glfwTerminate

	glfwTerminate();
	return 0;
}