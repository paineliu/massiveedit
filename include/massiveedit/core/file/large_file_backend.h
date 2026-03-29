#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace massiveedit::core::file {

class LargeFileBackend {
 public:
  bool open(const std::filesystem::path& path, std::string* error = nullptr);
  void close();

  [[nodiscard]] bool isOpen() const;
  [[nodiscard]] std::uint64_t size() const;
  [[nodiscard]] std::string read(std::uint64_t offset, std::size_t length) const;
  [[nodiscard]] const std::filesystem::path& path() const;

 private:
  std::filesystem::path path_;
  std::uint64_t size_ = 0;
  mutable std::ifstream stream_;
  mutable std::mutex stream_mutex_;
};

}  // namespace massiveedit::core::file

