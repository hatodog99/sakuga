#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <sakuga/mesh.h>
#include <sakuga/shader.h>

#include <string>
#include <vector>
using namespace std;

// Loads a texture into an OpenGL texture object.
// `path` is either a file name relative to `directory`, or an embedded texture
// reference such as "*0" (GLB/glTF). Pass the scene so embedded textures can be found.
// On failure a 1x1 white texture is returned so the shader doesn't sample black.
unsigned int TextureFromFile(const char* path, const string& directory, bool gamma = false, const aiScene* scene = nullptr);

class Model
{
public:
    // model data
    vector<Texture> textures_loaded;	// stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;

    // constructor, expects a filepath to a 3D model.
    Model(string const& path, bool gamma = false);

    // draws the model, and thus all its meshes
    void Draw(Shader& shader);

private:
    // loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
    void loadModel(string const& path);

    // processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
    void processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform);

    Mesh processMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform);

    // checks all material textures of a given type and loads the textures if they're not loaded yet.
    // the required info is returned as a Texture struct.
    vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, string typeName, const aiScene* scene);
};

#endif