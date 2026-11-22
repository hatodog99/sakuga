// demo client for the sakuga renderer: owns the window, input and gui, and hands the renderer
// a scene + settings every frame. all GL drawing lives in sakuga/renderer.{h,cpp}.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui/imgui.h>

#include <sakuga/camera.h>
#include <sakuga/gui.h>
#include <sakuga/input.h>
#include <sakuga/renderer.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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
	// the renderer sets its own viewports; the app only tracks the size
	SCR_WIDTH = width;
	SCR_HEIGHT = height;
}

// files dropped onto the window. the callback only queues them: models are loaded at the start of the next frame
std::vector<std::string> droppedPaths;
void drop_callback(GLFWwindow* window, int count, const char** paths)
{
	for (int i = 0; i < count; ++i)
		droppedPaths.emplace_back(paths[i]);
}

// ---- models in the scene -------------------------------------------------------
const float floorY = -0.8f;
const float floorHW = 2.0f;		// 4x4 floor

struct CharTransform {
	glm::vec3 position = glm::vec3(0.0f, floorY, 0.0f);
	glm::vec3 rotation = glm::vec3(0.0f);	// degrees, XYZ
	float scale = 1.0f;
};

glm::mat4 getModelMatrix(const CharTransform& t) {
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::translate(model, t.position);
	model = glm::rotate(model, glm::radians(t.rotation.x), glm::vec3(1, 0, 0));
	model = glm::rotate(model, glm::radians(t.rotation.y), glm::vec3(0, 1, 0));
	model = glm::rotate(model, glm::radians(t.rotation.z), glm::vec3(0, 0, 1));
	model = glm::scale(model, glm::vec3(t.scale));
	return model;
}

