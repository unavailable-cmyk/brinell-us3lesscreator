#include "VulkanBackend.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "generated/TriangleVertex.hpp"
#include "generated/TriangleFragment.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

namespace vulkanizer {
namespace {
using Clock = std::chrono::steady_clock;
void check(VkResult result, char const* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: VkResult " + std::to_string(result));
}
template<class T, class Enumerate> std::vector<T> enumerate(Enumerate call) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t count = 0;
        check(call(&count, static_cast<T*>(nullptr)), "enumeration count");
        std::vector<T> values(count);
        auto result = call(&count, values.data());
        if (result == VK_INCOMPLETE) continue;
        check(result, "enumeration values");
        values.resize(count);
        return values;
    }
    throw std::runtime_error("Vulkan enumeration did not stabilize");
}
template<class T> bool hasExtension(std::vector<T> const& extensions, char const* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](auto const& value) {
        return std::strcmp(value.extensionName, name) == 0;
    });
}

#define INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(EnumerateDeviceExtensionProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define SURFACE_FUNCTIONS(X) \
    X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) X(GetPhysicalDeviceSurfacePresentModesKHR)
#define DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateSwapchainKHR) X(DestroySwapchainKHR) \
    X(GetSwapchainImagesKHR) X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateGraphicsPipelines) X(DestroyPipeline) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdBeginRenderPass) X(CmdBindPipeline) X(CmdDraw) X(CmdEndRenderPass) \
    X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(ResetFences) \
    X(AcquireNextImageKHR) X(QueueSubmit) X(QueuePresentKHR)

struct Context {
    Reporter const& report;
    HMODULE library = nullptr;
    PFN_vkGetInstanceProcAddr getProc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> finished;
    std::vector<VkCommandBuffer> commands;
    HWND window = nullptr;
    HINSTANCE windowModule = nullptr;
    bool windowClassRegistered = false;
    bool closing = false;
    bool validation = false;
    bool cleaned = false;
    std::atomic<uint32_t> validationErrors{0};
    std::string deviceName;
#define DECLARE(name) PFN_vk##name name = nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    SURFACE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    explicit Context(Reporter const& reporter) : report(reporter) {}
    Context(Context const&) = delete;
    Context& operator=(Context const&) = delete;

    template<class T> T instanceFunction(char const* name) {
        auto function = reinterpret_cast<T>(getProc(instance, name));
        if (!function) throw std::runtime_error(std::string("Missing Vulkan function: ") + name);
        return function;
    }
    template<class T> T deviceFunction(char const* name) {
        auto function = reinterpret_cast<T>(GetDeviceProcAddr(device, name));
        if (!function) throw std::runtime_error(std::string("Missing Vulkan device function: ") + name);
        return function;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, VkDebugUtilsMessengerCallbackDataEXT const* data, void* user) {
        auto& context = *static_cast<Context*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++context.validationErrors;
        // No exceptions may cross the driver callback boundary.
        try { context.report(data && data->pMessage ? data->pMessage : "Vulkan validation message"); } catch (...) {}
        return VK_FALSE;
    }

