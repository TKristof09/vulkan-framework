#include "DescriptorSet.hpp"
#include "VulkanContext.hpp"
#include <set>

void DescriptorSetLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType t, uint32_t count)
{
    VkDescriptorSetLayoutBinding b{};
    b.binding         = binding;
    b.descriptorCount = count;
    b.descriptorType  = t;

    bindings.push_back(b);
}

VkDescriptorSetLayout DescriptorSetLayoutBuilder::Build(VkShaderStageFlags stageFlags, VkDescriptorSetLayoutCreateFlags flags)
{
    for(auto& binding : bindings)
    {
        binding.stageFlags = stageFlags;
    }
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.flags        = flags | VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCi = {};
    std::vector<VkDescriptorBindingFlags> bindingFlags(bindings.size(), VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

    bindingFlagsCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsCi.pBindingFlags = bindingFlags.data();
    bindingFlagsCi.bindingCount  = static_cast<uint32_t>(bindingFlags.size());

    ci.pNext = &bindingFlagsCi;


    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(VulkanContext::GetDevice(), &ci, nullptr, &layout), "Failed to create descriptor set layout");
    return layout;
}


void DescriptorSetLayoutBuilder::operator+=(const DescriptorSetLayoutBuilder& other)
{
    bindings.append_range(other.bindings);
    std::set<uint32_t> s;
    for(auto binding : bindings)
    {
        if(s.contains(binding.binding))
        {
            Log::Error("Same binding {} defined in multiple shaders, this behaviour is not allowed", binding.binding);
        }
        s.insert(binding.binding);
    }
}
