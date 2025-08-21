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

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui 
    GIT_TAG v1.92.2b-docking
    GIT_SHALLOW TRUE
    SYSTEM
)

FetchContent_Declare(stb
    GIT_REPOSITORY  https://github.com/nothings/stb/
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

FetchContent_MakeAvailable(glm Vulkan-Utility-Libraries VulkanMemoryAllocator glfw slang imgui stb)

# --- Create a custom target for Dear ImGui ---
# Glob the core ImGui source files
file(GLOB IMGUI_CORE_SOURCES
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
)

# Add the specific backend files we need
set(IMGUI_BACKEND_SOURCES
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

add_library(imgui_vulkan STATIC
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)
target_include_directories(imgui_vulkan PUBLIC
    ${imgui_SOURCE_DIR}
)
set_target_properties(imgui_vulkan PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The ImGui backends have their own dependencies (Vulkan and GLFW)
# We make them PUBLIC so that any target linking against 'imgui' also links to them.
target_link_libraries(imgui_vulkan PUBLIC
    Vulkan::Vulkan
    glfw
)

add_library(stb_image INTERFACE)

target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})
target_compile_definitions(stb_image INTERFACE STB_IMAGE_IMPLEMENTATION)


target_link_libraries(VulkanFramework 
    PUBLIC 
        glfw GPUOpen::VulkanMemoryAllocator slang
        Vulkan::Vulkan Vulkan::UtilityHeaders glm::glm imgui_vulkan stb_image
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

    # $<$<CXX_COMPILER_ID:MSVC>:${MSVC_WARNINGS}>
)

target_compile_definitions(VulkanFramework PRIVATE
    $<$<CONFIG:Debug>:VDEBUG>
)


add_custom_target(copy-compile-commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)
