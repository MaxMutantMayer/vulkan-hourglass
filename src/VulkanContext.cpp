#include "VulkanContext.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <optional>

#include "ApplicationDefines.hpp"
#include "ApplicationSharedData.hpp"
#include "FileReading.hpp"
#include "GlfwContext.hpp"
#include "Macros.hpp"
#include "PushConstants.hpp"
#include "SpecializationConstants.hpp"

// NOTE(MM): Allocate twice to be able to swap in/out buffers on each execution.
static constexpr uint32_t BUFFERS_PER_COMPUTE = 2;
static constexpr uint32_t STORAGE_BUFFER_COUNT = 2;
static constexpr uint32_t TEXEL_BUFFER_COUNT = 1;

VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackPrint(VkDebugReportFlagsEXT /*flags*/,
                                                        VkDebugReportObjectTypeEXT /*objectType*/,
                                                        uint64_t /*object*/,
                                                        size_t /*location*/,
                                                        int32_t /*messageCode*/,
                                                        const char* /*pLayerPrefix*/,
                                                        const char* pMessage,
                                                        void* /* pUserData */)
{
    fprintf(stderr, "Validation error: %s\n", pMessage);
    return VK_FALSE;
}

namespace VkHourglass
{
static std::vector<const char*> getRequiredInstanceLayers(void)
{
    return {
#ifdef VALIDATION_LAYERS
        "VK_LAYER_KHRONOS_validation",
#endif
    };
}

static std::vector<const char*> getRequiredInstanceExtensions(const GlfwContext& glfwContext)
{
    std::vector<const char*> requiredExtensions{
#ifdef VALIDATION_LAYERS
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
    };

    const auto glfwExtensions = glfwContext.getRequiredExtensions();
    requiredExtensions.insert(requiredExtensions.cend(), glfwExtensions.cbegin(), glfwExtensions.cend());

    return requiredExtensions;
}

static std::vector<const char*> getRequiredDeviceLayers(void)
{
    // NOTE(MM): Device layers have been deprecated.
    return {};
}
static std::vector<const char*> getRequiredDeviceExtensions(void)
{
    return {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
}

static std::optional<VkInstance> createInstance(const GlfwContext& glfwContext)
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Vulkan Hourglass";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.pEngineName = "End of Time Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    const std::vector<const char*> instanceLayers = getRequiredInstanceLayers();
    const std::vector<const char*> instanceExtensions = getRequiredInstanceExtensions(glfwContext);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
    createInfo.ppEnabledLayerNames = instanceLayers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();

    VkInstance instance;
    VK_RETURN_ON_ERROR_V(vkCreateInstance(&createInfo, nullptr, &instance), std::nullopt);

    return instance;
}

[[maybe_unused]] static std::optional<VkDebugReportCallbackEXT> createDebugReportCallback(const VkInstance instance)
{
    auto createDebugReportCallbackFP = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));

    if (!createDebugReportCallbackFP)
    {
        return std::nullopt;
    }

    VkDebugReportCallbackCreateInfoEXT callbackCreateInfo{};
    callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    callbackCreateInfo.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    callbackCreateInfo.pfnCallback = &debugReportCallbackPrint;

    VkDebugReportCallbackEXT debugReportCallback;
    VK_RETURN_ON_ERROR_V(createDebugReportCallbackFP(instance, &callbackCreateInfo, nullptr, &debugReportCallback),
                         std::nullopt);

    return debugReportCallback;
}

static bool areDeviceLimitsSufficient(const VkPhysicalDeviceLimits limits)
{
    return limits.maxComputeWorkGroupInvocations > ApplicationDefines::COMPUTE_LOCAL_GROUP_SIZE_X
           && limits.maxComputeWorkGroupSize[0] > ApplicationDefines::COMPUTE_LOCAL_GROUP_SIZE_X
           && limits.maxComputeWorkGroupCount[0] > ApplicationDefines::NonModifiable::X_DISPATCH_COUNT
           && limits.maxStorageBufferRange > ApplicationDefines::NonModifiable::GRID_SIZE * sizeof(uint32_t)
           && limits.maxTexelBufferElements > ApplicationDefines::NonModifiable::GRID_SIZE
           && limits.maxPushConstantsSize > sizeof(PushConstants);
}

static bool isDeviceSupportingSurfacePresentation(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface)
{
    uint32_t formatCount = 0;
    VK_RETURN_ON_ERROR_V(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr), false);

    uint32_t presentModeCount = 0;
    VK_RETURN_ON_ERROR_V(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr),
                         false);

    return formatCount != 0 && presentModeCount != 0;
}

static bool isDeviceSupportingTexelBufferFormat(const VkPhysicalDevice physicalDevice, const VkFormat format)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);

    return formatProperties.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
}

static std::optional<uint32_t> chooseQueue(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::optional<uint32_t> queueFamilyToUse = std::nullopt;
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const VkQueueFamilyProperties& queueFamily = queueFamilies[i];

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT
            && presentSupport)
        {
            queueFamilyToUse = i;
        }
    }
    return queueFamilyToUse;
}

