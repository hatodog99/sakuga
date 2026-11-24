// Temporary verification harness for UT-01 .. UT-10 (QC Table 5).
//
// This is test support only: it is not part of the renderer API and ships nothing.
// It opens a hidden OpenGL 3.3 Core window, runs each procedure, and writes
//   test_results/unit_test_results.txt      per-test log + the rows to copy into Table 9
//   test_results/*.bmp, *.csv               evidence files (framebuffer captures, sample tables)
//
// Build it as a separate executable that links the same libraries as the demo
// (sakuga sources, GLFW, glad, GLM, Assimp, stb_image) and run it from the same working
// directory as the demo, so that resources/ resolves.
//
//   unit_tests            run everything
//   unit_tests UT-05      run one test (several ids can be listed)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sakuga/camera.h>
#include <sakuga/model.h>
#include <sakuga/renderer.h>
#include <sakuga/shader.h>
#include <sakuga/shader_config.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	const char* kResultsDir = "test_results";
	const char* kTmpDir = "test_tmp";
	const char* kModelPath = "resources/objects/2B/2B-1.glb";
	const int kShadowSize = 2048;

	// ============================================================================
	// test bookkeeping
	// ============================================================================
	struct TestCtx
	{
		std::string id;
		bool failed = false;
		bool blocked = false;
		int passed = 0;
		std::vector<std::string> log;
		std::vector<std::string> evidence;

		void note(const std::string& s) { log.push_back("  " + s); }
		void check(bool ok, const std::string& what)
		{
			log.push_back(std::string(ok ? "  [pass] " : "  [FAIL] ") + what);
			if (ok) ++passed; else failed = true;
		}
		void block(const std::string& why)
		{
			blocked = true;
			log.push_back("  [blocked] " + why);
		}
	};

	// redirects std::cout while alive (Shader and Model report their errors there)
	struct CoutCapture
	{
		std::ostringstream ss;
		std::streambuf* old;
		CoutCapture() : old(std::cout.rdbuf(ss.rdbuf())) {}
		~CoutCapture() { std::cout.rdbuf(old); }
		std::string str() const { return ss.str(); }
	};

	std::string firstLine(const std::string& s)
	{
		size_t end = s.find('\n');
		return s.substr(0, end);
	}

	bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

	// ============================================================================
	// images, offscreen target, evidence
	// ============================================================================
	struct Image
	{
		int w = 0, h = 0;
		std::vector<uint8_t> px;		// RGBA8, row 0 = bottom (glReadPixels order)
		const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
	};

	struct Offscreen
	{
		unsigned int fbo = 0, tex = 0;
		int w = 0, h = 0;

		~Offscreen() { release(); }
		void release()
		{
			if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
			if (tex) { glDeleteTextures(1, &tex); tex = 0; }
			w = h = 0;
		}
		void ensure(int W, int H)
		{
			if (fbo && W == w && H == h)
				return;
			release();
			w = W; h = H;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		}
		void fill(float r, float g, float b)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glViewport(0, 0, w, h);
			glClearColor(r, g, b, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		Image read() const
		{
			Image img;
			img.w = w; img.h = h;
			img.px.resize((size_t)w * h * 4);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
			return img;
		}
	};

	Image renderImage(sakuga::Renderer& r, Offscreen& off, const sakuga::Scene& sc, const sakuga::View& v,
		const sakuga::RenderSettings& s, int W, int H)
	{
		r.resize(W, H);
		off.ensure(W, H);
		r.render(sc, v, s, off.fbo);
		return off.read();
	}

	int maxDiff(const uint8_t* a, const uint8_t* b)
	{
		return std::max({ std::abs((int)a[0] - (int)b[0]), std::abs((int)a[1] - (int)b[1]), std::abs((int)a[2] - (int)b[2]) });
	}

	// pixels whose RGB differs by more than thr in any channel
	long countDiff(const Image& a, const Image& b, int thr)
	{
		if (a.w != b.w || a.h != b.h) return -1;
		long n = 0;
		for (int y = 0; y < a.h; ++y)
			for (int x = 0; x < a.w; ++x)
				if (maxDiff(a.at(x, y), b.at(x, y)) > thr) ++n;
		return n;
	}

	long sumAbsDiff(const Image& a, const Image& b)
	{
		if (a.w != b.w || a.h != b.h) return -1;
		long s = 0;
		for (int y = 0; y < a.h; ++y)
			for (int x = 0; x < a.w; ++x)
			{
				const uint8_t* p = a.at(x, y);
				const uint8_t* q = b.at(x, y);
				s += std::abs((int)p[0] - q[0]) + std::abs((int)p[1] - q[1]) + std::abs((int)p[2] - q[2]);
			}
		return s;
	}

	bool identical(const Image& a, const Image& b) { return countDiff(a, b, 0) == 0; }

	// pixels with any RGB channel above thr
	long countLit(const Image& a, int thr)
	{
		long n = 0;
		for (int y = 0; y < a.h; ++y)
			for (int x = 0; x < a.w; ++x)
			{
				const uint8_t* p = a.at(x, y);
				if (std::max({ p[0], p[1], p[2] }) > thr) ++n;
			}
		return n;
	}

	long sumLit(const Image& a)
	{
		long s = 0;
		for (int y = 0; y < a.h; ++y)
			for (int x = 0; x < a.w; ++x)
			{
				const uint8_t* p = a.at(x, y);
				s += p[0] + p[1] + p[2];
			}
		return s;
	}

	float lumOf(const uint8_t* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

	bool writeBmp(const std::string& path, const Image& img)
	{
		const int rowBytes = (img.w * 3 + 3) & ~3;
		const uint32_t dataSize = (uint32_t)(rowBytes * img.h);
		uint8_t header[54] = {};
		header[0] = 'B'; header[1] = 'M';
		const uint32_t fileSize = 54 + dataSize, offset = 54, dib = 40;
		const int32_t w = img.w, h = img.h;
		const uint16_t planes = 1, bpp = 24;
		std::memcpy(header + 2, &fileSize, 4);
		std::memcpy(header + 10, &offset, 4);
		std::memcpy(header + 14, &dib, 4);
		std::memcpy(header + 18, &w, 4);
		std::memcpy(header + 22, &h, 4);
		std::memcpy(header + 26, &planes, 2);
		std::memcpy(header + 28, &bpp, 2);
		std::memcpy(header + 34, &dataSize, 4);

		std::ofstream f(path, std::ios::binary);
		if (!f) return false;
		f.write((const char*)header, 54);
		std::vector<uint8_t> row(rowBytes, 0);
		for (int y = 0; y < img.h; ++y)		// BMP rows are bottom-up, like the readback
		{
			for (int x = 0; x < img.w; ++x)
			{
				const uint8_t* p = img.at(x, y);
				row[x * 3 + 0] = p[2]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[0];
			}
			f.write((const char*)row.data(), rowBytes);
		}
		return true;
	}

	void saveEvidence(TestCtx& t, const std::string& name, const Image& img)
	{
		const std::string path = std::string(kResultsDir) + "/" + t.id + "_" + name + ".bmp";
		if (writeBmp(path, img)) t.evidence.push_back(path);
	}

	// ============================================================================
	// scene helpers
	// ============================================================================
	glm::mat4 charMatrix() { return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.8f, 0.0f)); }

	sakuga::View frontView()
	{
		sakuga::View v;
		v.view = glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0, 0, 2), glm::vec3(0, 1, 0));
		v.position = glm::vec3(0, 0, 3);
		return v;
	}

	// straight down at the floor, screen right = +x
	sakuga::View topView()
	{
		sakuga::View v;
		v.view = glm::lookAt(glm::vec3(0, 3, 0), glm::vec3(0, -0.8f, 0), glm::vec3(0, 0, -1));
		v.position = glm::vec3(0, 3, 0);
		return v;
	}

	sakuga::Scene characterScene(sakuga::ModelHandle h)
	{
		sakuga::Scene sc;
		sakuga::ModelInstance inst;
		inst.model = h;
		inst.transform = charMatrix();
		sc.instances.push_back(inst);
		return sc;
	}

	sakuga::RendererDesc plainDesc(int w, int h)
	{
		sakuga::RendererDesc d;
		d.width = w; d.height = h;
		d.msaaSamples = 0;
		d.shadowMapSize = kShadowSize;
		return d;
	}

	void releaseModel(Model& model)
	{
		for (Texture& t : model.textures_loaded)
			if (t.id) glDeleteTextures(1, &t.id);
		model.textures_loaded.clear();
		for (Mesh& m : model.meshes)
			m.release();
	}

	// inspects the renderer's shadow depth texture through a depth-only framebuffer
	struct DepthProbe
	{
		unsigned int fbo = 0;
		int size = 0;
		~DepthProbe() { if (fbo) glDeleteFramebuffers(1, &fbo); }

		bool attach(unsigned int depthTex, int sz)
		{
			size = sz;
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
			glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);
			return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		}
		// overwrites the whole depth texture with a marker value (the renderer's own clears use 1.0)
		void fill(float v)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glDepthMask(GL_TRUE);
			glDisable(GL_SCISSOR_TEST);
			glClearDepth(v);
			glClear(GL_DEPTH_BUFFER_BIT);
			glClearDepth(1.0);
		}
		float texel(int x, int y)
		{
			float d = -1.0f;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
			return d;
		}
		std::vector<float> read()
		{
			std::vector<float> data((size_t)size * size);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, data.data());
			return data;
		}
		long populated()
		{
			long n = 0;
			for (float d : read()) if (d < 0.999999f) ++n;
			return n;
		}
	};

	// ---- minimal glTF fixture: one triangle whose material points at an external image ----
	std::string base64(const std::vector<uint8_t>& d)
	{
		static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		size_t i = 0;
		for (; i + 2 < d.size(); i += 3)
		{
			uint32_t n = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8) | d[i + 2];
			out += tbl[(n >> 18) & 63]; out += tbl[(n >> 12) & 63]; out += tbl[(n >> 6) & 63]; out += tbl[n & 63];
		}
		if (i + 1 == d.size())
		{
			uint32_t n = (uint32_t)d[i] << 16;
			out += tbl[(n >> 18) & 63]; out += tbl[(n >> 12) & 63]; out += "==";
		}
		else if (i + 2 == d.size())
		{
			uint32_t n = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8);
			out += tbl[(n >> 18) & 63]; out += tbl[(n >> 12) & 63]; out += tbl[(n >> 6) & 63]; out += '=';
		}
		return out;
	}

	bool writeTriangleGltf(const std::string& path, const std::string& textureUri)
	{
		std::vector<uint8_t> buf;
		auto putF = [&](float f) { uint8_t b[4]; std::memcpy(b, &f, 4); buf.insert(buf.end(), b, b + 4); };
		const float pos[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
		const float nrm[9] = { 0, 0, 1,  0, 0, 1,  0, 0, 1 };
		const float uv[6] = { 0, 0,  1, 0,  0, 1 };
		for (float f : pos) putF(f);
		for (float f : nrm) putF(f);
		for (float f : uv) putF(f);
		const uint16_t idx[3] = { 0, 1, 2 };
		for (uint16_t i : idx) { buf.push_back((uint8_t)(i & 0xFF)); buf.push_back((uint8_t)(i >> 8)); }
		buf.push_back(0); buf.push_back(0);		// pad the buffer to a multiple of 4 (104 bytes)

		std::ostringstream js;
		js << "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
		   << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
		   << "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
		   << "\"textures\":[{\"source\":0}],\"images\":[{\"uri\":\"" << textureUri << "\"}],"
		   << "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64," << base64(buf) << "\",\"byteLength\":" << buf.size() << "}],"
		   << "\"bufferViews\":["
		   << "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36,\"target\":34962},"
		   << "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36,\"target\":34962},"
		   << "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24,\"target\":34962},"
		   << "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6,\"target\":34963}],"
		   << "\"accessors\":["
		   << "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
		   << "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
		   << "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
		   << "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
		std::ofstream f(path, std::ios::binary);
		if (!f) return false;
		f << js.str();
		return true;
	}

	std::vector<uint8_t> readFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	// ============================================================================
	// UT-01  GLSL shader loader and compiler
	// ============================================================================
	void ut01_shaders(TestCtx& t)
	{
		const std::string dir = "resources/shaders/";
		struct Pair { const char* name; const char* vs; const char* fs; };
		const Pair pairs[] = {
			{ "lit",              "light.vs.glsl",           "light.fs.glsl" },
			{ "outline",          "outline.vs.glsl",         "outline.fs.glsl" },
			{ "shadow depth",     "depth.vs.glsl",           "depth.fs.glsl" },
			{ "debug depth",      "fullscreen_quad.vs.glsl", "debug_depth_quad.fs.glsl" },
			{ "bloom downsample", "fullscreen_quad.vs.glsl", "bloom_downsample.fs.glsl" },
			{ "bloom upsample",   "fullscreen_quad.vs.glsl", "bloom_upsample.fs.glsl" },
			{ "composite",        "fullscreen_quad.vs.glsl", "bloom_composite.fs.glsl" },
		};

		auto blockSize = [](unsigned int prog, const char* name) -> long
		{
			unsigned int idx = glGetUniformBlockIndex(prog, name);
			if (idx == GL_INVALID_INDEX) return -1;
			GLint size = 0;
			glGetActiveUniformBlockiv(prog, idx, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
			return size;
		};

		// ---- valid programs ----
		for (const Pair& p : pairs)
		{
			std::string log;
			GLint linked = 0;
			long frameSize = -1, lightSize = -1;
			{
				CoutCapture cap;
				Shader s((dir + p.vs).c_str(), (dir + p.fs).c_str());
				log = cap.str();
				glGetProgramiv(s.ID, GL_LINK_STATUS, &linked);
				frameSize = blockSize(s.ID, "FrameBlock");
				lightSize = blockSize(s.ID, "LightingBlock");
				glDeleteProgram(s.ID);
			}
			t.check(linked == GL_TRUE && !contains(log, "error::shader"),
				std::string(p.name) + " program links with no compile/link diagnostics");

			const std::string name = p.name;
			if (name == "lit")
			{
				t.check(frameSize == (long)sizeof(FrameData), "lit: FrameBlock size " + std::to_string(frameSize) + " == sizeof(FrameData) " + std::to_string(sizeof(FrameData)));
				t.check(lightSize == (long)sizeof(LightingData), "lit: LightingBlock size " + std::to_string(lightSize) + " == sizeof(LightingData) " + std::to_string(sizeof(LightingData)));
			}
			else if (name == "outline")
			{
				t.check(frameSize == (long)sizeof(FrameData), "outline: FrameBlock size " + std::to_string(frameSize) + " == sizeof(FrameData)");
			}
		}

		// ---- deliberately corrupted copy of light.fs.glsl ----
		{
			std::ifstream in(dir + "light.fs.glsl");
			std::stringstream ss;
			ss << in.rdbuf();
			const std::string corruptPath = std::string(kTmpDir) + "/corrupt.fs.glsl";
			std::ofstream out(corruptPath);
			out << ss.str() << "\nthis line is not valid glsl;\n";
			out.close();

			std::string log;
			GLint linked = 1;
			{
				CoutCapture cap;
				Shader s((dir + "light.vs.glsl").c_str(), corruptPath.c_str());
				log = cap.str();
				glGetProgramiv(s.ID, GL_LINK_STATUS, &linked);
				glDeleteProgram(s.ID);
			}
			t.check(linked == GL_FALSE, "corrupted shader: program reports unsuccessful link");
			t.check(contains(log, "compilation_failed"), "corrupted shader: compile diagnostic recorded");
			t.note("console: " + firstLine(log));
		}

		// ---- missing shader file ----
		{
			std::string log;
			GLint linked = 1;
			{
				CoutCapture cap;
				Shader s((dir + "light.vs.glsl").c_str(), (dir + "does_not_exist.glsl").c_str());
				log = cap.str();
				glGetProgramiv(s.ID, GL_LINK_STATUS, &linked);
				glDeleteProgram(s.ID);
			}
			t.check(linked == GL_FALSE, "missing shader file: program reports unsuccessful link");
			t.check(contains(log, "file_not_successfully_read"), "missing shader file: read diagnostic recorded");
			t.note("console: " + firstLine(log));
		}
	}

	// ============================================================================
	// UT-02  Assimp / stb_image model loader
	// ============================================================================
	void ut02_modelLoad(TestCtx& t)
	{
		size_t firstCount = 0;
		for (int pass = 0; pass < 2; ++pass)
		{
			const std::string tag = "load " + std::to_string(pass + 1) + ": ";
			Model m(kModelPath);

			t.check(!m.meshes.empty(), tag + "at least one mesh imported (" + std::to_string(m.meshes.size()) + ")");
			if (pass == 0) firstCount = m.meshes.size();
			else t.check(m.meshes.size() == firstCount, tag + "mesh count matches the first load");

			bool meshesOk = true;
			for (const Mesh& mesh : m.meshes)
				if (mesh.VAO == 0 || !glIsVertexArray(mesh.VAO) || mesh.indexCount == 0 || mesh.indexCount % 3 != 0)
					meshesOk = false;
			t.check(meshesOk, tag + "every mesh has a valid VAO and a positive index count divisible by three");

			int diffuseCount = 0;
			bool texturesOk = true;
			for (const Texture& tex : m.textures_loaded)
			{
				if (tex.type != "texture_diffuse") continue;
				++diffuseCount;
				GLint w = 0, h = 0;
				glBindTexture(GL_TEXTURE_2D, tex.id);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
				if (!glIsTexture(tex.id) || w <= 1 || h <= 1) texturesOk = false;
			}
			t.check(diffuseCount > 0, tag + "diffuse textures found (" + std::to_string(diffuseCount) + ")");
			t.check(texturesOk, tag + "every diffuse texture is a valid texture object larger than 1x1");

			releaseModel(m);
		}
	}

	// ============================================================================
	// UT-03  Invalid-asset fault handling
	// ============================================================================
	void ut03_invalidAssets(TestCtx& t)
	{
		sakuga::Renderer r(plainDesc(64, 64));

		// (a) invalid model imports
		const std::string truncatedPath = std::string(kTmpDir) + "/truncated.glb";
		{
			std::vector<uint8_t> bytes = readFile(kModelPath);
			if (bytes.size() > 100)
			{
				bytes.resize(bytes.size() / 2);
				std::ofstream f(truncatedPath, std::ios::binary);
				f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
			}
			else
				t.block("could not read " + std::string(kModelPath) + " to build the truncated copy");
		}

		const std::pair<std::string, std::string> badModels[] = {
			{ "empty path", "" },
			{ "nonexistent .glb", "resources/objects/does_not_exist.glb" },
			{ "directory path", "resources" },
			{ "truncated GLB copy", truncatedPath },
		};
		for (const auto& bm : badModels)
		{
			std::string log;
			sakuga::ModelHandle h;
			{
				CoutCapture cap;
				h = r.loadModel(bm.second);
				log = cap.str();
			}
			t.check(!h, "(a) loadModel(" + bm.first + ") returns an invalid handle");
			if (!log.empty()) t.note("console: " + firstLine(log));
		}

		// (b) missing standalone texture
		{
			std::string log;
			sakuga::TextureHandle th;
			{
				CoutCapture cap;
				th = r.loadTexture(std::string(kTmpDir) + "/no_such_image.png");
				log = cap.str();
			}
			t.check(!th, "(b) loadTexture(nonexistent) returns an invalid handle");
			if (!log.empty()) t.note("console: " + firstLine(log));
		}

		// (c) valid model whose referenced texture file is missing: built as a minimal glTF fixture
		{
			const std::string gltfPath = std::string(kTmpDir) + "/missing_texture_model.gltf";
			fs::remove(std::string(kTmpDir) + "/missing_texture.png");
			if (!writeTriangleGltf(gltfPath, "missing_texture.png"))
			{
				t.block("(c) could not write the glTF fixture");
				return;
			}

			sakuga::ModelHandle h;
			{
				CoutCapture cap;
				h = r.loadModel(gltfPath);
				t.note("console: " + firstLine(cap.str()));
			}
			t.check((bool)h, "(c) model with a missing referenced texture still loads (valid handle)");

			// inspect the fallback texture on a directly constructed copy
			CoutCapture cap;
			Model m(gltfPath);
			const std::string log = cap.str();
			bool found = false, white1x1 = false;
			for (const Texture& tex : m.textures_loaded)
			{
				if (tex.type != "texture_diffuse") continue;
				found = true;
				GLint w = 0, h2 = 0;
				glBindTexture(GL_TEXTURE_2D, tex.id);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h2);
				uint8_t px[4] = { 0, 0, 0, 0 };
				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
				white1x1 = (w == 1 && h2 == 1 && px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255);
			}
			releaseModel(m);
			t.check(found, "(c) the model has a diffuse texture slot");
			t.check(white1x1, "(c) the missing texture is replaced by a 1x1 opaque white texture");
			t.check(contains(log, "Could not load texture file"), "(c) a diagnostic names the missing texture file");
		}
	}

	// ============================================================================
	// UT-04  Camera controls and frame data preparation (CPU only)
	// ============================================================================
	void ut04_camera(TestCtx& t)
	{
		auto approx = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };

		Camera cam;
		t.check(approx(cam.Front.x, 0) && approx(cam.Front.y, 0) && approx(cam.Front.z, -1), "default camera looks down -Z");

		const float dt = 0.5f;
		const float d = cam.MovementSpeed * dt;
		struct Move { Camera_Movement dir; const char* name; glm::vec3 expect; };
		const Move moves[] = {
			{ FORWARD,  "FORWARD",  glm::vec3(0, 0, -d) },
			{ BACKWARD, "BACKWARD", glm::vec3(0, 0,  d) },
			{ LEFT,     "LEFT",     glm::vec3(-d, 0, 0) },
			{ RIGHT,    "RIGHT",    glm::vec3( d, 0, 0) },
			{ UP,       "UP",       glm::vec3(0,  d, 0) },
			{ DOWN,     "DOWN",     glm::vec3(0, -d, 0) },
		};
		for (const Move& m : moves)
		{
			cam.Position = glm::vec3(0.0f);
			cam.ProcessKeyboard(m.dir, dt);
			t.check(approx(cam.Position.x, m.expect.x) && approx(cam.Position.y, m.expect.y) && approx(cam.Position.z, m.expect.z),
				std::string("keyboard ") + m.name + " moves by speed * deltaTime in the implemented direction");
		}

		cam.ProcessMouseMovement(0.0f, 1.0e6f, GL_TRUE);
		t.check(approx(cam.Pitch, 89.0f), "pitch clamps at +89 (got " + std::to_string(cam.Pitch) + ")");
		cam.ProcessMouseMovement(0.0f, -2.0e6f, GL_TRUE);
		t.check(approx(cam.Pitch, -89.0f), "pitch clamps at -89 (got " + std::to_string(cam.Pitch) + ")");

		const glm::mat4 vm = cam.GetViewMatrix();
		bool finite = true;
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (!std::isfinite(vm[c][r])) finite = false;
		t.check(finite, "view matrix is finite at the pitch limits");

		cam.ProcessMouseScroll(1000.0f);
		t.check(approx(cam.Zoom, 1.0f), "zoom clamps at 1 degree (got " + std::to_string(cam.Zoom) + ")");
		cam.ProcessMouseScroll(-1000.0f);
		t.check(approx(cam.Zoom, 45.0f), "zoom clamps at 45 degrees (got " + std::to_string(cam.Zoom) + ")");

		// (b) frame data
		const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
		const glm::mat4 view = glm::lookAt(glm::vec3(1, 2, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		const glm::mat4 light = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 1.0f, 20.0f);
		const glm::vec3 pos(1.0f, 2.0f, 3.0f);
		const FrameData fd = shader_config::makeFrameData(proj, view, light, pos);
		t.check(fd.view == view && fd.projection == proj && fd.lightSpaceMatrix == light, "frame data holds the supplied view, projection, and light-space matrices");
		t.check(fd.viewPos.x == 1.0f && fd.viewPos.y == 2.0f && fd.viewPos.z == 3.0f && fd.viewPos.w == 0.0f, "frame data camera position keeps xyz and viewPos.w == 0");
	}

	// ============================================================================
	// UT-05  Toon quantization and color ramp
	// ============================================================================
	glm::vec3 refRamp(float ndl, int levels, float softness, float lowT, float highT,
		const glm::vec3& shadow, const glm::vec3& mid, const glm::vec3& light)
	{
		const int n = std::clamp(levels, 2, 8);
		const float s = std::max(softness, 0.0001f);
		float tt = 0.0f;
		for (int i = 0; i < n - 1; ++i)
		{
			const float k = (n > 2) ? float(i) / float(n - 2) : 0.5f;
			const float edge = glm::mix(lowT, highT, k);
			tt += glm::smoothstep(edge - s, edge + s, ndl);
		}
		tt /= float(n - 1);
		return (tt < 0.5f) ? glm::mix(shadow, mid, tt * 2.0f) : glm::mix(mid, light, (tt - 0.5f) * 2.0f);
	}

	int expectedByte(float v)
	{
		v = std::clamp(v, 0.0f, 1.0f);
		return (int)std::lround(255.0f * std::pow(v, 1.0f / 2.2f));
	}

	void ut05_toon(TestCtx& t)
	{
		sakuga::Renderer r(plainDesc(64, 64));
		Offscreen off;
		const int W = 64, H = 64;

		sakuga::Scene sc;				// no models: only the white floor
		sakuga::View view = topView();
		sakuga::RenderSettings s;
		s.lighting = true; s.toonShading = true;
		s.shadowMapping = false; s.rimLighting = false; s.bloomEnabled = false; s.outlines = false;
		s.clearColor = glm::vec3(0.0f);
		s.dirLight.ambientIntensity = 0.6f / 0.7f;		// makes the shader's ambient scale exactly 1
		s.ramp.shadow = glm::vec3(0.20f, 0.10f, 0.30f);
		s.ramp.mid = glm::vec3(0.50f, 0.60f, 0.40f);
		s.ramp.light = glm::vec3(0.90f, 0.80f, 0.70f);
		s.ramp.lowT = 0.2f;
		s.ramp.highT = 0.6f;

		const int tolerance = 6;		// R11G11B10F targets keep 5-6 mantissa bits, then gamma and rounding
		const int levelsList[] = { 2, 3, 4, 8 };
		const float softList[] = { 0.0f, 0.2f };
		const float ndlSamples[] = { 0.02f, 0.10f, 0.23f, 0.33f, 0.47f, 0.57f, 0.80f, 0.97f };

		std::ostringstream csv;
		csv << "levels,softness,ndl,expected_rgb,actual_rgb,max_diff\n";
		int compared = 0, outside = 0;

		auto centerPixel = [&](float ndl) -> std::vector<int>
		{
			s.dirLight.direction = glm::vec3(-std::sqrt(1.0f - ndl * ndl), -ndl, 0.0f);
			const Image img = renderImage(r, off, sc, view, s, W, H);
			const uint8_t* p = img.at(W / 2, H / 2);
			return { p[0], p[1], p[2] };
		};

		for (int levels : levelsList)
			for (float soft : softList)
			{
				s.ramp.levels = levels;
				s.ramp.softness = soft;
				for (float ndl : ndlSamples)
				{
					const std::vector<int> got = centerPixel(ndl);
					const glm::vec3 e = refRamp(ndl, levels, soft, s.ramp.lowT, s.ramp.highT, s.ramp.shadow, s.ramp.mid, s.ramp.light);
					const int ex[3] = { expectedByte(e.r), expectedByte(e.g), expectedByte(e.b) };
					const int diff = std::max({ std::abs(got[0] - ex[0]), std::abs(got[1] - ex[1]), std::abs(got[2] - ex[2]) });
					++compared;
					if (diff > tolerance) ++outside;
					csv << levels << "," << soft << "," << ndl << ",\"" << ex[0] << " " << ex[1] << " " << ex[2] << "\",\""
						<< got[0] << " " << got[1] << " " << got[2] << "\"," << diff << "\n";
				}
			}

		const std::string csvPath = std::string(kResultsDir) + "/UT-05_samples.csv";
		{ std::ofstream f(csvPath); f << csv.str(); }
		t.evidence.push_back(csvPath);

		t.check(outside == 0, std::to_string(compared - outside) + " of " + std::to_string(compared) +
			" sampled pixels match the ramp equation within +/-" + std::to_string(tolerance) + " (levels 2/3/4/8, softness 0 and 0.2)");

		// softness changes the transition region: levels 3, ndl just past the first edge
		s.ramp.levels = 3;
		s.ramp.softness = 0.0f;
		const std::vector<int> hard = centerPixel(0.25f);
		s.ramp.softness = 0.2f;
		const std::vector<int> soft = centerPixel(0.25f);
		const int d = std::max({ std::abs(hard[0] - soft[0]), std::abs(hard[1] - soft[1]), std::abs(hard[2] - soft[2]) });
		t.check(d >= 3, "raising Softness from 0 to 0.2 changes the pixel inside the transition region (max channel change " + std::to_string(d) + ")");
	}

	// ============================================================================
	// UT-06  Inverted-hull outline geometry
	// ============================================================================
	void ut06_outline(TestCtx& t)
	{
		sakuga::Renderer r(plainDesc(640, 360));
		sakuga::ModelHandle h = r.loadModel(kModelPath);
		if (!h) { t.check(false, "2B-1.glb loads"); return; }

		sakuga::Scene sc = characterScene(h);
		sc.floor.enabled = false;
		sakuga::View view = frontView();
		sakuga::RenderSettings s;
		s.shadowMapping = false; s.bloomEnabled = false; s.rimLighting = false;
		s.outline.color = glm::vec3(1.0f, 0.0f, 1.0f);
		Offscreen off;
		const int W = 640, H = 360;

		auto magenta = [](const Image& img) -> long
		{
			long n = 0;
			for (int y = 0; y < img.h; ++y)
				for (int x = 0; x < img.w; ++x)
				{
					const uint8_t* p = img.at(x, y);
					if (p[0] > 200 && p[1] < 60 && p[2] > 200) ++n;
				}
			return n;
		};

		s.outlines = false;
		const Image offImg = renderImage(r, off, sc, view, s, W, H);
		const long base = magenta(offImg);
		t.note("outline-colored pixels with outlines off (baseline): " + std::to_string(base));
		saveEvidence(t, "outlines_off", offImg);
		s.outlines = true;

		auto netCount = [&](float width, float bias, const std::string& name) -> long
		{
			s.outline.width = width;
			s.outline.depthBias = bias;
			const Image img = renderImage(r, off, sc, view, s, W, H);
			saveEvidence(t, name, img);
			return std::max(0L, magenta(img) - base);
		};

		const long w0 = netCount(0.0f, 0.001f, "width_0");
		const long w1 = netCount(0.001f, 0.001f, "width_0.001");
		const long w3 = netCount(0.003f, 0.001f, "width_0.003");
		t.note("net outline pixels: width 0 = " + std::to_string(w0) + ", 0.001 = " + std::to_string(w1) + ", 0.003 = " + std::to_string(w3));
		t.check(w1 > w0, "a positive Width produces more outline coverage than Width 0");
		t.check(w3 >= w1, "Width 0.003 gives at least the coverage of Width 0.001");

		const long b0 = netCount(0.001f, 0.0f, "bias_0");
		const long b1 = netCount(0.001f, 0.001f, "bias_0.001");
		const long b3 = netCount(0.001f, 0.003f, "bias_0.003");
		t.note("net outline pixels: depth bias 0 = " + std::to_string(b0) + ", 0.001 = " + std::to_string(b1) + ", 0.003 = " + std::to_string(b3) + " (inspect the bias_*.bmp captures for gaps)");
		t.check(b0 > 0 && b1 > 0 && b3 > 0, "outline stays visible at depth bias 0, 0.001, and 0.003");

		// the global switch and the per-instance flag both remove the outline
		s.outline.width = 0.001f; s.outline.depthBias = 0.001f;
		sakuga::Scene noOutline = sc;
		noOutline.instances[0].outlined = false;
		const Image instOff = renderImage(r, off, noOutline, view, s, W, H);
		t.check(identical(instOff, offImg), "ModelInstance::outlined = false removes the outline (image equals outlines off)");
	}

	// ============================================================================
	// UT-07  Shadow depth map and PCF pass
	// ============================================================================
	void ut07_shadow(TestCtx& t)
	{
		sakuga::Renderer r(plainDesc(512, 288));
		sakuga::ModelHandle h = r.loadModel(kModelPath);
		if (!h) { t.check(false, "2B-1.glb loads"); return; }

		sakuga::Scene sc = characterScene(h);
		sakuga::RenderSettings s;
		s.shadowMapping = true; s.bloomEnabled = false; s.outlines = false; s.rimLighting = false;
		s.shadow.lightDir = glm::vec3(-0.5f, -0.85f, 0.0f);
		Offscreen off;

		// (a) depth map
		renderImage(r, off, sc, topView(), s, 512, 288);
		const unsigned int depthTex = r.shadowMapTexture();
		t.check(depthTex != 0 && glIsTexture(depthTex), "(a) shadowMapTexture() returns a texture object");

		DepthProbe probe;
		const bool complete = probe.attach(depthTex, kShadowSize);
		t.check(complete, "(a) depth-only inspection framebuffer is GL_FRAMEBUFFER_COMPLETE");
		if (complete)
		{
			const std::vector<float> depth = probe.read();
			long populated = 0;
			float minD = 1.0f;
			for (float d : depth) { if (d < 0.999999f) ++populated; minD = std::min(minD, d); }
			const long total = (long)depth.size();
			t.note("depth texels populated: " + std::to_string(populated) + " of " + std::to_string(total) + ", minimum depth " + std::to_string(minD));
			t.check(populated > 0 && populated < total, "(a) the depth map holds values inside the caster's footprint and is not entirely cleared");
			t.check(minD > 0.0f && minD < 1.0f, "(a) populated depth values lie strictly between 0 and 1");
		}

		// (b) PCF boundary on the floor: shadow on versus off, same camera
		const Image onImg = renderImage(r, off, sc, topView(), s, 512, 288);
		saveEvidence(t, "shadow_on", onImg);
		sakuga::RenderSettings sOff = s;
		sOff.shadowMapping = false;
		const Image offImg = renderImage(r, off, sc, topView(), sOff, 512, 288);
		saveEvidence(t, "shadow_off", offImg);

		const int W = offImg.w, H = offImg.h;
		// two points well inside the floor (the image corners are background in this view)
		t.check(maxDiff(offImg.at(W / 5, H / 2), offImg.at(W * 4 / 5, H / 2)) == 0,
			"(b) with shadow mapping off two floor points share one color");

		// how much each pixel darkens when shadow mapping is switched on
		float maxDark = 0.0f;
		long changed = 0;
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
			{
				if (maxDiff(onImg.at(x, y), offImg.at(x, y)) > 0) ++changed;
				maxDark = std::max(maxDark, lumOf(offImg.at(x, y)) - lumOf(onImg.at(x, y)));
			}

		long full = 0, partial = 0;
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
			{
				const float dark = lumOf(offImg.at(x, y)) - lumOf(onImg.at(x, y));
				if (dark <= 2.0f) continue;
				if (dark >= maxDark - 2.0f) ++full; else ++partial;
			}

		t.note("pixels changed " + std::to_string(changed) + ", maximum darkening " + std::to_string(maxDark) +
			", fully shadowed " + std::to_string(full) + ", partially shadowed " + std::to_string(partial));
		t.check(changed > 0 && full > 0, "(b) enabling shadow mapping darkens a region of the floor");
		t.check(partial > 0, "(b) the shadow boundary contains partially shadowed samples (3x3 PCF)");
	}

	// ============================================================================
	// UT-08  Rim light and bloom pass
	// ============================================================================
	void ut08_rimBloom(TestCtx& t)
	{
		const int W = 384, H = 216;
		sakuga::Renderer r(plainDesc(W, H));
		sakuga::ModelHandle h = r.loadModel(kModelPath);
		if (!h) { t.check(false, "2B-1.glb loads"); return; }

		sakuga::Scene sc = characterScene(h);
		sakuga::View view = frontView();
		Offscreen off;

		sakuga::RenderSettings base;
		base.shadowMapping = false; base.outlines = false; base.bloomEnabled = false; base.rimLighting = false;

		// ---- (a) rim light, bloom off ----
		auto rimImg = [&](bool on, float intensity, float power)
		{
			sakuga::RenderSettings s = base;
			s.rimLighting = on; s.rim.intensity = intensity; s.rim.power = power;
			return renderImage(r, off, sc, view, s, W, H);
		};
		const Image rimOff = rimImg(false, 0.55f, 3.0f);
		const Image i0 = rimImg(true, 0.0f, 3.0f);
		const Image i55 = rimImg(true, 0.55f, 3.0f);
		const Image i100 = rimImg(true, 1.0f, 3.0f);
		saveEvidence(t, "rim_off", rimOff);
		saveEvidence(t, "rim_0.55_p3", i55);

		t.check(identical(i0, rimOff), "(a) Intensity 0 matches rim lighting disabled");
		const long d55 = sumAbsDiff(i55, rimOff), d100 = sumAbsDiff(i100, rimOff);
		t.note("rim contribution (sum of channel differences): intensity 0.55 = " + std::to_string(d55) + ", 1.0 = " + std::to_string(d100));
		t.check(d55 > 0, "(a) Intensity 0.55 adds a visible rim contribution");
		t.check(d100 > d55, "(a) Intensity 1.0 adds more rim light than 0.55");

		const Image p05 = rimImg(true, 0.55f, 0.5f);
		const Image p3 = i55;
		const Image p8 = rimImg(true, 0.55f, 8.0f);
		const long n05 = countDiff(p05, rimOff, 2), n3 = countDiff(p3, rimOff, 2), n8 = countDiff(p8, rimOff, 2);
		t.note("rim pixels: power 0.5 = " + std::to_string(n05) + ", 3 = " + std::to_string(n3) + ", 8 = " + std::to_string(n8));
		t.check(n05 > n3 && n3 > 0 && n3 >= n8, "(a) a higher Power gives a thinner rim (pixel count falls from power 0.5 to 3 to 8)");

		// ---- (b) bloom sources, viewed through the bloom-only debug output ----
		auto glowImg = [&](const sakuga::Scene& scene, auto modify)
		{
			sakuga::RenderSettings s = base;
			s.bloomEnabled = true; s.bloom.bloomOnly = true;
			s.rimLighting = true; s.rim.intensity = 0.55f;
			modify(s);
			return renderImage(r, off, scene, view, s, W, H);
		};
		auto isBlack = [](const Image& img) { return countLit(img, 0) == 0; };

		const Image none = glowImg(sc, [](sakuga::RenderSettings& s) { s.bloom.rimGlow = 0.0f; s.bloom.ambientGlow = 0.0f; });
		t.check(isBlack(none), "(b) rimGlow = 0 and ambientGlow = 0: no glow contribution");

		const Image amb05 = glowImg(sc, [](sakuga::RenderSettings& s) { s.bloom.rimGlow = 0.0f; s.bloom.ambientGlow = 0.5f; });
		const Image amb10 = glowImg(sc, [](sakuga::RenderSettings& s) { s.bloom.rimGlow = 0.0f; s.bloom.ambientGlow = 1.0f; });
		saveEvidence(t, "bloom_ambient_0.5", amb05);
		t.check(!isBlack(amb05), "(b) ambientGlow = 0.5 alone produces bloom");
		t.check(sumLit(amb10) > sumLit(amb05), "(b) ambientGlow = 1.0 glows more than 0.5");

		const Image rimOnly = glowImg(sc, [](sakuga::RenderSettings& s) { s.bloom.rimGlow = 0.59f; s.bloom.ambientGlow = 0.0f; });
		saveEvidence(t, "bloom_rim_only", rimOnly);
		t.check(!isBlack(rimOnly), "(b) rimGlow = 0.59 with rim lighting on (intensity 0.55) alone produces bloom");

		const Image lightingOff = glowImg(sc, [](sakuga::RenderSettings& s) { s.lighting = false; s.bloom.rimGlow = 0.59f; s.bloom.ambientGlow = 0.5f; });
		t.check(isBlack(lightingOff), "(b) Lighting off suppresses bloom even with both glow weights above zero");

		sakuga::Scene noBloomScene = sc;
		noBloomScene.instances[0].receiveBloom = false;
		const Image recvOff = glowImg(noBloomScene, [](sakuga::RenderSettings&) {});
		const Image recvOn = glowImg(sc, [](sakuga::RenderSettings&) {});
		t.check(isBlack(recvOff), "(b) receiveBloom = false: the character stops contributing and the floor stays non-glowing");
		t.check(!isBlack(recvOn), "(b) receiveBloom = true with the default glow weights produces bloom");

		// bloom off: the chain is skipped but the composite still draws the scene
		sakuga::RenderSettings sBloomOff = base;
		sBloomOff.bloomEnabled = false; sBloomOff.bloom.bloomOnly = true;
		const Image bloomOff = renderImage(r, off, sc, view, sBloomOff, W, H);
		sBloomOff.bloom.bloomOnly = false;
		const Image bloomOffPlain = renderImage(r, off, sc, view, sBloomOff, W, H);
		t.check(!isBlack(bloomOff), "(b) Bloom off: the composite still draws the scene");
		t.check(identical(bloomOff, bloomOffPlain), "(b) Bloom off: the bloom-only view has no effect, so the bloom chain contributes nothing");
		sakuga::RenderSettings sBloomOn = base;
		sBloomOn.bloomEnabled = true; sBloomOn.rimLighting = true;
		const Image bloomOn = renderImage(r, off, sc, view, sBloomOn, W, H);
		t.check(countDiff(bloomOn, bloomOffPlain, 0) > 0, "(b) Bloom on changes the final image compared with Bloom off");

		// levels 1, 4, 8
		long lit[3] = { 0, 0, 0 };
		const int levelList[3] = { 1, 4, 8 };
		for (int i = 0; i < 3; ++i)
		{
			const int lv = levelList[i];
			const Image img = glowImg(sc, [lv](sakuga::RenderSettings& s) { s.bloom.levels = lv; });
			lit[i] = countLit(img, 0);
			saveEvidence(t, "bloom_levels_" + std::to_string(lv), img);
		}
		t.note("glowing pixels: levels 1 = " + std::to_string(lit[0]) + ", 4 = " + std::to_string(lit[1]) + ", 8 = " + std::to_string(lit[2]));
		t.check(lit[0] > 0 && lit[1] > lit[0] && lit[2] >= lit[1], "(b) more blur levels spread the glow (levels 1 < 4 <= 8)");

		// ---- (c) rendering state after Renderer::render ----
		auto checkState = [&](const std::string& label, unsigned int expectFbo, int w, int hgt)
		{
			GLint fb = -1; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
			GLint vp[4] = { -1, -1, -1, -1 }; glGetIntegerv(GL_VIEWPORT, vp);
			GLint cullMode = 0; glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
			const bool depth = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
			const bool cull = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
			const bool blend = glIsEnabled(GL_BLEND) == GL_TRUE;
			const bool ok = (unsigned)fb == expectFbo && vp[0] == 0 && vp[1] == 0 && vp[2] == w && vp[3] == hgt &&
				depth && cull && !blend && cullMode == GL_BACK;
			t.check(ok, "(c) " + label + ": target bound, viewport " + std::to_string(vp[2]) + "x" + std::to_string(vp[3]) +
				", depth test " + (depth ? "on" : "off") + ", culling " + (cull ? "on" : "off") + ", blending " + (blend ? "on" : "off") +
				", cull mode " + (cullMode == GL_BACK ? "GL_BACK" : "other"));
		};

		sakuga::RenderSettings sa;			// bloom on, outlines on
		sa.shadowMapping = false;
		off.ensure(W, H);
		r.resize(W, H); r.render(sc, view, sa, off.fbo);
		checkState("bloom on, outlines on", off.fbo, W, H);

		sakuga::RenderSettings sb = sa; sb.bloomEnabled = false; sb.outlines = false;
		r.render(sc, view, sb, off.fbo);
		checkState("bloom off, outlines off", off.fbo, W, H);

		sakuga::RenderSettings sc2 = sa; sc2.shadowMapping = true; sc2.shadow.shadowOnly = true;
		r.render(sc, view, sc2, off.fbo);
		checkState("shadow debug overlay on", off.fbo, W, H);

		r.setMsaa(4);
		r.render(sc, view, sa, off.fbo);
		checkState("4x MSAA resolve path", off.fbo, W, H);
		r.setMsaa(0);

		r.resize(256, 256);
		r.render(sc, view, sa, 0);
		checkState("default framebuffer as target", 0, 256, 256);
	}

	// ============================================================================
	// UT-09  Model replacement and shadow-cache invalidation
	// ============================================================================
	void ut09_modelReplace(TestCtx& t)
	{
		const int W = 256, H = 144;
		sakuga::Renderer r(plainDesc(W, H));
		Offscreen off;
		sakuga::View view = frontView();
		sakuga::RenderSettings s;
		s.shadowMapping = true; s.bloomEnabled = false; s.outlines = false;
		s.shadow.lightDir = glm::vec3(-0.5f, -0.85f, 0.0f);

		sakuga::ModelHandle a = r.loadModel(kModelPath);
		if (!a) { t.check(false, "2B-1.glb loads"); return; }
		sakuga::Scene sc = characterScene(a);

		// frame 1: the shadow pass runs
		renderImage(r, off, sc, view, s, W, H);
		DepthProbe probe;
		if (!probe.attach(r.shadowMapTexture(), kShadowSize)) { t.check(false, "depth inspection framebuffer is complete"); return; }
		t.check(probe.populated() > 0, "frame 1: the shadow pass ran and populated the depth map");

		// frame 2, nothing changed: the pass must be skipped. a marker written into the map survives
		probe.fill(0.5f);
		renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 0.5f) < 1e-3f, "frame 2 (unchanged inputs): the shadow pass is skipped, the marker is untouched");

		// unload A while the scene still references the stale handle: no crash, nothing drawn, cache invalidated
		r.unloadModel(a);
		probe.fill(0.5f);
		const Image stale = renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 1.0f) < 1e-3f, "after unloadModel: the shadow pass runs again (cache invalidated)");
		t.check(probe.populated() == 0, "after unloadModel: no casters are drawn into the depth map");
		sakuga::Scene empty;
		const Image floorOnly = renderImage(r, off, empty, view, s, W, H);
		t.check(identical(stale, floorOnly), "a stale model handle draws nothing (image equals the floor-only scene)");

		// load B (the same file again, a new handle)
		sakuga::ModelHandle b = r.loadModel(kModelPath);
		t.check((bool)b && b.id != a.id, "model B loads with a new handle");
		sc.instances[0].model = b;
		probe.fill(0.5f);
		renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 1.0f) < 1e-3f && probe.populated() > 0, "after loading B: the shadow pass runs and draws the new model");

		probe.fill(0.5f);
		renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 0.5f) < 1e-3f, "next frame, unchanged: the shadow pass is skipped again");

		// changing only the shadow direction refreshes the map exactly once
		s.shadow.lightDir = glm::vec3(-0.3f, -0.9f, 0.2f);
		probe.fill(0.5f);
		renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 1.0f) < 1e-3f, "changed shadow direction: the shadow pass runs");
		probe.fill(0.5f);
		renderImage(r, off, sc, view, s, W, H);
		t.check(std::fabs(probe.texel(0, 0) - 0.5f) < 1e-3f, "same direction on the next frame: no second refresh");

		// a failed load leaves the current model in place
		const Image before = renderImage(r, off, sc, view, s, W, H);
		sakuga::ModelHandle bad;
		{
			CoutCapture cap;
			bad = r.loadModel(std::string(kTmpDir) + "/does_not_exist.glb");
		}
		const Image after = renderImage(r, off, sc, view, s, W, H);
		t.check(!bad, "loadModel on an invalid path returns an invalid handle");
		t.check(identical(before, after), "the current model still renders identically after the failed load");
	}

	// ============================================================================
	// UT-10  Boundary and degenerate inputs
	// ============================================================================
	void ut10_boundaries(TestCtx& t)
	{
		const int W = 128, H = 72;
		sakuga::Renderer r(plainDesc(W, H));
		sakuga::ModelHandle h = r.loadModel(kModelPath);
		if (!h) { t.check(false, "2B-1.glb loads"); return; }

		sakuga::Scene sc = characterScene(h);
		sakuga::View view = frontView();
		Offscreen off;
		sakuga::RenderSettings s;
		s.bloomEnabled = false; s.outlines = false;
		s.shadowMapping = true;

		// (a) zero-length shadow direction falls back to straight down, with no NaN
		s.shadow.lightDir = glm::vec3(0.0f, 0.0f, 0.0f);
		const Image zeroDir = renderImage(r, off, sc, view, s, W, H);
		DepthProbe probe;
		const bool probeOk = probe.attach(r.shadowMapTexture(), kShadowSize);
		const long populated = probeOk ? probe.populated() : -1;
		s.shadow.lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
		const Image downDir = renderImage(r, off, sc, view, s, W, H);
		t.check(identical(zeroDir, downDir), "(a) a zero shadow direction renders identically to the straight-down direction");
		t.check(populated > 0, "(a) the depth map is populated for the zero-direction case (no NaN matrices)");

		// (b) resize(0, 0): render returns without drawing and without a GL error
		off.ensure(64, 64);
		off.fill(0.25f, 0.5f, 0.75f);
		const Image marker = off.read();
		while (glGetError() != GL_NO_ERROR) {}
		r.resize(0, 0);
		r.render(sc, view, s, off.fbo);
		const GLenum err = glGetError();
		const Image afterZero = off.read();
		t.check(identical(marker, afterZero), "(b) render at 0x0 draws nothing (the target keeps its contents)");
		t.check(err == GL_NO_ERROR, "(b) render at 0x0 raises no GL error (got 0x" + std::to_string((int)err) + ")");
		const Image back = renderImage(r, off, sc, view, s, 64, 64);
		t.check(!identical(marker, back), "(b) rendering resumes after resizing back to 64x64");

		// (c) render scale and MSAA clamping
		sakuga::RenderSettings plain;
		plain.shadowMapping = false; plain.bloomEnabled = false; plain.outlines = false;
		r.setRenderScale(0.1f);
		const Image scaleLow = renderImage(r, off, sc, view, plain, W, H);
		r.setRenderScale(0.25f);
		const Image scaleMin = renderImage(r, off, sc, view, plain, W, H);
		r.setRenderScale(1.0f);
		const Image scaleFull = renderImage(r, off, sc, view, plain, W, H);
		t.check(identical(scaleLow, scaleMin), "(c) setRenderScale(0.1) renders identically to 0.25 (clamped)");
		t.check(!identical(scaleMin, scaleFull), "(c) the clamped scale differs from full scale (sanity check)");

		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		std::string log;
		{
			CoutCapture cap;
			r.setMsaa(999);
			renderImage(r, off, sc, view, plain, W, H);
			log = cap.str();
		}
		t.check(r.activeMsaaSamples() == maxSamples, "(c) setMsaa(999) clamps to GL_MAX_SAMPLES (" + std::to_string(r.activeMsaaSamples()) + " of " + std::to_string(maxSamples) + ")");
		t.check(!contains(log, "framebuffer not complete"), "(c) the clamped MSAA targets are complete");
		r.setMsaa(0);

		// (d) bloom levels clamp to 1..8
		sakuga::RenderSettings glow;
		glow.shadowMapping = false; glow.outlines = false; glow.bloomEnabled = true; glow.bloom.bloomOnly = true;
		auto bloomLevels = [&](int levels)
		{
			sakuga::RenderSettings g = glow;
			g.bloom.levels = levels;
			return renderImage(r, off, sc, view, g, W, H);
		};
		t.check(identical(bloomLevels(0), bloomLevels(1)), "(d) Bloom levels 0 renders identically to 1");
		t.check(identical(bloomLevels(99), bloomLevels(8)), "(d) Bloom levels 99 renders identically to 8");

		// (d) toon levels clamp to 2..8 (floor only, top view, ndl 0.3)
		sakuga::Scene floorScene;
		sakuga::RenderSettings toon;
		toon.shadowMapping = false; toon.bloomEnabled = false; toon.outlines = false; toon.rimLighting = false;
		toon.ramp.lowT = 0.2f; toon.ramp.highT = 0.6f; toon.ramp.softness = 0.0f;
		toon.dirLight.direction = glm::vec3(-std::sqrt(1.0f - 0.45f * 0.45f), -0.45f, 0.0f);	// N.L = 0.45
		auto toonLevels = [&](int levels)
		{
			sakuga::RenderSettings g = toon;
			g.ramp.levels = levels;
			return renderImage(r, off, floorScene, topView(), g, W, H);
		};
		t.check(identical(toonLevels(0), toonLevels(2)) && identical(toonLevels(1), toonLevels(2)), "(d) toon levels 0 and 1 render identically to 2");
		t.check(identical(toonLevels(9), toonLevels(8)) && identical(toonLevels(99), toonLevels(8)), "(d) toon levels 9 and 99 render identically to 8");
		t.check(!identical(toonLevels(2), toonLevels(8)), "(d) toon levels 2 and 8 differ (sanity check)");
	}

	// ============================================================================
	// runner
	// ============================================================================
	struct TestDef
	{
		const char* id;
		const char* title;
		void (*fn)(TestCtx&);
	};

	const TestDef kTests[] = {
		{ "UT-01", "GLSL shader loader and compiler", ut01_shaders },
		{ "UT-02", "Assimp and stb_image model loader", ut02_modelLoad },
		{ "UT-03", "Invalid-asset fault handling", ut03_invalidAssets },
		{ "UT-04", "Camera controls and frame data preparation", ut04_camera },
		{ "UT-05", "Toon quantization and color ramp", ut05_toon },
		{ "UT-06", "Inverted-hull outline geometry", ut06_outline },
		{ "UT-07", "Shadow depth map and PCF pass", ut07_shadow },
		{ "UT-08", "Rim light and bloom pass", ut08_rimBloom },
		{ "UT-09", "Model replacement and shadow-cache invalidation", ut09_modelReplace },
		{ "UT-10", "Boundary and degenerate inputs", ut10_boundaries },
	};

	std::string glString(GLenum name)
	{
		const GLubyte* s = glGetString(name);
		return s ? std::string((const char*)s) : std::string("(unavailable)");
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> only;
	for (int i = 1; i < argc; ++i) only.push_back(argv[i]);

	fs::create_directories(kResultsDir);
	fs::create_directories(kTmpDir);

	if (!glfwInit()) { std::cout << "glfwInit failed\n"; return 2; }
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(256, 256, "unit tests", nullptr, nullptr);
	if (!window) { std::cout << "could not create an OpenGL 3.3 Core context\n"; glfwTerminate(); return 2; }
	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "gladLoadGLLoader failed\n"; glfwTerminate(); return 2; }

	GLint maxSamples = 0;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
	char when[64];
	{ std::time_t now = std::time(nullptr); std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now)); }

	std::ostringstream report;
	report << "Unit test run: " << when << "\n"
		   << "GL_VERSION:  " << glString(GL_VERSION) << "\n"
		   << "GL_RENDERER: " << glString(GL_RENDERER) << "\n"
		   << "GL_VENDOR:   " << glString(GL_VENDOR) << "\n"
		   << "GL_MAX_SAMPLES: " << maxSamples << "\n\n";

	struct Row { std::string id, outcome, actual; };
	std::vector<Row> rows;
	int failures = 0;

	for (const TestDef& def : kTests)
	{
		if (!only.empty() && std::find(only.begin(), only.end(), def.id) == only.end())
			continue;

		TestCtx t;
		t.id = def.id;
		std::cout << "running " << def.id << " " << def.title << " ..." << std::flush;
		try
		{
			def.fn(t);
		}
		catch (const std::exception& e)
		{
			t.check(false, std::string("exception: ") + e.what());
		}
		catch (...)
		{
			t.check(false, "unknown exception");
		}

		// a pending GL error is recorded as information; tests that require "no GL error" check it themselves
		std::string glErrors;
		for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) glErrors += " 0x" + std::to_string((int)e);
		if (!glErrors.empty()) t.note("GL errors pending at the end of the test:" + glErrors);

		const std::string outcome = t.failed ? "Fail" : (t.blocked ? "Blocked" : "Pass");
		if (t.failed) ++failures;
		std::cout << " " << outcome << "\n";

		std::string actual;
		if (t.failed)
		{
			for (const std::string& l : t.log) if (l.find("[FAIL]") != std::string::npos) { actual = l.substr(l.find("[FAIL]") + 7); break; }
		}
		else if (t.blocked)
		{
			for (const std::string& l : t.log) if (l.find("[blocked]") != std::string::npos) { actual = l.substr(l.find("[blocked]") + 10); break; }
		}
		else
			actual = std::to_string(t.passed) + " checks passed";
		rows.push_back({ t.id, outcome, actual });

		report << "=== " << def.id << "  " << def.title << "  ->  " << outcome << " ===\n";
		for (const std::string& l : t.log) report << l << "\n";
		for (const std::string& e : t.evidence) report << "  evidence: " << e << "\n";
		report << "\n";
	}

	report << "---- rows for Table 9 (Test ID | Outcome | Actual result) ----\n";
	for (const Row& row : rows)
		report << row.id << " | " << row.outcome << " | " << row.actual << "\n";

	const std::string reportPath = std::string(kResultsDir) + "/unit_test_results.txt";
	{ std::ofstream f(reportPath); f << report.str(); }
	std::cout << "\nresults written to " << reportPath << "\n";
	for (const Row& row : rows) std::cout << "  " << row.id << "  " << row.outcome << "  " << row.actual << "\n";

	glfwDestroyWindow(window);
	glfwTerminate();
	return failures == 0 ? 0 : 1;
}