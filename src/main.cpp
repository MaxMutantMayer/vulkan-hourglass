#include <filesystem>
#include <iostream>
#include <random>

#include "ApplicationDefines.hpp"
#include "ApplicationSharedData.hpp"
#include "ComputeUpdateTimer.hpp"
#include "GlfwContext.hpp"
#include "Grid.hpp"
#include "Macros.hpp"
#include "PushConstants.hpp"
#include "RuntimeStatistics.hpp"
#include "VulkanContext.hpp"

static bool beginCommandBuffer(const VkCommandBuffer commandBuffer);

static void recordComputeCommands(const VkHourglass::VulkanContext::ComputePipeline& computePipeline,
                                  const VkCommandBuffer commandBuffer,
                                  size_t currentBuffer,
                                  std::mt19937& mtRand);

static void addMemoryBarrier(const VkCommandBuffer commandBuffer,
                             uint32_t queueIndex,
                             size_t currentBuffer,
                             const std::vector<VkBuffer>& cellBuffers);

static bool recordDrawCommands(VkCommandBuffer commandBuffer,
                               const VkHourglass::VulkanContext::GraphicsPipeline& graphicsPipeline,
                               const VkExtent2D& swapchainExtent,
                               size_t currentFrame,
                               uint32_t swapchainImageIndex);

static void submitCommands(const VkHourglass::VulkanContext& context);
static VkResult presentFramebuffer(VkHourglass::VulkanContext& context, uint32_t swapchainImageIndex);

int main(int argc, char* argv[])
{
    if (argc == 0 || !std::filesystem::exists(argv[0]))
    {
        fprintf(stderr, "argv[0] is expected to be executable path / path relative to shaders!\n");
        return EXIT_FAILURE;
    }

    const std::filesystem::path executableDirectory = std::filesystem::absolute(argv[0]).parent_path();
    VkHourglass::ApplicationSharedData applicationSharedData{executableDirectory, false, false};

    VkHourglass::GlfwContext glfwContext(applicationSharedData,
                                         VkHourglass::ApplicationDefines::WINDOW_WIDTH,
                                         VkHourglass::ApplicationDefines::WINDOW_HEIGHT);
    if (!glfwContext)
    {
        fprintf(stderr, "Failed to initialize GLFW!\n");
        return EXIT_FAILURE;
    }

    const std::vector<uint32_t> grid = VkHourglass::generateHourglass();
    VkHourglass::VulkanContext vulkanContext(applicationSharedData, glfwContext, grid);
    if (!vulkanContext)
    {
        fprintf(stderr, "Failed to initialize Vulkan!\n");
        return EXIT_FAILURE;
    }

    std::random_device randomDevice;
    std::mt19937 mtRand(randomDevice());
    size_t currentGridBuffer = 0;

    VkHourglass::RuntimeStatistics runtimeStatistics;
    VkHourglass::ComputeUpdateTimer computeUpdateTimer(VkHourglass::ApplicationDefines::CELL_UPDATE_INTERVAL_MS);

    while (!applicationSharedData.exitApplication.load())
    {
        runtimeStatistics.notifyFrameBegin();
        glfwContext.update();

        vkWaitForFences(vulkanContext.deviceWrapper.device, 1, &vulkanContext.inFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(vulkanContext.deviceWrapper.device,
                                                vulkanContext.swapchain.swapchain,
                                                UINT64_MAX,
                                                vulkanContext.imageAvailableSemaphore,
                                                VK_NULL_HANDLE,
                                                &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR
            || applicationSharedData.framebufferResized)
        {
            applicationSharedData.framebufferResized.store(false);
            vulkanContext.recreateSwapchain();
            continue;
        }

        vkResetFences(vulkanContext.deviceWrapper.device, 1, &vulkanContext.inFlightFence);

        const VkCommandBuffer commandBuffer = vulkanContext.commandBuffer;
        vkResetCommandBuffer(commandBuffer, 0);
        beginCommandBuffer(commandBuffer);

        if (computeUpdateTimer.isUpdateNeeded())
        {
            recordComputeCommands(vulkanContext.computePipeline, commandBuffer, currentGridBuffer, mtRand);
            addMemoryBarrier(
                commandBuffer, vulkanContext.deviceWrapper.queueIndex, currentGridBuffer, vulkanContext.cellBuffers);

            currentGridBuffer = !currentGridBuffer;
            computeUpdateTimer.notifyUpdateScheduled();
        }

        recordDrawCommands(commandBuffer,
                           vulkanContext.graphicsPipeline,
                           vulkanContext.swapchain.imageExtent,
                           currentGridBuffer,
                           imageIndex);

        submitCommands(vulkanContext);

        result = presentFramebuffer(vulkanContext, imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            vulkanContext.recreateSwapchain();
        }
    }
    runtimeStatistics.printResults();

    vkDeviceWaitIdle(vulkanContext.deviceWrapper.device);

    return EXIT_SUCCESS;
}

static bool beginCommandBuffer(const VkCommandBuffer commandBuffer)
{
    VkCommandBufferBeginInfo commandBufferBeginInfo;
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_RETURN_ON_ERROR_V(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo), false);

    return true;
}

