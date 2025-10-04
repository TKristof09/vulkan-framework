#include "Raytracing.hpp"
#include <cstring>

namespace Raytracing
{
BLAS CreateBLAS(const Model& model)
{
    size_t numPrimitives = 0;
    for(const auto& mesh : model.GetMeshes())
        for(const auto& _ : mesh.primitives)
            numPrimitives++;

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos{};
    std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRangeInfoArrays;

    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos{};
    std::vector<VkAccelerationStructureGeometryKHR> geometries{};
    buildInfos.reserve(model.GetMeshes().size());
    buildRangeInfoArrays.reserve(model.GetMeshes().size());
    pBuildRangeInfos.reserve(model.GetMeshes().size());
    geometries.reserve(numPrimitives);


    size_t totalAccelerationSize = 0;
    size_t totalPrimitiveCount   = 0;
    size_t maxScratchSize        = 0;
    std::vector<size_t> accelerationOffsets;
    std::vector<size_t> accelerationSizes;
    std::vector<size_t> scratchSizes;
    std::vector<size_t> primitiveCounts;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties{};
    accelerationProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2{};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &accelerationProperties;
    vkGetPhysicalDeviceProperties2(VulkanContext::GetPhysicalDevice(), &deviceProperties2);
    const uint32_t scratchAlignment          = accelerationProperties.minAccelerationStructureScratchOffsetAlignment;
    const size_t accelerationOffsetAlignment = 256;

    for(const auto& mesh : model.GetMeshes())
    {
        std::vector<uint32_t> maxPrimitiveCounts{};
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
        ranges.reserve(mesh.primitives.size());

        uint32_t meshTriangleCount = 0;

        size_t geometriesStart = geometries.size();
        for(const auto& primitive : mesh.primitives)
        {
            VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
            VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};

            vertexBufferDeviceAddress.deviceAddress = model.GetVertexBuffer().GetDeviceAddress() + primitive.vertexBufferOffset;  // TODO: offset? need to see how gltf models are set up
            indexBufferDeviceAddress.deviceAddress  = model.GetIndexBuffer().GetDeviceAddress() + primitive.indexBufferOffset;

            VkAccelerationStructureGeometryKHR geometry{};
            geometry.sType                           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.flags                           = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometryType                    = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData   = vertexBufferDeviceAddress;
            geometry.geometry.triangles.maxVertex    = model.GetVertexBuffer().GetSize() / Model::VERTEX_SIZE;
            geometry.geometry.triangles.vertexStride = Model::VERTEX_SIZE;
            geometry.geometry.triangles.indexType    = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData    = indexBufferDeviceAddress;


            uint32_t primitiveCount  = primitive.indexBufferSize / (3 * sizeof(uint32_t));
            meshTriangleCount       += primitiveCount;
            geometries.push_back(geometry);
            maxPrimitiveCounts.push_back(primitiveCount);

            VkAccelerationStructureBuildRangeInfoKHR r{};
            r.firstVertex     = 0;  // you use device-addressed vertex/index buffers with offsets
            r.primitiveOffset = 0;
            r.primitiveCount  = primitiveCount;
            r.transformOffset = 0;
            ranges.push_back(r);
        }
        buildRangeInfoArrays.push_back(std::move(ranges));


        VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
        accelerationStructureBuildGeometryInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        accelerationStructureBuildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        accelerationStructureBuildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        accelerationStructureBuildGeometryInfo.geometryCount = static_cast<uint32_t>(mesh.primitives.size());
        accelerationStructureBuildGeometryInfo.pGeometries   = &geometries[geometriesStart];

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            VulkanContext::GetDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &accelerationStructureBuildGeometryInfo,
            maxPrimitiveCounts.data(),
            &sizeInfo);

        accelerationOffsets.push_back(totalAccelerationSize);
        accelerationSizes.push_back(sizeInfo.accelerationStructureSize);
        scratchSizes.push_back(sizeInfo.buildScratchSize);

        buildInfos.push_back(accelerationStructureBuildGeometryInfo);