static std::optional<VulkanContext::DeviceWrapper> createDevice(const VkInstance instance, const VkSurfaceKHR surface)
{
    uint32_t physicalDeviceCount = 0;
    VK_RETURN_ON_ERROR_V(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr), std::nullopt);

    if (physicalDeviceCount == 0)
    {
        fprintf(stderr, "No physical devices!");
        return std::nullopt;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    VK_RETURN_ON_ERROR_V(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()),
                         std::nullopt);

    std::optional<VkPhysicalDevice> bestDeviceOpt = std::nullopt;
    uint64_t bestDeviceScore = 0;
    uint32_t queueIndex = 0;

    for (const auto& physicalDevice : physicalDevices)
    {
        // NOTE(MM): We want a device with a single graphics/compute queue plus presentation and texel buffer support.
        if (!isDeviceSupportingSurfacePresentation(physicalDevice, surface)
            || !isDeviceSupportingTexelBufferFormat(physicalDevice, VK_FORMAT_R32_UINT))
        {
            continue;
        }

        auto queueIndexOpt = chooseQueue(physicalDevice, surface);
        if (!queueIndexOpt.has_value())
        {
            continue;
        }

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

        if (!areDeviceLimitsSufficient(deviceProperties.limits))
        {
            fprintf(stderr,
                    "Found suitable device, but its limits are exceeded. Consider lowering grid size and compute group "
                    "size in case no other device is sufficient.");
            continue;
        }

        uint64_t deviceScore = 0;
        switch (deviceProperties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        {
            deviceScore += 100;
            break;
        }
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        {
            deviceScore += 50;
            break;
        }
        default:
            deviceScore += 10;
            break;
        }

        if (deviceScore > bestDeviceScore)
        {
            bestDeviceScore = deviceScore;
            bestDeviceOpt = physicalDevice;
            queueIndex = queueIndexOpt.value();
        }
    }

    if (!bestDeviceOpt.has_value())
    {

        fprintf(stderr, "Failed to find suitable device!\n");
        return std::nullopt;
    }

    VkPhysicalDevice physicalDevice = bestDeviceOpt.value();

    std::vector<const char*> deviceLayers = getRequiredDeviceLayers();
    std::vector<const char*> deviceExtensions = getRequiredDeviceExtensions();
    constexpr float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo deviceQueueCreateInfo{};
    deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    deviceQueueCreateInfo.queueCount = 1;
    deviceQueueCreateInfo.queueFamilyIndex = queueIndex;
    deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
    deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(deviceLayers.size());
    deviceCreateInfo.ppEnabledLayerNames = deviceLayers.data();
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkDevice device;
    VK_RETURN_ON_ERROR_V(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), std::nullopt);

    VkQueue deviceQueue;
    vkGetDeviceQueue(device, queueIndex, 0, &deviceQueue);

    VkDescriptorPoolSize storageBufferPoolSize;
    storageBufferPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storageBufferPoolSize.descriptorCount = STORAGE_BUFFER_COUNT;

    VkDescriptorPoolSize texelBufferPoolSize;
    texelBufferPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    texelBufferPoolSize.descriptorCount = TEXEL_BUFFER_COUNT;

    std::array<VkDescriptorPoolSize, 2> poolSizes{storageBufferPoolSize, texelBufferPoolSize};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = (STORAGE_BUFFER_COUNT + TEXEL_BUFFER_COUNT) * BUFFERS_PER_COMPUTE + 1;

    VkDescriptorPool descriptorPool;
    VK_RETURN_ON_ERROR_V(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), std::nullopt);

    return std::make_optional<VulkanContext::DeviceWrapper>(
        {physicalDevice, device, deviceQueue, queueIndex, descriptorPool});
}

