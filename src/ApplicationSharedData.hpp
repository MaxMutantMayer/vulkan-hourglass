#ifndef VULKANHOURGLASS_APPLICATIONSHAREDDATA_HPP
#define VULKANHOURGLASS_APPLICATIONSHAREDDATA_HPP

#include <atomic>
#include <filesystem>

namespace VkHourglass
{

// NOTE(MM): Assumed to be shared between all threads and resources (GLFW and Vulkan).
// Hence, all member access thread safe, or access is explicitly marked if it's not the case (e.g. "setXNoLock()").
struct ApplicationSharedData
{
    const std::filesystem::path executableDirectory;
    std::atomic_bool exitApplication = false;
    std::atomic_bool framebufferResized = false;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_APPLICATIONSHAREDDATA_HPP
