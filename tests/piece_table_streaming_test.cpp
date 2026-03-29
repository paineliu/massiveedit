#include <cassert>
#include <algorithm>
#include <cstdint>
#include <string>

#include "massiveedit/core/piece_table.h"

using massiveedit::core::PieceTable;

int main() {
  PieceTable table;
  table.loadFromOriginalSize(11);  // "hello\nworld"

  auto reader = [](std::uint64_t offset, std::size_t length) {
    static const std::string source = "hello\nworld";
    if (offset >= source.size()) {
      return std::string{};
    }
    const std::size_t take = std::min<std::size_t>(length, source.size() - offset);
    return source.substr(static_cast<std::size_t>(offset), take);
  };

  assert(table.toString(reader) == "hello\nworld");
  table.insert(5, ", codex");
  assert(table.toString(reader) == "hello, codex\nworld");

  table.erase(5, 7);
  assert(table.toString(reader) == "hello\nworld");

  return 0;
}
