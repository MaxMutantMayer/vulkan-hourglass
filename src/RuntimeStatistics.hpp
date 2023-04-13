#ifndef VULKANHOURGLASS_RUNTIMESTATISTICS_HPP
#define VULKANHOURGLASS_RUNTIMESTATISTICS_HPP

#include <chrono>
#include <cstdio>

namespace VkHourglass
{

class RuntimeStatistics
{
public:
    RuntimeStatistics();

    void notifyFrameBegin(void);
    void printResults(void) const;

private:
    std::chrono::steady_clock::time_point _runtimeStart;
    std::chrono::steady_clock::time_point _previousFrameStart;
    long _longestFrameTime;
    long _shortestFrameTime;
    long _frameCount;
    bool _isFirstFrame;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_RUNTIMESTATISTICS_HPP
