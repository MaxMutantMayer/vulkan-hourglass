#ifndef VULKANHOURGLASS_MACROS_HPP
#define VULKANHOURGLASS_MACROS_HPP

#define RETURN_ON_NULLOPT(opt)                                                                                         \
    if (!opt.has_value())                                                                                              \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
    else                                                                                                               \
        ((void)0)

#define RETURN_ON_NULLOPT_V(opt, v)                                                                                    \
    if (!opt.has_value())                                                                                              \
    {                                                                                                                  \
        return v;                                                                                                      \
    }                                                                                                                  \
    else                                                                                                               \
        ((void)0)

#define VK_RETURN_ON_ERROR(expr)                                                                                       \
    if (const VkResult result = expr; result != VK_SUCCESS)                                                            \
    {                                                                                                                  \
        fprintf(stderr, "Vulkan command '%s' failed with result: %d\n", #expr, result);                                \
        return;                                                                                                        \
    }                                                                                                                  \
    else                                                                                                               \
        ((void)0)

#define VK_RETURN_ON_ERROR_V(expr, v)                                                                                  \
    if (const VkResult result = expr; result != VK_SUCCESS)                                                            \
    {                                                                                                                  \
        fprintf(stderr, "Vulkan command '%s' failed with result: %d\n", #expr, result);                                \
        return v;                                                                                                      \
    }                                                                                                                  \
    else                                                                                                               \
        ((void)0)

#endif // VULKANHOURGLASS_MACROS_HPP
