#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace massiveedit::core {

class LineIndexer {
 public:
  using Reader = std::function<std::string(std::uint64_t, std::size_t)>;

  void reset(std::uint64_t document_size);
  bool ensureLineIndexed(std::size_t line_index,
                         const Reader& reader,
                         std::size_t chunk_size = 256ULL * 1024);
  bool ensureOffsetIndexed(std::uint64_t offset,
                           const Reader& reader,
                           std::size_t chunk_size = 256ULL * 1024);
  bool indexNextChunk(const Reader& reader, std::size_t chunk_size = 256ULL * 1024);
  void ensureComplete(const Reader& reader, std::size_t chunk_size = 256ULL * 1024);

  [[nodiscard]] bool isComplete() const;
  [[nodiscard]] std::size_t knownLineCount() const;
  [[nodiscard]] std::size_t estimatedLineCount() const;
  [[nodiscard]] std::uint64_t lineStart(std::size_t line) const;
  [[nodiscard]] std::size_t lineIndexForOffset(std::uint64_t offset) const;

 private:
  std::vector<std::uint64_t> line_starts_;
  std::uint64_t document_size_ = 0;
  std::uint64_t scan_offset_ = 0;
  bool complete_ = true;
};

}  // namespace massiveedit::core