static void recordComputeCommands(const VkHourglass::VulkanContext::ComputePipeline& computePipeline,
                                  const VkCommandBuffer commandBuffer,
                                  size_t currentBuffer,
                                  std::mt19937& mtRand)
{
    const VkPipelineLayout pipelineLayout = computePipeline.pipelineLayout;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout,
                            0,
                            1,
                            &computePipeline.descriptorSets[currentBuffer],
                            0,
                            0);

    const VkHourglass::PushConstants pushConstants{static_cast<uint32_t>(currentBuffer),
                                                   static_cast<int32_t>(mtRand())};
    vkCmdPushConstants(
        commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer, VkHourglass::ApplicationDefines::NonModifiable::X_DISPATCH_COUNT, 1, 1);
}

static void addMemoryBarrier(const VkCommandBuffer commandBuffer,
                             uint32_t queueIndex,
                             size_t currentBuffer,
                             const std::vector<VkBuffer>& cellBuffers)
{
    VkBufferMemoryBarrier bufferMemoryBarrier{};
    bufferMemoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferMemoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    bufferMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    bufferMemoryBarrier.srcQueueFamilyIndex = queueIndex;
    bufferMemoryBarrier.dstQueueFamilyIndex = queueIndex;

    const size_t writtenBuffer = !currentBuffer;
    bufferMemoryBarrier.buffer = cellBuffers[writtenBuffer];
    bufferMemoryBarrier.offset = 0;

    static constexpr uint32_t bufferSize = VkHourglass::ApplicationDefines::NonModifiable::GRID_SIZE * sizeof(uint32_t);
    bufferMemoryBarrier.size = bufferSize;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_DEPENDENCY_DEVICE_GROUP_BIT,
                         0,
                         nullptr,
                         1,
                         &bufferMemoryBarrier,
                         0,
                         nullptr);
}

static bool recordDrawCommands(VkCommandBuffer commandBuffer,
                               const VkHourglass::VulkanContext::GraphicsPipeline& graphicsPipeline,
                               const VkExtent2D& swapchainExtent,
                               size_t currentFrame,
                               uint32_t swapchainImageIndex)
{

    VkRenderPassBeginInfo renderPassBeginInfo;
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.renderPass = graphicsPipeline.renderPass;
    renderPassBeginInfo.framebuffer = graphicsPipeline.framebuffers[swapchainImageIndex];
    renderPassBeginInfo.renderArea = {{0, 0}, swapchainExtent};
    renderPassBeginInfo.clearValueCount = 1;

    VkClearValue clearColor{{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassBeginInfo.pClearValues = &clearColor;

    // VK_SUBPASS_CONTENTS_INLINE: Render pass is embedded in primary command buffer and not
    // secondary buffer is used.
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.pipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphicsPipeline.pipelineLayout,
                            0,
                            1,
                            &graphicsPipeline.descriptorSets[currentFrame],
                            0,
                            0);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    VK_RETURN_ON_ERROR_V(vkEndCommandBuffer(commandBuffer), false);

    return true;
}

static void submitCommands(const VkHourglass::VulkanContext& context)
{
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &context.imageAvailableSemaphore;

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &context.renderingFinishedSemaphore;

    if (vkQueueSubmit(context.deviceWrapper.queue, 1, &submitInfo, context.inFlightFence) != VK_SUCCESS)
    {
        fprintf(stderr, "Failed to submit draw command!\n");
    }
}

VkResult presentFramebuffer(VkHourglass::VulkanContext& context, uint32_t swapchainImageIndex)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &context.renderingFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &context.swapchain.swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;
    presentInfo.pResults = nullptr;

    return vkQueuePresentKHR(context.deviceWrapper.queue, &presentInfo);
}
