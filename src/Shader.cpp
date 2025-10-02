#include "Shader.hpp"
#include "Renderer.hpp"
#include "VulkanContext.hpp"

#include <cmath>
#include <set>
#include <slang.h>
#include <slang-com-helper.h>
#include <slang-com-ptr.h>

#include <algorithm>
#include <ranges>


struct PairHash
{
    std::size_t operator()(const std::pair<uint32_t, uint32_t>& p) const noexcept
    {
        return (static_cast<std::size_t>(p.first) << 32) ^ static_cast<std::size_t>(p.second);
    }
};

VkShaderStageFlags SlangStageToVulkan(SlangStage stage);
VkDescriptorType SlangBindingTypeToVulkan(slang::BindingType bindingType);

static Slang::ComPtr<slang::IGlobalSession> globalSession;

Shader::Shader(const std::filesystem::path& path, VkShaderStageFlagBits stage, std::string_view entryPoint)
{
    m_stage = stage;

    m_name = std::format("{}::{}", path.filename().string(), entryPoint);

    if(!Compile(path, entryPoint))
    {
        abort();
    }
}
void Shader::Finalize(Pipeline* pipeline)
{
    m_pipeline = pipeline;

    if(m_uniformBufferSize > 0)
    {
        for(int i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            const auto& buf = m_uniformBuffers.emplace_back(m_uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
            VK_SET_DEBUG_NAME(buf.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, m_name.c_str());
        }
        for(int i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            for(const auto& [set, binding, size, offset] : m_uniformBufferInfos)
            {
                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.dstBinding      = binding;
                descriptorWrite.dstSet          = pipeline->m_descriptorSets[i][set];
                descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = m_uniformBuffers[i].GetVkBuffer();
                bufferInfo.range  = size;
                bufferInfo.offset = offset;

                descriptorWrite.pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(VulkanContext::GetDevice(), 1, &descriptorWrite, 0, nullptr);
            }
        }
    }
    m_pushConstantData.resize(m_pushConstantRange.size + m_pushConstantRange.offset);
}
void Shader::BindResources(CommandBuffer& cb, uint32_t frameIndex, VkPipelineLayout layout, VkPipelineBindPoint bindPoint) const
{
    if(m_pushConstantRange.size > 0)
        vkCmdPushConstants(cb.GetCommandBuffer(), layout, m_stage, m_pushConstantRange.offset, m_pushConstantRange.size, &m_pushConstantData[m_pushConstantRange.offset]);
}

void Shader::Dispatch(CommandBuffer& cb, uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ)
{
    assert(m_stage == VK_SHADER_STAGE_COMPUTE_BIT);
    uint32_t groupCountX = std::ceil(threadCountX / (float)m_numThreadsX);
    uint32_t groupCountY = std::ceil(threadCountY / (float)m_numThreadsY);
    uint32_t groupCountZ = std::ceil(threadCountZ / (float)m_numThreadsZ);

    vkCmdDispatch(cb.GetCommandBuffer(), groupCountX, groupCountY, groupCountZ);
}

bool Shader::Compile(const std::filesystem::path& path, std::string_view entryPoint)
{
    if(!globalSession.get())
        createGlobalSession(globalSession.writeRef());

    // 2. Create Session
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc   = {};
    targetDesc.format              = SLANG_SPIRV;
    targetDesc.profile             = globalSession->findProfile("spirv_latest");

    sessionDesc.targets                 = &targetDesc;
    sessionDesc.targetCount             = 1;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;


    auto parentPathStr                    = path.parent_path().string();  // std::string
    std::array<const char*, 1> searchPath = {parentPathStr.c_str()};
    sessionDesc.searchPaths               = searchPath.data();
    sessionDesc.searchPathCount           = searchPath.size();

    std::vector<slang::CompilerOptionEntry> options = {
        {
         {slang::CompilerOptionName::EmitSpirvDirectly, {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}},
         {slang::CompilerOptionName::DebugInformation, {slang::CompilerOptionValueKind::Int, SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_MAXIMAL, 0, nullptr, nullptr}},
         }
    };
    sessionDesc.compilerOptionEntries    = options.data();
    sessionDesc.compilerOptionEntryCount = options.size();

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());

    // 3. Load module
    std::string path_str = path.filename().string();
    Slang::ComPtr<slang::IModule> slangModule;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule = session->loadModule(path_str.c_str(), diagnosticsBlob.writeRef());
        if(diagnosticsBlob != nullptr)
        {
            Log::Error("{}", (const char*)diagnosticsBlob->getBufferPointer());
        }
        if(!slangModule)
        {
            return false;
        }
    }

    // 4. Query Entry Points
    Slang::ComPtr<slang::IEntryPoint> shaderEntryPoint;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule->findEntryPointByName(entryPoint.data(), shaderEntryPoint.writeRef());
        if(!shaderEntryPoint)
        {
            Log::Error("Error getting entry point {}", entryPoint);
            return false;
        }
    }

    // 5. Compose Modules + Entry Points
    std::array<slang::IComponentType*, 2> componentTypes = {
        slangModule,
        shaderEntryPoint};

    Slang::ComPtr<slang::IComponentType> composedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef());
        if(diagnosticsBlob != nullptr)
        {
            Log::Error("{}", (const char*)diagnosticsBlob->getBufferPointer());
        }
        if(result < 0)
            Log::Error("Failed to compose shader program");
    }
    // 6. Link
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = composedProgram->link(
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
        if(diagnosticsBlob != nullptr)
        {
            Log::Error("{}", (const char*)diagnosticsBlob->getBufferPointer());
        }
        if(result < 0)
        {
            Log::Error("Failed to link shader program");
            return false;
        }
    }

    // 7. Get Target Kernel Code
    Slang::ComPtr<slang::IBlob> spirvCode;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangResult result = linkedProgram->getEntryPointCode(
            0,
            0,
            spirvCode.writeRef(),
            diagnosticsBlob.writeRef());
        if(diagnosticsBlob != nullptr)
        {
            Log::Error("{}", (const char*)diagnosticsBlob->getBufferPointer());
        }
        if(result < 0)
        {
            Log::Error("Failed to compile shader program");
            return false;
        }
    }
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize                 = spirvCode->getBufferSize();
    createInfo.pCode                    = (const uint32_t*)spirvCode->getBufferPointer();


    VK_CHECK(vkCreateShaderModule(VulkanContext::GetDevice(), &createInfo, nullptr, &m_shaderModule), "Failed to create shader module");
    Reflect(composedProgram->getLayout());

    return true;
}

