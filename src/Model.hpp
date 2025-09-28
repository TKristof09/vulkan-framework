#pragma once

#include "Buffer.hpp"
#include <filesystem>
#include <vector>
#include <span>

class Model
{
public:
    struct Material
    {
        glm::vec4 baseColor = glm::vec4(1.0);

        float metallicness = 1.0;
        float roughness    = 1.0;

        float ior = 1.5;

        glm::vec3 emissiveColor = glm::vec4(0.0);
        float emissiveStrength  = 1.0f;

        float transmission = 0.0f;

        glm::vec3 specularTint = glm::vec3(1.0);
    };
    struct Primitive
    {
        Material material;
        uint64_t vertexBufferOffset;
        uint64_t vertexBufferSize;  // in bytes
        uint64_t indexBufferOffset;
        uint64_t indexBufferSize;  // in bytes
    };
    struct Mesh
    {
        std::vector<Primitive> primitives;
        glm::mat4 transform;
    };

    Model(std::filesystem::path p);

    static constexpr uint32_t VERTEX_SIZE = (3 + 3 + 2) * sizeof(float);

    const Buffer& GetVertexBuffer() const { return m_vertexBuffer; }
    const Buffer& GetIndexBuffer() const { return m_indexBuffer; }

    const std::span<const Mesh> GetMeshes() const { return std::span(m_meshes); }


private:
    std::vector<Mesh> m_meshes;
    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
};
