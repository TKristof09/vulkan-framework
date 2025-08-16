cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
project(VulkanFramework VERSION 0.1.0)

include(FetchContent)

set( CMAKE_EXPORT_COMPILE_COMMANDS ON)



file(GLOB_RECURSE CPP_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

add_library(VulkanFramework ${CPP_FILES})

set_property(TARGET VulkanFramework PROPERTY CXX_STANDARD 23)
set_property(TARGET VulkanFramework PROPERTY STANDARD_REQUIRED ON)


find_package(Vulkan REQUIRED COMPONENTS SPIRV-Tools)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(Vulkan-Utility-Libraries
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries
    GIT_TAG vulkan-sdk-${Vulkan_VERSION}
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG v3.3.0
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw 
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
    SYSTEM
)

set(SLANG_ENABLE_SLANGC OFF)
set(SLANG_ENABLE_SLANGI OFF)
set(SLANG_ENABLE_SLANGD OFF)
set(SLANG_ENABLE_CUDA OFF)
set(SLANG_ENABLE_OPTIX OFF)
set(SLANG_ENABLE_NVAPI OFF)
set(SLANG_ENABLE_AFTERMATH OFF)
set(SLANG_ENABLE_RHI OFF)
set(SLANG_ENABLE_GFX OFF)
set(SLANG_ENABLE_TESTS OFF)
set(SLANG_ENABLE_EXAMPLES OFF)
set(SLANG_ENABLE_SPLIT_DEBUG_INFO OFF)
FetchContent_Declare(slang
    GIT_REPOSITORY https://github.com/shader-slang/slang
    GIT_TAG vulkan-sdk-${Vulkan_VERSION}.0
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_MakeAvailable(glm Vulkan-Utility-Libraries VulkanMemoryAllocator glfw slang)


target_link_libraries(VulkanFramework 
    PUBLIC 
        glfw GPUOpen::VulkanMemoryAllocator slang
        Vulkan::Vulkan Vulkan::UtilityHeaders glm::glm
    )

target_include_directories(VulkanFramework 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

# target_precompile_headers(VulkanFramework 
#     PRIVATE 
#     PUBLIC
#         <string>
#         <string_view>
#         <cstdint>
#         <memory>
#         <vector>
#         <unordered_map>
#         <cmath>
#         <Log.hpp>
#         <cstdint>
#     )

set(GCC_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
)

set(MSVC_WARNINGS
    /W4
)

target_compile_options(VulkanFramework PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${GCC_CLANG_WARNINGS}>

    $<$<CXX_COMPILER_ID:MSVC>:${MSVC_WARNINGS}>
)

target_compile_definitions(VulkanFramework PRIVATE
    $<$<CONFIG:Debug>:VDEBUG>
)


add_custom_target(copy-compile-commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)
