// TODO: 
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

#include <algorithm>
#include <cmath>
#include <iostream>

// screen
unsigned int SCR_WIDTH = 1024;
unsigned int SCR_HEIGHT = 576;

// gui scale = window size / this reference size. A bigger reference gives a smaller gui:
// 1280x720 puts the gui at ~0.77x in the default 1024x576 window (both sides limit equally).
const float GUI_BASE_SCALE = 0.74f;
const float GUI_BASE_WIDTH = SCR_WIDTH / GUI_BASE_SCALE;
const float GUI_BASE_HEIGHT = SCR_HEIGHT / GUI_BASE_SCALE;

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

// ---- resource management ---------------------------------------------------
// every create has a matching release. releases zero the ids, so calling them twice is safe,
// and they must run while the GL context still exists (before glfwTerminate).

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO = 0;
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

void releaseQuad()
{
	if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
	if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
}

// render 1x1 plane in normalized device coords
unsigned int planeVAO = 0;
unsigned int planeVBO = 0;
unsigned int planeEBO = 0;
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

void releasePlane()
{
	if (planeVAO) { glDeleteVertexArrays(1, &planeVAO); planeVAO = 0; }
	if (planeVBO) { glDeleteBuffers(1, &planeVBO); planeVBO = 0; }
	if (planeEBO) { glDeleteBuffers(1, &planeEBO); planeEBO = 0; }
}

float floorHW = 4.0f;	// 4x4 floor
void drawFloor(Shader& shader) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, glm::vec3(0.0f, -0.8f, 0.0f));
	model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::scale(model, glm::vec3(floorHW, floorHW, 1.0f));
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

// frees the GL objects the model owns that are reachable from outside the class: its textures
// and mesh VAOs. Mesh keeps its VBO/EBO private, so those still leak until Mesh gets a
// release() method that deletes them (add one to sakuga/mesh.h and call it here).
void releaseModel(Model& model)
{
	for (Texture& t : model.textures_loaded)
		if (t.id) glDeleteTextures(1, &t.id);
	model.textures_loaded.clear();

	for (Mesh& m : model.meshes)
		if (m.VAO) { glDeleteVertexArrays(1, &m.VAO); m.VAO = 0; }
}

// editable from the "Character" panel
const float floorY = -0.8f;
struct CharTransform {
	glm::vec3 position = glm::vec3(0.0f, floorY, 0.0f);
	glm::vec3 rotation = glm::vec3(0.0f);	// degrees, XYZ
	float scale = 1.0f;
};
CharTransform charTransform;

glm::mat4 getCharacterModelMatrix() {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, charTransform.position);
	model = glm::rotate(model, glm::radians(charTransform.rotation.x), glm::vec3(1, 0, 0));
	model = glm::rotate(model, glm::radians(charTransform.rotation.y), glm::vec3(0, 1, 0));
	model = glm::rotate(model, glm::radians(charTransform.rotation.z), glm::vec3(0, 0, 1));
	model = glm::scale(model, glm::vec3(charTransform.scale));
	return model;
}

unsigned int depthMapFBO = 0;
unsigned int depthMap = 0;
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

void releaseDepthMap()
{
	if (depthMapFBO) { glDeleteFramebuffers(1, &depthMapFBO); depthMapFBO = 0; }
	if (depthMap) { glDeleteTextures(1, &depthMap); depthMap = 0; }
}

// ---- bloom / HDR render targets ---------------------------------------------
// the scene is rendered in linear HDR (RGBA16F) so values above 1.0 survive, then:
//   sceneFBO    multisampled target the scene is drawn into (keeps the 4x MSAA). two outputs:
//               attachment 0 = the lit scene, attachment 1 = glow mask (only what should bloom)
//   resolveFBO  single-sample copy of it (MSAA textures can't be sampled by the post passes)
//   pingpongFBO half-resolution pair used for the separable blur
const int MSAA_SAMPLES = 4;
unsigned int sceneFBO = 0, sceneColorRBO = 0, sceneDepthRBO = 0;
unsigned int resolveFBO = 0, resolveTex = 0;
unsigned int sceneGlowMaskRBO = 0;		// attachment 1: glow mask (only what should bloom)
unsigned int glowMaskResolveFBO = 0, glowMaskResolveTex = 0;
constexpr std::size_t pingpongSize = 4;
unsigned int pingpongFBO[pingpongSize] = { 0,0 };
unsigned int pingpongTex[pingpongSize] = { 0,0 };
int bloomW = 0, bloomH = 0;		// size the targets were last allocated for

