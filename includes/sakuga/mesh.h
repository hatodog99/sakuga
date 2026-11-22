#ifndef MESH_H
#define MESH_H

#include <glad/glad.h> // holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sakuga/shader.h>

#include <cstdint>
#include <string>
#include <vector>
using namespace std;

#define MAX_BONE_INFLUENCE 4

// 24 bytes per vertex (was 32): the normal is packed into 4 bytes, which cuts the vertex fetch
// bandwidth by a quarter. that matters on integrated GPUs, where the GPU shares RAM with the CPU.
struct Vertex {
    // position
    glm::vec3 Position;
    // normal, packed with glm::packSnorm3x10_1x2 (GL_INT_2_10_10_10_REV)
    uint32_t Normal;
    // texCoords
    glm::vec2 TexCoords;
};

struct Texture {
    unsigned int id;
    string type;
    string path;
};

class Mesh {
public:
    // the CPU copies of the vertices and indices are dropped once uploaded, only the GL objects stay
    vector<Texture> textures;
    unsigned int VAO = 0;
    unsigned int indexCount = 0;

    // constructor
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures);

    // binds the diffuse texture (unit 0) and draws
    void Draw();

    // draws the geometry only. the shadow depth pass has no textures to bind
    void DrawGeometry() const;

    // deletes the VAO, VBO and EBO. call while the GL context still exists; safe to call twice
    void release();

private:
    // render data
    unsigned int VBO = 0, EBO = 0;
    unsigned int diffuseTexture = 0;     // the only texture the shaders sample (texture_diffuse1)
    GLenum indexType = GL_UNSIGNED_INT;  // 16-bit indices when the mesh has < 65536 vertices

    // initializes all the buffer objects/arrays
    void setupMesh(const vector<Vertex>& vertices, const vector<unsigned int>& indices);
};

#endif