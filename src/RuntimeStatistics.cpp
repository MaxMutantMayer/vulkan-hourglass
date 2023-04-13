#include "RuntimeStatistics.hpp"

#include <algorithm>
#include <limits>

namespace VkHourglass
{

RuntimeStatistics::RuntimeStatistics()
    : _runtimeStart(std::chrono::steady_clock::now())
    , _longestFrameTime(std::numeric_limits<long>::min())
    , _shortestFrameTime(std::numeric_limits<long>::max())
    , _frameCount(0)
    , _isFirstFrame(true)
{
}

void RuntimeStatistics::notifyFrameBegin(void)
{
    ++_frameCount;

    const auto previousStart = _previousFrameStart;
    const auto now = std::chrono::steady_clock::now();
    _previousFrameStart = now;

    if (_isFirstFrame)
    {
        _isFirstFrame = false;
        return;
    }

    const auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - previousStart);
    _shortestFrameTime = std::min(_shortestFrameTime, frameTime.count());
    _longestFrameTime = std::max(_longestFrameTime, frameTime.count());
}

void RuntimeStatistics::printResults(void) const
{
    const auto now = std::chrono::steady_clock::now();
    const auto runtime = std::chrono::duration_cast<std::chrono::milliseconds>(now - _runtimeStart);

    printf("Overall runtime: %zums\n", runtime.count());
    printf("Drawn Frames: %zu\n", _frameCount);

    const long averageFrameTime = runtime.count() / _frameCount;
    printf("Average frame time: %zums / %zu fps\n", averageFrameTime, 1000 / averageFrameTime);
    printf("Best frame time: %zums\n", _shortestFrameTime);
    printf("Worst frame time: %zums\n", _longestFrameTime);
}

} // namespace VkHourglass
