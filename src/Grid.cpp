#include "Grid.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <limits>
#include <random>

#include "ApplicationDefines.hpp"

namespace VkHourglass
{
using namespace ApplicationDefines;

static constexpr int32_t AIR_VALUE = 0;
static constexpr int32_t SAND_VALUE = 1;
static constexpr int32_t WALL_VALUE = 2;

static_assert(GRID_WIDTH >= 2 && GRID_HEIGHT >= 2);
// NOTE(MM): using uint32_t throughout application that holds 'GRID_SIZE * sizeof(uint32_t)'
static_assert(NonModifiable::GRID_SIZE < (std::numeric_limits<uint32_t>::max() / sizeof(uint32_t)));
static_assert(GRID_WIDTH < std::numeric_limits<int32_t>::max());
static_assert(GRID_HEIGHT < std::numeric_limits<int32_t>::max());
static_assert(GRID_WIDTH >= GenerateHourglass::HOURGLASS_WIDTH + GenerateHourglass::HOURGLASS_BORDER_WIDTH);
static_assert(GRID_HEIGHT >= GenerateHourglass::HOURGLASS_HEIGHT + GenerateHourglass::HOURGLASS_BORDER_WIDTH);
static_assert(GenerateHourglass::HOURGLASS_CENTER_WIDTH >= 2);
static_assert(GRID_WIDTH >= GenerateHourglass::HOURGLASS_CENTER_WIDTH + GenerateHourglass::HOURGLASS_BORDER_WIDTH);
static_assert(GRID_WIDTH % 2 == 0 && GenerateHourglass::HOURGLASS_WIDTH % 2 == 0);
static_assert(GRID_HEIGHT % 2 == 0 && GenerateHourglass::HOURGLASS_HEIGHT % 2 == 0);
static_assert((NonModifiable::GRID_SIZE / NonModifiable::ELEMENTS_PER_CELL) % COMPUTE_LOCAL_GROUP_SIZE_X == 0);
static_assert(GenerateCenterCircle::RADIUS < std::numeric_limits<int32_t>::max());
static_assert(GenerateRandomCircles::MIN_RADIUS < std::numeric_limits<int32_t>::max());
static_assert(GenerateRandomCircles::MAX_RADIUS < std::numeric_limits<int32_t>::max());

// NOTE(MM): Due to double-buffering and margolus neighborhood, sand can't be in the first row, as well as in the first
// column of the second and third row. Sand there would infinitely spawn new grains of sand, as they are only updated
// every 2nd iteration.
static void fixGridEdgeCases(std::vector<uint32_t>& grid)
{
    memset(grid.data(), 0, sizeof(uint32_t) * GRID_WIDTH);
    grid[GRID_WIDTH] = 0;

    if (GRID_HEIGHT > 2)
    {
        grid[GRID_WIDTH * 2] = 0;
    }
}

std::vector<uint32_t> generateHourglass(void)
{
    constexpr uint32_t startRow = (GRID_HEIGHT - GenerateHourglass::HOURGLASS_HEIGHT) / 2;
    constexpr uint32_t endRow = startRow + GenerateHourglass::HOURGLASS_HEIGHT;
    constexpr uint32_t halfHourglassHeight = GenerateHourglass::HOURGLASS_HEIGHT / 2;
    constexpr uint32_t upperCenterRow = startRow + halfHourglassHeight;
    constexpr uint32_t lowerCenterRow = upperCenterRow + 1u;

    constexpr uint32_t startColumn = (GRID_WIDTH - GenerateHourglass::HOURGLASS_WIDTH) / 2;
    constexpr uint32_t halfHourglassWidth = GenerateHourglass::HOURGLASS_WIDTH / 2;
    constexpr uint32_t leftCenterColumn = startColumn + halfHourglassWidth;
    constexpr uint32_t rightCenterColumn = leftCenterColumn + 1u;

    std::vector<uint32_t> grid(NonModifiable::GRID_SIZE, AIR_VALUE);

    // NOTE(MM): "Drawing" the hourglass from the center to top and bottom in lock-step (each iteration goes up and down
    // one row). The width at the center of the hourglass corresponds to the defined
    // GenerateHourglass::HOURGLASS_CENTER_WIDTH and will be increased to the full size with each row. Seemed to be the
    // most concise way to approach this.
    uint32_t currentWidth = GenerateHourglass::HOURGLASS_CENTER_WIDTH;
    for (uint32_t yUp = upperCenterRow + 1, yDown = lowerCenterRow; yUp-- > startRow && yDown < endRow; ++yDown)
    {
        const uint32_t currentHalfWidth = currentWidth / 2;

        const uint32_t leftBorderEnd = leftCenterColumn - currentHalfWidth;
        const uint32_t leftBorderBegin = leftBorderEnd - GenerateHourglass::HOURGLASS_BORDER_WIDTH;

        const uint32_t rightBorderBegin = rightCenterColumn + currentHalfWidth;
        const uint32_t rightBorderEnd = rightBorderBegin + GenerateHourglass::HOURGLASS_BORDER_WIDTH;

        const bool isTop = yUp <= startRow + GenerateHourglass::HOURGLASS_BORDER_WIDTH;
        const bool isFilled =
            yUp <= static_cast<uint32_t>(startRow + halfHourglassHeight * GenerateHourglass::HOURGLASS_FILL_PERCENTAGE);
        const bool isBottom = yDown >= endRow - GenerateHourglass::HOURGLASS_BORDER_WIDTH;

        for (uint32_t x = leftBorderBegin; x <= rightBorderEnd; ++x)
        {
            const uint32_t idxUp = x + GRID_WIDTH * yUp;
            const uint32_t idxDown = x + GRID_WIDTH * yDown;

            const bool isBorder = isTop || isBottom || x < leftBorderEnd || x > rightBorderBegin;
            if (isBorder)
            {
                grid[idxUp] = grid[idxDown] = WALL_VALUE;
            }
            else
            {
                grid[idxUp] = isFilled ? SAND_VALUE : AIR_VALUE;
                grid[idxDown] = AIR_VALUE;
            }
        }

        currentWidth = std::min(currentWidth + 1u, GenerateHourglass::HOURGLASS_WIDTH);
    }

    if (GenerateHourglass::HOURGLASS_BORDER_WIDTH == 0)
    {
        fixGridEdgeCases(grid);
    }

    return grid;
}

static void generateCircle(int32_t centerX, int32_t centerY, int32_t radius, std::vector<uint32_t>& grid)
{
    for (int y = centerY - radius; y <= centerY + radius; ++y)
    {
        for (int x = centerX - radius; x <= centerX + radius; ++x)
        {
            if (x < 0 || x >= static_cast<int32_t>(GRID_WIDTH) || y < 0 || y >= static_cast<int32_t>(GRID_HEIGHT))
            {
                continue;
            }

            int32_t dx = x - centerX;
            int32_t dy = y - centerY;

            if (dy * dy + dx * dx < radius * radius)
            {
                // NOTE(MM): No need to bound check here, as idx is in range:
                // [0, (GRID_HEIGHT - 1) * GRID_WIDTH + (GRID_WIDTH - 1) ]
                uint32_t idx = static_cast<uint32_t>(y) * GRID_WIDTH + static_cast<uint32_t>(x);
                grid[idx] = SAND_VALUE;
            }
        }
    }
}

std::vector<uint32_t> generateCenterCircle(void)
{
    std::vector<uint32_t> grid(NonModifiable::GRID_SIZE, AIR_VALUE);

    const int32_t centerX = GRID_WIDTH / 2;
    const int32_t centerY = GRID_HEIGHT / 2;

    generateCircle(centerX, centerY, GenerateCenterCircle::RADIUS, grid);

    fixGridEdgeCases(grid);
    return grid;
}

std::vector<uint32_t> generateRandomCircles(void)
{
    std::vector<uint32_t> grid(NonModifiable::GRID_SIZE, AIR_VALUE);

    std::default_random_engine rndEngine((unsigned)time(nullptr));
    std::uniform_int_distribution<int32_t> radiusDist(GenerateRandomCircles::MIN_RADIUS,
                                                      GenerateRandomCircles::MAX_RADIUS);
    std::uniform_int_distribution<int32_t> xDist(0, GRID_WIDTH);
    std::uniform_int_distribution<int32_t> yDist(0, GRID_HEIGHT);

    for (size_t i = 0; i < GenerateRandomCircles::CIRCLE_COUNT; ++i)
    {
        const int32_t centerX = xDist(rndEngine);
        const int32_t centerY = yDist(rndEngine);
        const int32_t radius = radiusDist(rndEngine);

        generateCircle(centerX, centerY, radius, grid);
    }

    fixGridEdgeCases(grid);
    return grid;
}

std::vector<uint32_t> generateRandomNoise(void)
{
    std::default_random_engine rndEngine((unsigned)time(nullptr));
    std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

    std::vector<uint32_t> grid(NonModifiable::GRID_SIZE, AIR_VALUE);

    for (size_t i = 0; i < GenerateRandom::PARTICLE_COUNT; ++i)
    {
        size_t idx = static_cast<size_t>(NonModifiable::GRID_SIZE * rndDist(rndEngine));
        idx = idx % grid.size(); // NOTE(MM): in case random value would be 1.0f
        grid[idx] = SAND_VALUE;
    }

    fixGridEdgeCases(grid);
    return grid;
}

} // namespace VkHourglass
