#ifndef VULKANHOURGLASS_PUSHCONSTANTS_HPP
#define VULKANHOURGLASS_PUSHCONSTANTS_HPP

#include <cstdint>

namespace VkHourglass
{

struct PushConstants
{
    alignas(4) uint32_t cellOffset;
    alignas(4) int32_t seed;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_PUSHCONSTANTS_HPP
