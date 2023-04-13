#ifndef VULKANHOURGLASS_GRID_HPP
#define VULKANHOURGLASS_GRID_HPP

#include <cstdint>
#include <vector>

namespace VkHourglass
{

std::vector<uint32_t> generateHourglass(void);
std::vector<uint32_t> generateCenterCircle(void);
std::vector<uint32_t> generateRandomCircles(void);
std::vector<uint32_t> generateRandomNoise(void);

} // namespace VkHourglass

#endif // VULKANHOURGLASS_GRID_HPP