struct BindingSlot
{
    uint64_t set          = 0;
    uint64_t binding      = 0;
    uint64_t offset       = 0;
    uint64_t pushConstant = 0;
    bool isPushConstant   = false;
};
BindingSlot CalculateOffset(const std::deque<slang::VariableLayoutReflection*>& path, slang::ParameterCategory unit)
{
    BindingSlot slot{};
    // special handling because of the possible implicit constant buffers

    if(unit == slang::ParameterCategory::Uniform)
    {
        bool foundCB = false;
        for(const auto v : path)
        {
            slot.offset += v->getOffset(slang::ParameterCategory::Uniform);

            if(foundCB)
            {
                slot.set     += v->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot) + v->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
                slot.binding += v->getOffset(slang::ParameterCategory::DescriptorTableSlot);

                if(slot.isPushConstant)
                    slot.pushConstant += v->getOffset(slang::ParameterCategory::PushConstantBuffer);
            }
            else if(v->getTypeLayout()->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
            {
                slot.set     = v->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot) + v->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
                slot.binding = v->getOffset(slang::ParameterCategory::DescriptorTableSlot);

                foundCB = true;
                if(v->getTypeLayout()->getSize(slang::ParameterCategory::PushConstantBuffer) > 0)
                {
                    slot.pushConstant   = v->getOffset(slang::ParameterCategory::PushConstantBuffer);
                    slot.isPushConstant = true;
                }
            }
        }
    }
    else
    {
        for(const auto v : path)
        {
            slot.set     += v->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot) + v->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
            slot.binding += v->getOffset(slang::ParameterCategory::DescriptorTableSlot);

            // TODO this doesnt make sense? I think only uniforms can go in PC
            slot.pushConstant += v->getOffset(slang::ParameterCategory::PushConstantBuffer);
        }
    }

    return slot;
}

