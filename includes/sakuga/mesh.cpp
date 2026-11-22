#include <sakuga/mesh.h>

#include <cstddef>

// constructor
Mesh::Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures)
{
    this->textures = std::move(textures);

    // the lit and outline shaders only sample texture_diffuse1, so that is the only one worth binding
    for (const Texture& t : this->textures)
    {
        if (t.type == "texture_diffuse")
        {
            diffuseTexture = t.id;
            break;
        }
    }

    // now that we have all the required data, set the vertex buffers and its attribute pointers.
    setupMesh(vertices, indices);
}

// render the mesh
void Mesh::Draw()
{
    // texture_diffuse1 is set to unit 0 once at startup, so there are no uniform lookups here.
    // only unit 0 is touched: unit 1 keeps the shadow map
    if (diffuseTexture)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseTexture);
    }
    DrawGeometry();
}

void Mesh::DrawGeometry() const
{
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indexCount, indexType, 0);
}

void Mesh::release()
{
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (EBO) { glDeleteBuffers(1, &EBO); EBO = 0; }
}

// initializes all the buffer objects/arrays
void Mesh::setupMesh(const vector<Vertex>& vertices, const vector<unsigned int>& indices)
{
    indexCount = static_cast<unsigned int>(indices.size());

    // create buffers/arrays
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    // load data into vertex buffers
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    // indices: 16 bit when every index fits, half the index bandwidth
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    if (vertices.size() <= 65535)
    {
        indexType = GL_UNSIGNED_SHORT;
        vector<unsigned short> small(indices.begin(), indices.end());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, small.size() * sizeof(unsigned short), small.data(), GL_STATIC_DRAW);
    }
    else
    {
        indexType = GL_UNSIGNED_INT;
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    }

    // set the vertex attribute pointers
    // vertex Positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    // vertex normals: packed 2_10_10_10, normalized to [-1, 1]; size must be 4 for packed types,
    // the shader's vec3 input just ignores the 2-bit w
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
    // vertex texture coords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
    glBindVertexArray(0);
}