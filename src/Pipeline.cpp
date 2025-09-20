#include "Pipeline.hpp"
#include "VulkanContext.hpp"
#include "Log.hpp"
#include <cassert>
#include "Renderer.hpp"
#include "Shader.hpp"

Pipeline::Pipeline(const std::string& shaderName, PipelineCreateInfo createInfo)
    : m_name(shaderName),
      m_createInfo(createInfo)
{
    Setup();
}

void Pipeline::Setup()
{
    m_vertexInputAttributes.clear();

    m_shaders.resize(m_createInfo.shaders.size());
    switch(m_createInfo.type)
    {
    case PipelineType::GRAPHICS:
        {
            assert(!"Graphics pipelines not yet supported");
            assert(m_createInfo.shaders.size() == 2);
            for(const auto& shader : m_createInfo.shaders)
            {
                if(shader->m_stage == VK_SHADER_STAGE_VERTEX_BIT)
                    m_shaders[0] = shader;
                if(shader->m_stage == VK_SHADER_STAGE_FRAGMENT_BIT)
                    m_shaders[1] = shader;
            }
            break;
        }
    case PipelineType::COMPUTE:
        {
            assert(m_createInfo.shaders.size() == 1);
            m_shaders[0] = m_createInfo.shaders[0];
            break;
        }
    case PipelineType::RAYTRACING:
        {
            // TODO: allow more flexible shader setup rather than 1 raygen + 1 miss + 1 closest hit
            assert(m_createInfo.shaders.size() == 3);
            for(const auto& shader : m_createInfo.shaders)
            {
                if(shader->m_stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR)
                    m_shaders[0] = shader;
                if(shader->m_stage == VK_SHADER_STAGE_MISS_BIT_KHR)
                    m_shaders[1] = shader;
                if(shader->m_stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
                    m_shaders[2] = shader;
            }
            break;
        }
    }

    for(const auto& shader : m_shaders)
    {
        assert(shader && "Pipeline is missing some shaders");
    }


    CreateDescriptors();

    for(auto& shader : m_shaders)
    {
        shader->Finalize(this);
    }

    switch(m_createInfo.type)
    {
    case PipelineType::GRAPHICS:
        CreateGraphicsPipeline();
        break;
    case PipelineType::COMPUTE:
        CreateComputePipeline();
        break;
    case PipelineType::RAYTRACING:
        CreateRaytracingPipeline();
        break;
    }

    for(auto& shader : m_shaders)
    {
        shader->DestroyShaderModule();
    }

    VK_SET_DEBUG_NAME(m_pipeline, VK_OBJECT_TYPE_PIPELINE, m_shaders[0]->m_name.c_str());
}

Pipeline::~Pipeline()
{
    if(m_pipeline != VK_NULL_HANDLE)
    {
        for(auto layout : m_descriptorLayouts)
        {
            vkDestroyDescriptorSetLayout(VulkanContext::GetDevice(), layout, nullptr);
        }
        vkDestroyPipeline(VulkanContext::GetDevice(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(VulkanContext::GetDevice(), m_layout, nullptr);

        m_pipeline = VK_NULL_HANDLE;
    }
}

void Pipeline::CreateDescriptors()
{
    std::array<DescriptorSetLayoutBuilder, 4> descriptorLayoutBuilders;
    for(const auto& shader : m_shaders)
    {
        for(uint32_t i = 0; i < descriptorLayoutBuilders.size(); i++)
        {
            descriptorLayoutBuilders[i] += shader->m_descriptorLayoutBuilders[i];
        }
    }
    for(auto builder : descriptorLayoutBuilders)
    {
        // NOTE: can we have non continuous descriptors? like 0 and 2 are filled but not 1
        if(!builder.bindings.empty())
            m_descriptorLayouts.push_back(builder.Build());
    }

    if(m_descriptorLayouts.size() == 0)
        return;

    m_descriptorSets.resize(Renderer::MAX_FRAMES_IN_FLIGHT);


    for(uint32_t i = 0; i < m_descriptorLayouts.size(); i++)
    {
        auto name = std::format("dsl_{}_{}", m_name, i);
        VK_SET_DEBUG_NAME(m_descriptorLayouts[i], VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, name.c_str());
    }
    for(int frameIndex = 0; frameIndex < Renderer::MAX_FRAMES_IN_FLIGHT; frameIndex++)
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = VulkanContext::GetDescriptorPool();
        allocInfo.descriptorSetCount = static_cast<uint32_t>(m_descriptorLayouts.size());
        allocInfo.pSetLayouts        = m_descriptorLayouts.data();

        m_descriptorSets[frameIndex].resize(m_descriptorLayouts.size());

        VK_CHECK(vkAllocateDescriptorSets(VulkanContext::GetDevice(), &allocInfo, m_descriptorSets[frameIndex].data()), "Failed to allocate descriptors");
        for(uint32_t i = 0; i < m_descriptorLayouts.size(); i++)
        {
            auto name = std::format("ds_{}_{}_{}", m_name, frameIndex, i);
            VK_SET_DEBUG_NAME(m_descriptorSets[frameIndex][i], VK_OBJECT_TYPE_DESCRIPTOR_SET, name.c_str());
        }
    }
}

void Pipeline::CreateGraphicsPipeline()
{
    std::vector<VkPipelineShaderStageCreateInfo> stagesCI;
    for(const auto& shader : m_shaders)
    {
        VkPipelineShaderStageCreateInfo ci = {};
        ci.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage                           = shader->m_stage;
        ci.pName                           = "main";
        ci.module                          = shader->GetShaderModule();

        stagesCI.push_back(ci);
    }
    // ##################### VERTEX INPUT #####################

    VkPipelineVertexInputStateCreateInfo vertexInput = {};  // vertex info hardcoded for the moment
    vertexInput.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if(m_vertexInputBinding.has_value())
    {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions    = &m_vertexInputBinding.value();
        ;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertexInputAttributes.size());
        vertexInput.pVertexAttributeDescriptions    = m_vertexInputAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo assembly = {};
    assembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ##################### VIEWPORT #####################

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount                     = 1;
    viewportState.scissorCount                      = 1;
    VkViewport viewport                             = {};
    VkRect2D scissor                                = {};
    if(!m_createInfo.useDynamicViewport)
    {
        viewport.width    = (float)m_createInfo.viewportExtent.width;
        viewport.height   = -(float)m_createInfo.viewportExtent.height;
        viewport.x        = 0.f;
        viewport.y        = (float)m_createInfo.viewportExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;


        scissor.offset = {0, 0};
        scissor.extent = m_createInfo.viewportExtent;


        viewportState.pViewports = &viewport;
        viewportState.pScissors  = &scissor;
    }


    // ##################### DYNAMIC VIEWPORT #####################
    std::vector<VkDynamicState> dynamicStates;
    if(m_createInfo.useDynamicViewport)
    {
        dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    }
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType                            = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount                = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates                   = dynamicStates.data();


    // ##################### RASTERIZATION #####################
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode                               = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.polygonMode                            = VK_POLYGON_MODE_FILL;
    rasterizer.depthClampEnable                       = m_createInfo.depthClampEnable;
    rasterizer.rasterizerDiscardEnable                = false;
    rasterizer.lineWidth                              = 1.0f;
    rasterizer.depthBiasEnable                        = false;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable                  = VK_TRUE;
    multisample.minSampleShading                     = 0.2f;  // closer to 1 is smoother
    multisample.rasterizationSamples                 = m_createInfo.useMultiSampling ? m_createInfo.msaaSamples : VK_SAMPLE_COUNT_1_BIT;


    // ##################### COLOR BLEND #####################
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask                      = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    colorBlendAttachment.blendEnable                         = m_createInfo.useColorBlend;
    colorBlendAttachment.srcColorBlendFactor                 = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor                 = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp                        = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor                 = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor                 = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp                        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable                       = VK_FALSE;
    colorBlend.attachmentCount                     = 1;
    colorBlend.pAttachments                        = &colorBlendAttachment;


    // ##################### DEPTH #####################
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType                                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable                       = m_createInfo.useDepth;
    depthStencil.depthWriteEnable                      = m_createInfo.depthWriteEnable;
    depthStencil.depthCompareOp                        = m_createInfo.depthCompareOp;  // not OP_LESS because we have a depth prepass
    // depthStencil.depthBoundsTestEnable	= VK_TRUE;
    // depthStencil.minDepthBounds			= 0.0f;
    // depthStencil.maxDepthBounds			= 1.0f;
    depthStencil.stencilTestEnable                     = m_createInfo.useStencil;


    // ##################### LAYOUT #####################
    VkDescriptorSetLayout descSetLayout         = VK_NULL_HANDLE;
    VkPushConstantRange pcRange                 = VulkanContext::GetGlobalPushConstantRange();
    VkPipelineLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCreateInfo.setLayoutCount             = 1;
    layoutCreateInfo.pSetLayouts                = &descSetLayout;
    layoutCreateInfo.pushConstantRangeCount     = 1;
    layoutCreateInfo.pPushConstantRanges        = &pcRange;

    VK_CHECK(vkCreatePipelineLayout(VulkanContext::GetDevice(), &layoutCreateInfo, nullptr, &m_layout), "Failed to create pipeline layout");


    // ##################### RENDERING #####################
    VkPipelineRenderingCreateInfo renderingCreateInfo = {};
    renderingCreateInfo.sType                         = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.viewMask                      = m_createInfo.viewMask;
    VkFormat defaultColorFormat                       = VulkanContext::GetSwapchainImageFormat();
    if(m_createInfo.colorFormats.empty() && m_createInfo.useColor)
    {
        renderingCreateInfo.colorAttachmentCount    = 1;
        renderingCreateInfo.pColorAttachmentFormats = &defaultColorFormat;
    }
    else
    {
        renderingCreateInfo.colorAttachmentCount    = static_cast<uint32_t>(m_createInfo.colorFormats.size());
        renderingCreateInfo.pColorAttachmentFormats = m_createInfo.colorFormats.data();
    }

    renderingCreateInfo.depthAttachmentFormat   = m_createInfo.useDepth ? m_createInfo.depthFormat : VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = m_createInfo.useStencil ? m_createInfo.stencilFormat : VK_FORMAT_UNDEFINED;

    // ##################### PIPELINE #####################
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount                   = static_cast<uint32_t>(stagesCI.size());
    pipelineInfo.pStages                      = stagesCI.data();
    pipelineInfo.pVertexInputState            = &vertexInput;
    pipelineInfo.pInputAssemblyState          = &assembly;
    pipelineInfo.pViewportState               = &viewportState;
    pipelineInfo.pRasterizationState          = &rasterizer;
    pipelineInfo.pMultisampleState            = &multisample;
    pipelineInfo.pColorBlendState             = &colorBlend;
    pipelineInfo.pDepthStencilState           = &depthStencil;
    if(m_createInfo.allowDerivatives)
        pipelineInfo.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    else if(m_createInfo.parent)
    {
        pipelineInfo.flags              = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        pipelineInfo.basePipelineHandle = m_createInfo.parent->m_pipeline;
        pipelineInfo.basePipelineIndex  = -1;
    }
    if(m_createInfo.useDynamicViewport)
        pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout     = m_layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass    = 0;

    pipelineInfo.pNext = &renderingCreateInfo;

    VK_CHECK(vkCreateGraphicsPipelines(VulkanContext::GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline), "Failed to create graphics pipeline");
}

void Pipeline::CreateComputePipeline()
{
    VkPipelineShaderStageCreateInfo shaderCi = {};
    shaderCi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderCi.stage                           = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderCi.pName                           = "main";
    shaderCi.module                          = m_shaders[0]->GetShaderModule();  // only 1 compute shader allowed

    VkPipelineLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCreateInfo.setLayoutCount             = m_descriptorLayouts.size();
    layoutCreateInfo.pSetLayouts                = m_descriptorLayouts.data();
    layoutCreateInfo.pushConstantRangeCount     = m_shaders[0]->m_pushConstantRange.size > 0 ? 1 : 0;
    layoutCreateInfo.pPushConstantRanges        = &m_shaders[0]->m_pushConstantRange;


    VK_CHECK(vkCreatePipelineLayout(VulkanContext::GetDevice(), &layoutCreateInfo, nullptr, &m_layout), "Failed to create compute pipeline layout");


    VkComputePipelineCreateInfo pipelineCI = {};
    pipelineCI.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.layout                      = m_layout;
    pipelineCI.stage                       = shaderCi;
    if(m_createInfo.allowDerivatives)
        pipelineCI.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    else if(m_createInfo.parent)
    {
        pipelineCI.flags              = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        pipelineCI.basePipelineHandle = m_createInfo.parent->m_pipeline;
        pipelineCI.basePipelineIndex  = -1;
    }

    VK_CHECK(vkCreateComputePipelines(VulkanContext::GetDevice(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline), "Failed to create compute pipeline");
}

void Pipeline::CreateRaytracingPipeline()
{
    std::vector<VkPushConstantRange> pushConstantRanges;
    for(const auto& shader : m_shaders)
    {
        if(shader->m_pushConstantRange.size > 0)
        {
            pushConstantRanges.push_back(shader->m_pushConstantRange);
        }
    }
    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount         = m_descriptorLayouts.size();
    pipelineLayoutCI.pSetLayouts            = m_descriptorLayouts.data();
    pipelineLayoutCI.pushConstantRangeCount = pushConstantRanges.size();
    pipelineLayoutCI.pPushConstantRanges    = pushConstantRanges.data();
    VK_CHECK(vkCreatePipelineLayout(VulkanContext::GetDevice(), &pipelineLayoutCI, nullptr, &m_layout), "Failed to create pipeline layout");

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    // Ray generation group
    {
        VkPipelineShaderStageCreateInfo shaderCi{};
        shaderCi.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderCi.stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        shaderCi.pName  = "main";
        shaderCi.module = m_shaders[0]->GetShaderModule();
        shaderStages.push_back(shaderCi);

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader      = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader   = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader       = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
        VkPipelineShaderStageCreateInfo shaderCi{};
        shaderCi.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderCi.stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
        shaderCi.pName  = "main";
        shaderCi.module = m_shaders[1]->GetShaderModule();
        shaderStages.push_back(shaderCi);

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader      = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader   = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader       = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Closest hit group
    {
        VkPipelineShaderStageCreateInfo shaderCi{};
        shaderCi.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderCi.stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        shaderCi.pName  = "main";
        shaderCi.module = m_shaders[2]->GetShaderModule();
        shaderStages.push_back(shaderCi);

        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroup.generalShader      = VK_SHADER_UNUSED_KHR;
        shaderGroup.closestHitShader   = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.anyHitShader       = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount                   = static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages                      = shaderStages.data();
    rayTracingPipelineCI.groupCount                   = static_cast<uint32_t>(shaderGroups.size());
    rayTracingPipelineCI.pGroups                      = shaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
    rayTracingPipelineCI.layout                       = m_layout;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(VulkanContext::GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &m_pipeline), "Failed to create ray tracing pipeline");


    // TODO: allow more flexible shader setup rather than 1 raygen + 1 miss + 1 closest hit
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2{};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &rayTracingPipelineProperties;
    vkGetPhysicalDeviceProperties2(VulkanContext::GetPhysicalDevice(), &deviceProperties2);

    const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
    const uint32_t alignment  = rayTracingPipelineProperties.shaderGroupHandleAlignment;

    auto alignUp = [](const uint32_t size, const uint32_t alignment)
    { return (size + (alignment - 1)) & ~(alignment - 1); };
    const uint32_t handleSizeAligned = alignUp(handleSize, alignment);

    const uint32_t sbtSize = shaderGroups.size() * handleSize;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(VulkanContext::GetDevice(), m_pipeline, 0, shaderGroups.size(), sbtSize, shaderHandleStorage.data()), "Failed to get ray tracing group handles");

    m_sbt.raygen.size   = alignUp(handleSizeAligned, rayTracingPipelineProperties.shaderGroupBaseAlignment);
    m_sbt.raygen.stride = m_sbt.raygen.size;

    int missCount     = 1;
    // TODO: these two can have more than 1 shader in them
    m_sbt.miss.size   = alignUp(missCount * handleSizeAligned, rayTracingPipelineProperties.shaderGroupBaseAlignment);
    m_sbt.miss.stride = handleSizeAligned;

    int closestHitCount     = 1;
    m_sbt.closestHit.size   = alignUp(closestHitCount * handleSizeAligned, rayTracingPipelineProperties.shaderGroupBaseAlignment);
    m_sbt.closestHit.stride = handleSizeAligned;

    m_sbt.callable = {};

    m_sbtBuffer.Allocate(m_sbt.raygen.size + m_sbt.miss.size + m_sbt.closestHit.size, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true);

    m_sbt.raygen.deviceAddress     = m_sbtBuffer.GetDeviceAddress();
    m_sbt.miss.deviceAddress       = m_sbtBuffer.GetDeviceAddress() + m_sbt.raygen.size;
    m_sbt.closestHit.deviceAddress = m_sbtBuffer.GetDeviceAddress() + m_sbt.raygen.size + m_sbt.miss.size;

    auto getHandle = [&](int i)
    { return shaderHandleStorage.data() + i * handleSize; };
    int handleIndex = 0;
    uint64_t offset = 0;
    m_sbtBuffer.Fill(getHandle(handleIndex++), handleSize, offset);
    offset = m_sbt.raygen.size;
    for(int i = 0; i < missCount; i++)
    {
        m_sbtBuffer.Fill(getHandle(handleIndex++), handleSize, offset);
        offset += m_sbt.miss.stride;
    }
    offset = m_sbt.raygen.size + m_sbt.miss.size;
    for(int i = 0; i < missCount; i++)
    {
        m_sbtBuffer.Fill(getHandle(handleIndex++), handleSize, offset);
        offset += m_sbt.closestHit.stride;
    }
}

void Pipeline::Bind(CommandBuffer& cb, uint32_t frameIndex) const
{
    VkPipelineBindPoint bindPoint;
    switch(m_createInfo.type)
    {
    case PipelineType::GRAPHICS:
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        break;
    case PipelineType::COMPUTE:
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        break;
    case PipelineType::RAYTRACING:
        bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        break;
    }
    if(!m_descriptorSets.empty())
    {
        vkCmdBindDescriptorSets(cb.GetCommandBuffer(), bindPoint, m_layout, 0, m_descriptorSets[frameIndex].size(), m_descriptorSets[frameIndex].data(), 0, nullptr);
    }
    for(auto& shader : m_shaders)
    {
        // FIXME: push constants don't work if multiple stages use the same range. In this case we'd need to specify every stage flag in the vkCmdPushConstants call which we aren't doing yet
        shader->BindResources(cb, frameIndex, m_layout, bindPoint);
    }
    vkCmdBindPipeline(cb.GetCommandBuffer(), bindPoint, m_pipeline);
}
