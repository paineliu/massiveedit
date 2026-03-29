#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

#include "massiveedit/core/line_indexer.h"

using massiveedit::core::LineIndexer;

int main() {
  static const std::string source = "ab\ncde\nf";

  LineIndexer indexer;
  indexer.reset(static_cast<std::uint64_t>(source.size()));

  auto reader = [](std::uint64_t offset, std::size_t length) {
    if (offset >= source.size()) {
      return std::string{};
    }
    const std::size_t take = std::min<std::size_t>(length, source.size() - offset);
    return source.substr(static_cast<std::size_t>(offset), take);
  };

  assert(indexer.ensureOffsetIndexed(0, reader, 2));
  assert(indexer.lineIndexForOffset(0) == 0);

  assert(indexer.ensureOffsetIndexed(6, reader, 2));
  assert(indexer.lineIndexForOffset(6) == 1);

  assert(indexer.ensureOffsetIndexed(7, reader, 2));
  assert(indexer.lineIndexForOffset(7) == 2);

  assert(indexer.ensureOffsetIndexed(static_cast<std::uint64_t>(source.size()), reader, 2));
  assert(indexer.lineIndexForOffset(static_cast<std::uint64_t>(source.size())) == 2);

  static const std::string one_line = "abcdefghijklmnopqrstuvwxyz";
  LineIndexer chunked;
  chunked.reset(static_cast<std::uint64_t>(one_line.size()));
  auto chunk_reader = [](std::uint64_t offset, std::size_t length) {
    if (offset >= one_line.size()) {
      return std::string{};
    }
    const std::size_t take = std::min<std::size_t>(length, one_line.size() - offset);
    return one_line.substr(static_cast<std::size_t>(offset), take);
  };

  assert(chunked.indexNextChunk(chunk_reader, 5));
  assert(!chunked.isComplete());
  assert(chunked.knownLineCount() == 1);
  assert(chunked.lineIndexForOffset(4) == 0);

  while (chunked.indexNextChunk(chunk_reader, 5)) {
  }
  assert(chunked.isComplete());
  assert(chunked.knownLineCount() == 1);
  assert(chunked.lineIndexForOffset(static_cast<std::uint64_t>(one_line.size())) == 0);

  return 0;
}
