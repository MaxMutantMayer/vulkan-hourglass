#ifndef VULKANHOURGLASS_COMPUTEUPDATETIMER_HPP
#define VULKANHOURGLASS_COMPUTEUPDATETIMER_HPP

#include <chrono>
#include <cstdio>

namespace VkHourglass
{

class ComputeUpdateTimer
{
public:
    ComputeUpdateTimer(size_t updateIntervalMs);

    bool isUpdateNeeded(void) const;
    void notifyUpdateScheduled(void);

private:
    const std::chrono::milliseconds _updateIntervalMs;
    std::chrono::steady_clock::time_point _latestUpdate;
};

} // namespace VkHourglass

#endif // VULKANHOURGLASS_COMPUTEUPDATETIMER_HPP
