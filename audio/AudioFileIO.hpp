#pragma once

#include <filesystem>
#include <vector>

#include "audio/AudioBuffer.hpp"
#include "core/Result.hpp"

namespace autoequalizer::audio {

class AudioFileIO {
 public:
  static core::Result<AudioBuffer> read(const std::filesystem::path& path);
  static core::Result<void> write(const std::filesystem::path& path,
                                  const AudioBuffer& buffer);

  [[nodiscard]] static bool isSupportedExtension(
      const std::filesystem::path& path);
  [[nodiscard]] static std::vector<std::filesystem::path> collectSupportedFiles(
      const std::filesystem::path& inputPath);
};

}  // namespace autoequalizer::audio

