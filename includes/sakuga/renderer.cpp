#include <sakuga/renderer.h>

#include <sakuga/gpu_timer.h>
#include <sakuga/model.h>
#include <sakuga/shader.h>
#include <sakuga/uniform_buffer.h>

#include <stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>

namespace sakuga
{
	namespace
	{
		// every HDR target is R11G11B10F: 4 bytes per pixel instead of 8 for RGBA16F. nothing here needs
		// alpha, and the unsigned 11/10-bit floats are fine for the 0..few range this scene uses.
		constexpr GLenum HDR_FORMAT = GL_R11F_G11F_B10F;

		// shadow camera: orthographic box looking at the origin from this far away
		constexpr float LIGHT_NEAR = 1.0f;
		constexpr float LIGHT_FAR = 20.0f;
		constexpr float LIGHT_DISTANCE = 7.0f;

		// GL allows one timer at a time, so these run one after another, never nested
		enum GpuZone { ZONE_SHADOW, ZONE_SCENE, ZONE_OUTLINE, ZONE_BLOOM, ZONE_COMPOSITE, ZONE_COUNT };

		std::string joinPath(const std::string& dir, const char* file)
		{
			if (dir.empty())
				return file;
			const char last = dir.back();
			return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
		}

		void checkFramebuffer(const char* name)
		{
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "framebuffer not complete: " << name << std::endl;
		}

		// an HDR color texture
		void createHdrTexture(unsigned int& tex, int w, int h)
		{
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, HDR_FORMAT, w, h, 0, GL_RGB, GL_FLOAT, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}

		// an FBO whose only attachment is an HDR color texture
		void createColorTarget(unsigned int& fbo, unsigned int& tex, int w, int h, const char* name)
		{
			createHdrTexture(tex, w, h);
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
			checkFramebuffer(name);
		}

		// frees the GL objects a model owns: its textures and every mesh's VAO/VBO/EBO
		void releaseModel(Model& model)
		{
			for (Texture& t : model.textures_loaded)
				if (t.id) glDeleteTextures(1, &t.id);
			model.textures_loaded.clear();

			for (Mesh& m : model.meshes)
				m.release();
		}

		// per-draw matrices. the normal matrix is built once per draw, not per vertex in the shader
		void setModelMatrices(Shader& shader, const glm::mat4& model)
		{
			shader.setMat4("model", model);
			shader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
		}

		struct Caster
		{
			const Model* model;
			glm::mat4 matrix;
		};

