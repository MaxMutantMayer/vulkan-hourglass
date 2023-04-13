#ifndef VULKANHOURGLASS_FILEREADING_HPP
#define VULKANHOURGLASS_FILEREADING_HPP

#include <filesystem>
#include <optional>
#include <vector>

namespace VkHourglass::FileReading
{

std::optional<std::vector<char>> readFile(const std::filesystem::path& filePath, std::ios_base::openmode openMode);

} // namespace VkHourglass::FileReading

#endif // VULKANHOURGLASS_FILEREADING_HPP
