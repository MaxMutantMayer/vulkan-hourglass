#ifndef VULKANHOURGLASS_VULKANCONTEXT_HPP
#define VULKANHOURGLASS_VULKANCONTEXT_HPP

#include <vector>

#include <vulkan/vulkan_core.h>

namespace VkHourglass
{

struct ApplicationSharedData;
class GlfwContext;

// Wrapper object to hold all Vulkan resources of the application.
class VulkanContext
{
public:
    // Initialize Vulkan and create all needed resources.
    // Check with `operator bool()` if initialization succeeded.
    explicit VulkanContext(ApplicationSharedData& applicationSharedData,
                           GlfwContext& glfwContext,
                           const std::vector<uint32_t>& cellGrid);
    ~VulkanContext();

    // NOTE(MM): We don't need copies/moves in our application. Therefore, delete copy/moves operations to avoid
    // unwanted resource destruction.
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) noexcept = delete;
    VulkanContext& operator=(VulkanContext&&) noexcept = delete;

    explicit operator bool() const;

    bool recreateSwapchain(void);

public:
    VkInstance instance;
    VkSurfaceKHR surface;

    struct DeviceWrapper
    {
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkQueue queue;
        uint32_t queueIndex;
        VkDescriptorPool descriptorPool;
    };
    DeviceWrapper deviceWrapper;

    struct Swapchain
    {
        VkSwapchainKHR swapchain;
        VkFormat imageFormat;
        VkExtent2D imageExtent;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
    };
    Swapchain swapchain;

    struct ComputePipeline
    {
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorSetLayout;
        VkShaderModule shader;
        std::vector<VkDescriptorSet> descriptorSets;
    };
    ComputePipeline computePipeline;

    struct GraphicsPipeline
    {
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorSetLayout;
        VkRenderPass renderPass;
        VkShaderModule vertexShader;
        VkShaderModule fragmentShader;
        std::vector<VkDescriptorSet> descriptorSets;
        std::vector<VkFramebuffer> framebuffers;
    };
    GraphicsPipeline graphicsPipeline;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderingFinishedSemaphore;
    VkFence inFlightFence;

    std::vector<VkBuffer> cellBuffers;
    std::vector<VkDeviceMemory> cellBuffersMemory;
    std::vector<VkBufferView> cellBuffersView;

private:
#ifdef VALIDATION_LAYERS
    VkDebugReportCallbackEXT _debugReportCallback;
#endif

    GlfwContext& _glfwContext;
    bool _isInitialized;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_VULKANCONTEXT_HPP
