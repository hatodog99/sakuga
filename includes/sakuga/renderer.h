#ifndef SAKUGA_RENDERER_H
#define SAKUGA_RENDERER_H

// sakuga renderer: toon-shaded scene -> shadow map -> HDR scene -> bloom -> composite.
//
// The host app owns the window and the OpenGL context. The renderer only needs:
//   * a current OpenGL 3.3 core context with glad already loaded
//   * the output size in pixels (resize())
// It never touches GLFW, ImGui or input. Typical use:
//
//   {
//       sakuga::Renderer renderer(desc);                       // after the context exists
//       sakuga::ModelHandle hero = renderer.loadModel("hero.glb");
//
//       sakuga::Scene scene;
//       scene.instances.push_back({ hero });                   // transform etc. have defaults
//       sakuga::RenderSettings settings;
//
//       while (running) {
//           renderer.resize(fbWidth, fbHeight);
//           scene.instances[0].transform = ...;
//           renderer.render(scene, view, settings);            // leaves the output framebuffer bound
//           drawYourUiHere();
//           swapBuffers();
//       }
//   }                                                          // renderer destroyed BEFORE the context
//
// Threading: every call must happen on the thread that owns the GL context.
//
// Shader contract (files in RendererDesc::shaderDir):
//   light.vs/fs, outline.vs/fs, depth.vs/fs, fullscreen_quad.vs, debug_depth_quad.fs,
//   bloom_downsample.fs, bloom_upsample.fs, bloom_composite.fs  (all *.glsl)
//   UBO blocks: FrameBlock (binding 0), LightingBlock (binding 1), see shader_config.h
//   per-draw uniforms: model, normalMatrix, gReceiveShadowMapping, gReceiveRimLighting

#include <glm/glm.hpp>

#include <sakuga/shader_config.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sakuga
{
    // upper bound for Bloom::levels
    constexpr int BLOOM_MAX_LEVELS = 8;

    // ---- handles ------------------------------------------------------------
    // opaque ids for resources the renderer owns. id 0 = invalid.
    struct ModelHandle
    {
        std::uint32_t id = 0;
        explicit operator bool() const { return id != 0; }
    };

    struct TextureHandle
    {
        std::uint32_t id = 0;
        explicit operator bool() const { return id != 0; }
    };

    // ---- creation options ---------------------------------------------------
    struct RendererDesc
    {
        int width = 1024;                               // output size in pixels
        int height = 576;
        std::string shaderDir = "resources/shaders";
        int msaaSamples = 4;                            // 0 = off (clamped to what the GPU supports)
        float renderScale = 1.0f;                       // <1 renders the 3D scene smaller and upscales it
        int shadowMapSize = 2048;                       // shadow map is square
    };

    // ---- scene description --------------------------------------------------
    struct ModelInstance
    {
        ModelHandle model;
        glm::mat4 transform = glm::mat4(1.0f);
        bool castShadow = true;
        bool receiveShadow = false;
        bool receiveRim = true;
        bool outlined = true;
    };

    // a textured square drawn at height y, facing up. receives shadows, never outlined
    struct Floor
    {
        bool enabled = true;
        float y = -0.8f;
        float halfWidth = 2.0f;         // 2.0 = 4x4 units
        TextureHandle texture;          // invalid = plain white
    };

    struct Scene
    {
        std::vector<ModelInstance> instances;
        Floor floor;
    };

    // camera for one frame. the projection is built from the output aspect ratio
    struct View
    {
        glm::mat4 view = glm::mat4(1.0f);
        glm::vec3 position = glm::vec3(0.0f);   // world-space eye position (for specular / rim)
        float fovDegrees = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    // ---- per-frame settings -------------------------------------------------
    struct RenderSettings
    {
        // pass toggles
        bool lighting = true;
        bool toonShading = true;
        bool outlines = true;
        bool rimLighting = true;
        bool shadowMapping = true;
        bool postProcessing = true;
        bool bloomEnabled = true;       // only has an effect while postProcessing is on

        // look (types live in shader_config.h)
        glm::vec3 clearColor = glm::vec3(0.10f);    // sRGB, converted to linear internally
        glm::vec3 lightColor = glm::vec3(1.0f);
        DirLight dirLight;
        ShadowMap shadow;               // shadow.shadowOnly draws the depth map as a debug overlay
        Material material;
        Outline outline;
        ColorRamp ramp;
        RimLight rim;
        Bloom bloom;

        // half-size of the shadow camera's orthographic box: anything outside gets no shadow
        float shadowExtent = 2.0f;
    };

    struct GpuTimings
    {
        float shadow = 0.0f, scene = 0.0f, outline = 0.0f, bloom = 0.0f, composite = 0.0f;   // ms, smoothed
        float total() const { return shadow + scene + outline + bloom + composite; }
    };

    // ---- the renderer -------------------------------------------------------
    class Renderer
    {
    public:
        explicit Renderer(const RendererDesc& desc = RendererDesc());
        ~Renderer();     // releases every GL object: the GL context must still exist

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) noexcept;
        Renderer& operator=(Renderer&&) noexcept;

        // ---- resources (the renderer owns them until unloaded or destroyed)
        // an invalid handle is returned when the file can't be loaded
        ModelHandle loadModel(const std::string& path);
        void unloadModel(ModelHandle model);
        TextureHandle loadTexture(const std::string& path);
        void unloadTexture(TextureHandle texture);

        // ---- output + quality
        void resize(int width, int height);     // output size in pixels. cheap when unchanged
        void setMsaa(int samples);              // 0 = off. targets are rebuilt on the next render()
        void setRenderScale(float scale);       // clamped to [0.25, 1]. targets rebuilt on the next render()
        int width() const;
        int height() const;
        int activeMsaaSamples() const;          // what the GPU is really using (valid after a render())

        // ---- drawing
        // draws the scene into targetFramebuffer (0 = the window) at the size given to resize().
        // afterwards depth test and face culling are enabled and the target is left bound,
        // so a UI can be drawn straight on top
        void render(const Scene& scene, const View& view, const RenderSettings& settings,
            unsigned int targetFramebuffer = 0);

        // ---- diagnostics
        GpuTimings gpuTimings() const;          // smoothed, a few frames old
        unsigned int shadowMapTexture() const;  // GL texture id of the depth map (for debug views)

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

#endif