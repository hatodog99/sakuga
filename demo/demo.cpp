// TODO: 
// color ramps
// post-processing: bloom
// api integration
// 
// nice to haves:
// perspective shadows

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
float currentFrameTime = 0.0f;

Input input(camera, deltaTime);

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	SCR_WIDTH = width;
	SCR_HEIGHT = height;
	glViewport(0, 0, width, height);
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad()
{
	if (quadVAO == 0)	// initalize if necessary
	{
		float quadVertices[] = {
			// positions			// uv
			-1.0f,  1.0f, 0.0f,		0.0f, 1.0f,		// top left
			-1.0f, -1.0f, 0.0f,		0.0f, 0.0f,		// bottom left
			 1.0f,  1.0f, 0.0f,		1.0f, 1.0f,		// top right
			 1.0f, -1.0f, 0.0f,		1.0f, 0.0f,		// bottom right
		};
		// setup plane VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}

	// render quad
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

// render 1x1 plane in normalized device coords
unsigned int planeVAO = 0;
unsigned int planeVBO;
unsigned int planeEBO;
void renderPlane() {
	if (planeVAO == 0) {	// initalize if necessary

		float vertices[] = {
			//  position           	normal					uv
			1.0f,  1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 3.0f,  // top right
			1.0f, -1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 0.0f,  // bottom right
			-1.0f, -1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 0.0f,  // bottom left
			-1.0f,  1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 3.0f   // top left
		};
		unsigned int indices[] = {  // note that we start from 0!
			0, 3, 1,   // first triangle
			1, 3, 2    // second triangle
		};  
		glGenVertexArrays(1, &planeVAO);
		glGenBuffers(1, &planeVBO);
		glGenBuffers(1, &planeEBO);
		
		glBindVertexArray(planeVAO);
	
		glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeEBO);
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

	// render plane
	glBindVertexArray(planeVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

void drawFloor(Shader& shader) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, glm::vec3(0.0f, -0.8f, 0.0f));
	model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::scale(model, glm::vec3(7.0f, 7.0f, 1.0f));	// 7x7 floor
	shader.setMat4("model", model);
	renderPlane();
}

void drawCharacter(Shader& shader, Model& charModel, const glm::mat4& model) {
	shader.setMat4("model", model);
	charModel.Draw(shader);
}

void drawOutline(Shader& shader, Model& charModel, const glm::mat4& model) {
	shader.setMat4("model", model);
	charModel.Draw(shader);
}

const float charModelScale = 1.0f;
const float floorY = -0.8f;
glm::mat4 getCharacterModelMatrix() {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, glm::vec3(0.0f, floorY, 0.0f));
	model = glm::scale(model, glm::vec3(charModelScale));
	return model;
}

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;
void configureDepthMapFBO() {
	glGenFramebuffers(1, &depthMapFBO);

	// create depth texture
	glGenTextures(1, &depthMap);
	glBindTexture(GL_TEXTURE_2D, depthMap);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

	// attach depth texture to FBO's depth buffer
	glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void renderDepthMap(Shader& shader, Model& charModel) {
	glCullFace(GL_FRONT);
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
	glClear(GL_DEPTH_BUFFER_BIT);
	drawCharacter(shader, charModel, getCharacterModelMatrix());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glCullFace(GL_BACK);
}

unsigned int loadTexture(const std::string& path) {
	unsigned int floorTexture;
	glGenTextures(1, &floorTexture);
	glBindTexture(GL_TEXTURE_2D, floorTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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

int main()
{
	// initialize stuff
	// ------------------------------------------------------------------------
	// GLFW
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_SAMPLES, 4);

	// make & setup window
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

	// GLAD
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "failed to initialize glad" << std::endl;
		return -1;
	}

	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glEnable(GL_MULTISAMPLE);

	// ImGui
	gui::init(window);
	ImGui::GetIO().FontGlobalScale = 1.0f;
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(6, 6);
	style.ItemSpacing = ImVec2(6, 3);
	style.FramePadding = ImVec2(4, 2);
	 
	// fps display
	float fpsTimer = 0.0f;
	int fpsFrames = 0;
	float displayedFPS = 0.0f;
	float displayedMs = 0.0f;

	// shaders
	Shader shader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Shader outlineShader("resources/shaders/outline.vs.glsl", "resources/shaders/outline.fs.glsl");

	Shader depthShader("resources/shaders/depth.vs.glsl", "resources/shaders/depth.fs.glsl");
	Shader debugDepthQuadShader("resources/shaders/debug_depth_quad.vs.glsl", "resources/shaders/debug_depth_quad.fs.glsl");

	Model charModel("resources/objects/2B/2B-1.glb");
	//Model charModel("resources/objects/ijichi-nijika/1.fbx");

	// shader uniforms
	DirLight dirLight;
	ShadowMap shadowMap;
	Material material;
	Outline outline;

	lastFrameTime = static_cast<float>(glfwGetTime());	// avoid first huge deltaTime

	unsigned int floorTexture = loadTexture("resources/textures/concrete/Concrete047A_1K-JPG_Color.jpg");	
	configureDepthMapFBO();

	shader.use();
	shader.setInt("diffuseTexture", 0);
	shader.setInt("shadowMap", 1);

	// for depth (shadow) map debugging
	debugDepthQuadShader.use();
	debugDepthQuadShader.setInt("depthMap", 0);

	// render stuff
	// ------------------------------------------------------------------------
	while (!glfwWindowShouldClose(window))
	{
		float currentFrameTime = static_cast<float>(glfwGetTime());
		deltaTime = currentFrameTime - lastFrameTime;
		lastFrameTime = currentFrameTime;

		fpsTimer += deltaTime;
		++fpsFrames;
		if (fpsTimer >= 0.5f)
		{
			displayedMs = (fpsTimer / fpsFrames) * 1000.0f;
			displayedFPS = fpsFrames / fpsTimer;
			fpsTimer = 0.0f;
			fpsFrames = 0;
		}

		input.processInput(window);

		// ui (built first, so edits apply to this frame)
		float sliderWidth = 150.0f;
		gui::setMouseEnabled(!input.mouseLook);
		gui::beginFrame();
		ImGui::SetNextWindowBgAlpha(0.75f);

		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("%.1f FPS (%.2f ms)", displayedFPS, displayedMs);

		ImGui::Separator();
		// ligthing
		ImGui::Checkbox("lighting", &input.lighting);
		ImGui::Checkbox("cell shading", &input.cellShading);
		ImGui::Checkbox("rim lighting", &input.rimLighting);
		ImGui::PushItemWidth(sliderWidth);
		ImGui::Checkbox("move light", &input.lightMovement);
		ImGui::SliderFloat("x##light", &dirLight.direction.x, -2.0f, 2.0f);
		ImGui::SliderFloat("y##light", &dirLight.direction.y, -2.0f, 2.0f);
		ImGui::SliderFloat("z##light", &dirLight.direction.z, -2.0f, 2.0f);

		ImGui::Separator();

		ImGui::Text("light intensity");
		ImGui::SliderFloat("ambient", &dirLight.ambientIntensity, 0.0f, 2.0f);
		ImGui::SliderFloat("diffuse", &dirLight.diffuseIntensity, 0.0f, 2.0f);
		// disbled when cell shading
		// --------------------------------------------------------------------
		ImGui::BeginDisabled(input.cellShading);
		ImGui::SliderFloat("specular", &dirLight.specularIntensity, 0.0f, 2.0f);
		ImGui::PopItemWidth();

		ImGui::Separator();
		// shininess
		ImGui::Text("material shininess");
		ImGui::PushItemWidth(sliderWidth);
		ImGui::SliderFloat("##material", &material.shininess, 1.0f, 128.0f);
		ImGui::PopItemWidth();
		ImGui::EndDisabled();
		// --------------------------------------------------------------------

		ImGui::Separator();
		// outline
		ImGui::Checkbox("outline", &input.outline);
		ImGui::PushItemWidth(sliderWidth);
		ImGui::SliderFloat("width", &outline.width, 0.0f, 0.01f, "%.4f");
		ImGui::SliderFloat("depth bias", &outline.depthBias, 0.0f, 0.05f, "%.4f");
		ImGui::PopItemWidth();

		ImGui::Separator();
		// shadow
		ImGui::Checkbox("shadow", &input.shadowMapping);
		ImGui::Text("direction");
		ImGui::PushItemWidth(sliderWidth);
		ImGui::SliderFloat("x##shadow", &shadowMap.lightDir.x, -1.0f, 1.0f);
		ImGui::SliderFloat("y (steepness)##shadow", &shadowMap.lightDir.y, -2.0f, 0.0f);
		ImGui::SliderFloat("z##shadow", &shadowMap.lightDir.z, -1.0f, 1.0f);
		ImGui::PopItemWidth();

		ImGui::Separator();
		// background color
		ImGui::Checkbox("toggle background color", &input.toggleBackground);

		ImGui::End();

		// background
		if (input.toggleBackground)
			glClearColor(0.10f, 0.10f, 0.10f, 1.0f);	// dark
		else
			glClearColor(0.90f, 0.90f, 0.90f, 1.0f);	// light

		// clear buffers for every frame
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// render depth of scene to texture (from light's POV) in orthographic view
		float lightNearPlane = 1.0f, lightFarPlane = 20.0f;
		float lightDisFromOrig = 7.0f;
		glm::vec3 lightPos = -glm::normalize(shadowMap.lightDir) * lightDisFromOrig;
		glm::vec3 up = (fabs(shadowMap.lightDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

		glm::mat4 lightProjection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, lightNearPlane, lightFarPlane);
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
		glm::mat4 lightSpaceMatrix = lightProjection * lightView;

		// depth map (used for rendering shadows)
		depthShader.use();
		depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
		renderDepthMap(depthShader, charModel);

		// reset viewport
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// view/projection transformations
		float aspect = (SCR_HEIGHT > 0) ? (float)SCR_WIDTH / (float)SCR_HEIGHT : 1.0f;
		float nearPlane = 0.1f;
		float farPlane = 100.0f;

		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, nearPlane, farPlane);
		glm::mat4 view = camera.GetViewMatrix();
		
		// rendering
		// --------------------------------------------------------------------
		// character
		shader.use();	
		shader_config::applyCamera(shader, projection, view, camera.Position);
		shader_config::applyLightingToggles(shader, input.lighting, input.cellShading, input.rimLighting, input.shadowMapping);
		shader_config::applyMaterial(shader, material);
		shader_config::applyDirLight(shader, dirLight, colors::whiteLight);
		shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
		shader.setBool("gReceiveShadowMapping", false);
		shader.setBool("gReceiveRimLighting", true);
		drawCharacter(shader, charModel, getCharacterModelMatrix());

		// floor
		shader.setBool("gReceiveShadowMapping", true);
		shader.setBool("gReceiveRimLighting", false);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, floorTexture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, depthMap);
		glActiveTexture(GL_TEXTURE0);
		drawFloor(shader);

		// outline
		if (input.outline)
		{
			glCullFace(GL_FRONT);

			glm::vec3 outlineColor = input.toggleBackground ? colors::whiteOutline : colors::blackOutline;

			outlineShader.use();
			shader_config::applyOutline(outlineShader, outline, outlineColor, projection, view);
			outlineShader.setFloat("aspect", aspect);
			drawOutline(outlineShader, charModel, getCharacterModelMatrix());

			glCullFace(GL_BACK);
		}

		// render depth map to quad for visual shadow debugging
		//debugDepthQuadShader.use();
		//debugDepthQuadShader.setFloat("near_plane", lightNearPlane);
		//debugDepthQuadShader.setFloat("far_plane", lightFarPlane);
		//glActiveTexture(GL_TEXTURE0);
		//glBindTexture(GL_TEXTURE_2D, depthMap);
		//renderQuad();

		// light movement
		if (input.lightMovement)
		{
			float speed = 0.7f;
			float amplitude = 0.8f;
			dirLight.direction.x = std::sin(currentFrameTime * speed) * amplitude;
			dirLight.direction.z = std::cos(currentFrameTime * speed) * amplitude;

			//shadowMap.lightDir.x = std::sin(currentFrameTime * speed);
			//shadowMap.lightDir.z = std::cos(currentFrameTime * speed);
		}

		// ui is drawn last so it sits on top of the scene
		gui::endFrame();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	gui::shutdown();

	glDeleteVertexArrays(1, &planeVAO);
	glDeleteBuffers(1, &planeVBO);
	glDeleteBuffers(1, &planeEBO);

	glfwTerminate();
	return 0;
}