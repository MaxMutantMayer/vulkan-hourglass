#include "ComputeUpdateTimer.hpp"

namespace VkHourglass
{

ComputeUpdateTimer::ComputeUpdateTimer(size_t updateIntervalMs)
    : _updateIntervalMs(updateIntervalMs)
    , _latestUpdate(std::chrono::steady_clock::now())
{
}

bool ComputeUpdateTimer::isUpdateNeeded(void) const
{
    const auto now = std::chrono::steady_clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - _latestUpdate);
    return diff > _updateIntervalMs;
}

void ComputeUpdateTimer::notifyUpdateScheduled(void)
{
    _latestUpdate = std::chrono::steady_clock::now();
}

} // namespace VkHourglass
