#ifndef VULKANHOURGLASS_GLFWCONTEXT_HPP
#define VULKANHOURGLASS_GLFWCONTEXT_HPP

#include <tuple>
#include <vector>

#include <vulkan/vulkan_core.h>

struct GLFWwindow;

namespace VkHourglass
{
struct ApplicationSharedData;

class GlfwContext
{
public:
    // Initialize GLFW and create the application's window.
    // Check with `operator bool()` if initialization succeeded.
    GlfwContext(ApplicationSharedData& applicationSharedData, int windowWidth, int windowHeight);
    ~GlfwContext();

    // NOTE(MM): We don't need copies/moves in our application. Therefore, delete copy/moves operations to avoid
    // unwanted resource destruction.
    GlfwContext(const GlfwContext&) = delete;
    GlfwContext(GlfwContext&&) noexcept = delete;
    GlfwContext& operator=(const GlfwContext&) = delete;
    GlfwContext& operator=(GlfwContext&&) noexcept = delete;

    explicit operator bool() const;

    void update(void);

    std::vector<const char*> getRequiredExtensions(void) const;
    std::tuple<int, int> getFramebufferSize(void) const;

    VkResult createWindowSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

private:
    ApplicationSharedData& _applicationSharedData;
    GLFWwindow* _window;
    bool _isInitialized;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_GLFWCONTEXT_HPP
