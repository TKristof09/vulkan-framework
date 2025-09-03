#include "Shader.hpp"
#include "Renderer.hpp"
#include "VulkanContext.hpp"

#include <slang.h>
#include <slang-com-helper.h>
#include <slang-com-ptr.h>


VkShaderStageFlags SlangStageToVulkan(SlangStage stage);
VkDescriptorType SlangBindingTypeToVulkan(slang::BindingType bindingType);

Shader::Shader(const std::string& filename, VkShaderStageFlagBits stage)
{
    std::filesystem::path path = filename;
    m_stage                    = stage;

    m_name = path.filename().string();

    if(!Compile(path))
    {
        abort();
    }

    CreateDescriptors();
    if(m_uniformBufferSize > 0)
    {
        for(int i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_uniformBuffers.emplace_back(m_uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        }
        for(int i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            for(const auto& [set, binding, size, offset] : m_uniformBufferInfos)
            {
                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.dstBinding      = binding;
                descriptorWrite.dstSet          = m_descriptorSets[i][set];
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
}

bool Shader::Compile(const std::filesystem::path& path)
{
    // 1. Create Global Session
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    createGlobalSession(globalSession.writeRef());

    // 2. Create Session
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc   = {};
    targetDesc.format              = SLANG_SPIRV;
    targetDesc.profile             = globalSession->findProfile("spirv_1_5");

    sessionDesc.targets     = &targetDesc;
    sessionDesc.targetCount = 1;

    auto parentPathStr                    = path.parent_path().string();  // std::string
    std::array<const char*, 1> searchPath = {parentPathStr.c_str()};
    sessionDesc.searchPaths               = searchPath.data();
    sessionDesc.searchPathCount           = searchPath.size();

    std::array<slang::CompilerOptionEntry, 2> options = {
        {{slang::CompilerOptionName::EmitSpirvDirectly,
          {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}},
         {slang::CompilerOptionName::DebugInformation,
          {slang::CompilerOptionValueKind::Int, SlangDebugInfoLevel::SLANG_DEBUG_INFO_LEVEL_MAXIMAL, 0, nullptr, nullptr}}}
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
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule->findEntryPointByName("main", entryPoint.writeRef());
        if(!entryPoint)
        {
            Log::Error("Error getting entry point");
            return false;
        }
    }

    // 5. Compose Modules + Entry Points
    std::array<slang::IComponentType*, 2> componentTypes = {
        slangModule,
        entryPoint};

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


void Shader::ParsePushConsts(uint32_t rangeIndex, slang::VariableLayoutReflection* var, std::string path, uint32_t offset)
{
    if(var->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Struct)
    {
        int fieldCount = var->getTypeLayout()->getFieldCount();

        for(int i = 0; i < fieldCount; i++)
        {
            auto field = var->getTypeLayout()->getFieldByIndex(i);

            if(field->getTypeLayout()->getSize(slang::ParameterCategory::Uniform) > 0)
            {
                auto n    = field->getName();
                auto name = path + n;
                auto offs = field->getOffset() + offset;
                if(field->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Struct)
                {
                    ParsePushConsts(rangeIndex, field, name + ".", offs);
                }
                else
                {
                    m_bindings[name] = {
                        .set            = rangeIndex,
                        .binding        = 0,
                        .offset         = offs,
                        .size           = field->getTypeLayout()->getSize(slang::ParameterCategory::Uniform),
                        .type           = VK_DESCRIPTOR_TYPE_MAX_ENUM,
                        .isPushConstant = true};
                }
            }
        }
    }
};


void Shader::ParseStruct(Offset binding, slang::VariableLayoutReflection* var, std::string path, uint32_t offset)
{
    if(var->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Struct)
    {
        int fieldCount = var->getTypeLayout()->getFieldCount();

        for(int i = 0; i < fieldCount; i++)
        {
            auto field = var->getTypeLayout()->getFieldByIndex(i);

            if(field->getTypeLayout()->getSize(slang::ParameterCategory::Uniform) > 0)
            {
                auto n      = field->getName();
                auto name   = path + n;
                auto offs   = field->getOffset() + offset + m_uniformBufferSize;
                auto stride = field->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Array ? field->getTypeLayout()->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM) : 0;
                if(field->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Struct)
                {
                    ParseStruct(binding, field, name + ".", offs);
                }
                else
                {
                    m_bindings[name] = {
                        .set            = binding.bindingSet,
                        .binding        = binding.binding,
                        .offset         = offs,
                        .size           = field->getTypeLayout()->getSize(slang::ParameterCategory::Uniform),
                        .stride         = static_cast<uint32_t>(stride),
                        .type           = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .isPushConstant = false};
                }
            }
        }
    }
};
void Shader::GetLayout(slang::VariableLayoutReflection* vl, Offset offset, std::string path, bool isParameterBlock)
{
    auto tl = vl->getTypeLayout();

    int bindingRangeCount = tl->getBindingRangeCount();
    for(int r = 0; r < bindingRangeCount; ++r)
    {
        slang::BindingType bindingRangeType = tl->getBindingRangeType(r);

        switch(bindingRangeType)
        {
        default:
            break;

        // We will skip over ranges that represent sub-objects for now, and handle
        // them in a separate pass.
        //
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ExistentialValue:
        case slang::BindingType::PushConstant:
            continue;
        }


        auto descriptorRangeCount = tl->getBindingRangeDescriptorRangeCount(r);
        if(descriptorRangeCount == 0)
            continue;
        auto slangDescriptorSetIndex = tl->getBindingRangeDescriptorSetIndex(r);

        auto firstDescriptorRangeIndex = tl->getBindingRangeFirstDescriptorRangeIndex(r);
        for(int j = 0; j < descriptorRangeCount; ++j)
        {
            auto descriptorRangeIndex = firstDescriptorRangeIndex + j;
            auto slangDescriptorType  = tl->getDescriptorSetDescriptorRangeType(
                slangDescriptorSetIndex,
                descriptorRangeIndex);

            // Certain kinds of descriptor ranges reflected by Slang do not
            // manifest as descriptors at the Vulkan level, so we will skip those.
            //
            switch(slangDescriptorType)
            {
            case slang::BindingType::ExistentialValue:
            case slang::BindingType::InlineUniformData:
            case slang::BindingType::PushConstant:
                continue;
            default:
                break;
            }

            auto vkDescriptorType    = SlangBindingTypeToVulkan(slangDescriptorType);
            uint32_t descriptorCount = (uint32_t)tl->getDescriptorSetDescriptorRangeDescriptorCount(
                slangDescriptorSetIndex,
                descriptorRangeIndex);


            uint32_t descriptorSetIndex = isParameterBlock ? offset.subelement : offset.bindingSet;
            uint32_t binding            = offset.binding + (uint32_t)tl->getDescriptorSetDescriptorRangeIndexOffset(slangDescriptorSetIndex, descriptorRangeIndex);
            m_descriptorLayoutBuilders[descriptorSetIndex].AddBinding(binding, vkDescriptorType, descriptorCount);

            auto leafVar = tl->getBindingRangeLeafVariable(r);
            auto name    = leafVar ? (path + leafVar->getName()) : path.substr(0, path.length() - 1);

            m_bindings[name] = {.set = descriptorSetIndex, .binding = binding, .offset = 0, .type = vkDescriptorType};
        }
    }
    auto subObjectRangeCount = tl->getSubObjectRangeCount();
    for(auto subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; ++subObjectRangeIndex)
    {
        auto bindingRangeIndex = tl->getSubObjectRangeBindingRangeIndex(subObjectRangeIndex);
        auto bindingRangeType  = tl->getBindingRangeType(bindingRangeIndex);

        auto subObjectTypeLayout = tl->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        auto subobjectOffsetVl  = tl->getSubObjectRangeOffset(subObjectRangeIndex);
        auto subObjectOffset    = offset;
        subObjectOffset        += Offset(subobjectOffsetVl);

        auto varName = tl->getBindingRangeLeafVariable(bindingRangeIndex)->getName();
        auto varPath = path + (varName ? (std::string(varName) + ".") : "");
        switch(bindingRangeType)
        {
        // A `ParameterBlock<X>` never contributes descripto ranges to the
        // decriptor sets of a parent object.
        //
        case slang::BindingType::ParameterBlock:
            {
                if(subObjectTypeLayout->getElementTypeLayout()->getSize() > 0)
                {
                    m_descriptorLayoutBuilders[subObjectOffset.subelement].AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                    subObjectOffset.binding += 1;
                }
                GetLayout(subObjectTypeLayout->getElementVarLayout(), subObjectOffset, varPath, true);
            }
        default:
            break;

        case slang::BindingType::ExistentialValue:
            assert(!"TODO: Idk what these are");
            break;

        case slang::BindingType::ConstantBuffer:
            {
                // A `ConstantBuffer<X>` range will contribute any nested descriptor
                // ranges in `X`, along with a leading descriptor range for a
                // uniform buffer to hold ordinary/uniform data, if there is any.

                auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();

                auto elementVarLayout = subObjectTypeLayout->getElementVarLayout();

                auto elementTypeLayout = elementVarLayout->getTypeLayout();

                Offset containerOffset  = subObjectOffset;
                containerOffset        += Offset(subObjectTypeLayout->getContainerVarLayout());

                Offset elementOffset  = subObjectOffset;
                elementOffset        += Offset(elementVarLayout);

                // If the type has ordinary uniform data fields, we need to make sure to create
                // a descriptor set with a constant buffer binding in the case that the shader
                // object is bound as a stand alone parameter block.
                if(elementTypeLayout->getSize(slang::ParameterCategory::Uniform) != 0)
                {
                    auto descriptorSetIndex = isParameterBlock ? containerOffset.subelement : containerOffset.bindingSet;
                    m_descriptorLayoutBuilders[descriptorSetIndex].AddBinding(containerOffset.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                    if(elementTypeLayout->getKind() == slang::TypeReflection::Kind::Struct)
                    {
                        ParseStruct(containerOffset, elementVarLayout, varPath, containerVarLayout->getOffset());
                    }
                    uint64_t size = elementTypeLayout->getSize(slang::ParameterCategory::Uniform);

                    m_uniformBufferInfos.push_back({descriptorSetIndex, containerOffset.binding, size, m_uniformBufferSize});

                    m_uniformBufferSize += size;
                }
                GetLayout(elementVarLayout, elementOffset, varPath);
            }
            break;

        case slang::BindingType::PushConstant:
            {
                // This case indicates a `ConstantBuffer<X>` that was marked as being
                // used for push constants.
                //
                // Much of the handling is the same as for an ordinary
                // `ConstantBuffer<X>`, but of course we need to handle the ordinary
                // data part differently.

                auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();

                auto elementVarLayout  = subObjectTypeLayout->getElementVarLayout();
                auto elementTypeLayout = elementVarLayout->getTypeLayout();

                Offset containerOffset  = subObjectOffset;
                containerOffset        += Offset(subObjectTypeLayout->getContainerVarLayout());

                Offset elementOffset  = subObjectOffset;
                elementOffset        += Offset(elementVarLayout);


                uint32_t size = static_cast<uint32_t>(elementTypeLayout->getSize(slang::ParameterCategory::Uniform));
                if(size > 0)
                {
                    auto rangeIndex = containerOffset.pushConstantRange;
                    ParsePushConsts(rangeIndex, elementVarLayout, varPath, containerVarLayout->getOffset());
                    VkPushConstantRange pc = {
                        .stageFlags = VK_SHADER_STAGE_ALL,
                        .offset     = 0xFFFFFFFF,
                        .size       = size,
                    };
                    if(m_pushConstants.size() <= rangeIndex)
                    {
                        m_pushConstants.resize(rangeIndex + 1);  // we shouldnt have that many ranges so performance doesnt matter that much here
                    }
                    m_pushConstants[rangeIndex] = pc;
                }
                if(elementVarLayout->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Struct && varPath.empty())
                {
                    int fieldCount = elementVarLayout->getTypeLayout()->getFieldCount();

                    for(int i = 0; i < fieldCount; i++)
                    {
                        auto field = elementVarLayout->getTypeLayout()->getFieldByIndex(i);
                        Offset fieldOffset(field);
                        fieldOffset += elementOffset;
                        GetLayout(field, fieldOffset, std::string(field->getName()) + ".");
                    }
                }
                else
                    GetLayout(elementVarLayout, elementOffset, varPath);
            }
            break;
        }
    }
}
void Shader::Reflect(slang::ProgramLayout* layout)
{
    auto* globals    = layout->getGlobalParamsVarLayout();
    auto* entryPoint = layout->getEntryPointByIndex(0);

    GetLayout(globals, Offset(globals), "");
    GetLayout(entryPoint->getVarLayout(), Offset(entryPoint->getVarLayout()), "");

    uint32_t totalSize = 0;
    for(size_t i = 0; i < m_pushConstants.size(); ++i)
    {
        m_pushConstants[i].offset = totalSize;
        for(auto& [_, binding] : m_bindings)
        {
            if(binding.isPushConstant && binding.set == i)
            {
                binding.offset += totalSize;
            }
        }
        totalSize += m_pushConstants[i].size;
    }

    for(auto builder : m_descriptorLayoutBuilders)
    {
        // NOTE: can we have non continuous descriptors? like 0 and 2 are filled but not 1
        if(!builder.bindings.empty())
            m_descriptorLayouts.push_back(builder.Build());
    }
}

void Shader::CreateDescriptors()
{
    m_descriptorSets.resize(Renderer::MAX_FRAMES_IN_FLIGHT);
    for(int frameIndex = 0; frameIndex < Renderer::MAX_FRAMES_IN_FLIGHT; frameIndex++)
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = VulkanContext::GetDescriptorPool();
        allocInfo.descriptorSetCount = static_cast<uint32_t>(m_descriptorLayouts.size());
        allocInfo.pSetLayouts        = m_descriptorLayouts.data();

        m_descriptorSets[frameIndex].resize(m_descriptorLayouts.size());

        VK_CHECK(vkAllocateDescriptorSets(VulkanContext::GetDevice(), &allocInfo, m_descriptorSets[frameIndex].data()), "Failed to allocate descriptors");
    }
}
void Shader::SetParameter(uint32_t frameIndex, std::string_view name, Image* image)
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
        Log::Warn("Push constants can't contain images");
    }
    else
    {
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstBinding      = binding.binding;
        descriptorWrite.dstSet          = m_descriptorSets[frameIndex][binding.set];
        descriptorWrite.descriptorType  = binding.type;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = image->GetLayout();
        imageInfo.imageView   = image->GetImageView();
        imageInfo.sampler     = VulkanContext::GetTextureSampler();

        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(VulkanContext::GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void Shader::SetParameter(uint32_t frameIndex, std::string_view name, Buffer* buffer)
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
        Log::Warn("Push constants can't contain buffers");
    }
    else
    {
        if(binding.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            Log::Warn("Trying to update binding {}, but it is not a storage buffer", name);
            return;
        }

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.dstBinding      = binding.binding;
        descriptorWrite.dstSet          = m_descriptorSets[frameIndex][binding.set];
        descriptorWrite.descriptorType  = binding.type;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer->GetVkBuffer();
        bufferInfo.range  = buffer->GetSize();
        bufferInfo.offset = 0;

        descriptorWrite.pBufferInfo = &bufferInfo;

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
#if 0
std::string printTypeKind(slang::TypeReflection::Kind kind)
{
    switch(kind)
    {
#define CASE(TAG)                          \
    case slang::TypeReflection::Kind::TAG: \
        return #TAG;

        CASE(None);
        CASE(Struct);
        CASE(Array);
        CASE(Matrix);
        CASE(Vector);
        CASE(Scalar);
        CASE(ConstantBuffer);
        CASE(Resource);
        CASE(SamplerState);
        CASE(TextureBuffer);
        CASE(ShaderStorageBuffer);
        CASE(ParameterBlock);
        CASE(GenericTypeParameter);
        CASE(Interface);
        CASE(OutputStream);
        CASE(Specialized);
        CASE(Feedback);
        CASE(Pointer);
        CASE(DynamicResource);
#undef CASE

    default:
        return "";
    }
}
#include <format>
std::string printLayoutUnit(slang::ParameterCategory layoutUnit)
{
    switch(layoutUnit)
    {
#define CASE(TAG, DESCRIPTION)          \
    case slang::ParameterCategory::TAG: \
        return std::format("{} ({})", #TAG, DESCRIPTION);

        CASE(ConstantBuffer, "constant buffer slots");
        CASE(ShaderResource, "texture slots");
        CASE(UnorderedAccess, "uav slots");
        CASE(VaryingInput, "varying input slots");
        CASE(VaryingOutput, "varying output slots");
        CASE(SamplerState, "sampler slots");
        CASE(Uniform, "bytes");
        CASE(DescriptorTableSlot, "bindings");
        CASE(SpecializationConstant, "specialization constant ids");
        CASE(PushConstantBuffer, "push-constant buffers");
        CASE(RegisterSpace, "register space offset for a variable");
        CASE(GenericResource, "generic resources");
        CASE(RayPayload, "ray payloads");
        CASE(HitAttributes, "hit attributes");
        CASE(CallablePayload, "callable payloads");
        CASE(ShaderRecord, "shader records");
        CASE(ExistentialTypeParam, "existential type parameters");
        CASE(ExistentialObjectParam, "existential object parameters");
        CASE(SubElementRegisterSpace, "register spaces / descriptor sets");
        CASE(InputAttachmentIndex, "subpass input attachments");
        CASE(MetalArgumentBufferElement, "Metal argument buffer elements");
        CASE(MetalAttribute, "Metal attributes");
        CASE(MetalPayload, "Metal payloads");
#undef CASE

    default:
        return "";
    }
}


void varlayout(slang::VariableLayoutReflection* vl, int ident = 0)
{
    auto pad    = std::format("{:>{}}", "", ident);
    ident      += 4;
    auto name   = vl->getName();
    auto tl     = vl->getTypeLayout();
    auto tname  = tl->getName();
    auto kind   = tl->getKind();
    auto s      = tl->getSize();

    Log::Info("-------------------------");
    if(name)
    {
        Log::Info("{} name: {}", pad, name);
    }

    if(tname)
    {
        Log::Info("{} tname: {}", pad, tname);
    }

    Log::Info("{} binding ranges: {}", pad, tl->getBindingRangeCount());
    Log::Info("{} kind: {}", pad, printTypeKind(kind));
    for(int i = 0; i < tl->getCategoryCount(); i++)
    {
        auto unit = tl->getCategoryByIndex(i);
        s         = tl->getSize(unit);
        Log::Info("{} size: {} {}", pad, s, printLayoutUnit(unit));
        if(unit == slang::ParameterCategory::DescriptorTableSlot || unit == slang::ParameterCategory::SubElementRegisterSpace)
        {
            auto binding = vl->getBindingIndex();
            auto set     = vl->getBindingSpace();
            Log::Info("{} set: {} binding: {}", pad, set, binding);
        }
    }


    if(kind == slang::TypeReflection::Kind::Struct)
    {
        int paramCount = tl->getFieldCount();
        for(int i = 0; i < paramCount; i++)
        {
            auto param = tl->getFieldByIndex(i);

            varlayout(param, ident);
        }
    }

    if(kind == slang::TypeReflection::Kind::ConstantBuffer)
    {
        Log::Info("{} ----CONTAINER----", pad);
        varlayout(tl->getContainerVarLayout(), ident);
        Log::Info("{} ----ELEMENT----", pad);
        varlayout(tl->getElementVarLayout(), ident);
    }

    if(kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        if(tl->getSize() > 0)
        {
            Log::Info("Automatic uniform buffer");
        }
        Log::Info("{} offset {}", pad, vl->getOffset(slang::ParameterCategory::SubElementRegisterSpace));
        Log::Info("{} ----CONTAINER----", pad);
        varlayout(tl->getContainerVarLayout(), ident);
        Log::Info("{} ----ELEMENT----", pad);
        varlayout(tl->getElementVarLayout(), ident);
    }
}
#endif
