#include "SpecializationConstants.hpp"

namespace VkHourglass
{

std::array<VkSpecializationMapEntry, 5> ComputeSpecializationConstants::getSpecializationMapEntries(void)
{
    std::array<VkSpecializationMapEntry, 5> constants;

    constants[0].constantID = 0;
    constants[0].offset = 0;
    constants[0].size = sizeof(uint32_t);

    constants[1].constantID = 1;
    constants[1].offset = offsetof(ComputeSpecializationConstants, gridWidth);
    constants[1].size = sizeof(uint32_t);

    constants[2].constantID = 2;
    constants[2].offset = offsetof(ComputeSpecializationConstants, gridHeight);
    constants[2].size = sizeof(uint32_t);

    constants[3].constantID = 3;
    constants[3].offset = offsetof(ComputeSpecializationConstants, enableHorizontalWrapping);
    constants[3].size = sizeof(uint32_t);

    constants[4].constantID = 4;
    constants[4].offset = offsetof(ComputeSpecializationConstants, stuckProbability);
    constants[4].size = sizeof(float);

    return constants;
}

std::array<VkSpecializationMapEntry, 2> FragmentSpecializationConstants::getSpecializationMapEntries(void)
{
    std::array<VkSpecializationMapEntry, 2> constants;

    constants[0].constantID = 0;
    constants[0].offset = 0;
    constants[0].size = sizeof(uint32_t);

    constants[1].constantID = 1;
    constants[1].offset = offsetof(FragmentSpecializationConstants, gridHeight);
    constants[1].size = sizeof(uint32_t);

    return constants;
}

}; // namespace VkHourglass
