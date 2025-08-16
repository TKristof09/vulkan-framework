#include "DescriptorSet.hpp"
#include "VulkanContext.hpp"

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
    ci.flags        = flags;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();
    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(VulkanContext::GetDevice(), &ci, nullptr, &layout), "Failed to create descriptor set layout");
    return layout;
}
