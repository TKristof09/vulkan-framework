#include "Raytracing.hpp"
#include <cstring>

namespace Raytracing
{
BLAS CreateBLAS(const Model& model)
{
    std::vector<VkTransformMatrixKHR> transformMatrices{};
    for(const auto& mesh : model.GetMeshes())
    {
        for(const auto& _ : mesh.primitives)
        {
            VkTransformMatrixKHR transformMatrix{};
            auto m = glm::mat3x4(glm::transpose(mesh.transform));
            std::memcpy(&transformMatrix, (void*)&m, sizeof(glm::mat3x4));
            transformMatrices.push_back(transformMatrix);
        }
    }

    // TODO: maybe use device local? not sure how much it matters since this is a one time operation
    Buffer transformBuffer(transformMatrices.size() * sizeof(VkTransformMatrixKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    transformBuffer.Fill(transformMatrices);

    std::vector<uint32_t> maxPrimitiveCounts{};
    std::vector<VkAccelerationStructureGeometryKHR> geometries{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos{};
    for(const auto& mesh : model.GetMeshes())
    {
        for(const auto& primitive : mesh.primitives)
        {
            VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
            VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
            VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

            vertexBufferDeviceAddress.deviceAddress    = model.GetVertexBuffer().GetDeviceAddress() + primitive.vertexBufferOffset;  // TODO: offset? need to see how gltf models are set up
            indexBufferDeviceAddress.deviceAddress     = model.GetIndexBuffer().GetDeviceAddress() + primitive.indexBufferOffset;
            transformBufferDeviceAddress.deviceAddress = transformBuffer.GetDeviceAddress() + static_cast<uint32_t>(geometries.size()) * sizeof(VkTransformMatrixKHR);

            VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType                            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.flags                            = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometryType                     = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat  = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData    = vertexBufferDeviceAddress;
            geometry.geometry.triangles.maxVertex     = model.GetVertexBuffer().GetSize() / Model::VERTEX_SIZE;
            geometry.geometry.triangles.vertexStride  = Model::VERTEX_SIZE;
            geometry.geometry.triangles.indexType     = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData     = indexBufferDeviceAddress;
            geometry.geometry.triangles.transformData = transformBufferDeviceAddress;


            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
            buildRangeInfo.firstVertex     = 0;
            buildRangeInfo.primitiveOffset = 0;  // we already do offsets using buffer device address pointer math
            buildRangeInfo.primitiveCount  = primitive.indexBufferSize / (3 * sizeof(uint32_t));
            buildRangeInfo.transformOffset = 0;

            geometries.push_back(geometry);
            buildRangeInfos.push_back(buildRangeInfo);
            maxPrimitiveCounts.push_back(buildRangeInfo.primitiveCount);
        }
    }

    for(auto& rangeInfo : buildRangeInfos)
    {
        pBuildRangeInfos.push_back(&rangeInfo);
    }

    BLAS blas;
    // Get size info
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = static_cast<uint32_t>(geometries.size());
    accelerationStructureBuildGeometryInfo.pGeometries   = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        VulkanContext::GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        maxPrimitiveCounts.data(),
        &accelerationStructureBuildSizesInfo);


    blas.buffer.Allocate(accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = blas.buffer.GetVkBuffer();
    accelerationStructureCreateInfo.size   = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(VulkanContext::GetDevice(), &accelerationStructureCreateInfo, nullptr, &blas.handle), "Failed to create BLAS");

    // Create a small scratch buffer used during build of the bottom level acceleration structure
    Buffer scratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    accelerationStructureBuildGeometryInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure  = blas.handle;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    CommandBuffer cb;
    cb.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdBuildAccelerationStructuresKHR(
        cb.GetCommandBuffer(),
        1,
        &accelerationStructureBuildGeometryInfo,
        pBuildRangeInfos.data());
    cb.SubmitIdle();

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = blas.handle;
    uint64_t deviceAddress                              = vkGetAccelerationStructureDeviceAddressKHR(VulkanContext::GetDevice(), &accelerationDeviceAddressInfo);

    // From testing these two are the same, so it would be nice to use our existing infrastructure
    assert(deviceAddress == blas.buffer.GetDeviceAddress());

    VK_SET_DEBUG_NAME(blas.handle, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, "BLAS");
    VK_SET_DEBUG_NAME(blas.buffer.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, "BLAS buffer");

    return blas;
}

TLAS CreateTLAS(const BLAS& blas)
{
    TLAS tlas;
    // We flip the matrix [1][1] = -1.0f to accomodate for the glTF up vector
    VkTransformMatrixKHR transformMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f};

    VkAccelerationStructureInstanceKHR instance{};
    instance.transform                              = transformMatrix;
    instance.instanceCustomIndex                    = 0;
    instance.mask                                   = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags                                  = 0;
    instance.accelerationStructureReference         = blas.buffer.GetDeviceAddress();

    // Buffer for instance data
    Buffer instancesBuffer(sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    instancesBuffer.Fill(&instance, sizeof(instance));

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = instancesBuffer.GetDeviceAddress();

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationStructureGeometry.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometry.instances.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometry.geometry.instances.data            = instanceDataDeviceAddress;

    // Get size info
    /*
    The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
    */
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries   = &accelerationStructureGeometry;

    uint32_t primitive_count = 1;

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        VulkanContext::GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &primitive_count,
        &accelerationStructureBuildSizesInfo);


    tlas.buffer.Allocate(accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = tlas.buffer.GetVkBuffer();
    accelerationStructureCreateInfo.size   = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(VulkanContext::GetDevice(), &accelerationStructureCreateInfo, nullptr, &tlas.handle), "Failed to create TLAS");

    // Create a small scratch buffer used during build of the top level acceleration structure
    Buffer scratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    // fill in the rest of the fields
    accelerationStructureBuildGeometryInfo.sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure  = tlas.handle;
    accelerationStructureBuildGeometryInfo.geometryCount             = 1;
    accelerationStructureBuildGeometryInfo.pGeometries               = &accelerationStructureGeometry;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount                      = 1;
    accelerationStructureBuildRangeInfo.primitiveOffset                     = 0;
    accelerationStructureBuildRangeInfo.firstVertex                         = 0;
    accelerationStructureBuildRangeInfo.transformOffset                     = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos = {&accelerationStructureBuildRangeInfo};

    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    CommandBuffer cb;
    cb.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdBuildAccelerationStructuresKHR(
        cb.GetCommandBuffer(),
        1,
        &accelerationStructureBuildGeometryInfo,
        pBuildRangeInfos.data());
    cb.SubmitIdle();
    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = tlas.handle;
    uint64_t deviceAddress                              = vkGetAccelerationStructureDeviceAddressKHR(VulkanContext::GetDevice(), &accelerationDeviceAddressInfo);

    // From testing these two are the same, so it would be nice to use our existing infrastructure
    assert(deviceAddress == tlas.buffer.GetDeviceAddress());

    VK_SET_DEBUG_NAME(tlas.handle, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, "TLAS");
    VK_SET_DEBUG_NAME(tlas.buffer.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, "TLAS buffer");

    return tlas;
}

}