std::string GetName(const std::deque<slang::VariableLayoutReflection*>& pathStack)
{
    std::string res;
    for(const auto v : std::ranges::reverse_view(pathStack))
    {
        auto n = v->getName();
        if(n)
            res += std::string(n) + ".";
    }
    return res.substr(0, res.length() - 1);
}


void Shader::GetLayout(slang::VariableLayoutReflection* vl, std::deque<slang::VariableLayoutReflection*>& pathStack, bool isEntryPoint)
{
    pathStack.push_front(vl);

    auto tl = vl->getTypeLayout();

    const char* varName = vl->getName();
    if(!varName)
        varName = "<anon_var>";

    const char* typeName = tl->getName();
    if(!typeName)
        typeName = "<anon_type>";

    auto category    = vl->getCategory();
    BindingSlot slot = CalculateOffset(pathStack, category);


    auto fieldCount = vl->getTypeLayout()->getFieldCount();
    for(unsigned int i = 0; i < fieldCount; i++)
    {
        auto field = tl->getFieldByIndex(i);
        GetLayout(field, pathStack, isEntryPoint);
    }

    switch(tl->getKind())
    {
    case slang::TypeReflection::Kind::ConstantBuffer:
    case slang::TypeReflection::Kind::ShaderStorageBuffer:
    case slang::TypeReflection::Kind::ParameterBlock:
    case slang::TypeReflection::Kind::TextureBuffer:
        GetLayout(tl->getElementVarLayout(), pathStack, isEntryPoint);
        break;
    default:
        break;
    }

    auto kind = tl->getKind();
    if(category != slang::ParameterCategory::None && kind != slang::TypeReflection::Kind::ParameterBlock && category != slang::ParameterCategory::RayPayload && category != slang::ParameterCategory::HitAttributes)
    {
        if(fieldCount == 0)
        {
            auto name        = GetName(pathStack);
            auto bindingType = tl->getBindingRangeCount() == 1 ? tl->getBindingRangeType(0) : slang::BindingType::ConstantBuffer;

            if(name.empty())
            {
                // asm("int3");
            }
            else
            {
                bool isPushConstant     = slot.isPushConstant || bindingType == slang::BindingType::PushConstant;
                bool isStructuredBuffer = tl->getKind() == slang::TypeReflection::Kind::Resource && tl->getResourceShape() == SlangResourceShape::SLANG_STRUCTURED_BUFFER;
                uint32_t stride         = tl->getKind() == slang::TypeReflection::Kind::Array ? tl->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM) : 0;

                if(isStructuredBuffer)
                    stride = tl->getElementTypeLayout()->getStride();  // FIXME: This seems to give wrong values for buffers using ScalarDataLayout

                if(bindingType == slang::BindingType::PushConstant)
                {
                    uint32_t size = tl->getElementTypeLayout()->getSize(slang::ParameterCategory::Uniform);
                    if(m_pushConstantSizes.size() <= slot.pushConstant)
                    {
                        m_pushConstantSizes.resize(slot.pushConstant + 1);  // we shouldnt have that many ranges so performance doesnt matter that much here
                    }
                    m_pushConstantSizes[slot.pushConstant] = size;
                }
                else
                {
                    uint64_t size = tl->getSize(slang::ParameterCategory::Uniform);

                    m_bindings[name] = {
                        .set               = isPushConstant ? static_cast<uint32_t>(slot.pushConstant) : static_cast<uint32_t>(slot.set),
                        .binding           = static_cast<uint32_t>(slot.binding),
                        .offset            = slot.offset,
                        .size              = size,
                        .stride            = stride,
                        .arrayElementCount = tl->getTotalArrayElementCount(),
                        .type              = slot.isPushConstant ? VK_DESCRIPTOR_TYPE_MAX_ENUM : SlangBindingTypeToVulkan(bindingType),
                        .isPushConstant    = isPushConstant,
                        .isVariableSize    = tl->getKind() == slang::TypeReflection::Kind::Array && tl->getTotalArrayElementCount() == 0,
                    };
                }
            }
        }
    }

    pathStack.pop_front();
}