        totalAccelerationSize  = (totalAccelerationSize + sizeInfo.accelerationStructureSize + accelerationOffsetAlignment - 1) & ~(accelerationOffsetAlignment - 1);
        totalPrimitiveCount   += meshTriangleCount;
        maxScratchSize         = std::max(maxScratchSize, size_t(sizeInfo.buildScratchSize));
    }

    for(auto& arr : buildRangeInfoArrays)
        pBuildRangeInfos.push_back(arr.data());

    BLAS blas;
    blas.handles.resize(accelerationSizes.size());
    // Get size info

    blas.buffer.Allocate(totalAccelerationSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    for(size_t i = 0; i < accelerationSizes.size(); i++)
    {
        VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
        accelerationStructureCreateInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelerationStructureCreateInfo.buffer = blas.buffer.GetVkBuffer();
        accelerationStructureCreateInfo.size   = accelerationSizes[i];
        accelerationStructureCreateInfo.offset = accelerationOffsets[i];
        accelerationStructureCreateInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VK_CHECK(vkCreateAccelerationStructureKHR(VulkanContext::GetDevice(), &accelerationStructureCreateInfo, nullptr, &blas.handles[i]), "Failed to create BLAS");
    }

    // Create a small scratch buffer used during build of the bottom level acceleration structure
    Buffer scratchBuffer(maxScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, scratchAlignment);

    CommandBuffer cb;
    cb.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    for(size_t i = 0; i < buildInfos.size(); i++)
    {
        buildInfos[i].mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfos[i].dstAccelerationStructure  = blas.handles[i];
        buildInfos[i].scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();
        vkCmdBuildAccelerationStructuresKHR(
            cb.GetCommandBuffer(),
            1,
            &buildInfos[i],
            &pBuildRangeInfos[i]);

        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        memoryBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo dependency{};
        dependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers    = &memoryBarrier;
        vkCmdPipelineBarrier2(cb.GetCommandBuffer(), &dependency);
    }

    cb.SubmitIdle();

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = blas.handles[0];
    uint64_t deviceAddress                              = vkGetAccelerationStructureDeviceAddressKHR(VulkanContext::GetDevice(), &accelerationDeviceAddressInfo);

    // From testing these two are the same, so it would be nice to use our existing infrastructure
    assert(deviceAddress == blas.buffer.GetDeviceAddress());

    for(size_t i = 0; i < blas.handles.size(); i++)
    {
        std::string name = std::format("BLAS_{}", i);
        VK_SET_DEBUG_NAME(blas.handles[i], VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, name.c_str());
    }
    VK_SET_DEBUG_NAME(blas.buffer.GetVkBuffer(), VK_OBJECT_TYPE_BUFFER, "BLAS buffer");

    return blas;
}
static VkTransformMatrixKHR ToVkTransform(const glm::mat4& m)
{
    // Vulkan expects a row-major 3x4 matrix stored as 12 floats:
    // { m00 m01 m02 m03, m10 m11 m12 m13, m20 m21 m22 m23 }
    // clang-format off
    VkTransformMatrixKHR t{};
    t.matrix[0][0] = m[0][0]; t.matrix[0][1] = m[1][0]; t.matrix[0][2] = m[2][0]; t.matrix[0][3] = m[3][0];
    t.matrix[1][0] = m[0][1]; t.matrix[1][1] = m[1][1]; t.matrix[1][2] = m[2][1]; t.matrix[1][3] = m[3][1];
    t.matrix[2][0] = m[0][2]; t.matrix[2][1] = m[1][2]; t.matrix[2][2] = m[2][2]; t.matrix[2][3] = m[3][2];
    // clang-format on
    return t;
}

TLAS CreateTLAS(const BLAS& blas, const Model& model)
{
    TLAS tlas;
    std::vector<VkAccelerationStructureInstanceKHR> instances{};
    uint32_t currentOffset = 0;
    for(size_t i = 0; i < blas.handles.size(); i++)
    {
        VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
        accelerationDeviceAddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationDeviceAddressInfo.accelerationStructure = blas.handles[i];
        uint64_t deviceAddress                              = vkGetAccelerationStructureDeviceAddressKHR(VulkanContext::GetDevice(), &accelerationDeviceAddressInfo);

        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                              = ToVkTransform(model.GetMeshes()[i].transform);
        instance.instanceCustomIndex                    = currentOffset;
        instance.mask                                   = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags                                  = 0;
        instance.accelerationStructureReference         = deviceAddress;
        instances.push_back(instance);

        currentOffset += model.GetMeshes()[i].primitives.size();
    }

    Buffer instancesBuffer(instances.size() * sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    instancesBuffer.Fill(instances);

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

    uint32_t primitiveCount = instances.size();

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        VulkanContext::GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo,
        &primitiveCount,
        &accelerationStructureBuildSizesInfo);


    tlas.buffer.Allocate(accelerationStructureBuildSizesInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = tlas.buffer.GetVkBuffer();
    accelerationStructureCreateInfo.size   = accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    VK_CHECK(vkCreateAccelerationStructureKHR(VulkanContext::GetDevice(), &accelerationStructureCreateInfo, nullptr, &tlas.handle), "Failed to create TLAS");

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties{};
    accelerationProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProperties2{};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &accelerationProperties;
    vkGetPhysicalDeviceProperties2(VulkanContext::GetPhysicalDevice(), &deviceProperties2);
    const uint32_t scratchAlignment = accelerationProperties.minAccelerationStructureScratchOffsetAlignment;
    // Create a small scratch buffer used during build of the top level acceleration structure
    Buffer scratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, scratchAlignment);

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
    accelerationStructureBuildRangeInfo.primitiveCount                      = primitiveCount;
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