void checkFramebuffer(const char* name)
{
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "framebuffer not complete: " << name << std::endl;
}

// an FBO whose only attachment is a float color texture
void createColorTarget(unsigned int& fbo, unsigned int& tex, int w, int h, const char* name)
{
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	checkFramebuffer(name);
}

void releaseBloomTargets()
{
	if (sceneFBO) { glDeleteFramebuffers(1, &sceneFBO); sceneFBO = 0; }
	if (sceneColorRBO) { glDeleteRenderbuffers(1, &sceneColorRBO); sceneColorRBO = 0; }
	if (sceneDepthRBO) { glDeleteRenderbuffers(1, &sceneDepthRBO); sceneDepthRBO = 0; }
	if (resolveFBO) { glDeleteFramebuffers(1, &resolveFBO); resolveFBO = 0; }
	if (resolveTex) { glDeleteTextures(1, &resolveTex); resolveTex = 0; }
	if (sceneGlowMaskRBO) { glDeleteRenderbuffers(1, &sceneGlowMaskRBO); sceneGlowMaskRBO = 0; }
	if (glowMaskResolveFBO) { glDeleteFramebuffers(1, &glowMaskResolveFBO); glowMaskResolveFBO = 0; }
	if (glowMaskResolveTex) { glDeleteTextures(1, &glowMaskResolveTex); glowMaskResolveTex = 0; }
	for (std::size_t i = 0; i < pingpongSize; ++i)
	{
		if (pingpongFBO[i]) { glDeleteFramebuffers(1, &pingpongFBO[i]); pingpongFBO[i] = 0; }
		if (pingpongTex[i]) { glDeleteTextures(1, &pingpongTex[i]); pingpongTex[i] = 0; }
	}
	bloomW = bloomH = 0;
}

// (re)creates every bloom target for a w x h window. call again whenever the window is resized.
void createBloomTargets(int w, int h)
{
	releaseBloomTargets();

	// 1st color attachment: lit msaa HDR color scene
	glGenFramebuffers(1, &sceneFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

	glGenRenderbuffers(1, &sceneColorRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, sceneColorRBO);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_RGBA16F, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorRBO);

	// 2nd color attachment: glow mask
	glGenRenderbuffers(1, &sceneGlowMaskRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, sceneGlowMaskRBO);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_RGBA16F, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, sceneGlowMaskRBO);
	const GLenum sceneBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, sceneBuffers);

	// depth
	glGenRenderbuffers(1, &sceneDepthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_DEPTH_COMPONENT24, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);
	checkFramebuffer("scene (msaa)");

	// single-sample resolve target
	createColorTarget(resolveFBO, resolveTex, w, h, "resolve");
	createColorTarget(glowMaskResolveFBO, glowMaskResolveTex, w, h, "glow resolve");

	// blur buffers at half resolution: 4x fewer pixels to blur, and the blur radius doubles for free
	int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
	for (std::size_t i = 0; i < pingpongSize; ++i)
		createColorTarget(pingpongFBO[i], pingpongTex[i], hw, hh, "bloom ping-pong");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	bloomW = w;
	bloomH = h;
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
		stbi_image_free(data);
	}
	else {
		std::cout << "failed to load texture: " << path << std::endl;
	}
	return floorTexture;
}

