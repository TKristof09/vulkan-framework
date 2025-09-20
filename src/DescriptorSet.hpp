#pragma once
#include <volk.h>
#include <vector>


struct DescriptorSetLayoutBuilder
{
    void AddBinding(uint32_t binding, VkDescriptorType t, uint32_t count = 1);
    VkDescriptorSetLayout Build(VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL, VkDescriptorSetLayoutCreateFlags flags = 0);
    void operator+=(const DescriptorSetLayoutBuilder& other);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};
