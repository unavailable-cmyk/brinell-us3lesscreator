cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Brinell VERSION 0.1.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is required")
endif()

file(READ "$ENV{GEODE_SDK}/VERSION" BRINELL_SDK_VERSION)
string(STRIP "${BRINELL_SDK_VERSION}" BRINELL_SDK_VERSION)
if (NOT BRINELL_SDK_VERSION STREQUAL "6.0.0-alpha.1")
    message(FATAL_ERROR "Brinell requires Geode 6.0.0-alpha.1; found ${BRINELL_SDK_VERSION}")
endif()
if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Brinell supports Windows x64 only")
endif()

include(FetchContent)
FetchContent_Declare(brinell_bindings
    GIT_REPOSITORY https://github.com/geode-sdk/bindings.git
    GIT_TAG 8228e2b104e4e26c183ad7e5ef7442967c316077
    SOURCE_SUBDIR download-only
)
FetchContent_MakeAvailable(brinell_bindings)
set(GEODE_BINDINGS_REPO_PATH "${brinell_bindings_SOURCE_DIR}")
if (DEFINED ENV{GEODE_BINDINGS_REPO_PATH})
    message(FATAL_ERROR "Unset GEODE_BINDINGS_REPO_PATH to use Brinell's pinned bindings")
endif()

add_subdirectory("$ENV{GEODE_SDK}" ${CMAKE_CURRENT_BINARY_DIR}/geode)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_compile_definitions(${PROJECT_NAME} PRIVATE BRINELL_BUILD)
setup_geode_mod(${PROJECT_NAME} LINK_TYPE PRIVATE)
