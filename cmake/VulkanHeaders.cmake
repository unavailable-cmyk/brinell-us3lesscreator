include(FetchContent)
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2 # v1.4.341
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(vulkan_headers)