int main()
{
	// initialize stuff -------------------------------------------------------
	// GLFW
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	// no GLFW_SAMPLES hint: the scene is rendered into our own MSAA FBO and the final pass is a
	// fullscreen quad, so MSAA on the default framebuffer would only waste memory

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
		glfwTerminate();
		return -1;
	}

	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	// ImGui
	gui::init(window);
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(6, 6);
	style.ItemSpacing = ImVec2(6, 3);
	style.FramePadding = ImVec2(4, 2);

	// fps display / stats
	float fpsTimer = 0.0f;
	int fpsFrames = 0;
	float displayedFPS = 0.0f;
	float displayedMs = 0.0f;
	float minFPS = 0.0f;
	float minFPSWindow = 1e9f;
	const int FPS_HISTORY = 90;
	float fpsHistory[FPS_HISTORY] = {};
	int fpsOffset = 0;
	float avgFPSAccum = 0.0f;
	int avgFPSCount = 0;

	// shaders
	Shader shader("resources/shaders/light.vs.glsl", "resources/shaders/light.fs.glsl");
	Shader outlineShader("resources/shaders/outline.vs.glsl", "resources/shaders/outline.fs.glsl");

	Shader depthShader("resources/shaders/depth.vs.glsl", "resources/shaders/depth.fs.glsl");
	Shader debugDepthQuadShader("resources/shaders/fullscreen_quad.vs.glsl", "resources/shaders/debug_depth_quad.fs.glsl");

	// bloom: glow mask -> gaussian blur (ping-pong) -> composite
	Shader blurShader("resources/shaders/fullscreen_quad.vs.glsl", "resources/shaders/bloom_blur.fs.glsl");
	Shader compositeShader("resources/shaders/fullscreen_quad.vs.glsl", "resources/shaders/bloom_composite.fs.glsl");

	Model charModel("resources/objects/2B/2B-1.glb");
	// Model charModel("resources/objects/ijichi-nijika/1.fbx");

	// shader uniforms
	DirLight dirLight;
	ShadowMap shadowMap;
	Material material;
	Outline outline;
	ColorRamp ramp;
	RimLight rim;
	Bloom bloom;

	// ui-only state
	bool vsync = false;
	glm::vec3 clearColor(0.10f, 0.10f, 0.10f);
	CharTransform pendingTransform;		// edited in the panel, applied with "Apply transform"
	float cameraSpeed = camera.MovementSpeed;

	// half-size of the shadow camera's orthographic box. anything outside it gets no shadow,
	// so keep it large enough for the character at its biggest scale
	const float SHADOW_EXTENT = floorHW;

	lastFrameTime = static_cast<float>(glfwGetTime());	// avoid first huge deltaTime

	unsigned int floorTexture = loadTexture("resources/textures/concrete/Concrete047A_1K-JPG_Color.jpg");
	configureDepthMapFBO();

	shader.use();
	shader.setInt("texture_diffuse1", 0);
	shader.setInt("shadowMap", 1);

	// for depth (shadow) map debugging
	debugDepthQuadShader.use();
	debugDepthQuadShader.setInt("depthMap", 0);

	// for bloom
	blurShader.use();
	blurShader.setInt("image", 0);
	compositeShader.use();
	compositeShader.setInt("scene", 0);
	compositeShader.setInt("bloomBlur", 1);

	// render loop ------------------------------------------------------------
	while (!glfwWindowShouldClose(window))
	{
		// calc current frame time
		float currentFrameTime = static_cast<float>(glfwGetTime());
		deltaTime = currentFrameTime - lastFrameTime;
		lastFrameTime = currentFrameTime;

		// if window is minimized: nothing to draw (and a 0x0 framebuffer is invalid), so wait for an event
		if (SCR_WIDTH == 0 || SCR_HEIGHT == 0)
		{
			glfwWaitEvents();
			lastFrameTime = static_cast<float>(glfwGetTime());	// don't turn the wait into one huge deltaTime
			continue;
		}

		// (re)allocate the HDR/bloom targets on first frame and whenever the window size changes
		if ((int)SCR_WIDTH != bloomW || (int)SCR_HEIGHT != bloomH)
			createBloomTargets((int)SCR_WIDTH, (int)SCR_HEIGHT);

		fpsTimer += deltaTime;
		++fpsFrames;
		if (fpsTimer >= 0.5f)
		{
			displayedMs = (fpsTimer / fpsFrames) * 1000.0f;
			displayedFPS = fpsFrames / fpsTimer;
			minFPS = minFPSWindow;
			minFPSWindow = 1e9f;
			fpsHistory[fpsOffset] = displayedFPS;
			fpsOffset = (fpsOffset + 1) % FPS_HISTORY;
			avgFPSAccum += displayedFPS;
			++avgFPSCount;
			fpsTimer = 0.0f;
			fpsFrames = 0;
		}
		if (deltaTime > 0.0f)
			minFPSWindow = std::min(minFPSWindow, 1.0f / deltaTime);

		input.processInput(window);

		// ui (built first, so edits apply to this frame) ---------------------
		gui::setMouseEnabled(!input.mouseLook);
		// scale the gui with the window (limiting side wins, so panels never outgrow the window)
		float guiScale = std::min(SCR_WIDTH / GUI_BASE_WIDTH, SCR_HEIGHT / GUI_BASE_HEIGHT);
		gui::setScale(std::clamp(guiScale, 0.6f, 3.0f));
		gui::beginFrame();
		const float guiItemWidth = 150.0f * gui::scale();

		// ---- left column ----
		gui::PanelStack left = gui::leftColumn();

		if (gui::beginPanel(left, "Performance"))
		{
			ImGui::Text("Average FPS: %.1f", avgFPSCount ? avgFPSAccum / avgFPSCount : 0.0f);
			ImGui::Text("Minimum FPS: %.1f", minFPS);
			ImGui::Text("Frame time:  %.2f ms", displayedMs);
			gui::fpsGraph(fpsHistory, FPS_HISTORY, fpsOffset);
			if (ImGui::Checkbox("Vsync", &vsync))
			{
				glfwSwapInterval(vsync ? 1 : 0);
				// restart the stats so the average/min reflect the new mode
				avgFPSAccum = 0.0f; avgFPSCount = 0;
				minFPS = 0.0f; minFPSWindow = 1e9f;
				fpsTimer = 0.0f; fpsFrames = 0;		// drop the half-finished window (mixes old and new mode)
				displayedFPS = 0.0f; displayedMs = 0.0f;
				std::fill(std::begin(fpsHistory), std::end(fpsHistory), 0.0f);
			}
		}
		gui::endPanel(left);

		if (gui::beginPanel(left, "Character properties"))
		{
			gui::sectionLabel("Transform");
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::DragFloat3("Position", &pendingTransform.position.x, 0.01f);
			ImGui::DragFloat3("Rotation", &pendingTransform.rotation.x, 0.5f);
			ImGui::DragFloat("Scale", &pendingTransform.scale, 0.01f, 0.01f, 10.0f);
			ImGui::PopItemWidth();
			if (ImGui::Button("Apply transform"))
				charTransform = pendingTransform;
			ImGui::SameLine();
			if (ImGui::Button("Reset"))
			{
				charTransform = CharTransform();
				pendingTransform = charTransform;
			}
		}
		gui::endPanel(left);

		if (gui::beginPanel(left, "Render passes"))
		{
			ImGui::Checkbox("Lighting", &input.lighting);
			ImGui::Checkbox("Shadow mapping", &input.shadowMapping);
			ImGui::Checkbox("Toon shading", &input.cellShading);
			ImGui::Checkbox("Outlines", &input.outline);
			ImGui::Checkbox("Rim lighting", &input.rimLighting);
			ImGui::Checkbox("Post-processing", &input.postProcessing);
			ImGui::PushItemWidth(guiItemWidth);
			if (ImGui::SliderFloat("Camera speed", &cameraSpeed, 0.5f, 10.0f))
				camera.MovementSpeed = cameraSpeed;
			ImGui::PopItemWidth();
			ImGui::ColorEdit3("Background color", &clearColor.x, ImGuiColorEditFlags_NoInputs);
			if (ImGui::Button("Reset all"))
			{
				dirLight = DirLight(); shadowMap = ShadowMap(); material = Material();
				outline = Outline(); ramp = ColorRamp(); rim = RimLight();
				input.lighting = true; input.cellShading = true; input.outline = true;
				input.shadowMapping = true; input.rimLighting = true;
			}
		}
		gui::endPanel(left);

		if (input.postProcessing && gui::beginPanel(left, "Post-processing"))
		{
			ImGui::Checkbox("Bloom", &input.bloom);
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::SliderFloat("Intensity", &bloom.intensity, 0.0f, 4.0f);
			ImGui::SliderInt("Blur passes", &bloom.passes, 1, 10);
			ImGui::PopItemWidth();
			ImGui::Checkbox("Bloom only (debug)", &bloom.bloomOnly);
		}
		if (input.postProcessing) gui::endPanel(left);

		// ---- right column ----
		gui::PanelStack right = gui::rightColumn();

		if (gui::beginPanel(right, "Lighting"))
		{
			ImGui::Checkbox("Enable##lighting", &input.lighting);

			if (ImGui::BeginTabBar("lighting_tabs"))
			{
				if (ImGui::BeginTabItem("Position"))
				{
					ImGui::Checkbox("Move light", &input.lightMovement);
					ImGui::PushItemWidth(guiItemWidth);
					ImGui::SliderFloat("x##light", &dirLight.direction.x, -2.0f, 2.0f);
					ImGui::SliderFloat("y##light", &dirLight.direction.y, -2.0f, 2.0f);
					ImGui::SliderFloat("z##light", &dirLight.direction.z, -2.0f, 2.0f);
					ImGui::PopItemWidth();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Intensity"))
				{
					ImGui::PushItemWidth(guiItemWidth);
					ImGui::SliderFloat("Ambient", &dirLight.ambientIntensity, 0.0f, 2.0f);
					ImGui::SliderFloat("Diffuse", &dirLight.diffuseIntensity, 0.0f, 2.0f);
					ImGui::SliderFloat("Specular", &dirLight.specularIntensity, 0.0f, 2.0f);
					ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 128.0f);
					ImGui::PopItemWidth();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		gui::endPanel(right);

		if (gui::beginPanel(right, "Shadow mapping"))
		{
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::Checkbox("Enable##dir", &input.shadowMapping);
			ImGui::Checkbox("Shadow only (debug)", &shadowMap.shadowOnly);
			gui::sectionLabel("Shadow direction");
			ImGui::SliderFloat("x##shadow", &shadowMap.lightDir.x, -1.0f, 1.0f);
			ImGui::SliderFloat("y (steepness)##shadow", &shadowMap.lightDir.y, -2.0f, -0.01f);	// can't reach 0: zero-length dir
			ImGui::SliderFloat("z##shadow", &shadowMap.lightDir.z, -1.0f, 1.0f);
			ImGui::PopItemWidth();
		}
		gui::endPanel(right);

		if (gui::beginPanel(right, "Toon shading"))
		{
			ImGui::Checkbox("Enable##shading", &input.cellShading);
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::SliderInt("Toon levels", &ramp.levels, 2, 8);
			ImGui::SliderFloat("Softness", &ramp.softness, 0.0f, 0.2f, "%.3f");
			ImGui::SliderFloat("Low threshold", &ramp.lowT, 0.0f, 1.0f);
			ImGui::SliderFloat("High threshold", &ramp.highT, 0.0f, 1.0f);
			ramp.lowT = std::min(ramp.lowT, ramp.highT);	// keep low <= high
			ImGui::PopItemWidth();

			gui::sectionLabel("Color ramp");
			gui::rampStrip(ramp.shadow, ramp.mid, ramp.light, ramp.levels, ramp.softness, ramp.lowT, ramp.highT);
			gui::colorRow("Shadow", ramp.shadow);
			gui::colorRow("Mid", ramp.mid);
			gui::colorRow("Light", ramp.light);
			if (ImGui::Button("Reset##ramp"))
			{
				int lv = ramp.levels;
				ramp = ColorRamp();
				ramp.levels = lv;
			}
		}
		gui::endPanel(right);

		if (gui::beginPanel(right, "Outlines"))
		{
			ImGui::Checkbox("Enable##outline", &input.outline);
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::SliderFloat("Width", &outline.width, 0.0f, 0.003f, "%.4f");
			ImGui::SliderFloat("Depth bias", &outline.depthBias, 0.0f, 0.003f, "%.4f");
			ImGui::PopItemWidth();
			gui::colorRow("Color", outline.color);
		}
		gui::endPanel(right);

		if (gui::beginPanel(right, "Rim light"))
		{
			ImGui::Checkbox("Enable##rim", &input.rimLighting);
			ImGui::PushItemWidth(guiItemWidth);
			ImGui::SliderFloat("Intensity##rim", &rim.intensity, 0.0f, 1.0f);
			ImGui::SliderFloat("Power", &rim.power, 0.5f, 8.0f);
			ImGui::PopItemWidth();
			gui::colorRow("Rim color", rim.color);
		}
		gui::endPanel(right);

		gui::controlsBar();
		// --------------------------------------------------------------------

		// background. the scene buffer is linear and gets gamma-encoded in the composite pass,
		// so the picked (sRGB) color has to be converted to linear here to look the same on screen
		glm::vec3 clearLinear = glm::pow(clearColor, glm::vec3(2.2f));
		glClearColor(clearLinear.r, clearLinear.g, clearLinear.b, 1.0f);

		// render depth of scene to texture (from light's POV) in orthographic view
		float lightNearPlane = 1.0f, lightFarPlane = 20.0f;
		float lightDisFromOrig = 7.0f;

		// normalize once (falling back to straight down if the direction is degenerate) and use
		// the normalized vector for both the position and the up-vector test
		glm::vec3 lightDirN = (glm::length(shadowMap.lightDir) > 1e-4f)
			? glm::normalize(shadowMap.lightDir)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		glm::vec3 lightPos = -lightDirN * lightDisFromOrig;
		glm::vec3 up = (std::abs(lightDirN.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

		glm::mat4 lightProjection = glm::ortho(-SHADOW_EXTENT, SHADOW_EXTENT, -SHADOW_EXTENT, SHADOW_EXTENT, lightNearPlane, lightFarPlane);
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
		glm::mat4 lightSpaceMatrix = lightProjection * lightView;

		// depth map (used for rendering shadows)
		depthShader.use();
		depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
		renderDepthMap(depthShader, charModel);

		// everything from here until the composite pass draws into the multisampled HDR target
		glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		const float noGlow[4] = { 0.0f, 0.0f,  0.0f, 1.0f };
		glClearBufferfv(GL_COLOR, 1, noGlow);	// glClear used the background color: reset glow mask to black

		// view/projection transformations
		float aspect = (SCR_HEIGHT > 0) ? (float)SCR_WIDTH / (float)SCR_HEIGHT : 1.0f;
		float nearPlane = 0.1f;
		float farPlane = 100.0f;

		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), aspect, nearPlane, farPlane);
		glm::mat4 view = camera.GetViewMatrix();

		// rendering ----------------------------------------------------------
		// the shadow map lives on unit 1 for the whole pass, so the sampler never points at an empty unit
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, depthMap);
		glActiveTexture(GL_TEXTURE0);

		// character
		shader.use();
		shader_config::applyCamera(shader, projection, view, camera.Position);
		shader_config::applyLightingToggles(shader, input.lighting, input.cellShading, input.rimLighting, input.shadowMapping);
		shader_config::applyMaterial(shader, material);
		shader_config::applyDirLight(shader, dirLight, colors::whiteLight);
		shader_config::applyColorRamp(shader, ramp);
		shader_config::applyRimLight(shader, rim);
		shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
		shader.setBool("gReceiveShadowMapping", false);
		shader.setBool("gReceiveRimLighting", true);
		drawCharacter(shader, charModel, getCharacterModelMatrix());

		// floor
		shader.setBool("gReceiveShadowMapping", true);
		shader.setBool("gReceiveRimLighting", false);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, floorTexture);
		drawFloor(shader);

		// outline
		if (input.outline)
		{
			glDrawBuffer(GL_COLOR_ATTACHMENT0);	// don't touch glow mask
			glCullFace(GL_FRONT);

			outlineShader.use();
			shader_config::applyOutline(outlineShader, outline, projection, view);
			outlineShader.setFloat("aspect", aspect);
			drawOutline(outlineShader, charModel, getCharacterModelMatrix());

			glCullFace(GL_BACK);
			const GLenum sceneBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
			glDrawBuffers(2, sceneBuffers);
		}

		// post-processing ----------------------------------------------------
		glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFBO);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glBlitFramebuffer(0, 0, bloomW, bloomH, 0, 0, bloomW, bloomH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, glowMaskResolveFBO);
		glReadBuffer(GL_COLOR_ATTACHMENT1);
		glBlitFramebuffer(0, 0, bloomW, bloomH, 0, 0, bloomW, bloomH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glReadBuffer(GL_COLOR_ATTACHMENT0);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		bool bloomActive = input.postProcessing && input.bloom;
		if (bloomActive)
		{
			glViewport(0, 0, std::max(1, bloomW / 2), std::max(1, bloomH / 2));

			glBindFramebuffer(GL_READ_FRAMEBUFFER, glowMaskResolveFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pingpongFBO[0]);
			glBlitFramebuffer(0, 0, bloomW, bloomH, 0, 0, std::max(1, bloomW / 2), std::max(1, bloomH / 2),
				GL_COLOR_BUFFER_BIT, GL_LINEAR);

			blurShader.use();
			bool horizontal = true;
			for (int i = 0; i < bloom.passes * 2; ++i)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
				blurShader.setBool("horizontal", horizontal);
				glBindTexture(GL_TEXTURE_2D, pingpongTex[!horizontal]);
				renderQuad();
				horizontal = !horizontal;
			}
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
		compositeShader.use();
		compositeShader.setBool("bloomEnabled", bloomActive);
		compositeShader.setFloat("bloomIntensity", bloom.intensity);
		compositeShader.setBool("bloomOnly", bloomActive && bloom.bloomOnly);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, resolveTex);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, pingpongTex[0]);
		renderQuad();
		glActiveTexture(GL_TEXTURE0);

		// render depth map to quad for visual shadow debugging
		if (input.shadowMapping && shadowMap.shadowOnly)
		{
			debugDepthQuadShader.use();
			debugDepthQuadShader.setFloat("near_plane", lightNearPlane);
			debugDepthQuadShader.setFloat("far_plane", lightFarPlane);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, depthMap);
			renderQuad();
		}
		
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		// light movement
		if (input.lightMovement)
		{
			float speed = 0.7f;
			float amplitude = 0.8f;
			dirLight.direction.x = std::sin(currentFrameTime * speed) * amplitude;
			dirLight.direction.z = std::cos(currentFrameTime * speed) * amplitude;
		}

		// ui is drawn last so it sits on top of the scene
		gui::endFrame();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	// cleanup: release every GL resource while the context still exists
	// ------------------------------------------------------------------------
	gui::shutdown();

	releaseModel(charModel);
	glDeleteTextures(1, &floorTexture);
	releaseDepthMap();
	releaseBloomTargets();
	releasePlane();
	releaseQuad();

	glDeleteProgram(shader.ID);
	glDeleteProgram(outlineShader.ID);
	glDeleteProgram(depthShader.ID);
	glDeleteProgram(debugDepthQuadShader.ID);
	glDeleteProgram(blurShader.ID);
	glDeleteProgram(compositeShader.ID);

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}