void Shader::Reflect(slang::ProgramLayout* layout)
{
    std::deque<slang::VariableLayoutReflection*> pathStack;
    auto* globals = layout->getGlobalParamsVarLayout();

    auto* entryPoint = layout->getEntryPointByIndex(0);

    GetLayout(globals, pathStack, false);
    GetLayout(entryPoint->getVarLayout(), pathStack, true);

    if(m_stage == VK_SHADER_STAGE_COMPUTE_BIT)
    {
        SlangUInt numThreads[3];
        entryPoint->getComputeThreadGroupSize(3, numThreads);
        m_numThreadsX = numThreads[0];
        m_numThreadsY = numThreads[1];
        m_numThreadsZ = numThreads[2];
    }

    {
        uint32_t totalSize     = 0;
        uint32_t initialOffset = std::numeric_limits<uint32_t>::max();
        for(auto& [name, binding] : m_bindings)
        {
            if(!binding.isPushConstant)
                continue;

            uint32_t baseOffset = 0;
            for(uint32_t i = 0; i < binding.set; i++)
            {
                baseOffset += m_pushConstantSizes[i];
            }
            binding.offset += baseOffset;

            totalSize     += binding.size;
            initialOffset  = std::min(static_cast<uint32_t>(binding.offset), initialOffset);
        }
        m_pushConstantRange.size       = totalSize;
        m_pushConstantRange.offset     = totalSize > 0 ? initialOffset : 0;  // don't add it if no push constants
        m_pushConstantRange.stageFlags = m_stage;
    }


    {
        const uint64_t alignment = VulkanContext::GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
        std::unordered_map<std::pair<uint32_t, uint32_t>, uint64_t, PairHash> aggregatedSizes;

        for(const auto& [name, binding] : m_bindings)
        {
            if(binding.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                continue;

            std::pair<uint32_t, uint32_t> key{binding.set, binding.binding};

            aggregatedSizes[key] = std::max(aggregatedSizes[key], binding.size + binding.offset);
        }

        m_uniformBufferInfos.clear();
        uint64_t currentOffset = 0;

        for(const auto& [key, totalSize] : aggregatedSizes)
        {
            uint32_t setIdx     = key.first;
            uint32_t bindingIdx = key.second;

            // Align the offset according to Vulkan requirements.
            if(alignment != 0)
            {
                currentOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
            }

            m_uniformBufferInfos.emplace_back(setIdx, bindingIdx, totalSize, currentOffset);

            currentOffset += totalSize;
        }
        m_uniformBufferSize = currentOffset;
    }

    {
        std::vector<std::set<uint32_t>> added(4);
        for(const auto& [name, binding] : m_bindings)
        {
            uint32_t set = binding.set;
            if(binding.isPushConstant)
                continue;
            if(added[set].contains(binding.binding))
                continue;
            m_descriptorLayoutBuilders[set].AddBinding(binding.binding, binding.type);
            added[set].insert(binding.binding);
        }
    }

    for(auto& [name, binding] : m_bindings)
    {
        auto type = string_VkDescriptorType(binding.type);
        if(binding.isPushConstant)
            Log::Info("{:30}: size:{} offset:{} stride:{} elementCount:{} type: PushConstant", name, binding.size, binding.offset, binding.stride, binding.arrayElementCount);
        else
            Log::Info("{:30}: set:{} binding:{} size:{} offset:{} stride:{} elementCount:{} type: {} variableSized:{}", name, binding.set, binding.binding, binding.size, binding.offset, binding.stride, binding.arrayElementCount, type, binding.isVariableSize);
    }
}

void Shader::SetParameter(uint32_t frameIndex, std::string_view name, const Image* image)
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
        Log::Error("Push constants can't contain images");
    }
    else
    {
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstBinding      = binding.binding;
        descriptorWrite.dstSet          = m_pipeline->m_descriptorSets[frameIndex][binding.set];
        descriptorWrite.descriptorType  = binding.type;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = image->GetLayout();
        imageInfo.imageView   = image->GetImageView();
        imageInfo.sampler     = VulkanContext::GetTextureSampler();

        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(VulkanContext::GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void Shader::SetParameter(uint32_t frameIndex, std::string_view name, const Buffer* buffer)
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
        Log::Error("Push constants can't contain buffers");
    }
    else
    {
        if(binding.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            Log::Warn("Trying to update binding {}, but it is not a storage buffer", name);
            return;
        }

        if(buffer->GetSize() % binding.stride != 0)
        {
            Log::Warn("Trying to update binding {} with a buffer whose size ({}) isn't divisible by the stride ({}). This may indicate a bug in your code", name, buffer->GetSize(), binding.stride);
        }

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstBinding      = binding.binding;
        descriptorWrite.dstSet          = m_pipeline->m_descriptorSets[frameIndex][binding.set];
        descriptorWrite.descriptorType  = binding.type;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer->GetVkBuffer();
        bufferInfo.range  = buffer->GetSize();
        bufferInfo.offset = 0;

        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(VulkanContext::GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void Shader::SetParameter(uint32_t frameIndex, std::string_view name, const Raytracing::TLAS& tlas)
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
        Log::Error("Push constants can't contain TLASes");
    }
    else
    {
        if(binding.type != VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        {
            Log::Warn("Trying to update binding {}, but it is not an acceleration structure", name);
            return;
        }

        VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
        descriptorAccelerationStructureInfo.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
        descriptorAccelerationStructureInfo.pAccelerationStructures    = &tlas.handle;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstBinding      = binding.binding;
        descriptorWrite.dstSet          = m_pipeline->m_descriptorSets[frameIndex][binding.set];
        descriptorWrite.descriptorType  = binding.type;
        descriptorWrite.pNext           = &descriptorAccelerationStructureInfo;

        vkUpdateDescriptorSets(VulkanContext::GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

// Maps a SlangStage enum to the corresponding Vulkan shader stage flags.
VkShaderStageFlags SlangStageToVulkan(SlangStage stage)
{
    switch(stage)
    {
    case SLANG_STAGE_VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case SLANG_STAGE_HULL:
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    case SLANG_STAGE_DOMAIN:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case SLANG_STAGE_GEOMETRY:
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    case SLANG_STAGE_FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case SLANG_STAGE_COMPUTE:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    case SLANG_STAGE_RAY_GENERATION:
        return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    case SLANG_STAGE_INTERSECTION:
        return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    case SLANG_STAGE_ANY_HIT:
        return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    case SLANG_STAGE_CLOSEST_HIT:
        return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    case SLANG_STAGE_MISS:
        return VK_SHADER_STAGE_MISS_BIT_KHR;
    case SLANG_STAGE_CALLABLE:
        return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    case SLANG_STAGE_MESH:
        return VK_SHADER_STAGE_MESH_BIT_EXT;
    default:
        return VK_SHADER_STAGE_ALL;
    }
}

// Maps a Slang binding type to the corresponding Vulkan descriptor type.
VkDescriptorType SlangBindingTypeToVulkan(slang::BindingType bindingType)
{
    switch(bindingType)
    {
    case slang::BindingType::PushConstant:
    default:
        // assert(!"Unhandled Slang binding type!");
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;

    case slang::BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case slang::BindingType::CombinedTextureSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case slang::BindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case slang::BindingType::MutableTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case slang::BindingType::TypedBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case slang::BindingType::MutableTypedBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case slang::BindingType::InputRenderTarget:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    case slang::BindingType::InlineUniformData:
        return VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
    case slang::BindingType::RayTracingAccelerationStructure:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    case slang::BindingType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}