static std::optional<VulkanContext::Swapchain> createSwapchain(const VulkanContext::DeviceWrapper& deviceWrapper,
                                                               const VkSurfaceKHR surface,
                                                               const GlfwContext& glfwContext)
{
    VkSurfaceCapabilitiesKHR surfaceCapabilites;
    VK_RETURN_ON_ERROR_V(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceWrapper.physicalDevice, surface, &surfaceCapabilites),
        std::nullopt);

    uint32_t minImageCount = surfaceCapabilites.minImageCount + 1;
    if (surfaceCapabilites.maxImageCount > 0)
    {
        minImageCount = std::min(minImageCount, surfaceCapabilites.maxImageCount);
    }

    uint32_t formatCount = 0;
    VK_RETURN_ON_ERROR_V(
        vkGetPhysicalDeviceSurfaceFormatsKHR(deviceWrapper.physicalDevice, surface, &formatCount, nullptr),
        std::nullopt);

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_RETURN_ON_ERROR_V(vkGetPhysicalDeviceSurfaceFormatsKHR(
                             deviceWrapper.physicalDevice, surface, &formatCount, surfaceFormats.data()),
                         std::nullopt);

    VkSurfaceFormatKHR surfaceFormat = surfaceFormats[0];
    for (const auto& sf : surfaceFormats)
    {
        if (sf.format == VK_FORMAT_B8G8R8A8_SRGB && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = sf;
            break;
        }
    }

    uint32_t presentModeCount = 0;
    VK_RETURN_ON_ERROR_V(
        vkGetPhysicalDeviceSurfacePresentModesKHR(deviceWrapper.physicalDevice, surface, &presentModeCount, nullptr),
        std::nullopt);

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_RETURN_ON_ERROR_V(vkGetPhysicalDeviceSurfacePresentModesKHR(
                             deviceWrapper.physicalDevice, surface, &presentModeCount, presentModes.data()),
                         std::nullopt);

    VkPresentModeKHR presentMode = presentModes[0];
    for (const auto& pm : presentModes)
    {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = pm;
            break;
        }
    }

    VkExtent2D imageExtent = surfaceCapabilites.currentExtent;

    // NOTE(MM): Extent set to max value in case window managers allows to differ to current window size. In this case
    // we need to set by ourselves.
    if (imageExtent.width == std::numeric_limits<uint32_t>::max())
    {
        auto [width, height] = glfwContext.getFramebufferSize();
        imageExtent.width = std::clamp(static_cast<uint32_t>(width),
                                       surfaceCapabilites.minImageExtent.width,
                                       surfaceCapabilites.maxImageExtent.width);
        imageExtent.height = std::clamp(static_cast<uint32_t>(height),
                                        surfaceCapabilites.minImageExtent.height,
                                        surfaceCapabilites.maxImageExtent.height);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = minImageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = imageExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = surfaceCapabilites.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    const VkDevice device = deviceWrapper.device;
    VkSwapchainKHR swapchain;
    VK_RETURN_ON_ERROR_V(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain), std::nullopt);

    uint32_t imageCount = 0;
    VK_RETURN_ON_ERROR_V(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr), std::nullopt);

    std::vector<VkImage> images(imageCount);
    VK_RETURN_ON_ERROR_V(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()), std::nullopt);

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = surfaceFormat.format;
    imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    std::vector<VkImageView> imageViews(imageCount);
    for (size_t i = 0; i < imageCount; ++i)
    {
        imageViewCreateInfo.image = images[i];
        VK_RETURN_ON_ERROR_V(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageViews[i]), std::nullopt);
    }

    return std::make_optional<VulkanContext::Swapchain>(
        {swapchain, surfaceFormat.format, imageExtent, std::move(images), std::move(imageViews)});
}

static std::optional<VkShaderModule> createShaderModule(const VkDevice device, const std::filesystem::path& shaderPath)
{
    auto shaderContentOpt = FileReading::readFile(shaderPath, std::ios::ate | std::ios::binary);
    RETURN_ON_NULLOPT_V(shaderContentOpt, std::nullopt);

    const std::vector<char>& shaderContent = shaderContentOpt.value();
    VkShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = shaderContent.size();
    shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(shaderContent.data());

    VkShaderModule shaderModule;
    VK_RETURN_ON_ERROR_V(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule), std::nullopt);

    return shaderModule;
}

static std::optional<uint32_t>
findMemoryType(const VkPhysicalDevice physicalDevice, uint32_t memoryRequirements, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // The i-th bit of the memory requirements is set if the i-th memory type of the device satisfies them. Hence
        // the check of the bits before the property check.
        if ((memoryRequirements & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    fprintf(stderr, "Failed to find suitable memory type for physical device: %p!\n", (void*)physicalDevice);
    return std::nullopt;
}

static std::optional<std::tuple<VkBuffer, VkDeviceMemory>>
createBuffer(const VulkanContext::DeviceWrapper& deviceWrapper,
             VkDeviceSize size,
             VkBufferUsageFlags usage,
             VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VkDevice device = deviceWrapper.device;
    VkBuffer buffer;
    VK_RETURN_ON_ERROR_V(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer), std::nullopt);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    std::optional<uint32_t> memoryTypeIndex =
        findMemoryType(deviceWrapper.physicalDevice, memRequirements.memoryTypeBits, properties);

    RETURN_ON_NULLOPT_V(memoryTypeIndex, std::nullopt);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex.value();

    VkDeviceMemory bufferMemory;
    VK_RETURN_ON_ERROR_V(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory), std::nullopt);

    VK_RETURN_ON_ERROR_V(vkBindBufferMemory(device, buffer, bufferMemory, 0), std::nullopt);

    return std::make_tuple(buffer, bufferMemory);
}

static bool copyBuffer(const VulkanContext::DeviceWrapper& deviceWrapper,
                       const VkCommandPool& commandPool,
                       const VkBuffer& srcBuffer,
                       const VkBuffer& dstBuffer,
                       VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    const VkDevice device = deviceWrapper.device;
    VkCommandBuffer commandBuffer;
    VK_RETURN_ON_ERROR_V(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), false);

    VkCommandBufferBeginInfo commandBufferBeginInfo{};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_RETURN_ON_ERROR_V(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo), false);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    VK_RETURN_ON_ERROR_V(vkEndCommandBuffer(commandBuffer), false);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    const VkQueue queue = deviceWrapper.queue;
    VK_RETURN_ON_ERROR_V(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), false);
    VK_RETURN_ON_ERROR_V(vkQueueWaitIdle(queue), false);

    // NOTE(MM): Temprorary buffer doesn't need to live until end of application.
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

    return true;
}

