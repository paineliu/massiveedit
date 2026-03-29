#include "massiveedit/core/file/large_file_backend.h"

#include <algorithm>

namespace massiveedit::core::file {

bool LargeFileBackend::open(const std::filesystem::path& path, std::string* error) {
  close();

  stream_.open(path, std::ios::binary);
  if (!stream_.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open file.";
    }
    return false;
  }

  stream_.seekg(0, std::ios::end);
  size_ = static_cast<std::uint64_t>(stream_.tellg());
  stream_.seekg(0, std::ios::beg);
  path_ = path;
  return true;
}

void LargeFileBackend::close() {
  std::lock_guard<std::mutex> lock(stream_mutex_);
  if (stream_.is_open()) {
    stream_.close();
  }
  size_ = 0;
  path_.clear();
}

bool LargeFileBackend::isOpen() const {
  return stream_.is_open();
}

std::uint64_t LargeFileBackend::size() const {
  return size_;
}

std::string LargeFileBackend::read(std::uint64_t offset, std::size_t length) const {
  std::lock_guard<std::mutex> lock(stream_mutex_);
  if (!stream_.is_open() || offset >= size_) {
    return {};
  }

  const std::uint64_t bytes_available = size_ - offset;
  const std::size_t bytes_to_read =
      static_cast<std::size_t>(std::min<std::uint64_t>(bytes_available, length));

  std::string output(bytes_to_read, '\0');
  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  stream_.read(output.data(), static_cast<std::streamsize>(bytes_to_read));
  output.resize(static_cast<std::size_t>(stream_.gcount()));
  return output;
}

const std::filesystem::path& LargeFileBackend::path() const {
  return path_;
}

}  // namespace massiveedit::core::file
