#include "model.h"

#include <stb/stb_image.h>

#include <cstdio>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
    // converts Assimp's row-major aiMatrix4x4 to GLM's column-major mat4
    glm::mat4 AiToGlm(const aiMatrix4x4& from)
    {
        glm::mat4 to;
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }

    // opens a file whose path is UTF-8 (so file names with non-ASCII characters work on Windows)
    FILE* OpenFileUtf8(const string& path)
    {
#ifdef _WIN32
        if (path.empty())
            return nullptr;
        int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (size <= 0)
            return nullptr;
        std::wstring wide(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], size);
        wide.resize(size - 1);	// drop the null terminator that was counted

        FILE* f = nullptr;
        _wfopen_s(&f, wide.c_str(), L"rb");
        return f;
#else
        return fopen(path.c_str(), "rb");
#endif
    }
}

// constructor, expects a filepath to a 3D model.
Model::Model(string const& path, bool gamma) : gammaCorrection(gamma)
{
    loadModel(path);
}

// draws the model, and thus all its meshes
void Model::Draw(Shader& shader)
{
    for (unsigned int i = 0; i < meshes.size(); i++)
        meshes[i].Draw(shader);
}

// loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
void Model::loadModel(string const& path)
{
    // the importer owns the scene, so the scene is only valid inside this function
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
    // check for errors
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
        return;
    }

    // directory of the model file (used to find external textures)
    size_t lastSlash = path.find_last_of("/\\");
    directory = (lastSlash == string::npos) ? string(".") : path.substr(0, lastSlash);

    // process ASSIMP's root node recursively
    processNode(scene->mRootNode, scene, glm::mat4(1.0f));
}

// processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
void Model::processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
{
    glm::mat4 nodeTransform = parentTransform * AiToGlm(node->mTransformation);

    // process each mesh located at the current node
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        // the node object only contains indices to index the actual objects in the scene.
        // the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene, nodeTransform));
    }
    // after we've processed all of the meshes (if any) we then recursively process each of the children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene, nodeTransform);
    }
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform)
{
    // data to fill
    vector<Vertex> vertices;
    vector<unsigned int> indices;
    vector<Texture> textures;

    // normals need the inverse-transpose so non-uniform scale doesn't skew them
    glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(transform)));
    glm::mat3 directionMatrix = glm::mat3(transform);

    // walk through each of the mesh's vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        // start from known values so nothing is left uninitialized
        vertex.Normal = glm::vec3(0.0f);
        vertex.TexCoords = glm::vec2(0.0f);
        vertex.Tangent = glm::vec3(0.0f);
        vertex.Bitangent = glm::vec3(0.0f);
        for (int b = 0; b < MAX_BONE_INFLUENCE; b++)
        {
            vertex.m_BoneIDs[b] = 0;
            vertex.m_Weights[b] = 0.0f;
        }

        // a placeholder, since assimp uses its own vector class that doesn't directly convert to glm's vec3
        glm::vec3 vec3Placeholder;

        // positions - apply the node's transform
        vec3Placeholder.x = mesh->mVertices[i].x;
        vec3Placeholder.y = mesh->mVertices[i].y;
        vec3Placeholder.z = mesh->mVertices[i].z;
        vertex.Position = glm::vec3(transform * glm::vec4(vec3Placeholder, 1.0f));

        // normals
        if (mesh->HasNormals())
        {
            vec3Placeholder.x = mesh->mNormals[i].x;
            vec3Placeholder.y = mesh->mNormals[i].y;
            vec3Placeholder.z = mesh->mNormals[i].z;
            vertex.Normal = glm::normalize(normalMatrix * vec3Placeholder);
        }

        // texture coordinates
        if (mesh->mTextureCoords[0])
        {
            // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);

            if (mesh->HasTangentsAndBitangents())
            {
                vec3Placeholder.x = mesh->mTangents[i].x;
                vec3Placeholder.y = mesh->mTangents[i].y;
                vec3Placeholder.z = mesh->mTangents[i].z;
                vertex.Tangent = glm::normalize(directionMatrix * vec3Placeholder);

                vec3Placeholder.x = mesh->mBitangents[i].x;
                vec3Placeholder.y = mesh->mBitangents[i].y;
                vec3Placeholder.z = mesh->mBitangents[i].z;
                vertex.Bitangent = glm::normalize(directionMatrix * vec3Placeholder);
            }
        }

        vertices.push_back(vertex);
    }

    // now walk through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    // process materials
    if (mesh->mMaterialIndex < scene->mNumMaterials)
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        // sampler names follow the convention texture_diffuseN, texture_specularN, texture_normalN, texture_heightN

        // 1. diffuse maps
        vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse", scene);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        // 2. specular maps
        vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular", scene);
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 3. normal maps
        vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal", scene);
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 4. height maps
        vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height", scene);
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
    }

    // return a mesh object created from the extracted mesh data
    return Mesh(vertices, indices, textures);
}

