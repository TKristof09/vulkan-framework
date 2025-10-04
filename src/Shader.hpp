#pragma once
#include "Buffer.hpp"
#include "DescriptorSet.hpp"
#include "Image.hpp"
#include "Raytracing.hpp"
#include "VulkanContext.hpp"
#include "slang.h"
#include <cstring>
#include <filesystem>
#include <array>
#include <deque>
#include "Log.hpp"
#include "Pipeline.hpp"


template<typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);
template<typename T>
concept IsSimpleParameter = !IsAnyOf<std::remove_cvref_t<std::remove_pointer_t<std::decay_t<T>>>, Buffer, Image, Raytracing::TLAS>;

class Shader
{
public:
    Shader(const std::filesystem::path& path, VkShaderStageFlagBits stage, std::string_view entryPoint = "main");
    ~Shader()
    {
        DestroyShaderModule();
    }

    Shader(const Shader& other) = delete;

    Shader(Shader&& other) noexcept
        : m_name(std::move(other.m_name)),
          m_shaderModule(other.m_shaderModule),
          m_stage(other.m_stage),

          m_descriptorLayoutBuilders(std::move(other.m_descriptorLayoutBuilders)),

          m_bindings(std::move(other.m_bindings)),
          m_pushConstantRange(std::move(other.m_pushConstantRange)),
          m_pushConstantData(std::move(other.m_pushConstantData)),

          m_uniformBuffers(std::move(other.m_uniformBuffers)),
          m_uniformBufferInfos(std::move(other.m_uniformBufferInfos)),
          m_uniformBufferSize(other.m_uniformBufferSize),

          m_numThreadsX(other.m_numThreadsX),
          m_numThreadsY(other.m_numThreadsY),
          m_numThreadsZ(other.m_numThreadsZ)
    {
        other.m_shaderModule      = VK_NULL_HANDLE;
        other.m_stage             = {};
        other.m_uniformBufferSize = 0;
    }

    Shader& operator=(Shader&& other) noexcept
    {
        if(this != &other)
        {
            DestroyShaderModule();

            m_name         = std::move(other.m_name);
            m_shaderModule = other.m_shaderModule;
            m_stage        = other.m_stage;

            m_descriptorLayoutBuilders = std::move(other.m_descriptorLayoutBuilders);

            m_bindings          = std::move(other.m_bindings);
            m_pushConstantRange = std::move(other.m_pushConstantRange);
            m_pushConstantData  = std::move(other.m_pushConstantData);

            m_uniformBuffers     = std::move(other.m_uniformBuffers);
            m_uniformBufferInfos = std::move(other.m_uniformBufferInfos);
            m_uniformBufferSize  = other.m_uniformBufferSize;

            m_numThreadsX = other.m_numThreadsX;
            m_numThreadsY = other.m_numThreadsY;
            m_numThreadsZ = other.m_numThreadsZ;

            other.m_shaderModule      = VK_NULL_HANDLE;
            other.m_stage             = {};
            other.m_uniformBufferSize = 0;
        }
        return *this;
    }

    void BindResources(CommandBuffer& cb, uint32_t frameIndex, VkPipelineLayout layout, VkPipelineBindPoint bindPoint) const;

    void SetParameter(uint32_t frameIndex, std::string_view name, const Image* image, uint32_t index = 0);
    void SetParameter(uint32_t frameIndex, std::string_view name, const Buffer* buffer, uint32_t index = 0);
    void SetParameter(uint32_t frameIndex, std::string_view name, const Raytracing::TLAS& tlas, uint32_t index = 0);

    template<typename T>
        requires(IsSimpleParameter<T>)
    void SetParameter(uint32_t frameIndex, std::string_view name, const T& data);
    template<typename T>
        requires(IsSimpleParameter<T>)
    void SetParameter(uint32_t frameIndex, std::string_view name, const std::vector<T>& data);

    void Dispatch(CommandBuffer& cb, uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ);


private:
    friend class Renderer;
    friend class Pipeline;

    void Finalize(Pipeline* pipeline);

    struct Offset
    {
        Offset(slang::VariableLayoutReflection* vl)
        {
            bindingSet        = (uint32_t)vl->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);
            binding           = (uint32_t)vl->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            pushConstantRange = (uint32_t)vl->getOffset(slang::ParameterCategory::PushConstantBuffer);
            subelement        = (uint32_t)vl->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
        }
        void operator+=(Offset const& offset)
        {
            binding           += offset.binding;
            bindingSet        += offset.bindingSet;
            pushConstantRange += offset.pushConstantRange;
            subelement        += offset.subelement;
        }

