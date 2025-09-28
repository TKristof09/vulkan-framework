#include "Model.hpp"
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include "Log.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

int NumComponents(int type);
int CompSize(int comp);
std::vector<float> ReadAccessorAsFloat(const tinygltf::Model& m, const tinygltf::Accessor& acc);
std::vector<uint32_t> ReadAccessorAsUInt32(const tinygltf::Model& m, const tinygltf::Accessor& acc);


Model::Model(std::filesystem::path p)
{
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    bool ret = loader.LoadASCIIFromFile(&gltf, &err, &warn, p.string());
    // Or LoadBinaryFromFile(&model, &err, &warn, "model.glb");

    if(!warn.empty())
    {
        Log::Warn("GLTF: {}", warn);
    }
    if(!err.empty())
    {
        Log::Error("GLTF: {}", err);
    }
    if(!ret)
    {
        Log::Error("Failed to load glTF");
        abort();
    }


    // first pass: compute buffer sizes
    uint64_t totalVertices = 0;
    uint64_t totalIndices  = 0;
    for(const auto& mesh : gltf.meshes)
    {
        for(const auto& prim : mesh.primitives)
        {
            if(prim.attributes.find("POSITION") == prim.attributes.end())
                throw std::runtime_error("primitive without POSITION not supported");

            const tinygltf::Accessor& posAcc  = gltf.accessors[prim.attributes.at("POSITION")];
            totalVertices                    += posAcc.count;

            if(prim.indices >= 0)
            {
                const tinygltf::Accessor& idxAcc  = gltf.accessors[prim.indices];
                totalIndices                     += idxAcc.count;
            }
        }
    }

    const uint64_t vertexBufferBytes = totalVertices * VERTEX_SIZE;
    const uint64_t indexBufferBytes  = totalIndices * sizeof(uint32_t);

    Buffer stagingVertexBuffer(vertexBufferBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    Buffer stagingIndexBuffer(indexBufferBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);

    m_vertexBuffer.Allocate(vertexBufferBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    m_indexBuffer.Allocate(indexBufferBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    // second pass: read data
    uint64_t vertexByteCursor = 0;
    uint64_t indexByteCursor  = 0;

    m_meshes.reserve(gltf.meshes.size());

    for(const auto& node : gltf.nodes)
    {
        if(node.mesh == -1)
            continue;

        assert(node.children.size() == 0);
        Mesh outMesh{};

        auto matrix = node.matrix;
        if(matrix.size() > 0)
        {
            glm::mat4 mat(
                static_cast<float>(matrix[0]), static_cast<float>(matrix[4]), static_cast<float>(matrix[8]), static_cast<float>(matrix[12]),
                static_cast<float>(matrix[1]), static_cast<float>(matrix[5]), static_cast<float>(matrix[9]), static_cast<float>(matrix[13]),
                static_cast<float>(matrix[2]), static_cast<float>(matrix[6]), static_cast<float>(matrix[10]), static_cast<float>(matrix[14]),
                static_cast<float>(matrix[3]), static_cast<float>(matrix[7]), static_cast<float>(matrix[11]), static_cast<float>(matrix[15]));
            outMesh.transform = mat;
        }
        else
        {
            glm::vec3 position(0.0f);
            glm::vec3 scale(1.0f);
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            if(node.translation.size() > 0)
                position = {node.translation[0], node.translation[1], node.translation[2]};
            if(node.scale.size() > 0)
                scale = {node.scale[0], node.scale[1], node.scale[2]};
            if(node.rotation.size() > 0)
                rotation = {static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])};

            outMesh.transform = glm::translate(glm::mat4(1.0f), position)
                              * glm::toMat4(rotation)
                              * glm::scale(glm::mat4(1.0f), scale);
        }

        const auto& mesh = gltf.meshes[node.mesh];
        outMesh.primitives.reserve(mesh.primitives.size());
        for(const auto& prim : mesh.primitives)
        {
            Primitive outPrim;
            if(prim.material >= 0)
            {
                const tinygltf::Material& gm = gltf.materials[prim.material];
                if(gm.values.find("baseColorFactor") != gm.values.end())
                {
                    const auto& v              = gm.values.at("baseColorFactor").ColorFactor();
                    outPrim.material.baseColor = glm::vec4{v[0], v[1], v[2], v[3]};
                }
                if(gm.values.find("metallicFactor") != gm.values.end())
                {
                    const auto& s                 = gm.values.at("metallicFactor").Factor();
                    outPrim.material.metallicness = s;
                }

                if(gm.values.find("roughnessFactor") != gm.values.end())
                {
                    const auto& s              = gm.values.at("roughnessFactor").Factor();
                    outPrim.material.roughness = s;
                }

                outPrim.material.emissiveColor = glm::vec3{gm.emissiveFactor[0], gm.emissiveFactor[1], gm.emissiveFactor[2]};

                if(gm.extensions.find("KHR_materials_emissive_strength") != gm.extensions.end())
                {
                    const tinygltf::Value& extVal = gm.extensions.at("KHR_materials_emissive_strength");
                    if(extVal.Has("emissiveStrength"))
                    {
                        outPrim.material.emissiveStrength = static_cast<float>(extVal.Get("emissiveStrength").Get<double>());
                    }
                }
                else
                {
                    outPrim.material.emissiveStrength = 0.0f;  // spec states default value is 1 but it should be 0 if no extension i think
                }

                if(gm.extensions.find("KHR_materials_ior") != gm.extensions.end())
                {
                    const tinygltf::Value& extVal = gm.extensions.at("KHR_materials_ior");
                    if(extVal.Has("ior"))
                    {
                        outPrim.material.ior = static_cast<float>(extVal.Get("ior").Get<double>());
                    }
                }


                if(gm.extensions.find("KHR_materials_transmission") != gm.extensions.end())
                {
                    const tinygltf::Value& extVal = gm.extensions.at("KHR_materials_transmission");
                    if(extVal.Has("transmissionFactor"))
                    {
                        outPrim.material.transmission = static_cast<float>(extVal.Get("transmissionFactor").Get<double>());
                    }
                }

                if(gm.extensions.find("KHR_materials_specular") != gm.extensions.end())
                {
                    const tinygltf::Value& extVal = gm.extensions.at("KHR_materials_specular");
                    if(extVal.Has("specularColorFactor"))
                    {
                        auto ext                        = extVal.Get("specularColorFactor");
                        outPrim.material.specularTint.r = ext.Get(0).GetNumberAsDouble();
                        outPrim.material.specularTint.g = ext.Get(1).GetNumberAsDouble();
                        outPrim.material.specularTint.b = ext.Get(2).GetNumberAsDouble();
                    }
                }
            }

            // read attributes
            const tinygltf::Accessor& posAcc = gltf.accessors[prim.attributes.at("POSITION")];

            auto positions = ReadAccessorAsFloat(gltf, posAcc);  // length = posAcc.count * 3

            std::vector<float> normals;
            if(prim.attributes.find("NORMAL") != prim.attributes.end())
            {
                normals = ReadAccessorAsFloat(gltf, gltf.accessors[prim.attributes.at("NORMAL")]);
            }
            else
            {
                // TODO: calculate normals if not present
                Log::Error("No vertex normals found for {}, insterting 0s", p.string());
                normals.assign(posAcc.count * 3, 0.0f);
            }

            std::vector<float> uvs;
            if(prim.attributes.find("TEXCOORD_0") != prim.attributes.end())
            {
                uvs = ReadAccessorAsFloat(gltf, gltf.accessors[prim.attributes.at("TEXCOORD_0")]);
            }
            else
            {
                Log::Error("No vertex UVs found for {}, inserting 0s", p.string());
                uvs.assign(posAcc.count * 2, 0.0f);
            }

            // pack vertices into contiguous CPU buffer for a primitive then upload
            const uint32_t vertCount = static_cast<uint32_t>(posAcc.count);
            std::vector<float> packed;
            packed.reserve(vertCount * VERTEX_SIZE);
            for(uint32_t i = 0; i < vertCount; ++i)
            {
                packed.push_back(positions[i * 3 + 0]);
                packed.push_back(positions[i * 3 + 1]);
                packed.push_back(positions[i * 3 + 2]);

                packed.push_back(normals[i * 3 + 0]);
                packed.push_back(normals[i * 3 + 1]);
                packed.push_back(normals[i * 3 + 2]);

                packed.push_back(uvs[i * 2 + 0]);
                packed.push_back(uvs[i * 2 + 1]);
            }

            uint64_t vbSize = uint64_t(packed.size() * sizeof(float));
            stagingVertexBuffer.Fill(packed.data(), vbSize, vertexByteCursor);
            outPrim.vertexBufferOffset  = vertexByteCursor;
            outPrim.vertexBufferSize    = vbSize;
            vertexByteCursor           += vbSize;

            if(prim.indices >= 0)
            {
                const tinygltf::Accessor& idxAcc = gltf.accessors[prim.indices];
                auto idxs                        = ReadAccessorAsUInt32(gltf, idxAcc);
                uint64_t ibSize                  = uint64_t(idxs.size() * sizeof(uint32_t));
                stagingIndexBuffer.Fill(idxs.data(), ibSize, indexByteCursor);
                outPrim.indexBufferOffset  = indexByteCursor;
                outPrim.indexBufferSize    = ibSize;
                indexByteCursor           += ibSize;
            }
            else
            {
                outPrim.indexBufferOffset = 0;
                outPrim.indexBufferSize   = 0;
            }
            outMesh.primitives.push_back(outPrim);
        }
        m_meshes.push_back(outMesh);
    }

    stagingVertexBuffer.Copy(&m_vertexBuffer);
    stagingIndexBuffer.Copy(&m_indexBuffer);

    auto nameVertex = p.filename().string() + "_vertex";
    VK_SET_DEBUG_NAME(m_vertexBuffer.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, nameVertex.c_str());
    auto nameIndex = p.filename().string() + "_index";
    VK_SET_DEBUG_NAME(m_indexBuffer.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, nameIndex.c_str());
}

int NumComponents(int type)
{
    return tinygltf::GetNumComponentsInType(type);
}
int CompSize(int comp)
{
    return tinygltf::GetComponentSizeInBytes(comp);
}

std::vector<float> ReadAccessorAsFloat(const tinygltf::Model& m, const tinygltf::Accessor& acc)
{
    if(acc.sparse.count > 0)
        throw std::runtime_error("sparse accessors not supported");

    const tinygltf::BufferView& bv = m.bufferViews[acc.bufferView];
    const tinygltf::Buffer& buf    = m.buffers[bv.buffer];
    const unsigned char* base      = buf.data.data() + bv.byteOffset + acc.byteOffset;

    const int compSize  = CompSize(acc.componentType);
    const int ncomp     = NumComponents(acc.type);
    const size_t stride = bv.byteStride ? bv.byteStride : (compSize * ncomp);

    std::vector<float> out;
    out.reserve(acc.count * ncomp);

    for(size_t i = 0; i < acc.count; ++i)
    {
        const unsigned char* elem = base + i * stride;
        for(int c = 0; c < ncomp; ++c)
        {
            const unsigned char* compPtr = elem + c * compSize;
            float v                      = 0.0f;
            switch(acc.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                {
                    float tmp;
                    std::memcpy(&tmp, compPtr, sizeof(float));
                    v = tmp;
                    break;
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    uint8_t tmp;
                    std::memcpy(&tmp, compPtr, sizeof(uint8_t));
                    if(acc.normalized)
                        v = tmp / 255.0f;
                    else
                        v = float(tmp);
                    break;
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    uint16_t tmp;
                    std::memcpy(&tmp, compPtr, sizeof(uint16_t));
                    if(acc.normalized)
                        v = tmp / 65535.0f;
                    else
                        v = float(tmp);
                    break;
                }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                {
                    int16_t tmp;
                    std::memcpy(&tmp, compPtr, sizeof(int16_t));
                    if(acc.normalized)
                        v = std::max(-1.0f, float(tmp) / 32767.0f);
                    else
                        v = float(tmp);
                    break;
                }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                {
                    int8_t tmp;
                    std::memcpy(&tmp, compPtr, sizeof(int8_t));
                    if(acc.normalized)
                        v = std::max(-1.0f, float(tmp) / 127.0f);
                    else
                        v = float(tmp);
                    break;
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    uint32_t tmp;
                    std::memcpy(&tmp, compPtr, sizeof(uint32_t));
                    v = float(tmp);
                    break;
                }
            default:
                throw std::runtime_error("unsupported component type in ReadAccessorAsFloat");
            }
            out.push_back(v);
        }
    }
    return out;
}

std::vector<uint32_t> ReadAccessorAsUInt32(const tinygltf::Model& m, const tinygltf::Accessor& acc)
{
    if(acc.sparse.count > 0)
        throw std::runtime_error("sparse accessors not supported");
    const tinygltf::BufferView& bv = m.bufferViews[acc.bufferView];
    const tinygltf::Buffer& buf    = m.buffers[bv.buffer];
    const unsigned char* base      = buf.data.data() + bv.byteOffset + acc.byteOffset;

    const int compSize = CompSize(acc.componentType);
    const int ncomp    = NumComponents(acc.type);
    if(ncomp != 1)
        throw std::runtime_error("index accessor type must be scalar");
    const size_t stride = bv.byteStride ? bv.byteStride : compSize;

    std::vector<uint32_t> out;
    out.reserve(acc.count);
    for(size_t i = 0; i < acc.count; ++i)
    {
        const unsigned char* compPtr = base + i * stride;
        uint32_t v                   = 0;
        switch(acc.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                uint16_t tmp;
                std::memcpy(&tmp, compPtr, sizeof(uint16_t));
                v = tmp;
                break;
            }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                uint8_t tmp;
                std::memcpy(&tmp, compPtr, sizeof(uint8_t));
                v = tmp;
                break;
            }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                uint32_t tmp;
                std::memcpy(&tmp, compPtr, sizeof(uint32_t));
                v = tmp;
                break;
            }
        default:
            throw std::runtime_error("unsupported index component type");
        }
        out.push_back(v);
    }
    return out;
}
