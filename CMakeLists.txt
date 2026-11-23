cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Vulkanizer VERSION 0.1.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

file(READ "$ENV{GEODE_SDK}/VERSION" VULKANIZER_SDK_VERSION)
string(STRIP "${VULKANIZER_SDK_VERSION}" VULKANIZER_SDK_VERSION)
if (NOT VULKANIZER_SDK_VERSION STREQUAL "6.0.0-alpha.1")
    message(FATAL_ERROR "Vulkanizer requires exactly Geode 6.0.0-alpha.1; found ${VULKANIZER_SDK_VERSION}")
endif()
if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Vulkanizer currently supports Windows x64 only")
endif()
# Keep the SDK's normal binding generation, pinned to the revision tested here.
include(FetchContent)
FetchContent_Declare(vulkanizer_bindings
    GIT_REPOSITORY https://github.com/geode-sdk/bindings.git
    GIT_TAG 8228e2b104e4e26c183ad7e5ef7442967c316077
    SOURCE_SUBDIR download-only
)
FetchContent_MakeAvailable(vulkanizer_bindings)
set(GEODE_BINDINGS_REPO_PATH "${vulkanizer_bindings_SOURCE_DIR}")
if (DEFINED ENV{GEODE_BINDINGS_REPO_PATH})
    message(FATAL_ERROR "Unset GEODE_BINDINGS_REPO_PATH to use Vulkanizer's pinned bindings")
endif()
add_subdirectory("$ENV{GEODE_SDK}" ${CMAKE_CURRENT_BINARY_DIR}/geode)

include(cmake/VulkanHeaders.cmake)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Headers opengl32 user32)
target_include_directories(${PROJECT_NAME} PRIVATE src include)
target_compile_definitions(${PROJECT_NAME} PRIVATE VULKANIZER_BUILD)
setup_geode_mod(${PROJECT_NAME} LINK_TYPE PRIVATE)
