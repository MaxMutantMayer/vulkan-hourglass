#include "FileReading.hpp"

#include <fstream>

namespace VkHourglass::FileReading
{

std::optional<std::vector<char>> readFile(const std::filesystem::path& filePath, std::ios_base::openmode openMode)
{
    std::ifstream file(filePath, openMode);

    if (!file)
    {
        fprintf(stderr, "Failed to read file at path: %s\n", filePath.c_str());
        return std::nullopt;
    }

    const long fileSize = file.tellg();
    if (fileSize < 0)
    {
        fprintf(stderr, "Failed to read size of file at path: %s\n", filePath.c_str());
        return std::nullopt;
    }

    std::vector<char> buffer(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(buffer.data(), fileSize);

    return buffer;
}

} // namespace VkHourglass::FileReading
