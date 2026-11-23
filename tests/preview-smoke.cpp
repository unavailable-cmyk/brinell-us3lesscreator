#include "backend/VulkanBackend.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char**) {
    auto report = [](std::string_view message) { std::cout << message << '\n'; };
    // This executes the very same backend used by the mod, independently of Geode.
    if (!vulkanizer::probeVulkan(report)) return 1;
    if (argc > 1) {
        auto result = vulkanizer::runVulkanPreview({}, report, 0, true);
        return result.initialized && result.error.empty() && result.presentedFrames && !result.validationErrors ? 0 : 2;
    }
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto result = vulkanizer::runVulkanPreview({}, report, 60, argc > 1);
        std::cout << "cycle=" << cycle << " presented=" << result.presentedFrames
            << " init_ms=" << result.initializationMs << " validation=" << result.validationEnabled
            << " validation_errors=" << result.validationErrors << '\n';
        if (!result.initialized || !result.error.empty() || result.presentedFrames != 60 || result.validationErrors || result.initializationMs <= 0 || result.device.empty()) return 2;
    }
    // Cancellation before rendering must still safely unwind all created resources.
    std::stop_source cancelled;
    cancelled.request_stop();
    auto result = vulkanizer::runVulkanPreview(cancelled.get_token(), report, 1, false);
    if (!result.initialized || !result.error.empty() || result.presentedFrames || result.validationErrors) return 3;
    return 0;
}