		bool sameCasters(const std::vector<Caster>& a, const std::vector<Caster>& b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].model != b[i].model || a[i].matrix != b[i].matrix)
					return false;
			return true;
		}

		// returns 0 on failure (the caller decides what to do about it)
		unsigned int loadTexture2D(const std::string& path)
		{
			int width, height, nrComponents;
			unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrComponents, 4);
			if (!data)
			{
				std::cout << "failed to load texture: " << path << std::endl;
				return 0;
			}

			unsigned int tex;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			glGenerateMipmap(GL_TEXTURE_2D);
			stbi_image_free(data);
			return tex;
		}
	}

	// ============================================================================
	// Impl: every GL object and all internal state
	// ============================================================================
	struct Renderer::Impl
	{
		explicit Impl(const RendererDesc& desc);
		~Impl();

		// ---- configuration ----
		int shadowSize;
		int outW, outH;					// output (window) size
		int requestedSamples;			// msaa as requested; targets rebuild when it differs from allocSamples
		float renderScale;

		// ---- programs ----
		Shader lit, outlineShader, depthShader, debugDepthShader, downsampleShader, upsampleShader, compositeShader;

		// ---- per-frame GPU data ----
		UniformBuffer<FrameData> frameUBO;
		UniformBuffer<LightingData> lightingUBO;
		GpuTimer gpuTimer;

		// ---- geometry ----
		unsigned int quadVAO = 0, quadVBO = 0;
		unsigned int planeVAO = 0, planeVBO = 0, planeEBO = 0;

		// ---- shadow map (re-rendered only when the light or a caster moves) ----
		unsigned int depthFBO = 0, depthTex = 0;
		std::vector<Caster> cachedCasters;
		glm::mat4 cachedLightSpace = glm::mat4(0.0f);
		bool shadowValid = false;

		// ---- HDR / bloom targets ----
		//   sceneFBO    multisampled target the scene is drawn into. two outputs:
		//               attachment 0 = the lit scene, attachment 1 = glow mask (only what should bloom)
		//   resolveFBO  single-sample copy of it (MSAA textures can't be sampled by the post passes)
		//   bloomFBO[]  dual-filter mip chain: level 0 is half resolution, each level half the one before.
		//               downsampled one level at a time, then upsampled back up with additive blending
		int renderW = 0, renderH = 0;	// render resolution the targets were last allocated for
		int allocSamples = -1;			// msaa setting the targets were last allocated for (as requested)
		int activeSamples = 0;			// samples actually in use (the request clamped to the GPU limit)
		unsigned int sceneFBO = 0, sceneColorRBO = 0, sceneDepthRBO = 0;
		unsigned int sceneGlowMaskRBO = 0;
		unsigned int resolveFBO = 0, resolveTex = 0;
		unsigned int glowMaskResolveFBO = 0, glowMaskResolveTex = 0;
		unsigned int bloomFBO[BLOOM_MAX_LEVELS] = {};
		unsigned int bloomTex[BLOOM_MAX_LEVELS] = {};

		// ---- owned resources ----
		std::unordered_map<std::uint32_t, std::unique_ptr<Model>> models;
		std::unordered_map<std::uint32_t, unsigned int> textures;
		std::uint32_t nextId = 1;
		unsigned int whiteTex = 0;		// stands in for a missing floor texture

		// size of mip level i (level 0 = half resolution)
		int levelW(int i) const { return std::max(1, renderW >> (i + 1)); }
		int levelH(int i) const { return std::max(1, renderH >> (i + 1)); }

		Model* findModel(ModelHandle h)
		{
			auto it = models.find(h.id);
			return it == models.end() ? nullptr : it->second.get();
		}

		void createGeometry();
		void releaseGeometry();
		void renderQuad();
		void renderPlane();

		void createDepthMap();
		void releaseDepthMap();
		void renderShadowMap(const glm::mat4& lightSpace, const std::vector<Caster>& casters);

		void createTargets(int w, int h, int samples);
		void releaseTargets();
		void ensureTargets();
	};

	Renderer::Impl::Impl(const RendererDesc& d)
		: shadowSize(std::max(64, d.shadowMapSize)),
		outW(d.width), outH(d.height),
		requestedSamples(std::max(0, d.msaaSamples)),
		renderScale(std::clamp(d.renderScale, 0.25f, 1.0f)),
		lit(joinPath(d.shaderDir, "light.vs.glsl").c_str(), joinPath(d.shaderDir, "light.fs.glsl").c_str()),
		outlineShader(joinPath(d.shaderDir, "outline.vs.glsl").c_str(), joinPath(d.shaderDir, "outline.fs.glsl").c_str()),
		depthShader(joinPath(d.shaderDir, "depth.vs.glsl").c_str(), joinPath(d.shaderDir, "depth.fs.glsl").c_str()),
		debugDepthShader(joinPath(d.shaderDir, "fullscreen_quad.vs.glsl").c_str(), joinPath(d.shaderDir, "debug_depth_quad.fs.glsl").c_str()),
		downsampleShader(joinPath(d.shaderDir, "fullscreen_quad.vs.glsl").c_str(), joinPath(d.shaderDir, "bloom_downsample.fs.glsl").c_str()),
		upsampleShader(joinPath(d.shaderDir, "fullscreen_quad.vs.glsl").c_str(), joinPath(d.shaderDir, "bloom_upsample.fs.glsl").c_str()),
		compositeShader(joinPath(d.shaderDir, "fullscreen_quad.vs.glsl").c_str(), joinPath(d.shaderDir, "bloom_composite.fs.glsl").c_str()),
		frameUBO(ubo::FRAME_BINDING),
		lightingUBO(ubo::LIGHTING_BINDING),
		gpuTimer(ZONE_COUNT)
	{
		// uniform blocks: the programs remember the binding, so this is done once
		lit.bindUniformBlock("FrameBlock", ubo::FRAME_BINDING);
		lit.bindUniformBlock("LightingBlock", ubo::LIGHTING_BINDING);
		outlineShader.bindUniformBlock("FrameBlock", ubo::FRAME_BINDING);

		// sampler units, set once
		lit.use();
		lit.setInt("texture_diffuse1", 0);
		lit.setInt("shadowMap", 1);
		outlineShader.use();
		outlineShader.setInt("texture_diffuse1", 0);
		debugDepthShader.use();
		debugDepthShader.setInt("depthMap", 0);
		downsampleShader.use();
		downsampleShader.setInt("image", 0);
		upsampleShader.use();
		upsampleShader.setInt("image", 0);
		compositeShader.use();
		compositeShader.setInt("scene", 0);
		compositeShader.setInt("bloomBlur", 1);

		// 1x1 white texture for a floor without a texture
		const unsigned char white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &whiteTex);
		glBindTexture(GL_TEXTURE_2D, whiteTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		createGeometry();
		createDepthMap();
	}

	Renderer::Impl::~Impl()
	{
		for (auto& entry : models)
			releaseModel(*entry.second);
		models.clear();
		for (auto& entry : textures)
			glDeleteTextures(1, &entry.second);
		textures.clear();
		if (whiteTex) { glDeleteTextures(1, &whiteTex); whiteTex = 0; }

		releaseDepthMap();
		releaseTargets();
		releaseGeometry();

		// the UBOs and the GPU timer release themselves in their destructors
		glDeleteProgram(lit.ID);
		glDeleteProgram(outlineShader.ID);
		glDeleteProgram(depthShader.ID);
		glDeleteProgram(debugDepthShader.ID);
		glDeleteProgram(downsampleShader.ID);
		glDeleteProgram(upsampleShader.ID);
		glDeleteProgram(compositeShader.ID);
	}

	// ---- geometry ---------------------------------------------------------------

	void Renderer::Impl::createGeometry()
	{
		// fullscreen quad (triangle strip) in NDC
		const float quadVertices[] = {
			// positions			// uv
			-1.0f,  1.0f, 0.0f,		0.0f, 1.0f,		// top left
			-1.0f, -1.0f, 0.0f,		0.0f, 0.0f,		// bottom left
			 1.0f,  1.0f, 0.0f,		1.0f, 1.0f,		// top right
			 1.0f, -1.0f, 0.0f,		1.0f, 0.0f,		// bottom right
		};
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

		// 2x2 plane facing +Z (uv tiles 3x3), scaled/rotated into the floor by the model matrix
		const float vertices[] = {
			//  position           	normal					uv
			 1.0f,  1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 3.0f,  // top right
			 1.0f, -1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		3.0f, 0.0f,  // bottom right
			-1.0f, -1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 0.0f,  // bottom left
			-1.0f,  1.0f, 0.0f,		0.0f, 0.0f, 1.0f,		0.0f, 3.0f   // top left
		};
		const unsigned int indices[] = {
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
		// model meshes store normals packed as 2_10_10_10, but the plane keeps plain floats.
		// the shader's `vec3 aNormal` input reads either layout
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(2);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	void Renderer::Impl::releaseGeometry()
	{
		if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
		if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
		if (planeVAO) { glDeleteVertexArrays(1, &planeVAO); planeVAO = 0; }
		if (planeVBO) { glDeleteBuffers(1, &planeVBO); planeVBO = 0; }
		if (planeEBO) { glDeleteBuffers(1, &planeEBO); planeEBO = 0; }
	}

	void Renderer::Impl::renderQuad()
	{
		glBindVertexArray(quadVAO);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindVertexArray(0);
	}

	void Renderer::Impl::renderPlane()
	{
		glBindVertexArray(planeVAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
	}

	// ---- shadow map -------------------------------------------------------------

	void Renderer::Impl::createDepthMap()
	{
		glGenFramebuffers(1, &depthFBO);

		// depth texture
		glGenTextures(1, &depthTex);
		glBindTexture(GL_TEXTURE_2D, depthTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

		// attach it as the FBO's depth buffer
		glBindFramebuffer(GL_FRAMEBUFFER, depthFBO);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Renderer::Impl::releaseDepthMap()
	{
		if (depthFBO) { glDeleteFramebuffers(1, &depthFBO); depthFBO = 0; }
		if (depthTex) { glDeleteTextures(1, &depthTex); depthTex = 0; }
	}

	void Renderer::Impl::renderShadowMap(const glm::mat4& lightSpace, const std::vector<Caster>& casters)
	{
		depthShader.use();
		depthShader.setMat4("lightSpaceMatrix", lightSpace);

		glCullFace(GL_FRONT);
		glViewport(0, 0, shadowSize, shadowSize);
		glBindFramebuffer(GL_FRAMEBUFFER, depthFBO);
		glClear(GL_DEPTH_BUFFER_BIT);
		for (const Caster& c : casters)
		{
			depthShader.setMat4("model", c.matrix);
			c.model->DrawGeometry();	// the depth shader has no textures, so no texture binds
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glCullFace(GL_BACK);
	}

	// ---- HDR / bloom targets ----------------------------------------------------

	void Renderer::Impl::releaseTargets()
	{
		if (sceneFBO) { glDeleteFramebuffers(1, &sceneFBO); sceneFBO = 0; }
		if (sceneColorRBO) { glDeleteRenderbuffers(1, &sceneColorRBO); sceneColorRBO = 0; }
		if (sceneDepthRBO) { glDeleteRenderbuffers(1, &sceneDepthRBO); sceneDepthRBO = 0; }
		if (sceneGlowMaskRBO) { glDeleteRenderbuffers(1, &sceneGlowMaskRBO); sceneGlowMaskRBO = 0; }
		if (resolveFBO) { glDeleteFramebuffers(1, &resolveFBO); resolveFBO = 0; }
		if (resolveTex) { glDeleteTextures(1, &resolveTex); resolveTex = 0; }
		if (glowMaskResolveFBO) { glDeleteFramebuffers(1, &glowMaskResolveFBO); glowMaskResolveFBO = 0; }
		if (glowMaskResolveTex) { glDeleteTextures(1, &glowMaskResolveTex); glowMaskResolveTex = 0; }
		for (int i = 0; i < BLOOM_MAX_LEVELS; ++i)
		{
			if (bloomFBO[i]) { glDeleteFramebuffers(1, &bloomFBO[i]); bloomFBO[i] = 0; }
			if (bloomTex[i]) { glDeleteTextures(1, &bloomTex[i]); bloomTex[i] = 0; }
		}
		renderW = renderH = 0;
	}

	// (re)creates every scene/bloom target for a w x h render resolution. samples = 0 turns msaa off.
	void Renderer::Impl::createTargets(int w, int h, int samples)
	{
		releaseTargets();

		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		allocSamples = samples;
		samples = std::min(samples, (int)maxSamples);
		activeSamples = samples;

		glGenFramebuffers(1, &sceneFBO);
		glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
		const GLenum sceneBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

		if (samples > 0)
		{
			// 1st color attachment: lit msaa HDR color scene
			glGenRenderbuffers(1, &sceneColorRBO);
			glBindRenderbuffer(GL_RENDERBUFFER, sceneColorRBO);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, HDR_FORMAT, w, h);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorRBO);

			// 2nd color attachment: glow mask
			glGenRenderbuffers(1, &sceneGlowMaskRBO);
			glBindRenderbuffer(GL_RENDERBUFFER, sceneGlowMaskRBO);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, HDR_FORMAT, w, h);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, sceneGlowMaskRBO);

			// depth
			glGenRenderbuffers(1, &sceneDepthRBO);
			glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);
			glDrawBuffers(2, sceneBuffers);
			checkFramebuffer("scene (msaa)");

			// single-sample resolve targets
			createColorTarget(resolveFBO, resolveTex, w, h, "resolve");
			createColorTarget(glowMaskResolveFBO, glowMaskResolveTex, w, h, "glow resolve");
		}
		else
		{
			// no msaa: the scene is drawn straight into the textures the post passes read,
			// so there are no resolve copies at all (the cheapest option for integrated GPUs)
			createHdrTexture(resolveTex, w, h);
			createHdrTexture(glowMaskResolveTex, w, h);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, glowMaskResolveTex, 0);

			glGenRenderbuffers(1, &sceneDepthRBO);
			glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);
			glDrawBuffers(2, sceneBuffers);
			checkFramebuffer("scene (no msaa)");
		}

		// dual-filter mip chain: half resolution, then halving each level. always allocated at full
		// depth (it is tiny next to the scene targets), Bloom::levels just picks how many are used
		renderW = w;
		renderH = h;
		for (int i = 0; i < BLOOM_MAX_LEVELS; ++i)
			createColorTarget(bloomFBO[i], bloomTex[i], levelW(i), levelH(i), "bloom mip");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Renderer::Impl::ensureTargets()
	{
		const int rw = std::max(1, (int)std::lround(outW * renderScale));
		const int rh = std::max(1, (int)std::lround(outH * renderScale));
		if (rw != renderW || rh != renderH || requestedSamples != allocSamples)
			createTargets(rw, rh, requestedSamples);
	}

	// ============================================================================
	// Renderer: public interface
	// ============================================================================

	Renderer::Renderer(const RendererDesc& desc) : impl(std::make_unique<Impl>(desc)) {}
	Renderer::~Renderer() = default;
	Renderer::Renderer(Renderer&&) noexcept = default;
	Renderer& Renderer::operator=(Renderer&&) noexcept = default;

	// ---- resources --------------------------------------------------------------

	ModelHandle Renderer::loadModel(const std::string& path)
	{
		auto model = std::make_unique<Model>(path);
		if (model->meshes.empty())
		{
			releaseModel(*model);
			return ModelHandle();
		}
		ModelHandle h;
		h.id = impl->nextId++;
		impl->models.emplace(h.id, std::move(model));
		return h;
	}

	void Renderer::unloadModel(ModelHandle model)
	{
		auto it = impl->models.find(model.id);
		if (it == impl->models.end())
			return;
		releaseModel(*it->second);
		impl->models.erase(it);
		impl->shadowValid = false;	// the cached casters may point at the model that just went away
	}

	TextureHandle Renderer::loadTexture(const std::string& path)
	{
		const unsigned int tex = loadTexture2D(path);
		if (!tex)
			return TextureHandle();
		TextureHandle h;
		h.id = impl->nextId++;
		impl->textures.emplace(h.id, tex);
		return h;
	}

	void Renderer::unloadTexture(TextureHandle texture)
	{
		auto it = impl->textures.find(texture.id);
		if (it == impl->textures.end())
			return;
		glDeleteTextures(1, &it->second);
		impl->textures.erase(it);
	}

	// ---- output + quality -------------------------------------------------------

	void Renderer::resize(int width, int height)
	{
		impl->outW = std::max(0, width);
		impl->outH = std::max(0, height);
	}

	void Renderer::setMsaa(int samples) { impl->requestedSamples = std::max(0, samples); }
	void Renderer::setRenderScale(float scale) { impl->renderScale = std::clamp(scale, 0.25f, 1.0f); }
	int Renderer::width() const { return impl->outW; }
	int Renderer::height() const { return impl->outH; }
	int Renderer::activeMsaaSamples() const { return impl->activeSamples; }

	// ---- drawing ----------------------------------------------------------------

	void Renderer::render(const Scene& scene, const View& view, const RenderSettings& s, unsigned int targetFramebuffer)
	{
		Impl& d = *impl;

		// nothing to draw into (e.g. a minimized window): a 0x0 framebuffer is invalid
		if (d.outW <= 0 || d.outH <= 0)
			return;

		d.ensureTargets();
		d.gpuTimer.nextFrame();

		// don't trust the host's GL state
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glFrontFace(GL_CCW);
		glDisable(GL_BLEND);
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// background. the scene buffer is linear and gets gamma-encoded in the composite pass,
		// so the picked (sRGB) color has to be converted to linear here to look the same on screen
		const glm::vec3 clearLinear = glm::pow(s.clearColor, glm::vec3(2.2f));
		glClearColor(clearLinear.r, clearLinear.g, clearLinear.b, 1.0f);

		// ---- shadow camera: orthographic, looking at the origin ----
		// normalize once (falling back to straight down if the direction is degenerate) and use
		// the normalized vector for both the position and the up-vector test
		const glm::vec3 lightDirN = (glm::length(s.shadow.lightDir) > 1e-4f)
			? glm::normalize(s.shadow.lightDir)
			: glm::vec3(0.0f, -1.0f, 0.0f);
		const glm::vec3 lightPos = -lightDirN * LIGHT_DISTANCE;
		const glm::vec3 up = (std::abs(lightDirN.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const float ext = std::max(0.01f, s.shadowExtent);
		const glm::mat4 lightProjection = glm::ortho(-ext, ext, -ext, ext, LIGHT_NEAR, LIGHT_FAR);
		const glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
		const glm::mat4 lightSpace = lightProjection * lightView;

		// ---- main camera ----
		const float aspect = (float)d.outW / (float)d.outH;
		const glm::mat4 projection = glm::perspective(glm::radians(view.fovDegrees), aspect, view.nearPlane, view.farPlane);

		// ---- shadow pass ----
		// only instances with castShadow draw into the depth map, and it only depends on the shadow
		// direction and those instances' transforms, so it is re-rendered only when one of them
		// changes (not every frame). skipped completely while shadow mapping is off
		std::vector<Caster> casters;
		for (const ModelInstance& inst : scene.instances)
			if (inst.castShadow)
				if (Model* m = d.findModel(inst.model))
					casters.push_back({ m, inst.transform });

		if (s.shadowMapping && (!d.shadowValid || lightSpace != d.cachedLightSpace || !sameCasters(casters, d.cachedCasters)))
		{
			d.gpuTimer.begin(ZONE_SHADOW);
			d.renderShadowMap(lightSpace, casters);
			d.gpuTimer.end(ZONE_SHADOW);
			d.cachedLightSpace = lightSpace;
			d.cachedCasters = casters;
			d.shadowValid = true;
		}

		// the glow mask only holds the rim light. with lighting or rim lighting off (or zero rim
		// intensity) it would be all black, so the second render target and the whole bloom
		// chain are skipped
		const bool bloomActive = s.postProcessing && s.bloomEnabled;
		const bool hasGlow = s.lighting && s.rimLighting && s.rim.intensity > 0.0f;
		const bool glowPass = bloomActive && hasGlow;
		const GLenum sceneBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		const int sceneBufferCount = glowPass ? 2 : 1;

		// ---- scene pass: everything until the composite draws into the HDR scene target ----
		// (at render resolution, which is smaller than the output when renderScale < 1)
		d.gpuTimer.begin(ZONE_SCENE);
		glBindFramebuffer(GL_FRAMEBUFFER, d.sceneFBO);
		glDrawBuffers(sceneBufferCount, sceneBuffers);
		glViewport(0, 0, d.renderW, d.renderH);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (glowPass)
		{
			const float noGlow[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			glClearBufferfv(GL_COLOR, 1, noGlow);	// glClear used the background color: reset glow mask to black
		}

		// the shadow map lives on unit 1 for the whole pass, so the sampler never points at an empty unit.
		// meshes only ever bind their diffuse texture to unit 0
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, d.depthTex);
		glActiveTexture(GL_TEXTURE0);

		// per-frame uniforms: two uploads instead of ~30 uniform calls
		d.frameUBO.update(shader_config::makeFrameData(projection, view.view, lightSpace, view.position));
		d.lightingUBO.update(shader_config::makeLightingData(s.dirLight, s.lightColor, s.material, s.ramp, s.rim,
			s.lighting, s.toonShading, s.rimLighting, s.shadowMapping));

		d.lit.use();
		for (const ModelInstance& inst : scene.instances)
		{
			Model* m = d.findModel(inst.model);
			if (!m)
				continue;
			d.lit.setBool("gReceiveShadowMapping", inst.receiveShadow);
			d.lit.setBool("gReceiveRimLighting", inst.receiveRim);
			setModelMatrices(d.lit, inst.transform);
			m->Draw();
		}

		if (scene.floor.enabled)
		{
			glm::mat4 floorModel = glm::mat4(1.0f);
			floorModel = glm::translate(floorModel, glm::vec3(0.0f, scene.floor.y, 0.0f));
			floorModel = glm::rotate(floorModel, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			floorModel = glm::scale(floorModel, glm::vec3(scene.floor.halfWidth, scene.floor.halfWidth, 1.0f));

			unsigned int floorTex = d.whiteTex;
			auto it = d.textures.find(scene.floor.texture.id);
			if (it != d.textures.end())
				floorTex = it->second;

			d.lit.setBool("gReceiveShadowMapping", true);
			d.lit.setBool("gReceiveRimLighting", false);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, floorTex);
			setModelMatrices(d.lit, floorModel);
			d.renderPlane();
		}
		d.gpuTimer.end(ZONE_SCENE);

		// ---- outlines: back faces pushed out along the normal ----
		if (s.outlines)
		{
			d.gpuTimer.begin(ZONE_OUTLINE);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);	// don't touch the glow mask
			glCullFace(GL_FRONT);

			d.outlineShader.use();
			shader_config::applyOutline(d.outlineShader, s.outline);
			for (const ModelInstance& inst : scene.instances)
			{
				Model* m = d.findModel(inst.model);
				if (!m || !inst.outlined)
					continue;
				setModelMatrices(d.outlineShader, inst.transform);
				m->Draw();
			}

			glCullFace(GL_BACK);
			glDrawBuffers(sceneBufferCount, sceneBuffers);
			d.gpuTimer.end(ZONE_OUTLINE);
		}

		// ---- bloom ----
		d.gpuTimer.begin(ZONE_BLOOM);
		if (d.activeSamples > 0)
		{
			// msaa: resolve into single-sample textures the post passes can sample.
			// without msaa the scene was already drawn straight into those textures
			glBindFramebuffer(GL_READ_FRAMEBUFFER, d.sceneFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d.resolveFBO);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glBlitFramebuffer(0, 0, d.renderW, d.renderH, 0, 0, d.renderW, d.renderH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			if (glowPass)
			{
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d.glowMaskResolveFBO);
				glReadBuffer(GL_COLOR_ATTACHMENT1);
				glBlitFramebuffer(0, 0, d.renderW, d.renderH, 0, 0, d.renderW, d.renderH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			}
			glReadBuffer(GL_COLOR_ATTACHMENT0);
		}

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		const int bloomLevels = std::clamp(s.bloom.levels, 1, BLOOM_MAX_LEVELS);
		if (glowPass)
		{
			glActiveTexture(GL_TEXTURE0);

			// downsample: glow mask -> level 0 (half res) -> level 1 -> ...
			d.downsampleShader.use();
			for (int i = 0; i < bloomLevels; ++i)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, d.bloomFBO[i]);
				glViewport(0, 0, d.levelW(i), d.levelH(i));
				glBindTexture(GL_TEXTURE_2D, i == 0 ? d.glowMaskResolveTex : d.bloomTex[i - 1]);
				d.renderQuad();
			}

			// upsample: walk back up from the smallest level, adding each one onto the level above it
			d.upsampleShader.use();
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			for (int i = bloomLevels - 1; i > 0; --i)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, d.bloomFBO[i - 1]);
				glViewport(0, 0, d.levelW(i - 1), d.levelH(i - 1));
				glBindTexture(GL_TEXTURE_2D, d.bloomTex[i]);
				d.renderQuad();
			}
			glDisable(GL_BLEND);
		}
		d.gpuTimer.end(ZONE_BLOOM);

		// ---- composite to the output. the only full-resolution pass: with renderScale < 1
		// the scene texture is bilinearly upscaled here ----
		d.gpuTimer.begin(ZONE_COMPOSITE);
		glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
		glViewport(0, 0, d.outW, d.outH);
		d.compositeShader.use();
		d.compositeShader.setBool("bloomEnabled", glowPass);
		// every level is added into level 0, so divide by the level count to keep the total glow
		// energy the same however many levels are used
		d.compositeShader.setFloat("bloomIntensity", s.bloom.intensity / (float)bloomLevels);
		d.compositeShader.setBool("bloomOnly", bloomActive && s.bloom.bloomOnly);	// nothing glowing = black, as expected
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, d.resolveTex);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, d.bloomTex[0]);
		d.renderQuad();
		glActiveTexture(GL_TEXTURE0);
		d.gpuTimer.end(ZONE_COMPOSITE);

		// ---- debug overlay: the shadow depth map ----
		if (s.shadowMapping && s.shadow.shadowOnly)
		{
			d.debugDepthShader.use();
			d.debugDepthShader.setFloat("near_plane", LIGHT_NEAR);
			d.debugDepthShader.setFloat("far_plane", LIGHT_FAR);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, d.depthTex);
			d.renderQuad();
		}

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
	}

	// ---- diagnostics ------------------------------------------------------------

	GpuTimings Renderer::gpuTimings() const
	{
		GpuTimings t;
		t.shadow = impl->gpuTimer.ms(ZONE_SHADOW);
		t.scene = impl->gpuTimer.ms(ZONE_SCENE);
		t.outline = impl->gpuTimer.ms(ZONE_OUTLINE);
		t.bloom = impl->gpuTimer.ms(ZONE_BLOOM);
		t.composite = impl->gpuTimer.ms(ZONE_COMPOSITE);
		return t;
	}

	unsigned int Renderer::shadowMapTexture() const { return impl->depthTex; }

}	// namespace sakuga