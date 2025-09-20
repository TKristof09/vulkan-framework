#pragma once

#include "Buffer.hpp"
#include "Model.hpp"

namespace Raytracing
{
struct BLAS
{
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer buffer;

    void Destroy()
    {
        vkDestroyAccelerationStructureKHR(VulkanContext::GetDevice(), handle, nullptr);
    }
};
struct TLAS
{
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer buffer;

    void Destroy()
    {
        vkDestroyAccelerationStructureKHR(VulkanContext::GetDevice(), handle, nullptr);
    }
};

// TODO: review this build process. It would probably be better to build 1 BLAS
// per mesh inside the model, then 1 TLAS containing a bunch of BLASes. But for
// now this should be fine for small scenes
BLAS CreateBLAS(const Model& model);
TLAS CreateTLAS(const BLAS& blas);
};
