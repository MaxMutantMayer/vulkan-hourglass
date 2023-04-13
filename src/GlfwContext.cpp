#include "GlfwContext.hpp"

#include <cassert>
#include <cstdio>

#include <GLFW/glfw3.h>

#include "ApplicationSharedData.hpp"

static void glfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void glfwKeyCallback(GLFWwindow* window, int key, int /* scancode */, int action, int /* mods */)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        auto applicationSharedData =
            reinterpret_cast<VkHourglass::ApplicationSharedData*>(glfwGetWindowUserPointer(window));
        assert(applicationSharedData && "Couldn't get shared data from GLFW window on ESC key press!");
        applicationSharedData->exitApplication.store(true);
    }
}

static void gflwWindowCloseCallback(GLFWwindow* window)
{
    auto applicationSharedData =
        reinterpret_cast<VkHourglass::ApplicationSharedData*>(glfwGetWindowUserPointer(window));
    assert(applicationSharedData && "Couldn't get shared data from GLFW window in window close callback!");
    applicationSharedData->exitApplication.store(true);
}

static void glfwFramebufferSizeCallback(GLFWwindow* window, int /* width */, int /* height */)
{
    auto applicationSharedData =
        reinterpret_cast<VkHourglass::ApplicationSharedData*>(glfwGetWindowUserPointer(window));
    assert(applicationSharedData && "Couldn't get shared data from GLFW window in framebuffer size callback!");
    applicationSharedData->framebufferResized.store(true);
}

namespace VkHourglass
{

GlfwContext::GlfwContext(ApplicationSharedData& applicationSharedData, int windowWidth, int windowHeight)
    : _applicationSharedData(applicationSharedData)
    , _window(nullptr)
    , _isInitialized(false)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW!\n");
        return;
    }

    if (!glfwVulkanSupported())
    {
        fprintf(stderr, "Vulkan not supported by GLFW!\n");
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    _window = glfwCreateWindow(windowWidth, windowHeight, "Vulkan Hourglass", NULL, NULL);

    if (!_window)
    {
        fprintf(stderr, "Couldn't create GLFW window!\n");
        return;
    }

    glfwSetWindowUserPointer(_window, &_applicationSharedData);
    glfwSetKeyCallback(_window, &glfwKeyCallback);
    glfwSetWindowCloseCallback(_window, &gflwWindowCloseCallback);
    glfwSetFramebufferSizeCallback(_window, &glfwFramebufferSizeCallback);

    _isInitialized = true;
}

GlfwContext::~GlfwContext()
{
    if (_window)
    {
        glfwDestroyWindow(_window);
    }
    glfwTerminate();
}

GlfwContext::operator bool() const
{
    return _isInitialized;
}

// NOTE(MM): Update is not marked as `const` because poll can lead to call of the set input/window callbacks. So
// although no members are explicitly modified in this call, assume that it will lead to modification of the
// `_applicationSharedData` member.
void GlfwContext::update(void)
{
    glfwPollEvents();
}

std::vector<const char*> GlfwContext::getRequiredExtensions(void) const
{
    uint32_t extensionCount = 0;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);

    if (!requiredExtensions)
    {
        fprintf(stderr, "Error while querying required GLFW result!\n");
        return {};
    }

    std::vector<const char*> result(extensionCount);
    for (size_t i = 0; i < extensionCount; ++i)
    {
        // NOTE(MM): Lifetime of array/pointers guaranteed to be valid until glfw is destroyed.
        // See: https://www.glfw.org/docs/latest/group__vulkan.html#ga99ad342d82f4a3421e2864978cb6d1d6
        result[i] = requiredExtensions[i];
    }
    return result;
}

std::tuple<int, int> GlfwContext::getFramebufferSize(void) const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(_window, &width, &height);

    return {width, height};
}

VkResult
GlfwContext::createWindowSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
{
    return glfwCreateWindowSurface(instance, _window, allocator, surface);
}

} // namespace VkHourglass