template <typename T>
static std::optional<std::tuple<VkBuffer, VkDeviceMemory>>
createDeviceLocalBuffer(const VulkanContext::DeviceWrapper& deviceWrapper,
                        const VkCommandPool commandPool,
                        const std::vector<T>& bufferData,
                        VkBufferUsageFlags usage)
{
    assert(!bufferData.empty() && "createDeviceLocalBuffer: Passed 'bufferData' vector is empty!");

    const auto bufferSize = static_cast<VkDeviceSize>(sizeof(bufferData[0]) * bufferData.size());
    auto stagingBufferAndMemoryOpt =
        createBuffer(deviceWrapper,
                     bufferSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    RETURN_ON_NULLOPT_V(stagingBufferAndMemoryOpt, std::nullopt);
    auto [stagingBuffer, stagingBufferMemory] = stagingBufferAndMemoryOpt.value();

    const VkDevice device = deviceWrapper.device;
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, bufferData.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    auto localBufferAndMemoryOpt = createBuffer(
        deviceWrapper, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RETURN_ON_NULLOPT_V(localBufferAndMemoryOpt, std::nullopt);

    std::tuple<VkBuffer, VkDeviceMemory> localBufferAndMemory = localBufferAndMemoryOpt.value();
    copyBuffer(deviceWrapper, commandPool, stagingBuffer, std::get<VkBuffer>(localBufferAndMemory), bufferSize);

    vkFreeMemory(device, stagingBufferMemory, nullptr);
    vkDestroyBuffer(device, stagingBuffer, nullptr);

    return localBufferAndMemory;
}

static std::optional<std::vector<VkBufferView>> createBufferViews(const VulkanContext::DeviceWrapper& deviceWrapper,
                                                                  const std::vector<VkBuffer>& buffers,
                                                                  size_t bufferSize)
{
    if (buffers.empty())
    {
        return std::nullopt;
    }

    VkBufferViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    createInfo.format = VK_FORMAT_R32_UINT;
    createInfo.offset = 0;
    createInfo.range = static_cast<uint32_t>(bufferSize);

    const VkDevice device = deviceWrapper.device;
    std::vector<VkBufferView> bufferViews(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        createInfo.buffer = buffers[i];
        VK_RETURN_ON_ERROR_V(vkCreateBufferView(device, &createInfo, nullptr, &bufferViews[i]), std::nullopt);
    }

    return bufferViews;
}

static std::optional<VulkanContext::ComputePipeline>
createComputePipeline(const VulkanContext::DeviceWrapper& deviceWrapper,
                      const std::vector<VkBuffer>& cellBuffers,
                      const std::vector<VkBufferView>& cellBufferViews,
                      const std::filesystem::path& executableDir,
                      size_t buffersize)
{
    assert(cellBuffers.size() == cellBufferViews.size() && "Amount of buffers needs to match buffer views!");

    std::filesystem::path shaderPath(executableDir);
    shaderPath.append(ApplicationDefines::NonModifiable::COMPUTE_SHADER_NAME);

    const VkDevice device = deviceWrapper.device;
    auto shaderModuleOpt = createShaderModule(device, shaderPath);
    RETURN_ON_NULLOPT_V(shaderModuleOpt, std::nullopt);
    VkShaderModule shaderModule = shaderModuleOpt.value();

    VkDescriptorSetLayoutBinding inBufferBinding{};
    inBufferBinding.binding = 0;
    inBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    inBufferBinding.descriptorCount = 1;
    inBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding outBufferBinding{};
    outBufferBinding.binding = 1;
    outBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outBufferBinding.descriptorCount = 1;
    outBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    const std::array<VkDescriptorSetLayoutBinding, STORAGE_BUFFER_COUNT> descriptorLayoutBindings{inBufferBinding,
                                                                                                  outBufferBinding};

    VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo{};
    descriptorLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutCreateInfo.bindingCount = static_cast<uint32_t>(descriptorLayoutBindings.size());
    descriptorLayoutCreateInfo.pBindings = descriptorLayoutBindings.data();

    VkDescriptorSetLayout descriptorSetLayout;
    VK_RETURN_ON_ERROR_V(
        vkCreateDescriptorSetLayout(device, &descriptorLayoutCreateInfo, nullptr, &descriptorSetLayout), std::nullopt);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout;
    VK_RETURN_ON_ERROR_V(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout),
                         std::nullopt);

    const auto specializationMapEntries = ComputeSpecializationConstants::getSpecializationMapEntries();
    ComputeSpecializationConstants specializationData{ApplicationDefines::COMPUTE_LOCAL_GROUP_SIZE_X,
                                                      ApplicationDefines::GRID_WIDTH,
                                                      ApplicationDefines::GRID_HEIGHT,
                                                      ApplicationDefines::ENABLE_HORIZONTAL_WRAPPING,
                                                      ApplicationDefines::STUCK_PROBABILITY};

    VkSpecializationInfo specializationInfo = {};
    specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationMapEntries.size());
    specializationInfo.pMapEntries = specializationMapEntries.data();
    specializationInfo.dataSize = sizeof(ComputeSpecializationConstants);
    specializationInfo.pData = &specializationData;

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo{};
    shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCreateInfo.module = shaderModule;
    shaderStageCreateInfo.pName = "main";
    shaderStageCreateInfo.pSpecializationInfo = &specializationInfo;

    VkComputePipelineCreateInfo pipelineCreateInfo{};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stage = shaderStageCreateInfo;
    pipelineCreateInfo.layout = pipelineLayout;

    VkPipeline pipeline;
    VK_RETURN_ON_ERROR_V(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline),
                         std::nullopt);

    std::array<VkDescriptorSetLayout, BUFFERS_PER_COMPUTE> descriptorSetLayouts{descriptorSetLayout,
                                                                                descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = deviceWrapper.descriptorPool;
    allocateInfo.descriptorSetCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    allocateInfo.pSetLayouts = descriptorSetLayouts.data();

    std::vector<VkDescriptorSet> descriptorSets(STORAGE_BUFFER_COUNT);
    VK_RETURN_ON_ERROR_V(vkAllocateDescriptorSets(device, &allocateInfo, descriptorSets.data()), std::nullopt);

    for (size_t i = 0; i < BUFFERS_PER_COMPUTE; i++)
    {
        VkDescriptorBufferInfo inBufferInfo{};
        inBufferInfo.buffer = cellBuffers[i];
        inBufferInfo.offset = 0;
        inBufferInfo.range = static_cast<uint32_t>(buffersize);

        VkDescriptorBufferInfo outBufferInfo{};
        const size_t outBufferIdx = (i + 1) % BUFFERS_PER_COMPUTE;
        outBufferInfo.buffer = cellBuffers[outBufferIdx];
        outBufferInfo.offset = 0;
        outBufferInfo.range = static_cast<uint32_t>(buffersize);

        std::array<VkWriteDescriptorSet, STORAGE_BUFFER_COUNT> writeDescriptorSets{};
        writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[0].dstSet = descriptorSets[i];
        writeDescriptorSets[0].dstBinding = 0;
        writeDescriptorSets[0].dstArrayElement = 0;
        writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].pBufferInfo = &inBufferInfo;

        writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[1].dstSet = descriptorSets[i];
        writeDescriptorSets[1].dstBinding = 1;
        writeDescriptorSets[1].dstArrayElement = 0;
        writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writeDescriptorSets[1].descriptorCount = 1;
        writeDescriptorSets[1].pBufferInfo = &outBufferInfo;

        vkUpdateDescriptorSets(
            device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    return std::make_optional<VulkanContext::ComputePipeline>({
        pipeline,
        pipelineLayout,
        descriptorSetLayout,
        shaderModule,
        std::move(descriptorSets),
    });
}

static std::optional<VkRenderPass> createRenderPass(const VkDevice& device, const VkFormat& swapchainFormat)
{
    VkAttachmentDescription colorAttachmentDescription{};
    colorAttachmentDescription.format = swapchainFormat;
    colorAttachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription{};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorAttachmentReference;

    // NOTE(MM): We have a dependency between subpasses (note that implicit subpasses are the ones
    // that are in front or after the render pass; so since we have a single one, this one happens
    // before and after it). We need to make sure that the color attachment is ready to use before
    // we try to write to it.
    VkSubpassDependency subpassDependency{};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL; // implicit subpass before render pass
    subpassDependency.dstSubpass = 0;
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &colorAttachmentDescription;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &subpassDependency;

    VkRenderPass renderPass;
    VK_RETURN_ON_ERROR_V(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass), std::nullopt);

    return renderPass;
}