    void createInstance(bool presentation) {
        // Never search the game directory for a graphics DLL.
        library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) throw std::runtime_error("System Vulkan loader is unavailable (Win32 " + std::to_string(GetLastError()) + ")");
        getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
        if (!getProc) throw std::runtime_error("System Vulkan loader lacks vkGetInstanceProcAddr");
        auto enumerateExtensions = instanceFunction<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
        auto extensions = enumerate<VkExtensionProperties>([&](auto* count, auto* values) {
            return enumerateExtensions(nullptr, count, values);
        });
        std::vector<char const*> requested;
        if (presentation) {
            for (auto* name : {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME}) {
                if (!hasExtension(extensions, name)) throw std::runtime_error(std::string("Missing instance extension: ") + name);
                requested.push_back(name);
            }
        }
        std::vector<char const*> layers;
#ifndef NDEBUG
        auto enumerateLayers = instanceFunction<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
        auto availableLayers = enumerate<VkLayerProperties>([&](auto* count, auto* values) { return enumerateLayers(count, values); });
        validation = std::any_of(availableLayers.begin(), availableLayers.end(), [](auto const& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        }) && hasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (validation) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            requested.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
#endif
        report(validation ? "Vulkan validation enabled" : "Vulkan validation inactive (release build or layer/debug-utils unavailable)");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Vulkanizer isolated preview";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "Vulkanizer";
        app.apiVersion = VK_API_VERSION_1_0;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = debugCallback;
        debug.pUserData = this;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(requested.size());
        info.ppEnabledExtensionNames = requested.data();
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.pNext = validation ? &debug : nullptr;
        auto create = instanceFunction<PFN_vkCreateInstance>("vkCreateInstance");
        check(create(&info, nullptr, &instance), "vkCreateInstance");
#define LOAD_INSTANCE(name) name = instanceFunction<PFN_vk##name>("vk" #name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
        if (presentation) { SURFACE_FUNCTIONS(LOAD_INSTANCE) }
#undef LOAD_INSTANCE
        if (validation) {
            destroyMessenger = instanceFunction<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT");
            auto createDebug = instanceFunction<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT");
            check(createDebug(instance, &debug, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
        }
    }

    std::vector<VkPhysicalDevice> devices() {
        auto result = enumerate<VkPhysicalDevice>([&](auto* count, auto* values) { return EnumeratePhysicalDevices(instance, count, values); });
        if (result.empty()) throw std::runtime_error("No Vulkan physical devices found");
        for (auto candidate : result) {
            VkPhysicalDeviceProperties properties{};
            VkPhysicalDeviceFeatures features{};
            VkPhysicalDeviceMemoryProperties memory{};
            GetPhysicalDeviceProperties(candidate, &properties);
            GetPhysicalDeviceFeatures(candidate, &features);
            GetPhysicalDeviceMemoryProperties(candidate, &memory);
            uint64_t localBytes = 0;
            for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
                if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) localBytes += memory.memoryHeaps[i].size;
            report(std::string("Vulkan GPU: ") + properties.deviceName + "; API " +
                std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) +
                "; vendor=" + std::to_string(properties.vendorID) + "; device-local MiB=" + std::to_string(localBytes / 1048576) +
                "; maxImage2D=" + std::to_string(properties.limits.maxImageDimension2D) +
                "; multiDrawIndirect=" + std::to_string(features.multiDrawIndirect));
        }
        return result;
    }

    static LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_CLOSE) {
            if (auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(handle, GWLP_USERDATA))) context->closing = true;
            return 0;
        }
        return DefWindowProcW(handle, message, wParam, lParam);
    }

    void createWindow(bool visible) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&windowProc), &windowModule)) throw std::runtime_error("GetModuleHandleExW failed");
        WNDCLASSW klass{};
        klass.lpfnWndProc = windowProc;
        klass.hInstance = windowModule;
        klass.lpszClassName = L"VulkanizerPreviewWindow";
        klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&klass)) throw std::runtime_error("RegisterClassW failed");
        windowClassRegistered = true;
        RECT rect{0, 0, 480, 360};
        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&rect, style, FALSE);
        window = CreateWindowExW(0, klass.lpszClassName,
            L"Vulkanizer - Vulkan triangle (GD stays OpenGL)", style,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, windowModule, this);
        if (!window) throw std::runtime_error("CreateWindowExW failed");
        if (visible) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            // A launcher's STARTUPINFO may override the first ShowWindow with
            // SW_HIDE. Explicitly show our opt-in window without stealing focus.
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        info.hinstance = windowModule;
        info.hwnd = window;
        check(CreateWin32SurfaceKHR(instance, &info, nullptr, &surface), "vkCreateWin32SurfaceKHR");
    }

    void createDevice() {
        int bestScore = -1;
        for (auto candidate : devices()) {
            auto extensions = enumerate<VkExtensionProperties>([&](auto* count, auto* values) {
                return EnumerateDeviceExtensionProperties(candidate, nullptr, count, values);
            });
            if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
            uint32_t count = 0;
            GetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            GetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            VkPhysicalDeviceProperties properties{};
            GetPhysicalDeviceProperties(candidate, &properties);
            for (uint32_t i = 0; i < count; ++i) {
                VkBool32 supported = VK_FALSE;
                check(GetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (!supported || !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !families[i].queueCount) continue;
                int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
                if (score > bestScore) {
                    bestScore = score;
                    physical = candidate;
                    family = i;
                    deviceName = properties.deviceName;
                }
            }
        }
        if (!physical) throw std::runtime_error("No device supports graphics + Win32 presentation + VK_KHR_swapchain");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        char const* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &extension;
        check(CreateDevice(physical, &info, nullptr, &device), "vkCreateDevice");
#define LOAD_DEVICE(name) name = deviceFunction<PFN_vk##name>("vk" #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        GetDeviceQueue(device, family, 0, &queue);
        report(std::string("Vulkan preview selected ") + deviceName + "; game backend remains vanilla OpenGL");
    }

    void createResources() {
        VkSurfaceCapabilitiesKHR caps{};
        check(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface cannot be a color attachment");
        auto formats = enumerate<VkSurfaceFormatKHR>([&](auto* count, auto* values) { return GetPhysicalDeviceSurfaceFormatsKHR(physical, surface, count, values); });
        auto modes = enumerate<VkPresentModeKHR>([&](auto* count, auto* values) { return GetPhysicalDeviceSurfacePresentModesKHR(physical, surface, count, values); });
        if (formats.empty() || modes.empty()) throw std::runtime_error("Surface has no usable formats/present modes");
        auto format = formats.front();
        for (auto value : formats) if (value.format == VK_FORMAT_B8G8R8A8_UNORM && value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) format = value;
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
        auto extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) extent = {std::clamp(480u, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(360u, caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (!extent.width || !extent.height) throw std::runtime_error("Surface has zero extent");
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount) imageCount = std::min(imageCount, caps.maxImageCount);
        VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swap.surface = surface;
        swap.minImageCount = imageCount;
        swap.imageFormat = format.format;
        swap.imageColorSpace = format.colorSpace;
        swap.imageExtent = extent;
        swap.imageArrayLayers = 1;
        swap.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swap.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swap.preTransform = caps.currentTransform;
        for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & flag) { swap.compositeAlpha = flag; break; }
        }
        swap.presentMode = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end() ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
        swap.clipped = VK_TRUE;
        check(CreateSwapchainKHR(device, &swap, nullptr, &swapchain), "vkCreateSwapchainKHR");
        auto images = enumerate<VkImage>([&](auto* count, auto* values) { return GetSwapchainImagesKHR(device, swapchain, count, values); });
        views.resize(images.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < images.size(); ++i) {
            VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            info.image = images[i]; info.viewType = VK_IMAGE_VIEW_TYPE_2D; info.format = format.format;
            info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(CreateImageView(device, &info, nullptr, &views[i]), "vkCreateImageView");
        }
        VkAttachmentDescription attachment{};
        attachment.format = format.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 1; pass.pAttachments = &attachment; pass.subpassCount = 1; pass.pSubpasses = &subpass;
        pass.dependencyCount = 1; pass.pDependencies = &dependency;
        check(CreateRenderPass(device, &pass, nullptr, &renderPass), "vkCreateRenderPass");
        auto shader = [&](auto const& code, VkShaderModule& module) {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = sizeof(code); info.pCode = code;
            check(CreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
        };
        shader(triangleVertex, vertex); shader(triangleFragment, fragment);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        check(CreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo viewportInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportInfo.viewportCount = viewportInfo.scissorCount = 1; viewportInfo.pViewports = &viewport; viewportInfo.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment{}; blendAttachment.colorWriteMask = 0xf;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = 1; blend.pAttachments = &blendAttachment;
        VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        graphics.stageCount = 2; graphics.pStages = stages.data(); graphics.pVertexInputState = &input; graphics.pInputAssemblyState = &assembly;
        graphics.pViewportState = &viewportInfo; graphics.pRasterizationState = &raster; graphics.pMultisampleState = &multisample; graphics.pColorBlendState = &blend;
        graphics.layout = layout; graphics.renderPass = renderPass;
        check(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics, nullptr, &pipeline), "vkCreateGraphicsPipelines");
        framebuffers.resize(images.size(), VK_NULL_HANDLE); finished.resize(images.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (size_t i = 0; i < images.size(); ++i) {
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass = renderPass;
            framebuffer.attachmentCount = 1; framebuffer.pAttachments = &views[i]; framebuffer.width = extent.width; framebuffer.height = extent.height; framebuffer.layers = 1;
            check(CreateFramebuffer(device, &framebuffer, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
            // Per-image semaphore: reused only after the presentation engine returns that image.
            check(CreateSemaphore(device, &semaphore, nullptr, &finished[i]), "vkCreateSemaphore(present)");
        }
        check(CreateSemaphore(device, &semaphore, nullptr, &acquired), "vkCreateSemaphore(acquire)");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(CreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex = family;
        check(CreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");
        commands.resize(images.size());
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = static_cast<uint32_t>(commands.size());
        check(AllocateCommandBuffers(device, &allocate, commands.data()), "vkAllocateCommandBuffers");
        for (size_t i = 0; i < commands.size(); ++i) {
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(BeginCommandBuffer(commands[i], &begin), "vkBeginCommandBuffer");
            VkClearValue clear{}; clear.color = {{0.025f, 0.03f, 0.055f, 1.0f}};
            VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; render.renderPass = renderPass; render.framebuffer = framebuffers[i];
            render.renderArea = {{0, 0}, extent}; render.clearValueCount = 1; render.pClearValues = &clear;
            CmdBeginRenderPass(commands[i], &render, VK_SUBPASS_CONTENTS_INLINE);
            CmdBindPipeline(commands[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            CmdDraw(commands[i], 3, 1, 0, 0);
            CmdEndRenderPass(commands[i]);
            check(EndCommandBuffer(commands[i]), "vkEndCommandBuffer");
        }
    }

    bool frame() {
        auto status = GetFenceStatus(device, fence);
        if (status == VK_NOT_READY) return false;
        check(status, "vkGetFenceStatus");
        uint32_t index = 0;
        auto acquire = AcquireNextImageKHR(device, swapchain, 0, acquired, VK_NULL_HANDLE, &index);
        if (acquire == VK_NOT_READY || acquire == VK_TIMEOUT) return false;
        if (acquire != VK_SUBOPTIMAL_KHR) check(acquire, "vkAcquireNextImageKHR (restart preview if surface changed)");
        check(ResetFences(device, 1, &fence), "vkResetFences");
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &commands[index]; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &finished[index];
        check(QueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &finished[index];
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &index;
        auto result = QueuePresentKHR(queue, &present);
        if (result != VK_SUBOPTIMAL_KHR) check(result, "vkQueuePresentKHR (restart preview if surface changed)");
        return true;
    }

    void cleanup() {
        if (cleaned) return;
        cleaned = true;
        if (device) {
            // The only idle wait: shutdown, on the preview worker, never per game frame.
            if (DeviceWaitIdle) {
                auto result = DeviceWaitIdle(device);
                if (result != VK_SUCCESS) { try { report("Vulkan device idle failed during teardown"); } catch (...) {} }
            }
            if (DestroyFence && fence) DestroyFence(device, fence, nullptr);
            if (DestroySemaphore) {
                if (acquired) DestroySemaphore(device, acquired, nullptr);
                for (auto value : finished) if (value) DestroySemaphore(device, value, nullptr);
            }
            if (DestroyCommandPool && pool) DestroyCommandPool(device, pool, nullptr);
            if (DestroyFramebuffer) for (auto value : framebuffers) if (value) DestroyFramebuffer(device, value, nullptr);
            if (DestroyPipeline && pipeline) DestroyPipeline(device, pipeline, nullptr);
            if (DestroyPipelineLayout && layout) DestroyPipelineLayout(device, layout, nullptr);
            if (DestroyShaderModule) { if (vertex) DestroyShaderModule(device, vertex, nullptr); if (fragment) DestroyShaderModule(device, fragment, nullptr); }
            if (DestroyRenderPass && renderPass) DestroyRenderPass(device, renderPass, nullptr);
            if (DestroyImageView) for (auto value : views) if (value) DestroyImageView(device, value, nullptr);
            if (DestroySwapchainKHR && swapchain) DestroySwapchainKHR(device, swapchain, nullptr);
            if (DestroyDevice) DestroyDevice(device, nullptr);
        }
        if (surface && DestroySurfaceKHR) DestroySurfaceKHR(instance, surface, nullptr);
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        if (instance && DestroyInstance) DestroyInstance(instance, nullptr);
        if (window) DestroyWindow(window);
        if (windowClassRegistered) UnregisterClassW(L"VulkanizerPreviewWindow", windowModule);
        if (library) FreeLibrary(library);
    }
    ~Context() { cleanup(); }
};
}

bool probeVulkan(Reporter const& report) {
    try {
        Context context(report);
        context.createInstance(false);
        context.devices();
        return true;
    } catch (std::exception const& error) {
        report(std::string("Vulkan detection unavailable: ") + error.what() + "; retaining vanilla OpenGL");
        return false;
    }
}

PreviewResult runVulkanPreview(std::stop_token stop, Reporter const& report, uint64_t frameLimit, bool visible) {
    PreviewResult result;
    Context context(report);
    auto start = Clock::now();
    try {
        context.createInstance(true);
        context.createWindow(visible);
        context.createDevice();
        context.createResources();
        result.initialized = true;
        result.initializationMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        result.device = context.deviceName;
        report("Vulkan preview initialized in " + std::to_string(result.initializationMs) + " ms; 1 vkCmdDraw per preview frame; GPU timing unavailable");
        auto deadline = Clock::now() + std::chrono::seconds(30);
        while (!stop.stop_requested() && !context.closing && (!frameLimit || result.presentedFrames < frameLimit)) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (context.closing) break;
            if (!IsIconic(context.window) && context.frame()) ++result.presentedFrames;
            if (frameLimit && Clock::now() > deadline) throw std::runtime_error("Preview smoke test timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    } catch (std::exception const& error) {
        result.error = error.what();
        if (!result.initialized) result.initializationMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        report("Vulkan preview stopped: " + result.error + "; game backend remains vanilla OpenGL");
    }
    context.cleanup();
    result.validationEnabled = context.validation;
    result.validationErrors = context.validationErrors.load();
    return result;
}
}
