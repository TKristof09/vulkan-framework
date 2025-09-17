#pragma once

#include "CommandBuffer.hpp"
#include "VulkanContext.hpp"
#include "Shader.hpp"
#include <vulkan/vulkan.h>
#include <optional>


enum class PipelineType
{
    GRAPHICS,
    COMPUTE
};

class Pipeline;
struct PipelineCreateInfo
{
    PipelineType type;

    bool allowDerivatives = false;
    Pipeline* parent      = nullptr;


    // for GRAPHICS
    bool useColor         = true;
    bool useDepth         = false;
    bool useStencil       = false;
    bool useColorBlend    = false;
    bool useMultiSampling = false;
    bool useTesselation   = false;  // not supported yet

    bool useDynamicViewport = false;


    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat   = VK_FORMAT_D32_SFLOAT;
    VkFormat stencilFormat = VK_FORMAT_S8_UINT;  // TODO look into stencil stuff

    VkShaderStageFlags stages = 0;
    VkExtent2D viewportExtent = {};

    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    bool depthWriteEnable      = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    bool depthClampEnable = false;

    uint32_t viewMask = 0;

    bool isGlobal = false;
};


class Pipeline
{
public:
    Pipeline(const std::string& shaderName, PipelineCreateInfo createInfo);
    ~Pipeline();


    Pipeline(const Pipeline& other) = delete;

    Pipeline(Pipeline&& other) noexcept
        : m_name(std::move(other.m_name)),
          m_shaders(std::move(other.m_shaders)),
          m_pipeline(other.m_pipeline),
          m_layout(other.m_layout),
          m_usesDescriptorSet(other.m_usesDescriptorSet),
          m_vertexInputAttributes(std::move(other.m_vertexInputAttributes)),
          m_vertexInputBinding(other.m_vertexInputBinding)
    {
        other.m_pipeline = VK_NULL_HANDLE;
    }

    Pipeline& operator=(const Pipeline& other) = delete;

    Pipeline& operator=(Pipeline&& other) noexcept
    {
        if(this == &other)
            return *this;
        m_name                  = std::move(other.m_name);
        m_shaders               = std::move(other.m_shaders);
        m_pipeline              = other.m_pipeline;
        m_layout                = other.m_layout;
        m_usesDescriptorSet     = other.m_usesDescriptorSet;
        m_vertexInputAttributes = std::move(other.m_vertexInputAttributes);
        m_vertexInputBinding    = other.m_vertexInputBinding;

        other.m_pipeline = VK_NULL_HANDLE;
        return *this;
    }

    void Bind(CommandBuffer& cb, uint32_t frameIndex) const;
    [[nodiscard]] uint32_t GetViewMask() const { return m_createInfo.viewMask; }
    Shader& GetShader(uint32_t idx) { return m_shaders[idx]; }


private:
    friend class Renderer;
    friend class MaterialSystem;
    friend class DescriptorSetAllocator;
    friend class Shader;

    void Setup();

    void CreateGraphicsPipeline();
    void CreateComputePipeline();

    [[nodiscard]] inline VkPipelineBindPoint GetBindPoint() const
    {
        switch(m_createInfo.type)
        {
        case PipelineType::GRAPHICS:
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case PipelineType::COMPUTE:
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        default:
            Log::Error("Invalid pipeline type");
            return VK_PIPELINE_BIND_POINT_MAX_ENUM;
        }
    }

    std::string m_name;
    PipelineCreateInfo m_createInfo;
    std::vector<Shader> m_shaders;


    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;

    bool m_usesDescriptorSet = false;

    std::vector<VkVertexInputAttributeDescription> m_vertexInputAttributes;
    std::optional<VkVertexInputBindingDescription> m_vertexInputBinding;  // only support one for now

    std::vector<uint64_t> m_shaderDataSlots;
};