static std::optional<VkPipelineLayout> createPipelineLayout(const VkDevice& device,
                                                            const VkDescriptorSetLayout descriptorSetLayout)
{
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    VK_RETURN_ON_ERROR_V(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout),
                         std::nullopt);

    return pipelineLayout;
}

static std::optional<VkDescriptorSetLayout> createDescriptorSetLayout(const VkDevice& device)
{
    VkDescriptorSetLayoutBinding cellBufferBinding{};
    cellBufferBinding.binding = 0;
    cellBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    cellBufferBinding.descriptorCount = 1;
    cellBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo{};
    descriptorLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutCreateInfo.bindingCount = 1;
    descriptorLayoutCreateInfo.pBindings = &cellBufferBinding;

    VkDescriptorSetLayout descriptorSetLayout;
    VK_RETURN_ON_ERROR_V(
        vkCreateDescriptorSetLayout(device, &descriptorLayoutCreateInfo, nullptr, &descriptorSetLayout), std::nullopt);

    return descriptorSetLayout;
}

static std::optional<std::vector<VkDescriptorSet>>
createDescriptorSets(const VulkanContext::DeviceWrapper& deviceWrapper,
                     const VkDescriptorSetLayout& descriptorSetLayout,
                     const std::vector<VkBufferView>& cellBufferViews)

