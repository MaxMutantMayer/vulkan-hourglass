#ifndef VULKANHOURGLASS_SPECIALIZATIONCONSTANTS_HPP
#define VULKANHOURGLASS_SPECIALIZATIONCONSTANTS_HPP

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace VkHourglass
{

struct ComputeSpecializationConstants
{
    static std::array<VkSpecializationMapEntry, 5> getSpecializationMapEntries(void);

    alignas(4) uint32_t localGroupSizeX;
    alignas(4) uint32_t gridWidth;
    alignas(4) uint32_t gridHeight;
    alignas(4) uint32_t enableHorizontalWrapping;
    alignas(4) float stuckProbability;
};

struct FragmentSpecializationConstants
{
    static std::array<VkSpecializationMapEntry, 2> getSpecializationMapEntries(void);

    alignas(4) uint32_t gridWidth;
    alignas(4) uint32_t gridHeight;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_SPECIALIZATIONCONSTANTS_HPP
