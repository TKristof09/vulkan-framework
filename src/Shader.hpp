#pragma once
#include "Buffer.hpp"
#include "DescriptorSet.hpp"
#include "Image.hpp"
#include "VulkanContext.hpp"
#include "slang.h"
#include <filesystem>
#include <array>
#include "Log.hpp"

class Pipeline;

class Shader
{
public:
    Shader(const std::string& filename, VkShaderStageFlagBits stage);
    ~Shader()
    {
        for(auto layout : m_descriptorLayouts)
        {
            vkDestroyDescriptorSetLayout(VulkanContext::GetDevice(), layout, nullptr);
        }
        DestroyShaderModule();
    }

    Shader(const Shader& other) = delete;

    Shader(Shader&& other) noexcept
        : m_shaderModule(other.m_shaderModule),
          m_stage(other.m_stage)
    {
        other.m_shaderModule = VK_NULL_HANDLE;
    }

    Shader& operator=(const Shader& other) = delete;

    Shader& operator=(Shader&& other) noexcept
    {
        if(this == &other)
            return *this;
        m_shaderModule       = other.m_shaderModule;
        other.m_shaderModule = VK_NULL_HANDLE;
        m_stage              = other.m_stage;
        return *this;
    }

    void SetParameter(uint32_t frameIndex, std::string_view name, Image* image);
    void SetParameter(uint32_t frameIndex, std::string_view name, Buffer* buffer);

    template<typename T>
    void SetParameter(uint32_t frameIndex, std::string_view name, const T& data);
    template<typename T>
    void SetParameter(uint32_t frameIndex, std::string_view name, const std::vector<T>& data);


private:
    friend class Renderer;
    friend class Pipeline;


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
    bool Compile(const std::filesystem::path& path);
    void Reflect(slang::ProgramLayout* layout);
    void GetLayout(slang::VariableLayoutReflection* vl, Offset offset, std::string path, bool isParameterBlock = false);
    void ParsePushConsts(uint32_t rangeIndex, slang::VariableLayoutReflection* var, std::string path, uint32_t offset);
    void ParseStruct(Offset binding, slang::VariableLayoutReflection* var, std::string path, uint32_t offset);

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

    struct Binding
    {
        uint32_t set     = 0;
        uint32_t binding = 0;
        uint64_t offset  = 0;
        uint64_t size    = 0;
        uint32_t stride  = 0;
        VkDescriptorType type;
        bool isPushConstant = false;
    };
    std::array<DescriptorSetLayoutBuilder, 4> m_descriptorLayoutBuilders;
    std::vector<VkDescriptorSetLayout> m_descriptorLayouts;
    std::vector<std::vector<VkDescriptorSet>> m_descriptorSets;

    struct string_hash
    {
        using hash_type      = std::hash<std::string_view>;
        using is_transparent = void;

        std::size_t operator()(const char* str) const { return hash_type{}(str); }
        std::size_t operator()(std::string_view str) const { return hash_type{}(str); }
        std::size_t operator()(std::string const& str) const { return hash_type{}(str); }
    };

    std::unordered_map<std::string, Binding, string_hash, std::equal_to<>> m_bindings;

    std::vector<VkPushConstantRange> m_pushConstants;

    std::vector<Buffer> m_uniformBuffers;
    std::vector<std::tuple<uint32_t, uint32_t, uint64_t, uint64_t>> m_uniformBufferInfos;
    uint64_t m_uniformBufferSize = 0;
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
        // TODO
        abort();
    }
    else
    {
        m_uniformBuffers[frameIndex].Fill(&data, binding.size, binding.offset);
    }
}


template<typename T>
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
        // TODO
        abort();
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
