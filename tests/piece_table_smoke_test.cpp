#include <cassert>
#include <string>

#include "massiveedit/core/piece_table.h"

using massiveedit::core::PieceTable;

int main() {
  PieceTable table;
  table.load("hello\nworld");

  table.insert(5, ", codex");
  assert(table.toString() == "hello, codex\nworld");

  table.erase(5, 7);
  assert(table.toString() == "hello\nworld");

  const std::string read = table.read(6, 5);
  assert(read == "world");

  return 0;
}

