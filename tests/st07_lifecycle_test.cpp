	// ST-07: renderer lifecycle and resource release.
	// Builds, uses and destroys the Renderer repeatedly in a hidden GLFW window while the GL context stays current.
	// After every cycle it logs glGetError() and the process memory (RSS), writes a CSV, and prints a summary.
	//
	// usage:  st07_lifecycle_test [cycles] [model path]
	//         defaults: 50 cycles, resources/objects/2B/2B-1.glb
	// run from the project root so the relative resource paths resolve.

	#include <glad/glad.h>
	#include <GLFW/glfw3.h>

	#include <glm/glm.hpp>

	#include <sakuga/camera.h>
	#include <sakuga/renderer.h>

	#include <cstdlib>
	#include <fstream>
	#include <iostream>
	#include <string>
	#include <vector>

	#if defined(_WIN32)
		#define WIN32_LEAN_AND_MEAN
		#include <windows.h>
		#include <psapi.h>
		#pragma comment(lib, "psapi.lib")
	#endif

	// resident memory of this process in kB (0 if it can't be read)
	long readRssKb()
	{
	#if defined(_WIN32)
		PROCESS_MEMORY_COUNTERS pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
			return (long)(pmc.WorkingSetSize / 1024);
		return 0;
	#elif defined(__linux__)
		std::ifstream f("/proc/self/status");
		std::string line;
		while (std::getline(f, line))
		{
			if (line.rfind("VmRSS:", 0) == 0)
				return std::atol(line.c_str() + 6);
		}
		return 0;
	#else
		return 0;
	#endif
	}

	// drains the GL error queue, returns the first error (GL_NO_ERROR if none)
	GLenum drainGlErrors()
	{
		GLenum first = GL_NO_ERROR;
		GLenum e;
		while ((e = glGetError()) != GL_NO_ERROR)
		{
			if (first == GL_NO_ERROR)
				first = e;
		}
		return first;
	}

	int main(int argc, char** argv)
	{
		const int cycles = (argc > 1) ? std::atoi(argv[1]) : 50;
		const std::string modelPath = (argc > 2) ? argv[2] : "resources/objects/2B/2B-1.glb";
		const std::string floorTexPath = "resources/textures/concrete/Concrete047A_1K-JPG_Color.jpg";
		const int warmup = 5;	// memory normally grows over the first few cycles (driver and allocator caches)

		// hidden GL 3.3 core context -------------------------------------------------
		if (!glfwInit())
		{
			std::cout << "failed to init glfw\n";
			return 2;
		}
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	#ifdef __APPLE__
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	#endif
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		const int W = 640, H = 360;
		GLFWwindow* window = glfwCreateWindow(W, H, "st07", NULL, NULL);
		if (!window)
		{
			std::cout << "failed to create glfw window\n";
			glfwTerminate();
			return 2;
		}
		glfwMakeContextCurrent(window);
		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			std::cout << "failed to init glad\n";
			glfwTerminate();
			return 2;
		}
		drainGlErrors();	// ignore anything raised during context setup

		std::cout << "GL_VERSION:  " << glGetString(GL_VERSION) << "\n"
				<< "GL_RENDERER: " << glGetString(GL_RENDERER) << "\n"
				<< "cycles: " << cycles << "   model: " << modelPath << "\n\n";

		std::ofstream csv("ST-06_log.csv");
		csv << "cycle,gl_error,rss_kb,model_loaded,texture_loaded\n";

		std::vector<long> rss;
		int glErrorCycles = 0;
		int loadFailures = 0;

		Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

		for (int i = 1; i <= cycles; ++i)
		{
			bool modelOk = false, texOk = false;

			{	// everything the renderer owns must be released when this scope ends
				sakuga::RendererDesc desc;
				desc.width = W;
				desc.height = H;
				sakuga::Renderer renderer(desc);

				sakuga::Scene scene;
				scene.floor.y = -0.8f;
				scene.floor.halfWidth = 2.0f;
				scene.floor.texture = renderer.loadTexture(floorTexPath);
				texOk = (bool)scene.floor.texture;

				sakuga::ModelHandle model = renderer.loadModel(modelPath);
				modelOk = (bool)model;
				if (model)
				{
					sakuga::ModelInstance instance;
					instance.model = model;
					instance.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.8f, 0.0f));
					scene.instances.push_back(instance);
				}

				sakuga::RenderSettings settings;
				settings.shadowExtent = 2.0f;

				sakuga::View view;
				view.view = camera.GetViewMatrix();
				view.position = camera.Position;
				view.fovDegrees = camera.Zoom;

				renderer.render(scene, view, settings);
				glfwSwapBuffers(window);
				glFinish();
			}	// ~Renderer runs here, with the context still current

			glFinish();
			const GLenum err = drainGlErrors();
			const long mem = readRssKb();
			rss.push_back(mem);

			if (err != GL_NO_ERROR)
				++glErrorCycles;
			if (!modelOk || !texOk)
				++loadFailures;

			std::cout << "cycle " << i << "  gl_error=0x" << std::hex << err << std::dec
					<< "  rss=" << mem << " kB  model=" << modelOk << " tex=" << texOk << "\n";
			csv << i << ",0x" << std::hex << err << std::dec << "," << mem << "," << modelOk << "," << texOk << "\n";

			glfwPollEvents();
		}

		// summary ----------------------------------------------------------------------
		std::cout << "\n---- summary ----\n";
		std::cout << "cycles run:            " << cycles << "\n";
		std::cout << "cycles with GL error:  " << glErrorCycles << "\n";
		std::cout << "cycles with load fail: " << loadFailures << "\n";

		if ((int)rss.size() > warmup + 1 && rss.back() > 0)
		{
			const long afterWarmup = rss[warmup - 1];
			const long last = rss.back();
			const long growth = last - afterWarmup;
			const int remaining = (int)rss.size() - warmup;
			std::cout << "RSS after cycle " << warmup << ": " << afterWarmup << " kB\n";
			std::cout << "RSS after cycle " << rss.size() << ": " << last << " kB\n";
			std::cout << "growth over the last " << remaining << " cycles: " << growth << " kB ("
					<< (growth / remaining) << " kB per cycle)\n";
			std::cout << "(a steady rise every cycle suggests a leak; a flat line after warm-up does not)\n";
		}
		else
		{
			std::cout << "not enough memory samples to judge growth (RSS unreadable or too few cycles)\n";
		}
		std::cout << "log written to ST-07_log.csv\n";
		std::cout << "GPU object counts were NOT measured by this program.\n";

		glfwDestroyWindow(window);
		glfwTerminate();
		return (glErrorCycles == 0 && loadFailures == 0) ? 0 : 1;
	}