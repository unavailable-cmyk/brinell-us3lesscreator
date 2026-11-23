#pragma once
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace vulkanizer {
using Reporter = std::function<void(std::string_view)>;
struct PreviewResult {
    bool initialized = false;
    bool validationEnabled = false;
    uint32_t validationErrors = 0;
    uint64_t presentedFrames = 0;
    double initializationMs = 0;
    std::string device;
    std::string error;
};

// Owns only a separate Win32 window and Vulkan resources. Never touches GD/GL.
PreviewResult runVulkanPreview(std::stop_token stop, Reporter const& report,
    uint64_t frameLimit = 0, bool visible = true);
bool probeVulkan(Reporter const& report);
}