        uint32_t bindingSet        = -1;
        uint32_t binding           = -1;
        uint32_t pushConstantRange = -1;
        uint32_t subelement        = -1;
    };
    bool Compile(const std::filesystem::path& path, std::string_view entryPoint);
    void Reflect(slang::ProgramLayout* layout);

    void GetLayout(slang::VariableLayoutReflection* vl, std::deque<slang::VariableLayoutReflection*>& pathStack, bool isEntryPoint);


    void CreateDescriptors();

    VkShaderModule GetShaderModule() const
    {
        return m_shaderModule;
    };

    void DestroyShaderModule()
    {
        if(m_shaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(VulkanContext::GetDevice(), m_shaderModule, nullptr);
            m_shaderModule = VK_NULL_HANDLE;
        }
    }


    std::string m_name;
    VkShaderModule m_shaderModule;
    VkShaderStageFlagBits m_stage;
    Pipeline* m_pipeline;

    struct Binding
    {
        uint32_t set               = 0;
        uint32_t binding           = 0;
        uint64_t offset            = 0;
        uint64_t size              = 0;
        uint32_t stride            = 0;
        uint64_t arrayElementCount = 0;
        VkDescriptorType type;
        bool isPushConstant = false;
        bool isVariableSize = false;
    };
    std::array<DescriptorSetLayoutBuilder, 4> m_descriptorLayoutBuilders;

    struct string_hash
    {
        using hash_type      = std::hash<std::string_view>;
        using is_transparent = void;

        std::size_t operator()(const char* str) const { return hash_type{}(str); }
        std::size_t operator()(std::string_view str) const { return hash_type{}(str); }
        std::size_t operator()(std::string const& str) const { return hash_type{}(str); }
    };
    std::unordered_map<std::string, Binding, string_hash, std::equal_to<>> m_bindings;

    // NOTE: I assume that slang attributes a single contiguous range for a
    // shader, I can't think of a situtation that would result otherwise
    VkPushConstantRange m_pushConstantRange;
    std::vector<uint32_t> m_pushConstantSizes;
    std::vector<uint8_t> m_pushConstantData;

    std::vector<Buffer> m_uniformBuffers;
    std::vector<std::tuple<uint32_t, uint32_t, uint64_t, uint64_t>> m_uniformBufferInfos;
    uint64_t m_uniformBufferSize = 0;

    uint32_t m_numThreadsX;
    uint32_t m_numThreadsY;
    uint32_t m_numThreadsZ;
};

// Bools in shaders are actually ints, so we have to make sure the extra 3 bytes don't contain garbage
template<>
inline void Shader::SetParameter<bool>(uint32_t frameIndex, std::string_view name, const bool& data)
{
    int32_t dataInt = static_cast<int32_t>(data);
    SetParameter(frameIndex, name, dataInt);
}
template<>
inline void Shader::SetParameter<bool>(uint32_t frameIndex, std::string_view name, const std::vector<bool>& data)
{
    std::vector<int32_t> dataInts;
    dataInts.reserve(data.size());
    for(const bool& value : data)
    {
        dataInts.push_back(static_cast<int32_t>(value));
    }
    SetParameter(frameIndex, name, dataInts);
}

template<typename T>
    requires(IsSimpleParameter<T>)
void Shader::SetParameter(uint32_t frameIndex, std::string_view name, const T& data)
{
    auto it = m_bindings.find(name);
    if(it == m_bindings.end())
    {
        Log::Warn("Shader parameter {} not found in shader {}", name, m_name);
        return;
    }

    auto binding = it->second;

    if(binding.isPushConstant)
    {
        std::memcpy(&m_pushConstantData[binding.offset], &data, binding.size);
    }
    else
    {
        m_uniformBuffers[frameIndex].Fill(&data, binding.size, binding.offset);
    }
}


template<typename T>
    requires(IsSimpleParameter<T>)
void Shader::SetParameter(uint32_t frameIndex, std::string_view name, const std::vector<T>& data)
{
    auto it = m_bindings.find(name);
    if(it == m_bindings.end())
    {
        Log::Warn("Shader parameter {} not found in shader {}", name, m_name);
        return;
    }

    auto binding = it->second;

    if(binding.isPushConstant)
    {
        if(binding.stride != sizeof(T))
        {
            for(size_t i = 0; i < data.size(); i++)
            {
                std::memcpy(&m_pushConstantData[binding.offset + i * binding.stride], &data[i], sizeof(T));
            }
        }
        else
        {
            std::memcpy(&m_pushConstantData[binding.offset], data.data(), binding.size);
        }
    }
    else
    {
        if(binding.stride != sizeof(T))
        {
            for(size_t i = 0; i < data.size(); i++)
            {
                m_uniformBuffers[frameIndex].Fill(&data[i], sizeof(T), binding.offset + i * binding.stride);
            }
        }
        else
        {
            m_uniformBuffers[frameIndex].Fill(data.data(), binding.size, binding.offset);
        }
    }
}
