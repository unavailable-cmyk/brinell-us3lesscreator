#pragma once

#include <cstdint>

#define VULKANIZER_BACKEND_CAPABILITY_ABI_VERSION 1u

namespace vulkanizer::api {

// Values are part of the V1 binary contract; append new states only in a new ABI.
enum class BackendStateV1 : std::uint32_t {
    initializationPending = 0,
    operational = 1,
    openGLFallback = 2,
    initializationFailed = 3,
};

// Fixed-width, C-compatible snapshot returned by Vulkanizer_GetBackendCapabilityV1.
struct BackendCapabilityV1 {
    std::uint32_t structSize;
    std::uint32_t abiVersion;
    std::uint32_t state;
    std::uint32_t reserved;
};

using QueryBackendCapabilityV1 = int(__cdecl*)(BackendCapabilityV1*) noexcept;

}

#if defined(VULKANIZER_BUILD)
#define VULKANIZER_CAPABILITY_EXPORT __declspec(dllexport)
#else
#define VULKANIZER_CAPABILITY_EXPORT
#endif

#define VULKANIZER_CAPABILITY_CALL __cdecl

extern "C" VULKANIZER_CAPABILITY_EXPORT std::int32_t VULKANIZER_CAPABILITY_CALL
Vulkanizer_GetBackendCapabilityV1(vulkanizer::api::BackendCapabilityV1* capability) noexcept;