{
    std::array<VkDescriptorSetLayout, BUFFERS_PER_COMPUTE> descriptorSetLayouts{descriptorSetLayout,
                                                                                descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = deviceWrapper.descriptorPool;
    allocateInfo.descriptorSetCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    allocateInfo.pSetLayouts = descriptorSetLayouts.data();

    const VkDevice device = deviceWrapper.device;
    std::vector<VkDescriptorSet> descriptorSets(STORAGE_BUFFER_COUNT);
    VK_RETURN_ON_ERROR_V(vkAllocateDescriptorSets(device, &allocateInfo, descriptorSets.data()), std::nullopt);

    for (size_t i = 0; i < BUFFERS_PER_COMPUTE; i++)
    {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSets[i];
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.pTexelBufferView = &cellBufferViews[i];

        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
    }

    return descriptorSets;
}

static std::optional<std::vector<VkFramebuffer>>
createFramebuffers(const VkDevice& device, const VkRenderPass& renderPass, const VulkanContext::Swapchain& swapchain)
{
    const auto& swapchainImageViews = swapchain.imageViews;
    const auto& swapchainImageExtent = swapchain.imageExtent;

    std::vector<VkFramebuffer> framebuffers;
    framebuffers.reserve(swapchainImageViews.size());

    for (const auto& imageView : swapchainImageViews)
    {
        std::array<const VkImageView, 1> framebufferAttachments{imageView};

        VkFramebufferCreateInfo framebufferCreateInfo{};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments.size());
        framebufferCreateInfo.pAttachments = framebufferAttachments.data();
        framebufferCreateInfo.width = swapchainImageExtent.width;
        framebufferCreateInfo.height = swapchainImageExtent.height;
        framebufferCreateInfo.layers = 1;

        VkFramebuffer framebuffer;
        VK_RETURN_ON_ERROR_V(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer), std::nullopt);

        framebuffers.push_back(framebuffer);
    }

    return framebuffers;
}

static std::optional<VulkanContext::GraphicsPipeline>
createGraphicsPipeline(const VulkanContext::DeviceWrapper& deviceWrapper,
                       const VulkanContext::Swapchain& swapchain,
                       const std::vector<VkBufferView>& cellBufferViews,
                       const std::filesystem::path& executableDir)
{
    std::filesystem::path vertexShaderPath(executableDir);
    vertexShaderPath.append(ApplicationDefines::NonModifiable::VERTEX_SHADER_NAME);

    const VkDevice device = deviceWrapper.device;
    auto vertexShaderModuleOpt = createShaderModule(device, vertexShaderPath);
    RETURN_ON_NULLOPT_V(vertexShaderModuleOpt, std::nullopt);
    VkShaderModule vertexShaderModule = vertexShaderModuleOpt.value();

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{};
    vertexShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShaderStageInfo.module = vertexShaderModule;
    vertexShaderStageInfo.pName = "main";

    std::filesystem::path fragmentShaderPath(executableDir);
    fragmentShaderPath.append(ApplicationDefines::NonModifiable::FRAGMENT_SHADER_NAME);

    auto fragmentShaderModuleOpt = createShaderModule(device, fragmentShaderPath);
    RETURN_ON_NULLOPT_V(fragmentShaderModuleOpt, std::nullopt);
    VkShaderModule fragmentShaderModule = fragmentShaderModuleOpt.value();

    const auto fragmentSpecializationMapEntries = FragmentSpecializationConstants::getSpecializationMapEntries();
    FragmentSpecializationConstants fragmentSpecializationData{ApplicationDefines::GRID_WIDTH,
                                                               ApplicationDefines::GRID_HEIGHT};

    VkSpecializationInfo fragmentSpecializationInfo = {};
    fragmentSpecializationInfo.mapEntryCount = static_cast<uint32_t>(fragmentSpecializationMapEntries.size());
    fragmentSpecializationInfo.pMapEntries = fragmentSpecializationMapEntries.data();
    fragmentSpecializationInfo.dataSize = sizeof(FragmentSpecializationConstants);
    fragmentSpecializationInfo.pData = &fragmentSpecializationData;

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{};
    fragmentShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShaderStageInfo.module = fragmentShaderModule;
    fragmentShaderStageInfo.pName = "main";
    fragmentShaderStageInfo.pSpecializationInfo = &fragmentSpecializationInfo;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{vertexShaderStageInfo, fragmentShaderStageInfo};

    auto descriptorSetLayoutOpt = createDescriptorSetLayout(device);
    RETURN_ON_NULLOPT_V(descriptorSetLayoutOpt, std::nullopt);
    VkDescriptorSetLayout descriptorSetLayout = descriptorSetLayoutOpt.value();

    auto pipelineLayoutOpt = createPipelineLayout(device, descriptorSetLayout);
    RETURN_ON_NULLOPT_V(pipelineLayoutOpt, std::nullopt);
    VkPipelineLayout pipelineLayout = pipelineLayoutOpt.value();

    auto renderPassOpt = createRenderPass(device, swapchain.imageFormat);
    RETURN_ON_NULLOPT_V(renderPassOpt, std::nullopt);
    VkRenderPass renderPass = renderPassOpt.value();

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{};
    vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateCreateInfo.vertexBindingDescriptionCount = 0;
    vertexInputStateCreateInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo{};
    inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportStateCreateInfo{};
    viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCreateInfo.viewportCount = 1;
    viewportStateCreateInfo.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo{};
    rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
    rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
    rasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
    rasterizationStateCreateInfo.depthBiasClamp = VK_FALSE;
    rasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
    rasterizationStateCreateInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo{};
    multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
    multisampleStateCreateInfo.minSampleShading = 1.0f;
    multisampleStateCreateInfo.pSampleMask = nullptr;
    multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
    multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};
    colorBlendAttachmentState.blendEnable = VK_FALSE;
    colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo{};
    colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
    colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    colorBlendStateCreateInfo.attachmentCount = 1;
    colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;

    std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{};
    dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
    graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphicsPipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    graphicsPipelineCreateInfo.pStages = shaderStages.data();
    graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
    graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
    graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
    graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
    graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
    graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
    graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
    graphicsPipelineCreateInfo.layout = pipelineLayout;
    graphicsPipelineCreateInfo.renderPass = renderPass;
    graphicsPipelineCreateInfo.subpass = 0;
    graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    graphicsPipelineCreateInfo.basePipelineIndex = -1;

    VkPipeline pipeline;
    VK_RETURN_ON_ERROR_V(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &pipeline),
        std::nullopt);

    auto descriptorSetsOpt = createDescriptorSets(deviceWrapper, descriptorSetLayout, cellBufferViews);
    RETURN_ON_NULLOPT_V(descriptorSetsOpt, std::nullopt);
    std::vector<VkDescriptorSet> descriptorSets = descriptorSetsOpt.value();

    auto framebuffersOpt = createFramebuffers(device, renderPass, swapchain);
    RETURN_ON_NULLOPT_V(framebuffersOpt, std::nullopt);
    std::vector<VkFramebuffer> framebuffers = framebuffersOpt.value();

    return std::make_optional<VulkanContext::GraphicsPipeline>({pipeline,
                                                                pipelineLayout,
                                                                descriptorSetLayout,
                                                                renderPass,
                                                                vertexShaderModule,
                                                                fragmentShaderModule,
                                                                std::move(descriptorSets),
                                                                std::move(framebuffers)});
}