// file name without the directory
std::string baseName(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// trims whitespace and the quotes Windows adds with "Copy as path"
std::string cleanPath(std::string s)
{
	auto junk = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"'; };
	while (!s.empty() && junk(s.back()))
		s.pop_back();
	size_t start = 0;
	while (start < s.size() && junk(s[start]))
		++start;
	return s.substr(start);
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
	// no GLFW_SAMPLES hint: the renderer draws into its own MSAA target

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
	glfwSetDropCallback(window, drop_callback);
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

	// the renderer lives in its own scope: it must be destroyed while the GL context still exists
	{
		// renderer + scene -----------------------------------------------------
		sakuga::RendererDesc desc;
		desc.width = (int)SCR_WIDTH;
		desc.height = (int)SCR_HEIGHT;
		sakuga::Renderer renderer(desc);

		sakuga::Scene scene;
		scene.floor.y = floorY;
		scene.floor.halfWidth = floorHW;
		scene.floor.texture = renderer.loadTexture("resources/textures/concrete/Concrete047A_1K-JPG_Color.jpg");

		// everything the gui edits that the renderer reads
		sakuga::RenderSettings settings;
		settings.shadowExtent = floorHW;

		// model management ------------------------------------------------------
		sakuga::ModelHandle currentModel;		// one model per session: importing replaces it
		std::string modelName;
		CharTransform charTransform;			// what is drawn
		CharTransform pendingTransform;			// edited in the panel, applied with "Apply transform"
		char importPath[512] = "";
		std::string importStatus;

		// loads a model file and makes it the scene's model. returns false (and sets importStatus) on failure,
		// in which case the current model stays
		auto importModel = [&](std::string path) -> bool
			{
				path = cleanPath(path);
				if (path.empty())
				{
					importStatus = "Enter a model path first.";
					return false;
				}
				sakuga::ModelHandle handle = renderer.loadModel(path);
				if (!handle)
				{
					importStatus = "Could not load " + baseName(path);
					return false;
				}

				// one model per session: the new model replaces the old one (the transform carries over)
				if (currentModel)
					renderer.unloadModel(currentModel);
				currentModel = handle;
				modelName = baseName(path);

				sakuga::ModelInstance instance;		// defaults: casts shadows, rim lit, outlined
				instance.model = handle;
				scene.instances.clear();
				scene.instances.push_back(instance);

				importStatus = "Loaded " + modelName;
				return true;
			};

		// removes the model from the scene and frees its GPU resources
		auto deleteModel = [&]()
			{
				if (!currentModel)
					return;
				renderer.unloadModel(currentModel);
				currentModel = sakuga::ModelHandle();
				scene.instances.clear();
				importStatus = "Deleted " + modelName;
				modelName.clear();
			};

		importModel("resources/objects/2B/2B-1.glb");
		// importModel("resources/objects/ijichi-nijika/1.fbx");
		importStatus.clear();

		// ui-only state
		bool vsync = false;
		float cameraSpeed = camera.MovementSpeed;

		// quality knobs for weak GPUs
		const int msaaValues[3] = { 0, 2, 4 };
		int msaaChoice = 2;						// index into msaaValues: starts at 4x
		float renderScale = 1.0f;				// 1.0 = native resolution; lower renders the 3D scene smaller and upscales it
		float pendingRenderScale = renderScale;	// slider value, committed when the slider is released (resizing every frame while dragging would thrash)

		lastFrameTime = static_cast<float>(glfwGetTime());	// avoid first huge deltaTime

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

			// a model dragged onto the window (if several were dropped, the last one wins)
			if (!droppedPaths.empty())
				importModel(droppedPaths.back());
			droppedPaths.clear();

			// quality settings: the renderer rebuilds its targets only when something actually changed
			renderer.resize((int)SCR_WIDTH, (int)SCR_HEIGHT);
			renderer.setMsaa(msaaValues[msaaChoice]);
			renderer.setRenderScale(renderScale);

			fpsTimer += deltaTime;
			++fpsFrames;
			if (fpsTimer >= 0.3f)
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

				gui::sectionLabel("GPU Timers");
				const sakuga::GpuTimings gpu = renderer.gpuTimings();
				ImGui::Text("shadow %.2fms				scene %.2fms", gpu.shadow, gpu.scene);
				ImGui::Text("outline %.2fms			bloom %.2fms", gpu.outline, gpu.bloom);
				ImGui::Text("composite %.2fms	total %.2fms", gpu.composite, gpu.total());
				ImGui::PushItemWidth(guiItemWidth);

				const char* msaaItems[3] = { "Off", "2x", "4x" };
				ImGui::Combo("MSAA", &msaaChoice, msaaItems, 3);
				ImGui::SliderFloat("Render scale", &pendingRenderScale, 0.5f, 1.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit())
					renderScale = pendingRenderScale;
				ImGui::PopItemWidth();
			}
			gui::endPanel(left);

			if (gui::beginPanel(left, "Model"))
			{
				ImGui::TextWrapped("Current: %s", currentModel ? modelName.c_str() : "(none)");

				ImGui::BeginDisabled(!currentModel);
				if (ImGui::Button("Delete model"))
					deleteModel();
				ImGui::EndDisabled();

				ImGui::SetNextItemWidth(-1.0f);
				const bool enterPressed = ImGui::InputTextWithHint("##importPath", "path to .glb / .gltf / .fbx / .obj",
					importPath, sizeof(importPath), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::Button("Import") || enterPressed)
				{
					if (importModel(importPath))
						importPath[0] = '\0';
				}
				ImGui::TextDisabled("or drag a model file onto the window");
			}
			gui::endPanel(left);

			if (gui::beginPanel(left, "Model properties"))
			{
				if (currentModel)
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
				else
					ImGui::TextDisabled("No model loaded");
			}
			gui::endPanel(left);

			if (gui::beginPanel(left, "Render passes"))
			{
				ImGui::Checkbox("Lighting", &input.lighting);
				ImGui::SameLine(100.0f);
				ImGui::Checkbox("Shadow mapping", &input.shadowMapping);
				ImGui::Checkbox("Toon shading", &input.cellShading);
				ImGui::SameLine(100.0f);
				ImGui::Checkbox("Outlines", &input.outline);
				ImGui::Checkbox("Rim lighting", &input.rimLighting);
				ImGui::SameLine(100.0f);
				ImGui::Checkbox("Bloom", &input.bloom);
				ImGui::PushItemWidth(guiItemWidth);
				if (ImGui::SliderFloat("Camera speed", &cameraSpeed, 0.5f, 10.0f))
					camera.MovementSpeed = cameraSpeed;
				ImGui::PopItemWidth();
				ImGui::ColorEdit3("Background color", &settings.clearColor.x, ImGuiColorEditFlags_NoInputs);
				if (ImGui::Button("Reset all"))
				{
					settings.dirLight = DirLight(); settings.shadow = ShadowMap(); settings.material = Material();
					settings.outline = Outline(); settings.ramp = ColorRamp(); settings.rim = RimLight();
					input.lighting = true; input.cellShading = true; input.outline = true;
					input.shadowMapping = true; input.rimLighting = true;
				}
			}
			gui::endPanel(left);

			if (gui::beginPanel(left, "Bloom"))
			{
				ImGui::Checkbox("Enable##bloom", &input.bloom);
				ImGui::Checkbox("Bloom only (debug)", &settings.bloom.bloomOnly);
				ImGui::PushItemWidth(guiItemWidth);
				ImGui::SliderFloat("Intensity", &settings.bloom.intensity, 0.0f, 4.0f);
				ImGui::SliderInt("Blur levels", &settings.bloom.levels, 1, sakuga::BLOOM_MAX_LEVELS);
				ImGui::PopItemWidth();
			}
			gui::endPanel(left);	// always, even when collapsed

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
						ImGui::SliderFloat("x##light", &settings.dirLight.direction.x, -2.0f, 2.0f);
						ImGui::SliderFloat("y##light", &settings.dirLight.direction.y, -2.0f, 2.0f);
						ImGui::SliderFloat("z##light", &settings.dirLight.direction.z, -2.0f, 2.0f);
						ImGui::PopItemWidth();
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Intensity"))
					{
						ImGui::PushItemWidth(guiItemWidth);
						ImGui::SliderFloat("Ambient", &settings.dirLight.ambientIntensity, 0.0f, 2.0f);
						ImGui::SliderFloat("Diffuse", &settings.dirLight.diffuseIntensity, 0.0f, 2.0f);
						ImGui::SliderFloat("Specular", &settings.dirLight.specularIntensity, 0.0f, 2.0f);
						ImGui::SliderFloat("Shininess", &settings.material.shininess, 1.0f, 128.0f);
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
				ImGui::Checkbox("Shadow only (debug)", &settings.shadow.shadowOnly);
				gui::sectionLabel("Shadow direction");
				ImGui::SliderFloat("x##shadow", &settings.shadow.lightDir.x, -1.0f, 1.0f);
				ImGui::SliderFloat("y (steepness)##shadow", &settings.shadow.lightDir.y, -2.0f, -0.01f);	// can't reach 0: zero-length dir
				ImGui::SliderFloat("z##shadow", &settings.shadow.lightDir.z, -1.0f, 1.0f);
				ImGui::PopItemWidth();
			}
			gui::endPanel(right);

			if (gui::beginPanel(right, "Toon shading"))
			{
				ImGui::Checkbox("Enable##shading", &input.cellShading);
				ImGui::PushItemWidth(guiItemWidth);
				ImGui::SliderInt("Toon levels", &settings.ramp.levels, 2, 8);
				ImGui::SliderFloat("Softness", &settings.ramp.softness, 0.0f, 0.2f, "%.3f");
				ImGui::SliderFloat("Low threshold", &settings.ramp.lowT, 0.0f, 1.0f);
				ImGui::SliderFloat("High threshold", &settings.ramp.highT, 0.0f, 1.0f);
				settings.ramp.lowT = std::min(settings.ramp.lowT, settings.ramp.highT);	// keep low <= high
				ImGui::PopItemWidth();

				gui::sectionLabel("Color ramp");
				gui::rampStrip(settings.ramp.shadow, settings.ramp.mid, settings.ramp.light,
					settings.ramp.levels, settings.ramp.softness, settings.ramp.lowT, settings.ramp.highT);
				gui::colorRow("Shadow", settings.ramp.shadow);
				gui::colorRow("Mid", settings.ramp.mid);
				gui::colorRow("Light", settings.ramp.light);
				if (ImGui::Button("Reset##ramp"))
				{
					int lv = settings.ramp.levels;
					settings.ramp = ColorRamp();
					settings.ramp.levels = lv;
				}
			}
			gui::endPanel(right);

			if (gui::beginPanel(right, "Outlines"))
			{
				ImGui::Checkbox("Enable##outline", &input.outline);
				ImGui::PushItemWidth(guiItemWidth);
				ImGui::SliderFloat("Width", &settings.outline.width, 0.0f, 0.003f, "%.4f");
				ImGui::SliderFloat("Depth bias", &settings.outline.depthBias, 0.0f, 0.003f, "%.4f");
				ImGui::PopItemWidth();
				gui::colorRow("Color", settings.outline.color);
			}
			gui::endPanel(right);

			if (gui::beginPanel(right, "Rim light"))
			{
				ImGui::Checkbox("Enable##rim", &input.rimLighting);
				ImGui::PushItemWidth(guiItemWidth);
				ImGui::SliderFloat("Intensity##rim", &settings.rim.intensity, 0.0f, 1.0f);
				ImGui::SliderFloat("Power", &settings.rim.power, 0.5f, 8.0f);
				ImGui::PopItemWidth();
				gui::colorRow("Rim color", settings.rim.color);
			}
			gui::endPanel(right);

			gui::controlsBar();
			// --------------------------------------------------------------------

			// hand the renderer this frame's toggles (input owns them: the gui and the hotkeys both edit it)
			settings.lighting = input.lighting;
			settings.toonShading = input.cellShading;
			settings.outlines = input.outline;
			settings.rimLighting = input.rimLighting;
			settings.shadowMapping = input.shadowMapping;
			settings.bloomEnabled = input.bloom;

			// camera
			sakuga::View view;
			view.view = camera.GetViewMatrix();
			view.position = camera.Position;
			view.fovDegrees = camera.Zoom;

			// scene: the model's current transform
			if (!scene.instances.empty())
				scene.instances[0].transform = getModelMatrix(charTransform);

			renderer.render(scene, view, settings);

			// light movement
			if (input.lightMovement)
			{
				float speed = 0.7f;
				float amplitude = 0.8f;
				settings.dirLight.direction.x = std::sin(currentFrameTime * speed) * amplitude;
				settings.dirLight.direction.z = std::cos(currentFrameTime * speed) * amplitude;
			}

			// ui is drawn last so it sits on top of the scene
			gui::endFrame();

			glfwSwapBuffers(window);
			glfwPollEvents();
		}
	}	// renderer destroyed here: every GL resource it owns is released

	gui::shutdown();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}