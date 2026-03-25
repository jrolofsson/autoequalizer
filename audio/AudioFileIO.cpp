#include "audio/AudioFileIO.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

#include <sndfile.h>

namespace autoequalizer::audio {

namespace {

constexpr std::size_t kIoChunkFrames = 16384U;

[[nodiscard]] std::string lowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return extension;
}

[[nodiscard]] int outputFormatFromExtension(
    const std::filesystem::path& path) {
  const std::string extension = lowerExtension(path);
  if (extension == ".wav") {
    return SF_FORMAT_WAV | SF_FORMAT_PCM_24;
  }
  if (extension == ".flac") {
    return SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
  }
  if ((extension == ".aif") || (extension == ".aiff")) {
    return SF_FORMAT_AIFF | SF_FORMAT_PCM_24;
  }

  return SF_FORMAT_WAV | SF_FORMAT_PCM_24;
}

}  // namespace

core::Result<AudioBuffer> AudioFileIO::read(const std::filesystem::path& path) {
  SF_INFO info{};
  SNDFILE* handle = sf_open(path.string().c_str(), SFM_READ, &info);
  if (handle == nullptr) {
    return core::Error{"audio_open_failed",
                       std::string("Failed to open input file: ") +
                           sf_strerror(nullptr)};
  }

  const std::size_t frameCount = static_cast<std::size_t>(info.frames);
  const std::size_t channelCount = static_cast<std::size_t>(info.channels);
  AudioBuffer buffer(info.samplerate, channelCount, frameCount);
  std::vector<float> interleaved(kIoChunkFrames * channelCount, 0.0F);

  std::size_t framesRead = 0U;
  while (framesRead < frameCount) {
    const std::size_t framesThisChunk =
        std::min(kIoChunkFrames, frameCount - framesRead);
    const sf_count_t chunkRead =
        sf_readf_float(handle, interleaved.data(),
                       static_cast<sf_count_t>(framesThisChunk));
    if (chunkRead != static_cast<sf_count_t>(framesThisChunk)) {
      sf_close(handle);
      return core::Error{"audio_read_failed",
                         "File could not be decoded completely."};
    }

    for (std::size_t frame = 0; frame < framesThisChunk; ++frame) {
      for (std::size_t channel = 0; channel < channelCount; ++channel) {
        buffer.channel(channel)[framesRead + frame] =
            interleaved[(frame * channelCount) + channel];
      }
    }
    framesRead += framesThisChunk;
  }

  sf_close(handle);

  return buffer;
}

core::Result<void> AudioFileIO::write(const std::filesystem::path& path,
                                      const AudioBuffer& buffer) {
  SF_INFO info{};
  info.samplerate = buffer.sampleRate();
  info.channels = static_cast<int>(buffer.channelCount());
  info.format = outputFormatFromExtension(path);

  SNDFILE* handle = sf_open(path.string().c_str(), SFM_WRITE, &info);
  if (handle == nullptr) {
    return core::Error{"audio_write_failed",
                       std::string("Failed to open output file: ") +
                           sf_strerror(nullptr)};
  }

  std::vector<float> interleaved(kIoChunkFrames * buffer.channelCount(), 0.0F);
  std::size_t framesWritten = 0U;
  while (framesWritten < buffer.frameCount()) {
    const std::size_t framesThisChunk =
        std::min(kIoChunkFrames, buffer.frameCount() - framesWritten);
    for (std::size_t frame = 0; frame < framesThisChunk; ++frame) {
      for (std::size_t channel = 0; channel < buffer.channelCount();
           ++channel) {
        interleaved[(frame * buffer.channelCount()) + channel] =
            buffer.channel(channel)[framesWritten + frame];
      }
    }

    const sf_count_t chunkWritten = sf_writef_float(
        handle, interleaved.data(), static_cast<sf_count_t>(framesThisChunk));
    if (chunkWritten != static_cast<sf_count_t>(framesThisChunk)) {
      sf_close(handle);
      return core::Error{"audio_write_failed",
                         "Output audio could not be written completely."};
    }

    framesWritten += framesThisChunk;
  }

  sf_write_sync(handle);
  sf_close(handle);

  return core::Result<void>{};
}

bool AudioFileIO::isSupportedExtension(const std::filesystem::path& path) {
  static constexpr std::array supported{".wav", ".flac", ".aif", ".aiff"};
  const std::string extension = lowerExtension(path);
  return std::find(supported.begin(), supported.end(), extension) !=
         supported.end();
}

std::vector<std::filesystem::path> AudioFileIO::collectSupportedFiles(
    const std::filesystem::path& inputPath) {
  std::vector<std::filesystem::path> files;

  if (std::filesystem::is_regular_file(inputPath)) {
    if (isSupportedExtension(inputPath)) {
      files.push_back(inputPath);
    }
    return files;
  }

  if (!std::filesystem::is_directory(inputPath)) {
    return files;
  }

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(inputPath)) {
    if (entry.is_regular_file() && isSupportedExtension(entry.path())) {
      files.push_back(entry.path());
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace autoequalizer::audio
