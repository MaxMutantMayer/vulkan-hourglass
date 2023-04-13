#ifndef VULKANHOURGLASS_APPLICATIONDEFINES_HPP
#define VULKANHOURGLASS_APPLICATIONDEFINES_HPP

#include <cstdint>
#include <string>

namespace VkHourglass::ApplicationDefines
{

constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 1024;

constexpr uint32_t GRID_WIDTH = 1024;
constexpr uint32_t GRID_HEIGHT = 1024;

constexpr uint32_t COMPUTE_LOCAL_GROUP_SIZE_X = 32;
constexpr uint32_t CELL_UPDATE_INTERVAL_MS = 0;
constexpr uint32_t ENABLE_HORIZONTAL_WRAPPING = false;
constexpr float STUCK_PROBABILITY = 0.25f;

namespace GenerateHourglass
{
constexpr uint32_t HOURGLASS_WIDTH = 300;
constexpr uint32_t HOURGLASS_HEIGHT = 1000;
constexpr uint32_t HOURGLASS_BORDER_WIDTH = 4;
constexpr uint32_t HOURGLASS_CENTER_WIDTH = 4;
constexpr float HOURGLASS_FILL_PERCENTAGE = 0.8f;
} // namespace GenerateHourglass

namespace GenerateCenterCircle
{
constexpr uint32_t RADIUS = 250;
}

namespace GenerateRandomCircles
{
constexpr uint32_t MIN_RADIUS = 10;
constexpr uint32_t MAX_RADIUS = 100;
constexpr uint32_t CIRCLE_COUNT = 50;
} // namespace GenerateRandomCircles

namespace GenerateRandom
{
constexpr uint32_t PARTICLE_COUNT = 1000000;
}

namespace NonModifiable
{
constexpr std::string_view COMPUTE_SHADER_NAME = "comp.spv";
constexpr std::string_view VERTEX_SHADER_NAME = "vert.spv";
constexpr std::string_view FRAGMENT_SHADER_NAME = "frag.spv";

constexpr uint32_t GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;
constexpr uint32_t ELEMENTS_PER_CELL = 4;
constexpr uint32_t X_DISPATCH_COUNT = GRID_SIZE / ELEMENTS_PER_CELL / COMPUTE_LOCAL_GROUP_SIZE_X;
} // namespace NonModifiable

} // namespace VkHourglass::ApplicationDefines

#endif // VULKANHOURGLASS_APPLICATIONDEFINES_HPP