static std::optional<VkCommandPool> createCommandPool(const VulkanContext::DeviceWrapper& deviceWrapper)
{
    VkCommandPoolCreateInfo commandPoolCreateInfo{};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = deviceWrapper.queueIndex;

    VkCommandPool commandPool;
    VK_RETURN_ON_ERROR_V(vkCreateCommandPool(deviceWrapper.device, &commandPoolCreateInfo, nullptr, &commandPool),
                         std::nullopt);

    return commandPool;
}

static std::optional<VkCommandBuffer> createCommandBuffer(const VulkanContext::DeviceWrapper& deviceWrapper,
                                                          const VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer buffer;
    VK_RETURN_ON_ERROR_V(vkAllocateCommandBuffers(deviceWrapper.device, &commandBufferAllocateInfo, &buffer),
                         std::nullopt);

    return buffer;
}

VulkanContext::VulkanContext(ApplicationSharedData& applicationSharedData,
                             GlfwContext& glfwContext,
                             const std::vector<uint32_t>& cellGrid)
    : instance(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , deviceWrapper({VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_NULL_HANDLE})
    , swapchain({VK_NULL_HANDLE, VK_FORMAT_UNDEFINED, {0, 0}, {}, {}})
    , computePipeline({VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, {}})
    , graphicsPipeline(
          {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, {}, {}})
    , commandPool(VK_NULL_HANDLE)
    , imageAvailableSemaphore(VK_NULL_HANDLE)
    , renderingFinishedSemaphore(VK_NULL_HANDLE)
    , inFlightFence(VK_NULL_HANDLE)
#ifdef VALIDATION_LAYERS
    , _debugReportCallback(VK_NULL_HANDLE)
#endif
    , _glfwContext(glfwContext)
    , _isInitialized(false)
{
    auto instanceOpt = createInstance(_glfwContext);
    RETURN_ON_NULLOPT(instanceOpt);
    instance = instanceOpt.value();

#ifdef VALIDATION_LAYERS
    auto debugReportCallbackOpt = createDebugReportCallback(instance);
    RETURN_ON_NULLOPT(debugReportCallbackOpt);
    _debugReportCallback = debugReportCallbackOpt.value();
#endif

    // NOTE(MM): Use glfw functionality instead of directly calling 'vkCreateXcbSurfaceKHR' manually.
    VK_RETURN_ON_ERROR(glfwContext.createWindowSurface(instance, nullptr, &surface));

    auto vulkanDeviceOpt = createDevice(instance, surface);
    RETURN_ON_NULLOPT(vulkanDeviceOpt);
    deviceWrapper = std::move(vulkanDeviceOpt.value());

    auto swapchainOpt = createSwapchain(deviceWrapper, surface, _glfwContext);
    RETURN_ON_NULLOPT(swapchainOpt);
    swapchain = std::move(swapchainOpt.value());

    auto commandPoolOpt = createCommandPool(deviceWrapper);
    RETURN_ON_NULLOPT(commandPoolOpt);
    commandPool = commandPoolOpt.value();

    auto commandBufferOpt = createCommandBuffer(deviceWrapper, commandPool);
    RETURN_ON_NULLOPT(commandBufferOpt);
    commandBuffer = std::move(commandBufferOpt.value());

    // NOTE(MM): Creating and uploading buffers individually isn't the fastest approach. However, since
    // we only do it twice for the whole application the overhead is negligible.
    for (uint32_t i = 0; i < BUFFERS_PER_COMPUTE; ++i)
    {
        auto localBufferAndMemoryOpt =
            createDeviceLocalBuffer(deviceWrapper,
                                    commandPool,
                                    cellGrid,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);

        RETURN_ON_NULLOPT(localBufferAndMemoryOpt);
        auto [buffer, deviceMemory] = localBufferAndMemoryOpt.value();

        cellBuffers.push_back(buffer);
        cellBuffersMemory.push_back(deviceMemory);
    }

    const size_t bufferSize = cellGrid.size() * sizeof(cellGrid[0]);
    auto buffersViewOpt = createBufferViews(deviceWrapper, cellBuffers, bufferSize);
    RETURN_ON_NULLOPT(buffersViewOpt);
    cellBuffersView = std::move(buffersViewOpt.value());

    const std::filesystem::path& executableDirectory = applicationSharedData.executableDirectory;
    auto computePipelineOpt =
        createComputePipeline(deviceWrapper, cellBuffers, cellBuffersView, executableDirectory, bufferSize);
    RETURN_ON_NULLOPT(computePipelineOpt);
    computePipeline = std::move(computePipelineOpt.value());

    auto graphicsPipelineOpt = createGraphicsPipeline(deviceWrapper, swapchain, cellBuffersView, executableDirectory);
    RETURN_ON_NULLOPT(graphicsPipelineOpt);
    graphicsPipeline = std::move(graphicsPipelineOpt.value());

    const VkDevice device = deviceWrapper.device;
    VkSemaphoreCreateInfo semaphoreCreateInfo;
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;
    VK_RETURN_ON_ERROR(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore));
    VK_RETURN_ON_ERROR(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderingFinishedSemaphore));

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // otherwise we would wait for the fence on first draw
    VK_RETURN_ON_ERROR(vkCreateFence(device, &fenceCreateInfo, nullptr, &inFlightFence));

    _isInitialized = true;
}