// checks all material textures of a given type and loads the textures if they're not loaded yet.
// the required info is returned as a Texture struct.
vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, string typeName, const aiScene* scene)
{
    vector<Texture> textures;
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);
        // check if texture was loaded before and if so, skip loading it again
        bool skip = false;
        for (unsigned int j = 0; j < textures_loaded.size(); j++)
        {
            if (std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0)
            {
                textures.push_back(textures_loaded[j]);
                skip = true;
                break;
            }
        }
        if (!skip)
        {
            Texture texture;
            texture.id = TextureFromFile(str.C_Str(), this->directory, false, scene);
            texture.type = typeName;
            texture.path = str.C_Str();
            textures.push_back(texture);
            textures_loaded.push_back(texture);
        }
    }
    return textures;
}

unsigned int TextureFromFile(const char* path, const string& directory, bool gamma, const aiScene* scene)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width = 0, height = 0, nrComponents = 0;
    unsigned char* data = nullptr;			// owned by stb, freed with stbi_image_free
    std::vector<unsigned char> raw;			// owned here, used for uncompressed embedded textures
    const unsigned char* pixels = nullptr;

    // embedded textures (GLB/glTF) have paths like "*0", "*1": the image data lives inside the model file
    const aiTexture* embedded = (scene != nullptr) ? scene->GetEmbeddedTexture(path) : nullptr;

    if (embedded)
    {
        if (embedded->mHeight == 0)
        {
            // compressed image (png/jpg) stored in memory; mWidth holds the byte size
            data = stbi_load_from_memory(
                reinterpret_cast<const unsigned char*>(embedded->pcData),
                static_cast<int>(embedded->mWidth),
                &width, &height, &nrComponents, 4);
            pixels = data;
            if (!data)
            {
                std::cout << "Embedded texture " << path << " could not be decoded (format hint '"
                    << embedded->achFormatHint << "'): " << stbi_failure_reason() << std::endl;
            }
        }
        else
        {
            // raw uncompressed texels (aiTexel is stored as b, g, r, a)
            width = static_cast<int>(embedded->mWidth);
            height = static_cast<int>(embedded->mHeight);
            raw.resize(static_cast<size_t>(width) * height * 4);
            for (int i = 0; i < width * height; i++)
            {
                raw[i * 4 + 0] = embedded->pcData[i].r;
                raw[i * 4 + 1] = embedded->pcData[i].g;
                raw[i * 4 + 2] = embedded->pcData[i].b;
                raw[i * 4 + 3] = embedded->pcData[i].a;
            }
            pixels = raw.data();
        }
    }
    else
    {
        // external file: keep only the file name and look for it next to the model
        string pathStr(path);
        size_t lastSlash = pathStr.find_last_of("/\\");
        string filenameOnly = (lastSlash == string::npos) ? pathStr : pathStr.substr(lastSlash + 1);
        string filename = directory + '/' + filenameOnly;

        FILE* f = OpenFileUtf8(filename);
        if (f)
        {
            data = stbi_load_from_file(f, &width, &height, &nrComponents, 4);
            fclose(f);
            pixels = data;
        }
        if (!data)
            std::cout << "Could not load texture file: " << filename << std::endl;
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (pixels)
    {
        // always loaded as 4 channels (RGBA), so a missing alpha channel becomes fully opaque
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    else
    {
        std::cout << "Texture failed to load path: " << path << std::endl;
        const unsigned char white[4] = { 255, 255, 255, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (data)
        stbi_image_free(data);

    return textureID;
}