VulkanContext::~VulkanContext()
{
    const VkDevice device = deviceWrapper.device;
    if (device != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, inFlightFence, nullptr);
        vkDestroySemaphore(device, renderingFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

        for (auto& framebuffer : graphicsPipeline.framebuffers)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        vkDestroyPipeline(device, graphicsPipeline.pipeline, nullptr);
        vkDestroyRenderPass(device, graphicsPipeline.renderPass, nullptr);
        vkDestroyPipelineLayout(device, graphicsPipeline.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, graphicsPipeline.descriptorSetLayout, nullptr);
        vkDestroyShaderModule(device, graphicsPipeline.fragmentShader, nullptr);
        vkDestroyShaderModule(device, graphicsPipeline.vertexShader, nullptr);

        vkDestroyPipeline(device, computePipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(device, computePipeline.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, computePipeline.descriptorSetLayout, nullptr);
        vkDestroyShaderModule(device, computePipeline.shader, nullptr);

        for (auto& cellBufferView : cellBuffersView)
        {
            vkDestroyBufferView(device, cellBufferView, nullptr);
        }

        for (auto& cellBufferMemory : cellBuffersMemory)
        {
            vkFreeMemory(device, cellBufferMemory, nullptr);
        }

        for (auto& cellBuffer : cellBuffers)
        {
            vkDestroyBuffer(device, cellBuffer, nullptr);
        }

        vkDestroyCommandPool(device, commandPool, nullptr);

        for (auto& imageView : swapchain.imageViews)
        {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
        vkDestroyDescriptorPool(device, deviceWrapper.descriptorPool, nullptr);
        vkDestroyDevice(device, nullptr);
    }

    if (instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);

#ifdef VALIDATION_LAYERS
        auto destroyDebugReportCallbackFP = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
        if (destroyDebugReportCallbackFP) destroyDebugReportCallbackFP(instance, _debugReportCallback, nullptr);
#endif
        vkDestroyInstance(instance, nullptr);
    }
}

VulkanContext::operator bool() const
{
    return _isInitialized;
}

bool VulkanContext::recreateSwapchain(void)
{
    const VkDevice device = deviceWrapper.device;

    vkDeviceWaitIdle(device);

    // NOTE(MM): Recreating of swapchain possibly happens after the call to 'vkAcquireNextImageKHR'. In this case the
    // 'imageAvailableSemaphore' ends up in a signaled state, which is probably not wanted. Hence, recreate this
    // semaphore here.
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

    for (auto& framebuffer : graphicsPipeline.framebuffers)
    {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (auto& imageView : swapchain.imageViews)
    {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);

    auto swapchainOpt = createSwapchain(deviceWrapper, surface, _glfwContext);
    RETURN_ON_NULLOPT_V(swapchainOpt, false);
    swapchain = std::move(swapchainOpt.value());

    auto framebuffersOpt = createFramebuffers(device, graphicsPipeline.renderPass, swapchain);
    RETURN_ON_NULLOPT_V(framebuffersOpt, false);
    graphicsPipeline.framebuffers = std::move(framebuffersOpt.value());

    VkSemaphoreCreateInfo semaphoreCreateInfo;
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;
    VK_RETURN_ON_ERROR_V(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &imageAvailableSemaphore), false);

    return true;
}

} // namespace VkHourglass
